#include "papagaio/MapaDeRisco.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "papagaio/DetectorMicrofonia.h"

namespace papagaio {

bool MapaDeRisco::acrescentar(const Evento& e) {
    if (e.estado < Estado::laranja && !e.preApito) return false;
    const int pior = (int) e.estado;
    const int i = perto(e.f);
    if (i >= 0) {
        auto& r = itens_[(size_t) i];
        // a frequência é a média dos alertas que caíram nela
        r.f = (r.f * r.vezes + e.f) / (r.vezes + 1);
        ++r.vezes;
        r.pior = std::max(r.pior, pior);
        r.nivelMaxDb = std::max(r.nivelMaxDb, e.nivelMaxDb);
    } else {
        FrequenciaDeRisco r;
        r.f = e.f;
        r.vezes = 1;
        r.pior = pior;
        r.nivelMaxDb = e.nivelMaxDb;
        itens_.push_back(r);
    }
    ordenar();
    return true;
}

void MapaDeRisco::acrescentarManual(double f) {
    if (f <= 0 || perto(f) >= 0) return;
    FrequenciaDeRisco r;
    r.f = f;
    r.manual = true;
    itens_.push_back(r);
    ordenar();
}

void MapaDeRisco::remover(size_t i) {
    if (i < itens_.size()) itens_.erase(itens_.begin() + (long) i);
}

int MapaDeRisco::perto(double f, double tolerancia) const {
    int melhor = -1;
    double distancia = tolerancia;
    for (size_t i = 0; i < itens_.size(); ++i) {
        const double d = std::abs(itens_[i].f / f - 1.0);
        if (d < distancia) {
            distancia = d;
            melhor = (int) i;
        }
    }
    return melhor;
}

void MapaDeRisco::ordenar() {
    std::sort(itens_.begin(), itens_.end(), [](const FrequenciaDeRisco& a, const FrequenciaDeRisco& b) { return a.f < b.f; });
}

std::string MapaDeRisco::emTexto() const {
    std::string s;
    char item[96];
    for (const auto& r : itens_) {
        std::snprintf(item, sizeof item, "%s%.1f:%d:%d:%.1f:%d", s.empty() ? "" : ";", r.f, r.vezes, r.pior, r.nivelMaxDb, r.manual ? 1 : 0);
        s += item;
    }
    return s;
}

MapaDeRisco MapaDeRisco::deTexto(const std::string& texto) {
    MapaDeRisco m;
    std::stringstream itens(texto);
    std::string item;
    while (std::getline(itens, item, ';')) {
        FrequenciaDeRisco r;
        int manual = 0;
        // "f" sozinho também vale: frequência posta à mão
        const int lidos = std::sscanf(item.c_str(), "%lf:%d:%d:%lf:%d", &r.f, &r.vezes, &r.pior, &r.nivelMaxDb, &manual);
        if (lidos < 1 || r.f <= 0) continue;
        r.manual = lidos < 5 ? lidos == 1 : manual != 0;
        if (m.perto(r.f) < 0) m.itens_.push_back(r);
    }
    m.ordenar();
    return m;
}

} // namespace papagaio
