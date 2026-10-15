#include "papagaio/DetectorMicrofonia.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace papagaio {

namespace {

// Segundos de suspeita acumulada para subir cada degrau.
constexpr double kTempoAmarelo = 0.15;
constexpr double kTempoLaranja = 0.35;
constexpr double kTempoVermelho = 0.20;

constexpr double kPnprFrouxo = 6.0;           // PNPR mínimo para um pico manter uma trilha viva
constexpr double kMargemFundamental = 12.0;   // pico em f/2 ou f/3 até 12 dB abaixo torna o candidato um harmônico
constexpr double kQuedaMaxima = 20.0;         // alerta aberto segue enquanto o pico não cair isso do máximo
constexpr double kFolgaDoMaximo = 1.0;        // dB: apito crescendo está a isso do próprio máximo
constexpr double kResiduoMaximo = 2.0;        // dB de desvio da reta para valer como crescimento de apito
constexpr double kTempoDominante = 0.4;       // segundos dominando a banda para vermelho sem precisar crescer
constexpr double kFolgaDominante = 4.0;       // dominar = o pico sozinho a até 4 dB da soma da banda inteira
constexpr double kPhprSaturado = 12.0;        // PHPR exigido de um pico que já domina a banda
constexpr double kMargemLinha = 10.0;         // quanto o mic pode destacar uma nota além do que a linha destaca
constexpr double kQuedaDaRetencao = 30.0;     // dB/s; cobre o atraso do PA até o mic e a cauda da sala
constexpr double kMinusculo = 1e-20;
constexpr double kMaiorHarmonicoDoApito = 8;   // até que múltiplo um pico conta como distorção de um apito tocando
constexpr double kJuntarAmarelos = 10.0;

// Prioridade nas frequências do mapa de risco: ali a malha já fechou no ring-out, então o detector
// aceita um pico menos destacado, sobe para amarelo e laranja mais cedo e chama de pré-apito um anel
// menos longo. O vermelho não afrouxa: nota pura em crescendo numa frequência do mapa viraria falso alarme.
constexpr double kToleranciaDeRisco = 0.02;   // ±2 % da frequência do mapa (a malha anda um pouco entre passagem e show)
constexpr double kRiscoPnpr = 5.0;            // dB a menos de PNPR exigido
constexpr double kRiscoPhpr = 7.0;            // dB a menos de PHPR exigido
constexpr double kRiscoTempo = 0.66;          // fração do tempo de suspeita para amarelo e laranja
constexpr double kRiscoAnel = 1.5;            // anel até 1,5 vez mais rápido que o limite já vale
constexpr double kRiscoAlongou = 1.3;         // e basta alongar 1,3 vez      // amarelos da mesma frequência com menos que isso entre eles viram um só

// Pré-apito: o anel de uma frequência, medido a cada queda depois de um ataque.
constexpr double kPnprAnel = 10.0;            // pico estreito o bastante para valer como anel
constexpr double kPhprAnel = 15.0;            // e sem harmônicos fortes
constexpr double kTopoDaQueda = 1.0;          // o primeiro dB depois do pico é borrão da janela: fica fora da reta
constexpr double kReexcitou = 3.0;            // subiu isso acima do mínimo: outro ataque, a queda acabou
constexpr double kQuedaLonga = 1.5;           // segundos; anel mais longo que isso já é apito, não pré-apito
constexpr double kQuedaFunda = 30.0;          // dB abaixo do pico: a reta já tem o bastante
constexpr double kExtensaoMinima = 6.0;       // dB de queda para valer como medida
constexpr double kDesvioDaQueda = 1.5;        // dB de desvio da reta: queda que não é exponencial não é anel
constexpr double kJoelho = 6.0;               // sem reta na queda inteira, tenta a cauda, abaixo de 6 dB do pico
constexpr double kAndaNaQueda = 0.25;         // bins que o pico pode andar durante a queda do anel
constexpr double kAlongou = 1.6;              // anel 1,6 vez mais lento que nos ataques anteriores
constexpr double kJanelaDeMedidas = 60.0;     // segundos: medidas mais velhas não entram na comparação
constexpr double kSemMedida = 20.0;           // alerta de pré-apito fecha depois disso sem queda medida
constexpr double kEsquecerAnel = 30.0;        // frequência que não mostra pico estreito por isso sai da vigia

double mediana3(double a, double b, double c) { return std::max(std::min(a, b), std::min(std::max(a, b), c)); }

// Quanto um pico se destaca do resto da banda, em dB: nível do pico menos a soma de todo o resto.
double destaque(double nivelDb, double totalDb) {
    const double resto = std::pow(10.0, totalDb / 10.0) - std::pow(10.0, nivelDb / 10.0);
    return nivelDb - 10.0 * std::log10(std::max(resto, std::pow(10.0, (nivelDb - 30.0) / 10.0)));
}

// Média dos bins a ±4..7 do pico. A janela Hann ocupa ±2, então isso já é o fundo.
float vizinhanca(const std::vector<float>& mag, int k) {
    return (mag[k - 7] + mag[k - 6] + mag[k - 5] + mag[k - 4] + mag[k + 4] + mag[k + 5] + mag[k + 6] + mag[k + 7]) * 0.125f;
}

float maxPerto(const std::vector<float>& mag, double kf) {
    const int k = std::clamp((int) std::lround(kf), 1, (int) mag.size() - 2);
    return std::max(mag[k], std::max(mag[k - 1], mag[k + 1]));
}

} // namespace

