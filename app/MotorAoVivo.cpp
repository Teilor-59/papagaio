#include "MotorAoVivo.h"

#include <cmath>
#include <filesystem>

MotorAoVivo::MotorAoVivo() : juce::Thread("Papagaio: analise") {
    for (auto* v : { &filaMic_, &filaLinhas_, &filaLr_ }) v->resize((size_t) fila_.getTotalSize());
    for (auto* v : { &blocoMic_, &blocoLinhas_, &blocoLr_, &silencio_ }) v->resize(8192);
    dispositivos_.addAudioCallback(this);
    startThread();
}

MotorAoVivo::~MotorAoVivo() {
    dispositivos_.removeAudioCallback(this);
    stopThread(2000);
    dispositivos_.closeAudioDevice();
}

juce::String MotorAoVivo::iniciar(const juce::XmlElement* estadoSalvo) {
    // Pede todas as entradas da placa; o usuário escolhe quais usar. O Papagaio não toca nada.
    return dispositivos_.initialise(256, 0, estadoSalvo, true);
}

bool MotorAoVivo::abrirArquivo(const juce::File& arquivo, juce::String& erro, const Simulacao& simulacao) {
    auto leitor = std::make_unique<papagaio::LeitorWav>();
    std::string motivo;
    if (!leitor->abrir(std::filesystem::u8path(arquivo.getFullPathName().toStdString()), motivo)) {
        erro = juce::String::fromUTF8(motivo.c_str());
        return false;
    }
    const juce::ScopedLock l(trava_);
    canaisDoArquivo_ = leitor->canais();
    nomeDoArquivo_ = arquivo.getFileName();
    leitorPendente_ = std::move(leitor);
    simulacaoPendente_ = simulacao;
    escolherCanais(0, -1, -1, -1);
    emArquivo_ = true;
    return true;
}

void MotorAoVivo::voltarAoVivo() {
    escolherCanais(0, -1, -1, -1);
    emArquivo_ = false;
}

juce::StringArray MotorAoVivo::nomesDosCanais() const {
    juce::StringArray nomes;
    if (emArquivo_.load()) {
        const juce::ScopedLock l(trava_);
        for (int i = 0; i < canaisDoArquivo_; ++i) nomes.add("Canal " + juce::String(i + 1));
    } else if (auto* d = dispositivos_.getCurrentAudioDevice()) {
        const auto todos = d->getInputChannelNames();
        const auto ativos = d->getActiveInputChannels();
        for (int i = 0; i < todos.size(); ++i)
            if (ativos[i]) nomes.add(todos[i]);
    }
    return nomes;
}

juce::String MotorAoVivo::descricaoDaFonte() const {
    if (emArquivo_.load()) {
        const juce::ScopedLock l(trava_);
        return juce::String::fromUTF8("Simulação: ") + nomeDoArquivo_;
    }
    if (auto* d = dispositivos_.getCurrentAudioDevice())
        if (d->getActiveInputChannels().countNumberOfSetBits() > 0)
            return d->getName() + juce::String::fromUTF8(" · ") + juce::String(d->getCurrentSampleRate() / 1000.0, 1).replaceCharacter('.', ',') + " kHz";
    return juce::String::fromUTF8("Sem entrada de áudio");
}

void MotorAoVivo::escolherCanais(int mic, int linhas, int lrA, int lrB) {
    canalMic_ = juce::jmax(0, mic);
    canalLinhas_ = linhas;
    canalLrA_ = lrA;
    canalLrB_ = lrB;
}

Instantaneo MotorAoVivo::instantaneo() const {
    const juce::ScopedLock l(trava_);
    return instantaneo_;
}

std::vector<Registro> MotorAoVivo::historico() const {
    const juce::ScopedLock l(trava_);
    return historico_;
}

MotorAoVivo::Espectrograma MotorAoVivo::espectrograma() const {
    const juce::ScopedLock l(trava_);
    return { geracao_, primeiroPedaco_ * kLinhasPorPedaco, linhasEscritas_ };
}

