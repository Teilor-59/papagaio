#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "papagaio/Fft.h"
#include "papagaio/MapaDeRisco.h"

namespace papagaio {

// Hierarquia de alertas da proposta: amarelo vai só para o histórico,
// laranja e vermelho interrompem a tela.
enum class Estado : int {
    nenhum = 0,
    amarelo = 1,     // frequência instável, pré-apito
    laranja = 2,     // ressonância tocando
    vermelho = 3,    // apito estabelecido, crescendo
};

struct ConfigMicrofonia {
    double pnprDb = 15.0;              // pico acima da vizinhança: apito é estreito
    double phprDb = 25.0;              // pico acima dos próprios harmônicos: apito não tem harmônicos
    double crescimentoDbPorS = 6.0;    // inclinação mínima para vermelho
    double pisoDb = -70.0;             // abaixo disso o pico é ignorado (dBFS)
    double fMin = 100.0;
    double fMax = 12000.0;
    bool usarLinhas = false;           // canais de linha como gabarito do que é música
    bool preApito = true;              // vigia o anel depois de cada ataque
    double anelLongoDbPorS = 60.0;     // queda mais lenta que isso, e alongando, é pré-apito
    int tamanhoFft = 4096;             // 4096 ou 8192 em 44,1/48 kHz; dobra acima de 60 kHz
};

// Pico suspeito que está sendo acompanhado agora.
struct Suspeita {
    double f = 0;
    double nivelDb = -200;
    double inclinacaoDbPorS = 0;
    double duracao = 0;
    Estado estado = Estado::nenhum;
    bool preApito = false;             // anel alongando: amarelo, nunca interrompe
    double decaiDbPorS = 0;            // pré-apito: a queda do anel agora
    bool risco = false;                // numa frequência do mapa de risco do ring-out
};

// Alerta que já terminou. É a linha do histórico e do relatório pós-show.
struct Evento {
    double tInicio = 0;        // segundos desde o começo, na primeira vez em amarelo
    double tLaranja = -1;      // -1 se não chegou lá
    double tVermelho = -1;
    double duracao = 0;
    double f = 0;
    double nivelMaxDb = -200;
    Estado estado = Estado::nenhum;
    int voltou = 0;            // quantas vezes essa frequência já tinha tocado antes (laranja ou vermelho)
    int vezes = 1;             // amarelo: quantos amarelos da mesma frequência, um logo depois do outro, viraram este

    // Pré-apito: o anel daquela frequência ficou mais longo a cada ataque. Estado é sempre amarelo.
    bool preApito = false;
    double decaiAntesDbPorS = 0;     // queda do anel antes de alongar (mediana de 3 ataques)
    double decaiMinDbPorS = 0;       // a queda mais lenta medida durante o alerta
    int ataques = 0;                 // quedas medidas durante o alerta

    bool ringOut = false;            // aconteceu durante o ring-out: alimenta o mapa, não é falha do show
    bool risco = false;              // numa frequência que já estava no mapa de risco
};

// Banda do equalizador gráfico de 1/3 de oitava mais perto de f, como vem escrito na mesa: "2.5k".
std::string bandaGeq(double f);
const char* nomeEstado(Estado e);

// Módulo 1 da proposta. Lê só áudio: o mic de medição e, quando houver, a soma das linhas.
//
// Critérios por pico do espectro (van Waterschoot e Moonen): estreito (PNPR), sem harmônicos
// (PHPR), parado em frequência, persistente entre quadros e crescendo em linha reta em dB.
// Sozinhos confundem apito com nota sustentada; as linhas desempatam.
//
// Pré-apito: depois de cada ataque, mede quanto um pico estreito e sem harmônicos leva para cair.
// Com ganho de malha g, o anel perde 20·log10(1/g) dB a cada volta: perto do limite ele cai devagar,
// e cai cada vez mais devagar conforme o ganho sobe. Anel mais lento que anelLongoDbPorS, e 1,6 vez
// mais lento que nos ataques anteriores, vira amarelo de pré-apito.
//
// Mapa de risco: durante o ring-out da passagem, cada laranja, vermelho e pré-apito entra no mapa.
// No show, perto das frequências do mapa os critérios afrouxam: ali a malha já fechou uma vez.
class DetectorMicrofonia {
public:
    void preparar(double taxaDeAmostragem, const ConfigMicrofonia& config);
    void configurar(const ConfigMicrofonia& config);

    // linhas pode ser nullptr. Roda uma análise a cada salto de 1024 amostras (21 ms em 48 kHz).
    void processar(const float* mic, const float* linhas, int numAmostras);

    // Fecha os alertas ainda abertos, no fim de um arquivo.
    void encerrar();

    double tempo() const { return agora_; }
    double hzPorBin() const { return hzPorBin_; }
    const std::vector<float>& espectroMicDb() const { return magMic_; }
    const std::vector<float>& espectroLinhasDb() const { return magLinhas_; }
    const std::vector<Suspeita>& suspeitas() const { return suspeitas_; }
    const std::vector<Evento>& eventos() const { return eventos_; }
    const ConfigMicrofonia& config() const { return cfg_; }
    // O pior alerta aberto agora; estado nenhum se não houver.
    Suspeita pior() const;

