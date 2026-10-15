#include "Blocos.h"

#include <cmath>

using papagaio::Estado;
using papagaio::EventoDeDrift;

namespace {

// "tipo=rta;fracao=3" -> pares
juce::StringPairArray lerConfig(const juce::String& texto) {
    juce::StringPairArray pares;
    for (const auto& item : juce::StringArray::fromTokens(texto, ";", ""))
        if (item.contains("=")) pares.set(item.upToFirstOccurrenceOf("=", false, false), item.fromFirstOccurrenceOf("=", false, false));
    return pares;
}

juce::String comSinal(double v, int casas) { return (v < 0 ? txt("−") : juce::String("+")) + numero(std::abs(v), casas); }
juce::String semSinal(double v, int casas) { return (v < 0 ? txt("−") : juce::String()) + numero(std::abs(v), casas); }

juce::String relogio(double t) {
    t = std::round(t * 10.0) / 10.0;      // arredonda antes de separar, para nunca mostrar 1:60,0
    const int m = (int) (t / 60.0);
    return juce::String(m) + ":" + numero(t - 60.0 * m, 1).paddedLeft('0', 4);
}

// ------------------------------------------------------------------ faixa de alerta

class BlocoAlerta : public Bloco {
public:
    explicit BlocoAlerta(const Servicos& s) : Bloco(s) { addAndMakeVisible(faixa_); }
    juce::String tipo() const override { return "alerta"; }
    juce::String titulo() const override { return "Faixa de alerta"; }
    void atualizar(const Contexto& c) override { faixa_.mostrar(c.alerta); }
    void resized() override { faixa_.setBounds(getLocalBounds()); }

private:
    FaixaDeAlerta faixa_;
};

// ------------------------------------------------------------------ níveis

class BlocoNiveis : public Bloco {
public:
    explicit BlocoNiveis(const Servicos& s) : Bloco(s) {
        painel_.botaoZerar.onClick = [this] { servicos_.motor->zerarNiveis(); };
        painel_.botaoCalibrar.onClick = [this] { servicos_.calibrar(); };
        painel_.comboTempo.onChange = [this] { servicos_.definirNivelLento(painel_.comboTempo.getSelectedId() == 2); };
        addAndMakeVisible(painel_);
    }
    juce::String tipo() const override { return "niveis"; }
    juce::String titulo() const override { return txt("Níveis"); }
    void atualizar(const Contexto& c) override {
        // o tempo rápido ou lento é do medidor, um só: todo painel de níveis mostra o mesmo
        if (!painel_.comboTempo.isPopupActive()) painel_.comboTempo.setSelectedId(c.inst.nivelLento ? 2 : 1, juce::dontSendNotification);
        painel_.mostrar(c.inst);
    }
    void resized() override { painel_.setBounds(getLocalBounds()); }

private:
    PainelDeNiveis painel_;
};

// ------------------------------------------------------------------ sala e mix

class BlocoSalaEMix : public Bloco {
public:
    explicit BlocoSalaEMix(const Servicos& s) : Bloco(s) {
        painel_.botaoPassagem.onClick = [this] {
            if (gravando_)
                servicos_.motor->fecharPassagem();
            else
                servicos_.motor->iniciarPassagem();
        };
        painel_.botaoShow.onClick = [this] { servicos_.motor->marcarInicioDoShow(); };
        addAndMakeVisible(painel_);
    }
    juce::String tipo() const override { return "salaemix"; }
    juce::String titulo() const override { return "Sala e mix"; }
    void atualizar(const Contexto& c) override {
        gravando_ = c.inst.drift.gravandoPassagem;
        painel_.mostrar(c.inst);
    }
    void resized() override { painel_.setBounds(getLocalBounds()); }

private:
    PainelDeDrift painel_;
    bool gravando_ = false;
};

// ------------------------------------------------------------------ histórico

struct Coluna {
    int x, largura;
    juce::Justification::Flags alinhamento;
    const char* nome;
};

const Coluna kColunas[] = {
    { 14, 86, juce::Justification::centredLeft, "HORA" },
    { 100, 90, juce::Justification::centredRight, "FREQUÊNCIA" },
    { 214, 70, juce::Justification::centredLeft, "GEQ" },
    { 290, 110, juce::Justification::centredLeft, "ALERTA" },
    { 400, 80, juce::Justification::centredRight, "DURAÇÃO" },
    { 484, 100, juce::Justification::centredRight, "NÍVEL MÁX." },
    { 608, 110, juce::Justification::centredLeft, "OBS." },
};

// Voltou, ring-out, frequência do mapa: o que mais vale saber sobre o alerta.
juce::String observacao(const papagaio::Evento& e) {
    juce::StringArray marcas;
    if (e.voltou > 0) marcas.add(txt("voltou ") + juce::String(e.voltou) + txt("×"));
    if (e.ringOut)
        marcas.add("ring-out");
    else if (e.risco)
        marcas.add("do mapa");
    return marcas.joinIntoString(txt(" · "));
}

class BlocoHistorico : public Bloco, private juce::ListBoxModel {
public:
    explicit BlocoHistorico(const Servicos& s) : Bloco(s) {
        lista_.setModel(this);
        lista_.setRowHeight(28);
        lista_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        limpar_.setButtonText("Limpar");
        limpar_.onClick = [this] { servicos_.limparHistorico(); };
        addAndMakeVisible(lista_);
        addAndMakeVisible(limpar_);
    }
    ~BlocoHistorico() override { lista_.setModel(nullptr); }