int MotorAoVivo::lerEspectrograma(long long& primeira, int quantas, std::vector<juce::uint8>& linhas) const {
    constexpr size_t colunas = papagaio::AnalisadorDeEspectro::kNumBandas;
    const juce::ScopedLock l(trava_);
    primeira = juce::jmax(primeira, primeiroPedaco_ * kLinhasPorPedaco);
    const int n = (int) juce::jlimit<long long>(0, quantas, linhasEscritas_ - primeira);
    linhas.resize((size_t) n * colunas);
    for (int i = 0; i < n; ++i) {
        const long long linha = primeira + i;
        const auto& pedaco = pedacos_[(size_t) (linha / kLinhasPorPedaco - primeiroPedaco_)];
        std::copy_n(pedaco.begin() + (std::ptrdiff_t) ((size_t) (linha % kLinhasPorPedaco) * colunas), colunas,
                    linhas.begin() + (std::ptrdiff_t) ((size_t) i * colunas));
    }
    return n;
}

void MotorAoVivo::limparHistorico() {
    const juce::ScopedLock l(trava_);
    historico_.clear();
    ++revisao_;
}

void MotorAoVivo::audioDeviceAboutToStart(juce::AudioIODevice* dispositivo) {
    taxaDaPlaca_ = dispositivo->getCurrentSampleRate();
    reiniciar_ = true;
}

void MotorAoVivo::audioDeviceStopped() {}

void MotorAoVivo::audioDeviceIOCallbackWithContext(const float* const* entradas, int numEntradas, float* const* saidas, int numSaidas,
                                                   int numAmostras, const juce::AudioIODeviceCallbackContext&) {
    for (int c = 0; c < numSaidas; ++c)
        if (saidas[c] != nullptr) juce::FloatVectorOperations::clear(saidas[c], numAmostras);
    if (numEntradas <= 0 || emArquivo_.load()) return;

    const auto canal = [&](int c) -> const float* { return c >= 0 && c < numEntradas ? entradas[c] : nullptr; };
    const float* mic = entradas[juce::jlimit(0, numEntradas - 1, canalMic_.load())];
    const float* linhas = canal(canalLinhas_.load());
    const float* lrA = canal(canalLrA_.load());
    const float* lrB = canal(canalLrB_.load());
    if (lrA == nullptr) std::swap(lrA, lrB);

    // Se a análise atrasar e a fila encher, o que não coube se perde; a placa nunca espera.
    const auto e = fila_.write(numAmostras);
    const auto copiar = [&](int destino, int origem, int n) {
        if (n <= 0) return;
        juce::FloatVectorOperations::copy(filaMic_.data() + destino, mic + origem, n);
        if (linhas != nullptr)
            juce::FloatVectorOperations::copy(filaLinhas_.data() + destino, linhas + origem, n);
        else
            juce::FloatVectorOperations::clear(filaLinhas_.data() + destino, n);
        if (lrA == nullptr) {
            juce::FloatVectorOperations::clear(filaLr_.data() + destino, n);
        } else if (lrB == nullptr) {
            juce::FloatVectorOperations::copy(filaLr_.data() + destino, lrA + origem, n);
        } else {
            juce::FloatVectorOperations::copyWithMultiply(filaLr_.data() + destino, lrA + origem, 0.5f, n);
            juce::FloatVectorOperations::addWithMultiply(filaLr_.data() + destino, lrB + origem, 0.5f, n);
        }
    };
    copiar(e.startIndex1, 0, e.blockSize1);
    copiar(e.startIndex2, e.blockSize1, e.blockSize2);
}

void MotorAoVivo::definirMapa(const papagaio::MapaDeRisco& mapa) {
    const juce::ScopedLock l(trava_);
    mapaPendente_ = mapa;
    temMapaPendente_ = true;
}

void MotorAoVivo::preparar(double taxa) {
    auto cfg = detector_.config();
    cfg.usarLinhas = canalLinhas_.load() >= 0;
    detector_.preparar(taxa, cfg);
    medidor_.preparar(taxa);
    espectro_.preparar(taxa);
    linhasPorSegundo_ = espectro_.quadrosPorSegundo();
    temBloco_ = false;
    taxa_ = taxa;
    eventosLidos_ = 0;
    lrAtivo_ = false;       // o drift recomeça quando o L/R aparecer
    inicioDaFonte_ = juce::Time::getCurrentTime();
    pico_ = 0.0f;
    const juce::ScopedLock l(trava_);
    instantaneo_ = Instantaneo {};
    pedacos_.clear();
    primeiroPedaco_ = 0;
    linhasEscritas_ = 0;
    ++geracao_;
}

