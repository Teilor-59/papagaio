#include "Mesa.h"

#include "Aparencia.h"

namespace {

constexpr int kFolga = 5;                   // metade do vão entre dois blocos, em pixels
constexpr int kMinimoColunas = 4, kMinimoLinhas = 3;
constexpr int kAlturaDaTarja = 24;
const juce::Colour kTinta = cores::tinta;

} // namespace

// Um bloco com o que é preciso para arrumá-lo: uma capa por cima, que segura o mouse enquanto
// se edita o layout, e uma borda que redimensiona.
class Mesa::Moldura : public juce::Component {
public:
    Moldura(Mesa& mesa, std::unique_ptr<Bloco> b) : bloco(std::move(b)), mesa_(mesa), capa_(*this), borda_(*this) {
        limites.setMinimumSize(90, 60);
        limites.setMinimumOnscreenAmounts(10000, 10000, 10000, 10000);     // o bloco não sai da mesa
        addAndMakeVisible(*bloco);
        addChildComponent(capa_);
        addChildComponent(borda_);
    }

    void resized() override {
        bloco->setBounds(getLocalBounds());
        capa_.setBounds(getLocalBounds());
        borda_.setBounds(getLocalBounds());
    }

    void editar(bool editando) {
        capa_.setVisible(editando);
        borda_.setVisible(editando);
    }

    std::unique_ptr<Bloco> bloco;
    juce::Rectangle<int> celulas;
    juce::ComponentBoundsConstrainer limites;

private:
    struct Capa : juce::Component {
        explicit Capa(Moldura& m) : moldura(m) {
            fechar.setButtonText(txt("×"));
            fechar.setTooltip("Tirar este painel da tela");
            fechar.setColour(juce::TextButton::buttonColourId, kTinta.withAlpha(0.15f));
            fechar.setColour(juce::TextButton::textColourOffId, kTinta);
            fechar.onClick = [this] {
                // o clique acontece dentro da moldura que vai sumir; a remoção espera o clique terminar
                juce::Component::SafePointer<Moldura> alvo(&moldura);
                auto* mesa = &moldura.mesa_;
                juce::MessageManager::callAsync([alvo, mesa] {
                    if (alvo != nullptr) mesa->remover(alvo.getComponent());
                });
            };
            addAndMakeVisible(fechar);

            soltar.setButtonText("Soltar");
            soltar.setTooltip(txt("Tira este painel da janela principal e abre numa janela só dele, que pode ir para outra tela"));
            soltar.setColour(juce::TextButton::buttonColourId, kTinta.withAlpha(0.15f));
            soltar.setColour(juce::TextButton::textColourOffId, kTinta);
            soltar.onClick = [this] {
                juce::Component::SafePointer<Moldura> alvo(&moldura);
                auto* mesa = &moldura.mesa_;
                juce::MessageManager::callAsync([alvo, mesa] {
                    if (alvo != nullptr) mesa->soltar(alvo.getComponent());
                });
            };
            addAndMakeVisible(soltar);
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }

        void resized() override {
            fechar.setBounds(getWidth() - 30, 2, 26, kAlturaDaTarja - 4);
            soltar.setBounds(getWidth() - 30 - 58, 2, 54, kAlturaDaTarja - 4);
            soltar.setVisible(getWidth() > 190);
        }

        void paint(juce::Graphics& g) override {
            const auto r = getLocalBounds().toFloat();
            g.setColour(juce::Colours::black.withAlpha(0.30f));
            g.fillRoundedRectangle(r, 14.0f);
            g.setColour(cores::amarelo);
            g.drawRoundedRectangle(r.reduced(1.0f), 14.0f, 2.0f);
            g.fillRoundedRectangle(r.withHeight((float) kAlturaDaTarja), 14.0f);
            g.fillRect(r.withHeight((float) kAlturaDaTarja).withTrimmedTop(12.0f));
            g.setColour(kTinta);
            g.setFont(fonte(13.0f, true));
            g.drawText(moldura.bloco->titulo(), getLocalBounds().removeFromTop(kAlturaDaTarja).withTrimmedLeft(10).withTrimmedRight(soltar.isVisible() ? 92 : 34),
                       juce::Justification::centredLeft);
        }

