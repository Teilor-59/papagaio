#include "TelaPrincipal.h"

#include "Aparencia.h"
#include "Calibracao.h"

#include <algorithm>
#include <cmath>

using papagaio::Estado;

namespace {

constexpr int kMargem = 14;
constexpr int kFolgaDaMesa = 5;     // a mesa já deixa esse vão em volta de cada bloco
const double kMedias[] = { 0.0, 1.0, 3.0, 10.0 };

} // namespace

TelaPrincipal::TelaPrincipal(const Opcoes& opcoes) : opcoes_(opcoes) {
    juce::PropertiesFile::Options po;
    po.applicationName = "Papagaio";
    po.folderName = "Papagaio";
    po.filenameSuffix = ".settings";
    po.osxLibrarySubFolder = "Application Support";
    propriedades_.setStorageParameters(po);

    fonte_.setFont(fonte(14.0f));
    fonte_.setColour(juce::Label::textColourId, cores::suave);
    fonte_.setJustificationType(juce::Justification::centredRight);
    for (auto* r : { &rotuloMic_, &rotuloLinhas_, &rotuloLr_ }) {
        r->setFont(fonte(14.0f));
        r->setColour(juce::Label::textColourId, cores::suave);
    }
    rotuloMic_.setText("Mic", juce::dontSendNotification);
    rotuloLinhas_.setText("Linhas", juce::dontSendNotification);
    rotuloLr_.setText("L/R", juce::dontSendNotification);

    botaoAudio_.setButtonText(txt("Áudio…"));
    botaoAudio_.onClick = [this] { abrirConfiguracaoDeAudio(); };
    botaoArquivo_.onClick = [this] { alternarArquivo(); };
    botaoLayout_.setButtonText("Layout");
    botaoLayout_.setTooltip(txt("Arrume a tela: mova, redimensione, acrescente e tire painéis."));
    botaoLayout_.onClick = [this] {
        if (mesa_->editando())
            editarLayout(false);
        else
            menuDoLayout();
    };
    for (auto* c : { &comboMic_, &comboLinhas_, &comboLrA_, &comboLrB_ }) c->onChange = [this] { canaisEscolhidos(); };
    comboLinhas_.setTooltip(txt("Canal com a soma das linhas (DI, teclado, playback): o gabarito do que é música."));
    comboLrA_.setTooltip(txt("L/R da mesa: um canal mono ou o par. Com ele dá para separar o que mudou na sala do que mudou no mix."));
    comboLrB_.setTooltip(comboLrA_.getTooltip());

    // a curva de referência do RTA é uma só, vale para todos os RTAs e fica guardada entre sessões
    referencia_.deTexto(capturando() ? opcoes_.referencia : preferencias()->getValue("referencia"));
    referencia_.aoMudar = [this] {
        if (!capturando()) preferencias()->setValue("referencia", referencia_.paraTexto());
    };
    equilibrio_.deTexto(capturando() ? opcoes_.equilibrio : preferencias()->getValue("equilibrio"));
    equilibrio_.aoMudar = [this] {
        if (!capturando()) preferencias()->setValue("equilibrio", equilibrio_.paraTexto());
    };
    if (!capturando()) {
        media_ = juce::jlimit(1, 4, preferencias()->getIntValue("media", 2));
        motor_.definirNivelLento(preferencias()->getBoolValue("nivelLento", false));
    }
    motor_.definirMediaDoEspectro(kMedias[media_ - 1]);

    // o mapa de risco é da passagem de som de hoje: fica guardado até o engenheiro limpar
    motor_.definirMapa(papagaio::MapaDeRisco::deTexto((capturando() ? opcoes_.mapa : preferencias()->getValue("mapaDeRisco")).toStdString()));
    if (capturando() && opcoes_.ringOut) motor_.iniciarRingOut();

    montarMesa();

    for (juce::Component* c : std::initializer_list<juce::Component*> { &fonte_, &rotuloMic_, &rotuloLinhas_, &rotuloLr_, &botaoAudio_,
                                                                       &botaoArquivo_, &botaoLayout_, &comboMic_, &comboLinhas_, &comboLrA_,
                                                                       &comboLrB_, &medidor_, mesa_.get() })
        addAndMakeVisible(c);

    motor_.dispositivos().addChangeListener(this);

    // A captura de tela é ferramenta de desenvolvimento e não deve depender de placa nenhuma.
    if (!capturando()) {
        juce::Component::SafePointer<TelaPrincipal> eu(this);
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [eu](bool permitido) {
            if (eu == nullptr) return;
            if (!permitido) {
                eu->erroDeAudio_ = txt("Sem permissão para usar o microfone");
                return;
            }
            const auto estado = eu->preferencias()->getXmlValue("audio");
            eu->erroDeAudio_ = eu->motor_.iniciar(estado.get());
            eu->atualizarCanais();
        });
    }

    if (opcoes_.arquivo.existsAsFile()) {
        Simulacao s;
        s.velocidade = capturando() ? 40.0 : 1.0;
        s.pausarEm = capturando() ? opcoes_.apos : 0.0;
        s.passagemAte = opcoes_.passagemAte;
        s.janelasCurtas = opcoes_.janelasCurtas;
        abrirArquivo(opcoes_.arquivo, s, opcoes_.linhas, opcoes_.lr);
    }

    setSize(1100, 880);
    startTimerHz(30);
}