    juce::String tipo() const override { return "historico"; }
    juce::String titulo() const override { return txt("Histórico"); }

    void atualizar(const Contexto& c) override {
        const bool mudouEscala = c.escala.calibrado != escala_.calibrado || c.escala.calibracaoDb != escala_.calibracaoDb;
        escala_ = c.escala;
        historico_ = c.historico;
        if (c.revisaoDoHistorico != revisao_ || mudouEscala) {
            revisao_ = c.revisaoDoHistorico;
            lista_.updateContent();
            repaint();
        }
    }

    void resized() override {
        auto a = getLocalBounds();
        cabecalho_ = a.removeFromTop(30);
        limpar_.setBounds(cabecalho_.withTrimmedRight(10).removeFromRight(80).reduced(0, 4));
        lista_.setBounds(a.withTrimmedBottom(6));
    }

    void paint(juce::Graphics& g) override {
        pintarVidro(g, getLocalBounds().toFloat());
        g.setColour(cores::suave);
        g.setFont(fonte(12.0f, true));
        const float k = aperto(getWidth());
        for (const auto& c : kColunas)
            if ((float) (c.x + c.largura) * k < (float) limpar_.getX() - 4.0f)
                g.drawText(txt(c.nome), juce::roundToInt(c.x * k), 0, juce::roundToInt(c.largura * k), cabecalho_.getHeight(), c.alinhamento);
        g.setColour(cores::borda);
        g.drawHorizontalLine(cabecalho_.getBottom() - 1, 10.0f, (float) getWidth() - 10.0f);
        if (getNumRows() == 0) {
            g.setColour(cores::suave);
            g.setFont(fonte(14.0f));
            g.drawText(txt("O histórico do show aparece aqui."), lista_.getBounds(), juce::Justification::centred);
        }
    }

private:
    int getNumRows() override { return historico_ != nullptr ? (int) historico_->size() : 0; }

    // Em bloco estreito as colunas se apertam para caber, em vez de sumir pela direita.
    static float aperto(int largura) { return juce::jmin(1.0f, (float) (largura - 12) / 700.0f); }

    const Registro* registro(int linha) const {
        if (historico_ == nullptr || linha < 0 || linha >= (int) historico_->size()) return nullptr;
        return &(*historico_)[historico_->size() - 1 - (size_t) linha];     // mais recente em cima
    }

    void paintListBoxItem(int linha, juce::Graphics& g, int largura, int altura, bool) override {
        const auto* r = registro(linha);
        if (r == nullptr) return;
        const juce::String hora = r->deArquivo ? relogio(r->tempo) : r->hora.formatted("%H:%M:%S");

        if (r->deDrift) {
            // mudança de sala ou de mix: uma frase, sem interromper nada
            g.setColour(cores::suave);
            g.setFont(fonte(15.0f));
            const float k = aperto(largura);
            const int x = juce::roundToInt(kColunas[1].x * k);
            g.drawText(hora, juce::roundToInt(kColunas[0].x * k), 0, juce::roundToInt(kColunas[0].largura * k), altura, kColunas[0].alinhamento);
            const auto t = r->drift.tipo;
            g.setColour(t == EventoDeDrift::Tipo::sala ? cores::sala : t == EventoDeDrift::Tipo::mix ? cores::mix : cores::suave);
            g.fillEllipse((float) x + 14.0f, altura * 0.5f - 4.0f, 8.0f, 8.0f);
            g.setColour(cores::texto);
            g.drawText(juce::String::fromUTF8(papagaio::descrever(r->drift).c_str()), x + 30, 0, largura - x - 40, altura, juce::Justification::centredLeft);
            return;
        }

        const auto& e = r->evento;
        const bool forte = e.estado >= Estado::laranja;
        const juce::String celulas[] = {
            hora,
            juce::String(juce::roundToInt(e.f)) + " Hz",
            juce::String(papagaio::bandaGeq(e.f)),
            e.preApito ? txt("pré-apito")
            : forte    ? juce::String(papagaio::nomeEstado(e.estado)).toUpperCase()
                       : juce::String(papagaio::nomeEstado(e.estado)) + (e.vezes > 1 ? txt(" ×") + juce::String(e.vezes) : juce::String()),
            numero(e.duracao, 1) + " s",
            escala_.texto(e.nivelMaxDb, 0),
            observacao(e),
        };
        const float k = aperto(largura);
        for (int i = 0; i < (int) std::size(kColunas); ++i) {
            const auto& c = kColunas[i];
            const bool alerta = i == 3;
            g.setColour(alerta ? cores::doEstado(e.estado) : forte ? cores::texto : cores::suave);
            g.setFont(fonte(juce::jmax(12.0f, 15.0f * std::sqrt(k)), alerta && forte));
            g.drawText(celulas[i], juce::roundToInt(c.x * k), 0, juce::roundToInt(c.largura * k), altura, c.alinhamento);
        }
    }