const char* nomeEstado(Estado e) {
    switch (e) {
        case Estado::vermelho: return "vermelho";
        case Estado::laranja: return "laranja";
        case Estado::amarelo: return "amarelo";
        default: return "nenhum";
    }
}

std::string bandaGeq(double f) {
    static const double centros[31] = { 20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                        1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000 };
    const int i = std::clamp((int) std::lround(10.0 * std::log10(std::max(f, 1.0) / 1000.0)), -17, 13);
    const double c = centros[i + 17];
    char texto[16];
    if (c < 1000.0)
        std::snprintf(texto, sizeof texto, "%g", c);
    else
        std::snprintf(texto, sizeof texto, "%gk", c / 1000.0);
    return texto;
}

void DetectorMicrofonia::preparar(double taxaDeAmostragem, const ConfigMicrofonia& config) {
    taxa_ = taxaDeAmostragem;
    cfg_ = config;
    const int mult = taxa_ > 60000.0 ? 2 : 1;
    n_ = (config.tamanhoFft >= 8192 ? 8192 : 4096) * mult;
    salto_ = 1024 * mult;
    tempoDoSalto_ = salto_ / taxa_;
    hzPorBin_ = taxa_ / n_;
    normDb_ = 20.0 * std::log10(n_ / 4.0);     // seno em fundo de escala com janela Hann lê 0 dBFS

    fft_ = std::make_unique<Fft>(n_);
    anelMic_.assign(n_, 0.0f);
    anelLinhas_.assign(n_, 0.0f);
    janela_.resize(n_);
    for (int i = 0; i < n_; ++i)
        janela_[i] = (float) (0.5 - 0.5 * std::cos(6.283185307179586 * i / n_));
    re_.resize(n_);
    im_.resize(n_);
    magMic_.assign(n_ / 2 + 1, -200.0f);
    magLinhas_.assign(n_ / 2 + 1, -200.0f);
    linhasRetidas_.assign(n_ / 2 + 1, -200.0f);
    picos_.clear();
    picos_.reserve(256);
    for (auto& t : trilhas_) t = Trilha {};
    for (auto& a : aneis_) a = Anel {};
    suspeitas_.clear();
    eventos_.clear();
    amarelosPendentes_.clear();
    gravandoRingOut_ = false;
    inicioDoRingOut_ = fimDoRingOut_ = -1;
    marcarRisco();
    pos_ = contador_ = 0;
    amostras_ = 0;
    agora_ = 0;

    janelaReta_ = std::max(4, (int) std::lround(0.15 / tempoDoSalto_));
    janelaParado_ = std::min(kHistorico, std::max(4, (int) std::lround(0.35 / tempoDoSalto_)));
    borrao_ = n_ / salto_ + 1;     // quadros que ainda enxergam o ataque do som
    maxRuim_ = std::max(2, (int) std::lround(0.12 / tempoDoSalto_));
    atualizarLimites();
}

void DetectorMicrofonia::configurar(const ConfigMicrofonia& config) {
    if ((config.tamanhoFft >= 8192) != (cfg_.tamanhoFft >= 8192) || n_ == 0) {
        preparar(taxa_, config);
        return;
    }
    cfg_ = config;
    atualizarLimites();
}

void DetectorMicrofonia::atualizarLimites() {
    kMin_ = std::max(8, (int) std::ceil(cfg_.fMin / hzPorBin_));
    kMax_ = std::min(n_ / 2 - 9, (int) std::floor(cfg_.fMax / hzPorBin_));
}

