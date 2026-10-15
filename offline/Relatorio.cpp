#include "Relatorio.h"

#include <algorithm>
#include <cstdio>
#include <sstream>

using papagaio::Estado;
using papagaio::Evento;

namespace {

std::string escapar(const std::string& s) {
    std::string r;
    for (const char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': break;
            case '<': r += "\\u003c"; break;      // o JSON vai dentro de uma tag <script>
            default: r += c;
        }
    }
    return r;
}

std::string num(double v, int casas) {
    char t[32];
    std::snprintf(t, sizeof t, "%.*f", casas, v);
    return t;
}

std::string tempoOuNulo(double t) { return t < 0 ? "null" : num(t, 3); }

const char* kPagina1 = R"html(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Relatório pós-show</title>
<style>
:root { --fundo:#f6f5f2; --cartao:#ffffff; --texto:#1c1c1e; --suave:#6b6b70; --linha:#e2e0db;
        --vermelho:#d6342c; --laranja:#e07a12; --amarelo:#b8930a; --ok:#2e8b57; }
@media (prefers-color-scheme: dark) {
  :root { --fundo:#141416; --cartao:#1e1e21; --texto:#ececee; --suave:#9a9aa0; --linha:#313136;
          --vermelho:#f0554c; --laranja:#f59a3a; --amarelo:#e6c63c; --ok:#5fc48d; }
}
* { box-sizing: border-box; }
body { margin:0; background:var(--fundo); color:var(--texto); font:15px/1.45 system-ui, "Segoe UI", sans-serif; }
main { max-width:1080px; margin:0 auto; padding:28px 16px 60px; }
h1 { font-size:26px; margin:0 0 4px; letter-spacing:-0.01em; }
.sub { color:var(--suave); margin:0 0 20px; overflow-wrap:anywhere; }
.fichas { display:flex; flex-wrap:wrap; gap:10px; margin-bottom:18px; }
.ficha { background:var(--cartao); border:1px solid var(--linha); border-radius:10px; padding:10px 16px; min-width:120px; }
.ficha b { display:block; font-size:28px; line-height:1.1; font-variant-numeric:tabular-nums; }
.ficha span { color:var(--suave); font-size:13px; }
.vermelho { color:var(--vermelho); } .laranja { color:var(--laranja); } .amarelo { color:var(--amarelo); }
.cartao { background:var(--cartao); border:1px solid var(--linha); border-radius:10px; padding:14px; margin-bottom:18px; }
.cartao h2 { font-size:15px; margin:0 0 10px; }
svg { display:block; width:100%; height:auto; }
svg text { fill:var(--suave); font-size:11px; }
svg .grade { stroke:var(--linha); stroke-width:1; }
.filtros { display:flex; flex-wrap:wrap; gap:16px; margin-bottom:10px; color:var(--suave); font-size:14px; }
.rolagem { overflow-x:auto; }
table { width:100%; border-collapse:collapse; font-variant-numeric:tabular-nums; }
th { text-align:left; color:var(--suave); font-weight:600; font-size:12px; text-transform:uppercase; letter-spacing:0.04em; }
th, td { padding:8px 10px; border-bottom:1px solid var(--linha); white-space:nowrap; }
td.num, th.num { text-align:right; }
.selo { font-weight:700; text-transform:uppercase; font-size:12px; letter-spacing:0.04em; }
.rotulos button { font:inherit; font-size:13px; padding:3px 9px; margin-right:4px; border-radius:6px; cursor:pointer;
                  border:1px solid var(--linha); background:transparent; color:var(--suave); }
.rotulos button.ativo { color:var(--cartao); background:var(--texto); border-color:var(--texto); }
.rodape { display:flex; flex-wrap:wrap; gap:12px; align-items:center; justify-content:space-between; color:var(--suave); }
.rodape button { font:inherit; padding:8px 14px; border-radius:8px; border:1px solid var(--linha); background:var(--cartao); color:var(--texto); cursor:pointer; }
.vazio { color:var(--suave); padding:18px 0; }
:root { --serie1:#2a78d6; --serie2:#eb6834; }
@media (prefers-color-scheme: dark) { :root { --serie1:#3987e5; --serie2:#d95926; } }
.legenda { display:flex; flex-wrap:wrap; gap:18px; color:var(--suave); font-size:13px; margin:4px 0 12px; }
.legenda i { display:inline-block; vertical-align:middle; margin-right:6px; }
.traco { width:18px; height:2px; } .faixa { width:18px; height:10px; background:var(--linha); border-radius:2px; }
.s1 { background:var(--serie1); stroke:var(--serie1); } .s2 { background:var(--serie2); stroke:var(--serie2); }
.paineis { display:grid; grid-template-columns:repeat(auto-fit, minmax(280px, 1fr)); gap:14px; margin-bottom:20px; }
.quadroTopo { display:flex; flex-wrap:wrap; gap:8px; align-items:baseline; font-size:13px; color:var(--suave); }
.quadroTopo b { color:var(--texto); font-size:14px; }
.quadroFim { margin-left:auto; white-space:pre; font-variant-numeric:tabular-nums; }
.ponto { display:inline-block; width:8px; height:8px; border-radius:50%; margin-right:4px; }
svg .serie { fill:none; stroke-width:2; stroke-linejoin:round; stroke-linecap:round; background:none; }
svg .tolerancia { fill:var(--linha); opacity:0.55; }
svg .marco { stroke:var(--suave); stroke-width:1; stroke-dasharray:3 3; }
svg .mira { stroke:var(--suave); stroke-width:1; }
#dica { position:fixed; z-index:5; pointer-events:none; background:var(--texto); color:var(--cartao); padding:5px 9px;
        border-radius:6px; font-size:13px; font-variant-numeric:tabular-nums; }
td.quebra { white-space:normal; min-width:220px; } td.suave { color:var(--suave); }
details { margin-top:14px; } summary { cursor:pointer; color:var(--suave); margin-bottom:8px; }
</style>
</head>
<body>
<main>
  <h1>Relatório pós-show</h1>
  <p class="sub" id="sub"></p>
  <div class="fichas" id="fichas"></div>
  <div class="cartao"><h2>Linha do tempo</h2><div id="tempo"></div></div>
  <div class="cartao">
    <h2>Alertas</h2>
    <div class="filtros" id="filtros"></div>
    <div class="rolagem"><table>
      <thead><tr><th>Hora</th><th class="num">Frequência</th><th>Banda do GEQ</th><th>Alerta</th>
        <th class="num">Duração</th><th class="num">Nível máx.</th><th>Voltou</th><th>Foi isso mesmo?</th></tr></thead>
      <tbody id="linhas"></tbody>
    </table></div>
    <div class="vazio" id="vazio" hidden>Nenhum alerta com esses filtros.</div>
  </div>
  <div class="cartao" id="cartaoMapa" hidden>
    <h2>Mapa de risco do ring-out</h2>
    <p class="sub" id="subMapa"></p>
    <div class="rolagem"><table>
      <thead><tr><th class="num">Frequência</th><th>Banda do GEQ</th><th>Pior no ring-out</th><th class="num">Vezes</th>
        <th class="num">Alertas no show</th></tr></thead>
      <tbody id="linhasMapa"></tbody>
    </table></div>
  </div>
  <div class="cartao">
    <h2>Níveis no mic</h2>
    <p class="sub" id="subNiveis"></p>
    <div class="rolagem"><table>
      <thead><tr><th>Ponderação</th><th class="num">Médio (Leq)</th><th class="num">Máximo</th><th class="num">Mínimo</th>
        <th class="num">Maior Leq 1 min</th><th class="num">Maior Leq 5 min</th><th class="num">Maior Leq 15 min</th></tr></thead>
      <tbody id="linhasNiveis"></tbody>
    </table></div>
  </div>
  <div class="cartao" id="cartaoDrift" hidden>
    <h2>Drift entre passagem e show</h2>
    <p class="sub" id="subDrift"></p>
    <div class="fichas" id="fichasDrift"></div>
    <div class="legenda">
      <span><i class="traco s1"></i>sala: mudou desde a passagem de som</span>
      <span><i class="traco s2"></i>mix: mudou desde o primeiro bloco do show</span>
      <span><i class="faixa"></i>dentro da tolerância</span>
    </div>
    <div class="paineis" id="paineis"></div>
    <h2>Mudanças sustentadas</h2>
    <div class="rolagem"><table>
      <thead><tr><th>Hora</th><th>O que mudou</th><th>Leitura</th></tr></thead>
      <tbody id="linhasDrift"></tbody>
    </table></div>
    <div class="vazio" id="vazioDrift" hidden>Nenhuma mudança sustentada.</div>
    <details><summary>Valores no fim do show</summary>
      <div class="rolagem"><table>
        <thead><tr><th>Região</th><th>Faixa</th><th class="num">Sala</th><th class="num">Mix</th></tr></thead>
        <tbody id="valoresDrift"></tbody>
      </table></div>
    </details>
  </div>
  <div id="dica" hidden></div>
  <div class="rodape"><span id="placar"></span><button id="exportar">Exportar rótulos (JSON)</button></div>
</main>
<script>
const D = )html";

const char* kPagina2 = R"html(;
const ORDEM = ['vermelho', 'laranja', 'amarelo'];
const NOME = { vermelho: 'Vermelho', laranja: 'Laranja', amarelo: 'Amarelo' };
const nomeDoAlerta = e => e.pre_apito ? 'Pré-apito' : NOME[e.estado];
const ROTULOS = [['acertou', 'acertou'], ['errou', 'errou'], ['irrelevante', 'irrelevante']];
const chave = 'papagaio:' + D.mic + ':' + D.duracao;
let rotulos = {};
try { rotulos = JSON.parse(localStorage.getItem(chave) || '{}'); } catch (e) {}
const mostrar = { vermelho: true, laranja: true, amarelo: true };

const n = (v, casas) => v.toLocaleString('pt-BR', { minimumFractionDigits: casas, maximumFractionDigits: casas });
function hora(t) {
  const m = Math.floor(t / 60), s = t - 60 * m;
  return m + ':' + (s < 10 ? '0' : '') + s.toFixed(1).replace('.', ',');
}
function el(tag, attrs, texto) {
  const e = document.createElement(tag);
  for (const k in attrs || {}) e.setAttribute(k, attrs[k]);
  if (texto !== undefined) e.textContent = texto;
  return e;
}
function svg(tag, attrs, texto) {
  const e = document.createElementNS('http://www.w3.org/2000/svg', tag);
  for (const k in attrs || {}) e.setAttribute(k, attrs[k]);
  if (texto !== undefined) e.textContent = texto;
  return e;
}

document.getElementById('sub').textContent =
  D.mic + (D.linhas.length ? ' · gabarito de linhas: ' + D.linhas.join(', ') : ' · só mic, sem gabarito de linhas') +
  ' · ' + hora(D.duracao) + ' de áudio';

const fichas = document.getElementById('fichas');
for (const e of ORDEM) {
  const f = el('div', { class: 'ficha' });
  f.append(el('b', { class: e }, String(D.resumo[e])), el('span', {}, NOME[e].toLowerCase()));
  fichas.append(f);
}

function passoDoTempo(d) {
  for (const p of [5, 10, 30, 60, 120, 300, 600, 900, 1800]) if (d / p <= 10) return p;
  return 3600;
}
function desenharTempo() {
  const L = 1000, A = 250, x0 = 46, x1 = L - 10, y0 = 10, y1 = A - 26;
  const fa = Math.max(50, D.config.f_min), fb = D.config.f_max;
  const X = t => x0 + (x1 - x0) * t / Math.max(D.duracao, 1);
  const Y = f => y1 - (y1 - y0) * Math.log(Math.min(Math.max(f, fa), fb) / fa) / Math.log(fb / fa);
  const s = svg('svg', { viewBox: `0 0 ${L} ${A}`, role: 'img', 'aria-label': 'Alertas ao longo do show, por frequência' });
  for (const f of [100, 250, 500, 1000, 2000, 4000, 8000]) {
    if (f < fa || f > fb) continue;
    s.append(svg('line', { class: 'grade', x1: x0, x2: x1, y1: Y(f), y2: Y(f) }));
    s.append(svg('text', { x: x0 - 6, y: Y(f) + 4, 'text-anchor': 'end' }, f >= 1000 ? f / 1000 + 'k' : f));
  }
  const p = passoDoTempo(D.duracao);
  for (let t = 0; t <= D.duracao; t += p) {
    s.append(svg('line', { class: 'grade', x1: X(t), x2: X(t), y1: y0, y2: y1 }));
    s.append(svg('text', { x: X(t), y: A - 8, 'text-anchor': 'middle' }, Math.floor(t / 60) + ':' + String(t % 60).padStart(2, '0')));
  }
  for (const e of [...D.eventos].sort((a, b) => ORDEM.indexOf(b.estado) - ORDEM.indexOf(a.estado))) {
    if (!mostrar[e.estado]) continue;
    const w = Math.max(4, X(e.t_inicio + e.duracao) - X(e.t_inicio));
    const r = svg('rect', { x: X(e.t_inicio), y: Y(e.f) - 4, width: w, height: 8, rx: 3, fill: `var(--${e.estado})` });
    r.append(svg('title', {}, `${hora(e.t_inicio)} · ${n(e.f, 0)} Hz · ${nomeDoAlerta(e)}`));
    s.append(r);
  }
  document.getElementById('tempo').replaceChildren(s);
}

function desenharTabela() {
  const corpo = document.getElementById('linhas');
  corpo.replaceChildren();
  D.eventos.forEach((e, i) => {
    if (!mostrar[e.estado]) return;
    const tr = el('tr');
    tr.append(el('td', {}, hora(e.t_inicio)), el('td', { class: 'num' }, n(e.f, 0) + ' Hz'), el('td', {}, e.banda_geq));
    const alerta = el('td');
    alerta.append(el('span', { class: 'selo ' + e.estado }, nomeDoAlerta(e) + (e.vezes > 1 ? ' ×' + e.vezes : '')));
    if (e.ring_out || e.risco)
      alerta.append(el('div', { class: 'suave' }, e.ring_out ? 'no ring-out' : 'frequência do mapa de risco'));
    if (e.pre_apito)
      alerta.append(el('div', { class: 'suave' }, `anel caindo ${n(e.decai_min_db_s, 0)} dB/s (antes ${n(e.decai_antes_db_s, 0)})`));
    tr.append(alerta, el('td', { class: 'num' }, n(e.duracao, 1) + ' s'), el('td', { class: 'num' }, n(e.nivel_max_db + D.niveis.calibracao_db, 0) + ' ' + D.niveis.unidade),
      el('td', {}, e.voltou ? e.voltou + '×' : ''));
    const td = el('td', { class: 'rotulos' });
    for (const [valor, texto] of ROTULOS) {
      const b = el('button', { type: 'button', 'aria-pressed': rotulos[i] === valor }, texto);
      if (rotulos[i] === valor) b.className = 'ativo';
      b.onclick = () => {
        if (rotulos[i] === valor) delete rotulos[i]; else rotulos[i] = valor;
        try { localStorage.setItem(chave, JSON.stringify(rotulos)); } catch (err) {}
        desenharTabela();
      };
      td.append(b);
    }
    tr.append(td);
    corpo.append(tr);
  });
  document.getElementById('vazio').hidden = corpo.children.length > 0;
  const feitos = Object.keys(rotulos).length;
  const falsos = D.eventos.filter((e, i) => rotulos[i] === 'errou' && e.estado !== 'amarelo').length;
  document.getElementById('placar').textContent = feitos
    ? `${feitos} de ${D.eventos.length} rotulados · falsos alarmes (vermelho ou laranja marcado como errou): ${falsos}`
    : 'Marque cada alerta para medir o falso alarme por show.';
}

const filtros = document.getElementById('filtros');
for (const e of ORDEM) {
  const rot = el('label');
  const cx = el('input', { type: 'checkbox' });
  cx.checked = true;
  cx.onchange = () => { mostrar[e] = cx.checked; desenharTempo(); desenharTabela(); };
  rot.append(cx, ' ' + NOME[e].toLowerCase());
  filtros.append(rot);
}

document.getElementById('exportar').onclick = () => {
  const saida = { mic: D.mic, duracao: D.duracao, config: D.config,
    eventos: D.eventos.map((e, i) => Object.assign({}, e, { rotulo: rotulos[i] || null })) };
  const a = el('a', { href: URL.createObjectURL(new Blob([JSON.stringify(saida, null, 2)], { type: 'application/json' })),
    download: 'rotulos_papagaio.json' });
  a.click();
};

desenharTempo();
desenharTabela();
)html";

// Drift entre passagem e show: um quadro pequeno por região, com sala e mix lado a lado.
const char* kPagina3 = R"html(
function sinal(v, casas) { return (v < 0 ? '−' : '+') + n(Math.abs(v), casas); }
function minSeg(t) { return Math.floor(t / 60) + ':' + String(Math.floor(t % 60)).padStart(2, '0'); }

function desenharDrift() {
  const R = D.drift, P = R.curva;
  document.getElementById('cartaoDrift').hidden = false;
  document.getElementById('subDrift').textContent = 'L/R: ' + R.lr.join(', ') +
    (R.tem_base ? ' · passagem de som até ' + minSeg(R.passagem_ate) : ' · sem passagem de som: não há linha de base da sala') +
    ' · show a partir de ' + minSeg(R.show_em);

  let teto = 6;
  for (const p of P) for (const serie of [p.sala, p.mix]) for (const v of serie || []) if (v !== null) teto = Math.max(teto, Math.abs(v) + 1);
  teto = Math.min(15, Math.ceil(teto));
  const L = 320, A = 150, x0 = 30, x1 = L - 8, y0 = 8, y1 = A - 20;
  const fim = Math.max(D.duracao, 1);
  const X = t => x0 + (x1 - x0) * t / fim;
  const Y = v => (y0 + y1) / 2 - (y1 - y0) / 2 * Math.max(-teto, Math.min(teto, v)) / teto;
  const caminho = (r, chave) => {
    let d = '', solto = true;
    for (const p of P) {
      const v = p[chave] ? p[chave][r] : null;
      if (v === null) { solto = true; continue; }
      d += (solto ? 'M' : 'L') + X(p.t).toFixed(1) + ' ' + Y(v).toFixed(1);
      solto = false;
    }
    return d;
  };

  const dica = document.getElementById('dica'), miras = [];
  const paineis = document.getElementById('paineis');
  R.regioes.forEach((reg, r) => {
    const quadro = el('div', { class: 'quadro' });
    const topo = el('div', { class: 'quadroTopo' });
    topo.append(el('b', {}, reg.nome), el('span', {}, reg.faixa));
    const ultimo = P[P.length - 1] || {};
    const rotulo = el('span', { class: 'quadroFim' });
    for (const [chave, classe] of [['sala', 's1'], ['mix', 's2']]) {
      const v = ultimo[chave] ? ultimo[chave][r] : null;
      rotulo.append(el('i', { class: 'ponto ' + classe }), chave + ' ' + (v === null ? '—' : sinal(v, 1)) + '  ');
    }
    topo.append(rotulo);
    const s = svg('svg', { viewBox: `0 0 ${L} ${A}`, role: 'img', 'aria-label': `Drift de sala e de mix em ${reg.nome}` });
    s.append(svg('rect', { class: 'tolerancia', x: x0, y: Y(R.limiar_db), width: x1 - x0, height: Y(-R.limiar_db) - Y(R.limiar_db) }));
    for (const v of [teto, 0, -teto]) {
      s.append(svg('line', { class: 'grade', x1: x0, x2: x1, y1: Y(v), y2: Y(v) }));
      s.append(svg('text', { x: x0 - 5, y: Y(v) + 4, 'text-anchor': 'end' }, v === 0 ? '0' : sinal(v, 0)));
    }
    if (R.show_em > 0) s.append(svg('line', { class: 'marco', x1: X(R.show_em), x2: X(R.show_em), y1: y0, y2: y1 }));
    s.append(svg('text', { x: x0, y: A - 5 }, '0:00'), svg('text', { x: x1, y: A - 5, 'text-anchor': 'end' }, minSeg(fim)));
    s.append(svg('path', { class: 'serie s1', d: caminho(r, 'sala') }), svg('path', { class: 'serie s2', d: caminho(r, 'mix') }));
    const mira = svg('line', { class: 'mira', y1: y0, y2: y1, visibility: 'hidden' });
    miras.push(mira);
    s.append(mira);
    s.onmousemove = ev => {
      const caixa = s.getBoundingClientRect();
      const t = ((ev.clientX - caixa.left) / caixa.width * L - x0) / (x1 - x0) * fim;
      let p = P[0];
      for (const q of P) if (Math.abs(q.t - t) < Math.abs(p.t - t)) p = q;
      if (!p) return;
      for (const m of miras) { m.setAttribute('x1', X(p.t)); m.setAttribute('x2', X(p.t)); m.setAttribute('visibility', 'visible'); }
      const sala = p.sala ? p.sala[r] : null, mix = p.mix ? p.mix[r] : null;
      dica.textContent = `${minSeg(p.t)} · ${reg.nome} · sala ${sala === null ? '—' : sinal(sala, 1) + ' dB'} · mix ${mix === null ? '—' : sinal(mix, 1) + ' dB'}`;
      dica.hidden = false;
      dica.style.left = Math.min(window.innerWidth - dica.offsetWidth - 12, ev.clientX + 14) + 'px';
      dica.style.top = (ev.clientY + 16) + 'px';
    };
    s.onmouseleave = () => { dica.hidden = true; for (const m of miras) m.setAttribute('visibility', 'hidden'); };
    quadro.append(topo, s);
    paineis.append(quadro);
  });

  const u = P[P.length - 1];
  const fichas = document.getElementById('fichasDrift');
  const ficha = (valor, nome) => { const f = el('div', { class: 'ficha' }); f.append(el('b', {}, valor), el('span', {}, nome)); fichas.append(f); };
  if (u) {
    ficha(u.volume === null ? '—' : sinal(u.volume, 1) + ' dB', 'volume desde o primeiro bloco');
    ficha(u.delta_atraso_ms === null ? '—' : sinal(u.delta_atraso_ms, 1) + ' ms', 'atraso do PA desde a passagem' + (u.atraso_ms === null ? '' : ' (agora ' + n(u.atraso_ms, 1) + ' ms)'));
    ficha(u.do_pa === null ? '—' : n(100 * u.do_pa, 0) + ' %', 'do que chega no FOH vem do PA');
  }

  const corpo = document.getElementById('linhasDrift');
  for (const e of R.eventos) {
    const tr = el('tr');
    tr.append(el('td', {}, minSeg(e.t)), el('td', { class: 'quebra' }, e.texto), el('td', { class: 'quebra suave' }, e.leitura));
    corpo.append(tr);
  }
  document.getElementById('vazioDrift').hidden = R.eventos.length > 0;

  const valores = document.getElementById('valoresDrift');
  R.regioes.forEach((reg, r) => {
    const tr = el('tr');
    const v = chave => (u && u[chave] && u[chave][r] !== null) ? sinal(u[chave][r], 1) + ' dB' : '—';
    tr.append(el('td', {}, reg.nome), el('td', {}, reg.faixa), el('td', { class: 'num' }, v('sala')), el('td', { class: 'num' }, v('mix')));
    valores.append(tr);
  });
}
if (D.drift) desenharDrift();

(function desenharMapa() {
  const M = D.mapa_de_risco;
  if (!M) return;
  document.getElementById('cartaoMapa').hidden = false;
  document.getElementById('subMapa').textContent = (M.ring_out_ate > 0
    ? 'Gravado no ring-out, de 0:00 até ' + minSeg(M.ring_out_ate)
    : 'Mapa dado de antemão') + ' · no show, essas frequências são vigiadas com prioridade (±2 %)';
  const corpo = document.getElementById('linhasMapa');
  for (const r of M.frequencias) {
    const show = D.eventos.filter(e => !e.ring_out && Math.abs(e.f / r.f - 1) < 0.02).length;
    const tr = el('tr');
    const pior = el('td');
    if (r.manual) pior.append('posta à mão');
    else pior.append(el('span', { class: 'selo ' + r.pior }, r.pior === 'amarelo' ? 'Pré-apito' : NOME[r.pior]));
    tr.append(el('td', { class: 'num' }, n(r.f, 0) + ' Hz'), el('td', {}, r.banda_geq), pior,
      el('td', { class: 'num' }, r.manual ? '' : String(r.vezes)), el('td', { class: 'num' }, String(show)));
    corpo.append(tr);
  }
  if (!M.frequencias.length) corpo.append(el('tr', {}, el('td', { colspan: 5, class: 'suave' }, 'Nenhuma frequência entrou no mapa.')));
})();

(function desenharNiveis() {
  const N = D.niveis;
  document.getElementById('subNiveis').textContent = (N.calibrado
    ? 'Em dB SPL, com 0 dB do arquivo valendo ' + n(N.calibracao_db, 1) + ' dB SPL'
    : 'Em dBFS: sem calibração do mic, só as diferenças valem') +
    ' · máximo e mínimo com ponderação de tempo ' + N.tempo +
    ' · maior Leq: o pior trecho daquela duração no show (— se o arquivo é mais curto que a janela)';
  const corpo = document.getElementById('linhasNiveis');
  for (const curva of ['A', 'C', 'Z']) {
    const tr = el('tr');
    const v = x => x === null ? '—' : n(x, 1) + ' ' + N.unidade;
    tr.append(el('td', {}, curva), el('td', { class: 'num' }, v(N[curva].medio)), el('td', { class: 'num' }, v(N[curva].maximo)),
      el('td', { class: 'num' }, v(N[curva].minimo)), ...N[curva].maior_leq.map(x => el('td', { class: 'num' }, v(x))));
    corpo.append(tr);
  }
})();
</script>
</body>
</html>
)html";

void escreverDrift(std::ostringstream& s, const DadosDoRelatorio& d) {
    using papagaio::EventoDeDrift;
    using papagaio::kNumRegioes;
    const auto ou = [](bool tem, double v, int casas) { return tem ? num(v, casas) : std::string("null"); };
    const bool temBase = !d.curva.empty() && d.curva.back().estado.temBase;

    s << "{\n    \"lr\": [";
    for (size_t i = 0; i < d.lr.size(); ++i) s << (i ? ", " : "") << '"' << escapar(d.lr[i]) << '"';
    s << "],\n    \"passagem_ate\": " << num(d.passagemAte, 1) << ", \"show_em\": " << num(d.showEm, 1)
      << ", \"tem_base\": " << (temBase ? "true" : "false") << ", \"limiar_db\": " << num(d.configDrift.limiarDb, 1) << ",\n";
    s << "    \"regioes\": [";
    for (int r = 0; r < kNumRegioes; ++r)
        s << (r ? ", " : "") << "{\"nome\": \"" << papagaio::nomeDaRegiao(r) << "\", \"faixa\": \"" << papagaio::faixaDaRegiao(r) << "\"}";
    s << "],\n    \"eventos\": [";
    for (size_t i = 0; i < d.drift.size(); ++i) {
        const EventoDeDrift& e = d.drift[i];
        const char* tipo = e.tipo == EventoDeDrift::Tipo::sala ? "sala" : e.tipo == EventoDeDrift::Tipo::mix ? "mix"
                           : e.tipo == EventoDeDrift::Tipo::volume ? "volume" : "atraso";
        s << (i ? "," : "") << "\n      {\"t\": " << num(e.tempo, 1) << ", \"desde\": " << num(e.desde, 1) << ", \"tipo\": \"" << tipo
          << "\", \"regiao\": " << (e.regiao >= 0 ? std::to_string(e.regiao) : "null") << ", \"valor\": " << num(e.valor, 2)
          << ", \"texto\": \"" << escapar(papagaio::descrever(e)) << "\", \"leitura\": \"" << escapar(papagaio::sugerir(e)) << "\"}";
    }
    s << (d.drift.empty() ? "" : "\n    ") << "],\n    \"curva\": [";
    for (size_t i = 0; i < d.curva.size(); ++i) {
        const auto& e = d.curva[i].estado;
        s << (i ? "," : "") << "\n      {\"t\": " << num(d.curva[i].tempo, 1) << ", \"sala\": ";
        if (e.temBase && !e.gravandoPassagem) {
            s << "[";
            for (size_t r = 0; r < kNumRegioes; ++r) s << (r ? ", " : "") << ou(e.salaValida[r], e.salaDb[r], 2);
            s << "]";
        } else {
            s << "null";
        }
        s << ", \"mix\": ";
        if (e.mixValido) {
            s << "[";
            for (size_t r = 0; r < kNumRegioes; ++r) s << (r ? ", " : "") << num(e.mixDb[r], 2);
            s << "]";
        } else {
            s << "null";
        }
        s << ", \"volume\": " << ou(e.mixValido, e.volumeDb, 2) << ", \"atraso_ms\": " << ou(e.temAtraso, e.atrasoMs, 2)
          << ", \"delta_atraso_ms\": " << ou(e.temAtraso && e.temAtrasoBase && !e.gravandoPassagem, e.deltaAtrasoMs, 2)
          << ", \"mic_a\": " << ou(e.temNiveis, e.nivelMicADb, 1) << ", \"ref_a\": " << ou(e.temNiveis, e.nivelRefADb, 1)
          << ", \"fora_a\": " << ou(e.temNiveis, e.foraDoPaADb, 1) << ", \"do_pa\": " << ou(e.temNiveis, e.coerenciaMedia, 3)
          << ", \"piso_a\": " << ou(e.temPiso, e.pisoADb, 1) << "}";
    }
    s << (d.curva.empty() ? "" : "\n    ") << "]\n  }";
}

} // namespace

