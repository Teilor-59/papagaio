// papagaio-offline: analisa um show gravado e gera o relatório pós-show (a v0 da proposta).
//
//   papagaio-offline --mic show.wav:1 --linhas show.wav:2 --saida relatorio
//
// O mic é o canal vigiado. Cada --linhas soma mais um canal ao gabarito do que é música.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Relatorio.h"
#include "papagaio/AnalisadorDuplo.h"
#include "papagaio/DetectorMicrofonia.h"
#include "papagaio/IgualAudibilidade.h"
#include "papagaio/LeitorWav.h"
#include "papagaio/MedidorDeNivel.h"
#include "papagaio/MonitorDeDrift.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
using papagaio::Estado;
using papagaio::LeitorWav;

namespace {

struct Fonte {
    fs::path caminho;
    int canal = 0;      // a partir de 0
    int leitor = -1;
    std::string nome() const { return caminho.filename().u8string() + " canal " + std::to_string(canal + 1); }
};

// "arquivo.wav:3" -> arquivo.wav, canal 3. Sem o sufixo, canal 1.
Fonte lerFonte(const std::string& texto) {
    Fonte f;
    const size_t p = texto.rfind(':');
    const bool temCanal = p != std::string::npos && p + 1 < texto.size()
                          && std::all_of(texto.begin() + (std::ptrdiff_t) p + 1, texto.end(), [](char c) { return c >= '0' && c <= '9'; });
    f.caminho = fs::u8path(temCanal ? texto.substr(0, p) : texto);
    f.canal = temCanal ? std::max(1, std::atoi(texto.c_str() + p + 1)) - 1 : 0;
    return f;
}

std::string relogio(double t) {
    char s[32];
    t = std::round(t * 10.0) / 10.0;      // arredonda antes de separar, para nunca mostrar 1:60.0
    const int m = (int) (t / 60);
    std::snprintf(s, sizeof s, "%d:%04.1f", m, t - 60.0 * m);
    return s;
}

void uso() {
    std::puts("uso: papagaio-offline --mic ARQ.wav[:canal] [--linhas ARQ.wav[:canal]]... [--lr ARQ.wav[:canal]]... [opções]\n"
              "\n"
              "  --mic          canal vigiado: o mic de medição\n"
              "  --linhas       canal de linha (DI, teclado, playback) para o gabarito; pode repetir\n"
              "  --saida BASE   grava BASE.html e BASE.json (padrão: ao lado do WAV do mic)\n"
              "  --pnpr DB      pico acima da vizinhança (15)\n"
              "  --phpr DB      pico acima dos harmônicos (25)\n"
              "  --crescimento DB_S   inclinação mínima para vermelho (6)\n"
              "  --piso DBFS    nível mínimo de um pico (-70)\n"
              "  --fmin HZ, --fmax HZ   faixa de análise (100 a 12000)\n"
              "  --fft N        4096 ou 8192\n"
              "\n"
              "mapa de risco do ring-out:\n"
              "  --ring-out-ate SEG   o começo do arquivo, até aqui, é o ring-out: os alertas viram o mapa de risco\n"
              "  --mapa \"F;F...\"     começa com essas frequências no mapa (Hz), como se viessem de um ring-out\n"
              "\n"
              "níveis (A, C e Z; médio, máximo e mínimo):\n"
              "  --calibracao DB  quantos dB SPL valem 0 dB do arquivo; sem isso os níveis saem em dBFS\n"
              "  --lento          ponderação de tempo lenta (1 s) no lugar da rápida (125 ms)\n"
              "\n"
              "drift entre passagem e show (precisa do L/R da mesa):\n"
              "  --lr           canal do L/R da mesa; pode repetir (L e R)\n"
              "  --passagem-ate SEG   o começo do arquivo, até aqui, é a passagem de som: a linha de base da sala\n"
              "  --show-em SEG        começo do show; o primeiro bloco vira a referência do mix (padrão: fim da passagem)\n"
              "  --janela-sala SEG (60), --janela-mix SEG (180), --referencia SEG (180), --sustentacao SEG (120)\n"
              "  --bloco SEG    duração de cada medição (10)\n"
              "  --quieto       não mostra o andamento");
}

int rodar(const std::vector<std::string>& args) {
    Fonte mic;
    std::vector<Fonte> linhas, lr;
    papagaio::ConfigMicrofonia cfg;
    papagaio::ConfigDrift cfgDrift;
    double passagemAte = 0, showEm = -1, duracaoDoBloco = 10, calibracao = 0, ringOutAte = 0;
    std::string mapaInicial;
    bool temCalibracao = false, lento = false;
    std::string saida;
    bool temMic = false, quieto = false;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        const auto valor = [&]() -> std::string {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "falta o valor de %s\n", a.c_str());
                std::exit(2);
            }
            return args[++i];
        };
        if (a == "--mic") { mic = lerFonte(valor()); temMic = true; }
        else if (a == "--linhas") linhas.push_back(lerFonte(valor()));
        else if (a == "--saida") saida = valor();
        else if (a == "--pnpr") cfg.pnprDb = std::atof(valor().c_str());
        else if (a == "--phpr") cfg.phprDb = std::atof(valor().c_str());
        else if (a == "--crescimento") cfg.crescimentoDbPorS = std::atof(valor().c_str());
        else if (a == "--piso") cfg.pisoDb = std::atof(valor().c_str());
        else if (a == "--fmin") cfg.fMin = std::atof(valor().c_str());
        else if (a == "--fmax") cfg.fMax = std::atof(valor().c_str());
        else if (a == "--fft") cfg.tamanhoFft = std::atoi(valor().c_str());
        else if (a == "--lr") lr.push_back(lerFonte(valor()));
        else if (a == "--calibracao") { calibracao = std::atof(valor().c_str()); temCalibracao = true; }
        else if (a == "--lento") lento = true;
        else if (a == "--passagem-ate") passagemAte = std::atof(valor().c_str());
        else if (a == "--show-em") showEm = std::atof(valor().c_str());
        else if (a == "--janela-sala") cfgDrift.janelaSala = std::atof(valor().c_str());
        else if (a == "--janela-mix") cfgDrift.janelaMix = std::atof(valor().c_str());
        else if (a == "--referencia") cfgDrift.duracaoDaReferencia = std::atof(valor().c_str());
        else if (a == "--sustentacao") cfgDrift.sustentacao = std::atof(valor().c_str());
        else if (a == "--bloco") duracaoDoBloco = std::atof(valor().c_str());
        else if (a == "--quieto") quieto = true;
        else if (a == "--ring-out-ate") ringOutAte = std::atof(valor().c_str());
        else if (a == "--mapa") mapaInicial = valor();
        else if (a == "--igual-audibilidade") {
            // conferência das curvas de phon contra a tabela da norma
            const double phon = std::atof(valor().c_str());
            for (const double f : { 20.0, 50.0, 100.0, 1000.0, 4000.0, 12500.0 })
                std::printf("%g phon, %g Hz: %.2f dB SPL\n", phon, f, papagaio::nivelDeIgualAudibilidade(phon, f));
            return 0;
        }
        else if (a == "--ajuda" || a == "-h" || a == "--help") { uso(); return 0; }
        else { std::fprintf(stderr, "opção desconhecida: %s\n\n", a.c_str()); uso(); return 2; }
    }
    if (!temMic) { uso(); return 2; }
    cfg.usarLinhas = !linhas.empty();
    if (showEm < 0) showEm = passagemAte;

    // um leitor por arquivo, mesmo que vários canais saiam dele
    std::vector<std::unique_ptr<LeitorWav>> leitores;
    std::vector<fs::path> abertos;
    const auto abrir = [&](Fonte& f) -> bool {
        for (size_t i = 0; i < abertos.size(); ++i)
            if (abertos[i] == f.caminho) f.leitor = (int) i;
        if (f.leitor < 0) {
            auto l = std::make_unique<LeitorWav>();
            std::string erro;
            if (!l->abrir(f.caminho, erro)) {
                std::fprintf(stderr, "%s: %s\n", f.caminho.u8string().c_str(), erro.c_str());
                return false;
            }
            f.leitor = (int) leitores.size();
            leitores.push_back(std::move(l));
            abertos.push_back(f.caminho);
        }
        if (f.canal >= leitores[f.leitor]->canais()) {
            std::fprintf(stderr, "%s tem %d canais; pediu o canal %d\n", f.caminho.u8string().c_str(),
                         leitores[f.leitor]->canais(), f.canal + 1);
            return false;
        }
        return true;
    };
    if (!abrir(mic)) return 1;
    for (auto& l : linhas)
        if (!abrir(l)) return 1;
    for (auto& l : lr)
        if (!abrir(l)) return 1;

    const double taxa = leitores[mic.leitor]->taxa();
    for (const auto& l : leitores)
        if (l->taxa() != taxa) {
            std::fprintf(stderr, "os arquivos têm taxas de amostragem diferentes\n");
            return 1;
        }
    const long long total = leitores[mic.leitor]->quadros();

    papagaio::DetectorMicrofonia detector;
    detector.preparar(taxa, cfg);
    if (!mapaInicial.empty()) detector.definirMapa(papagaio::MapaDeRisco::deTexto(mapaInicial));
    if (ringOutAte > 0) detector.iniciarRingOut();

    papagaio::MedidorDeNivel medidor;
    medidor.preparar(taxa);
    medidor.definirLento(lento);

    // Com o L/R da mesa, mede também o drift entre passagem e show.
    papagaio::AnalisadorDuplo analisador;
    papagaio::MonitorDeDrift monitor;
    std::vector<PontoDeDrift> curva;
    bool showMarcado = false;
    if (!lr.empty()) {
        analisador.preparar(taxa, duracaoDoBloco);
        monitor.preparar(cfgDrift);
        if (passagemAte > 0) monitor.iniciarPassagem();
    }

    const int bloco = 16384;
    std::vector<std::vector<float>> lido(leitores.size());
    std::vector<int> quantos(leitores.size());
    std::vector<float> canalMic(bloco), canalLinhas(bloco), canalLr(bloco);
    long long feitos = 0;
    int ultimoPorcento = -1;
    for (;;) {
        for (size_t i = 0; i < leitores.size(); ++i) quantos[i] = leitores[i]->ler(lido[i], bloco);
        const int n = quantos[mic.leitor];
        if (n <= 0) break;
        const int canaisMic = leitores[mic.leitor]->canais();
        for (int i = 0; i < n; ++i) canalMic[i] = lido[mic.leitor][(size_t) i * canaisMic + mic.canal];
        std::fill(canalLinhas.begin(), canalLinhas.begin() + n, 0.0f);
        for (const auto& l : linhas) {
            const int c = leitores[l.leitor]->canais();
            const int m = std::min(n, quantos[l.leitor]);
            for (int i = 0; i < m; ++i) canalLinhas[i] += lido[l.leitor][(size_t) i * c + l.canal];
        }
        // o fim do ring-out cai no meio do bloco: processa em dois pedaços
        const int ateORingOut = detector.gravandoRingOut() ? (int) std::clamp((long long) (ringOutAte * taxa) - feitos, 0LL, (long long) n) : n;
        detector.processar(canalMic.data(), linhas.empty() ? nullptr : canalLinhas.data(), ateORingOut);
        if (ateORingOut < n) {
            detector.encerrarRingOut();
            detector.processar(canalMic.data() + ateORingOut, linhas.empty() ? nullptr : canalLinhas.data() + ateORingOut, n - ateORingOut);
        }
        medidor.processar(canalMic.data(), n);
        if (!lr.empty()) {
            std::fill(canalLr.begin(), canalLr.begin() + n, 0.0f);
            for (const auto& l : lr) {
                const int c = leitores[l.leitor]->canais();
                const int m = std::min(n, quantos[l.leitor]);
                for (int i = 0; i < m; ++i) canalLr[i] += lido[l.leitor][(size_t) i * c + l.canal] / (float) lr.size();
            }
            analisador.processar(canalLr.data(), canalMic.data(), n);
            for (const auto& b : analisador.retirarBlocos()) {
                if (monitor.estado().gravandoPassagem && b.tempo > passagemAte) monitor.fecharPassagem();
                if (!showMarcado && b.tempo > showEm) {
                    monitor.marcarInicioDoShow();
                    showMarcado = true;
                }
                monitor.receber(b);
                curva.push_back({ b.tempo, monitor.estado() });
            }
        }
        feitos += n;
        const int porcento = total > 0 ? (int) (100 * feitos / total) : 100;
        if (!quieto && porcento != ultimoPorcento) {
            std::fprintf(stderr, "\ranalisando %3d %%", porcento);
            ultimoPorcento = porcento;
        }
    }
    if (!quieto) std::fprintf(stderr, "\r                 \r");
    detector.encerrar();

    DadosDoRelatorio dados;
    dados.mic = mic.nome();
    for (const auto& l : linhas) dados.linhas.push_back(l.nome());
    dados.taxa = taxa;
    dados.duracao = feitos / taxa;
    dados.config = cfg;
    dados.eventos = detector.eventos();
    dados.mapa = detector.mapa();
    dados.temMapa = ringOutAte > 0 || !mapaInicial.empty();
    dados.ringOutAte = ringOutAte;
    dados.niveis = medidor.leituras();
    dados.temCalibracao = temCalibracao;
    dados.calibracaoDb = calibracao;
    dados.nivelLento = lento;
    if (!lr.empty()) {
        dados.temDrift = true;
        for (const auto& l : lr) dados.lr.push_back(l.nome());
        dados.passagemAte = passagemAte;
        dados.showEm = showEm;
        dados.configDrift = cfgDrift;
        dados.drift = monitor.eventos();
        dados.curva = std::move(curva);
    }
    std::sort(dados.eventos.begin(), dados.eventos.end(), [](const auto& a, const auto& b) { return a.tInicio < b.tInicio; });

    fs::path base = saida.empty() ? fs::path(mic.caminho).replace_extension() += "_papagaio" : fs::u8path(saida);
    const std::string json = montarJson(dados);
    fs::path arqJson = base, arqHtml = base;
    arqJson += ".json";
    arqHtml += ".html";
    std::ofstream(arqJson, std::ios::binary) << json;
    std::ofstream(arqHtml, std::ios::binary) << montarHtml(json);

    int cont[4] = {};
    for (const auto& e : dados.eventos) ++cont[(int) e.estado];
    std::printf("%s de áudio: %d vermelho, %d laranja, %d amarelo\n", relogio(dados.duracao).c_str(), cont[3], cont[2], cont[1]);
    for (const auto& e : dados.eventos)
        if (e.estado >= Estado::laranja)
            std::printf("  %8s  %-8s  %6.0f Hz  GEQ %-5s  %5.1f s  %4.0f %s%s%s%s\n", relogio(e.tInicio).c_str(),
                        papagaio::nomeEstado(e.estado), e.f, papagaio::bandaGeq(e.f).c_str(), e.duracao, e.nivelMaxDb + calibracao,
                        temCalibracao ? "dB SPL" : "dBFS",
                        e.voltou ? "  (voltou)" : "", e.ringOut ? "  (ring-out)" : "", e.risco && !e.ringOut ? "  (no mapa)" : "");
    if (dados.temMapa) {
        std::printf("mapa de risco%s:", ringOutAte > 0 ? (" do ring-out (até " + relogio(ringOutAte) + ")").c_str() : "");
        for (const auto& r : dados.mapa.itens())
            std::printf("  %.0f Hz (%s, %d×)", r.f, r.manual ? "à mão" : papagaio::nomeEstado((Estado) r.pior), r.vezes);
        std::printf("\n");
    }
    for (const auto& e : dados.eventos)
        if (e.preApito)
            std::printf("  %8s  pré-apito %6.0f Hz  GEQ %-5s  %5.1f s  anel caindo %.0f dB/s (antes %.0f), %d ataques%s%s\n",
                        relogio(e.tInicio).c_str(), e.f, papagaio::bandaGeq(e.f).c_str(), e.duracao, e.decaiMinDbPorS, e.decaiAntesDbPorS,
                        e.ataques, e.ringOut ? "  (ring-out)" : "", e.risco && !e.ringOut ? "  (no mapa)" : "");
    {
        const char* nomes[] = { "A", "C", "Z" };
        std::printf("níveis em %s, tempo %s:\n", temCalibracao ? "dB SPL" : "dBFS", lento ? "lento" : "rápido");
        for (size_t w = 0; w < dados.niveis.size(); ++w)
        {
            const auto& n = dados.niveis[w];
            std::printf("  %s  médio %6.1f   máx %6.1f   mín %6.1f", nomes[w], n.medio + calibracao, n.maximo + calibracao, n.minimo + calibracao);
            for (size_t j = 0; j < n.maiorLeqJanela.size(); ++j)
                if (n.maiorLeqJanela[j] > -190.0f)
                    std::printf("   maior Leq %d min %6.1f", papagaio::kMinutosDaJanela[j], n.maiorLeqJanela[j] + calibracao);
            std::printf("\n");
        }
    }
    for (const auto& e : dados.drift) std::printf("  %8s  drift     %s\n", relogio(e.tempo).c_str(), papagaio::descrever(e).c_str());
    std::printf("relatório: %s\n", arqHtml.u8string().c_str());
    return 0;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(fs::path(argv[i]).u8string());
    return rodar(args);
}
#else
int main(int argc, char** argv) {
    return rodar(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
