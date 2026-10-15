#pragma once

#include <array>
#include <memory>
#include <vector>

#include "papagaio/Bandas.h"
#include "papagaio/Fft.h"

namespace papagaio {

// Resumo de alguns segundos de medição mic contra L/R, por banda de 1/3 de oitava.
struct BlocoDeBandas {
    double tempo = 0;              // fim do bloco, em segundos desde o começo
    double duracaoAtiva = 0;       // segundos em que havia sinal na referência
    int quadros = 0;               // quadros com sinal; com poucos, a coerência não vale

    std::array<float, kNumBandas> ganhoDb {};      // parte do mic que vem do L/R, sobre o L/R: a sala e o sistema
    std::array<float, kNumBandas> coerencia {};    // fração da energia do mic que vem do L/R, de 0 a 1
    std::array<float, kNumBandas> nivelRefDb {};   // o que sai da mesa
    std::array<float, kNumBandas> nivelMicDb {};   // o que chega no FOH

    float nivelRefADb = -200;      // totais com ponderação A, em dBFS
    float nivelMicADb = -200;
    float foraDoPaADb = -200;      // parte do mic que não vem do L/R: público, palco, cauda da sala
    float pisoADb = -200;          // mic com a mesa em silêncio, entre músicas
    bool temPiso = false;

    double atrasoMs = 0;           // tempo do L/R até o mic
    bool temAtraso = false;
};

// Medição de dois canais, como a função de transferência do Smaart: acha o atraso entre a
// referência (L/R da mesa) e o mic, alinha os dois e acumula espectros cruzados. A coerência
// separa o que vem do PA do que não vem, então barulho e canto do público não contaminam o ganho.
// Sem sinal na referência, entre músicas, a medição pausa e o mic mede o piso da casa.
class AnalisadorDuplo {
public:
    void preparar(double taxaDeAmostragem, double duracaoDoBloco = 10.0);
    void processar(const float* referencia, const float* mic, int numAmostras);

    // Blocos fechados desde a última chamada.
    std::vector<BlocoDeBandas> retirarBlocos();

    bool atrasoTravado() const { return travado_; }
    double atrasoMs() const { return atrasoFino_ * 1000.0 / taxa_; }

private:
    void quadro();
    void procurarAtraso();
    void fecharBloco();
    void zerarBloco();

    double taxa_ = 48000.0, duracaoDoBloco_ = 10.0, hzPorBin_ = 0, normDb_ = 0;
    int n_ = 0, salto_ = 0, m_ = 0, mascara_ = 0;
    long long total_ = 0, inicioDoBloco_ = 0, proximoQuadro_ = 0, proximaBusca_ = 0;

    std::vector<float> anelRef_, anelMic_, janela_, janelaLonga_;
    std::vector<float> xr_, xi_, yr_, yi_;
    std::unique_ptr<Fft> fft_, fftLonga_;
    std::vector<double> sxx_, syy_, sxyRe_, sxyIm_, pesoA_;
    std::array<int, kNumBandas> kDe_ {}, kAte_ {};
    int kMinA_ = 1;

    int quadrosAtivos_ = 0, quadrosEmSilencio_ = 0;
    double silencioA_ = 0;

    bool travado_ = false;
    int atraso_ = 0;                       // amostras usadas para alinhar
    double atrasoFino_ = 0;                // última estimativa, com fração de amostra
    std::vector<double> ultimosAtrasos_, atrasosDoBloco_;

    std::vector<BlocoDeBandas> blocos_;
};

} // namespace papagaio