void DetectorMicrofonia::processar(const float* mic, const float* linhas, int numAmostras) {
    for (int i = 0; i < numAmostras; ++i) {
        anelMic_[pos_] = mic[i];
        anelLinhas_[pos_] = linhas != nullptr ? linhas[i] : 0.0f;
        if (++pos_ >= n_) pos_ = 0;
        ++amostras_;
        if (++contador_ >= salto_) {
            contador_ = 0;
            agora_ = amostras_ / taxa_;
            analisar();
        }
    }
}

void DetectorMicrofonia::encerrar() {
    for (auto& t : trilhas_)
        if (t.ativa) fecharTrilha(t);
    for (auto& a : aneis_)
        if (a.alertando) fecharAnel(a);
    soltarAmarelos(true);
    suspeitas_.clear();
}

// O anel de uma frequência perto do limite acende amarelo a cada fim de frase. Um registro por
// repetição encheria o histórico; o amarelo comum espera kJuntarAmarelos e vira um só, com a contagem.
void DetectorMicrofonia::soltarAmarelos(bool todos) {
    for (size_t i = 0; i < amarelosPendentes_.size();) {
        const Evento& e = amarelosPendentes_[i];
        if (todos || agora_ - (e.tInicio + e.duracao) > kJuntarAmarelos) {
            eventos_.push_back(e);
            amarelosPendentes_.erase(amarelosPendentes_.begin() + (long) i);
        } else {
            ++i;
        }
    }
}

Suspeita DetectorMicrofonia::pior() const {
    Suspeita p;
    // entre amarelos, o pré-apito diz mais que uma frequência instável
    const auto peso = [](const Suspeita& s) { return 2 * (int) s.estado + (s.preApito ? 1 : 0); };
    for (const auto& s : suspeitas_)
        if (peso(s) > peso(p) || (peso(s) == peso(p) && s.nivelDb > p.nivelDb)) p = s;
    return p;
}

// Devolve a potência somada na banda de análise e preenche magDb.
double DetectorMicrofonia::espectro(const std::vector<float>& anel, std::vector<float>& magDb) {
    int j = pos_;
    for (int i = 0; i < n_; ++i) {
        re_[i] = anel[j] * janela_[i];
        im_[i] = 0.0f;
        if (++j >= n_) j = 0;
    }
    fft_->transformar(re_.data(), im_.data());
    double soma = 0;
    for (int k = 0; k <= n_ / 2; ++k) {
        const double p = (double) re_[k] * re_[k] + (double) im_[k] * im_[k];
        if (k >= kMin_ && k <= kMax_) soma += p;
        magDb[k] = (float) (10.0 * std::log10(p + kMinusculo) - normDb_);
    }
    return soma;
}

// O candidato em kf é harmônico de um pico estreito em kf/divisor?
bool DetectorMicrofonia::temFundamental(double kf, double nivel, int divisor) const {
    const double kb = kf / divisor;
    if (kb < 4.0) return false;
    const int k = (int) std::lround(kb);
    const float m = maxPerto(magMic_, kb);
    return m > nivel - kMargemFundamental && m - 0.5f * (magMic_[k - 3] + magMic_[k + 3]) > 8.0f;
}

