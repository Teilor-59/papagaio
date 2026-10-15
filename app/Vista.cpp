#include <algorithm>
#include <cmath>
#include <cstring>

#include "Componentes.h"
#include "papagaio/IgualAudibilidade.h"

using papagaio::AnalisadorDeEspectro;

namespace {

constexpr double kFreqMin = 20.0, kFreqMax = 20000.0;
constexpr float kMargemEsquerda = 34.0f;

// Mapa de cor do espectrograma: um caminho só do escuro ao claro, que lê igual em tons de cinza.
juce::Colour corDoNivel(float t) {
    static const juce::uint32 paradas[] = { 0xff000004, 0xff1b0c41, 0xff4a0c6b, 0xff781c6d, 0xffa52c60,
                                            0xffcf4446, 0xffed6925, 0xfffb9b06, 0xfff7d13d, 0xfffcffa4 };
    constexpr int n = (int) std::size(paradas);
    const float p = juce::jlimit(0.0f, 1.0f, t) * (float) (n - 1);
    const int i = juce::jmin(n - 2, (int) p);
    return juce::Colour(paradas[i]).interpolatedWith(juce::Colour(paradas[i + 1]), p - (float) i);
}

constexpr float kRtaMin = -96.0f, kRtaMax = 0.0f;      // faixa do RTA, na escala do motor
constexpr float kRaioDoPonto = 5.0f, kAlcance = 11.0f;  // ponto da referência e a distância em que o mouse o pega

constexpr float kEspectrogramaMin = -100.0f, kEspectrogramaMax = -15.0f;      // dB mostrados de ponta a ponta do mapa

} // namespace

double CurvaDeReferencia::em(double f) const {
    if (pontos.empty()) return 0.0;
    if (f <= pontos.front().f) return pontos.front().db;
    if (f >= pontos.back().f) return pontos.back().db;
    for (size_t i = 1; i < pontos.size(); ++i) {
        if (f > pontos[i].f) continue;
        const double t = std::log(f / pontos[i - 1].f) / std::log(pontos[i].f / pontos[i - 1].f);
        return pontos[i - 1].db + t * (pontos[i].db - pontos[i - 1].db);
    }
    return pontos.back().db;
}

juce::String CurvaDeReferencia::paraTexto() const {
    juce::StringArray partes;
    for (const auto& p : pontos) partes.add(juce::String(p.f, 2) + ":" + juce::String(p.db, 2));
    return juce::String(fracao) + "|" + (visivel ? "1" : "0") + "|" + partes.joinIntoString(";");
}

void CurvaDeReferencia::deTexto(const juce::String& texto) {
    const auto campos = juce::StringArray::fromTokens(texto, "|", "");
    pontos.clear();
    if (campos.size() < 3) return;
    fracao = juce::jlimit(1, 48, campos[0].getIntValue());
    visivel = campos[1] != "0";
    for (const auto& par : juce::StringArray::fromTokens(campos[2], ";", "")) {
        const double f = par.upToFirstOccurrenceOf(":", false, false).getDoubleValue();
        if (f >= 10.0 && par.contains(":")) pontos.push_back({ f, par.fromFirstOccurrenceOf(":", false, false).getDoubleValue() });
    }
    std::sort(pontos.begin(), pontos.end(), [](const Ponto& a, const Ponto& b) { return a.f < b.f; });
}

juce::String ReferenciaDeEquilibrio::paraTexto() const {
    if (!tem) return {};
    juce::StringArray partes;
    partes.add(juce::String(nivelZ, 2));
    for (const double b : bandas) partes.add(juce::String(b, 2));
    return partes.joinIntoString(";");
}

void ReferenciaDeEquilibrio::deTexto(const juce::String& texto) {
    const auto partes = juce::StringArray::fromTokens(texto, ";", "");
    tem = partes.size() == papagaio::kNumBandas + 1;
    if (!tem) return;
    nivelZ = partes[0].getDoubleValue();
    for (int b = 0; b < papagaio::kNumBandas; ++b) bandas[(size_t) b] = partes[b + 1].getDoubleValue();
}

juce::StringArray Vista::nomes() {
    return { "Nenhuma", "Espectro", "RTA em bandas", "Espectrograma", txt("Transferência (mic ÷ L/R)"), txt("Equilíbrio pelo volume (phon)") };
}

Vista::Vista() {
    const auto passo = [this](juce::TextButton& botao, const char* nome, double segundos) {
        botao.setButtonText(txt(nome));
        botao.onClick = [this, segundos] { andar((long long) std::llround(segundos * linhasPorSegundo_)); };
        addChildComponent(botao);
    };
    passo(botaoMenosMinuto_, "−1 min", -60.0);
    passo(botaoMenos10_, "−10 s", -10.0);
    passo(botaoMais10_, "+10 s", 10.0);
    botaoAoVivo_.setButtonText("Tempo real");
    botaoAoVivo_.setTooltip(txt("Volta a acompanhar o agora. Para rever, role a roda do mouse ou arraste o espectrograma."));
    botaoAoVivo_.onClick = [this] {
        aoVivo_ = true;
        repaint();
    };
    addChildComponent(botaoAoVivo_);

    botaoReferencia_.setButtonText(txt("Referência"));
    botaoReferencia_.setTooltip(txt("Curva de referência por cima do RTA: trace a sua, ou capture o que está tocando."));
    botaoReferencia_.onClick = [this] { menuDaReferencia(); };
    addChildComponent(botaoReferencia_);

    botaoMarcar_.setButtonText(txt("Marcar referência"));
    botaoMarcar_.setTooltip(txt("Guarda o nível e o espectro de agora como o ponto em que o equilíbrio estava bom. O gráfico passa a comparar com ele: "
                                "acima de zero, a região soa mais presente do que soava na referência; abaixo, menos."));
    botaoMarcar_.onClick = [this] { marcarEquilibrio(); };
    addChildComponent(botaoMarcar_);

    // estes botões ficam por cima do gráfico: precisam de um fundo que se sustente sozinho
    for (auto* b : { &botaoMenosMinuto_, &botaoMenos10_, &botaoMais10_, &botaoAoVivo_, &botaoReferencia_, &botaoMarcar_ })
        b->setColour(juce::TextButton::buttonColourId, cores::cartao.withAlpha(0.9f));
}

void Vista::definirTipo(Tipo tipo) {
    if (tipo == tipo_) return;
    tipo_ = tipo;
    imagem_ = juce::Image();
    fimDesenhado_ = -1;
    aoVivo_ = true;
    for (auto* b : { &botaoMenosMinuto_, &botaoMenos10_, &botaoMais10_, &botaoAoVivo_ }) b->setVisible(tipo == Tipo::espectrograma);
    botaoReferencia_.setVisible(tipo == Tipo::rta);
    botaoMarcar_.setVisible(tipo == Tipo::equilibrio);
    temSuave_ = false;
    repaint();
}

void Vista::resized() {
    // os botões de tempo ficam no canto de cima do espectrograma, à esquerda
    auto r = juce::Rectangle<int>(12 + (int) kMargemEsquerda + 6, 10 + 6, 320, 22);
    botaoMenosMinuto_.setBounds(r.removeFromLeft(62));
    r.removeFromLeft(4);
    botaoMenos10_.setBounds(r.removeFromLeft(54));
    r.removeFromLeft(4);
    botaoMais10_.setBounds(r.removeFromLeft(54));
    r.removeFromLeft(10);
    botaoAoVivo_.setBounds(r.removeFromLeft(96));
    botaoReferencia_.setBounds(12 + (int) kMargemEsquerda + 6, 10 + 6, 96, 22);
    botaoMarcar_.setBounds(12 + (int) kMargemEsquerda + 6, 10 + 6, 138, 22);
}