TelaPrincipal::~TelaPrincipal() {
    stopTimer();
    motor_.dispositivos().removeChangeListener(this);
}

// Cria a mesa, diz a ela o que os blocos podem pedir à tela e carrega o layout.
void TelaPrincipal::montarMesa() {
    Servicos s;
    s.motor = &motor_;
    s.referencia = &referencia_;
    s.equilibrio = &equilibrio_;
    s.calibrar = [this] { abrirCalibracao(); };
    s.limparHistorico = [this] { motor_.limparHistorico(); };
    s.mostrarNoEspectrograma = [this](double segundos) { mostrarNoEspectrograma(segundos); };
    s.media = [this] { return media_; };
    s.definirMedia = [this](int media) {
        media_ = juce::jlimit(1, 4, media);
        motor_.definirMediaDoEspectro(kMedias[media_ - 1]);
        if (!capturando()) preferencias()->setValue("media", media_);
    };
    s.definirNivelLento = [this](bool lento) {
        motor_.definirNivelLento(lento);
        if (!capturando()) preferencias()->setValue("nivelLento", lento);
    };
    mesa_ = std::make_unique<Mesa>(s);

    bool carregou = false;
    if (capturando()) {
        if (opcoes_.layout.existsAsFile()) carregou = mesa_->deTexto(opcoes_.layout.loadFileAsString());
        if (!carregou) {
            const auto vistas = juce::StringArray::fromTokens(opcoes_.vistas, ",", "");
            mesa_->layoutPadrao(vistas.isEmpty() ? juce::StringArray { "espectro" } : vistas, opcoes_.fracao > 0 ? opcoes_.fracao : 3);
        }
        editarLayout(opcoes_.editando);
    } else {
        carregou = mesa_->deTexto(preferencias()->getValue("layout"));
        if (!carregou) mesa_->layoutPadrao();
    }
    mesa_->aoMudar = [this] {
        if (!capturando()) preferencias()->setValue("layout", mesa_->paraTexto());
    };
}

