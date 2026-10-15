"""Roda o papagaio-offline sobre os sinais de teste e confere com o gabarito de cada cenário.

Critérios do módulo 1 da proposta:
  - todo apito do gabarito vira vermelho, em menos de 0,5 s depois de ficar audível
  - ressonância que não dispara nunca vira vermelho
  - nenhum vermelho fora do gabarito (falso alarme)
  - pré-apito: onde o gabarito tem anel alongando, amarelo de pré-apito antes de a malha cruzar 1;
    e nenhum pré-apito fora do gabarito

Mapa de risco (cenário com ring-out):
  - o mapa tem as frequências que apitaram no ring-out, e só elas
  - o apito do show na frequência do mapa sai marcado, vira vermelho a tempo, e o primeiro aviso não
    chega depois do que chegaria sem o mapa
  - nenhum vermelho falso no show

Critérios do módulo 2 (cenário com L/R):
  - o drift da sala e o do mix batem com o que o cenário mudou, cada um na sua região
  - volume, atraso do PA e piso entre músicas batem com o gabarito
  - cada mudança vira um evento, só depois de se sustentar, e nenhum evento a mais

Níveis (cenário de tons e ruído):
  - médio, máximo e mínimo em A, C e Z batem com a conta independente do gerador

Uso:
  python tools/conferir_detector.py           # todos os cenários, com linhas e só mic
  python tools/conferir_detector.py 01 03     # só os que começam com 01 e 03
"""
import json
import subprocess
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
SINAIS = RAIZ / "testes" / "sinais"
SAIDA = RAIZ / "testes" / "saida"
EXE = RAIZ / "build" / "offline" / "Release" / ("papagaio-offline.exe" if sys.platform == "win32" else "papagaio-offline")

ORDEM = {"amarelo": 1, "laranja": 2, "vermelho": 3}


def analisar(wav, com_linhas, base):
    cmd = [str(EXE), "--mic", f"{wav}:1", "--saida", str(base), "--quieto"]
    if com_linhas:
        cmd += ["--linhas", f"{wav}:2"]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))["eventos"]


def conferir(nome, alertas, info):
    linhas, falhas, usados = [], [], set()
    for ev in info["eventos"]:
        perto = [i for i, a in enumerate(alertas)
                 if a["t_inicio"] + a["duracao"] > ev["t_ini"] and a["t_inicio"] < ev["t_fim"] + 1
                 and abs(a["f"] / ev["f"] - 1) < 0.03]
        usados.update(perto)
        pior = max((alertas[i]["estado"] for i in perto), key=ORDEM.get, default="nada")
        t_verm = min((alertas[i]["t_vermelho"] for i in perto if alertas[i]["t_vermelho"] is not None), default=None)
        t_aviso = min((alertas[i]["t_inicio"] for i in perto), default=None)
        txt = f"  {ev['tipo']:<11} {ev['f']:>7.1f} Hz  g={ev['g']:<5}  -> {pior}"
        if ev.get("pre_apito"):
            janela = ev["pre_apito"]
            pre = [alertas[i] for i in perto if alertas[i].get("pre_apito") and janela["desde"] <= alertas[i]["t_inicio"] <= janela["ate"]]
            if pre:
                txt += f", pré-apito {janela['ate'] - pre[0]['t_inicio']:.1f} s antes de cruzar ({pre[0]['decai_min_db_s']:.0f} dB/s)"
            else:
                falhas.append(f"{nome}: faltou o pré-apito em {ev['f']} Hz entre {janela['desde']} e {janela['ate']} s")
        if ev["tipo"] == "apito":
            if t_verm is None:
                falhas.append(f"{nome}: apito em {ev['f']} Hz sem vermelho")
            elif ev["t_audivel"] is not None:
                atraso = t_verm - ev["t_audivel"]
                txt += f", vermelho {atraso:+.2f} s e primeiro aviso {t_aviso - ev['t_audivel']:+.2f} s do audível"
                if atraso > 0.5:
                    falhas.append(f"{nome}: vermelho em {ev['f']} Hz demorou {atraso:.2f} s")
        elif pior == "vermelho":
            falhas.append(f"{nome}: ressonância em {ev['f']} Hz virou vermelho")
        linhas.append(txt)

    sobra = [a for i, a in enumerate(alertas) if i not in usados]
    for a in sobra:
        if a.get("pre_apito"):
            linhas.append(f"  fora do gabarito: pré-apito em {a['t_inicio']:.2f} s, {a['f']:.0f} Hz")
            falhas.append(f"{nome}: pré-apito falso em {a['t_inicio']:.2f} s, {a['f']:.0f} Hz")
    for estado in ("vermelho", "laranja", "amarelo"):
        quantos = [a for a in sobra if a["estado"] == estado]
        if quantos:
            linhas.append(f"  fora do gabarito: {len(quantos)} {estado}")
        if estado != "amarelo":
            for a in quantos:
                linhas.append(f"     {estado} em {a['t_inicio']:.2f} s, {a['f']:.0f} Hz, {a['duracao']:.2f} s")
    falhas += [f"{nome}: falso vermelho em {a['t_inicio']:.2f} s, {a['f']:.0f} Hz" for a in sobra if a["estado"] == "vermelho"]
    return linhas, falhas