        void mouseDown(const juce::MouseEvent& e) override {
            moldura.toFront(false);
            arrasto.startDraggingComponent(&moldura, e);
        }
        void mouseDrag(const juce::MouseEvent& e) override { arrasto.dragComponent(&moldura, e, &moldura.limites); }
        void mouseUp(const juce::MouseEvent&) override { moldura.mesa_.encaixar(moldura); }

        Moldura& moldura;
        juce::ComponentDragger arrasto;
        juce::TextButton fechar, soltar;
    };

    struct Borda : juce::ResizableBorderComponent {
        explicit Borda(Moldura& m) : juce::ResizableBorderComponent(&m, &m.limites), moldura(m) { setBorderThickness(juce::BorderSize<int>(8)); }
        void mouseUp(const juce::MouseEvent& e) override {
            juce::ResizableBorderComponent::mouseUp(e);
            moldura.mesa_.encaixar(moldura);
        }
        Moldura& moldura;
    };

    Mesa& mesa_;
    Capa capa_;
    Borda borda_;
};

// Um bloco solto da mesa, numa janela do sistema: vai para outra tela, maximiza, fica do tamanho que for.
class Mesa::Janela : public juce::DocumentWindow {
public:
    Janela(Mesa& mesa, std::unique_ptr<Bloco> b, juce::Rectangle<int> celulasDeOrigem)
        : juce::DocumentWindow(b->titulo(), cores::fundo, juce::DocumentWindow::allButtons), bloco(std::move(b)), celulas(celulasDeOrigem), mesa_(mesa) {
        setUsingNativeTitleBar(true);
        setResizable(true, false);
        setResizeLimits(140, 90, 10000, 10000);
        recheio_.addAndMakeVisible(*bloco);
        setContentNonOwned(&recheio_, false);
    }

    // Fechar a janela não joga o painel fora: ele volta para a mesa.
    void closeButtonPressed() override {
        juce::Component::SafePointer<Janela> eu(this);
        auto* mesa = &mesa_;
        juce::MessageManager::callAsync([eu, mesa] {
            if (eu != nullptr) mesa->recolher(eu.getComponent());
        });
    }

    void moved() override {
        juce::DocumentWindow::moved();
        if (pronta) mesa_.avisar();
    }
    void resized() override {
        juce::DocumentWindow::resized();
        if (pronta) mesa_.avisar();
    }

    std::unique_ptr<Bloco> bloco;
    juce::Rectangle<int> celulas;       // onde o bloco estava na mesa, para a volta
    bool pronta = false;                // só avisa a mesa de mudanças depois de aberta

private:
    struct Recheio : juce::Component {
        void paint(juce::Graphics& g) override { pintarFundo(g, getLocalBounds()); }
        void resized() override {
            if (auto* filho = getChildComponent(0)) filho->setBounds(getLocalBounds().reduced(8));
        }
    };

    Mesa& mesa_;
    Recheio recheio_;
};

Mesa::Mesa(const Servicos& servicos) : servicos_(servicos) {}
Mesa::~Mesa() = default;

void Mesa::atualizar(const Contexto& contexto) {
    for (auto* m : molduras_) m->bloco->atualizar(contexto);
    for (auto* j : janelas_) {
        j->bloco->atualizar(contexto);
        if (j->getName() != j->bloco->titulo()) j->setName(j->bloco->titulo());
    }
}

std::vector<juce::Image> Mesa::fotosDasJanelas() {
    std::vector<juce::Image> fotos;
    for (auto* j : janelas_) fotos.push_back(j->createComponentSnapshot(j->getLocalBounds(), true, 1.0f));
    return fotos;
}

void Mesa::editar(bool editando) {
    editando_ = editando;
    for (auto* m : molduras_) m->editar(editando);
    repaint();
}

void Mesa::avisar() {
    if (aoMudar) aoMudar();
}

std::vector<Bloco*> Mesa::blocos() const {
    std::vector<Bloco*> r;
    for (auto* m : molduras_) r.push_back(m->bloco.get());
    for (auto* j : janelas_) r.push_back(j->bloco.get());
    return r;
}

