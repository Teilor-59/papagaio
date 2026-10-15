#include "papagaio/LeitorWav.h"

#include <algorithm>
#include <cstring>

namespace {

uint32_t u32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t u16(const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
uint64_t u64(const unsigned char* p) { return u32(p) | ((uint64_t) u32(p + 4) << 32); }

} // namespace

namespace papagaio {

bool LeitorWav::abrir(const std::filesystem::path& caminho, std::string& erro) {
    arquivo_.open(caminho, std::ios::binary);
    if (!arquivo_) {
        erro = "não consegui abrir o arquivo";
        return false;
    }
    std::error_code ec;
    const uint64_t tamanhoDoArquivo = std::filesystem::file_size(caminho, ec);

    unsigned char cab[12];
    arquivo_.read((char*) cab, 12);
    const bool rf64 = std::memcmp(cab, "RF64", 4) == 0;
    if (!arquivo_ || (!rf64 && std::memcmp(cab, "RIFF", 4) != 0) || std::memcmp(cab + 8, "WAVE", 4) != 0) {
        erro = "não é um arquivo WAV";
        return false;
    }

    uint64_t dadosRf64 = 0;
    bool temFormato = false;
    for (;;) {
        unsigned char c[8];
        arquivo_.read((char*) c, 8);
        if (!arquivo_) {
            erro = "WAV sem bloco de áudio";
            return false;
        }
        uint64_t tamanho = u32(c + 4);
        const uint64_t inicio = (uint64_t) arquivo_.tellg();

        if (std::memcmp(c, "ds64", 4) == 0) {
            unsigned char d[24];
            arquivo_.read((char*) d, 24);
            dadosRf64 = u64(d + 8);
        } else if (std::memcmp(c, "fmt ", 4) == 0) {
            unsigned char f[40] = {};
            arquivo_.read((char*) f, (std::streamsize) std::min<uint64_t>(tamanho, 40));
            int formato = u16(f);
            canais_ = u16(f + 2);
            taxa_ = u32(f + 4);
            const int alinhamento = u16(f + 12);
            if (formato == 0xFFFE && tamanho >= 26) formato = u16(f + 24);   // extensível: o formato real está no GUID
            flutuante_ = formato == 3;
            if ((formato != 1 && formato != 3) || canais_ < 1 || taxa_ <= 0) {
                erro = "formato de WAV não suportado (só PCM e float)";
                return false;
            }
            bytesPorAmostra_ = alinhamento / canais_;
            const bool ok = flutuante_ ? (bytesPorAmostra_ == 4 || bytesPorAmostra_ == 8)
                                       : (bytesPorAmostra_ >= 2 && bytesPorAmostra_ <= 4);
            if (!ok) {
                erro = "resolução de WAV não suportada";
                return false;
            }
            temFormato = true;
        } else if (std::memcmp(c, "data", 4) == 0) {
            if (!temFormato) {
                erro = "WAV com o áudio antes do formato";
                return false;
            }
            if (rf64 && tamanho == 0xFFFFFFFFu) tamanho = dadosRf64;
            // gravação interrompida deixa o tamanho zerado ou errado: vale o que existe no arquivo
            if (!ec && (tamanho == 0 || inicio + tamanho > tamanhoDoArquivo)) tamanho = tamanhoDoArquivo - inicio;
            quadros_ = (long long) (tamanho / ((uint64_t) bytesPorAmostra_ * canais_));
            return true;
        }
        arquivo_.clear();
        arquivo_.seekg((std::streamoff) (inicio + tamanho + (tamanho & 1)));
    }
}

int LeitorWav::ler(std::vector<float>& intercalado, int maxQuadros) {
    const long long restam = quadros_ - lidos_;
    const int pedir = (int) std::min<long long>(maxQuadros, restam);
    if (pedir <= 0) return 0;
    const size_t bytesPorQuadro = (size_t) bytesPorAmostra_ * canais_;
    bruto_.resize(pedir * bytesPorQuadro);
    arquivo_.read((char*) bruto_.data(), (std::streamsize) bruto_.size());
    const int n = (int) ((size_t) arquivo_.gcount() / bytesPorQuadro);
    lidos_ += n;

    const size_t total = (size_t) n * canais_;
    intercalado.resize(total);
    const unsigned char* p = bruto_.data();
    for (size_t i = 0; i < total; ++i, p += bytesPorAmostra_) {
        if (flutuante_) {
            if (bytesPorAmostra_ == 4) {
                float v;
                std::memcpy(&v, p, 4);
                intercalado[i] = v;
            } else {
                double v;
                std::memcpy(&v, p, 8);
                intercalado[i] = (float) v;
            }
        } else if (bytesPorAmostra_ == 2) {
            intercalado[i] = (int16_t) u16(p) / 32768.0f;
        } else if (bytesPorAmostra_ == 3) {
            const int32_t v = (int32_t) ((p[0] << 8) | (p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8;
            intercalado[i] = v / 8388608.0f;
        } else {
            intercalado[i] = (float) ((int32_t) u32(p) / 2147483648.0);
        }
    }
    return n;
}

} // namespace papagaio