void MotorAoVivo::run() {
    bool preparado = false;
    while (!threadShouldExit()) {
        bool trocou = false;
        {
            const juce::ScopedLock l(trava_);
            if (leitorPendente_ != nullptr) {
                leitor_ = std::move(leitorPendente_);
                simulacao_ = simulacaoPendente_;
                trocou = true;
            }
        }
        if (trocou) {
            lidosDoArquivo_ = 0;
            acabou_ = false;
            relogioDoArquivo_ = juce::Time::getMillisecondCounterHiRes();
            configDrift_ = papagaio::ConfigDrift {};
            duracaoDoBloco_ = 10.0;
            if (simulacao_.janelasCurtas) {
                duracaoDoBloco_ = 5.0;
                configDrift_.janelaSala = 30.0;
                configDrift_.janelaMix = 40.0;
                configDrift_.duracaoDaReferencia = 40.0;
                configDrift_.sustentacao = 20.0;
            }
            preparar(leitor_->taxa());
            preparado = true;
        }
        if (leitor_ != nullptr && !emArquivo_.load()) {
            leitor_.reset();
            configDrift_ = papagaio::ConfigDrift {};
            duracaoDoBloco_ = 10.0;
            preparado = false;
            reiniciar_ = true;
        }
        if (leitor_ == nullptr && reiniciar_.exchange(false) && taxaDaPlaca_.load() > 0) {
            preparar(taxaDaPlaca_.load());
            preparado = true;
        }
        if (preparado && (canalLinhas_.load() >= 0) != detector_.config().usarLinhas) {
            auto cfg = detector_.config();
            cfg.usarLinhas = canalLinhas_.load() >= 0;
            detector_.configurar(cfg);
        }

        const Pedido pedido = pedido_.exchange(Pedido::nenhum);
        if (pedido != Pedido::nenhum && lrAtivo_) {
            if (pedido == Pedido::iniciarPassagem) monitor_.iniciarPassagem();
            if (pedido == Pedido::fecharPassagem) monitor_.fecharPassagem();
            if (pedido == Pedido::inicioDoShow) monitor_.marcarInicioDoShow();
            publicar(0);
        }

        // o mapa e o ring-out são do detector, que só esta thread toca
        if (temMapaPendente_.exchange(false)) {
            papagaio::MapaDeRisco mapa;
            {
                const juce::ScopedLock l(trava_);
                mapa = mapaPendente_;
            }
            detector_.definirMapa(mapa);
            publicar(0);
        }
        if (const int ringOut = pedidoDeRingOut_.exchange(0); ringOut != 0) {
            if (ringOut == 1) detector_.iniciarRingOut();
            if (ringOut == 2) detector_.encerrarRingOut();
            publicar(0);
        }

        int feitos = 0;
        if (leitor_ != nullptr)
            feitos = analisarArquivo();
        else if (preparado)
            feitos = analisarFila();
        else
            fila_.finishedRead(fila_.getNumReady());     // sem detector ainda: joga fora

        wait(feitos > 0 ? 3 : 10);
    }
}

int MotorAoVivo::analisarFila() {
    int total = 0;
    for (;;) {
        const int n = juce::jmin(fila_.getNumReady(), (int) blocoMic_.size());
        if (n <= 0) break;
        {
            const auto r = fila_.read(n);
            const auto copiar = [&](std::vector<float>& bloco, const std::vector<float>& fila) {
                juce::FloatVectorOperations::copy(bloco.data(), fila.data() + r.startIndex1, r.blockSize1);
                if (r.blockSize2 > 0) juce::FloatVectorOperations::copy(bloco.data() + r.blockSize1, fila.data() + r.startIndex2, r.blockSize2);
            };
            copiar(blocoMic_, filaMic_);
            copiar(blocoLinhas_, filaLinhas_);
            copiar(blocoLr_, filaLr_);
        }
        analisar(n);
        total += n;
    }
    return total;
}

