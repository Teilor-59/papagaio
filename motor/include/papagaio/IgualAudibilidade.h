#pragma once

namespace papagaio {

// Curvas de igual audibilidade da ISO 226:2003: o nível, em dB SPL, que um tom puro na
// frequência f precisa ter para soar tão alto quanto um tom de 1 kHz a `phon` dB SPL.
//
// A norma vale de 20 a 90 phon e de 20 Hz a 12,5 kHz. Fora disso a função extrapola: acima de
// 90 phon usa a mesma fórmula, e acima de 12,5 kHz repete o valor de 12,5 kHz. Show ao vivo
// passa de 90 phon com facilidade, então o que sai daí é estimativa, não norma.
double nivelDeIgualAudibilidade(double phon, double f);

} // namespace papagaio