void Vista::irPara(double segundos) { pedidoDeIrPara_ = segundos; }

// Sai do agora e anda no tempo: negativo vai para o passado.
void Vista::andar(long long linhas) {
    fim_ = (aoVivo_ ? total_ : fim_) + linhas;
    aoVivo_ = false;
    limitar();
    repaint();
}

void Vista::limitar() {
    const long long minimo = juce::jmin(total_, primeira_ + kLinhasVisiveis);
    fim_ = juce::jlimit(minimo, juce::jmax(minimo, total_), fim_);
    if (fim_ >= total_) aoVivo_ = true;      // chegou no agora: volta a acompanhar
}

void Vista::mouseDown(const juce::MouseEvent& e) {
    fimNoArrasto_ = aoVivo_ ? total_ : fim_;
    arrastando_ = -1;
    if (tipo_ != Tipo::rta || referencia_ == nullptr || !referencia_->visivel || referencia_->pontos.empty()) return;
    arrastando_ = pontoPerto(e.position);
    if (arrastando_ < 0 && e.mods.isShiftDown()) arrastando_ = kLinhaToda;
    pontosNoArrasto_ = referencia_->pontos;
}

void Vista::mouseDrag(const juce::MouseEvent& e) {
    if (area_.getHeight() <= 0) return;
    if (tipo_ == Tipo::espectrograma) {
        // o conteúdo acompanha a mão: arrastar para cima traz o que estava embaixo, que é o passado
        fim_ = fimNoArrasto_ + (long long) std::llround(e.getDistanceFromDragStartY() * kLinhasVisiveis / area_.getHeight());
        aoVivo_ = false;
        limitar();
        repaint();
        return;
    }
    if (tipo_ != Tipo::rta || referencia_ == nullptr || arrastando_ == -1) return;
    auto& pontos = referencia_->pontos;
    if (arrastando_ == kLinhaToda) {
        // com Shift a linha inteira sobe e desce, sem mudar de forma
        const double delta = dbDoRta(e.position.y) - dbDoRta(e.mouseDownPosition.y);
        for (size_t i = 0; i < pontos.size() && i < pontosNoArrasto_.size(); ++i) pontos[i].db = pontosNoArrasto_[i].db + delta;
    } else if (arrastando_ < (int) pontos.size()) {
        // o ponto anda livre na vertical e, na horizontal, até encostar nos vizinhos
        const size_t i = (size_t) arrastando_;
        const double de = i > 0 ? pontos[i - 1].f * 1.03 : kFreqMin;
        const double ate = i + 1 < pontos.size() ? pontos[i + 1].f / 1.03 : kFreqMax;
        const double f = kFreqMin * std::pow(kFreqMax / kFreqMin, (double) ((e.position.x - area_.getX()) / area_.getWidth()));
        pontos[i].f = juce::jlimit(de, juce::jmax(de, ate), f);
        pontos[i].db = dbDoRta(e.position.y) - deslocamentoDaReferencia();
    }
    repaint();
}

void Vista::mouseUp(const juce::MouseEvent&) {
    if (arrastando_ != -1 && referencia_ != nullptr && referencia_->aoMudar) referencia_->aoMudar();
    arrastando_ = -1;
}

// Clique duplo cria um ponto onde clicou, ou apaga o que estiver debaixo.
void Vista::mouseDoubleClick(const juce::MouseEvent& e) {
    if (tipo_ != Tipo::rta || referencia_ == nullptr || !area_.contains(e.position)) return;
    auto& pontos = referencia_->pontos;
    const int perto = referencia_->visivel ? pontoPerto(e.position) : -1;
    if (perto >= 0) {
        pontos.erase(pontos.begin() + perto);
    } else {
        const double f = kFreqMin * std::pow(kFreqMax / kFreqMin, (double) ((e.position.x - area_.getX()) / area_.getWidth()));
        if (pontos.empty()) referencia_->fracao = fracao_;
        pontos.push_back({ f, dbDoRta(e.position.y) - deslocamentoDaReferencia() });
        std::sort(pontos.begin(), pontos.end(), [](const auto& a, const auto& b) { return a.f < b.f; });
        referencia_->visivel = true;
    }
    if (referencia_->aoMudar) referencia_->aoMudar();
    repaint();
}

void Vista::mouseMove(const juce::MouseEvent& e) {
    if (tipo_ != Tipo::espectrograma && tipo_ != Tipo::rta) return;
    temCursor_ = true;
    cursor_ = e.position;
    repaint();
}

void Vista::mouseExit(const juce::MouseEvent&) {
    temCursor_ = false;
    repaint();
}

void Vista::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& roda) {
    if (tipo_ != Tipo::espectrograma) {
        Component::mouseWheelMove(e, roda);
        return;
    }
    // roda para baixo desce para o passado, uns 4 s por clique
    andar((long long) std::llround(roda.deltaY * 400.0f));
}

// Desenha na imagem as linhas y0 a y0 + n - 1, a partir dos bytes guardados.
void Vista::renderizar(int y0, int n) {
    static juce::Colour cores256[256];
    static bool pronto = false;
    if (!pronto) {
        for (int v = 0; v < 256; ++v) {
            const float db = (float) v * (120.0f / 255.0f) - 120.0f;
            cores256[v] = corDoNivel((db - kEspectrogramaMin) / (kEspectrogramaMax - kEspectrogramaMin));
        }
        pronto = true;
    }
    const int colunas = AnalisadorDeEspectro::kNumBandas;
    juce::Image::BitmapData pixels(imagem_, 0, y0, colunas, n, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < n; ++y) {
        const juce::uint8* linha = visiveis_.data() + (size_t) (y0 + y) * (size_t) colunas;
        for (int c = 0; c < colunas; ++c) pixels.setPixelColour(c, y, cores256[linha[c]]);
    }
}

