#pragma once

#include "Mesa.h"

// Uma vista com os seus seletores. A tela precisa conhecer este bloco para abrir o
// espectrograma num momento do histórico.
class BlocoVista : public Bloco {
public:
    explicit BlocoVista(const Servicos& servicos);

    juce::String tipo() const override { return "vista"; }
    juce::String titulo() const override;
    void atualizar(const Contexto& contexto) override;
    juce::String config() const override;
    void configurar(const juce::String& config) override;
    void resized() override;

    Vista::Tipo vista() const { return vista_.tipo(); }
    void definirVista(Vista::Tipo tipo);
    void irPara(double segundos) { vista_.irPara(segundos); }

private:
    void aplicar(bool avisar);

    Vista vista_;
    juce::ComboBox comboTipo_, comboFracao_, comboMedia_, comboPico_;
};
