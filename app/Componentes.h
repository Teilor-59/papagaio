#pragma once

#include <array>
#include <functional>

#include "MotorAoVivo.h"
#include "Visual.h"

inline Escala escalaDe(const Instantaneo& i) { return { i.calibrado, i.calibracaoDb }; }

// A faixa do alerta: lida em um segundo, com o olho no palco. Laranja e vermelho tomam
// a faixa inteira; o resto fica discreto.
class FaixaDeAlerta : public juce::Component {
public:
    struct Dados {
        papagaio::Suspeita pior;
        int voltou = 0;                 // quantas vezes essa frequência já apareceu no histórico
        int vermelhos = 0, laranjas = 0, amarelos = 0, drifts = 0;
        bool temSinal = false;
        bool fimDoArquivo = false;
        bool gravandoRingOut = false;
        double segundosDeRingOut = 0;
        Escala escala;
    };
    void mostrar(const Dados& d);
    void paint(juce::Graphics& g) override;

private:
    Dados d_;
};

// Curva de referência do RTA: a linha que o engenheiro traça para comparar com o que está tocando.
// Os níveis ficam na unidade da tela (dB SPL com o mic calibrado), para a curva valer de um show para o outro.
struct CurvaDeReferencia {
    struct Ponto {
        double f = 1000.0, db = 0.0;
    };
    std::vector<Ponto> pontos;      // em ordem de frequência
    bool visivel = true;
    int fracao = 3;                 // largura de banda em que foi traçada: banda mais estreita lê mais baixo
    std::function<void()> aoMudar;  // para a tela guardar

    double em(double f) const;      // nível da curva em f; antes do primeiro ponto e depois do último, fica plana
    juce::String paraTexto() const; // "3|1|63:98.5;1000:95;8000:88"
    void deTexto(const juce::String& texto);
};

// O momento em que o equilíbrio estava do gosto do engenheiro: nível geral e espectro em 1/3 de oitava,
// em dB SPL. A vista de equilíbrio pelo volume compara o agora com isto.
struct ReferenciaDeEquilibrio {
    bool tem = false;
    double nivelZ = 0;
    std::array<double, papagaio::kNumBandas> bandas {};
    std::function<void()> aoMudar;

    juce::String paraTexto() const;     // "98.2;80.1;82.4;..."
    void deTexto(const juce::String& texto);
};

// Uma vista do sinal, à escolha, como no Smaart. Todas usam o mesmo eixo de frequência,
// de 20 Hz a 20 kHz, e marcam as frequências suspeitas do detector.
class Vista : public juce::Component {
public:
    enum class Tipo {
        nenhuma,
        espectro,          // linha fina: o que o detector de microfonia enxerga
        rta,               // barras por fração de oitava, com a média escolhida
        espectrograma,     // frequência na horizontal, tempo descendo, nível na cor
        transferencia,     // mic ÷ L/R por 1/3 de oitava, com a coerência embaixo
        equilibrio,        // quanto cada região soa mais ou menos presente por causa do volume (curvas de phon)
    };
    static juce::StringArray nomes();      // na ordem do enum

    void definirTipo(Tipo tipo);
    Tipo tipo() const { return tipo_; }
    void definirFracao(int porOitava);     // bandas do RTA: 1, 3, 6, 12, 24 ou 48
    // Retenção de pico do RTA: 0 desliga, kPicoSegurado segura até zerar, e um número de segundos
    // segura cada banda esse tempo e depois deixa cair até o nível de agora.
    static constexpr int kPicoSegurado = -1;
    void definirPico(int segundos);
    int pico() const { return pico_; }
    void zerarPico();

    void mostrar(const Instantaneo& i, const MotorAoVivo& motor);
    void paint(juce::Graphics& g) override;
    void resized() override;

    // Espectrograma: leva a vista para um instante da fonte (segundos desde o começo) e para de acompanhar o agora.
    void irPara(double segundos);

    // RTA: a curva de referência é da tela; a vista desenha e deixa editar.
    void usarReferencia(CurvaDeReferencia* referencia) { referencia_ = referencia; }
    void usarEquilibrio(ReferenciaDeEquilibrio* equilibrio) { equilibrio_ = equilibrio; }