def conferir_ring_out(nome, info):
    """Mapa de risco: o ring-out vira o mapa, e o show vigia essas frequências com prioridade."""
    wav = SINAIS / info["arquivo"]
    ate = info["ring_out"]["ate"]
    base = SAIDA / (Path(info["arquivo"]).stem + "_mapa")
    subprocess.run([str(EXE), "--mic", f"{wav}:1", "--ring-out-ate", str(ate), "--saida", str(base), "--quieto"],
                   check=True, stdout=subprocess.DEVNULL)
    com = json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))
    sem = analisar(wav, False, SAIDA / (Path(info["arquivo"]).stem + "_sem_mapa"))
    linhas, falhas = [], []

    mapa = [r["f"] for r in com["mapa_de_risco"]["frequencias"]]
    linhas.append("  mapa: " + ", ".join(f"{f:.0f} Hz" for f in mapa))
    for f in info["ring_out"]["mapa"]:
        if not any(abs(m / f - 1) < 0.03 for m in mapa):
            falhas.append(f"{nome}: {f} Hz apitou no ring-out e não entrou no mapa")
    for m in mapa:
        if not any(abs(m / f - 1) < 0.03 for f in info["ring_out"]["mapa"]):
            falhas.append(f"{nome}: {m:.0f} Hz entrou no mapa sem ter apitado")

    def primeiro(alertas, ev):
        perto = [a for a in alertas if not a.get("ring_out") and abs(a["f"] / ev["f"] - 1) < 0.03
                 and a["t_inicio"] + a["duracao"] > ev["t_ini"] and a["t_inicio"] < ev["t_fim"] + 1]
        aviso = min((a["t_inicio"] for a in perto), default=None)
        verm = min((a["t_vermelho"] for a in perto if a["t_vermelho"] is not None), default=None)
        return perto, aviso, verm

    for ev in info["eventos"]:
        if ev["ring_out"]:
            continue
        perto, aviso, verm = primeiro(com["eventos"], ev)
        _, aviso_sem, verm_sem = primeiro(sem, ev)
        if verm is None or verm - ev["t_audivel"] > 0.5:
            falhas.append(f"{nome}: apito do show em {ev['f']} Hz sem vermelho a tempo")
        if not any(a.get("risco") for a in perto):
            falhas.append(f"{nome}: o alerta em {ev['f']} Hz não saiu marcado como do mapa")
        if aviso is None or (aviso_sem is not None and aviso > aviso_sem + 0.05):
            falhas.append(f"{nome}: com o mapa o primeiro aviso veio depois ({aviso} contra {aviso_sem} s)")
        def rel(t):
            return "nenhum" if t is None else f"{t - ev['t_audivel']:+.2f} s"
        linhas.append(f"  show {ev['f']:.0f} Hz: primeiro aviso {rel(aviso)} do audível com o mapa, {rel(aviso_sem)} sem; "
                      f"vermelho {rel(verm)} com, {rel(verm_sem)} sem")

    gabarito = [e for e in info["eventos"] if not e["ring_out"]]
    for a in com["eventos"]:
        if a["estado"] == "vermelho" and not a.get("ring_out") and not any(abs(a["f"] / e["f"] - 1) < 0.03 for e in gabarito):
            falhas.append(f"{nome}: falso vermelho no show em {a['t_inicio']:.2f} s, {a['f']:.0f} Hz")
            linhas.append(f"  fora do gabarito: vermelho em {a['t_inicio']:.2f} s, {a['f']:.0f} Hz")
    return linhas, falhas


