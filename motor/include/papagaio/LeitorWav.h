#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace papagaio {

// Leitor de WAV em blocos, para shows longos: PCM 16/24/32 bits e float 32/64,
// qualquer número de canais, incluindo WAVE_FORMAT_EXTENSIBLE e RF64 (acima de 4 GB).
class LeitorWav {
public:
    bool abrir(const std::filesystem::path& caminho, std::string& erro);

    double taxa() const { return taxa_; }
    int canais() const { return canais_; }
    long long quadros() const { return quadros_; }

    // Lê até maxQuadros, intercalados, em float de -1 a 1. Devolve quantos quadros leu.
    int ler(std::vector<float>& intercalado, int maxQuadros);

private:
    std::ifstream arquivo_;
    std::vector<unsigned char> bruto_;
    double taxa_ = 0;
    int canais_ = 0, bytesPorAmostra_ = 0;
    bool flutuante_ = false;
    long long quadros_ = 0, lidos_ = 0;
};

} // namespace papagaio
