"""Gera sinais de teste sintéticos para o detector de microfonia.

Cada cenário vira um WAV estéreo 48 kHz / 24 bits em testes/sinais:
  canal 1 = mic de medição (banda + público + apito, quando houver)
  canal 2 = soma das linhas (baixo e teclado), o gabarito do que é música
e um JSON com o gabarito dos eventos (frequência, quando cruzou ganho 1, quando ficou audível).

O apito não é um seno colado por cima: é uma malha de realimentação simulada
(mic -> ganho -> caixa -> sala -> mic), então cresce em linha reta em dB, satura e
toca em ressonância quando o ganho fica logo abaixo do limite, como no palco.

O cenário 08 é o do pré-apito: só voz, em frases com respiro, e o ganho de malha subindo em degraus
(o fader indo para cima ao longo da música). A cada fim de frase o anel fica mais longo, até apitar.

O cenário 09 é o do mapa de risco: 24 s de ring-out (o engenheiro falando no mic e subindo o ganho até
apitar, em 2,5 kHz e depois em 1,25 kHz), e o show com a banda, onde a malha de 2,5 kHz volta devagar.

O cenário 05 é o do módulo 2 (drift): canal 1 = mic, canal 2 = L/R da mesa. O mic é o L/R
passado por uma "sala" (atraso, som direto e cauda), que muda no meio do show.

Só precisa de numpy. Serve para desenvolver; quem decide é show gravado.
"""
import json
from pathlib import Path

import numpy as np

SR = 48000
RAIZ = Path(__file__).resolve().parent.parent
SAIDA = RAIZ / "testes" / "sinais"

ATRASO = 576        # 12 ms de malha, cerca de 4 m entre caixa e mic
TAM_RESSOADOR = 2048


def db(x):
    return 10 ** (x / 20)


def ruido_rosa(n, rng):
    X = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1 / SR)
    X[1:] /= np.sqrt(f[1:])
    X[0] = 0
    x = np.fft.irfft(X, n)
    return x / np.sqrt(np.mean(x**2))


def nota(f0, dur, amps, ataque=0.01, tau=None, vibrato=(0, 0), solta=0.05):
    n = int(dur * SR)
    t = np.arange(n) / SR
    taxa, cents = vibrato
    prof = cents * np.clip((t - 0.15) / 0.2, 0, 1)     # vibrato entra depois do ataque
    f = f0 * 2 ** (prof * np.sin(2 * np.pi * taxa * t) / 1200)
    fase = 2 * np.pi * np.cumsum(f) / SR
    x = np.zeros(n)
    for h, a in enumerate(amps, start=1):
        if a and f0 * h < SR * 0.45:
            x += a * np.sin(h * fase)
    env = np.minimum(1, t / ataque)
    if tau:
        env = env * np.exp(-t / tau)
    env = env * np.clip((dur - t) / solta, 0, 1)
    return x * env


def somar(dest, x, t0):
    i = int(t0 * SR)
    j = min(len(dest), i + len(x))
    if j > i:
        dest[i:j] += x[: j - i]


def amps_voz(f0):
    """Harmônicos de uma vogal aberta: fonte caindo 1/h, três formantes."""
    h = np.arange(1, 25)
    f = h * f0
    g = lambda fc, bw: np.exp(-0.5 * ((f - fc) / bw) ** 2)
    return (1 / h) * (0.3 + 3 * g(650, 130) + 2 * g(1100, 160) + 1 * g(2700, 260))


ACORDES = [  # (baixo, tríade)
    (55.00, (220.00, 261.63, 329.63)),    # Am
    (43.65, (174.61, 220.00, 261.63)),    # F
    (65.41, (261.63, 329.63, 392.00)),    # C
    (49.00, (196.00, 246.94, 293.66)),    # G
]


