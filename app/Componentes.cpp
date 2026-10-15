#include "Componentes.h"

#include <cmath>

using papagaio::Estado;

namespace {

const juce::Colour kTinta = cores::tinta;      // texto sobre a faixa colorida

juce::String comSinal(double v, int casas) {
    return (v < 0 ? txt("−") : juce::String("+")) + numero(std::abs(v), casas);
}

} // namespace

void FaixaDeAlerta::mostrar(const Dados& d) {
    d_ = d;
    repaint();
}

void FaixaDeAlerta::paint(juce::Graphics& g) {
    const Estado e = d_.pior.estado;
    const bool interrompe = e >= Estado::laranja;
    if (interrompe) {
        // alerta não é vidro: cor cheia, para ser visto de longe
        const auto r = getLocalBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(cores::doEstado(e).brighter(0.12f), 0.0f, 0.0f, cores::doEstado(e).darker(0.12f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(r, 14.0f);
    } else {
        pintarVidro(g, getLocalBounds().toFloat());
    }

    auto dentro = getLocalBounds().reduced(22, 14);
    const float h = (float) dentro.getHeight();

    // contadores do show, à direita
    auto coluna = dentro.removeFromRight(juce::jmin(150, dentro.getWidth() / 4));
    const int linha = juce::jmax(18, coluna.getHeight() / 4);
    const auto contador = [&](int n, const char* nome, juce::Colour cor) {
        auto r = coluna.removeFromTop(linha);
        g.setColour(interrompe ? kTinta : cor);
        g.setFont(fonte(juce::jmin(26.0f, linha * 0.8f), true));
        g.drawText(juce::String(n), r.removeFromLeft(48), juce::Justification::centredRight);
        g.setColour(interrompe ? kTinta.withAlpha(0.75f) : cores::suave);
        g.setFont(fonte(15.0f));
        g.drawText(txt(nome), r.withTrimmedLeft(8), juce::Justification::centredLeft);
    };
    contador(d_.vermelhos, "vermelho", cores::vermelho);
    contador(d_.laranjas, "laranja", cores::laranja);
    contador(d_.amarelos, "amarelo", cores::amarelo);
    contador(d_.drifts, "drift", cores::suave);

    if (interrompe) {
        const juce::String banda = juce::String(papagaio::bandaGeq(d_.pior.f));
        g.setColour(kTinta);
        g.setFont(fonte(juce::jmax(14.0f, h * 0.11f), true));
        g.drawText(e == Estado::vermelho ? "APITO" : txt("RESSONÂNCIA"), dentro.removeFromTop((int) (h * 0.14f)), juce::Justification::centredLeft);

        g.setFont(fonte(h * 0.50f, true));
        g.drawText(juce::String(juce::roundToInt(d_.pior.f)) + " Hz", dentro.removeFromTop((int) (h * 0.46f)), juce::Justification::centredLeft);

        // Agora e depois: primeiro a ação mais rápida, depois a correção.
        juce::String acao;
        if (e == Estado::vermelho) acao = "Agora: baixe o ganho.  ";
        if (d_.voltou >= 2)
            acao += txt("Já voltou ") + juce::String(d_.voltou) + txt("×: EQ não segura, reveja posição ou ganho.");
        else
            acao += (e == Estado::vermelho ? juce::String("Depois: corte em ") : juce::String("Corte em ")) + banda + " no GEQ.";
        g.setFont(fonte(juce::jmax(16.0f, h * 0.15f), true));
        g.drawFittedText(acao, dentro.removeFromTop((int) (h * 0.22f)), juce::Justification::centredLeft, 1);

        g.setColour(kTinta.withAlpha(0.8f));
        g.setFont(fonte(juce::jmax(13.0f, h * 0.11f)));
        g.drawText(comSinal(d_.pior.inclinacaoDbPorS, 0) + txt(" dB/s  ·  há ") + numero(d_.pior.duracao, 1) + txt(" s  ·  ")
                       + d_.escala.texto(d_.pior.nivelDb, 0)
                       + (d_.gravandoRingOut ? txt("  ·  ring-out: entra no mapa") : d_.pior.risco ? txt("  ·  frequência do ring-out") : juce::String())
                       + txt("  ·  provável"),
                   dentro, juce::Justification::centredLeft);
        return;
    }

    juce::String titulo = "Sem alerta", detalhe;
    juce::Colour cor = cores::suave;
    if (e == Estado::amarelo && d_.pior.preApito) {
        // o anel dessa frequência está ficando mais longo a cada ataque: ganho de malha chegando no limite
        titulo = txt("Pré-apito em ") + juce::String(juce::roundToInt(d_.pior.f)) + " Hz";
        detalhe = txt("anel caindo ") + numero(d_.pior.decaiDbPorS, 0) + txt(" dB/s  ·  GEQ ") + juce::String(papagaio::bandaGeq(d_.pior.f))
                  + (d_.pior.risco ? txt("  ·  frequência do ring-out") : juce::String()) + txt("  ·  provável  ·  não interrompe");
        cor = cores::amarelo;
    } else if (e == Estado::amarelo) {
        titulo = txt("Instável em ") + juce::String(juce::roundToInt(d_.pior.f)) + " Hz";
        detalhe = "GEQ " + juce::String(papagaio::bandaGeq(d_.pior.f)) + (d_.pior.risco ? txt("  ·  frequência do ring-out") : juce::String())
                  + txt("  ·  só no histórico, não interrompe");
        cor = cores::amarelo;
    } else if (d_.gravandoRingOut) {
        const int s = juce::jmax(0, (int) d_.segundosDeRingOut);
        titulo = "Gravando ring-out";
        detalhe = txt("Suba o ganho devagar: cada apito entra no mapa de risco  ·  ") + juce::String(s / 60) + ":"
                  + juce::String(s % 60).paddedLeft('0', 2);
        cor = cores::amarelo;
    } else if (d_.fimDoArquivo) {
        titulo = "Fim do arquivo";
    } else if (!d_.temSinal) {
        titulo = "Sem sinal";
        detalhe = txt("Escolha a placa em Áudio… ou simule com um arquivo.");
    }
    g.setColour(cor);
    g.setFont(fonte(h * 0.30f, true));
    g.drawText(titulo, dentro.removeFromTop((int) (h * 0.62f)), juce::Justification::bottomLeft);
    g.setColour(cores::suave);
    g.setFont(fonte(juce::jmax(13.0f, h * 0.11f)));
    g.drawText(detalhe, dentro.withTrimmedTop(6), juce::Justification::topLeft);
}

PainelDeNiveis::PainelDeNiveis() {
    comboTempo.addItem(txt("Rápido"), 1);
    comboTempo.addItem("Lento", 2);
    comboTempo.setSelectedId(1, juce::dontSendNotification);
    comboTempo.setTooltip(txt("Ponderação de tempo do valor de agora, do máximo e do mínimo: rápida (125 ms) ou lenta (1 s)."));
    botaoZerar.setButtonText("Zerar");
    botaoZerar.setTooltip(txt("Recomeça o médio, o máximo e o mínimo."));
    botaoCalibrar.setButtonText(txt("Calibrar…"));
    botaoCalibrar.setTooltip(txt("Diz ao Papagaio quantos dB SPL o mic está ouvindo. Sem isso os níveis ficam em dBFS."));
    for (juce::Component* c : std::initializer_list<juce::Component*> { &comboTempo, &botaoZerar, &botaoCalibrar }) addAndMakeVisible(c);
}

void PainelDeNiveis::mostrar(const Instantaneo& i) {
    i_ = i;
    // sem calibração, o botão de calibrar é a coisa mais importante do painel
    botaoCalibrar.setColour(juce::TextButton::buttonColourId, i.calibrado ? cores::botao : cores::amarelo);
    botaoCalibrar.setColour(juce::TextButton::textColourOffId, i.calibrado ? cores::texto : cores::tinta);
    repaint();
}

void PainelDeNiveis::resized() {
    auto r = getLocalBounds().reduced(12, 10).removeFromBottom(26);
    comboTempo.setBounds(r.removeFromLeft(92));
    r.removeFromLeft(6);
    botaoZerar.setBounds(r.removeFromLeft(62));
    botaoCalibrar.setBounds(r.removeFromRight(104));
}

void PainelDeNiveis::paint(juce::Graphics& g) {
    pintarVidro(g, getLocalBounds().toFloat());
    auto a = getLocalBounds().reduced(12, 10);
    a.removeFromBottom(26 + 8);
    const Escala escala = escalaDe(i_);

    auto topo = a.removeFromTop(18);
    g.setColour(cores::texto);
    g.setFont(fonte(13.0f, true));
    g.drawText(txt("NÍVEIS"), topo, juce::Justification::centredLeft);
    const int s = (int) i_.segundosDeNivel;
    const juce::String tempo = txt("  ·  ") + juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
    g.setFont(fonte(12.0f, !escala.calibrado));
    g.setColour(escala.calibrado ? cores::suave : cores::amarelo);
    g.drawText((escala.calibrado ? juce::String("dB SPL") : txt("dBFS: mic sem calibração")) + tempo, topo, juce::Justification::centredRight);

    const int larguraDoNome = 26;
    const int coluna = (a.getWidth() - larguraDoNome) / 4;
    auto cabecalho = a.removeFromTop(16);
    cabecalho.removeFromLeft(larguraDoNome);
    g.setColour(cores::suave);
    g.setFont(fonte(12.0f));
    for (const char* nome : { "agora", "médio", "máx.", "mín." }) g.drawText(txt(nome), cabecalho.removeFromLeft(coluna), juce::Justification::centredRight);

    // Leq deslizante da curva A, embaixo, quando sobra altura: é o número que a casa e a norma cobram.
    if (a.getHeight() >= 3 * 18 + 22) {
        auto linha = a.removeFromBottom(18);
        const auto& l = i_.niveis[(size_t) papagaio::Ponderacao::A];
        g.setFont(fonte(12.0f));
        g.setColour(cores::suave);
        g.drawText("Leq(A)", linha.removeFromLeft(larguraDoNome + 30), juce::Justification::centredLeft);
        const int largura = linha.getWidth() / papagaio::kNumJanelasDeLeq;
        for (size_t j = 0; j < papagaio::kNumJanelasDeLeq; ++j) {
            // "1 min 96,2": o rótulo encostado no seu valor, com folga antes da janela seguinte
            auto celula = linha.removeFromLeft(largura).withTrimmedLeft(8);
            const float v = l.leqJanela[j];
            const double db = escala.mostrar(v);
            const juce::String valor = v <= -190.0f ? txt("—") : (db < 0 ? txt("−") : juce::String()) + numero(std::abs(db), 1);
            g.setColour(cores::suave);
            g.setFont(fonte(12.0f));
            g.drawText(juce::String(papagaio::kMinutosDaJanela[j]) + " min", celula.removeFromLeft(40), juce::Justification::centredLeft);
            // janela ainda enchendo: o valor é do que passou até agora, um tom abaixo
            g.setColour(l.janelaCheia[j] ? cores::texto : cores::suave.brighter(0.35f));
            g.setFont(fonte(14.0f, l.janelaCheia[j]));
            g.drawText(valor, celula, juce::Justification::centredLeft);
        }
        a.removeFromBottom(4);
    }

    const char* curvas[] = { "A", "C", "Z" };
    const int altura = juce::jlimit(16, 26, a.getHeight() / papagaio::kNumPonderacoes);
    for (size_t w = 0; w < papagaio::kNumPonderacoes; ++w) {
        auto linha = a.removeFromTop(altura);
        g.setColour(cores::suave);
        g.setFont(fonte(14.0f, true));
        g.drawText(curvas[w], linha.removeFromLeft(larguraDoNome), juce::Justification::centredLeft);
        const auto& l = i_.niveis[w];
        int n = 0;
        for (const float v : { l.agora, l.medio, l.maximo, l.minimo }) {
            // o valor de agora é o que se lê de relance; os outros ficam um tom abaixo
            g.setColour(n == 0 ? cores::texto : cores::suave.brighter(0.35f));
            g.setFont(fonte(15.0f, n == 0));
            const double db = escala.mostrar(v);
            g.drawText(v <= -190.0f ? txt("—") : (db < 0 ? txt("−") : juce::String()) + numero(std::abs(db), 1), linha.removeFromLeft(coluna),
                       juce::Justification::centredRight);
            ++n;
        }
    }
}

PainelDeDrift::PainelDeDrift() {
    botaoShow.setButtonText(txt("Começou o show"));
    botaoPassagem.setTooltip(txt("Com o sistema tocando na passagem de som, grave a linha de base da sala. Um minuto ou mais de música."));
    botaoShow.setTooltip(txt("Marca o primeiro bloco do show: é contra ele que o mix e o volume são comparados."));
    addAndMakeVisible(botaoPassagem);
    addAndMakeVisible(botaoShow);
}

void PainelDeDrift::mostrar(const Instantaneo& i) {
    i_ = i;
    botaoPassagem.setButtonText(i.drift.gravandoPassagem ? "Fechar passagem" : "Gravar passagem");
    botaoPassagem.setEnabled(i.temLr);
    botaoShow.setEnabled(i.temLr && !i.drift.gravandoPassagem);
    repaint();
}

void PainelDeDrift::resized() {
    auto r = getLocalBounds().reduced(12, 10).removeFromBottom(26);
    botaoPassagem.setBounds(r.removeFromLeft((r.getWidth() - 8) / 2));
    r.removeFromLeft(8);
    botaoShow.setBounds(r);
}

juce::String PainelDeDrift::situacao() const {
    const auto minSeg = [](double s) { return juce::String((int) s / 60) + ":" + juce::String((int) s % 60).paddedLeft('0', 2); };
    const auto& d = i_.drift;
    if (!i_.atrasoTravado) return txt("Procurando o atraso entre a mesa e o mic…");
    if (d.gravandoPassagem) return txt("Gravando a passagem: ") + minSeg(d.segundosDePassagem) + txt(" de medição");
    juce::String s = d.temBase ? "Sala contra " + minSeg(d.segundosDePassagem) + " de passagem" : juce::String("Sala sem linha de base");
    if (d.temReferencia)
        s += txt("  ·  mix contra o 1º bloco");
    else if (d.segundosDeReferencia > 0)
        s += txt("  ·  1º bloco em ") + minSeg(d.segundosDeReferencia);
    else
        s += txt("  ·  mix sem referência");
    return s;
}

void PainelDeDrift::paint(juce::Graphics& g) {
    constexpr float kEscalaDb = 8.0f;
    constexpr int kLarguraDoNome = 84, kLarguraDoValor = 46;

    pintarVidro(g, getLocalBounds().toFloat());
    auto a = getLocalBounds().reduced(12, 10);
    a.removeFromBottom(26 + 8);

    // título, com a legenda em cima das duas colunas de valor
    auto topo = a.removeFromTop(18);
    g.setColour(cores::texto);
    g.setFont(fonte(13.0f, true));
    g.drawText("SALA E MIX", topo, juce::Justification::centredLeft);
    g.setFont(fonte(12.0f));
    const auto legenda = [&](juce::Rectangle<int> r, juce::Colour cor, const char* nome) {
        g.setColour(cor);
        g.fillEllipse((float) r.getRight() - 40.0f, (float) r.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour(cores::suave);
        g.drawText(nome, r.withTrimmedLeft(r.getWidth() - 28), juce::Justification::centredLeft);
    };
    legenda(topo.removeFromRight(kLarguraDoValor), cores::mix, "mix");
    legenda(topo.removeFromRight(kLarguraDoValor), cores::sala, "sala");

    g.setColour(cores::suave);
    if (!i_.temLr) {
        g.setFont(fonte(14.0f));
        g.drawFittedText(txt("Escolha o L/R da mesa para separar o que mudou na sala do que mudou no mix."), a.reduced(10, 0),
                         juce::Justification::centred, 3);
        return;
    }
    g.drawText(situacao(), a.removeFromTop(18), juce::Justification::centredLeft);
    a.removeFromTop(4);
    auto rodape = a.removeFromBottom(36);

    const auto& d = i_.drift;
    const float limiar = (float) i_.limiarDeDriftDb;
    const int altura = juce::jlimit(14, 26, a.getHeight() / papagaio::kNumRegioes);
    for (int r = 0; r < papagaio::kNumRegioes; ++r) {
        auto linha = a.removeFromTop(altura);
        const bool temSala = d.salaValida[(size_t) r], temMix = d.mixValido;
        const float sala = d.salaDb[(size_t) r], mix = d.mixDb[(size_t) r];

        g.setFont(fonte(13.0f));
        const auto valor = [&](juce::Rectangle<int> celula, bool tem, float v) {
            // fora da tolerância o número ganha a cor do texto; dentro, fica discreto
            g.setColour(tem && std::abs(v) >= limiar ? cores::texto : cores::suave);
            g.drawText(tem ? comSinal(v, 1) : txt("—"), celula, juce::Justification::centredRight);
        };
        valor(linha.removeFromRight(kLarguraDoValor), temMix, mix);
        valor(linha.removeFromRight(kLarguraDoValor), temSala, sala);
        g.setColour(cores::suave);
        g.drawText(txt(papagaio::nomeDaRegiao(r)), linha.removeFromLeft(kLarguraDoNome), juce::Justification::centredLeft);

        const auto barras = linha.toFloat().reduced(6.0f, 3.0f);
        const float centro = barras.getCentreX(), meia = barras.getWidth() * 0.5f;
        g.setColour(cores::borda.withAlpha(0.55f));
        g.fillRect(centro - meia * limiar / kEscalaDb, barras.getY(), 2.0f * meia * limiar / kEscalaDb, barras.getHeight());
        g.setColour(cores::suave.withAlpha(0.6f));
        g.fillRect(centro - 0.5f, barras.getY() - 1.0f, 1.0f, barras.getHeight() + 2.0f);
        const float fina = (barras.getHeight() - 2.0f) * 0.5f;
        const auto barra = [&](bool tem, float v, juce::Colour cor, float y) {
            if (!tem) return;
            const float w = meia * juce::jlimit(-kEscalaDb, kEscalaDb, v) / kEscalaDb;
            g.setColour(cor);
            g.fillRoundedRectangle(juce::jmin(centro, centro + w), y, juce::jmax(1.5f, std::abs(w)), fina, juce::jmin(2.0f, fina * 0.5f));
        };
        barra(temSala, sala, cores::sala, barras.getY());
        barra(temMix, mix, cores::mix, barras.getY() + fina + 2.0f);
    }

    g.setFont(fonte(13.0f));
    g.setColour(cores::suave);
    juce::String primeira = "Volume " + (d.mixValido ? comSinal(d.volumeDb, 1) + " dB" : txt("—"));
    if (d.temAtraso) {
        primeira += txt("  ·  atraso ") + numero(d.atrasoMs, 1) + " ms";
        if (d.temAtrasoBase && !d.gravandoPassagem) primeira += " (" + comSinal(d.deltaAtrasoMs, 1) + ")";
    }
    juce::String segunda;
    if (d.temNiveis) segunda = "Do PA: " + numero(100.0 * d.coerenciaMedia, 0) + " % do que chega";
    if (d.temPiso) {
        const Escala escala = escalaDe(i_);
        segunda += txt(segunda.isEmpty() ? "Piso " : "  ·  piso ")
                   + (escala.calibrado ? numero(escala.mostrar(d.pisoADb), 0) + " dB(A)" : comSinal(d.pisoADb, 0) + " dBFS(A)");
    }
    g.drawText(primeira, rodape.removeFromTop(18), juce::Justification::centredLeft);
    g.drawText(segunda, rodape, juce::Justification::centredLeft);
}

void Medidor::mostrar(float picoDb) {
    if (std::abs(picoDb - db_) < 0.2f) return;
    db_ = picoDb;
    repaint();
}

void Medidor::paint(juce::Graphics& g) {
    const auto r = getLocalBounds().toFloat().reduced(0.0f, getHeight() * 0.3f);
    g.setColour(cores::borda);
    g.fillRoundedRectangle(r, 3.0f);
    const float parte = juce::jlimit(0.0f, 1.0f, (db_ + 60.0f) / 60.0f);
    g.setColour(db_ > -3.0f ? cores::vermelho : db_ > -12.0f ? cores::amarelo : cores::primaria);
    g.fillRoundedRectangle(r.withWidth(r.getWidth() * parte), 3.0f);
}
