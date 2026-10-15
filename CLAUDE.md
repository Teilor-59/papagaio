# Papagaio

Monitor inteligente para som ao vivo: um segundo par de ouvidos para o engenheiro de FOH, que avisa de microfonia antes de virar problema. "Papagaio" é codinome; o nome do produto está pendente.

A fonte da verdade do produto é `Proposta monitor inteligente para som ao vivo.docx` (visão, escopo do MVP, módulos, hierarquia de alertas, decisões tomadas). Leia antes de mexer em comportamento.

## O que é e o que não é

- É um **app desktop independente** em JUCE (Windows e macOS). Não é plugin, não roda dentro de DAW.
- Lê só áudio: mic de medição como base, L/R e multipista opcionais. Não fala com a mesa.
- Mostra dado e "provável", nunca certeza. Nenhuma ação automática.
- Visual: vidro sobre fundo azul-noite, com a paleta da identidade visual em `cores::` (`app/Visual.h`) e o logotipo de `Identidade Visual/Logotipo.png`, que vai embutido no executável. Todo painel pinta o fundo com `pintarVidro()`; botões e caixas de escolha saem de `Aparencia`. Não escreva cor solta no código: use `cores::`. O ícone do executável é `Identidade Visual/Icone.png` (o papagaio recortado do logotipo); se o logotipo mudar, gere de novo com `Papagaio.exe --exportar-icone "Identidade Visual/Icone.png"` e recompile.
- Interface, identificadores e comentários em português. Texto com acento em código JUCE passa por `txt("...")` (literais são UTF-8); número para a tela passa por `numero()` (vírgula decimal).

## Pastas

- `motor/` — motor de análise em C++17 puro, sem JUCE. Tudo que é DSP mora aqui, para rodar igual no app e offline.
  - `DetectorMicrofonia`: módulo 1, apito e pré-apito. O pré-apito mede a queda (dB/s) de cada pico estreito, sem harmônicos e parado em frequência depois de cada ataque, e avisa quando a mediana das três últimas quedas fica abaixo de `anelLongoDbPorS` e 1,6 vez mais lenta que as de antes. Amarelos comuns da mesma frequência com menos de 10 s entre eles viram um registro só (`vezes`).
  - `MapaDeRisco`: as frequências do ring-out. O detector grava nele (`iniciarRingOut`/`encerrarRingOut`) cada laranja, vermelho e pré-apito, juntando a ±3 %, e no show vigia ±2 % de cada uma com prioridade: pico menos exigente, amarelo e laranja mais cedo, pré-apito com anel menos longo. O vermelho não afrouxa: exige o pico suspeito pelo critério de sempre. O mapa sobrevive à troca de fonte (`definirMapa`) e o app o guarda nas preferências.
  - `AnalisadorDuplo`: medição mic × L/R (atraso, ganho e coerência por banda de 1/3 de oitava, piso com a mesa em silêncio). Fecha um bloco a cada 10 s.
  - `MonitorDeDrift`: módulo 2. Recebe os blocos, compara a sala com a passagem de som e o mix com o primeiro bloco do show, por região (`Bandas.h`), e só gera evento para mudança sustentada.
  - `MedidorDeNivel`: níveis do mic em A, C e Z (agora, Leq, máximo, mínimo), tempo rápido ou lento, e Leq deslizante de 1, 5 e 15 min com o maior valor de cada janela. A ponderação é aplicada no espectro.
  - `AnalisadorDeEspectro`: espectro em 1/48 de oitava, de 20 Hz a 20 kHz. É a base do RTA e do espectrograma.
  - `IgualAudibilidade`: curvas de phon da ISO 226:2003. Conferência: `papagaio-offline --igual-audibilidade 40` tem que dar 99,85 dB em 20 Hz, 64,37 em 100 Hz e 40,01 em 1 kHz.
- `offline/` — `papagaio-offline`: analisa WAV gravado e gera o relatório pós-show (HTML + JSON). É a v0 da proposta.
- `app/` — o app JUCE. `MotorAoVivo` liga a placa (ou um WAV, em simulação) a uma thread de análise; `TelaPrincipal` tem a barra de cima (fonte e canais) e a `Mesa`. A `Mesa` é a área onde ficam os painéis, numa grade de 48 × 36 células que acompanha a janela; cada painel é um `Bloco` (`Blocos.cpp`): faixa de alerta, vista, níveis, sala e mix, histórico e indicador. O usuário move, redimensiona, acrescenta e tira blocos pelo botão Layout, e o arranjo fica guardado como XML nas preferências. `Vista` desenha as vistas (espectro, RTA, espectrograma, transferência); `Componentes` tem os desenhos da faixa de alerta e dos painéis de níveis e de sala e mix, que os blocos embrulham.