int MotorAoVivo::analisarArquivo() {
    if (acabou_) return 0;
    const double taxa = leitor_->taxa();
    const double decorrido = (juce::Time::getMillisecondCounterHiRes() - relogioDoArquivo_) / 1000.0;
    long long alvo = (long long) (decorrido * taxa * simulacao_.velocidade);
    if (simulacao_.pausarEm > 0) alvo = juce::jmin(alvo, (long long) (simulacao_.pausarEm * taxa));

    const int canais = leitor_->canais();
    int total = 0;
    while (lidosDoArquivo_ < alvo && total < (1 << 18) && !threadShouldExit()) {
        const int n = leitor_->ler(intercalado_, (int) juce::jmin<long long>(alvo - lidosDoArquivo_, (long long) blocoMic_.size()));
        if (n <= 0) {
            detector_.encerrar();
            acabou_ = true;
            publicar(0);
            break;
        }
        const auto canal = [canais](int c) { return c >= 0 && c < canais ? c : -1; };
        const int cm = juce::jlimit(0, canais - 1, canalMic_.load());
        const int cl = canal(canalLinhas_.load());
        int ca = canal(canalLrA_.load()), cb = canal(canalLrB_.load());
        if (ca < 0) std::swap(ca, cb);
        for (int i = 0; i < n; ++i) {
            const float* quadro = intercalado_.data() + (size_t) i * (size_t) canais;
            blocoMic_[(size_t) i] = quadro[cm];
            blocoLinhas_[(size_t) i] = cl >= 0 ? quadro[cl] : 0.0f;
            blocoLr_[(size_t) i] = ca < 0 ? 0.0f : cb < 0 ? quadro[ca] : 0.5f * (quadro[ca] + quadro[cb]);
        }
        analisar(n);
        lidosDoArquivo_ += n;
        total += n;
    }
    return total;
}

// Roda os módulos sobre o que está em blocoMic_, blocoLinhas_ e blocoLr_.
void MotorAoVivo::analisar(int numAmostras) {
    // em pausa o detector ouve silêncio: o relógio dele segue, mas nada vira alerta
    detector_.processar(deteccaoEmPausa_.load() ? silencio_.data() : blocoMic_.data(),
                        detector_.config().usarLinhas ? blocoLinhas_.data() : nullptr, numAmostras);

    if (zerarNiveis_.exchange(false)) medidor_.zerar();
    medidor_.definirLento(nivelLento_.load());
    medidor_.processar(blocoMic_.data(), numAmostras);
    espectro_.definirMedia(mediaDoEspectro_.load());
    espectro_.processar(blocoMic_.data(), numAmostras);

    if (temLr() != lrAtivo_) {
        lrAtivo_ = temLr();
        if (lrAtivo_) {
            // o L/R acabou de aparecer: o drift começa a contar daqui
            analisador_.preparar(taxa_, duracaoDoBloco_);
            monitor_.preparar(configDrift_);
            driftLidos_ = 0;
            showMarcado_ = false;
            temBloco_ = false;
            inicioDoLr_ = detector_.tempo() - numAmostras / taxa_;
            if (leitor_ != nullptr && simulacao_.passagemAte > 0) monitor_.iniciarPassagem();
        }
    }
    if (lrAtivo_) {
        analisador_.processar(blocoLr_.data(), blocoMic_.data(), numAmostras);
        for (const auto& b : analisador_.retirarBlocos()) {
            if (leitor_ != nullptr && simulacao_.passagemAte > 0 && inicioDoLr_ + b.tempo > simulacao_.passagemAte) {
                monitor_.fecharPassagem();
                if (!showMarcado_) monitor_.marcarInicioDoShow();
                showMarcado_ = true;
            }
            monitor_.receber(b);
            ultimoBloco_ = b;
            temBloco_ = true;
        }
    }
    publicar(numAmostras);
}

