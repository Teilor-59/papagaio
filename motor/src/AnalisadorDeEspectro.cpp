#include "papagaio/AnalisadorDeEspectro.h"

#include <algorithm>
#include <cmath>

namespace papagaio {

double AnalisadorDeEspectro::centro(int banda) { return 1000.0 * std::pow(2.0, (double) (banda + kPrimeira) / kPorOitava); }

void AnalisadorDeEspectro::preparar(double taxaDeAmostragem) {
    taxa_ = taxaDeAmostragem;
    const int mult = taxa_ > 60000.0 ? 2 : 1;
    n_ = 16384 * mult;
    salto_ = 2048 * mult;
    normDb_ = 20.0 * std::log10(n_ / 4.0) + 1.76;      // a soma dos bins de um seno com Hann dá o pico + 1,76 dB

    fft_ = std::make_unique<Fft>(n_);
    anel_.assign((size_t) n_, 0.0f);
    janela_.resize((size_t) n_);
    for (int i = 0; i < n_; ++i) janela_[(size_t) i] = (float) (0.5 - 0.5 * std::cos(6.283185307179586 * i / n_));
    re_.resize((size_t) n_);
    im_.resize((size_t) n_);
    potencia_.assign((size_t) n_ / 2 + 1, 0.0);

    // Bordas de cada banda em unidades de bin: o bin k cobre o intervalo [k, k+1) depois de somar 0,5.
    const double hz = taxa_ / n_;
    const double meia = std::pow(2.0, 0.5 / kPorOitava);
    de_.resize(kNumBandas);
    ate_.resize(kNumBandas);
    for (int b = 0; b < kNumBandas; ++b) {
        const double fc = centro(b);
        de_[(size_t) b] = fc / meia / hz + 0.5;
        ate_[(size_t) b] = std::min((double) n_ / 2, fc * meia / hz + 0.5);
    }
    mediaPotencia_.assign(kNumBandas, 0.0);
    mediaDb_.assign(kNumBandas, -200.0f);
    linhas_.clear();
    pos_ = contador_ = 0;
    primeiro_ = true;
}

void AnalisadorDeEspectro::definirMedia(double segundos) { media_ = std::max(0.0, segundos); }

void AnalisadorDeEspectro::processar(const float* mic, int numAmostras) {
    for (int i = 0; i < numAmostras; ++i) {
        anel_[(size_t) pos_] = mic[i];
        if (++pos_ >= n_) pos_ = 0;
        if (++contador_ >= salto_) {
            contador_ = 0;
            quadro();
        }
    }
}

std::vector<float> AnalisadorDeEspectro::retirarLinhas() {
    std::vector<float> r;
    r.swap(linhas_);
    return r;
}

void AnalisadorDeEspectro::quadro() {
    int j = pos_;
    for (int i = 0; i < n_; ++i) {
        re_[(size_t) i] = anel_[(size_t) j] * janela_[(size_t) i];
        im_[(size_t) i] = 0.0f;
        if (++j >= n_) j = 0;
    }
    fft_->transformar(re_.data(), im_.data());
    for (int k = 0; k <= n_ / 2; ++k)
        potencia_[(size_t) k] = (double) re_[(size_t) k] * re_[(size_t) k] + (double) im_[(size_t) k] * im_[(size_t) k];

    const double alfa = media_ > 0 && !primeiro_ ? 1.0 - std::exp(-(salto_ / taxa_) / media_) : 1.0;
    primeiro_ = false;
    const size_t base = linhas_.size();
    linhas_.resize(base + kNumBandas);
    for (int b = 0; b < kNumBandas; ++b) {
        // cada bin entra na proporção em que cai dentro da banda
        const double de = de_[(size_t) b], ate = ate_[(size_t) b];
        double p = 0;
        for (int k = (int) de; k <= (int) ate && k <= n_ / 2; ++k)
            p += potencia_[(size_t) k] * (std::min(ate, k + 1.0) - std::max(de, (double) k));
        linhas_[base + (size_t) b] = (float) (10.0 * std::log10(p + 1e-20) - normDb_);
        mediaPotencia_[(size_t) b] += alfa * (p - mediaPotencia_[(size_t) b]);
        mediaDb_[(size_t) b] = (float) (10.0 * std::log10(mediaPotencia_[(size_t) b] + 1e-20) - normDb_);
    }
}

} // namespace papagaio