std::string montarJson(const DadosDoRelatorio& d) {
    int cont[4] = {};
    for (const auto& e : d.eventos) ++cont[(int) e.estado];

    std::ostringstream s;
    s << "{\n  \"versao\": \"0.1\",\n  \"mic\": \"" << escapar(d.mic) << "\",\n  \"linhas\": [";
    for (size_t i = 0; i < d.linhas.size(); ++i) s << (i ? ", " : "") << '"' << escapar(d.linhas[i]) << '"';
    s << "],\n  \"taxa\": " << num(d.taxa, 0) << ",\n  \"duracao\": " << num(d.duracao, 3) << ",\n";
    s << "  \"config\": {\"pnpr_db\": " << num(d.config.pnprDb, 1) << ", \"phpr_db\": " << num(d.config.phprDb, 1)
      << ", \"crescimento_db_s\": " << num(d.config.crescimentoDbPorS, 1) << ", \"piso_db\": " << num(d.config.pisoDb, 1)
      << ", \"f_min\": " << num(d.config.fMin, 0) << ", \"f_max\": " << num(d.config.fMax, 0)
      << ", \"usar_linhas\": " << (d.config.usarLinhas ? "true" : "false") << ", \"fft\": " << d.config.tamanhoFft << "},\n";
    s << "  \"resumo\": {\"vermelho\": " << cont[3] << ", \"laranja\": " << cont[2] << ", \"amarelo\": " << cont[1] << "},\n";
    s << "  \"eventos\": [";
    for (size_t i = 0; i < d.eventos.size(); ++i) {
        const Evento& e = d.eventos[i];
        s << (i ? "," : "") << "\n    {\"t_inicio\": " << num(e.tInicio, 3) << ", \"t_laranja\": " << tempoOuNulo(e.tLaranja)
          << ", \"t_vermelho\": " << tempoOuNulo(e.tVermelho) << ", \"duracao\": " << num(e.duracao, 3)
          << ", \"f\": " << num(e.f, 1) << ", \"banda_geq\": \"" << papagaio::bandaGeq(e.f) << "\", \"nivel_max_db\": "
          << num(e.nivelMaxDb, 1) << ", \"estado\": \"" << papagaio::nomeEstado(e.estado) << "\", \"voltou\": " << e.voltou << ", \"vezes\": " << e.vezes;
        if (e.ringOut) s << ", \"ring_out\": true";
        if (e.risco) s << ", \"risco\": true";
        if (e.preApito)
            s << ", \"pre_apito\": true, \"decai_antes_db_s\": " << num(e.decaiAntesDbPorS, 1) << ", \"decai_min_db_s\": "
              << num(e.decaiMinDbPorS, 1) << ", \"ataques\": " << e.ataques;
        s << "}";
    }
    s << (d.eventos.empty() ? "" : "\n  ") << "],\n  \"mapa_de_risco\": ";
    if (d.temMapa) {
        s << "{\"ring_out_ate\": " << num(d.ringOutAte, 1) << ", \"frequencias\": [";
        const auto& itens = d.mapa.itens();
        for (size_t i = 0; i < itens.size(); ++i)
            s << (i ? ", " : "") << "{\"f\": " << num(itens[i].f, 1) << ", \"banda_geq\": \"" << papagaio::bandaGeq(itens[i].f)
              << "\", \"vezes\": " << itens[i].vezes << ", \"pior\": \"" << papagaio::nomeEstado((papagaio::Estado) itens[i].pior)
              << "\", \"nivel_max_db\": " << num(itens[i].nivelMaxDb, 1) << ", \"manual\": " << (itens[i].manual ? "true" : "false") << "}";
        s << "]}";
    } else {
        s << "null";
    }
    s << ",\n  \"niveis\": {\"unidade\": \"" << (d.temCalibracao ? "dB SPL" : "dBFS")
      << "\", \"calibrado\": " << (d.temCalibracao ? "true" : "false") << ", \"calibracao_db\": " << num(d.calibracaoDb, 1)
      << ", \"tempo\": \"" << (d.nivelLento ? "lenta" : "rápida") << "\"";
    {
        const char* curvas[] = { "A", "C", "Z" };
        const auto valor = [&](float v) { return v <= -190.0f ? std::string("null") : num(v + d.calibracaoDb, 1); };
        for (size_t w = 0; w < d.niveis.size(); ++w)
            s << ", \"" << curvas[w] << "\": {\"medio\": " << valor(d.niveis[w].medio) << ", \"maximo\": " << valor(d.niveis[w].maximo)
              << ", \"minimo\": " << valor(d.niveis[w].minimo) << ", \"maior_leq\": [" << valor(d.niveis[w].maiorLeqJanela[0]) << ", "
              << valor(d.niveis[w].maiorLeqJanela[1]) << ", " << valor(d.niveis[w].maiorLeqJanela[2]) << "]}";
    }
    s << "},\n  \"drift\": ";
    if (d.temDrift)
        escreverDrift(s, d);
    else
        s << "null";
    s << "\n}\n";
    return s.str();
}

std::string montarHtml(const std::string& json) {
    return std::string(kPagina1) + json + kPagina2 + kPagina3;
}