void DetectorMicrofonia::acharPicos() {
    picos_.clear();
    const int metade = n_ / 2;
    for (int k = kMin_; k <= kMax_ && picos_.size() < 256; ++k) {
        const float b = magMic_[k];
        if (b <= cfg_.pisoDb || b <= magMic_[k - 1] || b < magMic_[k + 1]) continue;
        const double pnpr = b - vizinhanca(magMic_, k);
        if (pnpr < kPnprFrouxo) continue;

        // parábola pelos três bins: frequência e nível entre bins
        const double a = magMic_[k - 1], c = magMic_[k + 1];
        const double curva = a - 2.0 * b + c;
        const double desvio = curva < -1e-4 ? 0.5 * (a - c) / curva : 0.0;
        const double kf = k + desvio;
        const double nivel = b - 0.25 * (a - c) * desvio;

        double phpr = 60.0;
        if (2.0 * kf < metade - 2) phpr = nivel - maxPerto(magMic_, 2.0 * kf);
        if (3.0 * kf < metade - 2) phpr = std::min(phpr, nivel - (double) maxPerto(magMic_, 3.0 * kf));

        bool harmonico = temFundamental(kf, nivel, 2) || temFundamental(kf, nivel, 3);
        // Apito tocando satura no limitador e ganha harmônicos fracos (o 5º fica uns 35 dB abaixo): um pico
        // num múltiplo inteiro de uma trilha em laranja ou vermelho é distorção dela, não outro apito.
        for (const auto& t : trilhas_) {
            if (harmonico || !t.ativa || t.estado < Estado::laranja || t.f <= 0) continue;
            const double vezes = kf * hzPorBin_ / t.f;
            const double inteiro = std::round(vezes);
            harmonico = inteiro >= 2 && inteiro <= kMaiorHarmonicoDoApito && std::abs(vezes - inteiro) * t.f <= std::max(1.5 * hzPorBin_, 0.005 * kf * hzPorBin_);
        }

        // Frequência presente numa linha é nota; presente só nos mics é suspeita. Mas a nota só
        // desculpa o pico se o mic não o destacar muito mais do que a linha destaca: malha perto
        // do limite pega uma nota que cai no modo e a amplifica, e isso é apito, não música.
        bool naLinha = false;
        if (cfg_.usarLinhas) {
            const float m = maxPerto(linhasRetidas_, kf);
            naLinha = m > cfg_.pisoDb && m - vizinhanca(linhasRetidas_, (int) std::lround(kf)) > 6.0f
                      && destaque(nivel, totalMicDb_) <= destaque(m, totalLinhasDb_) + kMargemLinha;
        }

        Pico p;
        p.f = kf * hzPorBin_;
        p.nivel = nivel;
        // Apito que já domina a banda satura no limiter e ganha harmônicos: aí o PHPR afrouxa.
        const bool risco = risco_[(size_t) k] != 0;
        const double phprMinimo = nivel >= totalMicDb_ - kFolgaDominante ? kPhprSaturado : cfg_.phprDb;
        p.suspeitoEstrito = pnpr >= cfg_.pnprDb && phpr >= phprMinimo && !harmonico && !naLinha;
        p.suspeito = risco ? pnpr >= cfg_.pnprDb - kRiscoPnpr && phpr >= phprMinimo - kRiscoPhpr && !harmonico && !naLinha : p.suspeitoEstrito;
        p.estreito = pnpr >= kPnprAnel && phpr >= kPhprAnel && !harmonico && !naLinha;
        p.usado = false;
        picos_.push_back(p);
    }
}

void DetectorMicrofonia::guardar(Trilha& t) {
    t.nivelHist[t.pos] = (float) t.nivel;
    t.fHist[t.pos] = (float) t.f;
    if (++t.pos >= kHistorico) t.pos = 0;
}

// Reta de mínimos quadrados sobre os últimos n níveis: inclinação em dB/s e desvio em dB.
void DetectorMicrofonia::ajustarReta(const Trilha& t, int n, double& inclinacao, double& residuo) const {
    inclinacao = 0;
    residuo = 0;
    if (n < 3) return;
    const int p0 = (t.pos - n + kHistorico) % kHistorico;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; ++i) {
        const double y = t.nivelHist[(p0 + i) % kHistorico];
        sx += i;
        sy += y;
        sxx += (double) i * i;
        sxy += i * y;
    }
    const double den = n * sxx - sx * sx;
    const double b = den > 0 ? (n * sxy - sx * sy) / den : 0.0;
    const double a = (sy - b * sx) / n;
    double r = 0;
    for (int i = 0; i < n; ++i) {
        const double e = t.nivelHist[(p0 + i) % kHistorico] - (a + b * i);
        r += e * e;
    }
    residuo = std::sqrt(r / n);
    inclinacao = b / tempoDoSalto_;
}

// Quanto a frequência da trilha andou nos últimos n quadros, em Hz.
double DetectorMicrofonia::passeio(const Trilha& t, int n) const {
    const int p0 = (t.pos - n + kHistorico) % kHistorico;
    float lo = 1e9f, hi = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float v = t.fHist[(p0 + i) % kHistorico];
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return hi - lo;
}