void TelaPrincipal::menuDoLayout() {
    juce::PopupMenu menu, novos;
    const auto& tipos = tiposDeBloco();
    for (int i = 0; i < (int) tipos.size(); ++i) novos.addItem(100 + i, txt(tipos[(size_t) i].nome));
    menu.addItem(1, "Editar o layout");
    menu.addSubMenu("Acrescentar painel", novos);
    menu.addSeparator();
    menu.addItem(2, txt("Voltar ao layout padrão"));

    juce::Component::SafePointer<TelaPrincipal> eu(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(botaoLayout_), [eu](int escolha) {
        if (eu == nullptr || escolha == 0) return;
        if (escolha == 1) {
            eu->editarLayout(true);
        } else if (escolha == 2) {
            eu->mesa_->layoutPadrao();
            eu->mesa_->editar(eu->mesa_->editando());
            eu->mesa_->aoMudar();
        } else {
            // painel novo já entra em modo de edição, para ser posto no lugar
            eu->mesa_->adicionar(tiposDeBloco()[(size_t) (escolha - 100)].tipo);
            eu->editarLayout(true);
            eu->mesa_->aoMudar();
        }
    });
}

void TelaPrincipal::editarLayout(bool editando) {
    mesa_->editar(editando);
    botaoLayout_.setButtonText(editando ? "Concluir" : "Layout");
    botaoLayout_.setColour(juce::TextButton::buttonColourId, editando ? cores::amarelo : cores::botao);
    botaoLayout_.setColour(juce::TextButton::textColourOffId, editando ? cores::tinta : cores::texto);
}

void TelaPrincipal::paint(juce::Graphics& g) {
    g.drawImageAt(fundo_, 0, 0);
    g.drawImageAt(logo_, areaDoLogo_.getX(), areaDoLogo_.getY());
}

void TelaPrincipal::resized() {
    auto a = getLocalBounds().reduced(kMargem);

    // O fundo e o logotipo são desenhados uma vez por tamanho de janela: os painéis de vidro
    // repintam o que está por baixo a cada quadro, e isso tem que ser só copiar uma imagem.
    if (getWidth() > 0 && getHeight() > 0) {
        fundo_ = juce::Image(juce::Image::RGB, getWidth(), getHeight(), false);
        juce::Graphics g(fundo_);
        pintarFundo(g, getLocalBounds());
    }

    auto topo = a.removeFromTop(40);
    const auto logo = logotipo();
    areaDoLogo_ = topo.removeFromLeft(juce::roundToInt(40.0f * (float) logo.getWidth() / (float) juce::jmax(1, logo.getHeight())));
    logo_ = logo.rescaled(areaDoLogo_.getWidth(), areaDoLogo_.getHeight(), juce::Graphics::highResamplingQuality);
    topo.removeFromLeft(12);
    topo = topo.withSizeKeepingCentre(topo.getWidth(), 34);
    botaoAudio_.setBounds(topo.removeFromRight(86));
    topo.removeFromRight(8);
    botaoArquivo_.setBounds(topo.removeFromRight(184));
    topo.removeFromRight(8);
    botaoLayout_.setBounds(topo.removeFromRight(96));
    topo.removeFromRight(12);
    fonte_.setBounds(topo);

    a.removeFromTop(8);
    auto linha = a.removeFromTop(30);
    rotuloMic_.setBounds(linha.removeFromLeft(36));
    comboMic_.setBounds(linha.removeFromLeft(170));
    linha.removeFromLeft(10);
    medidor_.setBounds(linha.removeFromLeft(100));
    linha.removeFromLeft(22);
    rotuloLinhas_.setBounds(linha.removeFromLeft(54));
    comboLinhas_.setBounds(linha.removeFromLeft(170));
    linha.removeFromLeft(22);
    rotuloLr_.setBounds(linha.removeFromLeft(34));
    comboLrA_.setBounds(linha.removeFromLeft(150));
    linha.removeFromLeft(6);
    comboLrB_.setBounds(linha.removeFromLeft(150));

    a.removeFromTop(12 - kFolgaDaMesa);
    mesa_->setBounds(a.expanded(kFolgaDaMesa, 0).withTrimmedBottom(-kFolgaDaMesa));
}