def banda(dur, rng, toca_ate=None, senos_no_teclado=(), com_seco=False, chimbal_db=0.0):
    """Devolve (mic, linhas), e também a mixagem seca se com_seco. senos_no_teclado: lista de (t0, dur, f, ataque)."""
    n = int(dur * SR)
    toca_ate = dur if toca_ate is None else toca_ate
    bumbo, chimbal, baixo, teclado, voz = (np.zeros(n) for _ in range(5))

    t_kick = np.arange(int(0.25 * SR)) / SR
    kick = np.sin(2 * np.pi * (50 * t_kick + 70 * 0.04 * (1 - np.exp(-t_kick / 0.04)))) * np.exp(-t_kick / 0.06)

    tempo = 0.5
    t = 0.0
    compasso = 0
    while t < toca_ate:
        raiz, triade = ACORDES[compasso % 4]
        for b in range(4):
            tb = t + b * tempo
            somar(bumbo, 0.25 * kick, tb)
            hh = rng.standard_normal(int(0.04 * SR)) * np.exp(-np.arange(int(0.04 * SR)) / (0.01 * SR))
            somar(chimbal, 0.03 * db(chimbal_db) * np.diff(hh, 2, prepend=[0, 0]), tb + tempo / 2)
            for c in (0, tempo / 2):
                somar(baixo, 0.15 * nota(raiz, 0.24, 1 / np.arange(1, 9), tau=0.4), tb + c)
        for f0 in triade:
            somar(teclado, 0.06 * nota(f0, 2.0, 1 / np.arange(1, 7) ** 1.5, tau=1.2), t)
        # uma frase de voz por compasso, na oitava de cima, com vibrato
        tv = t + rng.uniform(0, 0.3)
        while tv < t + 1.8:
            dv = rng.uniform(0.4, 1.4)
            f0 = 2 * triade[rng.integers(3)]
            somar(voz, 0.05 * nota(f0, dv, amps_voz(f0), ataque=0.06, vibrato=(5.5, 35), solta=0.08), tv)
            tv += dv + rng.uniform(0.05, 0.25)
        t += 4 * tempo
        compasso += 1

    for t0, d, f, ataque in senos_no_teclado:
        somar(teclado, 0.10 * nota(f, d, [1], ataque=ataque, solta=0.2), t0)

    publico = db(-46) * ruido_rosa(n, rng)
    seco = bumbo + chimbal + baixo + teclado + voz
    mic = seco.copy()
    for atraso_ms, g in ((23, 0.30), (41, -0.22), (67, 0.15)):   # um pouco de sala
        d = int(atraso_ms * SR / 1000)
        mic[d:] += g * seco[:-d]
    if com_seco:
        return mic + publico, baixo + teclado, seco
    return mic + publico, baixo + teclado


def voz_em_frases(dur, rng, nivel=0.08):
    """Devolve (mic, linhas): uma voz cantando frases de 1,5 a 3 s com respiro de 0,6 a 1,2 s, sem banda.
    As linhas ficam em silêncio: a voz não passa por elas."""
    n = int(dur * SR)
    voz = np.zeros(n)
    notas = [220.0, 246.94, 261.63, 293.66, 329.63, 392.0]
    t = 0.5
    while t < dur - 2:
        fim = t + rng.uniform(1.5, 3.0)
        while t < fim:
            dv = rng.uniform(0.25, 0.7)
            f0 = notas[rng.integers(len(notas))]
            somar(voz, nivel * nota(f0, dv, amps_voz(f0), ataque=0.03, vibrato=(5.5, 30), solta=0.06), t)
            t += dv + rng.uniform(0.0, 0.06)
        t += rng.uniform(0.6, 1.2)
    mic = voz.copy()
    for atraso_ms, g in ((23, 0.30), (41, -0.22), (67, 0.15)):
        d = int(atraso_ms * SR / 1000)
        mic[d:] += g * voz[:-d]
    return mic + db(-50) * ruido_rosa(n, rng), np.zeros(n)


