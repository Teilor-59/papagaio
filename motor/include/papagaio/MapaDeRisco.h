#pragma once

#include <string>
#include <vector>

namespace papagaio {

enum class Estado : int;
struct Evento;

// Uma frequência onde a malha já fechou no ring-out da passagem de som.
struct FrequenciaDeRisco {
    double f = 0;
    int vezes = 0;                  // quantos alertas do ring-out caíram aqui
    int pior = 0;                   // Estado do pior alerta: 1 = só pré-apito, 2 = laranja, 3 = vermelho
    double nivelMaxDb = -200;       // escala do motor (seno em fundo de escala = 0 dB)
    bool manual = false;            // posta à mão pelo engenheiro, sem alerta
};

// Mapa de risco do ring-out (módulo 1 da proposta): as frequências que apitaram, tocaram em
// ressonância ou deram pré-apito enquanto o engenheiro subia o ganho na passagem. No show,
// o detector vigia essas com prioridade.
class MapaDeRisco {
public:
    // Alertas a menos de 3 % de uma frequência do mapa somam nela; os outros abrem uma nova.
    static constexpr double kJuntar = 0.03;

    // Só laranja, vermelho e pré-apito entram: amarelo comum é suspeita, não risco.
    bool acrescentar(const Evento& e);
    void acrescentarManual(double f);
    void remover(size_t i);
    void limpar() { itens_.clear(); }

    const std::vector<FrequenciaDeRisco>& itens() const { return itens_; }
    bool vazio() const { return itens_.empty(); }
    // Índice da frequência do mapa a menos de `tolerancia` (fração) de f, a mais perto; -1 se nenhuma.
    int perto(double f, double tolerancia = kJuntar) const;

    // "2502.0:3:3:-6.0:0;1250.0:1:2:-20.5:1", para guardar nas preferências.
    std::string emTexto() const;
    static MapaDeRisco deTexto(const std::string& texto);

private:
    std::vector<FrequenciaDeRisco> itens_;      // em ordem de frequência
    void ordenar();
};

} // namespace papagaio