Bloco* Mesa::adicionar(const juce::String& tipo, const juce::String& config, juce::Rectangle<int> celulas) {
    auto bloco = criarBloco(tipo, servicos_);
    if (bloco == nullptr) return nullptr;
    bloco->configurar(config);
    bloco->aoMudar = [this] { avisar(); };
    auto* ponteiro = bloco.get();

    if (celulas.isEmpty()) {
        // painel novo nasce perto do canto, um pouco deslocado do anterior para não cobrir o último
        int largura = 12, altura = 8;
        for (const auto& t : tiposDeBloco())
            if (tipo == t.tipo) {
                largura = t.largura;
                altura = t.altura;
            }
        const int passo = 2 * (molduras_.size() % 5);
        celulas = { 2 + passo, 2 + passo, largura, altura };
    }
    emoldurar(std::move(bloco), celulas);
    return ponteiro;
}

Mesa::Moldura* Mesa::emoldurar(std::unique_ptr<Bloco> bloco, juce::Rectangle<int> celulas) {
    auto* m = molduras_.add(new Moldura(*this, std::move(bloco)));
    m->celulas = celulas;
    addAndMakeVisible(m);
    m->editar(editando_);
    posicionar(*m);
    return m;
}

Mesa::Janela* Mesa::abrirJanela(std::unique_ptr<Bloco> bloco, juce::Rectangle<int> celulas, juce::Rectangle<int> naTela) {
    auto* j = janelas_.add(new Janela(*this, std::move(bloco), celulas));
    // se a tela onde a janela estava não existe mais, ela aparece inteira numa que exista
    j->setBoundsConstrained(naTela);
    j->setVisible(true);
    j->pronta = true;
    return j;
}

void Mesa::remover(Moldura* moldura) {
    molduras_.removeObject(moldura);
    avisar();
}

// O bloco sai da mesa e vira janela, no mesmo lugar e do mesmo tamanho em que estava.
void Mesa::soltar(Moldura* moldura) {
    const auto naTela = moldura->getScreenBounds().expanded(8).withTrimmedTop(-30);
    const auto celulas = moldura->celulas;
    auto bloco = std::move(moldura->bloco);
    moldura->removeChildComponent(bloco.get());
    molduras_.removeObject(moldura);
    abrirJanela(std::move(bloco), celulas, naTela);
    avisar();
}

void Mesa::recolher(Janela* janela) {
    auto bloco = std::move(janela->bloco);
    const auto celulas = janela->celulas;
    janela->pronta = false;
    janelas_.removeObject(janela);
    emoldurar(std::move(bloco), celulas);
    avisar();
}

void Mesa::layoutPadrao(const juce::StringArray& vistas, int fracao) {
    molduras_.clear();
    janelas_.clear();
    adicionar("alerta", {}, { 0, 0, 48, 7 });
    // as vistas dividem a coluna da esquerda, uma em cima da outra
    const int n = juce::jmax(1, vistas.size());
    for (int i = 0; i < n; ++i) {
        const int y = 7 + 19 * i / n, fim = 7 + 19 * (i + 1) / n;
        adicionar("vista", "tipo=" + (vistas.size() > i ? vistas[i] : juce::String("espectro")) + ";fracao=" + juce::String(fracao), { 0, y, 32, fim - y });
    }
    adicionar("niveis", {}, { 32, 7, 16, 8 });
    adicionar("salaemix", {}, { 32, 15, 16, 11 });
    adicionar("historico", {}, { 0, 26, 32, 10 });
    adicionar("mapa", {}, { 32, 26, 16, 10 });
}

