#include "Aparencia.h"

#include "BinaryData.h"

namespace {

constexpr float kRaioDoControle = 9.0f;

// O retângulo que tem desenho de verdade, deixando de fora a margem transparente do arquivo.
juce::Rectangle<int> areaComDesenho(const juce::Image& imagem) {
    const juce::Image::BitmapData pixels(imagem, juce::Image::BitmapData::readOnly);
    int x1 = imagem.getWidth(), y1 = imagem.getHeight(), x2 = 0, y2 = 0;
    for (int y = 0; y < imagem.getHeight(); y += 2)
        for (int x = 0; x < imagem.getWidth(); x += 2)
            if (pixels.getPixelColour(x, y).getAlpha() > 60) {
                x1 = juce::jmin(x1, x);
                y1 = juce::jmin(y1, y);
                x2 = juce::jmax(x2, x);
                y2 = juce::jmax(y2, y);
            }
    return x2 > x1 && y2 > y1 ? juce::Rectangle<int>(x1, y1, x2 - x1 + 1, y2 - y1 + 1) : imagem.getBounds();
}

const juce::Image& original() {
    static const juce::Image imagem = juce::ImageCache::getFromMemory(BinaryData::Logotipo_png, BinaryData::Logotipo_pngSize);
    return imagem;
}

} // namespace

juce::Image logotipo() {
    static const juce::Image recortado = original().getClippedImage(areaComDesenho(original()));
    return recortado;
}

juce::Image iconeDoApp() {
    // O papagaio ocupa o terço da esquerda do logotipo; o nome fica de fora do ícone.
    static const juce::Image icone = [] {
        const auto logo = logotipo();
        const int lado = juce::jmin(logo.getHeight(), juce::roundToInt(logo.getWidth() * 0.325f));
        return logo.getClippedImage({ 0, 0, lado, logo.getHeight() }).rescaled(256, 256, juce::Graphics::highResamplingQuality);
    }();
    return icone;
}

void pintarFundo(juce::Graphics& g, juce::Rectangle<int> area) {
    const auto r = area.toFloat();
    g.setGradientFill(juce::ColourGradient(cores::fundo.brighter(0.06f), r.getX(), r.getY(), cores::fundo.darker(0.25f), r.getX(), r.getBottom(), false));
    g.fillRect(r);
    // manchas de luz: verde no canto de cima, azul à direita, ciano embaixo
    const float raio = juce::jmax(r.getWidth(), r.getHeight()) * 0.62f;
    const auto mancha = [&](juce::Colour cor, float x, float y, float alfa) {
        g.setGradientFill(juce::ColourGradient(cor.withAlpha(alfa), x, y, cor.withAlpha(0.0f), x + raio, y, true));
        g.fillRect(r);
    };
    mancha(cores::primaria, r.getX() + r.getWidth() * 0.08f, r.getY() - r.getHeight() * 0.05f, 0.20f);
    mancha(cores::secundaria, r.getRight() - r.getWidth() * 0.05f, r.getY() + r.getHeight() * 0.25f, 0.30f);
    mancha(cores::apoio, r.getX() + r.getWidth() * 0.35f, r.getBottom() + r.getHeight() * 0.10f, 0.16f);
}

Aparencia::Aparencia() {
    setColourScheme(juce::LookAndFeel_V4::getDarkColourScheme());
#if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName("Segoe UI");
#endif
    setColour(juce::ResizableWindow::backgroundColourId, cores::fundo);
    setColour(juce::TextButton::buttonColourId, cores::botao);
    setColour(juce::TextButton::textColourOffId, cores::texto);
    setColour(juce::ComboBox::backgroundColourId, cores::botao);
    setColour(juce::ComboBox::outlineColourId, cores::vidroBorda);
    setColour(juce::ComboBox::textColourId, cores::texto);
    setColour(juce::ComboBox::arrowColourId, cores::suave);
    setColour(juce::PopupMenu::backgroundColourId, cores::cartao);
    setColour(juce::PopupMenu::textColourId, cores::texto);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, cores::primaria.withAlpha(0.85f));
    setColour(juce::PopupMenu::highlightedTextColourId, cores::fundo);
    setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::backgroundColourId, cores::fundo.withAlpha(0.6f));
    setColour(juce::TextEditor::outlineColourId, cores::vidroBorda);
    setColour(juce::TextEditor::focusedOutlineColourId, cores::primaria);
    setColour(juce::TextEditor::textColourId, cores::texto);
    setColour(juce::Label::textColourId, cores::texto);
    setColour(juce::ScrollBar::thumbColourId, cores::suave.withAlpha(0.6f));
    setColour(juce::TooltipWindow::backgroundColourId, cores::cartao);
    setColour(juce::TooltipWindow::textColourId, cores::texto);
    setColour(juce::TooltipWindow::outlineColourId, cores::vidroBorda);
    setColour(juce::AlertWindow::backgroundColourId, cores::cartao);
    setColour(juce::AlertWindow::textColourId, cores::texto);
    setColour(juce::AlertWindow::outlineColourId, cores::vidroBorda);
}

void Aparencia::drawButtonBackground(juce::Graphics& g, juce::Button& botao, const juce::Colour& cor, bool mouseEmCima, bool apertado) {
    const auto r = botao.getLocalBounds().toFloat().reduced(0.5f);
    const float alfa = botao.isEnabled() ? 1.0f : 0.45f;
    g.setColour(cor.withMultipliedAlpha(alfa));
    g.fillRoundedRectangle(r, kRaioDoControle);
    if (mouseEmCima || apertado) {
        g.setColour(juce::Colours::white.withAlpha(apertado ? 0.14f : 0.07f));
        g.fillRoundedRectangle(r, kRaioDoControle);
    }
    g.setColour(cores::vidroBorda.withMultipliedAlpha(alfa));
    g.drawRoundedRectangle(r, kRaioDoControle, 1.0f);
}

juce::Font Aparencia::getTextButtonFont(juce::TextButton&, int altura) { return fonte(juce::jmin(15.0f, (float) altura * 0.62f)); }

void Aparencia::drawComboBox(juce::Graphics& g, int largura, int altura, bool, int, int, int, int, juce::ComboBox& caixa) {
    const auto r = juce::Rectangle<float>(0.5f, 0.5f, (float) largura - 1.0f, (float) altura - 1.0f);
    const float alfa = caixa.isEnabled() ? 1.0f : 0.45f;
    g.setColour(caixa.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(alfa));
    g.fillRoundedRectangle(r, kRaioDoControle);
    g.setColour(caixa.findColour(juce::ComboBox::outlineColourId).withMultipliedAlpha(alfa));
    g.drawRoundedRectangle(r, kRaioDoControle, 1.0f);

    // a seta: um V fino
    const float cx = (float) largura - 16.0f, cy = (float) altura * 0.5f;
    juce::Path seta;
    seta.startNewSubPath(cx - 5.0f, cy - 2.5f);
    seta.lineTo(cx, cy + 2.5f);
    seta.lineTo(cx + 5.0f, cy - 2.5f);
    g.setColour(caixa.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(alfa));
    g.strokePath(seta, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font Aparencia::getComboBoxFont(juce::ComboBox&) { return fonte(14.5f); }
juce::Font Aparencia::getPopupMenuFont() { return fonte(15.0f); }

void Aparencia::drawPopupMenuBackground(juce::Graphics& g, int largura, int altura) {
    g.fillAll(cores::cartao);
    g.setColour(cores::vidroBorda);
    g.drawRect(0, 0, largura, altura, 1);
}
