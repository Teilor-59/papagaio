#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Blocos.h"
#include "Mesa.h"
#include "MotorAoVivo.h"

// A janela do app: em cima, a fonte de áudio e os canais; embaixo, a mesa, onde o engenheiro
// arruma os painéis do jeito dele.
class TelaPrincipal : public juce::Component, private juce::Timer, private juce::ChangeListener {
public:
    struct Opcoes {
        juce::File arquivo;         // abre já simulando com esse WAV
        int linhas = -1;            // canais do arquivo, a partir de 0; -1 = nenhum
        int lr = -1;
        double passagemAte = 0;     // o começo do arquivo, até aqui, é a passagem de som
        bool janelasCurtas = false; // drift com janelas de segundos, para os sinais de teste

        // Captura de tela, para desenvolvimento. Não lê nem grava as preferências do usuário.
        juce::File captura;         // grava a tela em PNG e sai...
        double apos = 0;            // ...quando a análise chegar a esse ponto do arquivo
        juce::String vistas;        // "rta,espectrograma": as vistas do layout padrão
        int fracao = 0;             // bandas do RTA por oitava
        juce::File layout;          // um layout inteiro, no XML que a mesa guarda
        bool editando = false;      // fotografa no modo de edição do layout
        bool temCalibracao = false; // níveis em dB SPL
        double calibracao = 0;
        bool capturarCalibracao = false;    // fotografa a janela de calibração, não a tela principal
        double rever = -1;          // leva o espectrograma para esse ponto do arquivo
        juce::String referencia;    // curva de referência do RTA, no formato de CurvaDeReferencia
        juce::String equilibrio;    // referência da vista de equilíbrio, no formato de ReferenciaDeEquilibrio
        juce::String mapa;          // mapa de risco, no formato de MapaDeRisco::emTexto ("2500;1250" também vale)
        bool ringOut = false;       // começa gravando o ring-out
    };

    explicit TelaPrincipal(const Opcoes& opcoes);
    ~TelaPrincipal() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    bool capturando() const { return opcoes_.captura != juce::File(); }
    juce::PropertiesFile* preferencias() { return propriedades_.getUserSettings(); }

    void montarMesa();
    void menuDoLayout();
    void editarLayout(bool editando);
    void mostrarNoEspectrograma(double segundos);

    void atualizarCanais();
    void canaisEscolhidos();
    juce::String chaveDeCalibracao();
    void carregarCalibracao();
    void definirCalibracao(bool tem, double db);
    void abrirCalibracao();
    void abrirConfiguracaoDeAudio();
    void alternarArquivo();
    void abrirArquivo(const juce::File& arquivo, const Simulacao& simulacao, int linhas, int lr);
    void salvarAudio();
    void capturar();

    Opcoes opcoes_;
    juce::ApplicationProperties propriedades_;
    MotorAoVivo motor_;

    juce::Label fonte_, rotuloMic_, rotuloLinhas_, rotuloLr_;
    juce::Image fundo_, logo_;          // prontos no tamanho da janela, para não redesenhar a cada quadro
    juce::Rectangle<int> areaDoLogo_;
    juce::TooltipWindow dicas_ { this, 600 };
    juce::TextButton botaoAudio_, botaoArquivo_, botaoLayout_;
    juce::ComboBox comboMic_, comboLinhas_, comboLrA_, comboLrB_;
    Medidor medidor_;
    std::unique_ptr<Mesa> mesa_;

    std::vector<Registro> historico_;
    int revisao_ = -1;
    int media_ = 2;                 // média do RTA, de 1 a 4
    Escala escala_;                 // calibração do canal do mic em uso
    CurvaDeReferencia referencia_;  // a curva traçada por cima do RTA
    ReferenciaDeEquilibrio equilibrio_;     // nível e espectro do momento em que o equilíbrio estava bom
    juce::StringArray canais_;
    juce::String erroDeAudio_;
    std::unique_ptr<juce::FileChooser> seletor_;
    bool capturou_ = false;
    bool ofereceuCalibracao_ = false;   // a janela de calibração abre sozinha uma vez, se o mic não estiver calibrado
    int esperaDaCaptura_ = -1;
    int revisaoDoMapa_ = -1;        // a última revisão do mapa de risco guardada nas preferências
};
