#pragma once

#include <array>
#include <memory>
#include <vector>

#include "papagaio/Fft.h"

namespace papagaio {

enum class Ponderacao : int { A = 0, C = 1, Z = 2 };
constexpr int kNumPonderacoes = 3;

// Janelas do Leq deslizante: 1, 5 e 15 minutos, as das normas de exposição e dos contratos de casa.
constexpr int kNumJanelasDeLeq = 3;
constexpr int kMinutosDaJanela[kNumJanelasDeLeq] = { 1, 5, 15 };

// Níveis em dB na convenção do resto do motor: seno em fundo de escala lê 0 dB.
// Para dB SPL, some a calibração do mic.
struct LeituraDeNivel {
    float agora = -200;      // com a ponderação de tempo escolhida (rápida ou lenta)
    float medio = -200;      // Leq: média de energia desde que zerou
    float maximo = -200;     // maior e menor valor que "agora" já mostrou
    float minimo = -200;
    // Leq dos últimos 1, 5 e 15 minutos. Até a janela encher, é o Leq do que já passou (cheia = false).
    std::array<float, kNumJanelasDeLeq> leqJanela { -200, -200, -200 };
    std::array<bool, kNumJanelasDeLeq> janelaCheia {};
    // Maior valor que cada Leq deslizante já teve com a janela cheia.
    std::array<float, kNumJanelasDeLeq> maiorLeqJanela { -200, -200, -200 };
};

// Medidor de nível sonoro com as curvas A, C e Z ao mesmo tempo.
//
// A ponderação em frequência é aplicada no espectro, então segue a curva da norma até 20 kHz
// em qualquer taxa de amostragem. A ponderação de tempo é a exponencial de sempre: rápida
// (125 ms) ou lenta (1 s).
class MedidorDeNivel {
public:
    void preparar(double taxaDeAmostragem);
    void definirLento(bool lento);      // zera máximo e mínimo, que dependem da ponderação de tempo
    bool lento() const { return lento_; }
    void zerar();
    void processar(const float* mic, int numAmostras);

    std::array<LeituraDeNivel, kNumPonderacoes> leituras() const;
    double segundosMedidos() const { return quadros_ * tempoDoSalto_; }

private:
    void quadro();
    double leqDaJanela(size_t w, int segundos, bool& cheia) const;

    double taxa_ = 48000.0, tempoDoSalto_ = 0, escala_ = 0;
    int n_ = 0, salto_ = 0, pos_ = 0, contador_ = 0;
    bool lento_ = false;
    long long quadros_ = 0;

    std::unique_ptr<Fft> fft_;
    std::vector<float> anel_, janela_, re_, im_;
    std::array<std::vector<float>, kNumPonderacoes> peso_;
    std::array<double, kNumPonderacoes> suave_ {}, soma_ {}, maximo_ {}, minimo_ {};

    // Leq deslizante: energia somada por segundo, num anel que cobre a janela mais longa e mais o
    // segundo que ainda está enchendo.
    struct Segundo {
        std::array<double, kNumPonderacoes> soma {};
        int quadros = 0;
    };
    std::vector<Segundo> segundos_;
    long long segundoAtual_ = 0;        // quantos segundos inteiros já fecharam
    std::array<std::array<double, kNumJanelasDeLeq>, kNumPonderacoes> maiorJanela_ {};
};

} // namespace papagaio
