#include "papagaio/MonitorDeDrift.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace papagaio {

namespace {

constexpr int kQuadrosMinimos = 8;            // menos que isso e a coerência do bloco não diz nada
constexpr double kPesoMinimoDaBase = 3.0;     // segundos de medição coerente para uma banda entrar na linha de base

std::string comVirgula(double v, int casas) {
    char t[32];
    std::snprintf(t, sizeof t, "%.*f", casas, std::abs(v));
    std::string s = t;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

double medianaDe(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

} // namespace

std::string descrever(const EventoDeDrift& e) {
    const char* verbo = e.valor >= 0 ? "subiu " : "caiu ";
    switch (e.tipo) {
        case EventoDeDrift::Tipo::sala:
            return std::string("Sala: ") + nomeDaRegiao(e.regiao) + " " + verbo + comVirgula(e.valor, 1) + " dB desde a passagem";
        case EventoDeDrift::Tipo::mix:
            return std::string("Mix: ") + nomeDaRegiao(e.regiao) + " (" + faixaDaRegiao(e.regiao) + ") " + verbo + comVirgula(e.valor, 1)
                   + " dB em relação ao primeiro bloco";
        case EventoDeDrift::Tipo::volume:
            return std::string("Volume ") + verbo + comVirgula(e.valor, 1) + " dB desde o primeiro bloco";
        case EventoDeDrift::Tipo::atraso:
            return std::string("Atraso do PA ") + (e.valor >= 0 ? "aumentou " : "diminuiu ") + comVirgula(e.valor, 1) + " ms desde a passagem";
    }
    return {};
}

std::string sugerir(const EventoDeDrift& e) {
    switch (e.tipo) {
        case EventoDeDrift::Tipo::sala:
            if (e.regiao >= 4 && e.valor < 0)
                return "Casa cheia, umidade ou temperatura absorvendo agudo. Corrija no master ou no processador do sistema, não canal por canal.";
            return "Mudou a sala ou o sistema, não o mix. Não corrija no mix o que é sala.";
        case EventoDeDrift::Tipo::atraso:
            return "O som anda mais rápido com o ar mais quente. O alinhamento de torres e subs feito na passagem pode não valer mais.";
        default:
            return {};     // mostra o dado e deixa a conclusão com o engenheiro
    }
}

void MonitorDeDrift::preparar(const ConfigDrift& config) {
    *this = MonitorDeDrift {};
    cfg_ = config;
}

void MonitorDeDrift::iniciarPassagem() {
    baseSoma_.fill(0);
    basePeso_.fill(0);
    baseValida_.fill(false);
    atrasosDaBase_.clear();
    gravandoBase_ = true;
    estado_.temBase = false;
    estado_.temAtrasoBase = false;
    estado_.segundosDePassagem = 0;
    estado_.gravandoPassagem = true;
}

void MonitorDeDrift::fecharPassagem() {
    if (!gravandoBase_) return;
    gravandoBase_ = false;
    estado_.gravandoPassagem = false;
    for (int b = 0; b < kNumBandas; ++b) {
        const size_t i = (size_t) b;
        baseValida_[i] = basePeso_[i] >= kPesoMinimoDaBase;
        if (baseValida_[i]) {
            base_[i] = baseSoma_[i] / basePeso_[i];
            estado_.temBase = true;
        }
    }
    if (!atrasosDaBase_.empty()) {
        atrasoBase_ = medianaDe(atrasosDaBase_);
        estado_.temAtrasoBase = true;
    }
    vigiaSala_.fill(Vigia {});
    vigiaAtraso_ = Vigia {};
}

void MonitorDeDrift::marcarInicioDoShow() {
    refPotencia_.fill(0);
    refPotenciaA_ = 0;
    refDuracao_ = 0;
    acumulandoRef_ = true;
    estado_.temReferencia = false;
    estado_.segundosDeReferencia = 0;
    estado_.mixValido = false;
    vigiaMix_.fill(Vigia {});
    vigiaVolume_ = Vigia {};
}

bool MonitorDeDrift::bandaValida(const BlocoDeBandas& b, int banda) const {
    return b.quadros >= kQuadrosMinimos && b.nivelRefDb[(size_t) banda] > -90.0f && b.coerencia[(size_t) banda] >= cfg_.coerenciaMinima;
}

void MonitorDeDrift::receber(const BlocoDeBandas& b) {
    agora_ = b.tempo;

    if (gravandoBase_) {
        for (int banda = 0; banda < kNumBandas; ++banda) {
            if (!bandaValida(b, banda)) continue;
            const double peso = b.coerencia[(size_t) banda] * b.duracaoAtiva;
            baseSoma_[(size_t) banda] += peso * b.ganhoDb[(size_t) banda];
            basePeso_[(size_t) banda] += peso;
        }
        if (b.quadros >= kQuadrosMinimos) estado_.segundosDePassagem += b.duracaoAtiva;
        if (b.temAtraso) atrasosDaBase_.push_back(b.atrasoMs);
    }

    if (acumulandoRef_ && b.quadros >= kQuadrosMinimos) {
        for (int banda = 0; banda < kNumBandas; ++banda)
            refPotencia_[(size_t) banda] += b.duracaoAtiva * std::pow(10.0, b.nivelRefDb[(size_t) banda] / 10.0);
        refPotenciaA_ += b.duracaoAtiva * std::pow(10.0, b.nivelRefADb / 10.0);
        refDuracao_ += b.duracaoAtiva;
        estado_.segundosDeReferencia = refDuracao_;
        if (refDuracao_ >= cfg_.duracaoDaReferencia) {
            for (auto& p : refPotencia_) p /= refDuracao_;
            refPotenciaA_ /= refDuracao_;
            acumulandoRef_ = false;
            estado_.temReferencia = true;
        }
    }

    recentes_.push_back(b);
    const double guardar = std::max(cfg_.janelaSala, cfg_.janelaMix) + 1.0;
    while (!recentes_.empty() && recentes_.front().tempo < agora_ - guardar) recentes_.pop_front();

    atualizar();
}

void MonitorDeDrift::atualizar() {
    // --- sala: ganho mic/L/R agora contra a passagem ---
    estado_.salaValida.fill(false);
    if (estado_.temBase && !gravandoBase_) {
        std::array<double, kNumBandas> soma {}, peso {};
        for (const auto& b : recentes_) {
            if (b.tempo <= agora_ - cfg_.janelaSala) continue;
            for (int banda = 0; banda < kNumBandas; ++banda) {
                if (!bandaValida(b, banda)) continue;
                const double p = b.coerencia[(size_t) banda] * b.duracaoAtiva;
                soma[(size_t) banda] += p * b.ganhoDb[(size_t) banda];
                peso[(size_t) banda] += p;
            }
        }
        std::array<double, kNumRegioes> delta {};
        std::array<int, kNumRegioes> validas {}, todas {};
        for (int banda = 0; banda < kNumBandas; ++banda) {
            const size_t i = (size_t) banda, r = (size_t) regiaoDaBanda(banda);
            ++todas[r];
            if (!baseValida_[i] || peso[i] < 0.25 * cfg_.janelaSala) continue;
            delta[r] += soma[i] / peso[i] - base_[i];
            ++validas[r];
        }
        for (size_t r = 0; r < kNumRegioes; ++r) {
            estado_.salaValida[r] = validas[r] * 2 >= todas[r];
            estado_.salaDb[r] = estado_.salaValida[r] ? (float) (delta[r] / validas[r]) : 0.0f;
        }
    }

    // --- mix: média longa do L/R agora contra o primeiro bloco ---
    estado_.mixValido = false;
    if (estado_.temReferencia) {
        std::array<double, kNumRegioes> agora {}, ref {};
        double agoraA = 0, duracao = 0;
        for (const auto& b : recentes_) {
            if (b.tempo <= agora_ - cfg_.janelaMix || b.quadros < kQuadrosMinimos) continue;
            for (int banda = 0; banda < kNumBandas; ++banda)
                agora[(size_t) regiaoDaBanda(banda)] += b.duracaoAtiva * std::pow(10.0, b.nivelRefDb[(size_t) banda] / 10.0);
            agoraA += b.duracaoAtiva * std::pow(10.0, b.nivelRefADb / 10.0);
            duracao += b.duracaoAtiva;
        }
        if (duracao >= 0.5 * cfg_.janelaMix) {
            for (int banda = 0; banda < kNumBandas; ++banda) ref[(size_t) regiaoDaBanda(banda)] += refPotencia_[(size_t) banda];
            std::array<double, kNumRegioes> delta {};
            std::array<bool, kNumRegioes> tem {};
            std::vector<double> usados;
            for (size_t r = 0; r < kNumRegioes; ++r) {
                tem[r] = ref[r] > 1e-9;      // região sem programa no primeiro bloco não diz nada
                if (!tem[r]) continue;
                delta[r] = 10.0 * std::log10((agora[r] / duracao + 1e-30) / ref[r]);
                usados.push_back(delta[r]);
            }
            if (!usados.empty()) {
                // tira a parte comum a todas as regiões: isso é volume, não equilíbrio
                const double comum = medianaDe(usados);
                for (size_t r = 0; r < kNumRegioes; ++r) estado_.mixDb[r] = tem[r] ? (float) (delta[r] - comum) : 0.0f;
                estado_.volumeDb = (float) (10.0 * std::log10((agoraA / duracao + 1e-30) / (refPotenciaA_ + 1e-30)));
                estado_.mixValido = true;
            }
        }
    }

    // --- atraso, níveis e piso ---
    const BlocoDeBandas& ultimo = recentes_.back();
    if (ultimo.temAtraso) {
        estado_.temAtraso = true;
        estado_.atrasoMs = ultimo.atrasoMs;
        estado_.deltaAtrasoMs = estado_.temAtrasoBase ? ultimo.atrasoMs - atrasoBase_ : 0.0;
    }
    if (ultimo.temPiso) {
        estado_.temPiso = true;
        estado_.pisoADb = ultimo.pisoADb;
    }
    double mic = 0, ref = 0, fora = 0;
    int n = 0;
    for (auto it = recentes_.rbegin(); it != recentes_.rend() && n < 3; ++it) {
        if (it->quadros < kQuadrosMinimos) continue;
        mic += std::pow(10.0, it->nivelMicADb / 10.0);
        ref += std::pow(10.0, it->nivelRefADb / 10.0);
        fora += std::pow(10.0, it->foraDoPaADb / 10.0);
        ++n;
    }
    estado_.temNiveis = n > 0;
    if (n > 0) {
        estado_.nivelMicADb = (float) (10.0 * std::log10(mic / n));
        estado_.nivelRefADb = (float) (10.0 * std::log10(ref / n));
        estado_.foraDoPaADb = (float) (10.0 * std::log10(fora / n));
        estado_.coerenciaMedia = (float) std::clamp(1.0 - fora / mic, 0.0, 1.0);
    }

    // --- só mudança sustentada entra no histórico ---
    for (int r = 0; r < kNumRegioes; ++r)
        vigiar(vigiaSala_[(size_t) r], estado_.salaValida[(size_t) r], estado_.salaDb[(size_t) r], cfg_.limiarDb, EventoDeDrift::Tipo::sala, r);
    if (estado_.mixValido) {
        for (int r = 0; r < kNumRegioes; ++r)
            vigiar(vigiaMix_[(size_t) r], true, estado_.mixDb[(size_t) r], cfg_.limiarDb, EventoDeDrift::Tipo::mix, r);
        vigiar(vigiaVolume_, true, estado_.volumeDb, cfg_.limiarDeVolumeDb, EventoDeDrift::Tipo::volume, -1);
    }
    if (estado_.temAtrasoBase && ultimo.temAtraso && !gravandoBase_)
        vigiar(vigiaAtraso_, true, estado_.deltaAtrasoMs, cfg_.limiarDeAtrasoMs, EventoDeDrift::Tipo::atraso, -1);
}

void MonitorDeDrift::vigiar(Vigia& v, bool valido, double delta, double limiar, EventoDeDrift::Tipo tipo, int regiao) {
    if (!valido) return;       // sem medição confiável, não muda de opinião
    const int sinal = delta >= limiar ? 1 : delta <= -limiar ? -1 : 0;
    if (sinal == 0) {
        if (std::abs(delta) < 0.8 * limiar) v = Vigia {};
        return;
    }
    if (sinal != v.sinal) {
        v = Vigia {};
        v.sinal = sinal;
        v.desde = agora_;
    }
    if (agora_ - v.desde < cfg_.sustentacao) return;
    // relata uma vez, e de novo só se andar bem mais
    if (v.relatou && std::abs(delta - v.relatado) < 0.6 * limiar) return;
    EventoDeDrift e;
    e.tipo = tipo;
    e.regiao = regiao;
    e.tempo = agora_;
    e.desde = v.desde;
    e.valor = delta;
    eventos_.push_back(e);
    v.relatou = true;
    v.relatado = delta;
}

} // namespace papagaio
