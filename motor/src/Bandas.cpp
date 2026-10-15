#include "papagaio/Bandas.h"

#include <cmath>

namespace papagaio {

double centroDaBanda(int banda) { return 1000.0 * std::pow(10.0, (banda - 16) / 10.0); }

int regiaoDaBanda(int banda) {
    if (banda <= 3) return 0;      // 25 a 50 Hz
    if (banda <= 8) return 1;      // 63 a 160 Hz
    if (banda <= 12) return 2;     // 200 a 400 Hz
    if (banda <= 18) return 3;     // 500 Hz a 1,6 kHz
    if (banda <= 22) return 4;     // 2 a 4 kHz
    return 5;                      // 5 a 16 kHz
}

const char* nomeDaRegiao(int regiao) {
    static const char* nomes[kNumRegioes] = { "sub", "grave", "médio-grave", "médio", "médio-agudo", "agudo" };
    return regiao >= 0 && regiao < kNumRegioes ? nomes[regiao] : "";
}

const char* faixaDaRegiao(int regiao) {
    static const char* faixas[kNumRegioes] = { "25 a 50 Hz", "63 a 160 Hz", "200 a 400 Hz", "500 Hz a 1,6 kHz", "2 a 4 kHz", "5 a 16 kHz" };
    return regiao >= 0 && regiao < kNumRegioes ? faixas[regiao] : "";
}

double ponderacaoA(double f) {
    const double f2 = f * f;
    const double r = 12194.0 * 12194.0 * f2 * f2
                     / ((f2 + 20.6 * 20.6) * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
    return 20.0 * std::log10(r + 1e-30) + 2.0;
}

double ponderacaoC(double f) {
    const double f2 = f * f;
    const double r = 12194.0 * 12194.0 * f2 / ((f2 + 20.6 * 20.6) * (f2 + 12194.0 * 12194.0));
    return 20.0 * std::log10(r + 1e-30) + 0.06;
}

} // namespace papagaio