def conferir_drift(nome, info):
    """Módulo 2: a sala e o mix mudaram o quanto o cenário diz, e cada mudança virou um evento só depois de acontecer."""
    g = info["drift"]
    a = g["analise"]
    wav = SINAIS / info["arquivo"]
    base = SAIDA / Path(info["arquivo"]).stem
    subprocess.run([str(EXE), "--mic", f"{wav}:{g['canal_mic']}", "--lr", f"{wav}:{g['canal_lr']}",
                    "--passagem-ate", str(g["passagem_ate"]), "--show-em", str(g["show_em"]), "--bloco", str(a["bloco"]),
                    "--janela-sala", str(a["janela_sala"]), "--janela-mix", str(a["janela_mix"]),
                    "--referencia", str(a["referencia"]), "--sustentacao", str(a["sustentacao"]),
                    "--saida", str(base), "--quieto"], check=True, stdout=subprocess.DEVNULL)
    saida = json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))
    d = saida["drift"]
    fim = d["curva"][-1]
    AGUDO, MEDIO_AGUDO = 5, 4
    linhas, falhas = [], []

    def checar(texto, valor, esperado, folga, unidade):
        ok = valor is not None and abs(valor - esperado) <= folga
        medido = "sem leitura" if valor is None else f"{valor:+.1f} {unidade}"
        linhas.append(f"  {texto:<34} {medido:>12}   (esperado {esperado:+.1f} ± {folga} {unidade}){'' if ok else '   <-- FORA'}")
        if not ok:
            falhas.append(f"{nome}: {texto}: {medido}, esperado {esperado:+.1f} ± {folga}")

    checar("sala, agudo", fim["sala"][AGUDO], g["queda_de_agudo_db"], 1.0, "dB")
    for r, regiao in enumerate(d["regioes"]):
        if r != AGUDO and fim["sala"][r] is not None:
            checar(f"sala, {regiao['nome']}", fim["sala"][r], 0.0, 1.0, "dB")
    checar("mix, médio-agudo", fim["mix"][MEDIO_AGUDO], g["realce_db"], 1.0, "dB")
    for r, regiao in enumerate(d["regioes"]):
        if r != MEDIO_AGUDO:
            checar(f"mix, {regiao['nome']}", fim["mix"][r], 0.0, 1.0, "dB")
    # o volume é medido da média do primeiro bloco até a média da última janela, então fica abaixo do total
    checar("volume", fim["volume"], g["volume_sobe_db"] - 0.7, 1.2, "dB")
    checar("atraso do PA", fim["delta_atraso_ms"], g["atraso_ms"][1] - g["atraso_ms"][0], 0.2, "ms")
    checar("piso entre músicas", fim["piso_a"], g["piso_a_db"], 1.5, "dB(A)")

    # cada mudança vira evento, e nunca antes de acontecer
    esperados = [("sala", AGUDO, g["sala_muda"][0]), ("mix", MEDIO_AGUDO, g["mix_muda"][0]), ("volume", None, g["show_em"]),
                 ("atraso", None, g["sala_muda"][0])]
    for tipo, regiao, desde in esperados:
        achados = [e for e in d["eventos"] if e["tipo"] == tipo and e["regiao"] == regiao]
        if not achados:
            falhas.append(f"{nome}: faltou o evento de {tipo}")
        elif achados[0]["t"] < desde + a["sustentacao"]:
            falhas.append(f"{nome}: evento de {tipo} em {achados[0]['t']} s, antes de se sustentar")
    conhecidos = {(t, r) for t, r, _ in esperados}
    for e in d["eventos"]:
        linhas.append(f"  {e['t']:5.0f} s  {e['texto']}")
        if (e["tipo"], e["regiao"]) not in conhecidos:
            falhas.append(f"{nome}: evento falso: {e['texto']}")
    falhas += [f"{nome}: falso vermelho de microfonia em {ev['t_inicio']:.1f} s" for ev in saida["eventos"] if ev["estado"] == "vermelho"]
    return linhas, falhas