void Vista::atualizarEspectrograma(const MotorAoVivo& motor) {
    const int colunas = AnalisadorDeEspectro::kNumBandas, linhas = kLinhasVisiveis;
    const size_t bytesPorLinha = (size_t) colunas;
    if (!imagem_.isValid()) {
        imagem_ = juce::Image(juce::Image::RGB, colunas, linhas, false);
        visiveis_.assign((size_t) linhas * bytesPorLinha, 0);
        fimDesenhado_ = -1;
    }
    const auto e = motor.espectrograma();
    linhasPorSegundo_ = motor.linhasPorSegundo();
    if (e.geracao != geracao_) {       // a fonte recomeçou
        geracao_ = e.geracao;
        aoVivo_ = true;
        fimDesenhado_ = -1;
    }
    total_ = e.total;
    primeira_ = e.primeira;
    if (pedidoDeIrPara_ >= 0) {
        // o instante pedido fica no meio da altura, com o que veio depois em cima
        fim_ = (long long) std::llround(pedidoDeIrPara_ * linhasPorSegundo_) + linhas / 2;
        aoVivo_ = false;
        pedidoDeIrPara_ = -1;
    }
    if (!aoVivo_) limitar();
    botaoAoVivo_.setEnabled(!aoVivo_);
    botaoMais10_.setEnabled(!aoVivo_);

    const long long fim = aoVivo_ ? total_ : fim_;
    if (fim == fimDesenhado_) return;

    // Caso comum, ao vivo: chegaram poucas linhas. A imagem desce e as novas entram em cima.
    const long long novas = fim - fimDesenhado_;
    if (fimDesenhado_ >= 0 && novas > 0 && novas < linhas) {
        long long primeira = fimDesenhado_;
        if (motor.lerEspectrograma(primeira, (int) novas, novas_) == novas && primeira == fimDesenhado_) {
            const int n = (int) novas;
            std::memmove(visiveis_.data() + (size_t) n * bytesPorLinha, visiveis_.data(), (size_t) (linhas - n) * bytesPorLinha);
            for (int i = 0; i < n; ++i)      // a mais recente na linha 0
                std::memcpy(visiveis_.data() + (size_t) (n - 1 - i) * bytesPorLinha, novas_.data() + (size_t) i * bytesPorLinha, bytesPorLinha);
            imagem_.moveImageSection(0, n, 0, 0, colunas, linhas - n);
            renderizar(0, n);
            fimDesenhado_ = fim;
            return;
        }
    }

    // Senão, monta a janela inteira: é o que acontece ao rever.
    std::fill(visiveis_.begin(), visiveis_.end(), (juce::uint8) 0);
    long long primeira = fim - linhas;
    const int n = motor.lerEspectrograma(primeira, (int) (fim - juce::jmax(primeira, 0LL)), novas_);
    for (int i = 0; i < n; ++i) {
        const long long y = fim - 1 - (primeira + i);
        if (y >= 0 && y < linhas) std::memcpy(visiveis_.data() + (size_t) y * bytesPorLinha, novas_.data() + (size_t) i * bytesPorLinha, bytesPorLinha);
    }
    renderizar(0, linhas);
    fimDesenhado_ = fim;
}

void Vista::definirFracao(int porOitava) {
    if (porOitava != fracao_) zerarPico();       // o pico de uma banda larga não vale para a estreita
    fracao_ = porOitava;
    repaint();
}

void Vista::definirPico(int segundos) {
    if (segundos == pico_) return;
    pico_ = segundos;
    zerarPico();
}

void Vista::zerarPico() {
    picoDb_.clear();
    picoDesde_.clear();
    ultimoTempoDoPico_ = -1;
    repaint();
}

// Cada banda guarda o maior nível que já leu. Com tempo de retenção, passado esse tempo o pico cai
// a 10 dB/s até encontrar o nível de agora. O relógio é o da fonte: em arquivo acelerado, segura
// os mesmos segundos do show.
void Vista::atualizarPico() {
    constexpr double kQuedaDbPorSegundo = 10.0;
    const int n = AnalisadorDeEspectro::kNumBandas;
    if ((int) i_.bandas48.size() != n) return;
    if (i_.tempo < ultimoTempoDoPico_) zerarPico();         // trocou de fonte ou recomeçou o arquivo
    if ((int) picoDb_.size() != n) {
        picoDb_.assign((size_t) n, -400.0);
        picoDesde_.assign((size_t) n, i_.tempo);
    }
    const double dt = ultimoTempoDoPico_ < 0 ? 0.0 : i_.tempo - ultimoTempoDoPico_;
    ultimoTempoDoPico_ = i_.tempo;
    const int largura = AnalisadorDeEspectro::kPorOitava / juce::jmax(1, fracao_);
    for (int c = 0; c < n; ++c) {
        if ((c + AnalisadorDeEspectro::kPrimeira) % largura != 0) continue;
        const double agora = nivelDaBanda(c, largura);
        auto& pico = picoDb_[(size_t) c];
        auto& desde = picoDesde_[(size_t) c];
        if (agora >= pico) {
            pico = agora;
            desde = i_.tempo;
        } else if (pico_ > 0 && i_.tempo - desde > pico_) {
            pico = juce::jmax(agora, pico - kQuedaDbPorSegundo * dt);
        }
    }
}

void Vista::mostrar(const Instantaneo& i, const MotorAoVivo& motor) {
    if (tipo_ == Tipo::nenhuma) return;
    i_ = i;
    if (tipo_ == Tipo::espectrograma) atualizarEspectrograma(motor);
    if (tipo_ == Tipo::equilibrio) suavizarEquilibrio();
    if (tipo_ == Tipo::rta && pico_ != 0) atualizarPico();
    repaint();
}

float Vista::xDe(double f) const {
    return area_.getX() + area_.getWidth() * (float) (std::log(juce::jlimit(kFreqMin, kFreqMax, f) / kFreqMin) / std::log(kFreqMax / kFreqMin));
}

// Eixo de frequência embaixo e, se pedido, linhas de nível com o valor à esquerda.
void Vista::grade(juce::Graphics& g, float dbMin, float dbMax, float passoDb) const {
    g.setFont(fonte(12.0f));
    for (const double f : { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 }) {
        const float x = xDe(f);
        g.setColour(cores::borda.withAlpha(0.6f));
        g.drawVerticalLine(juce::roundToInt(x), area_.getY(), area_.getBottom());
        g.setColour(cores::suave);
        const juce::String rotulo = f >= 1000.0 ? juce::String(f / 1000.0, 0) + "k" : f == 31.5 ? juce::String("31,5") : juce::String(f, 0);
        g.drawText(rotulo, juce::Rectangle<float>(x - 24.0f, area_.getBottom() + 2.0f, 48.0f, 14.0f), juce::Justification::centred);
    }
    if (passoDb <= 0) return;
    // Calibrado, as linhas caem em dezenas de dB SPL; sem calibração, no passo pedido em dBFS.
    const Escala escala = escalaDe(i_);
    const float passo = escala.calibrado ? 20.0f : passoDb;
    const float desloc = (float) (escala.calibrado ? escala.calibracaoDb : 0.0);
    for (float v = std::floor((dbMax + desloc) / passo) * passo; v >= dbMin + desloc; v -= passo) {
        const float y = area_.getBottom() - area_.getHeight() * (v - desloc - dbMin) / (dbMax - dbMin);
        g.setColour(cores::borda.withAlpha(0.6f));
        g.drawHorizontalLine(juce::roundToInt(y), area_.getX(), area_.getRight());
        g.setColour(cores::suave);
        g.drawText((v < 0 ? txt("−") : juce::String()) + juce::String(juce::roundToInt(std::abs(v))),
                   juce::Rectangle<float>(area_.getX() - kMargemEsquerda, y - 7.0f, kMargemEsquerda - 5.0f, 14.0f), juce::Justification::centredRight);
    }
    g.setFont(fonte(11.0f));
    g.drawText(escala.unidade(), juce::Rectangle<float>(area_.getX() - kMargemEsquerda - 8.0f, area_.getBottom() + 2.0f, kMargemEsquerda + 6.0f, 14.0f),
               juce::Justification::centredRight);
}