void MotorAoVivo::publicar(int numAmostras) {
    if (numAmostras > 0) {
        const auto faixa = juce::FloatVectorOperations::findMinAndMax(blocoMic_.data(), numAmostras);
        const float pico = juce::jmax(std::abs(faixa.getStart()), std::abs(faixa.getEnd()));
        // o medidor cai 20 dB a cada 0,7 s
        pico_ = juce::jmax(pico, pico_ * (float) std::exp(-numAmostras / (0.3 * taxa_)));
    }

    const auto linhas = espectro_.retirarLinhas();
    const auto& eventos = detector_.eventos();
    const juce::ScopedLock l(trava_);
    constexpr size_t colunas = papagaio::AnalisadorDeEspectro::kNumBandas;
    for (size_t i = 0; i + colunas <= linhas.size(); i += colunas) {
        if (linhasEscritas_ / kLinhasPorPedaco - primeiroPedaco_ >= (long long) pedacos_.size()) {
            pedacos_.emplace_back((size_t) kLinhasPorPedaco * colunas, (juce::uint8) 0);
            if ((int) pedacos_.size() > kMaxPedacos) {
                pedacos_.pop_front();      // passou de três horas: o começo do show sai
                ++primeiroPedaco_;
            }
        }
        auto* destino = pedacos_.back().data() + (size_t) (linhasEscritas_ % kLinhasPorPedaco) * colunas;
        for (size_t c = 0; c < colunas; ++c)
            destino[c] = (juce::uint8) juce::jlimit(0.0f, 255.0f, (linhas[i + c] + 120.0f) * (255.0f / 120.0f));
        ++linhasEscritas_;
    }
    for (; eventosLidos_ < eventos.size(); ++eventosLidos_) {
        Registro r;
        r.evento = eventos[eventosLidos_];
        r.tempo = r.evento.tInicio;
        r.hora = inicioDaFonte_ + juce::RelativeTime::seconds(r.tempo);
        r.deArquivo = leitor_ != nullptr;
        historico_.push_back(r);
        ++revisao_;
    }
    if (lrAtivo_) {
        const auto& drift = monitor_.eventos();
        for (; driftLidos_ < drift.size(); ++driftLidos_) {
            Registro r;
            r.deDrift = true;
            r.drift = drift[driftLidos_];
            r.tempo = inicioDoLr_ + r.drift.tempo;
            r.hora = inicioDaFonte_ + juce::RelativeTime::seconds(r.tempo);
            r.deArquivo = leitor_ != nullptr;
            historico_.push_back(r);
            ++revisao_;
        }
    }
    instantaneo_.temSinal = detector_.tempo() > 0;
    instantaneo_.inicioDaFonte = inicioDaFonte_;
    instantaneo_.deArquivo = leitor_ != nullptr;
    instantaneo_.fimDoArquivo = acabou_;
    instantaneo_.tempo = detector_.tempo();
    instantaneo_.hzPorBin = detector_.hzPorBin();
    instantaneo_.picoMicDb = juce::Decibels::gainToDecibels(pico_, -120.0f);
    instantaneo_.usandoLinhas = detector_.config().usarLinhas;
    instantaneo_.pior = detector_.pior();
    if (instantaneo_.revisaoDoMapa != detector_.revisaoDoMapa()) {
        instantaneo_.mapa = detector_.mapa();
        instantaneo_.revisaoDoMapa = detector_.revisaoDoMapa();
    }
    instantaneo_.gravandoRingOut = detector_.gravandoRingOut();
    instantaneo_.inicioDoRingOut = detector_.inicioDoRingOut();
    instantaneo_.suspeitas = detector_.suspeitas();
    instantaneo_.espectroMic = detector_.espectroMicDb();
    if (instantaneo_.usandoLinhas) instantaneo_.espectroLinhas = detector_.espectroLinhasDb();

    instantaneo_.niveis = medidor_.leituras();
    instantaneo_.segundosDeNivel = medidor_.segundosMedidos();
    instantaneo_.nivelLento = medidor_.lento();
    instantaneo_.bandas48 = espectro_.bandasDb();
    instantaneo_.temBloco = lrAtivo_ && temBloco_;
    if (instantaneo_.temBloco) instantaneo_.bloco = ultimoBloco_;

    instantaneo_.temLr = lrAtivo_;
    instantaneo_.limiarDeDriftDb = configDrift_.limiarDb;
    if (lrAtivo_) {
        instantaneo_.atrasoTravado = analisador_.atrasoTravado();
        instantaneo_.drift = monitor_.estado();
    } else {
        instantaneo_.atrasoTravado = false;
        instantaneo_.drift = papagaio::EstadoDoDrift {};
    }
}