    // Tocar numa linha abre o espectrograma naquele momento.
    void listBoxItemClicked(int linha, const juce::MouseEvent&) override {
        if (const auto* r = registro(linha)) servicos_.mostrarNoEspectrograma(r->tempo);
    }

    juce::ListBox lista_;
    juce::TextButton limpar_;
    juce::Rectangle<int> cabecalho_;
    const std::vector<Registro>* historico_ = nullptr;
    int revisao_ = -1;
    Escala escala_;
};

// ------------------------------------------------------------------ mapa de risco

// O mapa de risco do ring-out: começa e encerra a gravação, lista as frequências, deixa tirar uma,
// acrescentar à mão ou limpar tudo. Quem guarda o mapa é o motor; o bloco manda o mapa novo inteiro.
class BlocoMapa : public Bloco {
public:
    explicit BlocoMapa(const Servicos& s) : Bloco(s) {
        botaoRingOut_.onClick = [this] {
            if (gravando_)
                servicos_.motor->encerrarRingOut();
            else
                servicos_.motor->iniciarRingOut();
        };
        botaoRingOut_.setTooltip(txt("Na passagem de som, com o PA e os monitores ligados: suba o ganho das mixes devagar. "
                                     "Cada frequência que apitar entra no mapa e passa a ser vigiada com prioridade no show."));
        botaoAcrescentar_.setButtonText(txt("Acrescentar…"));
        botaoAcrescentar_.setTooltip(txt("Põe no mapa uma frequência que você já sabe que dá problema."));
        botaoAcrescentar_.onClick = [this] { acrescentar(); };
        botaoLimpar_.setButtonText("Limpar");
        botaoLimpar_.setTooltip(txt("Apaga o mapa inteiro. Faça isso a cada passagem de som nova."));
        botaoLimpar_.onClick = [this] { limpar(); };
        for (auto* b : { &botaoRingOut_, &botaoAcrescentar_, &botaoLimpar_ }) addAndMakeVisible(b);
    }

    juce::String tipo() const override { return "mapa"; }
    juce::String titulo() const override { return "Mapa de risco"; }

    void atualizar(const Contexto& c) override {
        const bool mudou = c.inst.revisaoDoMapa != revisao_ || c.inst.gravandoRingOut != gravando_
                           || (gravando_ && (int) (c.inst.tempo - c.inst.inicioDoRingOut) != segundos_);
        mapa_ = c.inst.mapa;
        revisao_ = c.inst.revisaoDoMapa;
        gravando_ = c.inst.gravandoRingOut;
        segundos_ = (int) (c.inst.tempo - c.inst.inicioDoRingOut);
        escala_ = c.escala;
        botaoRingOut_.setButtonText(gravando_ ? "Encerrar ring-out" : txt("Começar ring-out"));
        botaoRingOut_.setColour(juce::TextButton::buttonColourId, gravando_ ? cores::amarelo : cores::botao);
        botaoRingOut_.setColour(juce::TextButton::textColourOffId, gravando_ ? cores::tinta : cores::texto);
        botaoLimpar_.setEnabled(!mapa_.vazio());
        if (mudou) repaint();
    }

    void resized() override {
        auto r = getLocalBounds().reduced(12, 10).removeFromBottom(26);
        botaoRingOut_.setBounds(r.removeFromLeft(juce::jmin(150, r.getWidth() / 2)));
        r.removeFromLeft(6);
        botaoLimpar_.setBounds(r.removeFromRight(70));
        r.removeFromRight(6);
        botaoAcrescentar_.setBounds(r.removeFromRight(juce::jmin(110, r.getWidth())));
    }