void Vista::marcarSuspeitas(juce::Graphics& g) const {
    // o mapa de risco do ring-out: um traço discreto em cada frequência, com o valor logo abaixo do das suspeitas
    g.setFont(fonte(11.0f));
    for (const auto& r : i_.mapa.itens()) {
        const float x = xDe(r.f);
        g.setColour(cores::laranja.withAlpha(0.45f));
        const float tracos[] = { 4.0f, 4.0f };
        g.drawDashedLine(juce::Line<float>(x, area_.getY(), x, area_.getBottom()), tracos, 2, 1.0f);
        g.setColour(cores::laranja.withAlpha(0.8f));
        g.drawText(juce::String(juce::roundToInt(r.f)), juce::Rectangle<float>(x + 4.0f, area_.getY() + 16.0f, 60.0f, 14.0f),
                   juce::Justification::centredLeft);
    }
    g.setFont(fonte(13.0f, true));
    for (const auto& s : i_.suspeitas) {
        const float x = xDe(s.f);
        g.setColour(cores::doEstado(s.estado));
        g.fillRect(x - 1.0f, area_.getY(), 2.0f, area_.getHeight());
        g.drawText(juce::String(juce::roundToInt(s.f)), juce::Rectangle<float>(x + 5.0f, area_.getY(), 70.0f, 16.0f), juce::Justification::centredLeft);
    }
}

void Vista::aviso(juce::Graphics& g, const juce::String& texto) const {
    g.setColour(cores::suave);
    g.setFont(fonte(14.0f));
    g.drawFittedText(texto, area_.toNearestInt().reduced(20, 0), juce::Justification::centred, 3);
}

void Vista::paint(juce::Graphics& g) {
    pintarVidro(g, getLocalBounds().toFloat());
    area_ = getLocalBounds().toFloat().reduced(12.0f, 10.0f).withTrimmedBottom(16.0f).withTrimmedLeft(kMargemEsquerda);
    switch (tipo_) {
        case Tipo::espectro: pintarEspectro(g); break;
        case Tipo::rta: pintarRta(g); break;
        case Tipo::espectrograma: pintarEspectrograma(g); break;
        case Tipo::transferencia: pintarTransferencia(g); break;
        case Tipo::equilibrio: pintarEquilibrio(g); break;
        case Tipo::nenhuma: break;
    }
}

void Vista::pintarEspectro(juce::Graphics& g) {
    constexpr float dbMin = -108.0f, dbMax = 0.0f;
    grade(g, dbMin, dbMax, 24.0f);
    const auto yDe = [&](float db) { return area_.getBottom() - area_.getHeight() * (juce::jlimit(dbMin, dbMax, db) - dbMin) / (dbMax - dbMin); };
    const auto tracar = [&](const std::vector<float>& mag, juce::Colour cor, float espessura) {
        if (mag.size() < 4 || i_.hzPorBin <= 0) return;
        juce::Path p;
        bool primeiro = true;
        const float largura = area_.getWidth();
        const int ultimo = (int) mag.size() - 1;
        for (float x = 0; x < largura; x += 2.0f) {
            const double f1 = kFreqMin * std::pow(kFreqMax / kFreqMin, x / largura);
            const double f2 = kFreqMin * std::pow(kFreqMax / kFreqMin, (x + 2.0f) / largura);
            const int k1 = juce::jlimit(1, ultimo, (int) (f1 / i_.hzPorBin));
            const int k2 = juce::jlimit(k1, ultimo, (int) (f2 / i_.hzPorBin));
            float v = mag[(size_t) k1];
            for (int k = k1 + 1; k <= k2; ++k) v = juce::jmax(v, mag[(size_t) k]);
            if (primeiro)
                p.startNewSubPath(area_.getX() + x, yDe(v));
            else
                p.lineTo(area_.getX() + x, yDe(v));
            primeiro = false;
        }
        g.setColour(cor);
        g.strokePath(p, juce::PathStrokeType(espessura));
    };
    if (i_.usandoLinhas) tracar(i_.espectroLinhas, cores::linhas.withAlpha(0.8f), 1.0f);
    tracar(i_.espectroMic, cores::sinal, 1.5f);
    marcarSuspeitas(g);
    if (i_.usandoLinhas) {
        g.setFont(fonte(12.0f));
        g.setColour(cores::linhas);
        g.drawText("linhas", area_.withTrimmedRight(4.0f).removeFromTop(14.0f), juce::Justification::centredRight);
    }
}

// Nível, na escala do motor, da banda de `largura` bandas de 1/48 em volta de `centro`.
// As duas das pontas entram pela metade.
double Vista::nivelDaBanda(int centro, int largura) const {
    const int n = AnalisadorDeEspectro::kNumBandas;
    double p = 0;
    for (int j = centro - largura / 2; j <= centro + largura / 2; ++j) {
        if (j < 0 || j >= n) continue;
        const double peso = largura > 1 && (j == centro - largura / 2 || j == centro + largura / 2) ? 0.5 : 1.0;
        p += peso * std::pow(10.0, i_.bandas48[(size_t) j] / 10.0);
    }
    return 10.0 * std::log10(p + 1e-20);
}

float Vista::yDoRta(double db) const {
    return area_.getBottom() - area_.getHeight() * (float) ((juce::jlimit((double) kRtaMin, (double) kRtaMax, db) - kRtaMin) / (kRtaMax - kRtaMin));
}

double Vista::dbDoRta(float y) const { return kRtaMin + (kRtaMax - kRtaMin) * (double) ((area_.getBottom() - y) / area_.getHeight()); }

// Quanto somar ao nível guardado na curva para chegar à escala do motor, na banda que o RTA está usando.
// A curva está na unidade da tela, e foi traçada numa largura de banda: banda mais estreita pega menos energia.
double Vista::deslocamentoDaReferencia() const {
    if (referencia_ == nullptr) return 0.0;
    return -(i_.calibrado ? i_.calibracaoDb : 0.0) + 10.0 * std::log10((double) referencia_->fracao / juce::jmax(1, fracao_));
}

int Vista::pontoPerto(juce::Point<float> onde) const {
    if (referencia_ == nullptr) return -1;
    int melhor = -1;
    float distancia = kAlcance;
    for (size_t i = 0; i < referencia_->pontos.size(); ++i) {
        const auto& p = referencia_->pontos[i];
        const float d = onde.getDistanceFrom({ xDe(p.f), yDoRta(p.db + deslocamentoDaReferencia()) });
        if (d < distancia) {
            distancia = d;
            melhor = (int) i;
        }
    }
    return melhor;
}

void Vista::menuDaReferencia() {
    if (referencia_ == nullptr) return;
    juce::PopupMenu menu;
    menu.addItem(1, txt("Mostrar a referência"), true, referencia_->visivel);
    menu.addSeparator();
    menu.addItem(2, "Capturar o RTA de agora");
    menu.addItem(3, txt("Começar com uma reta"));
    menu.addItem(4, "Apagar", !referencia_->pontos.empty());
    juce::Component::SafePointer<Vista> eu(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(botaoReferencia_), [eu](int escolha) {
        if (eu == nullptr || eu->referencia_ == nullptr || escolha == 0) return;
        auto& r = *eu->referencia_;
        if (escolha == 1) r.visivel = !r.visivel;
        if (escolha == 2) eu->capturarReferencia();
        if (escolha == 3) eu->referenciaReta();
        if (escolha == 4) r.pontos.clear();
        if (r.aoMudar) r.aoMudar();
        eu->repaint();
    });
}

