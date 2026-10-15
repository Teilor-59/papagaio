#pragma once

#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "papagaio/AnalisadorDeEspectro.h"
#include "papagaio/AnalisadorDuplo.h"
#include "papagaio/DetectorMicrofonia.h"
#include "papagaio/LeitorWav.h"
#include "papagaio/MedidorDeNivel.h"
#include "papagaio/MonitorDeDrift.h"

// O que a tela precisa saber agora. Copiado inteiro a cada leitura.
struct Instantaneo {
    bool temSinal = false;          // já recebeu áudio da fonte atual
    bool fimDoArquivo = false;
    double tempo = 0;               // segundos de áudio analisados na fonte atual
    double hzPorBin = 0;
    float picoMicDb = -120.0f;
    bool usandoLinhas = false;
    papagaio::Suspeita pior;
    std::vector<papagaio::Suspeita> suspeitas;
    std::vector<float> espectroMic, espectroLinhas;

    // níveis do mic nas três ponderações, sem calibração (seno em fundo de escala = 0 dB)
    std::array<papagaio::LeituraDeNivel, papagaio::kNumPonderacoes> niveis;
    double segundosDeNivel = 0;
    bool nivelLento = false;

    // a calibração do mic; quem preenche é a tela, para todos os painéis mostrarem nível na mesma unidade
    bool calibrado = false;
    double calibracaoDb = 0;

    juce::Time inicioDaFonte;       // para dar hora a qualquer instante do passado
    bool deArquivo = false;

    // espectro em 1/48 de oitava, com a média escolhida: a base do RTA
    std::vector<float> bandas48;

    // mapa de risco do ring-out: as frequências vigiadas com prioridade
    papagaio::MapaDeRisco mapa;
    int revisaoDoMapa = 0;
    bool gravandoRingOut = false;
    double inicioDoRingOut = 0;     // segundos da fonte

    bool temLr = false;             // há L/R da mesa: o módulo de drift está medindo
    bool atrasoTravado = false;
    double limiarDeDriftDb = 2.5;
    papagaio::EstadoDoDrift drift;
    bool temBloco = false;          // última medição mic × L/R por banda de 1/3 de oitava
    papagaio::BlocoDeBandas bloco;
};

// Uma linha do histórico: um alerta de microfonia ou uma mudança sustentada de sala ou de mix.
// Ao vivo vale a hora do relógio; em arquivo, a posição na gravação.
struct Registro {
    bool deDrift = false;
    papagaio::Evento evento;
    papagaio::EventoDeDrift drift;
    double tempo = 0;               // segundos desde o começo da fonte
    juce::Time hora;
    bool deArquivo = false;
};

// Como simular com um arquivo no lugar da placa.
struct Simulacao {
    double velocidade = 1.0;        // > 1 acelera
    double pausarEm = 0.0;          // > 0 congela a análise nesse ponto do arquivo
    double passagemAte = 0.0;       // > 0: o começo do arquivo é a passagem de som, e o show começa em seguida
    bool janelasCurtas = false;     // drift com janelas de segundos, para os sinais de teste
};

// Liga o áudio aos módulos de análise. A placa (ou um WAV, para simular) alimenta uma thread
// de análise, que é a única dona do motor; a tela só lê cópias.
class MotorAoVivo : private juce::AudioIODeviceCallback, private juce::Thread {
public:
    MotorAoVivo();
    ~MotorAoVivo() override;

    juce::AudioDeviceManager& dispositivos() { return dispositivos_; }

    // Abre a placa. Devolve o erro do sistema, ou vazio se deu certo.
    juce::String iniciar(const juce::XmlElement* estadoSalvo);

    bool abrirArquivo(const juce::File& arquivo, juce::String& erro, const Simulacao& simulacao = {});
    void voltarAoVivo();
    bool emArquivo() const { return emArquivo_.load(); }

    juce::StringArray nomesDosCanais() const;
    juce::String descricaoDaFonte() const;
    // Canais a partir de 0; -1 quer dizer nenhum. O L/R pode ser um canal só ou um par.
    void escolherCanais(int mic, int linhas, int lrA, int lrB);
    int canalMic() const { return canalMic_.load(); }
    int canalLinhas() const { return canalLinhas_.load(); }
    int canalLrA() const { return canalLrA_.load(); }
    int canalLrB() const { return canalLrB_.load(); }

    // Drift: a passagem de som grava a linha de base da sala; o começo do show marca a referência do mix.
    void iniciarPassagem() { pedido_ = Pedido::iniciarPassagem; }
    void fecharPassagem() { pedido_ = Pedido::fecharPassagem; }
    void marcarInicioDoShow() { pedido_ = Pedido::inicioDoShow; }

    // Ring-out: enquanto grava, cada apito, ressonância e pré-apito entra no mapa de risco. O mapa é da
    // passagem de som, não da fonte: sobrevive à troca de placa ou de arquivo, e a tela pode trocá-lo inteiro.
    void iniciarRingOut() { pedidoDeRingOut_ = 1; }
    void encerrarRingOut() { pedidoDeRingOut_ = 2; }
    void definirMapa(const papagaio::MapaDeRisco& mapa);

