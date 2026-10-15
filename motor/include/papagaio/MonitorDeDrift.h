#pragma once

#include <array>
#include <deque>
#include <string>
#include <vector>

#include "papagaio/AnalisadorDuplo.h"

namespace papagaio {

struct ConfigDrift {
    double janelaSala = 60.0;             // segundos de medição para dizer como a sala está agora
    double janelaMix = 180.0;             // média longa do L/R; nunca música contra música
    double duracaoDaReferencia = 180.0;   // "o primeiro bloco do show"
    double sustentacao = 120.0;           // a mudança tem que durar isso para entrar no histórico
    double limiarDb = 2.5;
    double limiarDeVolumeDb = 3.0;
    double limiarDeAtrasoMs = 1.0;
    double coerenciaMinima = 0.35;        // abaixo disso a banda não entra na medição da sala
};

// A foto de agora, para o painel. Indicador dá contexto; não interrompe.
struct EstadoDoDrift {
    bool gravandoPassagem = false;
    double segundosDePassagem = 0;        // medição válida acumulada na linha de base
    bool temBase = false;
    bool temReferencia = false;           // primeiro bloco do show já fechado
    double segundosDeReferencia = 0;

    std::array<float, kNumRegioes> salaDb {};      // mudança da sala desde a passagem
    std::array<bool, kNumRegioes> salaValida {};
    std::array<float, kNumRegioes> mixDb {};       // mudança do equilíbrio do L/R desde o primeiro bloco
    bool mixValido = false;
    float volumeDb = 0;                            // quanto o L/R subiu desde o primeiro bloco, com ponderação A

    bool temAtraso = false;
    double atrasoMs = 0;
    bool temAtrasoBase = false;
    double deltaAtrasoMs = 0;

    bool temNiveis = false;
    float nivelMicADb = -200, nivelRefADb = -200, foraDoPaADb = -200;
    float coerenciaMedia = 0;             // fração do que chega no FOH que vem do PA
    bool temPiso = false;
    float pisoADb = -200;
};

struct EventoDeDrift {
    enum class Tipo { sala, mix, volume, atraso };
    Tipo tipo = Tipo::sala;
    int regiao = -1;
    double tempo = 0;          // quando entrou no histórico
    double desde = 0;          // quando a mudança começou
    double valor = 0;          // dB, ou ms no caso do atraso
};

// Uma linha para o histórico: "Sala: agudo caiu 4,1 dB desde a passagem".
std::string descrever(const EventoDeDrift& e);
// A leitura do padrão, quando houver: causa provável e onde corrigir. Vazio deixa a conclusão com o engenheiro.
std::string sugerir(const EventoDeDrift& e);

// Módulo 2 da proposta: separa o que mudou na sala do que mudou no mix.
//
// Drift da sala: ganho mic/L/R por banda agora contra a linha de base da passagem de som.
// Drift do mix: média longa do L/R agora contra o primeiro bloco do show.
// Só mudança sustentada vira evento, e nada daqui interrompe a tela.
class MonitorDeDrift {
public:
    void preparar(const ConfigDrift& config);

    void iniciarPassagem();        // começa a gravar a linha de base
    void fecharPassagem();
    void marcarInicioDoShow();     // a partir daqui acumula o primeiro bloco, a referência do mix

    void receber(const BlocoDeBandas& bloco);

    const EstadoDoDrift& estado() const { return estado_; }
    const std::vector<EventoDeDrift>& eventos() const { return eventos_; }

private:
    struct Vigia {
        int sinal = 0;
        double desde = 0;
        bool relatou = false;
        double relatado = 0;
    };

    bool bandaValida(const BlocoDeBandas& b, int banda) const;
    void atualizar();
    void vigiar(Vigia& v, bool valido, double delta, double limiar, EventoDeDrift::Tipo tipo, int regiao);

    ConfigDrift cfg_;
    double agora_ = 0;
    std::deque<BlocoDeBandas> recentes_;

    bool gravandoBase_ = false;
    std::array<double, kNumBandas> baseSoma_ {}, basePeso_ {}, base_ {};
    std::array<bool, kNumBandas> baseValida_ {};
    std::vector<double> atrasosDaBase_;
    double atrasoBase_ = 0;

    bool acumulandoRef_ = false;
    std::array<double, kNumBandas> refPotencia_ {};
    double refPotenciaA_ = 0, refDuracao_ = 0;

    std::array<Vigia, kNumRegioes> vigiaSala_, vigiaMix_;
    Vigia vigiaVolume_, vigiaAtraso_;

    EstadoDoDrift estado_;
    std::vector<EventoDeDrift> eventos_;
};

} // namespace papagaio