void DetectorMicrofonia::seguirTrilha(Trilha& t) {
    int melhor = -1;
    double distancia = 1.5 * hzPorBin_;
    for (int i = 0; i < (int) picos_.size(); ++i) {
        if (picos_[i].usado) continue;
        const double d = std::abs(picos_[i].f - t.f);
        if (d < distancia) {
            distancia = d;
            melhor = i;
        }
    }

    bool ok = false;
    if (melhor >= 0) {
        Pico& p = picos_[melhor];
        p.usado = true;
        t.f = p.f;
        t.nivel = p.nivel;
        // Apito estabelecido satura e ganha harmônicos: depois de laranja basta seguir estreito e forte.
        ok = t.estado >= Estado::laranja ? t.nivel >= t.nivelMax - kQuedaMaxima : p.suspeito;
        t.estrita = p.suspeitoEstrito ? t.estrita + 1 : std::max(0, t.estrita - 2);
    } else {
        t.estrita = std::max(0, t.estrita - 2);
        t.nivel = magMic_[std::clamp((int) std::lround(t.f / hzPorBin_), 0, n_ / 2)];
    }

    if (ok) {
        ++t.suspeita;
        t.ruim = 0;
    } else {
        t.suspeita = std::max(0, t.suspeita - 2);
        ++t.ruim;
    }
    ++t.idade;
    t.nivelMax = std::max(t.nivelMax, t.nivel);
    guardar(t);

    double inclinacao, residuo;
    ajustarReta(t, std::min(t.idade, janelaReta_), inclinacao, residuo);
    t.inclinacao = inclinacao;
    // Voz cantada tem vibrato; apito é seno parado.
    const double andou = passeio(t, std::min(t.idade, janelaParado_));
    const bool parado = andou <= std::max(0.35 * hzPorBin_, 0.003 * t.f);
    const double tempoSuspeito = t.suspeita * tempoDoSalto_;

    t.risco = emRisco(t.f);
    const double fracao = t.risco ? kRiscoTempo : 1.0;
    const double crescimento = cfg_.crescimentoDbPorS;
    // no mapa a folga leva até laranja; vermelho exige o pico suspeito também pelo critério de sempre
    const bool estritoBasta = !t.risco || t.estrita * tempoDoSalto_ >= kTempoVermelho;
    Estado novo = Estado::nenhum;
    if (parado) {
        if (tempoSuspeito >= kTempoAmarelo * fracao) novo = Estado::amarelo;
        if (tempoSuspeito >= kTempoLaranja * fracao && inclinacao > -6.0) novo = Estado::laranja;
        // Crescendo em linha reta, medido depois que o ataque saiu da janela. Apito que cresce está no
        // próprio máximo e sobe também numa janela duas vezes maior; nota que balança pode dar uma reta
        // curta subindo sem nenhuma das duas coisas.
        if (tempoSuspeito >= kTempoVermelho && estritoBasta && t.idade >= janelaReta_ + borrao_
            && inclinacao >= crescimento && residuo < kResiduoMaximo && t.nivel >= t.nivelMax - kFolgaDoMaximo) {
            double inclinacaoLonga, residuoLongo;
            ajustarReta(t, std::min(t.idade, 2 * janelaReta_), inclinacaoLonga, residuoLongo);
            if (inclinacaoLonga >= 0.5 * crescimento) novo = Estado::vermelho;
        }
    }
    // Já saturado: o pico sozinho domina a banda inteira. Apito no limiter balança alguns Hz,
    // então aqui vale 1 % de passeio, ainda bem menos que um vibrato.
    if (tempoSuspeito >= kTempoDominante && estritoBasta && inclinacao > -3.0 && t.nivel >= totalMicDb_ - kFolgaDominante
        && andou <= std::max(0.5 * hzPorBin_, 0.01 * t.f))
        novo = Estado::vermelho;
    if (novo > t.estado) {
        if (t.estado == Estado::nenhum) t.tInicio = agora_;
        if (novo >= Estado::laranja && t.tLaranja < 0) t.tLaranja = agora_;
        if (novo == Estado::vermelho) t.tVermelho = agora_;
        t.estado = novo;
    }
    if (t.estado != Estado::nenhum) {
        t.somaF += t.f;
        ++t.nF;
    }
    if (t.ruim > maxRuim_) fecharTrilha(t);
}

void DetectorMicrofonia::abrirTrilha(const Pico& p) {
    for (auto& t : trilhas_) {
        if (t.ativa) continue;
        t = Trilha {};
        t.ativa = true;
        t.f = p.f;
        t.nivel = t.nivelMax = p.nivel;
        t.idade = 1;
        t.suspeita = 1;
        guardar(t);
        return;
    }
}

