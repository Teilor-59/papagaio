#include "papagaio/IgualAudibilidade.h"

#include <algorithm>
#include <cmath>

namespace papagaio {

namespace {

// Tabela 1 da ISO 226:2003.
constexpr int kPontos = 29;
const double kF[kPontos] = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500,
                             630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500 };
const double kAf[kPontos] = { 0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330, 0.315, 0.301, 0.288, 0.276, 0.267,
                              0.259, 0.253, 0.250, 0.246, 0.244, 0.243, 0.243, 0.243, 0.242, 0.242, 0.245, 0.254, 0.271, 0.301 };
const double kLu[kPontos] = { -31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5, -3.1, -2.0, -1.1, -0.4, 0.0,
                              0.3, 0.5, 0.0, -2.7, -4.1, -1.0, 1.7, 2.5, 1.2, -2.1, -7.1, -11.2, -10.7, -3.1 };
const double kTf[kPontos] = { 78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4, 11.4, 8.6, 6.2, 4.4,
                              3.0, 2.2, 2.4, 3.5, 1.7, -1.3, -4.2, -6.0, -5.4, -1.5, 6.0, 12.6, 13.9, 12.3 };

double naTabela(double phon, int i) {
    const double a = 4.47e-3 * (std::pow(10.0, 0.025 * phon) - 1.15) + std::pow(0.4 * std::pow(10.0, (kTf[i] + kLu[i]) / 10.0 - 9.0), kAf[i]);
    return 10.0 / kAf[i] * std::log10(a) - kLu[i] + 94.0;
}

} // namespace

double nivelDeIgualAudibilidade(double phon, double f) {
    if (f <= kF[0]) return naTabela(phon, 0);
    if (f >= kF[kPontos - 1]) return naTabela(phon, kPontos - 1);
    const int i = (int) (std::upper_bound(kF, kF + kPontos, f) - kF) - 1;
    // entre dois pontos da tabela, reta no eixo logarítmico de frequência
    const double t = std::log(f / kF[i]) / std::log(kF[i + 1] / kF[i]);
    return naTabela(phon, i) + t * (naTabela(phon, i + 1) - naTabela(phon, i));
}

} // namespace papagaio