    // Enquanto o mic é calibrado, o tom do calibrador é um seno puro e parado: para o detector, um apito.
    // Com a detecção em pausa ele não vira alerta nem linha no histórico.
    void pausarDeteccao(bool pausar) { deteccaoEmPausa_ = pausar; }

    // Níveis e vistas.
    void zerarNiveis() { zerarNiveis_ = true; }
    void definirNivelLento(bool lento) { nivelLento_ = lento; }
    void definirMediaDoEspectro(double segundos) { mediaDoEspectro_ = segundos; }

    // Espectrograma: uma linha de AnalisadorDeEspectro::kNumBandas bytes por quadro, de 0 (-120 dB) a 255 (0 dB).
    // O motor guarda as últimas três horas, para a tela poder voltar no tempo. As linhas são numeradas desde o
    // começo da fonte; a geração muda quando a fonte recomeça e a numeração volta a zero.
    struct Espectrograma {
        int geracao = 0;
        long long primeira = 0, total = 0;      // estão guardadas as linhas de primeira até total - 1
    };
    Espectrograma espectrograma() const;
    // Copia até `quantas` linhas a partir de `primeira`, as que existirem, e ajusta `primeira` para a que veio de fato.
    int lerEspectrograma(long long& primeira, int quantas, std::vector<juce::uint8>& linhas) const;
    double linhasPorSegundo() const { return linhasPorSegundo_.load(); }

    Instantaneo instantaneo() const;
    std::vector<Registro> historico() const;
    int revisaoDoHistorico() const { return revisao_.load(); }
    void limparHistorico();

private:
    enum class Pedido { nenhum, iniciarPassagem, fecharPassagem, inicioDoShow };

    void audioDeviceIOCallbackWithContext(const float* const* entradas, int numEntradas, float* const* saidas, int numSaidas,
                                          int numAmostras, const juce::AudioIODeviceCallbackContext& contexto) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* dispositivo) override;
    void audioDeviceStopped() override;
    void run() override;

    void preparar(double taxa);
    int analisarFila();
    int analisarArquivo();
    void analisar(int numAmostras);
    void publicar(int numAmostras);
    bool temLr() const { return canalLrA_.load() >= 0 || canalLrB_.load() >= 0; }

    juce::AudioDeviceManager dispositivos_;

    // placa -> thread de análise
    juce::AbstractFifo fila_ { 1 << 16 };
    std::vector<float> filaMic_, filaLinhas_, filaLr_;
    std::atomic<int> canalMic_ { 0 }, canalLinhas_ { -1 }, canalLrA_ { -1 }, canalLrB_ { -1 };
    std::atomic<double> taxaDaPlaca_ { 0.0 };
    std::atomic<bool> reiniciar_ { false }, emArquivo_ { false };
    std::atomic<int> revisao_ { 0 };
    std::atomic<Pedido> pedido_ { Pedido::nenhum };
    std::atomic<bool> zerarNiveis_ { false }, nivelLento_ { false }, deteccaoEmPausa_ { false };
    std::atomic<double> mediaDoEspectro_ { 0.0 }, linhasPorSegundo_ { 23.4 };
    std::atomic<int> pedidoDeRingOut_ { 0 };        // 1 começa, 2 encerra
    std::atomic<bool> temMapaPendente_ { false };

    // só a thread de análise mexe
    papagaio::DetectorMicrofonia detector_;
    papagaio::AnalisadorDuplo analisador_;
    papagaio::MonitorDeDrift monitor_;
    papagaio::MedidorDeNivel medidor_;
    papagaio::AnalisadorDeEspectro espectro_;
    papagaio::BlocoDeBandas ultimoBloco_;
    bool temBloco_ = false;
    papagaio::ConfigDrift configDrift_;
    double duracaoDoBloco_ = 10.0;
    std::vector<float> blocoMic_, blocoLinhas_, blocoLr_, silencio_, intercalado_;
    size_t eventosLidos_ = 0, driftLidos_ = 0;
    double taxa_ = 48000.0;
    juce::Time inicioDaFonte_;
    std::unique_ptr<papagaio::LeitorWav> leitor_;
    Simulacao simulacao_;
    double relogioDoArquivo_ = 0;
    long long lidosDoArquivo_ = 0;
    bool acabou_ = false, showMarcado_ = false, lrAtivo_ = false;
    double inicioDoLr_ = 0;         // quando o L/R apareceu, no relógio da fonte

    // compartilhado com a tela
    mutable juce::CriticalSection trava_;
    Instantaneo instantaneo_;
    std::vector<Registro> historico_;
    std::unique_ptr<papagaio::LeitorWav> leitorPendente_;
    papagaio::MapaDeRisco mapaPendente_;
    Simulacao simulacaoPendente_;
    juce::String nomeDoArquivo_;
    int canaisDoArquivo_ = 0;
    float pico_ = 0.0f;
    // espectrograma em pedaços de 1024 linhas (44 s), alocados conforme o show anda
    static constexpr int kLinhasPorPedaco = 1024, kMaxPedacos = 248;
    std::deque<std::vector<juce::uint8>> pedacos_;
    long long primeiroPedaco_ = 0, linhasEscritas_ = 0;
    int geracao_ = 0;
};