    void paint(juce::Graphics& g) override {
        pintarVidro(g, getLocalBounds().toFloat());
        auto a = lista();
        auto topo = getLocalBounds().reduced(12, 10).removeFromTop(18);
        g.setColour(cores::texto);
        g.setFont(fonte(13.0f, true));
        g.drawText("MAPA DE RISCO", topo, juce::Justification::centredLeft);
        g.setFont(fonte(12.0f, gravando_));
        g.setColour(gravando_ ? cores::amarelo : cores::suave);
        const juce::String estado = gravando_ ? txt("gravando ring-out  ·  ") + juce::String(segundos_ / 60) + ":" + juce::String(segundos_ % 60).paddedLeft('0', 2)
                                    : mapa_.vazio() ? juce::String()
                                                    : juce::String((int) mapa_.itens().size()) + txt(" vigiadas com prioridade");
        g.drawText(estado, topo, juce::Justification::centredRight);

        if (mapa_.vazio()) {
            g.setColour(cores::suave);
            g.setFont(fonte(13.0f));
            g.drawFittedText(gravando_ ? txt("Suba o ganho devagar. Cada frequência que apitar, tocar em ressonância ou der pré-apito aparece aqui.")
                                       : txt("Na passagem de som, aperte Começar ring-out e suba o ganho das mixes até apitar. "
                                             "As frequências entram aqui e passam a ser vigiadas com prioridade no show."),
                             a.reduced(4, 0), juce::Justification::centred, 4);
            return;
        }
        const auto& itens = mapa_.itens();
        for (size_t i = 0; i < itens.size(); ++i) {
            auto linha = juce::Rectangle<int>(a.getX(), a.getY() + (int) i * kAltura, a.getWidth(), kAltura);
            if (linha.getBottom() > a.getBottom()) {
                g.setColour(cores::suave);
                g.setFont(fonte(12.0f));
                g.drawText(txt("+ ") + juce::String((int) (itens.size() - i)) + txt(" no mapa"), linha.withY(linha.getY() - kAltura / 2),
                           juce::Justification::centredRight);
                break;
            }
            const auto& r = itens[i];
            g.setColour(cores::borda.withAlpha(0.5f));
            g.drawHorizontalLine(linha.getBottom() - 1, (float) linha.getX(), (float) linha.getRight());
            auto x = linha;
            g.setColour(cores::texto);
            g.setFont(fonte(15.0f, true));
            g.drawText(juce::String(juce::roundToInt(r.f)) + " Hz", x.removeFromLeft(84), juce::Justification::centredLeft);
            g.setColour(cores::suave);
            g.setFont(fonte(13.0f));
            g.drawText("GEQ " + juce::String(papagaio::bandaGeq(r.f)), x.removeFromLeft(78), juce::Justification::centredLeft);
            g.setColour(cores::suave);
            g.setFont(fonte(16.0f));
            g.drawText(txt("×"), x.removeFromRight(kLargura), juce::Justification::centred);
            const auto piorEstado = (papagaio::Estado) r.pior;
            g.setColour(r.manual ? cores::suave : cores::doEstado(piorEstado));
            g.setFont(fonte(13.0f, !r.manual));
            const juce::String pior = r.manual ? txt("posta à mão") : piorEstado == papagaio::Estado::amarelo ? txt("pré-apito") : juce::String(papagaio::nomeEstado(piorEstado));
            g.drawText(pior + (r.vezes > 1 ? txt("  ") + juce::String(r.vezes) + txt("×") : juce::String()), x, juce::Justification::centredLeft);
        }
    }

    // o × de cada linha tira a frequência do mapa
    void mouseUp(const juce::MouseEvent& e) override {
        const auto a = lista();
        if (mapa_.vazio() || !a.contains(e.getPosition()) || e.x < a.getRight() - kLargura) return;
        const int i = (e.y - a.getY()) / kAltura;
        if (i < 0 || i >= (int) mapa_.itens().size()) return;
        auto novo = mapa_;
        novo.remover((size_t) i);
        servicos_.motor->definirMapa(novo);
    }

private:
    static constexpr int kAltura = 26, kLargura = 26;

    juce::Rectangle<int> lista() const {
        auto a = getLocalBounds().reduced(12, 10);
        a.removeFromTop(24);
        a.removeFromBottom(26 + 8);
        return a;
    }

