#include "papagaio/AnalisadorDuplo.h"

#include <algorithm>
#include <cmath>

namespace papagaio {

namespace {

constexpr double kMinusculo = 1e-30;
constexpr double kPotenciaMinimaDaRef = 1e-6;     // -60 dBFS: abaixo disso a mesa está em silêncio
constexpr double kSegundosEntreBuscas = 2.0;
constexpr double kAtrasoMaximo = 0.6;             // segundos; cobre 200 m de PA até o mic
constexpr double kQualidadeMinima = 6.0;          // pico da correlação sobre o resto

double mediana(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

void AnalisadorDuplo::preparar(double taxaDeAmostragem, double duracaoDoBloco) {
    taxa_ = taxaDeAmostragem;
    duracaoDoBloco_ = duracaoDoBloco;
    const int mult = taxa_ > 60000.0 ? 2 : 1;
    n_ = 16384 * mult;          // 2,9 Hz por bin: resolve 1/3 de oitava até o sub
    salto_ = n_ / 2;
    m_ = 65536 * mult;          // janela da busca de atraso
    const int anel = 131072 * mult;
    mascara_ = anel - 1;
    hzPorBin_ = taxa_ / n_;
    normDb_ = 20.0 * std::log10(n_ / 4.0);

    anelRef_.assign((size_t) anel, 0.0f);
    anelMic_.assign((size_t) anel, 0.0f);
    janela_.resize((size_t) n_);
    for (int i = 0; i < n_; ++i) janela_[(size_t) i] = (float) (0.5 - 0.5 * std::cos(6.283185307179586 * i / n_));
    janelaLonga_.resize((size_t) m_);
    for (int i = 0; i < m_; ++i) janelaLonga_[(size_t) i] = (float) (0.5 - 0.5 * std::cos(6.283185307179586 * i / m_));
    for (auto* v : { &xr_, &xi_, &yr_, &yi_ }) v->assign((size_t) m_, 0.0f);
    fft_ = std::make_unique<Fft>(n_);
    fftLonga_ = std::make_unique<Fft>(m_);

    const size_t bins = (size_t) n_ / 2 + 1;
    for (auto* v : { &sxx_, &syy_, &sxyRe_, &sxyIm_ }) v->assign(bins, 0.0);
    pesoA_.resize(bins);
    for (size_t k = 0; k < bins; ++k) pesoA_[k] = std::pow(10.0, ponderacaoA((double) k * hzPorBin_) / 10.0);
    kMinA_ = std::max(1, (int) std::ceil(20.0 / hzPorBin_));
    for (int b = 0; b < kNumBandas; ++b) {
        const double fc = centroDaBanda(b);
        kDe_[(size_t) b] = std::max(1, (int) std::ceil(fc * std::pow(2.0, -1.0 / 6.0) / hzPorBin_));
        kAte_[(size_t) b] = std::min(n_ / 2, std::max(kDe_[(size_t) b], (int) std::floor(fc * std::pow(2.0, 1.0 / 6.0) / hzPorBin_)));
    }

    total_ = 0;
    inicioDoBloco_ = 0;
    proximoQuadro_ = n_;
    proximaBusca_ = m_;
    travado_ = false;
    atraso_ = 0;
    atrasoFino_ = 0;
    ultimosAtrasos_.clear();
    blocos_.clear();
    zerarBloco();
}

void AnalisadorDuplo::zerarBloco() {
    for (auto* v : { &sxx_, &syy_, &sxyRe_, &sxyIm_ }) std::fill(v->begin(), v->end(), 0.0);
    quadrosAtivos_ = quadrosEmSilencio_ = 0;
    silencioA_ = 0;
    atrasosDoBloco_.clear();
}

void AnalisadorDuplo::processar(const float* referencia, const float* mic, int numAmostras) {
    const long long amostrasPorBloco = (long long) (duracaoDoBloco_ * taxa_);
    for (int i = 0; i < numAmostras; ++i) {
        const size_t p = (size_t) (total_ & mascara_);
        anelRef_[p] = referencia[i];
        anelMic_[p] = mic[i];
        ++total_;
        if (total_ >= proximaBusca_) {
            procurarAtraso();
            proximaBusca_ += (long long) (kSegundosEntreBuscas * taxa_);
        }
        if (total_ >= proximoQuadro_) {
            quadro();
            proximoQuadro_ += salto_;
        }
        if (total_ - inicioDoBloco_ >= amostrasPorBloco) fecharBloco();
    }
}

std::vector<BlocoDeBandas> AnalisadorDuplo::retirarBlocos() {
    std::vector<BlocoDeBandas> r;
    r.swap(blocos_);
    return r;
}

// Correlação cruzada com ponderação PHAT: o pico marca quanto o mic está atrasado em relação
// à referência. Dá o alinhamento da medição e, de quebra, o termômetro acústico.
void AnalisadorDuplo::procurarAtraso() {
    if (total_ < m_) return;
    const long long ini = total_ - m_;
    double potencia = 0;
    for (int i = 0; i < m_; ++i) {
        const size_t p = (size_t) ((ini + i) & mascara_);
        const float r = anelRef_[p];
        potencia += (double) r * r;
        xr_[(size_t) i] = r * janelaLonga_[(size_t) i];
        yr_[(size_t) i] = anelMic_[p] * janelaLonga_[(size_t) i];
        xi_[(size_t) i] = yi_[(size_t) i] = 0.0f;
    }
    if (potencia / m_ < kPotenciaMinimaDaRef) return;

    fftLonga_->transformar(xr_.data(), xi_.data());
    fftLonga_->transformar(yr_.data(), yi_.data());

    const double hz = taxa_ / m_;
    const int kDe = (int) (200.0 / hz), kAte = (int) (6000.0 / hz);
    double maior = 0;
    for (int k = kDe; k <= kAte; ++k)
        maior = std::max(maior, (double) xr_[(size_t) k] * xr_[(size_t) k] + (double) xi_[(size_t) k] * xi_[(size_t) k]);
    for (int k = 0; k < m_; ++k) {
        const int kk = k <= m_ / 2 ? k : m_ - k;
        const size_t i = (size_t) k;
        double cr = 0, ci = 0;
        const double px = (double) xr_[i] * xr_[i] + (double) xi_[i] * xi_[i];
        if (kk >= kDe && kk <= kAte && px > 1e-4 * maior) {
            cr = (double) yr_[i] * xr_[i] + (double) yi_[i] * xi_[i];
            ci = (double) yi_[i] * xr_[i] - (double) yr_[i] * xi_[i];
            const double mag = std::sqrt(cr * cr + ci * ci) + kMinusculo;
            cr /= mag;
            ci /= mag;
        }
        // transformada inversa pela direta: entra o conjugado, a parte real da saída é a correlação
        xr_[i] = (float) cr;
        xi_[i] = (float) -ci;
    }
    fftLonga_->transformar(xr_.data(), xi_.data());

    const int limite = std::min(m_ / 2, (int) (kAtrasoMaximo * taxa_));
    int pico = 0;
    double soma2 = 0;
    for (int i = 0; i <= limite; ++i) {
        soma2 += (double) xr_[(size_t) i] * xr_[(size_t) i];
        if (xr_[(size_t) i] > xr_[(size_t) pico]) pico = i;
    }
    const double rms = std::sqrt(soma2 / (limite + 1)) + kMinusculo;
    if (xr_[(size_t) pico] / rms < kQualidadeMinima) return;

    double fino = pico;
    if (pico > 0 && pico < limite) {
        const double a = xr_[(size_t) pico - 1], b = xr_[(size_t) pico], c = xr_[(size_t) pico + 1];
        const double curva = a - 2.0 * b + c;
        if (curva < 0) fino += 0.5 * (a - c) / curva;
    }

    ultimosAtrasos_.push_back(fino);
    if (ultimosAtrasos_.size() > 5) ultimosAtrasos_.erase(ultimosAtrasos_.begin());
    atrasosDoBloco_.push_back(fino);
    atrasoFino_ = mediana(ultimosAtrasos_);
    // Alguns ms de erro no alinhamento não mexem no ganho medido, então só realinha em mudança grande.
    if (!travado_ || std::abs(atrasoFino_ - atraso_) > 8.0) atraso_ = (int) std::lround(atrasoFino_);
    travado_ = true;
}

void AnalisadorDuplo::quadro() {
    if (!travado_) return;
    const long long ini = total_ - n_;
    if (ini - atraso_ < 0) return;

    double potencia = 0;
    for (int i = 0; i < n_; ++i) {
        const float r = anelRef_[(size_t) ((ini - atraso_ + i) & mascara_)];
        potencia += (double) r * r;
        xr_[(size_t) i] = r * janela_[(size_t) i];
        yr_[(size_t) i] = anelMic_[(size_t) ((ini + i) & mascara_)] * janela_[(size_t) i];
        xi_[(size_t) i] = yi_[(size_t) i] = 0.0f;
    }
    fft_->transformar(yr_.data(), yi_.data());

    if (potencia / n_ < kPotenciaMinimaDaRef) {
        // mesa em silêncio: o que o mic ouve é o piso da casa
        double s = 0;
        for (int k = kMinA_; k <= n_ / 2; ++k)
            s += pesoA_[(size_t) k] * ((double) yr_[(size_t) k] * yr_[(size_t) k] + (double) yi_[(size_t) k] * yi_[(size_t) k]);
        silencioA_ += s;
        ++quadrosEmSilencio_;
        return;
    }

    fft_->transformar(xr_.data(), xi_.data());
    for (int k = 0; k <= n_ / 2; ++k) {
        const size_t i = (size_t) k;
        const double ar = xr_[i], ai = xi_[i], br = yr_[i], bi = yi_[i];
        sxx_[i] += ar * ar + ai * ai;
        syy_[i] += br * br + bi * bi;
        sxyRe_[i] += ar * br + ai * bi;
        sxyIm_[i] += ar * bi - ai * br;
    }
    ++quadrosAtivos_;
}

void AnalisadorDuplo::fecharBloco() {
    BlocoDeBandas b;
    b.tempo = total_ / taxa_;
    b.quadros = quadrosAtivos_;
    b.duracaoAtiva = quadrosAtivos_ * salto_ / taxa_;

    const auto emDb = [this](double soma, int quadros) {
        return (float) (10.0 * std::log10(soma / std::max(1, quadros) + kMinusculo) - normDb_ - 1.76);
    };
    // Parte do mic explicada pela referência em cada bin: |Sxy|² / Sxx.
    const auto coerente = [this](size_t k) {
        return sxx_[k] > kMinusculo ? (sxyRe_[k] * sxyRe_[k] + sxyIm_[k] * sxyIm_[k]) / sxx_[k] : 0.0;
    };

    for (int banda = 0; banda < kNumBandas; ++banda) {
        const size_t i = (size_t) banda;
        double ref = 0, mic = 0, vem = 0;
        for (int k = kDe_[i]; k <= kAte_[i]; ++k) {
            ref += sxx_[(size_t) k];
            mic += syy_[(size_t) k];
            vem += coerente((size_t) k);
        }
        b.ganhoDb[i] = (float) (10.0 * std::log10((vem + kMinusculo) / (ref + kMinusculo)));
        b.coerencia[i] = mic > kMinusculo ? (float) std::min(1.0, vem / mic) : 0.0f;
        b.nivelRefDb[i] = emDb(ref, quadrosAtivos_);
        b.nivelMicDb[i] = emDb(mic, quadrosAtivos_);
    }

    if (quadrosAtivos_ > 0) {
        double ref = 0, mic = 0, vem = 0;
        for (int k = kMinA_; k <= n_ / 2; ++k) {
            const size_t i = (size_t) k;
            ref += pesoA_[i] * sxx_[i];
            mic += pesoA_[i] * syy_[i];
            vem += pesoA_[i] * coerente(i);
        }
        b.nivelRefADb = emDb(ref, quadrosAtivos_);
        b.nivelMicADb = emDb(mic, quadrosAtivos_);
        b.foraDoPaADb = emDb(std::max(mic - vem, mic * 1e-4), quadrosAtivos_);
    }
    if (quadrosEmSilencio_ >= 3) {
        b.pisoADb = emDb(silencioA_, quadrosEmSilencio_);
        b.temPiso = true;
    }
    if (!atrasosDoBloco_.empty()) {
        b.atrasoMs = mediana(atrasosDoBloco_) * 1000.0 / taxa_;
        b.temAtraso = true;
    } else if (travado_) {
        b.atrasoMs = atrasoMs();
        b.temAtraso = true;
    }

    blocos_.push_back(b);
    inicioDoBloco_ = total_;
    zerarBloco();
}

} // namespace papagaio
