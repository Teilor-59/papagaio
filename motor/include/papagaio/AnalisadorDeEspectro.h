#pragma once

#include <memory>
#include <vector>

#include "papagaio/Fft.h"

namespace papagaio {

// Espectro do mic em bandas de 1/48 de oitava, de 20 Hz a 20 kHz. É a base das vistas:
// o RTA soma essas bandas em frações mais largas e o espectrograma empilha uma linha por quadro.
//
// Nível na convenção do motor: um seno em fundo de escala lê 0 dB na banda em que cai.
// Abaixo de uns 140 Hz a banda é mais estreita que a resolução da FFT, então bandas vizinhas
// repartem a energia do mesmo bin e a curva sai em degraus.
class AnalisadorDeEspectro {
public:
    static constexpr int kPorOitava = 48;
    static constexpr int kPrimeira = -271;      // 1000 * 2^(-271/48) = 19,98 Hz
    static constexpr int kNumBandas = 479;      // a última fica em 19,87 kHz

    static double centro(int banda);            // Hz

    void preparar(double taxaDeAmostragem);
    void definirMedia(double segundos);         // 0 = sem média
    void processar(const float* mic, int numAmostras);

    // Uma linha de kNumBandas valores em dB para cada quadro fechado desde a última chamada, sem média.
    std::vector<float> retirarLinhas();
    // A última linha, com a média escolhida.
    const std::vector<float>& bandasDb() const { return mediaDb_; }
    double quadrosPorSegundo() const { return taxa_ / salto_; }

private:
    void quadro();

    double taxa_ = 48000.0, media_ = 0, normDb_ = 0;
    int n_ = 0, salto_ = 0, pos_ = 0, contador_ = 0;
    bool primeiro_ = true;

    std::unique_ptr<Fft> fft_;
    std::vector<float> anel_, janela_, re_, im_;
    std::vector<double> potencia_, de_, ate_, mediaPotencia_;
    std::vector<float> mediaDb_, linhas_;
};

} // namespace papagaio