// O RTA de agora, em 1/3 de oitava, vira a curva: 29 pontos que depois se ajustam à mão.
void Vista::capturarReferencia() {
    if ((int) i_.bandas48.size() != AnalisadorDeEspectro::kNumBandas) return;
    auto& r = *referencia_;
    r.pontos.clear();
    r.fracao = 3;
    r.visivel = true;
    const double unidade = i_.calibrado ? i_.calibracaoDb : 0.0;
    for (int c = 0; c < AnalisadorDeEspectro::kNumBandas; ++c) {
        if ((c + AnalisadorDeEspectro::kPrimeira) % 16 != 0) continue;
        const double f = AnalisadorDeEspectro::centro(c);
        if (f < 24.0 || f > 17000.0) continue;
        r.pontos.push_back({ f, nivelDaBanda(c, 16) + unidade });
    }
}

// Uma reta na altura média do que está tocando, com poucos pontos para puxar.
void Vista::referenciaReta() {
    auto& r = *referencia_;
    double soma = 0;
    int n = 0;
    if ((int) i_.bandas48.size() == AnalisadorDeEspectro::kNumBandas) {
        for (int c = 0; c < AnalisadorDeEspectro::kNumBandas; ++c) {
            const double f = AnalisadorDeEspectro::centro(c);
            if ((c + AnalisadorDeEspectro::kPrimeira) % 16 != 0 || f < 60.0 || f > 8500.0) continue;
            soma += nivelDaBanda(c, 16);
            ++n;
        }
    }
    const double nivel = (n > 0 ? juce::jmax(-80.0, soma / n) : -40.0) + (i_.calibrado ? i_.calibracaoDb : 0.0);
    r.pontos.clear();
    r.fracao = 3;
    r.visivel = true;
    for (const double f : { 31.5, 125.0, 500.0, 2000.0, 8000.0, 16000.0 }) r.pontos.push_back({ f, nivel });
}

void Vista::pintarReferencia(juce::Graphics& g) {
    if (referencia_ == nullptr || !referencia_->visivel) return;
    const auto& pontos = referencia_->pontos;
    g.setFont(fonte(12.0f));
    if (pontos.empty()) {
        if (temCursor_) {
            g.setColour(cores::suave);
            g.drawText(txt("clique duplo no gráfico para traçar a referência"), area_.withTrimmedLeft(8.0f).removeFromBottom(18.0f), juce::Justification::centredLeft);
        }
        return;
    }
    const double desloc = deslocamentoDaReferencia();
    juce::Path linha;
    linha.startNewSubPath(area_.getX(), yDoRta(pontos.front().db + desloc));
    for (const auto& p : pontos) linha.lineTo(xDe(p.f), yDoRta(p.db + desloc));
    linha.lineTo(area_.getRight(), yDoRta(pontos.back().db + desloc));

    {
        const juce::Graphics::ScopedSaveState estado(g);
        g.reduceClipRegion(area_.toNearestInt());
        // clara com contorno escuro: aparece tanto em cima das barras quanto do fundo
        g.setColour(cores::cartao.withAlpha(0.85f));
        g.strokePath(linha, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(cores::texto);
        g.strokePath(linha, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const int perto = temCursor_ ? pontoPerto(cursor_) : -1;
        // com muitos pontos (curva capturada) eles só aparecem com o mouse por perto, para não poluir
        const bool mostrarPontos = pontos.size() <= 12 || temCursor_;
        for (size_t i = 0; mostrarPontos && i < pontos.size(); ++i) {
            const float x = xDe(pontos[i].f), y = yDoRta(pontos[i].db + desloc);
            const float raio = (int) i == perto || (int) i == arrastando_ ? kRaioDoPonto + 2.0f : pontos.size() <= 12 ? kRaioDoPonto : 3.0f;
            g.setColour(cores::cartao);
            g.fillEllipse(x - raio - 1.5f, y - raio - 1.5f, 2.0f * raio + 3.0f, 2.0f * raio + 3.0f);
            g.setColour(cores::texto);
            g.fillEllipse(x - raio, y - raio, 2.0f * raio, 2.0f * raio);
        }
    }

    // o ponto debaixo do mouse diz quanto vale, na unidade da tela
    const int perto = arrastando_ >= 0 ? arrastando_ : temCursor_ ? pontoPerto(cursor_) : -1;
    if (perto >= 0 && perto < (int) pontos.size()) {
        const auto& p = pontos[(size_t) perto];
        const double f = p.f;
        const juce::String texto = (f >= 1000.0 ? numero(f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : numero(f, f < 100.0 ? 1 : 0) + " Hz") + txt("  ·  ")
                                   + numero(p.db + 10.0 * std::log10((double) referencia_->fracao / juce::jmax(1, fracao_)), 1) + " " + escalaDe(i_).unidade();
        g.setFont(fonte(13.0f, true));
        const float x = xDe(p.f), y = yDoRta(p.db + desloc);
        auto caixa = juce::Rectangle<float>(x + 12.0f, y - 30.0f, juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), texto) + 16.0f, 22.0f);
        if (caixa.getRight() > area_.getRight()) caixa.setX(x - 12.0f - caixa.getWidth());
        if (caixa.getY() < area_.getY()) caixa.setY(y + 10.0f);
        g.setColour(cores::texto);
        g.fillRoundedRectangle(caixa, 4.0f);
        g.setColour(cores::cartao);
        g.drawText(texto, caixa, juce::Justification::centred);
    } else if (temCursor_ && area_.contains(cursor_)) {
        g.setFont(fonte(12.0f));
        g.setColour(cores::suave);
        g.drawText(txt("arraste um ponto para mover  ·  clique duplo cria ou apaga  ·  Shift + arraste sobe ou desce a linha toda"),
                   area_.withTrimmedLeft(8.0f).removeFromBottom(18.0f), juce::Justification::centredLeft);
    }
}

void Vista::pintarRta(juce::Graphics& g) {
    grade(g, kRtaMin, kRtaMax, 24.0f);
    const int n = AnalisadorDeEspectro::kNumBandas;
    if ((int) i_.bandas48.size() == n) {
        const int largura = AnalisadorDeEspectro::kPorOitava / juce::jmax(1, fracao_);
        const double meiaBanda = std::pow(2.0, 0.5 / juce::jmax(1, fracao_));
        g.setColour(cores::sinal);
        const float folga = fracao_ <= 12 ? 1.0f : 0.0f;       // barras largas ganham um respiro entre elas
        for (int c = 0; c < n; ++c) {
            if ((c + AnalisadorDeEspectro::kPrimeira) % largura != 0) continue;
            const double fc = AnalisadorDeEspectro::centro(c);
            const float x1 = xDe(fc / meiaBanda), x2 = xDe(fc * meiaBanda);
            const float y = yDoRta(nivelDaBanda(c, largura));
            if (x2 - x1 - folga < 0.5f || y >= area_.getBottom()) continue;
            g.fillRect(x1 + folga * 0.5f, y, x2 - x1 - folga, area_.getBottom() - y);
        }
        // o pico: um traço em cima de cada barra, na cor das linhas de apoio, para não brigar com o sinal
        if (pico_ != 0 && (int) picoDb_.size() == n) {
            g.setColour(cores::apoio);
            for (int c = 0; c < n; ++c) {
                if ((c + AnalisadorDeEspectro::kPrimeira) % largura != 0) continue;
                const double fc = AnalisadorDeEspectro::centro(c);
                const float x1 = xDe(fc / meiaBanda), x2 = xDe(fc * meiaBanda);
                const float y = yDoRta(picoDb_[(size_t) c]);
                if (x2 - x1 - folga < 0.5f || y >= area_.getBottom()) continue;
                g.fillRect(x1 + folga * 0.5f, y - 1.0f, x2 - x1 - folga, 2.0f);
            }
        }
    }
    marcarSuspeitas(g);
    pintarReferencia(g);

    // legenda: a largura de banda e, se houver, a referência
    g.setFont(fonte(12.0f));
    g.setColour(cores::suave);
    auto legenda = area_.withTrimmedRight(4.0f).removeFromTop(14.0f);
    const juce::String banda = fracao_ == 1 ? juce::String("1 oitava") : "1/" + juce::String(fracao_) + " de oitava";
    g.drawText(banda, legenda, juce::Justification::centredRight);
    legenda.removeFromRight(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), banda) + 16.0f);
    if (pico_ != 0) {
        const juce::String texto = pico_ > 0 ? "pico " + juce::String(pico_) + " s" : txt("pico");
        g.setColour(cores::suave);
        g.drawText(texto, legenda, juce::Justification::centredRight);
        const float x = legenda.getRight() - juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), texto) - 24.0f;
        g.setColour(cores::apoio);
        g.fillRect(x, legenda.getCentreY() - 1.0f, 18.0f, 2.0f);
        legenda.removeFromRight(juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), texto) + 16.0f + 24.0f);
    }
    if (referencia_ != nullptr && referencia_->visivel && !referencia_->pontos.empty()) {
        g.setColour(cores::suave);
        g.drawText(txt("referência"), legenda, juce::Justification::centredRight);
        const float x = legenda.getRight() - juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), txt("referência")) - 24.0f;
        g.setColour(cores::texto);
        g.fillRect(x, legenda.getCentreY() - 1.0f, 18.0f, 2.0f);
    }
}