    void acrescentar() {
        auto* janela = new juce::AlertWindow("Acrescentar ao mapa", txt("Frequência que você já sabe que dá problema, em Hz."),
                                             juce::MessageBoxIconType::NoIcon);
        janela->addTextEditor("f", {}, "Hz");
        janela->addButton("Acrescentar", 1, juce::KeyPress(juce::KeyPress::returnKey));
        janela->addButton("Cancelar", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<BlocoMapa> eu(this);
        janela->enterModalState(true, juce::ModalCallbackFunction::create([eu, janela](int resposta) {
            const double f = janela->getTextEditorContents("f").trim().replaceCharacter(',', '.').getDoubleValue();
            if (resposta != 1 || eu == nullptr || f < 20.0 || f > 20000.0) return;
            auto novo = eu->mapa_;
            novo.acrescentarManual(f);
            eu->servicos_.motor->definirMapa(novo);
        }), true);
    }

    void limpar() {
        juce::Component::SafePointer<BlocoMapa> eu(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::NoIcon, "Limpar o mapa",
                                           txt("Apaga todas as frequências do mapa de risco. Faça isso numa passagem de som nova."), "Limpar",
                                           "Cancelar", this, juce::ModalCallbackFunction::create([eu](int resposta) {
                                               if (resposta == 1 && eu != nullptr) eu->servicos_.motor->definirMapa({});
                                           }));
    }

    juce::TextButton botaoRingOut_, botaoAcrescentar_, botaoLimpar_;
    papagaio::MapaDeRisco mapa_;
    int revisao_ = -1, segundos_ = 0;
    bool gravando_ = false;
    Escala escala_;
};

// ------------------------------------------------------------------ indicador

// Um valor que o indicador sabe mostrar.
struct Valor {
    juce::String id, grupo, nome;
};

const std::vector<Valor>& catalogo() {
    static const std::vector<Valor> valores = [] {
        std::vector<Valor> v;
        const char* curvas[] = { "A", "C", "Z" };
        const char* quando[][2] = { { "agora", "agora" }, { "medio", "médio (Leq)" }, { "max", "máximo" }, { "min", "mínimo" } };
        for (const char* c : curvas) {
            for (const auto& q : quando)
                v.push_back({ juce::String("nivel.") + c + "." + q[0], txt("Nível ") + c, txt("Nível ") + c + txt(" · ") + txt(q[1]) });
            for (const int minutos : papagaio::kMinutosDaJanela)
                v.push_back({ juce::String("nivel.") + c + ".leq" + juce::String(minutos), txt("Nível ") + c,
                              txt("Nível ") + c + txt(" · Leq ") + juce::String(minutos) + " min" });
            for (const int minutos : papagaio::kMinutosDaJanela)
                v.push_back({ juce::String("nivel.") + c + ".maiorleq" + juce::String(minutos), txt("Nível ") + c,
                              txt("Nível ") + c + txt(" · maior Leq ") + juce::String(minutos) + " min" });
        }
        v.push_back({ "alerta.freq", "Microfonia", txt("Frequência suspeita") });
        v.push_back({ "drift.volume", "Sala e mix", txt("Volume desde o 1º bloco") });
        v.push_back({ "drift.atraso", "Sala e mix", "Atraso do PA" });
        v.push_back({ "drift.dopa", "Sala e mix", txt("Quanto do som vem do PA") });
        v.push_back({ "drift.piso", "Sala e mix", txt("Piso entre músicas") });
        for (int r = 0; r < papagaio::kNumRegioes; ++r)
            v.push_back({ "drift.sala." + juce::String(r), txt("Sala, por região"), txt("Sala · ") + txt(papagaio::nomeDaRegiao(r)) });
        for (int r = 0; r < papagaio::kNumRegioes; ++r)
            v.push_back({ "drift.mix." + juce::String(r), txt("Mix, por região"), txt("Mix · ") + txt(papagaio::nomeDaRegiao(r)) });
        v.push_back({ "relogio", "", txt("Relógio") });
        return v;
    }();
    return valores;
}

struct Leitura {
    bool tem = false;
    double valor = 0;           // para comparar com o limite
    bool aceitaLimite = false;
    juce::String texto, unidade;
};

Leitura ler(const juce::String& id, const Contexto& c) {
    Leitura l;
    const auto partes = juce::StringArray::fromTokens(id, ".", "");
    const auto& d = c.inst.drift;

    if (partes[0] == "nivel" && partes.size() == 3) {
        const int curva = juce::String("ACZ").indexOf(partes[1]);
        if (curva < 0) return l;
        const auto& n = c.inst.niveis[(size_t) curva];
        // leq1, leq5, leq15 e maiorleq1... apontam para a janela de mesmo número de minutos
        const auto janela = [&](const juce::String& prefixo) {
            const int minutos = partes[2].fromFirstOccurrenceOf(prefixo, false, false).getIntValue();
            for (size_t j = 0; j < papagaio::kNumJanelasDeLeq; ++j)
                if (papagaio::kMinutosDaJanela[j] == minutos) return (int) j;
            return -1;
        };
        float v = -200.0f;
        if (partes[2].startsWith("maiorleq")) {
            if (const int j = janela("maiorleq"); j >= 0) v = n.maiorLeqJanela[(size_t) j];
        } else if (partes[2].startsWith("leq")) {
            if (const int j = janela("leq"); j >= 0) v = n.leqJanela[(size_t) j];
        } else {
            v = partes[2] == "agora" ? n.agora : partes[2] == "medio" ? n.medio : partes[2] == "max" ? n.maximo : n.minimo;
        }
        l.tem = v > -190.0f;
        l.valor = c.escala.mostrar(v);
        l.aceitaLimite = true;
        l.texto = semSinal(l.valor, 1);
        l.unidade = (c.escala.calibrado ? "dB(" : "dBFS(") + partes[1] + ")";
    } else if (id == "alerta.freq") {
        l.tem = c.inst.pior.estado != Estado::nenhum;
        l.texto = juce::String(juce::roundToInt(c.inst.pior.f));
        l.unidade = "Hz";
    } else if (id == "drift.volume") {
        l.tem = d.mixValido;
        l.texto = comSinal(d.volumeDb, 1);
        l.unidade = "dB";
    } else if (id == "drift.atraso") {
        l.tem = d.temAtraso;
        l.texto = numero(d.atrasoMs, 1);
        l.unidade = "ms";
    } else if (id == "drift.dopa") {
        l.tem = d.temNiveis;
        l.texto = numero(100.0 * d.coerenciaMedia, 0);
        l.unidade = "%";
    } else if (id == "drift.piso") {
        l.tem = d.temPiso;
        l.valor = c.escala.mostrar(d.pisoADb);
        l.aceitaLimite = true;
        l.texto = semSinal(l.valor, 0);
        l.unidade = c.escala.calibrado ? "dB(A)" : "dBFS(A)";
    } else if (partes[0] == "drift" && partes.size() == 3) {
        const size_t r = (size_t) juce::jlimit(0, papagaio::kNumRegioes - 1, partes[2].getIntValue());
        const bool sala = partes[1] == "sala";
        l.tem = sala ? d.salaValida[r] : d.mixValido;
        l.texto = comSinal(sala ? d.salaDb[r] : d.mixDb[r], 1);
        l.unidade = "dB";
    } else if (id == "relogio") {
        l.tem = true;
        l.texto = juce::Time::getCurrentTime().formatted("%H:%M:%S");
    }
    return l;
}

// Um número grande, à escolha: o engenheiro diz qual valor quer ver e de que tamanho.
class BlocoIndicador : public Bloco {
public:
    explicit BlocoIndicador(const Servicos& s) : Bloco(s) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    juce::String tipo() const override { return "indicador"; }
    juce::String titulo() const override { return "Indicador: " + nome(); }