juce::String Mesa::paraTexto() const {
    juce::XmlElement raiz("layout");
    for (auto* m : molduras_) {
        auto* e = raiz.createNewChildElement("bloco");
        e->setAttribute("tipo", m->bloco->tipo());
        e->setAttribute("x", m->celulas.getX());
        e->setAttribute("y", m->celulas.getY());
        e->setAttribute("w", m->celulas.getWidth());
        e->setAttribute("h", m->celulas.getHeight());
        e->setAttribute("config", m->bloco->config());
    }
    for (auto* j : janelas_) {
        auto* e = raiz.createNewChildElement("bloco");
        e->setAttribute("tipo", j->bloco->tipo());
        e->setAttribute("x", j->celulas.getX());
        e->setAttribute("y", j->celulas.getY());
        e->setAttribute("w", j->celulas.getWidth());
        e->setAttribute("h", j->celulas.getHeight());
        e->setAttribute("config", j->bloco->config());
        e->setAttribute("janela", j->getBounds().toString());     // posição na tela, em janela própria
    }
    return raiz.toString(juce::XmlElement::TextFormat().singleLine());
}

bool Mesa::deTexto(const juce::String& xml) {
    const auto raiz = juce::parseXML(xml);
    if (raiz == nullptr || !raiz->hasTagName("layout")) return false;
    molduras_.clear();
    janelas_.clear();
    for (auto* e : raiz->getChildWithTagNameIterator("bloco")) {
        const juce::Rectangle<int> celulas(juce::jlimit(0, kColunas - kMinimoColunas, e->getIntAttribute("x")),
                                           juce::jlimit(0, kLinhas - kMinimoLinhas, e->getIntAttribute("y")),
                                           juce::jlimit(kMinimoColunas, kColunas, e->getIntAttribute("w", 12)),
                                           juce::jlimit(kMinimoLinhas, kLinhas, e->getIntAttribute("h", 8)));
        if (e->hasAttribute("janela")) {
            if (auto bloco = criarBloco(e->getStringAttribute("tipo"), servicos_)) {
                bloco->configurar(e->getStringAttribute("config"));
                bloco->aoMudar = [this] { avisar(); };
                abrirJanela(std::move(bloco), celulas, juce::Rectangle<int>::fromString(e->getStringAttribute("janela")));
            }
            continue;
        }
        adicionar(e->getStringAttribute("tipo"), e->getStringAttribute("config"), celulas);
    }
    return !molduras_.isEmpty() || !janelas_.isEmpty();
}

// Solta o bloco na célula mais próxima de onde ele foi deixado.
void Mesa::encaixar(Moldura& moldura) {
    const double cw = getWidth() / (double) kColunas, ch = getHeight() / (double) kLinhas;
    if (cw <= 0 || ch <= 0) return;
    const auto b = moldura.getBounds().expanded(kFolga);
    const int w = juce::jlimit(kMinimoColunas, kColunas, juce::roundToInt(b.getWidth() / cw));
    const int h = juce::jlimit(kMinimoLinhas, kLinhas, juce::roundToInt(b.getHeight() / ch));
    moldura.celulas = { juce::jlimit(0, kColunas - w, juce::roundToInt(b.getX() / cw)), juce::jlimit(0, kLinhas - h, juce::roundToInt(b.getY() / ch)), w, h };
    posicionar(moldura);
    avisar();
}

void Mesa::posicionar(Moldura& moldura) {
    const double cw = getWidth() / (double) kColunas, ch = getHeight() / (double) kLinhas;
    const auto& c = moldura.celulas;
    const int x1 = juce::roundToInt(c.getX() * cw), y1 = juce::roundToInt(c.getY() * ch);
    const int x2 = juce::roundToInt(c.getRight() * cw), y2 = juce::roundToInt(c.getBottom() * ch);
    moldura.setBounds(juce::Rectangle<int>(x1, y1, x2 - x1, y2 - y1).reduced(kFolga));
}

void Mesa::resized() {
    for (auto* m : molduras_) posicionar(*m);
}

void Mesa::paint(juce::Graphics& g) {
    if (!editando_) return;
    // a grade aparece só enquanto se arruma a tela
    const double cw = getWidth() / (double) kColunas, ch = getHeight() / (double) kLinhas;
    g.setColour(cores::suave.withAlpha(0.35f));
    for (int x = 0; x <= kColunas; x += 2)
        for (int y = 0; y <= kLinhas; y += 2) g.fillRect((float) (x * cw) - 1.0f, (float) (y * ch) - 1.0f, 2.0f, 2.0f);
}
