#pragma once

#include <string>
#include <vector>

#include <array>

#include "papagaio/DetectorMicrofonia.h"
#include "papagaio/MedidorDeNivel.h"
#include "papagaio/MonitorDeDrift.h"

// O estado do drift ao fim de cada bloco de medição: vira as curvas do relatório.
struct PontoDeDrift {
    double tempo = 0;
    papagaio::EstadoDoDrift estado;
};

struct DadosDoRelatorio {
    std::string mic;                     // nome do arquivo, com o canal
    std::vector<std::string> linhas;
    double taxa = 0;
    double duracao = 0;
    papagaio::ConfigMicrofonia config;
    std::vector<papagaio::Evento> eventos;
    bool temMapa = false;                // houve ring-out, ou o mapa veio pronto
    double ringOutAte = 0;
    papagaio::MapaDeRisco mapa;

    std::array<papagaio::LeituraDeNivel, papagaio::kNumPonderacoes> niveis;
    bool temCalibracao = false;          // sem calibração os níveis saem em dBFS
    double calibracaoDb = 0;
    bool nivelLento = false;

    bool temDrift = false;               // só quando há L/R da mesa
    std::vector<std::string> lr;
    double passagemAte = 0, showEm = 0;
    papagaio::ConfigDrift configDrift;
    std::vector<papagaio::EventoDeDrift> drift;
    std::vector<PontoDeDrift> curva;
};

// JSON com o resultado da análise. É também o que alimenta a página do relatório.
std::string montarJson(const DadosDoRelatorio& dados);

// Página única, sem dependências, com linha do tempo, tabela e rotulagem de acerto.
std::string montarHtml(const std::string& json);