void TelaPrincipal::timerCallback() {
    Contexto c;
    c.inst = motor_.instantaneo();
    c.inst.calibrado = escala_.calibrado;
    c.inst.calibracaoDb = escala_.calibracaoDb;
    c.escala = escala_;
    if (motor_.revisaoDoHistorico() != revisao_) {
        revisao_ = motor_.revisaoDoHistorico();
        historico_ = motor_.historico();
        // os alertas chegam quando terminam; a lista segue a ordem em que começaram
        std::stable_sort(historico_.begin(), historico_.end(), [](const Registro& a, const Registro& b) { return a.hora < b.hora; });
    }
    c.historico = &historico_;
    c.revisaoDoHistorico = revisao_;

    auto& d = c.alerta;
    d.pior = c.inst.pior;
    d.temSinal = c.inst.temSinal;
    d.escala = escala_;
    d.fimDoArquivo = c.inst.fimDoArquivo;
    d.gravandoRingOut = c.inst.gravandoRingOut;
    d.segundosDeRingOut = c.inst.tempo - c.inst.inicioDoRingOut;
    for (const auto& r : historico_) {
        if (r.deDrift) {
            ++d.drifts;
            continue;
        }
        if (r.evento.estado == Estado::vermelho) ++d.vermelhos;
        if (r.evento.estado == Estado::laranja) ++d.laranjas;
        if (r.evento.estado == Estado::amarelo) ++d.amarelos;
        // só conta como reincidência o que chegou a tocar no show; amarelo é só suspeita, e o ring-out foi de propósito
        if (d.pior.estado != Estado::nenhum && r.evento.estado >= Estado::laranja && !r.evento.ringOut && std::abs(r.evento.f / d.pior.f - 1.0) < 0.03)
            ++d.voltou;
    }

    medidor_.mostrar(c.inst.picoMicDb);
    mesa_->atualizar(c);

    if (!capturando() && c.inst.revisaoDoMapa != revisaoDoMapa_) {
        revisaoDoMapa_ = c.inst.revisaoDoMapa;
        preferencias()->setValue("mapaDeRisco", juce::String(c.inst.mapa.emTexto()));
    }

    // Som ao vivo se mede em dB SPL, e isso depende de calibrar o mic. Sem calibração, o app pede logo de saída.
    if (!capturando() && !ofereceuCalibracao_ && !escala_.calibrado && c.inst.temSinal) {
        ofereceuCalibracao_ = true;
        abrirCalibracao();
    }

    botaoArquivo_.setButtonText(motor_.emArquivo() ? juce::String("Voltar ao vivo") : txt("Simular com arquivo…"));
    fonte_.setText(mesa_->editando() ? txt("Arraste um painel para mover, puxe a borda para mudar o tamanho, × para tirar")
                   : erroDeAudio_.isNotEmpty() && !motor_.emArquivo() ? erroDeAudio_ : motor_.descricaoDaFonte(),
                   juce::dontSendNotification);

    if (capturando() && !capturou_ && (c.inst.fimDoArquivo || c.inst.tempo >= opcoes_.apos - 0.05)) {
        // revendo, a vista precisa de uns quadros para montar a janela do passado antes da foto
        if (opcoes_.rever >= 0 && esperaDaCaptura_ < 0) {
            mostrarNoEspectrograma(opcoes_.rever);
            esperaDaCaptura_ = 4;
        }
        if (esperaDaCaptura_ <= 0)
            capturar();
        else
            --esperaDaCaptura_;
    }
}

void TelaPrincipal::changeListenerCallback(juce::ChangeBroadcaster*) {
    salvarAudio();
    if (!motor_.emArquivo()) atualizarCanais();
}