    Vista();

private:
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& roda) override;

    void atualizarEspectrograma(const MotorAoVivo& motor);
    void renderizar(int y0, int n);
    void andar(long long linhas);
    void limitar();

    // RTA e curva de referência
    double nivelDaBanda(int centro, int largura) const;
    float yDoRta(double db) const;
    double dbDoRta(float y) const;
    double deslocamentoDaReferencia() const;
    int pontoPerto(juce::Point<float> onde) const;
    void pintarReferencia(juce::Graphics& g);
    void menuDaReferencia();
    void capturarReferencia();
    void referenciaReta();

    float xDe(double f) const;
    void grade(juce::Graphics& g, float dbMin, float dbMax, float passoDb) const;
    void marcarSuspeitas(juce::Graphics& g) const;
    void pintarEspectro(juce::Graphics& g);
    void pintarRta(juce::Graphics& g);
    void atualizarPico();
    void pintarEspectrograma(juce::Graphics& g);
    void pintarTransferencia(juce::Graphics& g);
    void pintarEquilibrio(juce::Graphics& g);
    void suavizarEquilibrio();
    void marcarEquilibrio();
    void aviso(juce::Graphics& g, const juce::String& texto) const;

    Tipo tipo_ = Tipo::espectro;
    int fracao_ = 3;
    Instantaneo i_;
    juce::Rectangle<float> area_;

    // --- espectrograma ---
    static constexpr int kLinhasVisiveis = 512;     // 22 s de cima a baixo
    juce::Image imagem_;                // uma coluna por banda de 1/48; a linha mais nova em cima
    std::vector<juce::uint8> visiveis_; // os mesmos pontos em bytes, para ler o nível debaixo do cursor
    std::vector<juce::uint8> novas_;
    double linhasPorSegundo_ = 23.4;
    int geracao_ = -1;
    long long total_ = 0, primeira_ = 0;    // o que o motor tem guardado
    bool aoVivo_ = true;
    long long fim_ = 0;                 // revendo: a linha logo depois da mais nova mostrada
    long long fimDesenhado_ = -1;       // o que a imagem tem agora
    long long fimNoArrasto_ = 0;
    double pedidoDeIrPara_ = -1;        // segundos; atendido na próxima atualização
    bool temCursor_ = false;
    juce::Point<float> cursor_;
    juce::TextButton botaoMenosMinuto_, botaoMenos10_, botaoMais10_, botaoAoVivo_;

    // --- curva de referência do RTA ---
    CurvaDeReferencia* referencia_ = nullptr;

    // --- retenção de pico do RTA, por banda de 1/48 (só as que o RTA mostra), na escala do motor ---
    int pico_ = 0;
    std::vector<double> picoDb_, picoDesde_;     // nível segurado e o instante da fonte em que chegou nele
    double ultimoTempoDoPico_ = -1;

    // --- equilíbrio pelo volume ---
    ReferenciaDeEquilibrio* equilibrio_ = nullptr;
    juce::TextButton botaoMarcar_;
    bool temSuave_ = false;             // nível geral e bandas com média de uns 3 s, em potência, na escala do motor
    double nivelSuave_ = 0;
    std::array<double, papagaio::kNumBandas> bandasSuaves_ {};
    juce::TextButton botaoReferencia_;
    int arrastando_ = -1;               // índice do ponto, ou kLinhaToda
    static constexpr int kLinhaToda = -2;
    std::vector<CurvaDeReferencia::Ponto> pontosNoArrasto_;
};

// Níveis do mic em A, C e Z: agora, médio (Leq), máximo e mínimo, e o Leq(A) de 1, 5 e 15 min
// quando o painel tem altura. Em dB SPL com o mic calibrado.
class PainelDeNiveis : public juce::Component {
public:
    PainelDeNiveis();
    void mostrar(const Instantaneo& i);
    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::TextButton botaoZerar, botaoCalibrar;     // o de calibrar abre a janela de calibração
    juce::ComboBox comboTempo;

private:
    Instantaneo i_;
};

// Painel de sala e mix (módulos 2 e 3): quanto cada região mudou desde a passagem de som e desde
// o primeiro bloco do show. Fica na tela o tempo todo e nunca interrompe.
class PainelDeDrift : public juce::Component {
public:
    PainelDeDrift();
    void mostrar(const Instantaneo& i);
    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::TextButton botaoPassagem, botaoShow;

private:
    juce::String situacao() const;

    Instantaneo i_;
};

// Nível de entrada do mic, para confirmar que o sinal chega.
class Medidor : public juce::Component {
public:
    void mostrar(float picoDb);
    void paint(juce::Graphics& g) override;

private:
    float db_ = -120.0f;
};
