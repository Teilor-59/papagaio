#pragma once

#include "Visual.h"

// A cara do app: vidro sobre fundo escuro, com a paleta e o logotipo da identidade visual.

// O logotipo da pasta "Identidade Visual", sem as margens vazias, e o papagaio sozinho para o ícone.
juce::Image logotipo();
juce::Image iconeDoApp();

// O fundo das janelas: azul-noite com manchas de luz nas cores da marca. É ele que dá ao vidro o que mostrar.
void pintarFundo(juce::Graphics& g, juce::Rectangle<int> area);

// Botões, caixas de escolha e menus no mesmo vidro dos painéis.
class Aparencia : public juce::LookAndFeel_V4 {
public:
    Aparencia();

    void drawButtonBackground(juce::Graphics& g, juce::Button& botao, const juce::Colour& cor, bool mouseEmCima, bool apertado) override;
    juce::Font getTextButtonFont(juce::TextButton&, int altura) override;
    void drawComboBox(juce::Graphics& g, int largura, int altura, bool apertado, int, int, int, int, juce::ComboBox& caixa) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground(juce::Graphics& g, int largura, int altura) override;
};