// Leva o espectrograma a um momento do histórico. Sem espectrograma na tela, a primeira vista vira um.
void TelaPrincipal::mostrarNoEspectrograma(double segundos) {
    std::vector<BlocoVista*> vistas;
    for (auto* b : mesa_->blocos())
        if (auto* v = dynamic_cast<BlocoVista*>(b)) vistas.push_back(v);
    if (vistas.empty()) vistas.push_back(dynamic_cast<BlocoVista*>(mesa_->adicionar("vista")));
    const bool temEspectrograma = std::any_of(vistas.begin(), vistas.end(), [](auto* v) { return v->vista() == Vista::Tipo::espectrograma; });
    if (!temEspectrograma) vistas.front()->definirVista(Vista::Tipo::espectrograma);
    for (auto* v : vistas)
        if (v->vista() == Vista::Tipo::espectrograma) v->irPara(segundos);
}

void TelaPrincipal::atualizarCanais() {
    canais_ = motor_.nomesDosCanais();
    const int n = canais_.size();
    int mic = motor_.canalMic(), linhas = motor_.canalLinhas(), lrA = motor_.canalLrA(), lrB = motor_.canalLrB();
    if (!motor_.emArquivo()) {
        auto* p = preferencias();
        mic = p->getIntValue("canalMic", 0);
        linhas = p->getIntValue("canalLinhas", -1);
        lrA = p->getIntValue("canalLrA", -1);
        lrB = p->getIntValue("canalLrB", -1);
    }
    mic = juce::jlimit(0, juce::jmax(0, n - 1), mic);
    for (int* c : { &linhas, &lrA, &lrB })
        if (*c >= n || *c == mic) *c = -1;

    const auto nenhum = txt("Nenhum");
    comboMic_.clear(juce::dontSendNotification);
    for (auto* c : { &comboLinhas_, &comboLrA_, &comboLrB_ }) {
        c->clear(juce::dontSendNotification);
        c->addItem(nenhum, 1);
    }
    for (int i = 0; i < n; ++i) {
        comboMic_.addItem(canais_[i], i + 1);
        for (auto* c : { &comboLinhas_, &comboLrA_, &comboLrB_ }) c->addItem(canais_[i], i + 2);
    }
    comboMic_.setSelectedId(mic + 1, juce::dontSendNotification);
    comboLinhas_.setSelectedId(linhas + 2, juce::dontSendNotification);
    comboLrA_.setSelectedId(lrA + 2, juce::dontSendNotification);
    comboLrB_.setSelectedId(lrB + 2, juce::dontSendNotification);
    motor_.escolherCanais(mic, linhas, lrA, lrB);
    carregarCalibracao();
}

void TelaPrincipal::canaisEscolhidos() {
    const int mic = juce::jmax(0, comboMic_.getSelectedId() - 1);
    int linhas = comboLinhas_.getSelectedId() - 2, lrA = comboLrA_.getSelectedId() - 2, lrB = comboLrB_.getSelectedId() - 2;
    // o mic não pode ser também referência
    const auto tirar = [mic](int& canal, juce::ComboBox& combo) {
        if (canal != mic) return;
        canal = -1;
        combo.setSelectedId(1, juce::dontSendNotification);
    };
    tirar(linhas, comboLinhas_);
    tirar(lrA, comboLrA_);
    tirar(lrB, comboLrB_);
    motor_.escolherCanais(mic, linhas, lrA, lrB);
    if (!motor_.emArquivo()) {
        auto* p = preferencias();
        p->setValue("canalMic", mic);
        p->setValue("canalLinhas", linhas);
        p->setValue("canalLrA", lrA);
        p->setValue("canalLrB", lrB);
    }
    carregarCalibracao();
}

// A calibração vale para um canal de uma placa: cada um guarda a sua.
juce::String TelaPrincipal::chaveDeCalibracao() {
    if (motor_.emArquivo()) return "calibracao|arquivo";
    juce::String placa;
    if (auto* d = motor_.dispositivos().getCurrentAudioDevice()) placa = d->getName();
    const int mic = motor_.canalMic();
    return "calibracao|" + placa + "|" + (mic < canais_.size() ? canais_[mic] : juce::String(mic));
}

