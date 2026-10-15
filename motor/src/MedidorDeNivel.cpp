#include "papagaio/MedidorDeNivel.h"

#include <algorithm>
#include <cmath>

#include "papagaio/Bandas.h"

namespace papagaio {

namespace {

constexpr double kMinusculo = 1e-20;
constexpr double kTempoRapido = 0.125, kTempoLento = 1.0;

// média quadrática -> dB, com o seno em fundo de escala lendo 0
float emDb(double mediaQuadratica) { return (float) (10.0 * std::log10(mediaQuadratica + kMinusculo) + 3.0103); }

} // namespace

void MedidorDeNivel::preparar(double taxaDeAmostragem) {
    taxa_ = taxaDeAmostragem;
    const int mult = taxa_ > 60000.0 ? 2 : 1;
    n_ = 4096 * mult;
    salto_ = 1024 * mult;
    tempoDoSalto_ = salto_ / taxa_;
    // Com janela Hann, a soma de |X|² de 1 a N/2 vezes 16/(3N²) é a média quadrática do sinal.
    escala_ = 16.0 / (3.0 * (double) n_ * n_);

    fft_ = std::make_unique<Fft>(n_);
    anel_.assign((size_t) n_, 0.0f);
    janela_.resize((size_t) n_);
    for (int i = 0; i < n_; ++i) janela_[(size_t) i] = (float) (0.5 - 0.5 * std::cos(6.283185307179586 * i / n_));
    re_.resize((size_t) n_);
    im_.resize((size_t) n_);

    const double hz = taxa_ / n_;
    for (auto& p : peso_) p.assign((size_t) n_ / 2 + 1, 0.0f);
    for (int k = 1; k <= n_ / 2; ++k) {
        const double f = k * hz;
        peso_[(size_t) Ponderacao::A][(size_t) k] = (float) std::pow(10.0, ponderacaoA(f) / 10.0);
        peso_[(size_t) Ponderacao::C][(size_t) k] = (float) std::pow(10.0, ponderacaoC(f) / 10.0);
        peso_[(size_t) Ponderacao::Z][(size_t) k] = 1.0f;
    }
    segundos_.assign((size_t) kMinutosDaJanela[kNumJanelasDeLeq - 1] * 60 + 1, Segundo {});
    pos_ = contador_ = 0;
    zerar();
}

void MedidorDeNivel::definirLento(bool lento) {
    if (lento == lento_) return;
    lento_ = lento;
    maximo_.fill(0.0);
    minimo_.fill(-1.0);
}

void MedidorDeNivel::zerar() {
    quadros_ = 0;
    suave_.fill(0.0);
    soma_.fill(0.0);
    maximo_.fill(0.0);
    minimo_.fill(-1.0);      // negativo = ainda sem leitura
    for (auto& seg : segundos_) seg = Segundo {};
    segundoAtual_ = 0;
    for (auto& m : maiorJanela_) m.fill(-1.0);
}

void MedidorDeNivel::processar(const float* mic, int numAmostras) {
    for (int i = 0; i < numAmostras; ++i) {
        anel_[(size_t) pos_] = mic[i];
        if (++pos_ >= n_) pos_ = 0;
        if (++contador_ >= salto_) {
            contador_ = 0;
            quadro();
        }
    }
}

void MedidorDeNivel::quadro() {
    int j = pos_;
    for (int i = 0; i < n_; ++i) {
        re_[(size_t) i] = anel_[(size_t) j] * janela_[(size_t) i];
        im_[(size_t) i] = 0.0f;
        if (++j >= n_) j = 0;
    }
    fft_->transformar(re_.data(), im_.data());

    std::array<double, kNumPonderacoes> p {};
    for (int k = 1; k <= n_ / 2; ++k) {
        const double x = (double) re_[(size_t) k] * re_[(size_t) k] + (double) im_[(size_t) k] * im_[(size_t) k];
        for (size_t w = 0; w < kNumPonderacoes; ++w) p[w] += peso_[w][(size_t) k] * x;
    }

    const double tau = lento_ ? kTempoLento : kTempoRapido;
    const double alfa = 1.0 - std::exp(-tempoDoSalto_ / tau);
    // O quadro cai no segundo em que começa; ao virar o segundo, a casa do anel é reaproveitada.
    const long long seg = (long long) (quadros_ * tempoDoSalto_);
    const bool virou = seg > segundoAtual_;
    while (segundoAtual_ < seg) {
        ++segundoAtual_;
        segundos_[(size_t) (segundoAtual_ % (long long) segundos_.size())] = Segundo {};
    }
    auto& casa = segundos_[(size_t) (segundoAtual_ % (long long) segundos_.size())];
    ++casa.quadros;
    ++quadros_;
    // Máximo e mínimo só valem depois que a exponencial assentou.
    const bool assentou = quadros_ * tempoDoSalto_ >= 4.0 * tau;
    for (size_t w = 0; w < kNumPonderacoes; ++w) {
        const double ms = p[w] * escala_;
        soma_[w] += ms;
        casa.soma[w] += ms;
        suave_[w] = quadros_ == 1 ? ms : suave_[w] + alfa * (ms - suave_[w]);
        if (!assentou) continue;
        maximo_[w] = std::max(maximo_[w], suave_[w]);
        minimo_[w] = minimo_[w] < 0 ? suave_[w] : std::min(minimo_[w], suave_[w]);
    }

    // O Leq de uma janela só muda quando um segundo fecha: é aí que se procura o maior.
    if (virou)
        for (size_t w = 0; w < kNumPonderacoes; ++w)
            for (size_t j = 0; j < kNumJanelasDeLeq; ++j) {
                bool cheia = false;
                const double leq = leqDaJanela(w, kMinutosDaJanela[j] * 60, cheia);
                if (cheia) maiorJanela_[w][j] = std::max(maiorJanela_[w][j], leq);
            }
}

// Média de energia dos últimos `segundos` segundos inteiros, sem o que ainda está enchendo: assim a
// janela tem sempre a mesma duração. Antes do primeiro segundo fechar, usa o pedaço que tem.
double MedidorDeNivel::leqDaJanela(size_t w, int segundos, bool& cheia) const {
    const long long tamanho = (long long) segundos_.size();
    cheia = segundoAtual_ >= segundos;
    double soma = 0;
    long long quadros = 0;
    if (segundoAtual_ == 0) {
        soma = segundos_[0].soma[w];
        quadros = segundos_[0].quadros;
    } else {
        const long long n = std::min<long long>(segundoAtual_, segundos);
        for (long long k = 1; k <= n; ++k) {
            const auto& s = segundos_[(size_t) ((segundoAtual_ - k) % tamanho)];
            soma += s.soma[w];
            quadros += s.quadros;
        }
    }
    return quadros > 0 ? soma / (double) quadros : -1.0;
}

std::array<LeituraDeNivel, kNumPonderacoes> MedidorDeNivel::leituras() const {
    std::array<LeituraDeNivel, kNumPonderacoes> r;
    if (quadros_ == 0) return r;
    for (size_t w = 0; w < kNumPonderacoes; ++w) {
        r[w].agora = emDb(suave_[w]);
        r[w].medio = emDb(soma_[w] / (double) quadros_);
        if (minimo_[w] >= 0) {
            r[w].maximo = emDb(maximo_[w]);
            r[w].minimo = emDb(minimo_[w]);
        }
        for (size_t j = 0; j < kNumJanelasDeLeq; ++j) {
            bool cheia = false;
            const double leq = leqDaJanela(w, kMinutosDaJanela[j] * 60, cheia);
            if (leq >= 0) r[w].leqJanela[j] = emDb(leq);
            r[w].janelaCheia[j] = cheia;
            if (maiorJanela_[w][j] >= 0) r[w].maiorLeqJanela[j] = emDb(maiorJanela_[w][j]);
        }
    }
    return r;
}

} // namespace papagaio