    juce::String config() const override {
        return "valor=" + id_ + (temLimite_ ? ";limite=" + juce::String(limite_, 1) : juce::String());
    }
    void configurar(const juce::String& config) override {
        const auto pares = lerConfig(config);
        if (pares.containsKey("valor")) id_ = pares["valor"];
        temLimite_ = pares.containsKey("limite");
        limite_ = pares["limite"].getDoubleValue();
    }

    void atualizar(const Contexto& c) override {
        leitura_ = ler(id_, c);
        repaint();
    }

    void paint(juce::Graphics& g) override {
        pintarVidro(g, getLocalBounds().toFloat());
        auto a = getLocalBounds().reduced(14, 10);
        const float altura = (float) a.getHeight();

        const float alturaDoNome = juce::jlimit(12.0f, 20.0f, altura * 0.13f);
        g.setColour(cores::suave);
        g.setFont(fonte(alturaDoNome, true));
        g.drawText(nome(), a.removeFromTop((int) alturaDoNome + 4), juce::Justification::centredLeft);

        const bool vale = leitura_.aceitaLimite && temLimite_ && leitura_.tem;
        if (temLimite_ && leitura_.aceitaLimite) {
            g.setFont(fonte(juce::jlimit(11.0f, 15.0f, altura * 0.09f)));
            g.drawText("limite " + semSinal(limite_, 0), a.removeFromBottom(16), juce::Justification::centredRight);
        }

        // o número ocupa o que couber: limitado pela altura e pela largura do texto
        const juce::String texto = leitura_.tem ? leitura_.texto : txt("—");
        const juce::String unidade = leitura_.tem ? leitura_.unidade : juce::String();
        float tamanho = (float) a.getHeight() * 0.92f;
        const auto largura = [&](float t) {
            return juce::GlyphArrangement::getStringWidth(fonte(t, true), texto)
                   + (unidade.isEmpty() ? 0.0f : t * 0.12f + juce::GlyphArrangement::getStringWidth(fonte(t * 0.36f), unidade));
        };
        const float cabe = largura(tamanho);
        if (cabe > (float) a.getWidth()) tamanho *= (float) a.getWidth() / cabe;
        tamanho = juce::jmax(12.0f, tamanho);

        // estado do limite: vermelho acima, amarelo a 3 dB dele
        g.setColour(vale && leitura_.valor >= limite_ ? cores::vermelho : vale && leitura_.valor >= limite_ - 3.0 ? cores::amarelo
                                                                        : leitura_.tem ? cores::texto : cores::suave);
        const float total = largura(tamanho);
        const float larguraDoNumero = juce::GlyphArrangement::getStringWidth(fonte(tamanho, true), texto);
        auto linha = a.toFloat().withSizeKeepingCentre(total, tamanho);
        g.setFont(fonte(tamanho, true));
        g.drawText(texto, linha.withWidth(larguraDoNumero + 2.0f), juce::Justification::centredLeft, false);
        if (unidade.isNotEmpty()) {
            g.setColour(cores::suave);
            g.setFont(fonte(tamanho * 0.36f));
            // a unidade senta na linha de base do número
            g.drawText(unidade, linha.withTrimmedLeft(larguraDoNumero + tamanho * 0.12f).withTrimmedTop(tamanho * 0.50f).withTrimmedBottom(tamanho * 0.12f),
                       juce::Justification::bottomLeft, false);
        }
    }

    // Um clique abre a escolha do valor.
    void mouseUp(const juce::MouseEvent& e) override {
        if (!e.mouseWasDraggedSinceMouseDown()) menu();
    }

private:
    juce::String nome() const {
        for (const auto& v : catalogo())
            if (v.id == id_) return v.nome;
        return id_;
    }

