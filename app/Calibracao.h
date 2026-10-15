#pragma once

#include <array>
#include <functional>

#include "MotorAoVivo.h"
#include "Visual.h"

// Janela de calibração do mic: o caminho de dBFS para dB SPL.
//
// A placa só entrega números; quanto de pressão sonora cada número vale depende do mic e do
// ganho do pré. Por isso a calibração precisa de uma referência acústica, e vale só para o
// canal e o ganho em que foi feita. Três jeitos de dar essa referência:
//   1. calibrador acústico no mic (94 ou 114 dB em 1 kHz);
//   2. igualar a leitura à de um decibelímetro ao lado do mic;
//   3. calcular pelos dados do fabricante: sensibilidade do mic, entrada máxima da placa e ganho do pré
//      (não precisa de nenhum aparelho, mas é aproximado);
//   4. digitar um valor já conhecido.
class DialogoDeCalibracao : public juce::Component, private juce::Timer {
public:
    DialogoDeCalibracao(MotorAoVivo& motor, Escala escala, std::function<void(bool, double)> aoAplicar);
    ~DialogoDeCalibracao() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Lê o nível do motor e refaz a média. O timer chama sozinho; a captura de tela chama à mão.
    void atualizar();

private:
    void timerCallback() override { atualizar(); }
    void aplicar(bool tem, double db);
    void peloCalibrador();
    void peloDecibelimetro();
    void peloValor();
    void pelosDados();
    bool temLeitura() const { return amostras_ > 0 && media_[2] > -190.0; }

    MotorAoVivo& motor_;
    Escala escala_;
    std::function<void(bool, double)> aoAplicar_;

    // leitura mais firme que a do painel: média de 2 s de cada ponderação, sem calibração
    static constexpr int kJanela = 60;
    std::array<std::array<double, kJanela>, papagaio::kNumPonderacoes> potencias_ {};
    int pos_ = 0, amostras_ = 0;
    std::array<double, papagaio::kNumPonderacoes> media_ { -200.0, -200.0, -200.0 };

    juce::ComboBox comboCalibrador_, comboCurva_;
    juce::TextEditor campoLeitura_, campoValor_, campoSensibilidade_, campoEntrada_, campoGanho_;
    juce::TextButton botaoTom_, botaoIgualar_, botaoValor_, botaoDados_, botaoRemover_;
    juce::String aviso_;
};