void DetectorMicrofonia::fecharTrilha(Trilha& t) {
    if (t.estado != Estado::nenhum) {
        Evento e;
        e.f = t.nF > 0 ? t.somaF / t.nF : t.f;
        e.tInicio = t.tInicio;
        e.tLaranja = t.tLaranja;
        e.tVermelho = t.tVermelho;
        e.duracao = agora_ - t.tInicio;
        e.nivelMaxDb = t.nivelMax;
        e.estado = t.estado;
        e.risco = t.risco;
        e.ringOut = doRingOut(e.tInicio);
        // reincidência só conta o que chegou a tocar no show: amarelo é só suspeita, e o ring-out foi de propósito
        for (const auto& antes : eventos_)
            if (antes.estado >= Estado::laranja && !antes.ringOut && std::abs(antes.f / e.f - 1.0) < 0.03) ++e.voltou;
        if (e.ringOut && mapa_.acrescentar(e)) {
            ++revisaoDoMapa_;
            marcarRisco();
        }
        if (e.estado == Estado::amarelo) {
            for (auto& p : amarelosPendentes_)
                if (std::abs(p.f / e.f - 1.0) < 0.03) {
                    p.duracao = agora_ - p.tInicio;
                    p.nivelMaxDb = std::max(p.nivelMaxDb, e.nivelMaxDb);
                    ++p.vezes;
                    t.ativa = false;
                    return;
                }
            amarelosPendentes_.push_back(e);
        } else {
            eventos_.push_back(e);
        }
    }
    t.ativa = false;
}

// ------------------------------------------------------------------ mapa de risco

void DetectorMicrofonia::definirMapa(const MapaDeRisco& mapa) {
    mapa_ = mapa;
    ++revisaoDoMapa_;
    marcarRisco();
}

void DetectorMicrofonia::iniciarRingOut() {
    if (gravandoRingOut_) return;
    gravandoRingOut_ = true;
    inicioDoRingOut_ = agora_;
    fimDoRingOut_ = -1;
}

void DetectorMicrofonia::encerrarRingOut() {
    if (!gravandoRingOut_) return;
    gravandoRingOut_ = false;
    fimDoRingOut_ = agora_;
}

// Um apito que começou no ring-out e só fechou depois (o engenheiro encerrou com ele ainda tocando) é do ring-out.
bool DetectorMicrofonia::doRingOut(double tInicio) const {
    if (inicioDoRingOut_ < 0 || tInicio < inicioDoRingOut_) return false;
    return gravandoRingOut_ || tInicio <= fimDoRingOut_;
}

bool DetectorMicrofonia::emRisco(double f) const { return mapa_.perto(f, kToleranciaDeRisco) >= 0; }

// Marca os bins perto de cada frequência do mapa: ±2 %, e nunca menos de um bin e meio.
void DetectorMicrofonia::marcarRisco() {
    risco_.assign((size_t) n_ / 2 + 1, 0);
    if (hzPorBin_ <= 0) return;
    for (const auto& r : mapa_.itens()) {
        const double meia = std::max(kToleranciaDeRisco * r.f, 1.5 * hzPorBin_);
        const int de = std::max(0, (int) std::floor((r.f - meia) / hzPorBin_));
        const int ate = std::min(n_ / 2, (int) std::ceil((r.f + meia) / hzPorBin_));
        for (int k = de; k <= ate; ++k) risco_[(size_t) k] = 1;
    }
}

// ------------------------------------------------------------------ pré-apito

double DetectorMicrofonia::nivelEm(double f) const {
    const int k = std::clamp((int) std::lround(f / hzPorBin_), 1, n_ / 2 - 1);
    return std::max(magMic_[k], std::max(magMic_[k - 1], magMic_[k + 1]));
}

void DetectorMicrofonia::comecarQueda(Anel& a, double nivel) {
    a.pico = a.minimo = nivel;
    a.tPico = agora_;
    a.n = a.nEstreito = 0;
    a.fMin = 1e9;
    a.fMax = 0;
}

// Reta de mínimos quadrados sobre os pontos da queda abaixo de um nível. Vale se descer, se tiver
// pontos e extensão bastantes e se a queda for exponencial (reta em dB).
bool DetectorMicrofonia::ajustarQueda(const Anel& a, double abaixoDe, double& taxa) const {
    double sx = 0, sy = 0, sxx = 0, sxy = 0, alto = -1e9, baixo = 1e9;
    int n = 0;
    for (int i = 0; i < a.n; ++i) {
        const double x = a.x[(size_t) i], y = a.y[(size_t) i];
        if (y > abaixoDe) continue;
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
        alto = std::max(alto, y);
        baixo = std::min(baixo, y);
        ++n;
    }
    const double den = n * sxx - sx * sx;
    if (n < 4 || alto - baixo < kExtensaoMinima || den <= 0) return false;
    const double b = (n * sxy - sx * sy) / den;
    const double c = (sy - b * sx) / n;
    double r = 0;
    for (int i = 0; i < a.n; ++i) {
        if (a.y[(size_t) i] > abaixoDe) continue;
        const double e = a.y[(size_t) i] - (c + b * a.x[(size_t) i]);
        r += e * e;
    }
    if (b >= 0 || std::sqrt(r / n) > kDesvioDaQueda) return false;
    taxa = -b;
    return true;
}