    void menu() {
        juce::PopupMenu m, sub;
        juce::String grupo;
        const auto& valores = catalogo();
        const auto fecharGrupo = [&] {
            if (grupo.isNotEmpty()) m.addSubMenu(grupo, sub);
            sub = juce::PopupMenu();
        };
        for (int i = 0; i < (int) valores.size(); ++i) {
            const auto& v = valores[(size_t) i];
            if (v.grupo != grupo) {
                fecharGrupo();
                grupo = v.grupo;
            }
            (grupo.isEmpty() ? m : sub).addItem(i + 1, v.nome, true, v.id == id_);
        }
        fecharGrupo();
        m.addSeparator();
        m.addItem(1000, txt("Definir limite…"), leitura_.aceitaLimite);
        m.addItem(1001, "Tirar o limite", temLimite_);

        juce::Component::SafePointer<BlocoIndicador> eu(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [eu](int escolha) {
            if (eu == nullptr || escolha == 0) return;
            if (escolha == 1000) {
                eu->pedirLimite();
                return;
            }
            if (escolha == 1001)
                eu->temLimite_ = false;
            else
                eu->id_ = catalogo()[(size_t) (escolha - 1)].id;
            if (eu->aoMudar) eu->aoMudar();
            eu->repaint();
        });
    }

    void pedirLimite() {
        auto* janela = new juce::AlertWindow("Limite", txt("Acima desse valor o número fica vermelho; a 3 dB dele, amarelo."), juce::MessageBoxIconType::NoIcon);
        janela->addTextEditor("valor", temLimite_ ? numero(limite_, 1) : juce::String(), leitura_.unidade);
        janela->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        janela->addButton("Cancelar", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<BlocoIndicador> eu(this);
        janela->enterModalState(true, juce::ModalCallbackFunction::create([eu, janela](int resposta) {
            const auto texto = janela->getTextEditorContents("valor").trim().replaceCharacter(',', '.');
            if (resposta != 1 || eu == nullptr || texto.isEmpty()) return;
            eu->temLimite_ = true;
            eu->limite_ = texto.getDoubleValue();
            if (eu->aoMudar) eu->aoMudar();
            eu->repaint();
        }), true);
    }

    juce::String id_ = "nivel.A.agora";
    bool temLimite_ = false;
    double limite_ = 0;
    Leitura leitura_;
};

} // namespace

// ------------------------------------------------------------------ vista

namespace {

// Ids da caixa de pico. Os de retenção por tempo são os próprios segundos.
constexpr int kSemPico = 1, kIdPicoSegurado = 2, kZerarPico = 99;

int idDoPico(int segundos) { return segundos == 0 ? kSemPico : segundos < 0 ? kIdPicoSegurado : segundos; }
int segundosDoPico(int id) { return id == kSemPico ? 0 : id == kIdPicoSegurado ? Vista::kPicoSegurado : id; }

} // namespace

BlocoVista::BlocoVista(const Servicos& s) : Bloco(s) {
    const auto nomes = Vista::nomes();
    for (int i = 1; i < nomes.size(); ++i) comboTipo_.addItem(nomes[i], i);       // o id é o valor de Vista::Tipo
    for (const int f : { 1, 3, 6, 12, 24, 48 }) comboFracao_.addItem(f == 1 ? juce::String("1 oitava") : "1/" + juce::String(f) + " de oitava", f);
    comboFracao_.setTooltip(txt("Largura das bandas do RTA."));
    comboMedia_.addItem(txt("Sem média"), 1);
    comboMedia_.addItem(txt("Média de 1 s"), 2);
    comboMedia_.addItem(txt("Média de 3 s"), 3);
    comboMedia_.addItem(txt("Média de 10 s"), 4);
    comboMedia_.setTooltip(txt("Média no tempo do RTA. Vale para todos os RTAs da tela."));
    comboPico_.addItem(txt("Sem pico"), kSemPico);
    comboPico_.addItem(txt("Pico segurado"), kIdPicoSegurado);
    comboPico_.addItem(txt("Pico de 3 s"), 3);
    comboPico_.addItem(txt("Pico de 10 s"), 10);
    comboPico_.addSeparator();
    comboPico_.addItem(txt("Zerar o pico"), kZerarPico);
    comboPico_.setTooltip(txt("Retenção de pico do RTA: segura o maior nível de cada banda, para sempre ou por alguns segundos."));
    comboTipo_.setSelectedId((int) Vista::Tipo::espectro, juce::dontSendNotification);
    comboFracao_.setSelectedId(3, juce::dontSendNotification);
    comboPico_.setSelectedId(kSemPico, juce::dontSendNotification);

    comboTipo_.onChange = [this] { aplicar(true); };
    comboFracao_.onChange = [this] { aplicar(true); };
    comboMedia_.onChange = [this] { servicos_.definirMedia(comboMedia_.getSelectedId()); };
    comboPico_.onChange = [this] {
        if (comboPico_.getSelectedId() == kZerarPico) {
            // zerar é uma ação, não um estado: a caixa volta a mostrar a retenção que estava
            vista_.zerarPico();
            comboPico_.setSelectedId(idDoPico(vista_.pico()), juce::dontSendNotification);
            return;
        }
        aplicar(true);
    };
    vista_.usarReferencia(servicos_.referencia);
    vista_.usarEquilibrio(servicos_.equilibrio);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &comboTipo_, &comboFracao_, &comboMedia_, &comboPico_, &vista_ }) addAndMakeVisible(c);
    aplicar(false);
}

