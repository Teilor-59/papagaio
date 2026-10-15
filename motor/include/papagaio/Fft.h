#pragma once

#include <vector>

namespace papagaio {

// FFT complexa radix-2, direta, sem normalização. O tamanho é potência de 2.
class Fft {
public:
    explicit Fft(int tamanho);
    int tamanho() const { return n_; }
    void transformar(float* re, float* im) const;

private:
    int n_;
    std::vector<int> troca_;
    std::vector<float> cos_, sen_;
};

} // namespace papagaio
