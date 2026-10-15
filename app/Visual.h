#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "papagaio/DetectorMicrofonia.h"

// Texto com acento: os literais do código estão em UTF-8.
inline juce::String txt(const char* utf8) { return juce::String::fromUTF8(utf8); }

// Número com vírgula decimal.
inline juce::String numero(double v, int casas) {
    if (casas <= 0) return juce::String(juce::roundToInt(v));
    return juce::String(v, casas).replaceCharacter('.', ',');
}

inline juce::Font fonte(float altura, bool negrito = false) {
    return juce::Font(juce::FontOptions(altura, negrito ? juce::Font::bold : juce::Font::plain));
}

// Em que unidade o nível aparece: dB SPL quando o mic está calibrado, dBFS quando não.
// Por dentro o motor conta com um seno em fundo de escala valendo 0 dB; a calibração diz
// quantos dB SPL isso é neste canal e neste ganho.
struct Escala {
    bool calibrado = false;
    double calibracaoDb = 0;

    double mostrar(double db) const { return calibrado ? db + calibracaoDb : db; }
    juce::String unidade() const { return calibrado ? "dB SPL" : "dBFS"; }
    juce::String texto(double db, int casas) const {
        const double v = mostrar(db);
        return (v < 0 ? txt("−") : juce::String()) + numero(std::abs(v), casas) + " " + unidade();
    }
};

// Paleta da identidade visual do Papagaio.
namespace cores {
inline const juce::Colour primaria { 0xff00d084 };      // verde: a cor da marca e do sinal
inline const juce::Colour secundaria { 0xff2563eb };    // azul
inline const juce::Colour destaque { 0xfffacc15 };      // amarelo
inline const juce::Colour apoio { 0xff06b6d4 };         // ciano
inline const juce::Colour apoioQuente { 0xfff97316 };   // laranja
inline const juce::Colour fundo { 0xff0b1220 };
inline const juce::Colour cartao { 0xff1e293b };        // superfície
inline const juce::Colour texto { 0xfff8fafc };
inline const juce::Colour suave { 0xff94a3b8 };         // texto secundário: o texto, apagado em direção à superfície
inline const juce::Colour borda { 0xff334155 };         // linhas de grade
inline const juce::Colour tinta { 0xff0b1220 };         // texto sobre cor forte

// vidro: superfície translúcida, com um fio claro na borda
inline const juce::Colour vidro { 0xa61e293b };
inline const juce::Colour vidroBorda { 0x24ffffff };
inline const juce::Colour botao { 0x14ffffff };

// estados de alerta. A paleta não tem vermelho; apito precisa de um.
inline const juce::Colour vermelho { 0xffef4444 };
inline const juce::Colour laranja = apoioQuente;
inline const juce::Colour amarelo = destaque;

// séries dos gráficos
inline const juce::Colour sinal = primaria;             // barras do RTA e traços do mic
inline const juce::Colour linhas = apoio;               // espectro das linhas da mesa
inline const juce::Colour sala = apoio;                 // par sala e mix: ciano e laranja
inline const juce::Colour mix = apoioQuente;

inline juce::Colour doEstado(papagaio::Estado e) {
    switch (e) {
        case papagaio::Estado::vermelho: return vermelho;
        case papagaio::Estado::laranja: return laranja;
        case papagaio::Estado::amarelo: return amarelo;
        default: return suave;
    }
}
} // namespace cores

// O fundo de todo painel: vidro fosco, com brilho em cima e um fio de luz na borda.
inline void pintarVidro(juce::Graphics& g, juce::Rectangle<float> r, float raio = 14.0f) {
    g.setColour(cores::vidro);
    g.fillRoundedRectangle(r, raio);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), r.getX(), r.getY(), juce::Colours::white.withAlpha(0.0f), r.getX(),
                                           r.getY() + juce::jmin(r.getHeight(), 110.0f), false));
    g.fillRoundedRectangle(r, raio);
    g.setColour(cores::vidroBorda);
    g.drawRoundedRectangle(r.reduced(0.5f), raio, 1.0f);
}