    // Ring-out: enquanto grava, os alertas vão para o mapa (e para os eventos, marcados como ring-out).
    void iniciarRingOut();
    void encerrarRingOut();
    bool gravandoRingOut() const { return gravandoRingOut_; }
    double inicioDoRingOut() const { return inicioDoRingOut_; }
    // O mapa sobrevive a preparar(): é da passagem de som, não da fonte de áudio.
    void definirMapa(const MapaDeRisco& mapa);
    const MapaDeRisco& mapa() const { return mapa_; }
    int revisaoDoMapa() const { return revisaoDoMapa_; }

private:
    static constexpr int kTrilhas = 32;
    static constexpr int kHistorico = 64;

    struct Pico {
        double f, nivel;
        bool suspeito, usado;
        bool suspeitoEstrito;   // suspeito sem a folga do mapa de risco
        bool estreito;      // estreito e sem harmônicos, com critério mais frouxo: candidato a anel
    };

    // Uma frequência onde já apareceu pico estreito, vigiada a cada queda.
    static constexpr int kAneis = 64;
    static constexpr int kMedidas = 12;
    static constexpr int kPontosDaQueda = 96;     // 1,5 s de quadros, com folga
    struct Anel {
        bool ativo = false;
        double f = 0;
        double vistoEm = 0;             // último quadro com pico estreito aqui
        bool vistoAgora = false;
        double fVisto = 0;              // onde o pico estreito apareceu neste quadro
        // queda em andamento: nível contra o tempo desde o pico
        double pico = -200, tPico = 0, minimo = -200;
        double fMin = 0, fMax = 0;      // por onde o pico estreito andou durante a queda
        int n = 0, nEstreito = 0;
        std::array<float, kPontosDaQueda> x {}, y {};
        // quedas medidas, em dB/s, num anel circular
        std::array<double, kMedidas> taxa {}, quando {};
        int medidas = 0;
        // alerta de pré-apito
        bool alertando = false, risco = false;
        double tInicio = 0, taxaAntes = 0, taxaAgora = 0, taxaMin = 0, nivelMax = -200, ultimaMedida = 0;
        int ataques = 0;
    };

    struct Trilha {
        bool ativa = false;
        double f = 0, nivel = -200, nivelMax = -200, inclinacao = 0;
        double somaF = 0;
        int nF = 0;
        int idade = 0, suspeita = 0, ruim = 0, pos = 0;
        int estrita = 0;        // quadros de suspeita pelo critério sem a folga do mapa: é o que vale para vermelho
        bool risco = false;
        Estado estado = Estado::nenhum;
        double tInicio = 0, tLaranja = -1, tVermelho = -1;
        std::array<float, kHistorico> nivelHist {}, fHist {};
    };

    void atualizarLimites();
    void analisar();
    double espectro(const std::vector<float>& anel, std::vector<float>& magDb);
    void acharPicos();
    void seguirTrilha(Trilha& t);
    void abrirTrilha(const Pico& p);
    void fecharTrilha(Trilha& t);
    void guardar(Trilha& t);
    void soltarAmarelos(bool todos);
    void marcarRisco();
    bool emRisco(double f) const;
    void ajustarReta(const Trilha& t, int n, double& inclinacao, double& residuo) const;
    double passeio(const Trilha& t, int n) const;
    bool temFundamental(double kf, double nivel, int divisor) const;

    void vigiarAneis();
    void comecarQueda(Anel& a, double nivel);
    void fecharQueda(Anel& a);
    bool ajustarQueda(const Anel& a, double abaixoDe, double& taxa) const;
    void avaliarAnel(Anel& a);
    void fecharAnel(Anel& a);
    double nivelEm(double f) const;

    ConfigMicrofonia cfg_;
    double taxa_ = 48000.0, hzPorBin_ = 0, tempoDoSalto_ = 0, normDb_ = 0, agora_ = 0;
    double totalMicDb_ = -200, totalLinhasDb_ = -200;
    int n_ = 0, salto_ = 0, pos_ = 0, contador_ = 0;
    int kMin_ = 0, kMax_ = 0, janelaReta_ = 0, janelaParado_ = 0, borrao_ = 0, maxRuim_ = 0;
    long long amostras_ = 0;

    std::unique_ptr<Fft> fft_;
    std::vector<float> anelMic_, anelLinhas_, janela_, re_, im_, magMic_, magLinhas_;
    std::vector<float> linhasRetidas_;     // espectro das linhas com retenção de pico, para cobrir atraso e sala
    std::vector<Pico> picos_;
    std::array<Trilha, kTrilhas> trilhas_;
    std::array<Anel, kAneis> aneis_;
    std::vector<Suspeita> suspeitas_;
    std::vector<Evento> eventos_;
    std::vector<Evento> amarelosPendentes_;

    MapaDeRisco mapa_;
    std::vector<char> risco_;              // por bin: perto de uma frequência do mapa
    bool gravandoRingOut_ = false;
    double inicioDoRingOut_ = -1, fimDoRingOut_ = -1;
    bool doRingOut(double tInicio) const;
    int revisaoDoMapa_ = 0;    // amarelo comum espera um pouco para juntar as repetições
};

} // namespace papagaio