void Vista::pintarEspectrograma(juce::Graphics& g) {
    grade(g, 0, 0, 0);
    if (imagem_.isValid()) {
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        // a coluna 0 é a banda de 20 Hz e a última fica em 19,9 kHz, as pontas do eixo
        g.drawImage(imagem_, area_, juce::RectanglePlacement::stretchToFit);
    }
    const Escala escala = escalaDe(i_);
    const double lps = juce::jmax(1.0, linhasPorSegundo_);
    const long long fim = aoVivo_ ? total_ : fim_;
    const auto haQuanto = [](double segundos) {
        const int s = juce::roundToInt(segundos);
        return juce::String(s / 60) + ":" + juce::String(s % 60).paddedLeft('0', 2);
    };
    // a hora de uma linha: relógio ao vivo, posição na gravação em arquivo
    const auto horaDe = [&](long long linha) {
        const double t = (double) linha / lps;
        return i_.deArquivo ? haQuanto(t) : (i_.inicioDaFonte + juce::RelativeTime::seconds(t)).formatted("%H:%M:%S");
    };

    // as suspeitas marcadas são as de agora; revendo o passado elas não fazem sentido
    if (aoVivo_) marcarSuspeitas(g);

    // tempo à esquerda: o topo é o mais novo
    const double topo = (double) (total_ - fim) / lps;
    g.setFont(fonte(12.0f));
    g.setColour(aoVivo_ ? cores::suave : cores::amarelo);
    const auto margem = [&](float y) { return juce::Rectangle<float>(area_.getX() - kMargemEsquerda - 8.0f, y, kMargemEsquerda + 5.0f, 14.0f); };
    g.drawText(aoVivo_ ? juce::String("agora") : txt("−") + haQuanto(topo), margem(area_.getY() - 2.0f), juce::Justification::centredRight);
    g.drawText(txt("−") + haQuanto(topo + kLinhasVisiveis / lps), margem(area_.getBottom() - 13.0f), juce::Justification::centredRight);

    // revendo: uma etiqueta que não deixa confundir com o agora
    if (!aoVivo_) {
        const juce::String texto = txt("REVENDO  ·  ") + horaDe(fim - 1) + txt("  ·  há ") + haQuanto(topo);
        g.setFont(fonte(13.0f, true));
        const auto etiqueta = juce::Rectangle<float>(area_.getX() + 6.0f, area_.getY() + 34.0f,
                                                     juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), texto) + 18.0f, 22.0f);
        g.setColour(cores::amarelo);
        g.fillRoundedRectangle(etiqueta, 4.0f);
        g.setColour(cores::tinta);
        g.drawText(texto, etiqueta, juce::Justification::centred);
    }

    // legenda do mapa de cor
    const auto barra = juce::Rectangle<float>(area_.getRight() - 160.0f, area_.getY() + 8.0f, 90.0f, 7.0f);
    g.setColour(cores::cartao.withAlpha(0.85f));
    g.fillRoundedRectangle(barra.expanded(38.0f, 5.0f).withRight(area_.getRight() - 2.0f), 4.0f);
    for (int x = 0; x < (int) barra.getWidth(); ++x) {
        g.setColour(corDoNivel((float) x / barra.getWidth()));
        g.fillRect(barra.getX() + (float) x, barra.getY(), 1.0f, barra.getHeight());
    }
    g.setColour(cores::texto);
    g.setFont(fonte(11.0f));
    const auto valor = [&](float db) {
        const int v = juce::roundToInt(escala.mostrar(db));
        return (v < 0 ? txt("−") : juce::String()) + juce::String(std::abs(v));
    };
    g.drawText(valor(kEspectrogramaMin), barra.translated(-34.0f, -3.0f).withWidth(30.0f).withHeight(13.0f), juce::Justification::centredRight);
    g.drawText(valor(kEspectrogramaMax) + " " + escala.unidade(), barra.translated(barra.getWidth() + 4.0f, -3.0f).withWidth(66.0f).withHeight(13.0f),
               juce::Justification::centredLeft);

    // debaixo do cursor: que frequência, quando e quão alto
    if (temCursor_ && area_.contains(cursor_) && !visiveis_.empty()) {
        const int colunas = AnalisadorDeEspectro::kNumBandas;
        const int c = juce::jlimit(0, colunas - 1, (int) ((cursor_.x - area_.getX()) / area_.getWidth() * (float) colunas));
        const int y = juce::jlimit(0, kLinhasVisiveis - 1, (int) ((cursor_.y - area_.getY()) / area_.getHeight() * (float) kLinhasVisiveis));
        const long long linha = fim - 1 - y;
        g.setColour(cores::texto.withAlpha(0.55f));
        g.drawVerticalLine(juce::roundToInt(cursor_.x), area_.getY(), area_.getBottom());
        g.drawHorizontalLine(juce::roundToInt(cursor_.y), area_.getX(), area_.getRight());
        if (linha >= primeira_) {
            const float db = (float) visiveis_[(size_t) y * (size_t) colunas + (size_t) c] * (120.0f / 255.0f) - 120.0f;
            const juce::String texto = juce::String(juce::roundToInt(AnalisadorDeEspectro::centro(c))) + txt(" Hz  ·  ") + horaDe(linha) + txt("  ·  ")
                                       + escala.texto(db, 0);
            g.setFont(fonte(13.0f, true));
            auto caixa = juce::Rectangle<float>(cursor_.x + 12.0f, cursor_.y + 12.0f, juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), texto) + 16.0f, 22.0f);
            if (caixa.getRight() > area_.getRight()) caixa.setX(cursor_.x - 12.0f - caixa.getWidth());
            if (caixa.getBottom() > area_.getBottom()) caixa.setY(cursor_.y - 12.0f - caixa.getHeight());
            g.setColour(cores::texto);
            g.fillRoundedRectangle(caixa, 4.0f);
            g.setColour(cores::cartao);
            g.drawText(texto, caixa, juce::Justification::centred);
        }
    }
}

