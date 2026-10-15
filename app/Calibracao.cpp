#include "Calibracao.h"

#include <cmath>

namespace {

constexpr int kMargem = 20, kLinha = 28;
// onde começa cada bloco da janela
constexpr int kYLeitura = 16, kYCalibrador = 104, kYDecibelimetro = 204, kYDados = 304, kYValor = 420, kYRodape = 514;

double lerNumero(const juce::TextEditor& campo, bool& ok) {
    const auto texto = campo.getText().trim().replaceCharacter(',', '.');
    ok = texto.isNotEmpty() && texto.containsOnly("0123456789.-+");
    return texto.getDoubleValue();
}

} // namespace

DialogoDeCalibracao::DialogoDeCalibracao(MotorAoVivo& motor, Escala escala, std::function<void(bool, double)> aoAplicar)
    : motor_(motor), escala_(escala), aoAplicar_(std::move(aoAplicar)) {
    comboCalibrador_.addItem("94 dB", 94);
    comboCalibrador_.addItem("114 dB", 114);
    comboCalibrador_.setSelectedId(94, juce::dontSendNotification);
    comboCurva_.addItem("dB(A)", 1);
    comboCurva_.addItem("dB(C)", 2);
    comboCurva_.addItem("dB(Z)", 3);
    comboCurva_.setSelectedId(2, juce::dontSendNotification);

    botaoTom_.setButtonText("Calibrar pelo tom");
    botaoTom_.onClick = [this] { peloCalibrador(); };
    botaoIgualar_.setButtonText("Igualar");
    botaoIgualar_.onClick = [this] { peloDecibelimetro(); };
    botaoValor_.setButtonText("Aplicar");
    botaoValor_.onClick = [this] { peloValor(); };
    botaoDados_.setButtonText("Calcular");
    botaoDados_.onClick = [this] { pelosDados(); };
    botaoRemover_.setButtonText(txt("Remover calibração"));
    botaoRemover_.onClick = [this] { aplicar(false, 0.0); };

    for (auto* campo : { &campoLeitura_, &campoValor_, &campoSensibilidade_, &campoEntrada_, &campoGanho_ }) {
        campo->setJustification(juce::Justification::centred);
        campo->setFont(fonte(15.0f));
        campo->setInputRestrictions(7, "0123456789,.-");
    }
    campoLeitura_.setTextToShowWhenEmpty("ex.: 85", cores::suave);
    campoValor_.setTextToShowWhenEmpty("ex.: 124", cores::suave);
    campoSensibilidade_.setTextToShowWhenEmpty("ex.: 6", cores::suave);
    campoEntrada_.setTextToShowWhenEmpty("ex.: 12", cores::suave);
    campoGanho_.setTextToShowWhenEmpty("ex.: 30", cores::suave);
    campoSensibilidade_.setTooltip(txt("Sensibilidade do mic em mV/Pa, da ficha técnica. Se vier em dBV/Pa: mV/Pa = 1000 × 10^(dBV/20)."));
    campoEntrada_.setTooltip(txt("Nível máximo de entrada da placa ou da mesa, em dBu, com o ganho no mínimo. Está na ficha técnica como \"max input level\"."));
    campoGanho_.setTooltip(txt("Quantos dB de ganho o pré do mic está dando agora."));
    if (escala_.calibrado) campoValor_.setText(numero(escala_.calibracaoDb, 1), false);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &comboCalibrador_, &botaoTom_, &campoLeitura_, &comboCurva_, &botaoIgualar_,
                                                                       &campoValor_, &botaoValor_, &campoSensibilidade_, &campoEntrada_, &campoGanho_,
                                                                       &botaoDados_, &botaoRemover_ })
        addAndMakeVisible(c);

    motor_.pausarDeteccao(true);
    setSize(500, 562);
    startTimerHz(30);
}

DialogoDeCalibracao::~DialogoDeCalibracao() { motor_.pausarDeteccao(false); }