// Fecha a queda em andamento e, se ela foi uma reta limpa de um pico estreito, guarda quanto caiu por segundo.
void DetectorMicrofonia::fecharQueda(Anel& a) {
    // o anel é o que sobra depois do ataque: ele tem que aparecer como pico estreito em boa parte da queda
    // (no fim ela passa do piso e deixa de contar como pico, então um terço basta)
    if (a.n < 4 || a.pico - a.minimo < kExtensaoMinima || a.nEstreito < std::max(3, a.n / 3) || a.pico < cfg_.pisoDb) return;
    // e parado em frequência: harmônico de voz com vibrato também cai devagar no bin, mas anda.
    // O anel é um seno parado, e a parábola entre bins o acha no mesmo lugar a cada quadro.
    if (a.fMax - a.fMin > kAndaNaQueda * hzPorBin_) return;
    // A queda inteira, ou só a cauda: quando a nota sai do bin primeiro, a queda tem um joelho, e o anel é o que vem depois dele.
    double taxa = 0;
    if (!ajustarQueda(a, a.pico, taxa) && !ajustarQueda(a, a.pico - kJoelho, taxa)) return;
    const int i = a.medidas % kMedidas;
    a.taxa[(size_t) i] = taxa;
    a.quando[(size_t) i] = agora_;
    ++a.medidas;
    avaliarAnel(a);
}

// "Fica mais longo a cada ataque": a mediana das três últimas quedas contra a das (até) três de antes.
void DetectorMicrofonia::avaliarAnel(Anel& a) {
    const auto taxa = [&](int voltas) { return a.taxa[(size_t) ((a.medidas - 1 - voltas) % kMedidas)]; };
    const auto quando = [&](int voltas) { return a.quando[(size_t) ((a.medidas - 1 - voltas) % kMedidas)]; };
    if (a.medidas < 3) return;
    const double agora = mediana3(taxa(0), taxa(1), taxa(2));
    if (a.alertando) {
        a.taxaAgora = agora;
        a.taxaMin = std::min(a.taxaMin, agora);
        a.ultimaMedida = agora_;
        ++a.ataques;
        // o anel voltou a encurtar: alguém baixou o ganho
        if (agora > kAlongou * cfg_.anelLongoDbPorS * (a.risco ? kRiscoAnel : 1.0)) fecharAnel(a);
        return;
    }
    a.risco = emRisco(a.f);
    // no mapa basta uma medida de antes para comparar
    if (a.medidas < (a.risco ? 4 : 5) || agora_ - quando(a.risco ? 3 : 4) > kJanelaDeMedidas) return;
    const double antes = a.medidas >= 6 && agora_ - quando(5) <= kJanelaDeMedidas ? mediana3(taxa(3), taxa(4), taxa(5))
                         : a.medidas >= 5                                         ? 0.5 * (taxa(3) + taxa(4))
                                                                                  : taxa(3);
    const double limite = cfg_.anelLongoDbPorS * (a.risco ? kRiscoAnel : 1.0);
    if (agora > limite || antes < (a.risco ? kRiscoAlongou : kAlongou) * agora) return;
    a.alertando = true;
    a.tInicio = agora_;
    a.taxaAntes = antes;
    a.taxaAgora = a.taxaMin = agora;
    a.nivelMax = a.pico;
    a.ultimaMedida = agora_;
    a.ataques = 3;
}

void DetectorMicrofonia::fecharAnel(Anel& a) {
    Evento e;
    e.f = a.f;
    e.tInicio = a.tInicio;
    e.duracao = agora_ - a.tInicio;
    e.nivelMaxDb = a.nivelMax;
    e.estado = Estado::amarelo;
    e.preApito = true;
    e.decaiAntesDbPorS = a.taxaAntes;
    e.decaiMinDbPorS = a.taxaMin;
    e.ataques = a.ataques;
    e.risco = a.risco;
    e.ringOut = doRingOut(e.tInicio);
    if (e.ringOut && mapa_.acrescentar(e)) {
        ++revisaoDoMapa_;
        marcarRisco();
    }
    eventos_.push_back(e);
    a.alertando = false;
    a.medidas = 0;      // o próximo alerta nessa frequência precisa alongar de novo
}