def resposta_malha(f_res, tau=0.003):
    """Caminho caixa -> mic: atraso, um pico de ressonância em f_res e duas reflexões."""
    h = np.zeros(ATRASO + TAM_RESSOADOR)
    k = np.arange(TAM_RESSOADOR)
    res = np.exp(-k / (tau * SR)) * np.cos(2 * np.pi * f_res * k / SR)
    h[ATRASO:] = res / np.abs(np.fft.rfft(res, 65536)).max()
    h[ATRASO] += 0.25
    h[ATRASO + 300] += 0.15
    h[ATRASO + 711] -= 0.15
    # A malha só dispara onde a fase fecha em volta inteira. Normaliza pelo modo mais
    # forte, para que g seja o ganho de malha de verdade na frequência que vai apitar.
    H = np.fft.rfft(h, 1 << 18)
    modos = np.nonzero((np.diff(np.sign(H.imag)) != 0) & (H.real[:-1] > 0))[0]
    return h / np.abs(H[modos]).max()


def limitar(x):
    a = np.abs(x)
    return np.where(a < 0.4, x, np.sign(x) * (0.4 + 0.2 * np.tanh((a - 0.4) / 0.2)))


def simular_malha(s, episodios):
    """episodios: lista de dicts com t_ini, t_fim, f_res, g, rampa. Com "degraus" = [(t, g), ...], o ganho
    segue esses pontos (em segundos do arquivo, com meio segundo de transição) em vez da rampa até g."""
    n = len(s)
    ganho = np.zeros(n)
    qual = np.full(n, -1)
    for i, e in enumerate(episodios):
        a, b = int(e["t_ini"] * SR), int(e["t_fim"] * SR)
        if "degraus" in e:
            ts, gs = [], []
            for t, gd in e["degraus"]:
                ts += [t, t + 0.5]
                gs += [gs[-1] if gs else 0.3, gd]
            g = np.interp(np.arange(a, b) / SR, ts, gs)
        else:
            r = max(1, int(e["rampa"] * SR))
            g = np.full(b - a, e["g"], dtype=float)
            g[:r] = np.linspace(0.3, e["g"], r)[: len(g[:r])]
        ganho[a:b] = g
        cai = int(0.05 * SR)
        ganho[b:b + cai] = np.linspace(e["g"], 0, cai)[: len(ganho[b:b + cai])]
        qual[a:b + cai] = i
    hs = [resposta_malha(e["f_res"])[ATRASO:] for e in episodios]

    frente = ATRASO + TAM_RESSOADOR
    y = np.zeros(frente + n + ATRASO)
    for n0 in range(0, n, ATRASO):
        n1 = min(n, n0 + ATRASO)
        bloco = s[n0:n1]
        if qual[n0] >= 0:
            z = y[frente + n0 - ATRASO - (TAM_RESSOADOR - 1): frente + n0]
            volta = np.convolve(z, hs[qual[n0]], mode="valid")[: n1 - n0]
            bloco = bloco + ganho[n0:n1] * volta
        y[frente + n0: frente + n1] = limitar(bloco)
    return y[frente: frente + n], ganho


def nivel_banda_estreita(x, f, janela=4096, salto=512):
    """Nível em dBFS do bin mais perto de f ao longo do tempo."""
    w = np.hanning(janela)
    k = int(round(f * janela / SR))
    quadros = np.lib.stride_tricks.sliding_window_view(x, janela)[::salto]
    e = np.exp(-2j * np.pi * k * np.arange(janela) / janela) * w
    mag = np.abs(quadros @ e) / (janela / 4)
    t = (np.arange(len(mag)) * salto + janela) / SR      # instante do fim da janela
    return t, 20 * np.log10(mag + 1e-12)


