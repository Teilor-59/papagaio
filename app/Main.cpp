#include <juce_gui_extra/juce_gui_extra.h>

#include "Aparencia.h"
#include "TelaPrincipal.h"

class PapagaioApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    // Uma instância por engenheiro, mas nada impede FOH e monitor no mesmo computador.
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        juce::LookAndFeel::setDefaultLookAndFeel(&visual_);

        // --exportar-icone arquivo.png: grava o papagaio recortado do logotipo e sai. É com esse arquivo
        // que o CMake põe o ícone dentro do executável, que é o que o Windows mostra na barra de tarefas.
        {
            const auto args = getCommandLineParameterArray();
            const int i = args.indexOf("--exportar-icone");
            if (i >= 0 && i + 1 < args.size()) {
                const auto arquivo = juce::File::getCurrentWorkingDirectory().getChildFile(args[i + 1].unquoted());
                arquivo.deleteFile();
                juce::FileOutputStream saida(arquivo);
                if (saida.openedOk()) juce::PNGImageFormat().writeImageToStream(iconeDoApp(), saida);
                quit();
                return;
            }
        }

        // --arquivo show.wav [--linhas N]: abre simulando. --captura tela.png --apos SEG: grava a tela e sai.
        TelaPrincipal::Opcoes opcoes;
        const auto args = getCommandLineParameterArray();
        const auto pasta = juce::File::getCurrentWorkingDirectory();
        for (int i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == "--arquivo") opcoes.arquivo = pasta.getChildFile(args[++i].unquoted());
            else if (args[i] == "--linhas") opcoes.linhas = args[++i].getIntValue() - 1;
            else if (args[i] == "--lr") opcoes.lr = args[++i].getIntValue() - 1;
            else if (args[i] == "--vistas") opcoes.vistas = args[++i];
            else if (args[i] == "--fracao") opcoes.fracao = args[++i].getIntValue();
            else if (args[i] == "--calibracao") { opcoes.calibracao = args[++i].getDoubleValue(); opcoes.temCalibracao = true; }
            else if (args[i] == "--passagem-ate") opcoes.passagemAte = args[++i].getDoubleValue();
            else if (args[i] == "--captura") opcoes.captura = pasta.getChildFile(args[++i].unquoted());
            else if (args[i] == "--apos") opcoes.apos = args[++i].getDoubleValue();
            else if (args[i] == "--rever") opcoes.rever = args[++i].getDoubleValue();
            else if (args[i] == "--referencia") opcoes.referencia = args[++i].unquoted();
            else if (args[i] == "--equilibrio") opcoes.equilibrio = args[++i].unquoted();
            else if (args[i] == "--mapa") opcoes.mapa = args[++i].unquoted();
            else if (args[i] == "--layout") opcoes.layout = pasta.getChildFile(args[++i].unquoted());
        }
        opcoes.janelasCurtas = args.contains("--janelas-curtas");
        opcoes.editando = args.contains("--editando");
        opcoes.ringOut = args.contains("--ring-out");
        opcoes.capturarCalibracao = args.contains("--janela-de-calibracao");
        janela_ = std::make_unique<Janela>(getApplicationName(), opcoes);
    }

    void shutdown() override {
        janela_ = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    class Janela : public juce::DocumentWindow {
    public:
        Janela(const juce::String& nome, const TelaPrincipal::Opcoes& opcoes)
            : DocumentWindow(nome, cores::fundo, DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new TelaPrincipal(opcoes), true);
            setResizable(true, false);
            setResizeLimits(1000, 780, 4000, 3000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
            setIcon(iconeDoApp());
            if (auto* par = getPeer()) par->setIcon(iconeDoApp());     // barra de tarefas
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    Aparencia visual_;
    std::unique_ptr<Janela> janela_;
};

START_JUCE_APPLICATION(PapagaioApp)