Para criar um painel novo: uma classe derivada de `Bloco` em `Blocos.cpp`, uma linha em `tiposDeBloco()` e outra em `criarBloco()`. Para criar um valor novo de indicador: uma linha em `catalogo()` e um caso em `ler()`.
- `tools/` — `gerar_sinais_teste.py` (cenários sintéticos com gabarito: 01 a 04 para microfonia, com malha de realimentação simulada; 05 para drift, com sala e mix mudando; 06 para níveis, com tons e ruído; 07 para o Leq deslizante, seno em degraus por 6 min; 08 para o pré-apito, voz em frases com o ganho de malha subindo em degraus até apitar; 09 para o mapa de risco, ring-out e depois show em que a malha volta numa frequência do mapa) e `conferir_detector.py` (roda o offline e confere os critérios de tudo).

Convenção de nível em todo o motor: um seno em fundo de escala lê 0 dB (ou seja, 3 dB acima do dBFS RMS). O motor nunca sabe de SPL.

Nível na tela é em **dB SPL**: o uso é som ao vivo, não áudio digital. A conversão é uma soma só, a calibração do mic (`Escala` em `app/Visual.h`), e todo nível mostrado passa por ela: painel de níveis, eixos das vistas, espectrograma, faixa de alerta, histórico e piso. A calibração vem da janela `DialogoDeCalibracao` (calibrador de 94 ou 114 dB, igualar a um decibelímetro, conta pela ficha técnica do mic e da placa, ou valor digitado) e é guardada por placa e canal. Sem calibração o app mostra dBFS, avisa em amarelo e abre a janela de calibração sozinho, uma vez por sessão, assim que chega sinal. Não existe SPL sem essa referência: não invente um valor padrão para "parecer" SPL. Ao criar qualquer leitura de nível nova, use `Escala`; não escreva "dBFS" direto.
- `externos/JUCE` — JUCE 9.0.3, clone raso do GitHub oficial. Não editar.
- `testes/sinais`, `testes/saida`, `build/` — gerados.