void DetectorMicrofonia::vigiarAneis() {
    for (auto& a : aneis_) a.vistoAgora = false;

    // cada pico estreito entra na vigia, ou marca a frequência que já está nela
    for (const auto& p : picos_) {
        if (!p.estreito) continue;
        Anel* achado = nullptr;
        for (auto& a : aneis_)
            if (a.ativo && std::abs(a.f - p.f) <= 1.5 * hzPorBin_) achado = &a;
        if (achado == nullptr) {
            // vaga livre; senão a frequência vista há mais tempo, primeiro entre as que ainda não têm queda medida
            for (auto& a : aneis_)
                if (!a.ativo && achado == nullptr) achado = &a;
            const auto antes = [](const Anel& a, const Anel& b) {
                return (a.medidas > 0) != (b.medidas > 0) ? b.medidas > 0 : a.vistoEm < b.vistoEm;
            };
            if (achado == nullptr)
                for (auto& a : aneis_)
                    if (!a.alertando && (achado == nullptr || antes(a, *achado))) achado = &a;
            if (achado == nullptr) continue;
            *achado = Anel {};
            achado->ativo = true;
            achado->f = p.f;
            comecarQueda(*achado, nivelEm(p.f));
        } else if (!achado->alertando) {
            achado->f += 0.3 * (p.f - achado->f);
        }
        achado->vistoAgora = true;
        achado->vistoEm = agora_;
        achado->fVisto = p.f;
    }

    for (auto& a : aneis_) {
        if (!a.ativo) continue;
        const double v = nivelEm(a.f);
        if (a.alertando) a.nivelMax = std::max(a.nivelMax, v);
        if (v > a.pico) {
            comecarQueda(a, v);
        } else if (v > a.pico - kTopoDaQueda) {
            // ainda no topo: nem começou a cair
        } else if (v > a.minimo + kReexcitou || agora_ - a.tPico > kQuedaLonga || v < a.pico - kQuedaFunda) {
            fecharQueda(a);
            comecarQueda(a, v);
        } else if (a.n < kPontosDaQueda) {
            a.x[(size_t) a.n] = (float) (agora_ - a.tPico);
            a.y[(size_t) a.n] = (float) v;
            ++a.n;
            if (a.vistoAgora) {
                ++a.nEstreito;
                a.fMin = std::min(a.fMin, a.fVisto);
                a.fMax = std::max(a.fMax, a.fVisto);
            }
            a.minimo = std::min(a.minimo, v);
        }
        if (a.alertando && agora_ - a.ultimaMedida > kSemMedida) fecharAnel(a);
        if (!a.alertando && agora_ - a.vistoEm > kEsquecerAnel) a.ativo = false;
    }
}

void DetectorMicrofonia::analisar() {
    // a soma dos bins de um seno com janela Hann dá o pico + 1,76 dB
    totalMicDb_ = 10.0 * std::log10(espectro(anelMic_, magMic_) + kMinusculo) - normDb_ - 1.76;
    if (cfg_.usarLinhas) {
        totalLinhasDb_ = 10.0 * std::log10(espectro(anelLinhas_, magLinhas_) + kMinusculo) - normDb_ - 1.76;
        const float queda = (float) (kQuedaDaRetencao * tempoDoSalto_);
        for (size_t k = 0; k < magLinhas_.size(); ++k)
            linhasRetidas_[k] = std::max(magLinhas_[k], linhasRetidas_[k] - queda);
    }

    acharPicos();
    soltarAmarelos(false);
    if (cfg_.preApito) vigiarAneis();
    for (auto& t : trilhas_)
        if (t.ativa) seguirTrilha(t);
    for (const auto& p : picos_)
        if (!p.usado && p.suspeito) abrirTrilha(p);

    suspeitas_.clear();
    for (const auto& t : trilhas_) {
        if (!t.ativa || t.estado == Estado::nenhum) continue;
        Suspeita s;
        s.f = t.f;
        s.nivelDb = t.nivel;
        s.inclinacaoDbPorS = t.inclinacao;
        s.duracao = agora_ - t.tInicio;
        s.estado = t.estado;
        s.risco = t.risco;
        suspeitas_.push_back(s);
    }
    for (const auto& a : aneis_) {
        if (!a.ativo || !a.alertando) continue;
        Suspeita s;
        s.f = a.f;
        s.nivelDb = nivelEm(a.f);
        s.duracao = agora_ - a.tInicio;
        s.estado = Estado::amarelo;
        s.preApito = true;
        s.decaiDbPorS = a.taxaAgora;
        s.risco = a.risco;
        suspeitas_.push_back(s);
    }
}

} // namespace papagaio