void DialogoDeCalibracao::resized() {
    const auto linha = [this](int y) { return juce::Rectangle<int>(kMargem, y + 54, getWidth() - 2 * kMargem, kLinha); };

    auto a = linha(kYCalibrador);
    comboCalibrador_.setBounds(a.removeFromLeft(110));
    a.removeFromLeft(10);
    botaoTom_.setBounds(a.removeFromLeft(170));

    a = linha(kYDecibelimetro);
    campoLeitura_.setBounds(a.removeFromLeft(90));
    a.removeFromLeft(10);
    comboCurva_.setBounds(a.removeFromLeft(90));
    a.removeFromLeft(10);
    botaoIgualar_.setBounds(a.removeFromLeft(110));

    // três campos, cada um com o seu rótulo desenhado em cima
    a = linha(kYDados).translated(0, 18);
    campoSensibilidade_.setBounds(a.removeFromLeft(90));
    a.removeFromLeft(14);
    campoEntrada_.setBounds(a.removeFromLeft(90));
    a.removeFromLeft(14);
    campoGanho_.setBounds(a.removeFromLeft(90));
    a.removeFromLeft(14);
    botaoDados_.setBounds(a.removeFromLeft(110));

    a = linha(kYValor);
    campoValor_.setBounds(a.removeFromLeft(90));
    a.removeFromLeft(70);       // aqui a tela escreve "dB SPL"
    botaoValor_.setBounds(a.removeFromLeft(110));

    botaoRemover_.setBounds(getWidth() - kMargem - 170, kYRodape + 8, 170, kLinha);
}

void DialogoDeCalibracao::atualizar() {
    const auto inst = motor_.instantaneo();
    if (inst.niveis[2].agora > -190.0f) {
        for (size_t w = 0; w < papagaio::kNumPonderacoes; ++w) potencias_[w][(size_t) pos_] = std::pow(10.0, inst.niveis[w].agora / 10.0);
        pos_ = (pos_ + 1) % kJanela;
        amostras_ = juce::jmin(kJanela, amostras_ + 1);
        for (size_t w = 0; w < papagaio::kNumPonderacoes; ++w) {
            double soma = 0;
            for (int i = 0; i < amostras_; ++i) soma += potencias_[w][(size_t) i];
            media_[w] = 10.0 * std::log10(soma / amostras_ + 1e-20);
        }
    }
    botaoRemover_.setEnabled(escala_.calibrado);
    repaint();
}

void DialogoDeCalibracao::aplicar(bool tem, double db) {
    escala_.calibrado = tem;
    escala_.calibracaoDb = tem ? db : 0.0;
    aviso_.clear();
    campoValor_.setText(tem ? numero(db, 1) : juce::String(), false);
    if (aoAplicar_) aoAplicar_(tem, escala_.calibracaoDb);
}

void DialogoDeCalibracao::peloCalibrador() {
    if (!temLeitura()) {
        aviso_ = txt("Sem sinal no canal do mic.");
        return;
    }
    const double a = media_[0], c = media_[1], z = media_[2];
    // Em 1 kHz as três curvas valem zero: um tom de calibrador lê igual em A, C e Z.
    if (std::abs(a - z) > 1.0 || std::abs(c - z) > 1.0) {
        aviso_ = txt("Isso não parece o tom de 1 kHz do calibrador: A, C e Z deveriam ler quase igual. Confira o encaixe e o ruído em volta.");
        return;
    }
    if (z < -70.0) {
        aviso_ = txt("O tom está chegando muito baixo. Confira o canal do mic e o ganho do pré.");
        return;
    }
    aplicar(true, comboCalibrador_.getSelectedId() - z);
}

void DialogoDeCalibracao::peloDecibelimetro() {
    bool ok = false;
    const double leitura = lerNumero(campoLeitura_, ok);
    if (!ok) {
        aviso_ = txt("Digite o que o decibelímetro está mostrando.");
        return;
    }
    if (!temLeitura()) {
        aviso_ = txt("Sem sinal no canal do mic.");
        return;
    }
    aplicar(true, leitura - media_[(size_t) (comboCurva_.getSelectedId() - 1)]);
}

// Sem aparelho nenhum: a conta que o calibrador faria, com os números da ficha técnica.
// 94 dB SPL é 1 Pa; o mic entrega a sensibilidade dele em mV, e a placa lê isso contra o fundo de escala.
void DialogoDeCalibracao::pelosDados() {
    bool temSens = false, temEntrada = false, temGanho = false;
    const double sensibilidade = lerNumero(campoSensibilidade_, temSens);
    const double entradaMaxima = lerNumero(campoEntrada_, temEntrada);
    const double ganho = lerNumero(campoGanho_, temGanho);
    if (!temSens || !temEntrada || !temGanho || sensibilidade <= 0) {
        aviso_ = txt("Preencha os três campos: sensibilidade do mic, entrada máxima da placa e ganho do pré.");
        return;
    }
    const double dbuDoMic = 20.0 * std::log10(sensibilidade / 1000.0 / 0.7746);      // 0 dBu = 0,7746 V
    const double leitura = dbuDoMic - (entradaMaxima - ganho);                       // em 94 dB SPL, relativo ao fundo de escala
    aplicar(true, 94.0 - leitura);
    aviso_ = txt("Calibrado pela ficha técnica: serve de aproximação, com alguns dB de erro. Confira com um calibrador quando puder.");
}