void TelaPrincipal::carregarCalibracao() {
    if (capturando()) {
        escala_ = { opcoes_.temCalibracao, opcoes_.calibracao };
        return;
    }
    const auto chave = chaveDeCalibracao();
    escala_ = { preferencias()->containsKey(chave), preferencias()->getDoubleValue(chave, 0.0) };
}

void TelaPrincipal::definirCalibracao(bool tem, double db) {
    escala_ = { tem, tem ? db : 0.0 };
    if (capturando()) return;
    if (tem)
        preferencias()->setValue(chaveDeCalibracao(), db);
    else
        preferencias()->removeValue(chaveDeCalibracao());
}

void TelaPrincipal::abrirCalibracao() {
    juce::Component::SafePointer<TelaPrincipal> eu(this);
    auto* dialogo = new DialogoDeCalibracao(motor_, escala_, [eu](bool tem, double db) {
        if (eu != nullptr) eu->definirCalibracao(tem, db);
    });
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(dialogo);
    o.dialogTitle = txt("Calibração do mic");
    o.dialogBackgroundColour = cores::cartao;
    o.componentToCentreAround = this;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void TelaPrincipal::abrirConfiguracaoDeAudio() {
    auto* seletor = new juce::AudioDeviceSelectorComponent(motor_.dispositivos(), 1, 256, 0, 0, false, false, false, false);
    seletor->setSize(520, 440);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(seletor);
    o.dialogTitle = txt("Entrada de áudio");
    o.dialogBackgroundColour = cores::cartao;
    o.componentToCentreAround = this;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void TelaPrincipal::alternarArquivo() {
    if (motor_.emArquivo()) {
        motor_.voltarAoVivo();
        atualizarCanais();
        return;
    }
    seletor_ = std::make_unique<juce::FileChooser>(txt("Abrir gravação"), juce::File(), "*.wav");
    seletor_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto arquivo = fc.getResult();
        if (arquivo.existsAsFile()) abrirArquivo(arquivo, Simulacao {}, -1, -1);
    });
}

void TelaPrincipal::abrirArquivo(const juce::File& arquivo, const Simulacao& simulacao, int linhas, int lr) {
    juce::String erro;
    if (!motor_.abrirArquivo(arquivo, erro, simulacao)) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, txt("Não deu para abrir"), arquivo.getFileName() + ": " + erro);
        return;
    }
    motor_.escolherCanais(0, linhas, lr, -1);
    atualizarCanais();
}

void TelaPrincipal::salvarAudio() {
    if (capturando()) return;
    if (const auto estado = motor_.dispositivos().createStateXml()) preferencias()->setValue("audio", estado.get());
}

void TelaPrincipal::capturar() {
    capturou_ = true;
    juce::Image imagem;
    if (opcoes_.capturarCalibracao) {
        DialogoDeCalibracao dialogo(motor_, escala_, {});
        dialogo.atualizar();
        imagem = dialogo.createComponentSnapshot(dialogo.getLocalBounds(), true, 1.0f);
    } else {
        imagem = createComponentSnapshot(getLocalBounds(), true, 1.0f);
    }
    opcoes_.captura.deleteFile();
    juce::FileOutputStream saida(opcoes_.captura);
    if (saida.openedOk()) juce::PNGImageFormat().writeImageToStream(imagem, saida);
    // os painéis soltos em janela própria saem em arquivos ao lado: tela_janela1.png, tela_janela2.png...
    int n = 0;
    for (const auto& foto : mesa_->fotosDasJanelas()) {
        const auto arquivo = opcoes_.captura.getSiblingFile(opcoes_.captura.getFileNameWithoutExtension() + "_janela" + juce::String(++n) + ".png");
        arquivo.deleteFile();
        juce::FileOutputStream s(arquivo);
        if (s.openedOk()) juce::PNGImageFormat().writeImageToStream(foto, s);
    }
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}