def conferir_niveis(nome, info):
    """Medidor de nível: médio, máximo e mínimo em A, C e Z contra a conta feita no gerador."""
    base = SAIDA / Path(info["arquivo"]).stem
    subprocess.run([str(EXE), "--mic", str(SINAIS / info["arquivo"]), "--saida", str(base), "--quieto"], check=True, stdout=subprocess.DEVNULL)
    medido = json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))["niveis"]
    linhas, falhas = [], []
    for curva, esperado in info["niveis"].items():
        m = medido[curva]
        # ruído balança: a leitura rápida passa uns dB abaixo da média do trecho
        casos = [("médio", m["medio"], esperado["medio"], 0.3), ("máximo", m["maximo"], esperado["maximo"], 0.5),
                 ("mínimo", m["minimo"], esperado["minimo"] - (2.0 if esperado["minimo_no_ruido"] else 0.0),
                  2.5 if esperado["minimo_no_ruido"] else 0.5)]
        for rotulo, valor, alvo, folga in casos:
            ok = valor is not None and abs(valor - alvo) <= folga
            linhas.append(f"  {curva} {rotulo:<7} {valor:7.1f} dB   (esperado {alvo:6.1f} ± {folga}){'' if ok else '   <-- FORA'}")
            if not ok:
                falhas.append(f"{nome}: nível {curva} {rotulo}: {valor}, esperado {alvo:.1f} ± {folga}")
    return linhas, falhas


def conferir_leq(nome, info):
    """Leq deslizante: o maior Leq de 1, 5 e 15 min, em A e Z, contra a conta feita no gerador."""
    base = SAIDA / Path(info["arquivo"]).stem
    subprocess.run([str(EXE), "--mic", str(SINAIS / info["arquivo"]), "--saida", str(base), "--quieto"], check=True, stdout=subprocess.DEVNULL)
    medido = json.loads(base.with_suffix(".json").read_text(encoding="utf-8"))["niveis"]
    linhas, falhas = [], []
    for curva in "AZ":      # seno de 1 kHz: A e Z leem igual
        for j, (minutos, alvo) in enumerate(info["maior_leq"].items()):
            valor = medido[curva]["maior_leq"][j]
            if alvo is None:
                ok = valor is None
                linhas.append(f"  {curva} maior Leq {minutos:>2} min {'—' if valor is None else f'{valor:.1f}':>7}      (esperado —: o arquivo é mais curto){'' if ok else '   <-- FORA'}")
            else:
                ok = valor is not None and abs(valor - alvo) <= 0.3
                linhas.append(f"  {curva} maior Leq {minutos:>2} min {'—' if valor is None else f'{valor:.1f}':>7} dB   (esperado {alvo:6.1f} ± 0.3){'' if ok else '   <-- FORA'}")
            if not ok:
                falhas.append(f"{nome}: maior Leq {minutos} min em {curva}: {valor}, esperado {alvo}")
    return linhas, falhas


def main():
    if not EXE.exists():
        sys.exit(f"compile primeiro: não achei {EXE}")
    SAIDA.mkdir(parents=True, exist_ok=True)
    filtros = sys.argv[1:]
    falhas, conhecidas = [], []
    for js in sorted(SINAIS.glob("*.json")):
        if filtros and not any(js.stem.startswith(f) for f in filtros):
            continue
        info = json.loads(js.read_text(encoding="utf-8"))
        if info.get("ring_out"):
            nome = f"{js.stem} [só mic, com e sem mapa]"
            linhas, f = conferir_ring_out(nome, info)
            falhas += f
            print(nome)
            print("\n".join(linhas))
            continue
        if info.get("maior_leq"):
            nome = f"{js.stem} [só mic]"
            linhas, f = conferir_leq(nome, info)
            falhas += f
            print(nome)
            print("\n".join(linhas))
            continue
        if info.get("niveis"):
            nome = f"{js.stem} [só mic]"
            linhas, f = conferir_niveis(nome, info)
            falhas += f
            print(nome)
            print("\n".join(linhas))
            continue
        if info.get("drift"):
            nome = f"{js.stem} [mic e L/R]"
            linhas, f = conferir_drift(nome, info)
            falhas += f
            print(nome)
            print("\n".join(linhas))
            continue
        for com_linhas in (True, False):
            nome = f"{js.stem} [{'com linhas' if com_linhas else 'só mic'}]"
            alertas = analisar(SINAIS / info["arquivo"], com_linhas, SAIDA / f"{js.stem}_{'C' if com_linhas else 'A'}")
            linhas, f = conferir(nome, alertas, info)
            # Só com o mic, seno puro de teclado é indistinguível de apito. É limite conhecido do nível A.
            if not com_linhas and info.get("senos_na_linha"):
                conhecidas += f
            else:
                falhas += f
            print(nome)
            print("\n".join(linhas) if linhas else "  nenhum alerta")
    print()
    if conhecidas:
        print("Limite conhecido (só mic, seno puro na música):")
        print("\n".join("  " + f for f in conhecidas))
    if falhas:
        print("FALHAS:")
        print("\n".join("  " + f for f in falhas))
        return 1
    print("Tudo dentro dos critérios.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