juce::String BlocoVista::titulo() const { return "Vista: " + comboTipo_.getText(); }

void BlocoVista::aplicar(bool avisar) {
    vista_.definirTipo((Vista::Tipo) comboTipo_.getSelectedId());
    vista_.definirFracao(comboFracao_.getSelectedId());
    vista_.definirPico(segundosDoPico(comboPico_.getSelectedId()));
    // banda, média e pico só valem para o RTA
    const bool rta = vista_.tipo() == Vista::Tipo::rta;
    comboFracao_.setVisible(rta);
    comboMedia_.setVisible(rta);
    comboPico_.setVisible(rta);
    if (avisar && aoMudar) aoMudar();
}

void BlocoVista::definirVista(Vista::Tipo tipo) {
    comboTipo_.setSelectedId((int) tipo, juce::dontSendNotification);
    aplicar(true);
}

void BlocoVista::atualizar(const Contexto& c) {
    if (!comboMedia_.isPopupActive()) comboMedia_.setSelectedId(servicos_.media(), juce::dontSendNotification);
    vista_.mostrar(c.inst, *servicos_.motor);
}

juce::String BlocoVista::config() const {
    static const char* chaves[] = { "nenhuma", "espectro", "rta", "espectrograma", "transferencia", "equilibrio" };
    return juce::String("tipo=") + chaves[juce::jlimit(1, 5, comboTipo_.getSelectedId())] + ";fracao=" + juce::String(comboFracao_.getSelectedId())
           + (vista_.pico() != 0 ? ";pico=" + juce::String(vista_.pico()) : juce::String());
}

void BlocoVista::configurar(const juce::String& config) {
    const auto pares = lerConfig(config);
    const juce::StringArray chaves { "nenhuma", "espectro", "rta", "espectrograma", "transferencia", "equilibrio" };
    if (chaves.indexOf(pares["tipo"]) > 0) comboTipo_.setSelectedId(chaves.indexOf(pares["tipo"]), juce::dontSendNotification);
    const int fracao = pares["fracao"].getIntValue();
    if (comboFracao_.indexOfItemId(fracao) >= 0) comboFracao_.setSelectedId(fracao, juce::dontSendNotification);
    const int pico = pares["pico"].getIntValue();
    if (comboPico_.indexOfItemId(idDoPico(pico)) >= 0) comboPico_.setSelectedId(idDoPico(pico), juce::dontSendNotification);
    aplicar(false);
}

void BlocoVista::resized() {
    auto a = getLocalBounds();
    auto seletores = a.removeFromTop(26);
    const int largura = juce::jmin(190, (seletores.getWidth() - 24) / 4);
    comboTipo_.setBounds(seletores.removeFromLeft(largura));
    seletores.removeFromLeft(8);
    comboFracao_.setBounds(seletores.removeFromLeft(largura));
    seletores.removeFromLeft(8);
    comboMedia_.setBounds(seletores.removeFromLeft(largura));
    seletores.removeFromLeft(8);
    comboPico_.setBounds(seletores.removeFromLeft(largura));
    a.removeFromTop(6);
    vista_.setBounds(a);
}

// ------------------------------------------------------------------ fábrica

const std::vector<TipoDeBloco>& tiposDeBloco() {
    static const std::vector<TipoDeBloco> tipos = {
        { "indicador", "Indicador", 12, 6 },
        { "vista", "Vista (espectro, RTA, espectrograma, transferência)", 24, 12 },
        { "niveis", "Níveis", 16, 8 },
        { "salaemix", "Sala e mix", 16, 11 },
        { "alerta", "Faixa de alerta", 32, 7 },
        { "historico", "Histórico", 32, 9 },
        { "mapa", "Mapa de risco (ring-out)", 16, 10 },
    };
    return tipos;
}

std::unique_ptr<Bloco> criarBloco(const juce::String& tipo, const Servicos& s) {
    if (tipo == "alerta") return std::make_unique<BlocoAlerta>(s);
    if (tipo == "mapa") return std::make_unique<BlocoMapa>(s);
    if (tipo == "vista") return std::make_unique<BlocoVista>(s);
    if (tipo == "niveis") return std::make_unique<BlocoNiveis>(s);
    if (tipo == "salaemix") return std::make_unique<BlocoSalaEMix>(s);
    if (tipo == "historico") return std::make_unique<BlocoHistorico>(s);
    if (tipo == "indicador") return std::make_unique<BlocoIndicador>(s);
    return nullptr;
}
