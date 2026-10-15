#include "papagaio/Fft.h"

#include <cmath>
#include <utility>

namespace papagaio {

Fft::Fft(int tamanho) : n_(tamanho), troca_(tamanho), cos_(tamanho / 2), sen_(tamanho / 2) {
    int bits = 0;
    while ((1 << bits) < n_) ++bits;
    for (int i = 0; i < n_; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        troca_[i] = r;
    }
    const double doisPi = 6.283185307179586;
    for (int i = 0; i < n_ / 2; ++i) {
        cos_[i] = (float) std::cos(doisPi * i / n_);
        sen_[i] = (float) -std::sin(doisPi * i / n_);
    }
}

void Fft::transformar(float* re, float* im) const {
    for (int i = 0; i < n_; ++i) {
        const int j = troca_[i];
        if (j > i) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int tam = 2; tam <= n_; tam <<= 1) {
        const int meio = tam / 2;
        const int passo = n_ / tam;
        for (int ini = 0; ini < n_; ini += tam) {
            for (int k = 0; k < meio; ++k) {
                const float c = cos_[k * passo], s = sen_[k * passo];
                const int a = ini + k, b = a + meio;
                const float tr = re[b] * c - im[b] * s;
                const float ti = re[b] * s + im[b] * c;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
}

} // namespace papagaio