## Compilar e testar

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -- -m
python tools/gerar_sinais_teste.py      # só precisa de numpy; se o OpenBLAS reclamar de memória, OPENBLAS_NUM_THREADS=1
python tools/conferir_detector.py       # tem que terminar em "Tudo dentro dos critérios."
```

O app fica em `build/app/Papagaio_artefacts/Release/Papagaio.exe`. Para mandar a alguém, é só esse arquivo: o runtime do C++ vai embutido (`CMAKE_MSVC_RUNTIME_LIBRARY` no `CMakeLists.txt`), então roda em Windows 10/11 64 bits sem o Visual C++ Redistributable. Para ver a tela sem placa nem monitor:

```bash
build/app/Papagaio_artefacts/Release/Papagaio.exe --arquivo testes/sinais/01_show_tres_apitos.wav --linhas 2 --captura testes/saida/tela.png --apos 14.3
```

Isso analisa o WAV acelerado até 14,3 s, grava a tela em PNG e sai. Use depois de qualquer mudança visual. Para o painel de drift:

```bash
build/app/Papagaio_artefacts/Release/Papagaio.exe --arquivo testes/sinais/05_casa_enche.wav --lr 2 --passagem-ate 40 --janelas-curtas --captura testes/saida/tela_drift.png --apos 236
```

`--janelas-curtas` troca as janelas do drift de minutos por segundos; só serve para os sinais de teste. `--vistas rta,espectrograma` e `--fracao 48` escolhem as vistas da captura (`espectro`, `rta`, `espectrograma`, `transferencia`); a captura não lê nem grava as preferências do usuário. `--layout testes/layout_exemplo.xml` carrega um layout inteiro e `--editando` fotografa o modo de edição. `--calibracao 124` põe a captura em dB SPL e `--janela-de-calibracao` fotografa a janela de calibração no lugar da tela principal. `--rever 14` leva o espectrograma para os 14 s do arquivo antes da foto, para conferir o modo de revisão. `--referencia "3|1|63:100;1000:96;8000:86"` desenha uma curva de referência no RTA (largura de banda | visível | frequência:nível). `--vistas equilibrio --equilibrio "98;67;75;..."` mostra a vista de equilíbrio com uma referência (nível geral e 29 bandas, em dB SPL). Para o pré-apito: `--arquivo testes/sinais/08_pre_apito.wav --linhas 2 --vistas espectrograma --apos 50.5`. Para o mapa de risco: `--arquivo testes/sinais/09_ring_out.wav --linhas 2 --ring-out --apos 18.9` fotografa o ring-out gravando; `--mapa "2500;1250"` começa com esse mapa (aceita também o formato de `MapaDeRisco::emTexto`). No offline, `--ring-out-ate 24` trata o começo do arquivo como ring-out e `--mapa` dá o mapa de antemão. `testes/layout_leq_pico.xml` (com `05_casa_enche.wav --lr 2 --calibracao 124 --apos 150`) mostra o Leq deslizante no painel de níveis e nos indicadores e o RTA com pico segurado.

Ao editar arquivo com acento por script, grave o script num arquivo e rode com `python -X utf8`. Script passado por `python - <<EOF` no Git Bash já chegou com os acentos trocados.

## Estado (outubro de 2026)

Feito:
- Módulo 1 (microfonia) nos níveis A (só mic) e com um canal de linhas como gabarito.
- Mapa de risco do ring-out: bloco "Mapa de risco" (começar e encerrar ring-out, acrescentar à mão, tirar uma, limpar), traço laranja nas vistas, "frequência do ring-out" na faixa e "ring-out"/"do mapa" no histórico e no relatório. Alertas do ring-out não contam como "voltou". No sinal 09 o primeiro aviso do apito do show chega 12,7 s antes de ele ficar audível com o mapa, contra 0,15 s sem. Para o mapa sair limpo, o detector passou a tratar pico em múltiplo inteiro (até 8×) de uma trilha em laranja ou vermelho como distorção dela, e o vermelho por crescimento passou a exigir o nível a 1 dB do máximo da trilha e crescimento também no dobro da janela.
- Pré-apito por decaimento: anel que alonga a cada ataque vira amarelo de pré-apito, com a queda de antes e a de agora (faixa de alerta, histórico e relatório). No sinal de teste sai 15 s antes de a malha cruzar 1, sem nenhum falso nos outros cenários.
- Módulo 2 (drift) no nível B: drift da sala contra a passagem, drift do mix e volume contra o primeiro bloco, atraso do PA (termômetro acústico), fração do som que vem do PA e piso entre músicas. Os eventos vão para o histórico e nunca interrompem.
- Níveis em A, C e Z (agora, médio, máximo, mínimo), em dB SPL depois de calibrar o mic.
- Leq deslizante de 1, 5 e 15 min (anel de energia por segundo no `MedidorDeNivel`; a janela conta só segundos fechados). O painel de níveis mostra o Leq(A) das três janelas quando tem altura, com a que ainda está enchendo um tom abaixo. O indicador oferece Leq e maior Leq de cada janela em A, C e Z (`nivel.A.leq5`, `nivel.A.maiorleq15`...). O relatório offline traz o maior Leq de cada janela.
- Retenção de pico no RTA, por vista: sem pico, segurado até zerar, ou 3 s e 10 s (segura e depois cai a 10 dB/s). Conta no tempo da fonte, zera ao trocar a largura de banda, e fica no layout (`pico=` na config da vista; -1 é segurado).
- Vistas selecionáveis, duas ao mesmo tempo: espectro, RTA de 1 a 1/48 de oitava com média, espectrograma e transferência mic ÷ L/R em 1/3 de oitava com coerência.
- Revisão do espectrograma: o motor guarda até 3 horas (pedaços alocados conforme o show anda, uns 40 MB por hora); a vista volta no tempo com a roda do mouse, arrastando ou pelos botões, mostra frequência, hora e nível debaixo do cursor, e o botão "Tempo real" volta ao agora. Clicar numa linha do histórico abre o espectrograma naquele momento.
- Curva de referência no RTA (`CurvaDeReferencia`): pontos criados e apagados com clique duplo, arrastados com o mouse, Shift + arraste move a linha toda; pode nascer do RTA de agora ou de uma reta. Fica guardada na unidade da tela (dB SPL) e acompanha a troca de largura de banda.
- Layout livre: painéis móveis e redimensionáveis sobre uma grade, guardados entre sessões.
- Painéis soltos: no modo de edição, "Soltar" tira o bloco da mesa e abre numa janela do sistema (`Mesa::Janela`), para outra tela. Fechar a janela devolve o bloco à mesa. Posição e tamanho da janela ficam no layout. Na captura, cada janela solta sai em `<nome>_janela1.png`, `_janela2.png`...
- Indicadores avulsos: um número grande, com o valor escolhido num menu (níveis A/C/Z agora, médio, máximo e mínimo; frequência suspeita; volume, atraso, quanto vem do PA, piso; drift de sala e de mix por região; relógio) e limite opcional que pinta o número de amarelo e vermelho.
- Vista "Equilíbrio pelo volume (phon)": por banda de 1/3 de oitava, quanto cada região soa mais ou menos presente do que no momento marcado como referência. Barras: só o efeito do volume, pela diferença de forma entre a curva de phon do nível de referência e a do nível de agora (o phon é o nível geral em dB(Z), com média de 3 s). Linha: isso mais o que o espectro mudou de fato. Precisa do mic calibrado.
- Ferramenta offline com relatório (alertas, níveis, drift, rotulagem de acerto).
- App com entrada ao vivo, simulação por arquivo, faixa de alerta, vistas, painéis de níveis e de sala e mix, e histórico.

Validado só com sinal sintético. Os limiares em `DetectorMicrofonia.cpp` e em `ConfigDrift` são hipóteses; quem calibra é show gravado.

Não feito ainda: mapa de risco por mix (precisa dos buses de monitor no multipista; hoje é um mapa só, do mic), origem por canal e mix (nível C, multipista), o resto do módulo 3 (reverberação por EDT/T20/C80, equilíbrio tonal contra referência escolhida), transferência em banda estreita com fase e resposta ao impulso, várias curvas de referência com nome e a diferença entre o RTA e a referência, vários layouts com nome (hoje é um só), blocos que não se sobreponham sozinhos (hoje podem ficar um em cima do outro), zoom de tempo no espectrograma (hoje a janela é fixa em 22 s), revisão nas outras vistas, histórico do espectrograma salvo em disco (hoje se perde ao fechar o app ou trocar de placa), detecção automática do L/R, perfis salvos (a linha de base se perde ao fechar o app ou trocar de placa), rotulagem dentro do app, build de macOS.

Limites conhecidos: só com o mic, seno puro na música (teclado, playback) parece apito. Ressonância que não dispara e cai em cima de nota cantada passa despercebida; a saída é a função de transferência do nível B. O pré-apito só enxerga o anel quando ele sobra sozinho depois de um ataque (fim de frase, pausa, ataque curto): com a banda inteira tocando notas sustentadas na mesma região, um anel de ganho 0,98 tem meio hertz de largura e some na música. No mapa de risco, só com o mic, uma nota pura de teclado que caia numa frequência do mapa pode virar laranja (no sinal 02, com um mapa posto sobre as notas, 878 Hz virou laranja); com as linhas, não. Com voz solo, o amarelo comum ainda dispara em harmônicos sem vibrato (uns 30 registros por minuto no sinal 08, já juntados por frequência). No drift, banda sem programa ou com coerência abaixo de 0,35 fica sem leitura, e a calibração vale só para o ganho de pré em que foi feita: mexeu no ganho, o SPL fica errado até calibrar de novo, e o app não tem como perceber. No RTA, abaixo de uns 140 Hz as bandas de 1/48 e 1/24 são mais estreitas que a FFT e saem em degraus. O espectrograma usa um mapa de cor de um caminho só (escuro para claro), não o arco-íris do Smaart. Na vista de equilíbrio, as curvas de phon são de tom puro e a norma só vai até 90 phon e 12,5 kHz; show ao vivo passa disso, e aí o valor é extrapolação.

## Licenças

JUCE é AGPLv3 ou licença comercial. `JUCE_ASIO=1` usa o cabeçalho ASIO que vem com o JUCE, sob a licença do SDK da Steinberg. Produto fechado e vendido precisa resolver as duas antes de distribuir.