void DialogoDeCalibracao::peloValor() {
    bool ok = false;
    const double valor = lerNumero(campoValor_, ok);
    if (!ok) {
        aviso_ = txt("Digite quantos dB SPL valem o fundo de escala.");
        return;
    }
    aplicar(true, valor);
}

void DialogoDeCalibracao::paint(juce::Graphics& g) {
    g.fillAll(cores::cartao);
    const int largura = getWidth() - 2 * kMargem;

    // leitura de agora, na unidade em que o app está
    g.setColour(cores::suave);
    g.setFont(fonte(13.0f));
    g.drawText(txt("Leitura do mic agora, média de 2 s") + txt("  ·  ") + escala_.unidade(), kMargem, kYLeitura, largura, 18, juce::Justification::centredLeft);
    const char* curvas[] = { "A", "C", "Z" };
    for (int w = 0; w < papagaio::kNumPonderacoes; ++w) {
        const int x = kMargem + w * 150;
        g.setColour(cores::suave);
        g.setFont(fonte(18.0f, true));
        g.drawText(curvas[w], x, kYLeitura + 24, 24, 36, juce::Justification::centredLeft);
        g.setColour(cores::texto);
        g.setFont(fonte(30.0f, true));
        const double v = escala_.mostrar(media_[(size_t) w]);
        g.drawText(temLeitura() ? (v < 0 ? txt("−") : juce::String()) + numero(std::abs(v), 1) : txt("—"), x + 24, kYLeitura + 24, 120, 36,
                   juce::Justification::centredLeft);
    }
    g.setColour(cores::borda);
    g.drawHorizontalLine(kYCalibrador - 12, (float) kMargem, (float) (getWidth() - kMargem));

    const auto bloco = [&](int y, const char* titulo, const char* dica) {
        g.setColour(cores::texto);
        g.setFont(fonte(15.0f, true));
        g.drawText(txt(titulo), kMargem, y, largura, 20, juce::Justification::centredLeft);
        g.setColour(cores::suave);
        g.setFont(fonte(13.0f));
        g.drawFittedText(txt(dica), kMargem, y + 20, largura, 32, juce::Justification::topLeft, 2);
    };
    bloco(kYCalibrador, "1. Calibrador acústico no mic", "Encaixe o calibrador, ligue, espere a leitura parar e clique. É o jeito mais exato.");
    bloco(kYDecibelimetro, "2. Igualar a um decibelímetro",
          "Com um som constante tocando (ruído rosa serve), ponha o decibelímetro ao lado do mic e digite o que ele mostra, na curva em que ele está.");
    bloco(kYDados, "3. Dados do mic e da placa (sem aparelho)", "Aproximado. Pegue os números na ficha técnica do mic e da placa ou mesa.");
    g.setFont(fonte(12.0f));
    g.drawText("mic, mV/Pa", kMargem, kYDados + 54, 100, 16, juce::Justification::centredLeft);
    g.drawText(txt("entrada máx., dBu"), kMargem + 104, kYDados + 54, 104, 16, juce::Justification::centredLeft);
    g.drawText(txt("ganho do pré, dB"), kMargem + 208, kYDados + 54, 104, 16, juce::Justification::centredLeft);
    bloco(kYValor, "4. Valor já conhecido", "Quantos dB SPL um seno em fundo de escala vale neste canal, com este ganho.");
    g.setColour(cores::suave);
    g.setFont(fonte(14.0f));
    g.drawText("dB SPL", kMargem + 96, kYValor + 54, 64, kLinha, juce::Justification::centredLeft);

    g.setColour(cores::borda);
    g.drawHorizontalLine(kYRodape - 4, (float) kMargem, (float) (getWidth() - kMargem));
    const auto rodape = juce::Rectangle<int>(kMargem, kYRodape + 2, largura - 180, 40);
    if (aviso_.isNotEmpty()) {
        g.setColour(cores::amarelo);
        g.setFont(fonte(13.0f));
        g.drawFittedText(aviso_, rodape, juce::Justification::centredLeft, 3);
    } else {
        g.setColour(escala_.calibrado ? cores::texto : cores::amarelo);
        g.setFont(fonte(14.0f, true));
        g.drawFittedText(escala_.calibrado ? txt("Calibrado: fundo de escala = ") + numero(escala_.calibracaoDb, 1) + " dB SPL. Vale para este canal e este ganho."
                                           : txt("Sem calibração: os níveis aparecem em dBFS."),
                         rodape, juce::Justification::centredLeft, 2);
    }
}
