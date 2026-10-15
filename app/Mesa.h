#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "Componentes.h"

// O que todo bloco recebe a cada quadro da tela.
struct Contexto {
    Instantaneo inst;
    FaixaDeAlerta::Dados alerta;
    const std::vector<Registro>* historico = nullptr;
    int revisaoDoHistorico = 0;
    Escala escala;
};

// O que um bloco pode pedir à tela. É entregue na criação e não muda.
struct Servicos {
    MotorAoVivo* motor = nullptr;
    CurvaDeReferencia* referencia = nullptr;
    ReferenciaDeEquilibrio* equilibrio = nullptr;
    std::function<void()> calibrar, limparHistorico;
    std::function<void(double)> mostrarNoEspectrograma;
    std::function<int()> media;                 // média do RTA, de 1 a 4; é uma só para todos
    std::function<void(int)> definirMedia;
    std::function<void(bool)> definirNivelLento;
};

// Um painel da tela: faixa de alerta, vista, níveis, sala e mix, histórico ou indicador.
// A mesa cuida de onde ele fica e de que tamanho; o bloco só se desenha no espaço que recebe.
class Bloco : public juce::Component {
public:
    explicit Bloco(const Servicos& servicos) : servicos_(servicos) {}

    virtual juce::String tipo() const = 0;      // chave guardada no layout: "vista", "indicador"...
    virtual juce::String titulo() const = 0;    // nome que aparece ao editar o layout
    virtual void atualizar(const Contexto& contexto) = 0;

    // O que o bloco tem de seu, além da posição: que vista, que valor, que limite.
    virtual juce::String config() const { return {}; }
    virtual void configurar(const juce::String&) {}

    std::function<void()> aoMudar;              // a configuração mudou: a mesa guarda o layout

protected:
    Servicos servicos_;
};

struct TipoDeBloco {
    const char* tipo;
    const char* nome;
    int largura, altura;        // tamanho com que nasce, em células da grade
};
const std::vector<TipoDeBloco>& tiposDeBloco();
std::unique_ptr<Bloco> criarBloco(const juce::String& tipo, const Servicos& servicos);

// A área onde os blocos ficam. Uma grade de 48 por 36 células acompanha o tamanho da janela;
// no modo de edição cada bloco pode ser arrastado, redimensionado pela borda ou fechado.
// Um bloco também pode ser solto da mesa e virar uma janela própria, para ir a outra tela;
// fechar essa janela devolve o bloco ao lugar de onde saiu.
class Mesa : public juce::Component {
public:
    static constexpr int kColunas = 48, kLinhas = 36;

    explicit Mesa(const Servicos& servicos);
    ~Mesa() override;

    void atualizar(const Contexto& contexto);

    void editar(bool editando);
    bool editando() const { return editando_; }

    Bloco* adicionar(const juce::String& tipo, const juce::String& config = {}, juce::Rectangle<int> celulas = {});
    void layoutPadrao(const juce::StringArray& vistas = { "espectro" }, int fracao = 3);
    std::vector<Bloco*> blocos() const;

    juce::String paraTexto() const;
    bool deTexto(const juce::String& xml);

    std::function<void()> aoMudar;

    // Desenvolvimento: uma imagem de cada janela solta, na ordem em que estão no layout.
    std::vector<juce::Image> fotosDasJanelas();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class Moldura;
    class Janela;
    Moldura* emoldurar(std::unique_ptr<Bloco> bloco, juce::Rectangle<int> celulas);
    Janela* abrirJanela(std::unique_ptr<Bloco> bloco, juce::Rectangle<int> celulas, juce::Rectangle<int> naTela);
    void soltar(Moldura* moldura);
    void recolher(Janela* janela);
    void encaixar(Moldura& moldura);
    void posicionar(Moldura& moldura);
    void remover(Moldura* moldura);
    void avisar();

    Servicos servicos_;
    juce::OwnedArray<Moldura> molduras_;
    juce::OwnedArray<Janela> janelas_;      // blocos soltos, cada um na sua janela
    bool editando_ = false;
};