void Vista::pintarTransferencia(juce::Graphics& g) {
    if (!i_.temLr) {
        grade(g, 0, 0, 0);
        aviso(g, txt("Escolha o L/R da mesa para ver a resposta do sistema no mic."));
        return;
    }
    if (!i_.temBloco || i_.bloco.quadros < 8) {
        grade(g, 0, 0, 0);
        aviso(g, i_.atrasoTravado ? txt("Medindo. A primeira leitura sai em alguns segundos de música.") : txt("Procurando o atraso entre a mesa e o mic…"));
        return;
    }

    // Duas medidas, dois gráficos empilhados no mesmo eixo de frequência: ganho em cima, coerência embaixo.
    grade(g, 0, 0, 0);
    const auto inteira = area_;
    const float alturaDaCoerencia = juce::jmax(26.0f, inteira.getHeight() * 0.22f);
    const auto baixo = inteira.withTop(inteira.getBottom() - alturaDaCoerencia);
    area_ = inteira.withBottom(baixo.getY() - 18.0f);

    constexpr float faixaDb = 18.0f;
    const auto& b = i_.bloco;
    const auto valida = [&](int k) { return b.coerencia[(size_t) k] >= 0.35f && b.nivelRefDb[(size_t) k] > -90.0f; };
    // o ganho absoluto depende de como a mesa e a placa estão ligadas; o que interessa é a forma, em volta da mediana
    std::vector<float> ganhos;
    for (int k = 0; k < papagaio::kNumBandas; ++k)
        if (valida(k)) ganhos.push_back(b.ganhoDb[(size_t) k]);
    if (ganhos.empty()) {
        area_ = inteira;
        aviso(g, txt("Coerência baixa em todas as bandas: o mic não está ouvindo o que sai do L/R."));
        return;
    }
    std::nth_element(ganhos.begin(), ganhos.begin() + (std::ptrdiff_t) ganhos.size() / 2, ganhos.end());
    const float mediana = ganhos[ganhos.size() / 2];

    {
        // a grade usa o eixo de nível de -faixa a +faixa
        g.setFont(fonte(12.0f));
        for (const float db : { -12.0f, 0.0f, 12.0f }) {
            const float y = area_.getCentreY() - area_.getHeight() * 0.5f * db / faixaDb;
            g.setColour(cores::borda.withAlpha(db == 0.0f ? 1.0f : 0.6f));
            g.drawHorizontalLine(juce::roundToInt(y), area_.getX(), area_.getRight());
            g.setColour(cores::suave);
            g.drawText(db == 0.0f ? juce::String("0") : (db < 0 ? txt("−") : juce::String("+")) + juce::String(juce::roundToInt(std::abs(db))),
                       juce::Rectangle<float>(area_.getX() - kMargemEsquerda, y - 7.0f, kMargemEsquerda - 5.0f, 14.0f), juce::Justification::centredRight);
        }
    }
    juce::Path linha;
    bool solto = true;
    g.setColour(cores::sala);
    for (int k = 0; k < papagaio::kNumBandas; ++k) {
        if (!valida(k)) {
            solto = true;
            continue;
        }
        const float x = xDe(papagaio::centroDaBanda(k));
        const float y = area_.getCentreY() - area_.getHeight() * 0.5f * juce::jlimit(-faixaDb, faixaDb, b.ganhoDb[(size_t) k] - mediana) / faixaDb;
        if (solto)
            linha.startNewSubPath(x, y);
        else
            linha.lineTo(x, y);
        solto = false;
        g.fillEllipse(x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
    g.strokePath(linha, juce::PathStrokeType(2.0f));
    g.setFont(fonte(12.0f));
    g.setColour(cores::suave);
    g.drawText(txt("ganho mic ÷ L/R em dB, em volta da mediana  ·  1/3 de oitava  ·  atualiza a cada medição"), area_.withTrimmedLeft(6.0f).removeFromTop(14.0f),
               juce::Justification::centredLeft);

    // coerência: quanto do que o mic ouve em cada banda vem do L/R
    g.setColour(cores::borda.withAlpha(0.5f));
    g.fillRect(baixo);
    const double meiaBanda = std::pow(2.0, 1.0 / 6.0);
    g.setColour(cores::suave);
    for (int k = 0; k < papagaio::kNumBandas; ++k) {
        const double fc = papagaio::centroDaBanda(k);
        if (fc * meiaBanda < kFreqMin) continue;
        const float x1 = xDe(fc / meiaBanda), x2 = xDe(fc * meiaBanda);
        const float h = baixo.getHeight() * juce::jlimit(0.0f, 1.0f, b.coerencia[(size_t) k]);
        g.fillRect(x1 + 1.0f, baixo.getBottom() - h, juce::jmax(1.0f, x2 - x1 - 2.0f), h);
    }
    g.setColour(cores::suave);
    g.drawText(txt("coerência: quanto do que o mic ouve vem do L/R"), juce::Rectangle<float>(baixo.getX() + 6.0f, baixo.getY() - 16.0f, baixo.getWidth(), 14.0f),
               juce::Justification::centredLeft);
    g.drawText("1", juce::Rectangle<float>(baixo.getX() - kMargemEsquerda, baixo.getY() - 4.0f, kMargemEsquerda - 5.0f, 14.0f), juce::Justification::centredRight);
    g.drawText("0", juce::Rectangle<float>(baixo.getX() - kMargemEsquerda, baixo.getBottom() - 10.0f, kMargemEsquerda - 5.0f, 14.0f), juce::Justification::centredRight);

    area_ = inteira;
    marcarSuspeitas(g);
}

// ------------------------------------------------------------------ equilíbrio pelo volume

namespace {

constexpr double kPhonMin = 20.0, kPhonMax = 110.0;
constexpr float kEquilibrioDb = 12.0f;      // o gráfico vai de -12 a +12 dB

// A banda de 1/3 de oitava do motor (25 Hz a 16 kHz) na grade de 1/48 de oitava do analisador.
int centroEm48(int banda) { return (banda - 16) * 16 - AnalisadorDeEspectro::kPrimeira; }

// Quanto um tom nessa frequência precisa estar acima (ou abaixo) do de 1 kHz para soar igual, nesse volume.
double sobraDaCurva(double phon, double f) { return papagaio::nivelDeIgualAudibilidade(phon, f) - phon; }

} // namespace

// Nível geral e bandas com uma média de uns 3 s: phon é coisa de volume do show, não de cada batida.
void Vista::suavizarEquilibrio() {
    if ((int) i_.bandas48.size() != AnalisadorDeEspectro::kNumBandas) return;
    double total = 0;
    for (const float db : i_.bandas48) total += std::pow(10.0, db / 10.0);
    const double alfa = temSuave_ ? 1.0 - std::exp(-(1.0 / 30.0) / 3.0) : 1.0;
    nivelSuave_ += alfa * (total - nivelSuave_);
    for (int b = 0; b < papagaio::kNumBandas; ++b) {
        const double p = std::pow(10.0, nivelDaBanda(centroEm48(b), 16) / 10.0);
        bandasSuaves_[(size_t) b] += alfa * (p - bandasSuaves_[(size_t) b]);
    }
    temSuave_ = true;
}

void Vista::marcarEquilibrio() {
    if (equilibrio_ == nullptr || !temSuave_ || !i_.calibrado) return;
    equilibrio_->tem = true;
    equilibrio_->nivelZ = 10.0 * std::log10(nivelSuave_ + 1e-20) + i_.calibracaoDb;
    for (size_t b = 0; b < papagaio::kNumBandas; ++b) equilibrio_->bandas[b] = 10.0 * std::log10(bandasSuaves_[b] + 1e-20) + i_.calibracaoDb;
    if (equilibrio_->aoMudar) equilibrio_->aoMudar();
    repaint();
}

// O ouvido não tem a mesma curva em todo volume: baixo, ele perde grave e agudo; alto, a curva achata.
// Então a mesma mixagem soa com mais grave quando o volume sobe. Esta vista mostra, por banda, quanto
// cada região soa mais ou menos presente do que soava no momento marcado como referência:
//   barras: só o efeito do volume, pelas curvas de phon da ISO 226;
//   linha:  o efeito do volume somado ao que o espectro mudou de fato desde a referência.
void Vista::pintarEquilibrio(juce::Graphics& g) {
    auto faixa = area_.removeFromTop(30.0f).withTrimmedBottom(8.0f).withTrimmedLeft(6.0f + 138.0f + 10.0f).withTrimmedRight(4.0f);
    grade(g, 0, 0, 0);
    botaoMarcar_.setEnabled(i_.calibrado && temSuave_);
    if (!i_.calibrado) {
        aviso(g, txt("Calibre o mic: a curva de phon depende do nível em dB SPL."));
        return;
    }
    if (!temSuave_) {
        aviso(g, txt("Sem sinal."));
        return;
    }
    const double agora = 10.0 * std::log10(nivelSuave_ + 1e-20) + i_.calibracaoDb;
    if (equilibrio_ == nullptr || !equilibrio_->tem) {
        aviso(g, txt("Quando o equilíbrio estiver do seu gosto (na passagem de som, por exemplo), clique em Marcar referência. Agora: ")
                     + numero(agora, 1) + " dB(Z).");
        return;
    }
    const auto& ref = *equilibrio_;
    const double phonAgora = juce::jlimit(kPhonMin, kPhonMax, agora), phonRef = juce::jlimit(kPhonMin, kPhonMax, ref.nivelZ);

    const auto yDe = [&](double db) {
        return area_.getCentreY() - area_.getHeight() * 0.5f * (float) (juce::jlimit((double) -kEquilibrioDb, (double) kEquilibrioDb, db) / kEquilibrioDb);
    };
    g.setFont(fonte(12.0f));
    for (const float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f }) {
        const float y = yDe(db);
        g.setColour(cores::borda.withAlpha(db == 0.0f ? 1.0f : 0.6f));
        g.drawHorizontalLine(juce::roundToInt(y), area_.getX(), area_.getRight());
        g.setColour(cores::suave);
        g.drawText(db == 0.0f ? juce::String("0") : (db < 0 ? txt("−") : juce::String("+")) + juce::String(juce::roundToInt(std::abs(db))),
                   juce::Rectangle<float>(area_.getX() - kMargemEsquerda, y - 7.0f, kMargemEsquerda - 5.0f, 14.0f), juce::Justification::centredRight);
    }
    g.setFont(fonte(11.0f));
    g.drawText("dB", juce::Rectangle<float>(area_.getX() - kMargemEsquerda - 8.0f, area_.getBottom() + 2.0f, kMargemEsquerda + 6.0f, 14.0f),
               juce::Justification::centredRight);

    const double meiaBanda = std::pow(2.0, 1.0 / 6.0);
    const float zero = yDe(0.0);
    juce::Path linha;
    bool solto = true;
    std::vector<juce::Point<float>> pontos;
    for (int b = 0; b < papagaio::kNumBandas; ++b) {
        const double f = papagaio::centroDaBanda(b);
        // só o volume: quanto a curva de phon mudou de forma entre os dois níveis
        const double peloVolume = sobraDaCurva(phonRef, f) - sobraDaCurva(phonAgora, f);
        const float x1 = xDe(f / meiaBanda), x2 = xDe(f * meiaBanda), y = yDe(peloVolume);
        g.setColour(cores::sala);
        g.fillRect(x1 + 1.0f, juce::jmin(y, zero), juce::jmax(1.0f, x2 - x1 - 2.0f), juce::jmax(1.0f, std::abs(y - zero)));

        // mais o que o espectro mudou: a parte de cada banda no nível geral, agora contra a referência
        const double bandaAgora = 10.0 * std::log10(bandasSuaves_[(size_t) b] + 1e-20) + i_.calibracaoDb;
        if (bandaAgora < agora - 60.0 || ref.bandas[(size_t) b] < ref.nivelZ - 60.0) {      // banda sem programa não diz nada
            solto = true;
            continue;
        }
        const double total = peloVolume + (bandaAgora - agora) - (ref.bandas[(size_t) b] - ref.nivelZ);
        const juce::Point<float> p(xDe(f), yDe(total));
        if (solto)
            linha.startNewSubPath(p);
        else
            linha.lineTo(p);
        solto = false;
        pontos.push_back(p);
    }
    g.setColour(cores::cartao.withAlpha(0.85f));
    g.strokePath(linha, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(cores::mix);
    g.strokePath(linha, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (const auto& p : pontos) g.fillEllipse(p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);

    // legenda e leitura
    g.setFont(fonte(12.0f));
    auto& topo = faixa;
    const auto item = [&](juce::Colour cor, const juce::String& nome, bool barra) {
        g.setColour(cores::suave);
        const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), nome);
        g.drawText(nome, topo.removeFromRight(w + 2.0f), juce::Justification::centredRight);
        auto marca = topo.removeFromRight(22.0f);
        g.setColour(cor);
        if (barra)
            g.fillRect(marca.withSizeKeepingCentre(10.0f, 10.0f));
        else
            g.fillRect(marca.withSizeKeepingCentre(16.0f, 2.0f));
        topo.removeFromRight(12.0f);
    };
    item(cores::mix, txt("volume + espectro"), false);
    item(cores::sala, txt("só o volume"), true);

    // o que sobrou da faixa, entre o botão e a legenda, é da leitura
    g.setColour(cores::suave);
    const double diferenca = agora - ref.nivelZ;
    g.drawText(txt("ref. ") + numero(ref.nivelZ, 1) + txt("  ·  agora ") + numero(agora, 1) + " dB(Z)  (" + (diferenca < 0 ? txt("−") : juce::String("+"))
                   + numero(std::abs(diferenca), 1) + ")" + (agora > 90.0 || ref.nivelZ > 90.0 ? txt("  ·  acima de 90 phon: estimativa") : juce::String()),
               topo, juce::Justification::centredLeft);
}
