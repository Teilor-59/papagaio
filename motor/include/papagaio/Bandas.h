#pragma once

namespace papagaio {

// Bandas de 1/3 de oitava de 25 Hz a 16 kHz, agrupadas nas seis regiões do painel.
constexpr int kNumBandas = 29;
constexpr int kNumRegioes = 6;

double centroDaBanda(int banda);          // Hz
int regiaoDaBanda(int banda);
const char* nomeDaRegiao(int regiao);     // "sub", "grave", "médio-grave", "médio", "médio-agudo", "agudo"
const char* faixaDaRegiao(int regiao);    // "2 a 4 kHz"

// Curvas de ponderação A e C, em dB, pela fórmula da IEC 61672. A Z é plana.
double ponderacaoA(double f);
double ponderacaoC(double f);

} // namespace papagaio