def medir_evento(y, ganho, e):
    a, b = int(e["t_ini"] * SR), int(e["t_fim"] * SR)
    seg = y[b - SR // 2: b] * np.hanning(SR // 2)
    espectro = np.abs(np.fft.rfft(seg, 1 << 18))
    freqs = np.fft.rfftfreq(1 << 18, 1 / SR)
    perto = (freqs > e["f_res"] - 200) & (freqs < e["f_res"] + 200)
    f = float(freqs[perto][np.argmax(espectro[perto])])
    acima = np.nonzero(ganho[a:b] >= 1.0)[0]
    t_cruza = (a + acima[0]) / SR if len(acima) else None
    t, lvl = nivel_banda_estreita(y, f)
    # audível = passa 6 dB do máximo que a música já fez nessa frequência e fica lá por 0,25 s
    # Com o ganho em degraus, o anel já soa antes de cruzar 1, a cada fim de frase: isso é o pré-apito.
    # O apito em si é quando passa 6 dB do que o anel já fazia nos 8 s antes de cruzar.
    degraus = "degraus" in e and t_cruza is not None
    referencia = t_cruza if degraus else e["t_ini"]
    antes = lvl[(t > referencia - 8) & (t < referencia)]
    limiar = (float(antes.max()) if len(antes) else -90.0) + 6
    fica = int(0.25 * SR / 512)
    acima = np.convolve(lvl > limiar, np.ones(fica), mode="full")[fica - 1:] >= fica
    desde = t_cruza if degraus else e["t_ini"]
    dentro = (t >= desde) & (t <= e["t_fim"]) & acima[: len(t)]
    t_audivel = float(t[dentro][0]) if dentro.any() else None
    extra = {}
    if "degraus" in e:
        # o anel cai abaixo de uns 80 dB/s com ganho de malha 0,9 ou mais: daí até cruzar 1 é a janela do pré-apito
        perto = np.nonzero(ganho[a:b] >= 0.9)[0]
        extra["pre_apito"] = dict(desde=round((a + perto[0]) / SR, 3) if len(perto) else None,
                                  ate=None if t_cruza is None else round(t_cruza, 3))
    return extra | {
        "tipo": "apito" if e["g"] >= 1 else "ressonancia",
        "f": round(f, 1),
        "g": e["g"],
        "t_ini": e["t_ini"],
        "t_fim": e["t_fim"],
        "t_cruza": None if t_cruza is None else round(t_cruza, 3),
        "t_audivel": None if t_audivel is None else round(t_audivel, 3),
        "pico_dbfs": round(float(lvl[(t >= e["t_ini"]) & (t <= e["t_fim"] + 0.1)].max()), 1),
    }


def gravar_wav(caminho, canais):
    x = np.stack(canais, axis=1)
    pico = np.abs(x).max()
    assert pico < 1.0, f"{caminho.name}: clipou ({pico:.2f})"
    i = np.round(x * 8388607).astype("<i4")
    dados = i.view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    nc = x.shape[1]
    cab = b"RIFF" + (36 + len(dados)).to_bytes(4, "little") + b"WAVEfmt " + (16).to_bytes(4, "little")
    cab += (1).to_bytes(2, "little") + nc.to_bytes(2, "little") + SR.to_bytes(4, "little")
    cab += (SR * nc * 3).to_bytes(4, "little") + (nc * 3).to_bytes(2, "little") + (24).to_bytes(2, "little")
    cab += b"data" + len(dados).to_bytes(4, "little")
    caminho.write_bytes(cab + dados)


CENARIOS = {
    "01_show_tres_apitos": dict(
        descricao="Banda tocando o tempo todo; apito rápido em 2,5 kHz, lento em 630 Hz e rápido em 4 kHz.",
        dur=75, semente=1,
        episodios=[
            dict(t_ini=12, t_fim=15.5, f_res=2500, g=1.06, rampa=1.0),
            dict(t_ini=30, t_fim=42, f_res=630, g=1.012, rampa=2.0),
            dict(t_ini=55, t_fim=57.5, f_res=4000, g=1.10, rampa=0.5),
        ],
    ),
    "02_so_musica": dict(
        descricao="Só música, sem malha. Inclui dois senos puros no teclado (linha): um parado e um em crescendo.",
        dur=60, semente=2,
        senos=[(20, 3.0, 1318.5, 0.02), (40, 4.0, 880.0, 2.5)],
        episodios=[],
    ),
    "03_ressonancia_pre_apito": dict(
        descricao="Ganho de malha em 0,96 em 1 kHz por 25 s: toca em ressonância, nunca dispara.",
        dur=45, semente=3,
        episodios=[dict(t_ini=10, t_fim=35, f_res=1000, g=0.96, rampa=2.0)],
    ),
    "08_pre_apito": dict(
        descricao="Só voz, em frases com respiro; o ganho de malha em 2,5 kHz sobe em degraus (0,85, 0,90, 0,94, 0,97) "
                  "e apita em 56 s, até o fader descer em 59 s. A cada fim de frase o anel fica mais longo.",
        dur=66, semente=9, fonte="voz",
        episodios=[dict(t_ini=8, t_fim=59, f_res=2500, g=1.03, rampa=1,
                        degraus=[(8, 0.85), (20, 0.90), (32, 0.94), (44, 0.97), (56, 1.03)])],
    ),
    "04_apito_entre_musicas": dict(
        descricao="A banda para em 6 s; com o palco em silêncio, apito em 3,15 kHz.",
        dur=20, semente=4, toca_ate=6,
        episodios=[dict(t_ini=9, t_fim=13, f_res=3150, g=1.05, rampa=1.0)],
    ),
}


# ---------------------------------------------------------------------------
# Cenário de drift: a sala e o mix mudam entre a passagem de som e o show
# ---------------------------------------------------------------------------

def convoluir(x, h):
    """Convolução por FFT em blocos, para sinais de minutos com respostas de dezenas de milhares de pontos."""
    nfft = 1 << 17
    passo = nfft - len(h) + 1
    H = np.fft.rfft(h, nfft)
    y = np.zeros(len(x) + len(h) - 1)
    for i in range(0, len(x), passo):
        bloco = x[i:i + passo]
        y[i:i + len(bloco) + len(h) - 1] += np.fft.irfft(np.fft.rfft(bloco, nfft) * H, nfft)[: len(bloco) + len(h) - 1]
    return y[: len(x)]


def filtro_de_ganho(ganho_db_de_f, pontos=2049):
    """FIR de fase linear com a curva de ganho pedida; convolver e descartar (pontos-1)/2 amostras deixa em fase zero."""
    nfft = 8192
    f = np.fft.rfftfreq(nfft, 1 / SR)
    h = np.fft.irfft(10 ** (ganho_db_de_f(f) / 20), nfft)
    h = np.roll(h, pontos // 2)[:pontos] * np.hanning(pontos)
    return h


def aplicar_ganho(x, ganho_db_de_f):
    h = filtro_de_ganho(ganho_db_de_f)
    meio = len(h) // 2
    return convoluir(np.concatenate([x, np.zeros(meio)]), h)[meio:]


def rampa(t, a, b):
    """0 antes de a, 1 depois de b."""
    return np.clip((t - a) / (b - a), 0, 1)


def nivel_a_db(x):
    """Nível com ponderação A na convenção do motor: seno em fundo de escala lê 0 dB."""
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    f2 = f ** 2
    ra = 12194 ** 2 * f2 ** 2 / ((f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194 ** 2) + 1e-30)
    peso = (ra * 10 ** (2.0 / 20)) ** 2
    peso[f < 20] = 0
    potencia = 2 * np.sum(peso * np.abs(X) ** 2) / len(x) ** 2      # média quadrática ponderada
    return 10 * np.log10(potencia) + 3.01


DRIFT = dict(
    nome="05_casa_enche",
    descricao="Passagem de som até 40 s. No show a casa enche: agudo cai 4 dB e o atraso encurta 1,5 ms entre 80 e 100 s; "
              "o engenheiro sobe 3 dB em 2 a 4 kHz entre 150 e 165 s e o volume sobe 5 dB ao longo do show. Pausa de 6 s em 125 s.",
    dur=240, semente=5,
    passagem_ate=40, show_em=40,
    sala_muda=(80, 100), queda_de_agudo_db=-4.0, atraso_ms=(58.0, 56.5),
    mix_muda=(150, 165), realce_db=3.0, realce_hz=(1800, 4500),
    volume_sobe_db=5.0,
    pausa=(125, 131),
    # janelas curtas, para o teste caber em 4 minutos; os padrões do produto são de minutos
    analise=dict(bloco=5, janela_sala=30, janela_mix=40, referencia=40, sustentacao=20),
)


def gerar_drift():
    c = DRIFT
    rng = np.random.default_rng(c["semente"])
    n = c["dur"] * SR
    t = np.arange(n) / SR
    # pratos mais altos que nos outros cenários: sem programa no agudo não há o que medir lá
    _, _, seco = banda(c["dur"], rng, com_seco=True, chimbal_db=12.0)

    # o que sai da mesa: volume subindo no show, realce em 2 a 4 kHz a partir de mix_muda, uma pausa entre músicas
    lr = seco * db(-10)
    f1, f2 = c["realce_hz"]
    borda = lambda f, fc: 1 / (1 + (np.maximum(f, 1) / fc) ** 8)
    realcado = aplicar_ganho(lr, lambda f: c["realce_db"] * (1 - borda(f, f1)) * borda(f, f2))
    m = rampa(t, *c["mix_muda"])
    lr = (1 - m) * lr + m * realcado
    lr *= db(c["volume_sobe_db"] * rampa(t, c["show_em"], c["dur"]))
    p0, p1 = c["pausa"]
    lr *= 1 - rampa(t, p0, p0 + 0.3) * (1 - rampa(t, p1 - 0.3, p1))

    # a sala: som direto e cauda de 0,8 s a -8 dB. Um pré-atraso dá espaço para o filtro de fase zero.
    pre = 2048
    cauda = rng.standard_normal(int(0.8 * SR)) * np.exp(-6.91 * np.arange(int(0.8 * SR)) / (0.8 * SR))
    cauda *= db(-8) / np.sqrt(np.sum(cauda ** 2))
    def sala(agudo_db):
        h = np.zeros(pre + len(cauda))
        h[pre] = 1.0
        h[pre:] += cauda
        if agudo_db:
            # prateleira: 0 dB até 3,5 kHz, agudo_db de 6 kHz para cima
            curva = lambda f: agudo_db * np.clip(np.log(np.maximum(f, 1) / 3500) / np.log(6000 / 3500), 0, 1)
            h = aplicar_ganho(h, curva)
        return h
    m = rampa(t, *c["sala_muda"])
    no_mic = (1 - m) * convoluir(lr, sala(0.0)) + m * convoluir(lr, sala(c["queda_de_agudo_db"]))
    # o atraso do PA até o mic encurta aos poucos, como o ar esquentando: um caminho só, sem pente
    atraso = (c["atraso_ms"][0] + (c["atraso_ms"][1] - c["atraso_ms"][0]) * m) * SR / 1000 - pre
    no_mic = np.interp(np.arange(n) - atraso, np.arange(n), no_mic, left=0.0)
    publico = ruido_rosa(n, rng) * db(-60 + 22 * rampa(t, c["show_em"], c["show_em"] + 30))
    mic = no_mic + publico

    piso = nivel_a_db(publico[(p0 + 1) * SR: (p1 - 1) * SR])
    gravar_wav(SAIDA / f"{c['nome']}.wav", [mic, lr])
    info = dict(
        arquivo=f"{c['nome']}.wav", descricao=c["descricao"], duracao=c["dur"], eventos=[], senos_na_linha=[],
        drift=dict(
            canal_mic=1, canal_lr=2, passagem_ate=c["passagem_ate"], show_em=c["show_em"], analise=c["analise"],
            sala_muda=list(c["sala_muda"]), mix_muda=list(c["mix_muda"]), pausa=list(c["pausa"]),
            queda_de_agudo_db=c["queda_de_agudo_db"], realce_db=c["realce_db"], volume_sobe_db=c["volume_sobe_db"],
            atraso_ms=list(c["atraso_ms"]), piso_a_db=round(float(piso), 1),
        ),
    )
    (SAIDA / f"{c['nome']}.json").write_text(json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"{c['nome']}: {c['dur']} s, mic pico {20*np.log10(np.abs(mic).max()):.1f} dBFS, L/R pico {20*np.log10(np.abs(lr).max()):.1f} dBFS, "
          f"piso na pausa {piso:.1f} dB(A)")


# ---------------------------------------------------------------------------
# Cenário de níveis: tons e ruído com nível conhecido em cada ponderação
# ---------------------------------------------------------------------------

def curva_db(f, curva):
    """Ponderações A, C e Z pela fórmula da norma, escrita aqui de novo para não depender do motor."""
    f2 = np.maximum(f, 1e-6) ** 2
    if curva == "A":
        r = 12194 ** 2 * f2 ** 2 / ((f2 + 20.6 ** 2) * np.sqrt((f2 + 107.7 ** 2) * (f2 + 737.9 ** 2)) * (f2 + 12194 ** 2))
        return 20 * np.log10(r) + 2.0
    if curva == "C":
        return 20 * np.log10(12194 ** 2 * f2 / ((f2 + 20.6 ** 2) * (f2 + 12194 ** 2))) + 0.06
    return np.zeros_like(f2)


def nivel_db(x, curva):
    """Leq do trecho na convenção do motor (seno em fundo de escala = 0 dB), pesando o espectro inteiro."""
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(len(x), 1 / SR)
    peso = 10 ** (curva_db(f, curva) / 10)
    peso[0] = 0
    return float(10 * np.log10(2 * np.sum(peso * np.abs(X) ** 2) / len(x) ** 2) + 3.01)


def gerar_niveis():
    rng = np.random.default_rng(6)
    seg = 6 * SR
    t = np.arange(seg) / SR
    trechos = [
        ("seno de 1 kHz a -20 dBFS", db(-20) * np.sin(2 * np.pi * 1000 * t)),
        ("seno de 100 Hz a -14 dBFS", db(-14) * np.sin(2 * np.pi * 100 * t)),
        ("ruído rosa a -30 dBFS RMS", db(-30) * ruido_rosa(seg, rng)),
    ]
    x = np.concatenate([s for _, s in trechos])
    esperado = {}
    for curva in "ACZ":
        por_trecho = [nivel_db(s, curva) for _, s in trechos]
        esperado[curva] = dict(
            medio=round(nivel_db(x, curva), 2),
            maximo=round(max(por_trecho), 2),
            # o mínimo é o trecho mais baixo; se for o ruído, a leitura rápida ainda balança para baixo dele
            minimo=round(min(por_trecho), 2),
            minimo_no_ruido=bool(np.argmin(por_trecho) == 2),
        )
    nome = "06_niveis"
    gravar_wav(SAIDA / f"{nome}.wav", [x])
    info = dict(arquivo=f"{nome}.wav", descricao="Três trechos de 6 s: " + "; ".join(n for n, _ in trechos) + ".",
                duracao=len(x) / SR, eventos=[], senos_na_linha=[], niveis=esperado)
    (SAIDA / f"{nome}.json").write_text(json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"{nome}: " + "  ".join(f"{c}: médio {v['medio']:.1f} máx {v['maximo']:.1f} mín {v['minimo']:.1f}" for c, v in esperado.items()))


def gerar_leq():
    """Seis minutos de seno de 1 kHz em degraus, para o Leq deslizante de 1, 5 e 15 min."""
    degraus = [(120, -30.0), (60, -15.0), (60, -24.0), (120, -30.0)]      # (segundos, dBFS de pico)
    t = np.arange(SR) / SR
    um_segundo = np.sin(2 * np.pi * 1000 * t)        # 1000 ciclos inteiros: emenda sem degrau de fase
    x = np.concatenate([np.tile(db(nivel) * um_segundo, dur) for dur, nivel in degraus])
    # energia por segundo na convenção do motor: seno de pico a lê 20·log10(a) dB
    energia = np.concatenate([np.full(dur, db(nivel) ** 2) for dur, nivel in degraus])
    esperado = {}
    for minutos in (1, 5, 15):
        w = 60 * minutos
        if len(energia) < w:
            esperado[str(minutos)] = None
            continue
        media = np.convolve(energia, np.ones(w) / w, mode="valid")
        esperado[str(minutos)] = round(float(10 * np.log10(media.max())), 2)
    nome = "07_leq_deslizante"
    gravar_wav(SAIDA / f"{nome}.wav", [x])
    info = dict(arquivo=f"{nome}.wav", descricao="Seno de 1 kHz: " + ", ".join(f"{d} s a {n:g} dBFS" for d, n in degraus) + ".",
                duracao=len(x) / SR, eventos=[], senos_na_linha=[], maior_leq=esperado)
    (SAIDA / f"{nome}.json").write_text(json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"{nome}: maior Leq " + "  ".join(f"{m} min: {v}" for m, v in esperado.items()))


def gerar_ring_out():
    """Ring-out e depois show. O gabarito diz quais frequências entram no mapa e quando o apito do show volta."""
    rng = np.random.default_rng(10)
    ring_out_ate, show_em, dur = 24.0, 28.0, 90.0
    voz, _ = voz_em_frases(show_em, rng)
    musica, linhas_show = banda(dur - show_em, rng)
    mic = np.concatenate([voz, musica])
    linhas = np.concatenate([np.zeros(len(voz)), linhas_show])
    episodios = [
        # ring-out: o ganho sobe até apitar e o engenheiro corta logo
        dict(t_ini=1, t_fim=10, f_res=2500, g=1.04, rampa=1, degraus=[(1, 0.75), (4, 0.9), (7, 1.04)]),
        dict(t_ini=12, t_fim=21, f_res=1250, g=1.05, rampa=1, degraus=[(12, 0.8), (15, 0.92), (18, 1.05)]),
        # show: a mesma malha de 2,5 kHz volta, devagar, com a banda tocando
        dict(t_ini=50, t_fim=78, f_res=2500, g=1.03, rampa=1, degraus=[(50, 0.85), (58, 0.93), (66, 0.97), (74, 1.03)]),
    ]
    y, ganho = simular_malha(mic, episodios)
    eventos = [medir_evento(y, ganho, e) for e in episodios]
    for e in eventos:
        e["ring_out"] = e["t_fim"] <= ring_out_ate
    nome = "09_ring_out"
    gravar_wav(SAIDA / f"{nome}.wav", [y, linhas])
    info = dict(arquivo=f"{nome}.wav", duracao=dur, eventos=eventos, senos_na_linha=[],
                descricao="Ring-out até 24 s (apito em 2,5 kHz e em 1,25 kHz, cortados logo), show com a banda a partir de 28 s; "
                          "a malha de 2,5 kHz volta em 50 s e apita em 74 s.",
                ring_out=dict(ate=ring_out_ate, mapa=[e["f"] for e in eventos if e["ring_out"]]))
    (SAIDA / f"{nome}.json").write_text(json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"{nome}: mapa esperado " + ", ".join(f"{f:.0f} Hz" for f in info["ring_out"]["mapa"]))
    for e in eventos:
        print(f"   {'ring-out' if e['ring_out'] else 'show':<9} {e['f']:>7.1f} Hz  cruza={e['t_cruza']}  audível={e['t_audivel']}")


def main():
    SAIDA.mkdir(parents=True, exist_ok=True)
    for nome, c in CENARIOS.items():
        rng = np.random.default_rng(c["semente"])
        if c.get("fonte") == "voz":
            mic, linhas = voz_em_frases(c["dur"], rng)
        else:
            mic, linhas = banda(c["dur"], rng, c.get("toca_ate"), c.get("senos", ()))
        y, ganho = simular_malha(mic, c["episodios"])
        eventos = [medir_evento(y, ganho, e) for e in c["episodios"]]
        gravar_wav(SAIDA / f"{nome}.wav", [y, linhas])
        info = dict(arquivo=f"{nome}.wav", descricao=c["descricao"], duracao=c["dur"], eventos=eventos,
                    senos_na_linha=[dict(t_ini=s[0], t_fim=s[0] + s[1], f=s[2]) for s in c.get("senos", ())])
        (SAIDA / f"{nome}.json").write_text(json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8")
        rms = 20 * np.log10(np.sqrt(np.mean(y**2)))
        print(f"{nome}: {c['dur']} s, mic {rms:.1f} dBFS RMS, pico {20*np.log10(np.abs(y).max()):.1f} dBFS")
        for e in eventos:
            print(f"   {e['tipo']:<11} {e['f']:>7.1f} Hz  g={e['g']}  cruza={e['t_cruza']}  audível={e['t_audivel']}  pico={e['pico_dbfs']} dBFS")


if __name__ == "__main__":
    main()
    gerar_drift()
    gerar_niveis()
    gerar_leq()
    gerar_ring_out()
