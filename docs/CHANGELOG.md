# Modern Tyrian — Changelog

Histórico do projeto em ordem cronológica, **do mais novo para o mais antigo**, reunindo o antigo journal do plano geral e o diário da trilha Tyrian 2000. O estado atual do projeto está em [MODERNIZATION.md](MODERNIZATION.md); aqui ficam o que mudou, por quê, os commits, as decisões do usuário, as causas dos bugs e as mudanças de processo.

Convenções:

- Entradas só do Tyrian 2000 levam a marca **[2000]**; as sem marca valem para o 2.1 ou para os dois.
- "Worker" é o agente que implementa uma tarefa; o coordenador (Claude) revisa, faz commit e push.
- Números de casos de regressão eram os da época; o número atual está em [MODERNIZATION.md](MODERNIZATION.md) e no `AGENTS.md`.
- Ids de run/task do Orca citados são de sessões de trabalho e valem só como referência histórica.

---

## 2026-10-14

### Bike (FTMS): controles por toque (T3b.3)
- **Arrastar para dirigir:** o dedo no campo de jogo move a nave pelo seu deslocamento, somado ao caminho `mouseXC/YC` (clamp de ±30 e inércia stock mantidos), com a mesma `pointer_sensitivity` do mouse aéreo. O mouse sintetizado pelo SDL3 a partir do dedo é descartado em jogo (sem contagem dupla nem tiro por clique) e mantido fora dele, então os toques em menus continuam sendo cliques; o toque não passa pela tradução OK/Voltar do mouse aéreo.
- **Zonas nos painéis laterais:** esquerda REAR, SK-L, SK-R; direita BOMB, AUTO, MENU, PAUSE; dois dedos rápidos abrem o menu do jogo. O Modern desenha as zonas (só apresentação); no Classic e no 4:3 as zonas são a margem e a barra do quadro de 320. Auto-fire é o companheiro esperado: com Touch ligado e Auto-fire Off, o menu avisa e o rótulo AUTO fica apagado.
- **Código:** novo `src/bike_touch.[ch]` (também em `visualc/`), com um gancho em `handleSdlEvents`, um em `mainint.c` e um em `bike_controls_frame`.
- **Testes:** `--regress-bike-events` ganhou `finger_down|finger_move|finger_up` e `equip,rear=N`; casos novos em `check_bike.sh` e uma captura do HUD com as zonas em `doc/ftms/screens/hud-touch.png`. Os 207 baselines e os digests antigos de `test/bike/regress.txt` não mudaram. O que verificar na Surface: `doc/ftms/controls-probe.md`.

### Bike (FTMS): mouse aéreo e navegação pelo controle remoto (T3b.2, parte do T3b.4)
- **Mouse aéreo:** com mestre e `air_mouse` ligados, o movimento relativo do ponteiro dirige a nave do jogador 1 em 1P (`inputDevice` forçado a 0 no `JE_playerMovement`), escalado por `pointer_sensitivity`, mantido o clamp de ±30. OK mantém o tiro, o clique direito abre o menu do jogo, Play/Pause pausa, Próxima/Anterior trocam o tiro traseiro e disparam os sidekicks. One-Euro não foi portado.
- **Menus:** OK vira Enter e Voltar vira Esc (na linha focada, sem olhar o ponteiro), outros botões e a deriva do ponteiro são ignorados. Corrige o Voltar da loja, do cubo de dados e do lançador, o clique direito no Sim do pedido de saída, o título que saía com Voltar (agora pergunta) e os nomes em branco (`PILOT`). O pedido de saída começa em Não nesse modo.
- **Bug do seletor** (`opentyr.c`): o clique comparava y com um limite em x. Corrigido sem condição; nenhum baseline mudou (o regress não entrega mouse).
- **Testes:** novas ações de `--regress-bike-events` (movimento, botões e teclas de mídia injetados) e casos novos em `check_bike.sh`; os 207 baselines e os digests antigos de `test/bike/regress.txt` não mudaram. Tabela de problemas e estado em `doc/ftms/remote-navigation-audit.md`. Ainda abertos: confirmação ao sair pelo menu do jogo e pelo Esc do lançador, seletor de letras para os nomes, e a entrega real das teclas de mídia no Windows (sonda T3b.0).

### Bike (FTMS): menu e HUD (T2.5, T2.6)
- **Menu:** novo `src/bike_menu.[ch]`, "Bike and Controls..." no Setup (`SETUP_MENU_BIKE`) e no menu do jogo. Lista rolável com todos os toggles do `design.md` (mestre, Bike Power, Bridge host:port com caixa de texto, FTP com passo 5 e aceleração ao segurar, Strength, Floor, quatro surges, Auto-fire, reserva 25/50%, mouse aéreo, velocidade do ponteiro, toque, HUD) e uma linha de estado ao vivo. Aplica na hora e persiste em `[ftms]`; `&` e `<` não existem nas fontes do jogo, por isso o nome é "Bike and Controls".
- **HUD:** Modern (painel de status do 1P: ponto de link, watts, rpm, esforço, medidor de surge, AUTO/HOLD/SURGE) e Classic (ponto e watts). Só desenha com mestre e HUD ligados. `bike_autofire_state()` é um stub (falso) até o auto-fire do T3b.1.
- **Regress:** os 207 baselines não mudaram (a linha do menu do jogo só aparece com o mestre ligado). Novos `--regress-screen=bike-menu[-nav|-edit]` e `--regress-menu=bike`; `tools/check_bike.sh` guarda os hashes dos PNGs de menu e HUD e prova que o HUD não altera o hash de estado. Capturas em `doc/ftms/screens/`.

### Bike (FTMS): núcleo no jogo (T2.2 a T2.4)
- **Novo, opcional e desligado por padrão:** `src/bike.[ch]`, `src/bike_rules.[ch]` (o motor `bike_effort`/`bike_feed` já estava no branch). Interruptor mestre + um toggle por recurso, em `[ftms]` do `opentyrian.cfg`, na CLI (`--ftms-bridge`, `--ftms-ftp`, `--ftms-off`, ids 450–452) e no regress (`--regress-bike-feed`, `--regress-bike-log`, `--regress-bike-set`, ids 440–442).
- **Gerador:** mapeamento C com acumulador de resto exato e mistura de volta ao stock quando o link cai (1,5 s de atraso, 1/2 por segundo). Sem fonte ou amostra o jogo usa o `powerAdd` stock. O simulador da tela de upgrade não muda.
- **Hash de estado:** `power`, `lastPower` e o digest da bike só entram com feed ou mestre fixado ligado; nenhum dos 207 baselines mudou.
- **Testes:** `tools/check_bike.sh` e `make regress-bike` (flag desligada = baseline, tudo explicitamente desligado = baseline, 150 W constante, rampa, queda do link, parada, FTP 50 e 500, duas pedaladas Q200B, determinismo; digests em `test/bike/regress.txt`). Um subconjunto roda no quick.
- Pendente: transporte da ponte (`bike_ws`), efeitos de surge, auto-fire, HUD e menus (tarefas seguintes).

### Bike (FTMS): efeitos de surge e auto-fire de alternar (T3.1, T3b.1)
- **Surge:** quatro efeitos, cada um com toggle próprio sob o mestre (superbomb ligado por padrão, os outros desligados): +1 superbomb por overdrive-ready (teto 10), carga de sidekick 2x, recarga de special com decremento extra, regen de escudo de 15 até 8 quadros mantendo o custo do pool. Só com o portão aberto.
- **Auto-fire:** `src/bike_controls.[ch]`. A opção só habilita; um aperto durante o jogo (tecla `A` ou Menu, ou clique do analógico direito) liga/desliga, sem menu, sem novo slot de `keySettings`. Reserva Off/25 %/50 % de 900 segura o tiro automático enquanto `power` está abaixo dela, para o escudo regenerar; o fogo manual ignora a reserva. O trinco sobrevive entre fases e começa desligado a cada execução. Acessores para o HUD: `bike_autofire_active()`, `bike_autofire_held_back()`.
- **Regress:** `--regress-bike-events` (id 443) injeta alternâncias, fogo manual, mudanças de configuração e equipamento; novos casos em `check_bike.sh` com digests em `test/bike/regress.txt`. Os digests antigos não mudaram (os casos com overdrive fixam `surge_superbomb=0`); 207/207 casos do jogo inalterados.

### Portes do OpenTyrian upstream (carlreinke, 216c771..624e2f2)
- Portados por intenção, um commit cada, citando o hash upstream. Nenhuma linha de base mudou: 207/207 casos 2.1 passam.
  - `216c771` `math.h` incluído onde usado (8 `.c`) e `TreatSpecificWarningsAsErrors=4013` no projeto MSVC.
  - `49af573` `toupper`/`tolower` sem comportamento indefinido: o código das teclas secretas da tela de título e também os outros usos (`joystick.c`, `params.c`, `sprite.c`, nome do arquivo de shapes), que o upstream não tocou.
  - `dda8863` movimento do mouse só conta como entrada quando é diferente de zero; espaços sobrando em `JE_showVGA`. A troca `SDL_FALSE` -> `SDL_DISABLE` é só de SDL2 e não se aplica.
  - `52140ae` o arquivo de sons só é carregado se `init_audio()` der certo; se o dispositivo não abre, o subsistema de áudio é encerrado.
  - `a0ed9fd` página de código UTF-8 no Windows: `visualc/opentyrian.manifest` embutido por `visualc/resources.rc`. O build MSYS2 já compila esse `.rc` com `windres`, então recebe o manifesto sem mudança extra (a regra do `Makefile` passou a depender do manifesto). Não testado no Windows.
  - `a923d8b` `logFatal` formata a mensagem uma vez (antes reusava o `va_list`); passa por `SDL_LogMessage`, mantendo o `--log-file`.
  - `624e2f2` **parcial**: `src/swar.h` + SWAR nas linhas de tiles do fundo, em `drawlist_apply_filter_screen` e nos filtros iced/blur, com saída idêntica por construção. Ficaram de fora a reescrita de lava/água (o próprio upstream diz "slightly different"), o `showHeadlight` (diverge do original para posições fora da faixa e falha as asserts do upstream em px=py=0 numa comparação exaustiva) e `showFlipped`/`showNormal` (o `interp_blit_playfield` já faz essa cópia).
- Commits somente de README do upstream (`f3d4560`, `ba9c399`) ignorados.

## 2026-10-09

### Documentação: planos viram `MODERNIZATION.md` e `CHANGELOG.md`
- A pedido do usuário, os dois arquivos de plano da raiz (geral e da trilha Tyrian 2000) foram removidos. O estado atual das duas variantes passou para `docs/MODERNIZATION.md`, e o histórico para este arquivo.

### Luz dos tiros visível, corrigida e proporcional
- **Teste do usuário:** a etapa 3 de profundidade não aparecia ("não notei nenhuma diferença"). A diferença média entre antes e depois era de 1 a 6 níveis em 255, e a luz dos tiros clareava o terreno só +4 níveis, mesmo no High. Workers: Claude Sonnet 5.5 (medium).
- **Prévia:** flags de ajuste só para regressão (`--regress-light-scale`, `--regress-light-radius`, `--regress-fog-strength`, ids 435–437) permitem renderizar níveis de luz e de névoa sem recompilar.
- **Qualidade (`5f12511`):** o usuário viu pixelação "de JPEG", pontos avermelhados e o halo deslocado para cima e para a esquerda.
  - Todo resample 2× lia meia célula adiante (`d*128+64` em vez de `-64`), o que deslocava a luz e o bloom ~2–3 px.
  - As máscaras ficavam em 8 bits antes do ganho, com só 1–3 níveis por pixel. Os arredondamentos viravam blocos, e cada canal arredondava de um jeito, daí o anel oliva, as manchas vermelhas e magenta e a faixa vermelha entre os tiros.
  - Agora todos os planos são Q8 de 16 bits, e o centro da luz fica a menos de 0,07 px do centro do tiro.
  - Um limitador suave (joelho 150, assíntota 216) substitui o corte seco no teto.
- **Tiros grandes (`cce14a3`):** a luz crescia com a área emissora, e os tiros grandes chegavam a 3–3,4× o Pulse-Cannon. Antes do desfoque, a parte dos tiros do jogador na máscara passou a ser escalada por `(S_ref/S)^0.75`, com a energia local medida no suporte do blur. Os tiros grandes ficaram em ~1,4–2,1× o tiro pequeno. Explosões, tiros inimigos e bloom não mudaram.
- **Níveis (escolha do usuário):**
  - Névoa no bg1: 40 → **30**.
  - Ganho da luz: o High foi primeiro para 3328 (4×) e o usuário achou forte. Pediu 60% disso, então ficou **High 1997, Low 1198** (`5353944`). O Low continua 0,6× o High, e a opção segue Off/Low/High.
  - Aprovado jogando ("Tá ótimo").
- **Regressão:** só mudaram hashes de quadro Modern com luz ou Depth ligados. Classic, estado e Modern sem luz não mudaram.
- **Processo:** o usuário prefere testar no jogo a ver prints. Só gerar capturas quando ele pedir.

### Profundidade, etapa 3, e o INSERT COIN apagado
- **Workers:** dois Claude Sonnet 5.5 (medium), a pedido do usuário, cada um na sua worktree do Orca a partir do `master`, numa só Run.
- **INSERT COIN (`e41c056`):**
  - **Bug:** com Lighting ligado, o texto dos demos brilhava como um tiro. Ele era desenhado sem contexto próprio e herdava o do último objeto, quase sempre `DL_OBJ_PLAYER_SHOT`: ganhava tag de emissão, cor de luz e a camada de tiro.
  - **Correção:** o texto passa a usar `DL_OBJ_HUD`, e o contexto anterior é restaurado depois (`drawlist_get_context`, novo). A janela de mensagens (`JE_drawTextWindow`) e os rastros da nave no fim da fase receberam o mesmo tratamento.
  - **Revisão:** a primeira versão zerava o contexto depois do texto e alguns baselines foram justificados só por "consistência". Voltou para o worker, que trocou o zero por restauração e atribuiu cada baseline a uma das três correções por A/B.
  - **Cenários também mudam:** os cenários de regressão rodam com `playDemo`, então também mostram o INSERT COIN, e por isso os casos de fase mudaram junto.
  - **Prova:** `--regress-demo-hud-check` ganhou uma sonda em `modern-light-demo1-d4`. Os pixels emissores acrescentados pelo texto eram 3.933.643 e agora são 0.
  - **Baselines:** mudaram 11 do 2.1 e 12 do 2000, só hashes de quadro Modern.
- **Etapa 3 (`41660ff`), só com `Depth: On` (Off fica idêntico bit a bit):**
  - **Névoa:** os pixels do bg1 misturam 40/256 (depois ajustado para 30, ver a entrada acima) em direção a uma cor de névoa calculada por quadro (a média do bg1 ainda sem névoa, meio dessaturada e ~35% mais clara). A linha translúcida do bg2 recebe metade, e as fases de espaço ficam sem névoa.
  - **Sem ruído animado:** os quadros interpolados não têm um scroll único do bg1 em que ancorar o ruído, e um ruído mal ancorado "nadaria" sobre o terreno.
  - **Ordem dos passes:** névoa → sombras → luz, para a sombra manter a força sobre o terreno enevoado.
  - **Luz por camada:** bloom e luz ficam em planos separados, e a luz é pesada pela camada que a recebe (Q8): terreno e inimigos de chão 256, inimigos do céu e do topo 160, nave e sidekicks 144, bg3 24, tiros/explosões/VFX 256. O bloom não é pesado. Nas fases de espaço não há luz por camada.
  - **Telas seguradas:** pausa, menu e ajuda reaproveitam os dois efeitos do snapshot.
  - **Custo:** ~0,16 ms por quadro apresentado, além da etapa 2.
  - **Regressão:** linhas novas `Depth fog:` e `Depth light:`; as linhas de held ganharam campos; a fixture `--regress-depth-check` cobre a cor da névoa, a regra só-bg1 e a tabela de pesos; casos novos `depth-light-*` (4 no 2.1 e 3 no 2000); só mudaram baselines `depth-*` com Depth On e os de espaço ficaram idênticos. Depois do rebase sobre o INSERT COIN, os `depth-*` foram regenerados do binário combinado; nas suítes completas do worker, 207 casos no 2.1 e 183 no 2000.

### Release v0.6.0, testes rápidos e CI sem retrabalho
- **Release v0.6.0** ("Modern Tyrian v0.6.0: depth shadows") no commit `2ecacc4`, a pedido do usuário, depois da CI verde nos três sistemas. A CI anexou os quatro pacotes (Linux x86_64/arm64, Windows x86_64, macOS universal).
- **Regressão rápida (`61ef2e7`, `e3dff92`)**, a pedido do usuário ("essas suítes são muito longas"). Worker: Codex `gpt-6.1-sol` (medium).
  - `make regress-quick` (~20–25 s) cobre as 5 demos Classic, os cenários de água/spotlight/flip/blur, o núcleo Modern (4:3, 16:9, CRT, Depth On na TYRIAN, pausa segurada, a linha de pausa da matriz), os guards baratos e 4 casos do 2000 quando os dados existem. Sem dados, imprime `2000 quick: skipped (no data)`.
  - Filtro por área: `REGRESS_ONLY=<regex>` ou `--case=REGEX`, nas duas suítes.
  - Suíte 2000 em paralelo: de ~4,5 min para ~1 min 45 s, com os mesmos 180 casos. Os alvos completos e a CI não mudaram.
  - A primeira versão falhou só no Windows: no MSYS2 o `cp` de `./opentyrian` grava `opentyrian.exe`, e o guard de bootstrap acusou a própria cópia do executável. Corrigido no mesmo worker.
- **CI sem retrabalho (`6af6638`)**, depois de o usuário notar builds repetidos. Cada workflow ganhou um job de decisão (`.github/ci/decide.cjs`):
  - um push só de Markdown, `docs/` ou `.worker-reports/` pula o build e a regressão e termina verde;
  - um push cujo SHA já passou no mesmo workflow em outro branch também é pulado, então o fast-forward do `master` para um branch testado não custa nada;
  - a release baixa os pacotes do push verde do mesmo SHA e anexa os bytes originais; só recompila se não houver push verde e falha alto se os pacotes tiverem expirado;
  - o `workflow_dispatch` sempre roda tudo.

  O caminho da release só será exercitado de verdade na próxima release (ou numa pré-release descartável).
- **Processo:** merges por fast-forward quando o branch já contém o `master`; o coordenador roda só o quick localmente e usa a CI como portão completo; um trabalho pausado é retomado com `codex resume <sessão>` mais `worker-start --task … --terminal …` depois de um reinício do Orca.

### Profundidade no Modern: buffer de camadas e sombras projetadas
- **O que entrou (branch `vittau/depth-layers`, merge no `master`):** sombras projetadas entre as camadas, com a luz fixa vindo do alto à esquerda. É a Fase 3 "Camadas e profundidade", etapas 1 e 2 (pedido do usuário, 2026-10-08). Workers: Claude Sonnet 5.5 (high) nas três primeiras tarefas e Codex GPT-6.1 Sol (medium) na última, a pedido do usuário.
- **Decisão técnica (coordenador):** em vez de um buffer de cor por camada, um **buffer de camada por pixel**, carimbado pelas mesmas primitivas que já carimbam o tag de emissão. Buffers de cor separados obrigariam a refazer a composição dos blends e dos filtros fora do motor, o risco "preservando a matemática dos blends". O quadro de 8 bits fica intocado, então o bg2 e os filtros de lava, água e blur não precisaram ser refeitos. Cada pixel guarda a camada que o pintou por último, e cada tick guarda a ordem em que as camadas foram desenhadas, que muda por nível.
- **Como o quadro é montado (`JE_main`, `tyrian2.c`):** tudo é pintado em ordem num único quadro de 8 bits: bg1 → estrelas → bg2 (por cima, misturado ou com filtro) → inimigos de chão (slots 25–49 e 75–99) → bg2 nos níveis com `background2over == 1` → bg3 quando `background3over == 2` → inimigos do céu (0–24) → bg3 → inimigos do topo (50–74) → tiros, nave, explosões. `draw_background_2_blend`, `JE_darkenBackground` e os filtros leem o próprio framebuffer.
- **Etapa 1 (`7847f6d`):** o buffer de camada em `game_screen`, `VGAScreen2` e nas cópias do replay interpolado, como o tag. Quem carimba: linhas de fundo (identidade 1/2/3 pelo contexto, com marca de "misturado"), sprites pelo contexto (inimigo por faixa de slot, item, nave, sidekick, tiros, explosões), estrelas e superpixels; preenchimentos e limpeza zeram; o `darken` não muda a dona do pixel. A lava reproduz o próprio deslocamento na camada, a janela 264×184 é copiada para `modern_depth.c` com o mesmo flip do tag, e os pixels de VFX ganham uma marca "sem sombra". Nenhum pixel nem baseline mudou.
- **Etapa 2 (`e994e9a`) e ajuste (`ccccf5b`):** cada camada projeta sobre as camadas desenhadas antes dela e mais baixas, com deslocamento proporcional à altura (dx,dy): bg2 (4,6), inimigos do céu (8,12), nave e sidekicks (9,14), bg3 (10,15), inimigos do topo (12,18); inimigos de chão (2,2). O bg2 translúcido projeta com 31% do peso. Tiros, explosões, estrelas, superpixels e texto não projetam nem recebem. Fases de espaço (`starActive`) não têm sombra. Cálculo: silhueta deslocada inteira, desfoque separável em ponto fixo e escurecimento de 78/256, ~0,3 ms por quadro apresentado.
- **Teste do usuário:**
  - **Halo:** havia um halo claro entre a terra flutuante e a sombra, pior na HOLES (físico E1:L11, terra no bg3). A causa era a silhueta só ser marcada onde caía num receptor válido; o desfoque então clareava a faixa colada na borda. Corrigido em `ccccf5b`.
  - **Opção:** o usuário achou o High bom e pediu só um toggle, então a opção virou `Depth: Off/On`, padrão On (não Off/Low/High como previsto no plano), com o desfoque do High e a força reequilibrada para o mesmo escurecimento total. O config antigo (`low`/`high`) lê On.
  - **Pausa (`4dcf353`):** na pausa, no menu do ESC e na ajuda o efeito sumia, e o bloom e a luz também. Agora essas telas reaproveitam a camada, o tag e a luz do último quadro real (`modern_held.c`), e os pixels que o overlay mudou não recebem nada.
- **Fases × camadas:** o índice de `--regress-level` é físico, não a ordem de jogo. A TYRIAN é o físico 1:9 (1:15 no Hard), com a terra no bg1 e a água no bg2 translúcido; o 1:1 é ASTEROID1.
- **Regressão:** a regressão pina `Depth` em Off, então nenhum baseline antigo mudou fora das telas de Graphics, por causa da linha nova no menu. Entraram 20 casos `depth-*` no 2.1 e 16 no 2000 (Off × On, estado/RNG idênticos), a fixture `--regress-depth-check` e os checks `--regress-layer-check` e `--regress-held-check`. A CI passou nos três sistemas.
- **Pendências na época:** o "INSERT COIN" iluminado como tiro (corrigido na mesma data, ver acima) e a etapa 3.
- **Processo:** o usuário achou as suítes longas. A partir daqui o coordenador não roda mais as suítes completas localmente: revisa, faz push do branch e usa a CI como portão.

---

## 2026-10-07

### Release v0.5.0
- **Release v0.5.0** ("Modern Tyrian v0.5.0: PROGRESS bar") no commit `8d0e15f`. Traz a barra PROGRESS do HUD Modern sobre a v0.4.1. A CI passou nos três sistemas, no push e na release, e anexou quatro pacotes: Linux x86_64/arm64, Windows x86_64 e macOS universal (sem Windows arm64 desde a v0.4.0).

### PROGRESS no HUD Modern
- **Decisão:** barra horizontal contínua com paleta/acabamento de SHIELD, congelada durante boss reconhecido; preserva as barras de HP existentes. Saída antecipada ou alternativa pode terminar no valor atual, abaixo de 100%. Informação de progresso da fase, que a decisão original de 2026-09-27 deixava de fora, foi autorizada aqui com esses limites.
- **Implementação conservadora:** observador somente de apresentação, sem escrita no gameplay/RNG. Marco estimado pelo maior tempo positivo de ação 11/36; avanço por trecho até 99%, sem crédito de salto ou repetição. Objetivo principal exige marco executado e saída natural com sobrevivente, sem causa negativa anterior; um sobrevivente basta em 2P. Ausência temporária de jogadores vivos pausa sem cancelar nem resetar; saída antecipada fica encerrada até reset, mesmo se chegar outro marco no warp. Retornos, reposições 38/75 e mapas sem denominador congelam ou omitem a estimativa. Não há identificação universal de boss final; a exceção autoral de timer E4/L5 no modo comum não cobre Timed Battle nem demais timers.
- **Layout:** 1P/arcade à esquerda (rótulo y=142, barra y=150); 2P na linha de título sob o playfield, mantendo a linha de mensagens. Sem painéis/4:3 fica intencionalmente omitido para não cobrir informação. O desenho lê o snapshot, sem interpolar a barra nova; o latch confirmado permanece no warp/fade até o reset do próximo mapa.
- **Limites:** inventário de 62/70 níveis é estático. Grafo por trechos, perfis certificados, bosses sem barra, transformações e cobertura de todos os caminhos/modos ficam como evolução futura, sem promessa de porcentagem universal precisa. Selecionar um mapa não comprova execução de seus recuos nem vitória final. Detalhes em [progress-runtime.md](progress-runtime.md) e [progress-levels.md](progress-levels.md).
- **Validação final confirmada pelo coordenador:** release/debug, auditoria GCC16 dos oito arquivos C finais, 271 assertions da fixture escalar com ASan/UBSan (não o motor inteiro), guard de dados protegidos e portabilidade/VS XML/CRLF. Suítes release: 172 casos 2.1 e 153 casos 2000, com guards existentes e de PROGRESS, igualdade de estado/RNG OFF/ON e framebuffer Classic. Capturas reais 2.1 em 16:9, 16:10 e 2P aprovadas; sem capturas 2000.
- **Cobertura representativa de runtime:** release final sem aceleração: E1/L6 termina a 52% nas duas variantes; E1/L4 atinge 100% com 40 amostras mantidas; E4/L7 em 2P/fire, VFX OFF, tem três retomadas de boss por variante, 5/3 avanços posteriores e saída a 45%/43%. Não certifica todos os níveis, branches, bosses finais ou modos; nomes de casos sintéticos não provam todos os hooks reais.
- **Referências Modern:** os novos pixels de PROGRESS justificam as mudanças explícitas no 2.1: 20 arquivos de hashes de frame mais dois CRCs de frame de pausa na matriz mista (21 arquivos). No 2000, 12 arquivos de frames mais os dois CRCs de pausa (13 arquivos). Nenhuma referência Classic ou de estado mudou; os reruns finais passaram. Tarefa sem commit/push; dados 2000 não foram exportados.

---

## 2026-10-01

### Launcher no Game Mode do Steam Deck; release v0.4.1
- **Bug (v0.3.0–v0.4.0):** no Game Mode o jogo fechava antes da primeira tela, com o log "The launcher window resize did not settle: Can't stat: No such file or directory". No modo Desktop funcionava. O gamescope controla o tamanho da janela e nunca aplica o resize pedido pelo launcher, então o `SDL_SyncWindow` estoura o tempo. Desde `e7f0d16` esse timeout virava um `return false` em `launcherChoose`, e o `main` encerrava o jogo com sucesso. O "Can't stat" era um `SDL_GetError()` antigo, porque o SDL não define erro no timeout.
- **Correção:** `video_fit_launcher_window` não falha mais. O timeout vira um aviso no log, e o launcher desenha no tamanho que a janela tem, acompanhando os eventos de resize seguintes. Com Cocoa e janelas que assentam, nada muda.
- **Regressão:** o mock de `check_display.sh` ganhou um compositor que ignora resizes (timeout sem erro). O launcher precisa manter 1280×800 e ajustar o canvas a esse tamanho. A suíte 2.1 (172 casos) passou sem mudar baselines. Confirmado num Steam Deck físico em Game Mode com a v0.4.1: o launcher abre normalmente.

---

## 2026-09-30

### Filtro CRT na apresentação Modern
- **Menu:** `CRT Filter` em Setup → Graphics, só ativo no Modern: Off, Scanlines, NTSC e Scanl + NTSC. A fonte normal mede 117 px para `Scanlines + NTSC` e 90 px para a abreviação, dentro da coluna de 95 px. O `+` original é um placeholder vazio; uma cruz pequena ocupa o avanço já medido. A linha extra usa o espaçamento adaptativo do menu, sem sobrepor o status ou o picker.
- **Port:** kernel escalar de Shay Green (`snes_ntsc 0.2.2`, LGPL 2.1+), trazido do port do Deadly Dave com os créditos e a licença. RGB555, faixa RGB de 8 bits, preset composite, três fases de burst por frame apresentado e tabela de 16 MiB criada só no primeiro uso. Os últimos pixels de larguras que deixam um chunk parcial também entram no blit.
- **Scanlines:** mesmas constantes do Deadly Dave (`>> 1`, luminância limitada a 216), bandas de meia linha lógica e mistura proporcional quando uma linha física cruza a banda. O NTSC roda só nas 200 linhas de origem; a expansão vertical é feita depois, por linha. Alturas múltiplas de duas vezes a origem usam 400 linhas; as demais (inclusive 600 px, escala inteira ímpar) usam a altura do Fit em pixels nativos; destinos pequenos mantêm 200.
- **Integração:** depois dos passes e da captura do canvas em `modern_present_frame`, inclusive nos frames interpolados e nos modais. Buffer e textura próprios, recriados só quando as dimensões mudam; Off mantém o caminho original. Aspecto e mouse continuam usando o canvas e o mesmo retângulo Fit do presenter, agora exposto por um helper de vídeo. Config compartilhado `crt_filter`, default/valor desconhecido = Off; nenhum estado do jogo ou RNG é alterado.
- **Medição release (`-O2`, Apple A18 Pro, canvas 427×200, média de 60 chamadas):** em 1080/2160 linhas, Off 0,014/0,014 ms, Scanlines 0,382/0,494 ms, NTSC 0,311/0,305 ms e combinado 0,847/1,328 ms por frame. A tabela levou 7,711 ms para construir; upload e GPU ficam fora dessas medidas.
- **Regressão:** 6 casos novos (quatro modos, combinado em 2160 e picker), levando a suíte 2.1 de 166 a 172. 13 fixtures sintéticas (incluindo alturas ímpares de 1079/2161 px), hashes do buffer filtrado, estado/RNG idênticos nos quatro modos, roundtrip do config, reutilização/recriação em runtime e mouse nos modais das duas variantes. O coordenador autorizou explicitamente atualizar só `screen-setup.txt` e `modern-screen-setup-16x9.txt`: a linha nova muda a tela real de Graphics, e congelar a fixture esconderia essa alteração. Os demais baselines ficaram idênticos; os 153 casos do 2000 passaram sem mudanças.
- **Validação:** builds release e debug (`-O0`, `-Werror`, asserts), auditoria gcc-16 em todos os `.c` tocados e fixtures com ASan/UBSan. PNGs de revisão só do 2.1 (menu/picker e quatro modos em 1920×1080 e 3840×2160), fora do git. Worker Codex; sem mudança de branch, commit ou push.
- **Origem:** pedido de 2026-09-28, com referências do usuário no `deadly-dave` (port escalar do `snes_ntsc`, scanlines de meia linha) e no `antivirus-95` (passada estilo crt-geom). Decisão do usuário: sem curvatura e sem máscara; vinheta poderia entrar. Teoria em [CRT.md](CRT.md).

### Moldura chanfrada no HUD Modern
- **Moldura de vidro em volta do playfield (`c752fde`)**, a pedido do usuário, a partir de uma imagem de referência.
  - Desenhado: filete duplo na junção painel/playfield (branco-ciano encostado no jogo, laranja por fora), leve clareamento quente do painel perto da borda, dois glints fixos por lado (linhas 11 e 133), sombra interna nas 6 colunas das bordas e nas 3 linhas de baixo do playfield, e borda sutil no topo da faixa de mensagem.
  - **Decisão do usuário:** tudo na grade lógica do canvas, sem desenho em resolução de tela. O filete fica com 1 px lógico e entra nos hashes da regressão.
  - **Ordem:** filete e glints depois do ambilight e antes do HUD, então texto e barras ficam por cima. A sombra vem depois da amostragem do ambilight, e a borda da faixa só toca o fundo, nunca os glyphs.
  - **Escopo:** só o modo painel do Modern. Em 4:3 os painéis teriam 28 px, abaixo do mínimo de 51 (`MODERN_HUD_MIN_PANEL_WIDTH`), então o 4:3 segue no fallback sem painéis e não muda. Classic, telas fora do gameplay e todos os baselines de estado continuam idênticos.
  - **Regressão:** mudaram 15 baselines Modern do 2.1, 15 do 2000 e os dois agregados de pausa Modern em `final-regression.txt` de cada variante. As suítes passam com 166 e 153 casos.
  - **Worker:** Codex `gpt-6.1-sol` (medium), despachado por `--inject`.
- **Segunda passada (`e3325be`)**, depois do teste do usuário: o filete não reagia aos glints e os glints eram espelhados, o que ficava repetitivo.
  - **Bloom no filete:** longe dos glints o filete é ciano (~135,170,193); perto de cada glint sobe até quase branco numa queda quadrática de 40 a 56 linhas, espalha por três colunas do painel e vaza de leve numa coluna do playfield. Uma névoa quente fraca envolve o núcleo.
  - **Glints diferentes por lado:** cada lado tem sua tabela estática (linha, pico, rastro, raio e força do bloom): esquerda nas linhas 11 e 133, direita nas 38 e 166.
  - **Revisão:** a primeira versão do bloom foi recusada, porque subia só de ~150 para ~190 em ±10 linhas e o filete tinha perdido o tom frio. O worker refez com os valores medidos no relatório.
  - **Regressão:** mudaram os mesmos baselines da primeira passada.

### Barras de vida em telas ultrawide
- A pedido do usuário: as colunas do bloco de escudo/armadura/energia ocupavam 1/3 do painel e se afastavam demais (48 px em 21:9, 96 px em 32:9). Agora têm no máximo 36 px (`VIT_COL_MAX_W`), o que ainda cabe os rótulos inteiros, e um bloco limitado se alinha pela borda externa como o resto do painel. Abaixo do limite (16:9, 16:10) nada muda. Só mudaram os baselines Modern 21:9 e 32:9 do Tyrian 2000. Worker: Codex `gpt-6.1-sol` (medium); a primeira versão centralizava o bloco e foi devolvida na revisão.

### Windows arm64 fora da CI e das releases
- Decisão do usuário (pouquíssima adoção). A falha da CI do `a2bec4d` foi no `setup-msys2` desse runner: o `gpg` morreu ao popular o keyring do pacman e a sincronização recusou as assinaturas; o rerun passou. Nesse runner o MSYS2 é x64 emulado, e os scripts de shell da regressão ficam lentos (19 min por push contra 12 no x86_64). O Windows on ARM roda o pacote x86_64 pela emulação do sistema. A v0.3.1 ainda saiu com o pacote arm64; a partir da v0.4.0, não.

---

## 2026-09-29 / 09-30

### [2000] Rodadas finais: 3b, 3c, instalador, launcher com Install, Fases 4 e 7
- **Integrados em `modernization`:** 3b, regras de jogo (`b49b22c`); instalador (`ce9731e`); 3c, menus e placares (`6be3c20`); fluxo Install no launcher (`fb6e172`); Fase 7 (`7d132a1`); Fase 4 (`d39de50`). Cada um passou em `make regress` (então 166 casos mais os guards) e em `make regress-2000` (148 casos), sem mudar nenhum baseline do 2.1.
- **Placar:** a 3b e a 3c criaram módulos de placar paralelos. O worker da 3c unificou os dois em `src/highscores.[ch]` ao integrar.
- **Fase 7:** a auditoria do Modern no 2000 não achou defeito no renderer, porque tags e luz vêm do contexto do objeto e não do id do sprite. O HUD usa os rótulos semânticos. `--regress-demo-hud-check` prova, nas duas variantes, que as demos seguem o HUD do modo ativo (decisão do usuário).
- **Fase 4:** Timed Battle completo; episódios 1–5, arcade de 9 naves, Super Tyrian, Destruct e Natal são exercitados pelos menus reais. O `--regress-flow` é um teclado virtual que percorre os menus; há também uma varredura das 74 fases.
- **A conferir no DOS (Fase 4):** o overflow de 16 bits do bônus de tempo no fork; o mapeamento batalha→episódio (seções placeholder); a seção 44 do E1 com shapes inexistentes; o bônus de vidas; música e cancelamento do placar da batalha; o tamanho do sprite do Pretzel Pete; o estado 8 do Super Tyrian; as aproximações do fork nos eventos 58/59/68 e na trilha 198.
- **CI no Windows:**
  - Os hashes do launcher saíam com CRLF (`fopen "w"`); corrigido com `"wb"` (`da4fcb1`). Os pixels são idênticos nos três sistemas.
  - O `%zu` não passa no `-Werror` do MinGW e o `python3` faltava no MSYS2 (`4991bbd`).
  - Os testes do instalador usavam um curl falso em shell, que o `CreateProcess` não executa, e acabavam no curl real (rede). Foram trocados por um stub em C apontado por caminho absoluto no spec de teste (e uma proteção que impede acesso à rede). Duas rodadas no branch `ci/instwin` passaram nos três sistemas; merge `916ee80`. Também foi corrigido um bug real do Windows: arquivos somente leitura copiados de uma pasta não podiam ser removidos.
- **Pausas e modelos:** a cota do Codex acabou com os 4 workers GPT-6.1 Sol perto do fim, e eles foram concluídos por workers Sonnet 5.5. Depois o limite do Claude fechou os terminais dos workers Sonnet; as sessões foram retomadas com `claude --resume` e religadas a novos dispatches, sem perda. Para novos agentes, o usuário escolheu Codex GPT-6.1 Sol com raciocínio medium.

---

## 2026-09-29

### Release v0.2.1
- **Release v0.2.1** no commit `6e76162`, a pedido do usuário. Versão de correções sobre a v0.2.0: as telas de remapeamento não estouram mais a largura, e o HUD do 1P não mostra "Player 1". A CI passou nos três sistemas e anexou os cinco pacotes. O `master` continuou em `a81e640` e só avança se o usuário pedir.

### HUD sem "Player 1" no 1P
- `46d8be8`, a pedido do usuário. O nome só aparece em partidas de dois jogadores; no 1P e no arcade as linhas sobem e as barras se recentram. Worker: Codex `gpt-6-luna` até a cota acabar, depois Sonnet 5.5 no mesmo worktree.

### Release v0.2.0, `master` alinhado e remapeamento corrigido
- **Release v0.2.0** no commit `a81e640`, a pedido do usuário, com os cinco pacotes anexados pela CI. Inclui luz colorida, partículas de ambiente, o limite de luz dos tiros grandes, as barras mais altas, a costura do cabeçalho pic-1 e a regressão em paralelo.
- **`master` avançado por fast-forward** até `a81e640`, a pedido do usuário: 176 commits do `modernization`, sem commit de merge.
- **Telas de remapeamento (`67e063e`).**
  - **Causa:** com dois mapeamentos numa ação, o texto do controle passava de x=320. A tela pic-1 deixava de alargar no Modern (não sobrava coluna livre) e o excesso reaparecia na linha seguinte, à esquerda.
  - **Rótulos:** o valor que não cabe em x ≤ 310 vira rótulos curtos (`A/RB`, `LY-/UP`, `RT+/H12X-`), e `+N` entra como último recurso. O texto original fica sempre que cabe, então o Classic não muda. Nomes longos de teclas encurtam do mesmo jeito (`L Shift`, `KP …`, `SC n`).
  - **Corte na borda:** glyphs de fonte que cruzam a borda da superfície agora são cortados em vez de dar a volta.
  - **Regressão:** ganhou as telas `joystick-multi` e `keyboard-long` e ficou com 164 casos, em ~56 s em paralelo. Worker: Codex `gpt-6-sol`.

### HUD sem divisor, cabeçalho pic-1, barras, Mega Cannon, regressão em paralelo e troca de agentes
- **Divisor preto acima das barras removido (`fa23d6d`)**, a pedido do usuário.
- **Agentes:** a cota do OpenCode Go do usuário acabou no meio de três tarefas (Mega Cannon, cabeçalho pic-1, barras mais altas). As tentativas foram encerradas fechando os terminais, e as mesmas tarefas foram reabertas com `--retry-of`, nos mesmos worktrees, com Claude Sonnet 5.5 (esforço alto). Em seguida o usuário definiu o **Codex como worker padrão**: `gpt-6-sol` (esforço alto) na maioria das tarefas e `gpt-6-luna` (alto) nas bem simples.
- **Cabeçalho das telas pic-1 alargadas (`993ace9`).**
  - **Causa:** quando o conteúdo do painel chega a x=310 (compra de Shield, diálogo de Quit), o ponto de alargamento cai na coluna 311, que é o bisel direito claro da caixa de título, e a faixa inserida o repetia como um bloco chapado.
  - **Correção:** as linhas do cabeçalho (y ≤ 33) repetem a última coluna interior (310). O mapeamento do mouse não muda, e o Classic continua idêntico. O segmento escuro no topo da caixa (y=6, x 285–293) já está na pic 1 original.
  - **Regressão:** ganhou a tela `--regress-screen=shield` e ficou com 158 casos.
- **Barras de vida 50% mais altas (`ed0c9fb`).**
  - 1P e arcade: de 52 para 78 px. O bloco delas fica centrado entre o bloco de cima e a pilha de baixo (gerador sobre o aviso de trapaça), que agora fica presa ao pé do painel.
  - Arma especial: com uma especial equipada, as linhas sob o ícone ficam um pouco mais juntas, porque o pior caso (arcade + especial + trapaça a 16:9) só cabe assim.
  - 2P compacto: as barras crescem o quanto a largura libera: 39 px em 21:9 e 32:9, 35 em 16:9 e 29 em 16:10; o que limita é o pé do painel.
  - Rodapé: o aviso de trapaça usa de 1 a 3 linhas conforme a largura, e o cronômetro do 2P vai para uma linha só quando cabe.
- **Luz do Mega Cannon no power 6 (`c116186`).**
  - **Causa:** o orbe é desenhado com quatro sprites de 70 a 95 pixels brilhantes cada, contra menos de 30 nos tiros pequenos. Por isso somava 8 vezes a luz de um Pulse-Cannon e saturava um platô largo. A luz colorida não foi a causa: até reduziu a luz do orbe.
  - **Correção geral:** cada sprite grava sua pegada emissiva (número de pixels brilhantes) no byte de tag, junto da classe. Um tiro do jogador com mais de 32 pixels brilhantes tem a emissão escalada para esse valor de referência.
  - **Efeito medido:** a luz do Mega Cannon caiu 3,1×, e a do Laser e do Zica Laser cerca de 2,5×. Tiros pequenos, explosões, tiros inimigos e itens não mudam; custo ~0,02 ms.
  - Novas opções de teste `--regress-front-weapon` e `--regress-front-power` (311–312) montam o armamento no `--regress-script`.
  - **Falso alarme:** o agente viu rodadas em paralelo divergirem, mas o driver de medição dele não fixava `--regress-seed` e caía em `time(NULL)`. Os casos da suíte fixam a semente.
- **Regressão em paralelo (`68cde70`)**, a pedido do usuário. `tools/regress.sh -j N` (ou `REGRESS_JOBS`) usa por padrão o número de CPUs. Cada caso roda num worker com buffer próprio e a saída sai na ordem de declaração; os casos pesados começam primeiro; o script é portável para Bash 3.2 e MSYS2. A suíte caiu de ~245–293 s para ~67–93 s no Mac de 6 núcleos, com casos e baselines inalterados (saídas byte a byte idênticas, inclusive sob carga). CI verde nos três sistemas. Feito pelo primeiro worker Codex (`gpt-6-sol`); o Orca 1.4.215/216 não reconhecia o Codex v0.158 como pronto (`agent_readiness` expirava), então a tarefa foi injetada num terminal aberto à mão com `dispatch --inject`.
- **Overlay de performance do Steam Deck:** o usuário viu o overlay (MangoHud) não aparecer no build Linux. O código de vídeo é equivalente ao do Deadly Dave (SDL3 3.4.16 estático, renderizador padrão), e depois o overlay voltou a aparecer sem mudança nenhuma. Uma tarefa de log do renderizador (Codex `gpt-6-luna`) foi abortada a pedido do usuário.

### Release v0.1.0, luz colorida e partículas de ambiente
- **Release v0.1.0** a pedido do usuário no commit `93b373b`, com os pacotes Linux x86_64/arm64, Windows x86_64/arm64 e macOS universal anexados pela CI. Nas releases, a CI passou a pular a suíte de regressão (`521fc4b`; depois substituído pelo reaproveitamento de pacotes, ver 2026-10-09); os pushes continuam rodando.
- **Retomada:** a sessão nova precisou de `orca orchestration run-use --id run_e0cad877e2fd` para voltar a despachar (`consumer_fenced`).
- **Partículas de ambiente (`ambient`):** brasas na lava, neve no gelo, névoa na água e no desfoque, poeira fina no espaço e poeira no resto (62 fases amostradas). Só clareiam ou mesclam de leve, não emitem luz, usam RNG próprio e seguem o Effects (Low esparso, High um pouco mais). Muito discretas; o usuário avaliaria jogando.
- **Luz com a cor do objeto (`lightcol`):** a auditoria confirmou que a luz vinha dos miolos brancos (tiro azul inimigo: 94–98% da energia no bloco de fogo). Agora cada pixel marcado leva a cor representativa do objeto (o tom saturado da família de matiz dominante do sprite), que tinge a luz e o bloom. Intensidade praticamente igual e custo inalterado (~0,47 ms no High).
- A regressão ficou com 156 casos.

### [2000] Rodada paralela: CI, arte do launcher, 3b, 3c, launcher e instalador
- **CI do 2000 (`d773095`):** `tools/fetch_t2000_data.sh` baixa da Camanis a cada execução, confere o tamanho e o SHA-256, extrai com segurança fora do checkout e verifica o manifesto. Os três workflows rodam `make regress-2000` depois do 2.1, sem cache. Na falha, sobem só `*.txt`/`*.log`; as releases continuam só com o 2.1. No Windows, os baselines novos saíam com CRLF; corrigido com `test/regress-2000/** eol=lf` no `.gitattributes` (`a3787f4`), e a CI ficou verde nos três sistemas.
- **Arte do launcher (`cfa3c12`):** gerada por um worker Codex GPT-6 Luna. Dois painéis de 960×1080 sem texto e dois letreiros "TYRIAN 2.1"/"TYRIAN 2000" de 800×320 com alpha, originais e sem ™, em `assets/launcher/`, com os prompts em `SOURCES.md`.
- **Queda dos workers:** a máquina reiniciou durante a pausa de limite. Os 4 workers Sonnet (3b, 3c, launcher, instalador) morreram sem relatório, e o trabalho não commitado ficou salvo em `refs/keep/<worktree>-snap1`. Foram retomados nos mesmos worktrees.
- **Modelo dos workers:** Codex **GPT-6.1 Sol high** daí em diante, por decisão do usuário. Exigiu o Codex 0.159.1, porque a 0.158 não conhece o modelo e mostra o nome em minúsculas no rodapé.
- **Tipo de worker:** os quatro workers GPT-6.1 Sol pararam no limite de uso do Codex, todos perto do fim; as árvores estão em `refs/keep/<worktree>-snap2`. A 3b, a 3c e o instalador foram retomados por workers **Claude Sonnet 5.5 high**, e o usuário decidiu que os seguintes voltam a ser Sonnet 5.5.
- **Launcher (`ca400ec`):** o worker deixou o relatório pronto; a revisão, o commit e o merge ficaram com o coordenador.
  - **Abertura:** o launcher abre em todo início normal. `--variant`, regress e selftest continuam indo direto ao jogo.
  - **Desenho:** a tela é desenhada direto no renderer SDL, no tamanho físico da janela. As 4 PNGs vão embutidas no binário: `tools/embed_assets.sh` no Makefile e `visualc/embed_assets.ps1` no VS. Os textos usam a fonte de debug do SDL em escala inteira.
  - **Ordem de início:** a escolha passa pelo provider de dados antes de qualquer asset ou save da variante. Só depois vêm a migração 2b, os saves e `launcher/last_variant` no `opentyrian.cfg` (compartilhado; serve só para pré-selecionar o painel).
  - **Testes:** 10 casos `--regress-launcher` de hash, independentes dos dados, entram no `regress-2000`. `make regress` ficou em 164 casos mais os guards e `regress-2000` em 29 casos, tudo PASS.
  - **Pendências na época:** ligar o instalador (feito logo depois), validar no Windows/VS nativo e no Deck físico.
- **Decisões do usuário:** o 2000 também terá o modo Modern com o HUD novo, e as demos de atração seguem o HUD do modo ativo (Fase 7).

### [2000] Fase 3a: esquemas, carregadores e `regress-2000` (`d1a6bd7`)
- **Worker:** Claude Sonnet 5.5 high, ~1h20, com uma pausa no limite de uso.
- **Esquemas:** `src/game_schema.[ch]` com tabelas `GameDataSchema`/`GameStringSchema` para o 2.1 e o 2000, sem `if` de variante nos carregadores. Os arrays crescem ao máximo das duas versões, e o 2.1 lê os mesmos bytes na mesma ordem. Cobertos: itens com bancos de IDs, textos com as seções finais do 2000, 13 bancos de shapes e o banco extra de naves (IDs >500), 14 imagens/24 paletas, 31 efeitos com as vozes deslocadas, 126 créditos e 5 episódios. Os rótulos semânticos (jogador 1/2, timer) substituem os índices crus de `miscText`.
- **`--variant=2000`:** seleciona uma instalação do 2000 validada, sem fallback por arquivo. Busca padrão: `TYRIAN2000_DATA`, `tyrian2000/` ao lado do executável, `./tyrian2000`. O save do 2000 tem 4.722 bytes em `tyrian2000/`, e os nomes padrão vêm do HDT.
- **Eventos do 2000:** 58, 59, 68, 83, 84, 85 e 99 eram pulados, com log, até a 3b; o 68 deixou de rodar a regra do 2.1.
- **O que rodava:** o 2000 chega ao título e inicia o Episódio 1, e todas as fases do E1–E5 iniciam sem crash no build debug.
- **Testes:** `make regress-2000 TYRIAN2000_DATA=<pasta>` com 19 casos determinísticos (sem alegar fidelidade) e manifesto próprio de 79 arquivos; `make regress` ganhou `check_no_t2000_data.sh`, que recusa qualquer arquivo do 2000 no tree e isenta os 39 idênticos ao 2.1; os 164 casos do 2.1 passam sem mudar baseline.
- **Pendências na época:** tabelas do arcade/Super Tyrian e placares novos (3b); menus 3/12/15 com linhas extras, espaçamento do episódio 5, marca do 2000 no título e heurísticas pic-1/pic-2 (3c); offsets estáticos (`pcxpos`, músicas) que precisam de reset se o launcher trocar de versão no mesmo processo.

### [2000] Fase 3 começa
- **O usuário aprovou os três documentos de `docs/t2000/`** ("Aprove e siga").
- **Divisão da Fase 3:** 3a, esquemas de dados por variante, carregadores e a primeira `make regress-2000` (meta: o 2000 carregar e o Episódio 1 iniciar); 3b, eventos do T2K, substituição de inimigos e regras (sidekicks com carga, rear "None", twiddles) como hooks; 3c, conflitos com os menus e o HUD do Modern.
- **Workers da sessão:** Claude Sonnet 5.5 high, a pedido do usuário.
- **Decisão do usuário (launcher):** as imagens do launcher seriam geradas por um worker Codex GPT-6 Luna high, e só elas; os demais continuam Sonnet 5.5. A referência visual enviada pelo usuário foi registrada com os ajustes aprovados: sem arena, título sem cópia do logotipo, arte na resolução da janela. O launcher foi assim exceção à regra de arte só procedural.

### [2000] Fase 2b: saves por variante e migração (`dec73ff`)
- **Worker:** Codex `gpt-6-sol` high, ~15 min, com uma pergunta respondida: publicar a cópia com `link` + `unlink` no POSIX e `rename` no Windows e em FAT/exFAT.
- **Layout:** na raiz do usuário ficam o compartilhado (`opentyrian.cfg`, `tyrian.cfg` de 28 bytes com detalhe, gamma, teclas, joystick e volumes, `newsh$.shp` e o log); em `tyrian21/` ficam `tyrian.sav` e as demos gravadas (`demorec.N`); `tyrian2000/` reservado.
- **Desvio consciente do `integration.md`:** o `tyrian.cfg` também fica compartilhado, porque só guarda apresentação e controles (decisão 4 do §12 original: configuração compartilhada, saves e progresso separados).
- **API:** `userFileOpenKind`/`userFileExistsKind`, com um tipo compartilhado, save ou demo, em `src/file.c`. `userFileOpen` segue apontando para a raiz.
- **Migração** (só 2.1, antes de `loadSaves`, nunca em regress/selftest): copia o `tyrian.sav` da raiz somente se tiver exatamente 2.502 bytes; grava num temporário exclusivo e publica sem sobrescrever; nunca mexe no original nem num destino existente; de qualquer outro tamanho, pula com aviso; se a cópia falhar, a sessão lê o save da raiz só para leitura e não grava save nenhum, então nunca aparece um save em branco escondendo o antigo.
- **Testes:** `tools/check_user_paths.sh`, dentro do `make regress`, em sandbox com `--regress-user-root`/`--regress-user-files`. Cobre migração byte a byte, repetição, destino existente, tamanhos errados, falhas e nova tentativa, round trip do save, configs na raiz, demos, raízes portable e XDG e isolamento de regress e selftest.
- **Trava:** os 164 casos passam sem mudar baseline (61 s no tree integrado).
- **Para quem atualiza da v0.2.x:** a primeira abertura copia o save. Um binário antigo continua usando o save da raiz, e o progresso passa a divergir entre os dois.
- **Correção no Windows (`d313c43`):** o `stat` abaixo de um arquivo devolve `ENOENT` no Windows e `ENOTDIR` no POSIX, então um arquivo chamado `tyrian21` não ligava o modo só-leitura. A migração agora checa explicitamente a raiz e a pasta do namespace, e o `check_user_paths.sh` diz o passo que falhou. Worker: Claude Sonnet 5.5 high. CI verde nos três sistemas; o Linux arm64 precisou de nova execução porque o runner travou 45 min na compilação.

### [2000] Fase 2a: variante, provider e `--variant=` (`4c7ec1c`)
- **Worker:** Codex `gpt-6-sol` high, ~11 min.
- **`src/game_variant.c`:** descritores imutáveis do 2.1 e do 2000, com nome, rótulo de log, namespace de save, episódios e demos. Selecionar o 2000 devolvia "indisponível" nesta etapa.
- **`src/game_data.c`:** o `GameDataProvider` passou a fazer a busca dos dados, com a mesma ordem de antes (`--data`, pasta do executável/bundle, `TYRIAN_DIR`, cwd). Também faz a recusa do 1.x e do 2000, com as mesmas mensagens, e só abre arquivos para leitura, sem caminhos absolutos nem `..`. `dataFileOpen` passa por ele.
- **`src/bootstrap.c`:** lê `--variant=` e `--data` antes de SDL, config e saves. Valor desconhecido ou conflitante dá erro; os arquivos do usuário são desativados para regress/selftest logo no início; o `JE_paramCheck` aceita as duas opções sem reaplicar.
- **Log:** uma linha no startup com variante, raiz dos dados e status da validação.
- **Testes:** `tools/check_variant_bootstrap.sh`, chamado pelo `make regress`: erros de `--variant`, hashes idênticos com e sem `--variant=2.1`, formas curtas e abreviadas de `--data`, recusa com um cabeçalho de 13 bancos gerado pelo script (sem dado original), ausência de fallback entre pastas e nenhum arquivo de usuário criado.
- **Trava:** os 164 casos passam sem mudar baseline (81 s no tree integrado).
- **Desvios do `integration.md`, aceitos:** o descritor só tem os campos com uso atual; `gameVariantSelect` devolve um status em vez de `bool`; um `--data` repetido continua valendo a última ocorrência, como antes; a reordenação do startup para o launcher ficou para a 2b e a Fase 6.

### [2000] Fase 1 concluída: pesquisa
- **Worker:** Codex `gpt-6-sol` high, retomado a pedido do usuário (~22 min). A tentativa anterior com o Sonnet tinha sido parada antes de qualquer edição, a pedido do usuário. Documentos revisados e trazidos para `docs/t2000/`:
  - `fork-diff.md`: diff próprio do fork contra o merge-base `967c12e` (50 arquivos, +1.292/−476 linhas), 187 linhas de classificação e a evidência de cada diferença de gameplay;
  - `data-formats.md`: formatos 2.1 × 2000, arquivo a arquivo, e quais carregadores nossos quebrariam;
  - `integration.md`: riscos por arquivo, a API C99 da Fase 2 (`game_variant.h`, `game_data.h`, `bootstrap.h`, `user_paths.h`) e a suíte `regress-2000`.
- **Achados principais:**
  - o formato dos arquivos é o mesmo, e mudam as contagens: um segundo banco de armas e inimigos, mais strings, 13 bancos de shapes, 31 efeitos sonoros (as vozes mudam de ID), 14 imagens e 24 paletas;
  - o save do 2000 tem 4.722 bytes, os mesmos 2.502 do 2.1 mais placares sem criptografia. O carregador do 2.1 aceitaria o prefixo e truncaria o arquivo ao salvar, então namespaces separados são obrigatórios;
  - o evento 68 é explosão aleatória no 2.1 e substituição de inimigo no 2000, e tem que ficar atrás da variante;
  - os carregadores de strings, itens, shapes, imagens, sons e créditos quebrariam com os dados do 2000; o guarda em `src/opentyr.c` recusava esses dados antes de tudo;
  - as cores e classes do tag buffer já saem da análise de cada sprite, então não é preciso uma tabela de cores por ID.
- **Correções ao plano:** cinco demos, não quatro, todas iguais às do 2.1; sem cache do Actions na CI (um cache de repositório público pode ser restaurado por PRs de forks); a "correção" do fork no menu de upgrade não se aplica ao nosso código, que já protege o índice; no fim de cada bloco de itens sobram 77 bytes que o fork não lê (em aberto, sem inventar ID); o fork marca os eventos novos e o rastro do Flying Punch como aproximações, então a fidelidade ao DOS precisa de comparação manual.
- **Validação:** build GCC-16 C99 com `-Werror` e os 164 casos passando, sem mudança de baseline. Nenhum dado do 2000 entrou no worktree ou nos documentos, só nomes, tamanhos e checksums.

### [2000] Mapeamento e decisões iniciais
- **Briefing do usuário:** Tyrian 2000 integrado a uma única aplicação, launcher 16:9, instalador que baixa direto da fonte oficial com SHA-256, instalação manual e GOG, saves separados, renderer moderno nas duas variantes e nenhum dado do 2000 distribuído.
- **Pesquisa inicial:** o fork `KScl/opentyrian2000` é uma camada fina (~1.300 linhas em ~50 arquivos) sobre o OpenTyrian, 44 commits à frente do upstream e 19 atrás; o `tyrian2000.zip` oficial (Camanis) tem 5.051.363 bytes, SHA-256 `348bc76e…1667` e 100 arquivos, sem texto de licença (o `readme.txt` é o release note de 1999 da Eclipse/Stealth, e o status freeware vem do anúncio da Camanis); o nosso `EPISODE_MAX` já valia 5 (`EPISODE_AVAILABLE` 4). Pinos candidatos do fork: tag `v2000.20250408` (`573ccd6`) e `master` `aad5aca` (2026-02-22), que traz a mais o `dfe1050` ("Charging sidekicks do not auto-fire in Tyrian 2000"). O fork não tem arena: só `--net` pela linha de comando, nunca testada no 2000.
- **Correção do briefing:** o Modern Tyrian não tem arena multiplayer, então o launcher não pode anunciar uma.
- **Decisões do usuário:**
  - fork no `master` (`aad5aca`);
  - download da Camanis com o hash fixo; instalação manual e detecção do GOG como alternativas;
  - dados do 2000 baixados na CI, conferidos por hash e rodando `make regress-2000`;
  - apresentação e controle compartilhados entre as variantes; saves, high scores e progresso separados;
  - **sempre abrir no launcher, embutido no binário**, inclusive no Steam Deck; `--variant=` só para regressão e automação;
  - trabalho no `modernization`, fase a fase, com o 2.1 sempre verde.
- **Fases do plano:** 1 pesquisa, 2 variante/provider/saves, 3 núcleo do fork, 4 Tyrian 2000 completo, 5 instalador, 6 launcher, 7 renderer moderno no 2000, 8 regressão final.
- **Regras para os agentes desta trilha:** não reescrever o fork inteiro, não copiar arquivos do fork em bloco, não misturar regras do 2.1 e do 2000, não mudar comportamento histórico por preferência, nunca adicionar dados do 2000 ao Git, não presumir que os dados têm a licença do código, não remover nada do 2.1, não espalhar `if` de variante; fazer abstrações compartilhadas, mudanças pequenas, validar dados antes de carregar e manter o downloader fora da engine. Os workers não fazem commit, push nem troca de branch e não editam os arquivos de documentação do plano.

### Trilha Tyrian 2000 aberta
- O usuário passou um briefing para integrar o Tyrian 2000 (launcher, instalador de dados, saves separados, renderer moderno nas duas variantes). A trilha passou a ter plano e diário próprios (depois fundidos neste arquivo).

---

## 2026-09-28

### Teste no Steam Deck; detalhe Pentium fixo no Modern
- **Bugs do analógico achados pelo usuário num Deck LCD real:** a nave só andava em ângulo, com o stick e com o D-pad (o alvo de momento radial era aplicado inteiro a qualquer eixo não nulo, e o ruído de repouso do stick contaminava o eixo cruzado, inclusive no D-pad, que soma no mesmo slot); o movimento mínimo saía aos trancos. O agente `analog2` dividiu o alvo por eixo e fez o movimento lento só com o passo sub-pixel, sem embalo; os testes passaram a usar ruído no eixo cruzado.
- **Analógico no Deck corrigido (`467d25a`):** o alvo de momento é projetado por eixo como o passo, e o stick a até 5° de um eixo encaixa nele (zero de desvio nos 9 casos com ruído, inclusive o D-pad). Abaixo do passo máximo a nave anda só com o passo sub-pixel, com no máximo 1 px de diferença entre ticks. O stick todo inclinado continua idêntico ao original. Regressão com 150 casos.
- **Decisão do usuário:** no Modern, o nível de detalhe fica fixo em Pentium e a opção some do menu, para não oferecer uma versão pior; o Classic mantém a opção e a escolha do jogador.
- **Detalhe Pentium no branch principal (`420984d`):** no Modern o detalhe fica fixo em Pentium (ou SuperWild com a trapaça) e a linha some do menu. O Classic guarda a escolha do jogador em `tyrian.cfg`, e uma instalação nova começa em Pentium. As flags de detalhe são só visuais: o hash de estado é igual nos seis níveis e nas duas apresentações. Os casos Modern da regressão agora rodam em `-d4` (mais um `-d6`), e a suíte ficou com 151 casos. README atualizado (`c24a846`). O merge do branch `pentium` (`bc79b21`) tinha sido abortado no meio por uma pausa do usuário e foi retomado depois (remoção dos baselines Modern `-d2`/`-d3` em conflito, renomeação dos casos `stick-modern-noise`/`crawl` para `-d$MODERN_DETAIL`, `tools/regress.sh --update`).
- **README novo (`67fa07f`):** no estilo do antivirus-95, com banner e cabeçalhos SVG gerados por `docs/readme/generate.py`, capturas regeneráveis por `docs/screenshots/capture.sh` e todas as opções documentadas. Voltou para uma rodada de ajustes: capturas sem "INSERT COIN" e duas afirmações corrigidas (Classic idêntico só com `--starfield-speed=100`; o analógico do Modern muda o movimento).
- **Barra de força com linha azul (`a999780`):** o teto do realce das barras era `base + 15`, e a base da força (113) não é alinhada ao bloco de 16 cores, então a barra cheia chegava ao índice 128, que é azul. O teto agora é o fim do bloco (`base | 15`).

### Steam Deck, controle analógico e modal de Quit no branch principal
- **Steam Deck (`54c411b`).** Detecção do Deck (`SteamDeck=1` ou placa Jupiter/Galileo); na primeira execução abre em tela cheia, no Modern, com aspecto automático (16:10 em 1280×800). O log vai para `~/.config/opentyrian/opentyrian.log` no Deck, e `--log-file` (310) funciona em qualquer sistema. O backend HIDAPI/libusb entrou no SDL3 estático. Guia em [STEAM_DECK.md](STEAM_DECK.md), e um resumo dentro do `.tar.gz`. O pacote segue com os dados do jogo 2.1 (decisão do usuário; a CI já incluía os dados, o item do plano dizia o contrário e o agente `deck` levantou prós e contras). Falta validar num Deck real (feito depois, ver 2026-10-01).
- **Modal de Quit (`quit-centre`).** A caixa é centralizada e a sombra fica pendurada como no original, cortada no limite do painel alargado; em 21:9/32:9 a caixa para nesse limite. O diálogo de nome do recorde tinha o mesmo defeito e foi corrigido junto.
- **Regressão interrompida por sinal.** Durante o merge, um caso terminou no quadro 2210 com saída 0 e sem a mensagem final, e rodando sozinho passou. O SDL transforma SIGTERM/SIGINT em evento de quit, e o jogo saía com `exit(0)`, então o harness via uma saída limpa com o arquivo truncado (provável origem: outro agente encerrando os próprios processos do jogo). Agora a regressão desliga os handlers de sinal do SDL (o harness reporta "killed by signal") e um evento de quit aborta com erro.
- **Movimento suave, etapa 4 (`1092367`).** Fades de paleta e barras do HUD interpolados na taxa do monitor; regressão com 145 casos.
- **Controle analógico (merge `analog`), só no Modern** — pedido do usuário, parte do suporte ao Deck. A primeira entrega voltou para correção: o deslocamento do stick entrava na aceleração (`accelXC` → `x_velocity`) 4× mais forte que no analógico original, e o smoothie de controles invertidos não invertia o stick no Modern. Versão aprovada:
  - zona morta radial de 0–20% (padrão 10%, por controle, `--deadzone`);
  - curva linear até 75% e um alvo de momento escalado pela curva, com velocidade em regime monotônica até os 8 px/tick originais aos 75%;
  - com o stick todo inclinado, a trajetória é idêntica ao analógico antigo; a rede segue consistente porque o accel transmitido continua ±1;
  - botões no padrão Xbox/Deck, todos remapeáveis e persistidos.

  Na diagonal o stick é radial: o máximo fica em 9,7 px/tick, contra 11,3 do analógico antigo, que era mais rápido na diagonal. A regressão ganhou um stick virtual (`--regress-stick`) e ficou com 148 casos. O código já tinha analógico proporcional (`joystick_axis_reduce()` em `src/joystick.c`, que tira o `threshold` e divide pela `sensitivity`, somando no acumulador `mouseXC`/`mouseYC`, limitado a ±30 e aplicado como `(mouseXC ± 3) / 4` px por tick); a curva nova substitui essa redução só no Modern.

### Quatro agentes em paralelo
- O usuário aprovou paralelizar onde possível, em worktrees próprias: `quit-centre` (centralizar a caixa do modal de Quit; a correção anterior centralizou a sombra e a caixa ficou ~18 px à esquerda), `smooth4` (etapa 4 do movimento suave), `analog` (zona morta radial de 0–20%, velocidade progressiva até 75%, botões remapeáveis) e `deck` (prontidão do `.tar.gz` Linux para o Steam Deck: glibc, backends do SDL3, padrões em 1280×800, logs, guia "Add to Steam").
- As partículas de ambiente continuavam sem posição definida na fila.

### Pause, muzzle, HUD de vidro e estrelas a 25%; falha rara da CI resolvida
- Integrados em `modernization`:
  - pausa (P), menu (ESC) e ajuda (F1) mantêm o HUD Modern nas laterais, com "PAUSED" centralizado no campo de jogo; novo caso de regressão abre o menu no último quadro (`7fd7a72`);
  - flash do tiro centralizado no sprite visível do projétil, da nave e dos inimigos (`7f4593d`);
  - barras verticais com gradiente como o `JE_dBar3` do original e painéis laterais de "vidro" (fundo desfocado a 68%–32%) com sombra de 1 px sob o texto (`ab66dae`);
  - estrelas do fundo a 25% da velocidade anterior, com movimento sub-pixel, em Classic e Modern; `--starfield-speed=PERCENT` (10–100) e a chave `starfield_speed_percent` no config ajustam (`0ffde66`).
- **Falha rara da CI no Windows (arm64):** a causa era o canvas Modern alocado com `malloc` e lido antes de ser todo escrito no quadro 0. O macOS entrega páginas zeradas e o Windows não, por isso só o quadro 0 dos casos Modern demo1 divergia, com um hash diferente a cada vez. Confirmado localmente preenchendo o canvas com 0xAA; corrigido com `calloc` (`f9f0782`).
- O caso do menu ganhou um caso de quadros próprio para que `tools/regress.sh --update` gere o baseline dele (`19059cc`).
- **Pedido do usuário:** controle analógico com zona morta configurável e curva progressiva até 75% (entregue, ver acima).
- **Saves do usuário apagados pela regressão (corrigido).** Os casos `--regress-script` jogam uma fase real e chegam ao autosave "LAST LEVEL" do início da fase; como a regressão não carrega os saves, `saveSaves()` gravava a tabela vazia em `~/.config/opentyrian/tyrian.sav`. Cada `make regress` (meu ou dos agentes) apagava os jogos salvos do usuário. Agora regressão e selftest cortam o acesso ao diretório do usuário (`userFilesDisable()`); config e saves nunca são gravados nesses modos.

### Steam Deck no roadmap
- O usuário definiu o suporte a Steam Deck como prioridade (2026-09-28): build Linux x86_64 em `.tar.gz` com o binário direto, sem AppImage/Flatpak, linkado estaticamente com SDL3/SDL3_net e dependendo só da glibc (≥ 2.34), com os backends do SteamOS carregados em tempo de execução. **Decisão do usuário:** o pacote leva o mesmo que o OpenTyrian original distribui, só os dados do Tyrian 2.1 freeware (`tyrian21.zip` da camanis.net via `get_data.sh`) e a licença dele; nada comercial, para evitar problema jurídico. Release gerada pela CI a partir de uma tag. A validar no Deck: 1280×800 (16:10) em tela cheia com os painéis na geometria 16:10, controle nativo sem template do Steam Input, "Add to Steam" a partir do Desktop Mode com passo a passo no README, logs fáceis de achar, desempenho a 60/90 Hz.
- Também entrou no roadmap, para o futuro: filtro CRT com NTSC/Composite (Blargg) e scanlines (entregue em 2026-09-30).

### Lote de correções do teste do usuário; prioridades seguintes
- Integrados em `modernization`: telas Load/Save, Quit e Ship Specs (`b55bcde`); luz só de tiros, explosões, itens e VFX, com retune sutil (`76af276`); WARNING legível, flash em estrela, muzzle alinhado e ordem fixa do RNG dos VFX, que era a causa da CI vermelha (`c4d3b7f`); música parada ao voltar para os logos (`eb8c9b0`); HUD Modern acompanha o fade da fase e intro limpa (`397b5db`).
- A CI de `c4d3b7f` falhou só no Windows arm64, com dois casos terminando sem saída ("first differing line 0"); o commit seguinte passou nas três plataformas (a causa real, o `malloc` do canvas, foi achada no dia seguinte, ver acima).
- Em andamento, em worktrees próprias: estrelas rápidas em ASTEROID/ASTEROID2 (`starfield`), GAME OVER descentralizado no Modern (`gameover`), HUD com vitais à direita e barras verticais (`hud-layout`).
- **Prioridades definidas pelo usuário para depois do lote:** (1) arrumação do plano e a falha rara da CI no Windows arm64; (2) nitidez em Retina (`SDL_WINDOW_HIGH_PIXEL_DENSITY`); (3) fechar o movimento suave (etapa 4); (4) luz com a cor do objeto; (5) acessibilidade no futuro; (6) a posição das partículas de ambiente na fila ficou por confirmar.
- **HUD Modern, layout (pedido do usuário):** inverter os lados, com escudo, armadura e gerador/força no painel **direito**, como no original, e armas e sidekicks à esquerda; as barras desses três ficam **verticais** e um pouco mais compridas. Vale para 1P, 2P e arcade e para 16:10/16:9/21:9; entra depois do merge de `hud-fade`, porque os dois mexem em `src/modern_hud.c`.

### Retomada: escala e VFX no branch principal
- Nenhuma sessão fantasma do opencode depois do reboot. A CI do Windows de `a5aff13` também ficou verde.
- **Escala e níveis de luz (`159c519`, merge `ecf68e0`).**
  - Scalers de software removidos: o quadro é convertido em 1x e a GPU escala.
  - Fit usa sharp bilinear: pré-escala inteira nearest num render target em cache, depois um passe linear. As bordas dos pixels ficam numa grade uniforme (rms 0,04 px, contra 0,28 px do nearest).
  - Modern: sempre Fit com PAR 1,2; Scaling Mode e Pixel Aspect ficam escondidos no menu.
  - Luz: seletor único Off/Low/High, padrão Low. High = o antigo low, Low = metade; `medium` vira alias de high.
  - Custo no renderer de software: +1,9 ms/quadro, o limite superior; na GPU é um quad a mais. Janela real a ~119 fps.
  - No merge, os dois baselines `modern-light-*` mudaram só nas linhas de intro/fade da correção do HUD.
- **VFX parte 1 (`e88167b`, merge `fbd9d80`).**
  - Fumaça e anéis usam o blend/darken de nibble do motor, sem pontilhado.
  - Níveis Off/Low/High, padrão Low; `medium` vira alias de high. Custo com demo2 em Modern 16:9: +0,007 ms/quadro no high.
  - No merge: o menu Graphics ganhou "Lighting" e "Effects" (o item Effects também ficou clicável com o mouse, que faltava). Os `vfx-*` mudaram só nas linhas de intro/fade.
  - O `.filters` do Visual Studio tinha ficado com XML inválido na remoção dos scalers e foi corrigido; `vfx.c/h` entraram no projeto. 128/128 na regressão.
- **A investigar (em aberto na época):** a janela não pede `SDL_WINDOW_HIGH_PIXEL_DENSITY`. Numa tela Retina, o macOS amplia 2x um backbuffer em pontos, então o sharp bilinear roda na resolução lógica.

### Nuked OPL3, correções de tremido e HUD; pausa para desligar a máquina
- **Nuked-OPL3 (`570aa48`, merge `1536c6c`).** Upstream `765ec962` (LGPL-2.1+), em modo OPL2, gerado a 49716 Hz e convertido por um resampler polifásico contínuo. É integer-only, com o LFSR de ruído do próprio Nuked, e o Park–Miller saiu. Custo de ~3% do callback de áudio. Mudaram só as linhas music/mix do `audio.txt`. De quebra, foi corrigido um bug na decomposição de fase do resampler ao reduzir a taxa. CI verde nos três sistemas.
- **Bugs do teste do usuário corrigidos (`d9c9c21`, merge `a5aff13`).**
  - Tremido: a origem contínua do pan (`x − 24·bp`) é interpolada e depois reexpressa contra o mapa do tick atual. O novo `--regress-interp-smoothness` acusava 303 eventos na demo1-d2 e agora acusa 0.
  - HUD clássico piscando: a composição de gameplay fica mantida no intro, nos fades de morte e de fim de demo e na animação de fim de fase. `--regress-gameplay-check`: 0 quadros sem os painéis.
  - As opções novas ficaram em 287–289; 285/286 estão reservadas para o VFX. Os baselines Modern de gameplay mudaram só nos quadros de intro/fade.
- **Pausa (usuário foi desligar a máquina).** Dois trabalhos (VFX rodada 1, em `refs/keep/vfx-wip`; scaling rodada 2, em `refs/keep/scaling-wip`) ficaram salvos em ref com nota de passagem, e foram retomados e integrados na entrada seguinte (merges na ordem scaling → VFX, porque os dois mexem no menu Graphics em `src/opentyr.c` e em `params.c`/`config.c`). Estavam funcionais e verdes; o relatório do VFX estava com o §9 atualizado para 125/125.

### Regressão verde na CI nos três sistemas
- **`b4f42b6`: 123/123 em Linux (x86_64, arm64), macOS e Windows (x86_64, arm64).**
- Três causas de não-portabilidade corrigidas:
  1. CRLF no checkout do Windows: `.gitattributes` com `eol=lf` para `test/regress/**` e os scripts.
  2. Áudio: o resampler do SDL usa float/libm, e o ruído do OPL usava o `rand()` da libc. Agora há um sinc polifásico em inteiros e um Park–Miller local (depois substituído pelo LFSR do Nuked).
  3. Hash de estado: `Player.cash` é `unsigned long` e era hasheado com `sizeof`, que dá 8 bytes no LP64 e 4 no LLP64. Agora todos os escalares vão como little-endian de largura fixa, sem mudar os baselines.
- Todos os hashes de quadro bateram entre plataformas desde a primeira rodada: o desenho do jogo é portátil.
- As varreduras completas (`--replay-check`, `--interp-check`) ficam no workflow manual `regress-full.yml`.

### Iluminação no branch; conversor de áudio; níveis redefinidos pelo usuário
- **Iluminação (`ceadcf3`, merge `6b3d7cc`).** Bloom + mapa de luz dinâmico em `src/modern_bloom.c`, só no playfield dos quadros de jogo do Modern. Na revisão foram corrigidos os halos quadrados (três passes de box, isofotas a ~9% do redondo), a ampliação em blocos (agora bilinear), os núcleos estourados (blend estilo screen) e a força excessiva. Custo < 0,5 ms. As opções viraram 281–284 no merge.
- **Decisão do usuário:** mesmo o "baixo" ficou forte demais. Três níveis, **Desligado / Baixo / Alto**: Alto = o "baixo" de então, Baixo = metade. Um só seletor "Lighting" no Setup → Graphics controla bloom e luz juntos. Padrão: Baixo.
- **Conversor de áudio (`1c16c81`, merge `34093b2`).** Sinc polifásico com janela Kaiser em inteiros, igual em qualquer plataforma. Plano até 4,98 kHz e rejeição > 90 dB (o do SDL deixava passar a primeira imagem a −6 dB). Só os hashes de sfx/mix mudaram. Na mesma resolução de conflito, o `.vcxproj` voltou a ter CRLF (o merge da iluminação tinha convertido para LF).
- CI no Windows: os quadros batem; os 4 casos `state-*` falhavam desde a primeira linha por largura de tipo no hash de estado (LLP64) — corrigido na entrada "Regressão verde na CI".

### Testes do usuário: bugs e decisões de escala
- O usuário testou o binário. Três bugs viraram tarefas, e duas decisões foram tomadas:
  - **Bug: o cenário e as nuvens "pulam" quando a nave se move.** Causa: o pan horizontal segue a nave. O x da linha de fundo dá a volta (`mapXOfs % 24`) enquanto o ponteiro do mapa avança um tile, e a interpolação deslizava ~23 px para o lado errado a cada tile. Tarefa `interp-fix`, com uma checagem de suavidade na regressão (corrigido, ver acima).
  - **Bug: o HUD clássico pisca no início da fase.** Os primeiros quadros saíam como quadro de menu. Mesma tarefa.
  - **Bug: "Fit 4:3" e "Fit 8:5" eram iguais no Modern.** Unificados em "Fit"; a diferença passou para o Pixel Aspect, que passou a valer também no Clássico. Tarefa `scaling`.
- **Decisão:** o pixel aspect correto para a arte é o Original (1,2, desenhada para CRT 4:3). No Modern, o usuário não deve conseguir escolher a pior opção. Pixel Aspect e Scaling Mode saem do menu Modern, e a escala passa a ser sempre "sharp bilinear": pré-escala inteira com nearest e depois o ajuste fracionário final com linear. Isso deixa os pixels uniformes, sem linhas de 4 e 5 px misturadas que tremem no scroll, com aspecto exato. O Fit do Clássico usa o mesmo caminho.
- **Decisão:** os scalers de software (hq2x etc.) são removidos do jogo: "só queremos pixels perfeitos". A janela do Clássico passa a usar o dimensionamento por múltiplo inteiro.

### Áudio: só o emulador Nuked OPL3
- Diagnóstico: saída mono; emulador OPL2 derivado do DOSBox de 2010; mixer com corte duro; efeitos de 8 bits a 11 kHz.
- Na CI, o agente tinha trocado o conversor sinc do SDL por interpolação linear só para o teste de áudio ficar portátil. Recusado na revisão, porque piora o som do jogador. Pedido um conversor polifásico sinc próprio, em ponto fixo e idêntico em todas as plataformas, com qualidade igual ou melhor que a do SDL e resposta de frequência medida.
- **Decisão do usuário:** das melhorias propostas (estéreo posicional, Nuked OPL3, limitador suave e trilha OGG fornecida pelo usuário), só o **Nuked OPL3** (emulação de referência do chip, LGPL-2.1+, compatível com a GPL-2+). As outras três não serão feitas.
- Ordem: o Nuked começa depois do conversor sinc no branch. Ele gera o som em 49716 Hz e precisa desse conversor, em modo contínuo, para chegar a 44,1 kHz.

### Telas S2 entregues; primeira CI com regressão; referência vira o zip oficial
- **S2 (`2b27f5e`, merge `38ab9f1`).** No Modern, jukebox, ship specs e créditos desenham numa superfície de 8 bits da largura do canvas (`modern_screen_begin()`), com as rotinas do próprio jogo. A projeção das estrelas do jukebox só lê o estado: não gasta RNG nem escreve estado.
  - Mapa de navegação: a primeira versão (carta estelar larga atrás da moldura centralizada) foi recusada na revisão, porque a tela pulava de lugar em relação às outras da loja e mostrava grade à direita do painel, sem sentido espacial. Ficou com o painel alargado do S1.
  - O que bloqueava o alargamento era a margem direita que o mapa pinta (x 314..319). O corte agora ignora elementos além da última coluna de corte possível (311), e um preenchimento de altura inteira na faixa de baixo acompanha a borda.
  - Na revisão também saíram: nave do ship specs deslocada duas vezes (cobria o texto); texto dos créditos centralizado na área preta, à esquerda da arte; estado do teste do mapa (paleta 18 e lista de fases).
  - O simulador de armas continua como no S1: alargar a janela exigiria refazer o layout da loja.
  - 5 telas novas no `--regress-screen` e 12 casos; a regressão ficou com 121.
- **Manutenção (`7909049`).** O menu Setup → Graphics ganhou Presentation, Aspect, Pixel Aspect e Smooth Motion; antes, o Modern só era alcançável pelo cfg ou pela linha de comando. As opções valem na hora e ficam esmaecidas no Clássico. O Modern abre uma janela no formato escolhido, a ~80% da área útil da tela. O README documenta todas as opções. A trava de dados (`test/regress/data-manifest.txt`, tamanho + `cksum` POSIX) recusa dados diferentes e diz qual arquivo não bate. O projeto do Visual Studio voltou a listar todos os fontes, sem compilação testada. Regressão com 109 casos.
- **Descoberta:** o zip oficial baixado por `./get_data.sh` tem `newsh9.shp` com 38831 bytes; a cópia de referência local tinha 34888 bytes, idêntica ao seu `newsh^.shp`. Por isso um clone novo não conseguia rodar a regressão, e a CI nunca rodou `make regress`: só compilava.
- **Decisão do usuário:** a referência dos testes passa a ser o zip oficial do `get_data.sh`, e a CI passa a rodar a regressão (worktree `regress-ci`: trocar o manifesto, regenerar só os baselines afetados com prova de que a diferença vem do `newsh9.shp`, criar os jobs de CI com cache dos dados — depois o cache foi removido para o 2000 e a trava passou a conferir o zip oficial).
- **Primeira CI com regressão (`bdfc0e0`):** macOS verde. No Linux (x86_64 e arm64), só o caso `audio` falhava, com todos os quadros e estados batendo com os baselines gerados no Mac. No Windows, a trava de dados recusou tudo, provavelmente por CRLF no checkout.

### 60 fps ou mais: etapas 1–3
- **Etapas 1–2 (`f35198f`, merge `f7535d7`).** Lista de desenho por tick no nível das primitivas, com identidade de objeto, e replay com prova de igualdade byte a byte (82/82 casos; ~0,17 ms por quadro). No merge, `--regress-replay-check` virou a opção 275, ao lado de `--regress-screen` (274). A regressão ficou com 103 casos.
- **Etapa 3 (`09441a8`, merge `b9b65da`).** Modern em jogo apresenta na taxa do monitor com movimento interpolado. Na espera do tick fixo (`delayUntilElapsed` em `JE_starShowVGA`, agora `interp_present_gameplay()` em `src/interp.c`), o jogo apresenta quadros a `alpha = decorrido / período do tick`, com vsync. Sem vsync, dorme até o próximo refresh; nunca gira. O deadline continua na grade absoluta do tick.
  - Medido numa janela real neste Mac (painel de 120 Hz, embora `SDL_GetCurrentDisplayMode` reporte 60): **~120 fps** apresentados, p99 de 9–9,5 ms, lógica a ~34,8 ticks/s, igual ao original. Custo de ~1,1 ms por quadro apresentado em 16:9.
  - Casamento por identidade + ordem dentro da identidade, com índice hash fixo. Snap em nascimento, salto > 64 px e troca de sheet (reuso de slot). As linhas de fundo casam pela linha do mapa com o pan removido (`row_key`), então o scroll não salta na virada de tile (a identidade da etapa 1, camada e linha de tela, saltava 28 px quando o mapa avançava uma linha de tiles). Starfield e superpixels são interpolados por índice. HUD, textos e barras de chefe usam o tick atual.
  - O renderizador mantém uma referência do quadro do tick: os filtros de água, lava, blur e iced misturam com o destino e partem dela. A 120 fps esses filtros são aplicados sobre quadros intermediários; o quadro do tick continua exato.
  - Provas: `--regress-interp-check`, com alpha = 1 byte a byte igual ao quadro real em 52 casos e 0 overshoots. `--regress-interp-alpha=A` captura quadros intermediários, e `--regress-realtime` mede o ritmo. Regressão com 107 casos.
  - Opção `smooth_motion` no `[video]` do cfg (ligada por padrão) e `--smooth-motion=on|off`. Só vale no Modern.
  - Lição de processo: depois de um `worker_done`, a correção da revisão tem que ir por `worker-start --terminal` no mesmo terminal. Um `terminal send` simples funciona, mas o `worker_done` seguinte é rejeitado por capacidade revogada, e só a tela confirma o término.

### Telas fora do jogo: S1 entregue (`296cf7e`)
- Título e menus da pic 2 com Vert-; todas as telas da moldura da loja (menu do jogo, upgrade, compra, opções, data cubes, teclado, joystick, load/save) com o painel direito alargado; pic 5/11 com borda sólida. Nenhuma tela da pic 1 caiu no desfoque.
- As colunas de corte fixas da primeira tentativa (158/310) bloqueavam quase todas as telas: a linha de ajuda do rodapé cruza a tela inteira e o leitor de data cube vai até x=310. A solução foi um corte único, decidido pelo conteúdo: logo depois do elemento mais à direita do painel (y < 184), com a faixa de baixo tratada à parte (o fundo acompanha, o texto de ajuda fica no lugar).
- Efeito colateral aceito: o título da caixa ("Game Menu") não é recentralizado, porque um segundo corte à esquerda cortaria a linha de ajuda. A faixa de destaque do item selecionado termina onde o painel original acabava.
- `--regress-screen=NAME` renderiza 13 telas sem janela com as funções do próprio jogo (em modo tela, `hasInput()` diz que há entrada, e o limite de quadros encerra). São 28 casos novos: Modern 16:9 e 21:9, mais os Clássicos, que provam que o harness não altera as telas de 8 bits. A regressão passou a ter 99 casos.
- Mouse: o mapeamento por partes foi verificado por uma checagem temporária (a faixa inserida cai na coluna de corte, dentro da área clicável dos itens).
- A navegação por teclas simuladas não funcionou nesta máquina, daí o `--regress-screen`.

---

## 2026-09-27

### Retomada depois do desligamento; pausa para desligar
- **Pausa:** o S1 das telas foi interrompido de propósito antes do desligamento para o serviço do opencode não retomar a sessão sozinho. O trabalho ficou sem commit no working tree (`src/modern.c/.h`, `video.c/.h`, `picload.c`, `keyboard.c`, `mouse.c`), com backup em `refs/keep/screens-s1-wip` (stash `507e703`) e nota de passagem em `.worker-reports/phase1-screens-s1-handoff.md`. A CI estava vermelha no Linux e no Windows por falta de `#include <stdio.h>` em `src/modern.c`; a correção entrou junto com o S1. Ao retomar: conferir que nenhuma sessão opencode ficou ocupada, passar o S1 para um agente novo e remover os ganchos de teste, com atenção a eventos de teclado injetados em `keyboard.c`/`mouse.c`, que ficam fora do escopo combinado.
- **Retomada:** nenhuma sessão fantasma. O S1 foi para um agente novo com a nota, no checkout principal, que também criaria o `--regress-screen`. O usuário pediu prioridade para 60 fps ou mais e quer muito ver a iluminação melhorada. Em paralelo, em worktrees separadas: `framerate` (design e etapas 1–2) e `lighting` (bloom mais mapa de luz dinâmica a partir dos pixels emissivos da paleta; tiros, explosões e chamas iluminam o terreno com a própria cor; luzes por objeto viriam depois, alimentadas pela lista de desenho). As telas S2 esperaram o S1, porque usam o mesmo compositor.

### Só o HUD novo na partida; desfoque nos menus
- Nos quadros de jogo com painéis, o compositor copia só o playfield de 264×184. A barra lateral e a faixa de baixo originais continuam sendo desenhadas no quadro de 8 bits (lógica intacta), mas não entram no canvas. Os painéis ficam com 81/82 px em 16:9, e 16:10 passa a ter painéis (60 px). Os 16 pixels sob o playfield viram uma faixa com o nome da fase e a mensagem do jogo, que chega por um gancho só de leitura em `JE_drawTextWindow` e no apagamento da mensagem. O HUD ganhou a barra de energia das armas (`PWR`), o único dado vital que só a barra original mostrava.
- As telas fora do jogo trocaram o ambilight pelo desfoque da própria tela.
- Revisão: inventário barra→HUD completo, capturas conferidas (1P e 2P em 16:9, 16:10, 21:9, arcade, telas reais de título e menus), ganchos conferidos (o `--textErase` continua avaliado igual), auditoria GCC limpa, `make regress` 71/71 duas vezes. Os `state-*` e os baselines Classic e 4:3 não mudaram.

### Reboot, sessão fantasma e HUD completo (etapa 2) — `5a5952b`
- O Mac reiniciou no meio das correções da etapa 2 e derrubou o Orca, o agente e o scratchpad. Um agente novo terminou as correções: vidas e dinheiro em linhas separadas, nomes com espaços sobrando aparados antes do corte, a reserva do ícone especial só no 2P, e o caso `state-scenario-spotlight-2p-d3`, o primeiro que roda o bloco de vidas, e portanto as escritas em `tempW`.
- Falhas intermitentes na revisão ("no output written" em demo1-d6, divergência em iced) vinham de uma sessão fantasma disputando o `test/regress/actual/` (ver o achado técnico "sessões opencode voltam sozinhas" em [MODERNIZATION.md](MODERNIZATION.md)). Com ela interrompida: 70/70 duas vezes, auditoria GCC limpa. A sessão fantasma também revelou que a falha antiga do quadro 855 era o `data/` errado.
- O usuário aprovou o visual ("bem bonito"), mas não quer o HUD original e o novo juntos (decisão 2026-09-27: só o HUD novo no Modern; vidas seguem o original — só em arcade e 2P — e escudo e armadura ficam com barra e número).

### HUD modernizado, etapa 1 entregue
- O agente da worktree `hud` desenha os elementos que ficavam dentro do playfield em duas superfícies de 8 bits fora da tela, uma por painel, e o `modern_build_frame` compõe essas superfícies sobre o ambilight. A largura mínima de 51 px vem da barra de chefe. Em 16:10 original (32 px) e em 16:9 square (18 px), tudo continua dentro do playfield, para não deixar um HUD pela metade.
- Prova: os fluxos de hash de estado de demo 1, demo 3 e spotlight são byte a byte iguais entre Classic e Modern 16:9 com o HUD movido. Isso virou os 3 casos `state-*`. Só os 3 baselines `modern-wide-*` mudaram, como esperado.
- Revisão: diff conferido (todas as escritas em `tempW` preservadas; a atualização da barra de chefe continua separada do desenho), capturas 1P real e 2P com barras de chefe (sonda headless). Depois do merge: build ok, auditoria GCC limpa, autoteste do gamepad ok, `make regress` 69/69.
- Para a etapa 2: em 16:9 os painéis tinham só ~53 px, então cabia um layout de uma coluna; em 21:9 cabe mais. Nomes longos de jogador em rede precisam de truncamento. O P2 deveria ficar alinhado à borda externa.
- **Preparação:** o desenho do HUD tem efeitos colaterais na lógica (`JE_inGameDisplays` escreve na global `tempW`, que o pan lê; `draw_boss_bar` atualiza `boss_bar[]` enquanto desenha). A tarefa criou a prova permanente `--regress-state-out`, um hash do estado do jogo por quadro.
- **Decisão (aceita pelo usuário):** o espaço lateral do widescreen vai para HUD, em duas etapas — (1) elementos que ficavam dentro do playfield nos painéis; (2) painéis completos com nomes e nível das armas, munição e carga dos sidekicks, escudo e armadura numéricos e layout 2P, com protótipo e capturas para aprovação. Tudo na grade de 320×200, com as fontes e os sprites do jogo e molduras procedurais.

### Harness imune a input real
- O agente fechou o vazamento na fronteira de entrada: `handleSdlEvents` descarta tudo menos `SDL_EVENT_QUIT` no modo regress; `poll_joysticks` fica inerte; `windowHasFocus` fica fixo em true (o build de release pausa sozinho sem foco); `SDL_HINT_MAC_BACKGROUND_APP` é ligado antes do `SDL_Init`.
- Nesta sessão o `osascript` não conseguiu mais entregar teclas ao processo, que nunca virou frontmost. A prova usou um injetor temporário dentro do processo: uma tecla Up via `SDL_PushEvent` no quadro 855 do cenário flip. Sem o descarte, 3/3 divergem a partir do quadro 858; com o descarte, 3/3 batem com o baseline.
- Revisão: diff de 4 arquivos, build ok, auditoria GCC limpa, autoteste do gamepad ok, `make regress` 66/66.
- Observação: o projeto do Visual Studio em `visualc/` não listava `modern.c` nem `regress.c` desde a Fase 0 (corrigido na manutenção `7909049` de 2026-09-28); a CI do Windows usa MSYS2 e não era afetada.

### Gamepad integrado
- A branch `gamepad` entrou em `modernization` no merge `85f741a`. Os conflitos foram só de adições: as duas seções do README ficaram, e `--selftest-gamepad` virou a opção 269, depois das opções do widescreen (266–268). Depois do merge: build ok, auditoria GCC limpa, `--selftest-gamepad` passou e `make regress` fechou 66/66.

### Widescreen e PAR entregues (Fase 1a); visão estendida descartada
- O agente de widescreen entregou os ajustes `aspect` e `pixel_aspect` do modo Modern (`b1e4313`). O canvas tem largura `round(200 × PAR × aspect)`, com o quadro original centralizado e laterais "ambilight" procedurais, determinísticas e escurecidas. Durante o jogo elas usam a coluna 263 do playfield, pulando o HUD. O mapeamento do mouse leva em conta o deslocamento do quadro, e o custo é de ~65–72 µs por quadro em 16:9.
- Na revisão: diff lido, capturas de tela conferidas (4:3 e 16:9, original e square), `make regress` com 66/66 (os 63 antigos com baselines intactos, mais 3 `modern-wide-*` em 16:9) e auditoria GCC 16 limpa. O agente encerrou o turno antes de terminar e foi retomado (técnica de destravar workers opencode registrada no CLAUDE.md global).
- Pendências menores anotadas pelo agente: no modo Integer, com PAR 1,2, uma janela pequena (640×400) fica com escala quase quadrada; e a janela ainda segue o tamanho do scaler Classic. (Resolvidas na reformulação de escala de 2026-09-28.)
- **Visão estendida descartada pelo usuário** depois das medições (próxima entrada): das três opções (só fundo, fundo com objetos, desistir), escolheu desistir. As laterais do widescreen ficam só com o preenchimento procedural.

### Visão estendida medida
- Um agente numa worktree separada instrumentou o renderizador de 8 bits (depois reverteu tudo), varreu o pan inteiro e rodou as 62 fases dos 4 episódios mais as 5 demos. Relatório completo: `.worker-reports/phase1-extview-investigation.md`.
- **Resultado:** dá para mostrar ~24 px de arte real por lado, com a direita mais pobre (64 % coberta em média). Mostrar também os inimigos nessas faixas esbarra no portão de desenho de `JE_drawEnemy` e exigiria alargar o framebuffer das fases. Mostrar objetos ali daria ao jogador informação que o original não dava.
- **Números:** cada camada de fundo é desenhada como uma janela de 12 tiles (288 px) deslizando sobre um mapa de 336 px (bg1/bg2) ou 360 px (bg3). Somando todo o range do pan, há arte em x ∈ [-45,315) na bg1, [-41,342) na bg2 e [-69,369) na bg3, com o playfield em [24,288). A margem garantida em qualquer pan é assimétrica: ~45 px à esquerda e só 3 px à direita (7 px onde há bg2/bg3 densa). Seis fases não têm fundo nenhum (E1:L1, E2:L5, L7, L8, L9 e E4:L14). Com 24 px por lado, a faixa esquerda tem em média 94 % de arte real e a direita 64 %. Inimigos já simulados aparecem nessas faixas em 11–21 % dos quadros, mas `JE_drawEnemy` só anima e desenha inimigos em x ∈ (-29,300), o que limita a revelação à direita a ~12 px, e tiros somem visivelmente na borda direita em ~1,5 % dos quadros. Dois cuidados para qualquer implementação futura: `blit_sprite2*` usavam o `VGAScreen->pitch` global em vez do pitch da superfície (`sprite.c:559,636,672,708`, corrigido depois), e `draw_background_2/3` avançam o scroll dentro da função de desenho, então não podem ser chamadas duas vezes por quadro.

### Gamepad entregue; harness vulnerável a input real
- O agente do gamepad (worktree `gamepad`) caiu uma vez por HTTP 400 do provedor e foi retomado. Entregou a API de Gamepad do SDL3 com mapeamento padrão, hot-plug e remapeamento por nome no cfg; o caminho legado de joystick não mudou. Aprovado; commit `1a33820` na branch local `gamepad`; autoteste `--selftest-gamepad` com gamepad virtual (51 checks). A integração ficou para depois do widescreen, porque os dois mexem em `README.md`, `params.c` e `opentyr.c`.
- Na verificação apareceu falha intermitente no cenário `flip` (5 casos, por volta do quadro 855), atribuída a input real injetado pelo `osascript` das capturas do agente de widescreen; isolado, passava 3/3. Depois se mostrou que a causa provável da falha era outra: o `data/` do repositório tinha um `newsh9.shp` diferente do Tyrian 2.1 oficial, que diverge exatamente no quadro 855 do cenário flip, quando esse sprite aparece. A proteção contra input real continua valendo como endurecimento.
- Os eventos de hot-plug do SDL3: `SDL_EVENT_GAMEPAD_ADDED/REMOVED` chegam mesmo com os eventos desabilitados, mas `SDL_EVENT_JOYSTICK_ADDED/REMOVED` não; um gamepad gera os dois pares, então é preciso deduplicar por instance id.

### Pergunta sobre ampliar o campo horizontal
- O usuário perguntou se dá para aproveitar o scroll horizontal nativo para mostrar mais campo no widescreen. Análise: o "scroll" é só o parallax do fundo (camada da frente ~70 px, meio ~47, fundo ~24), os mapas têm 336–360 px, e tiros e inimigos têm limites de remoção próximos da borda original (inimigos existem de -80 a 340; tiros inimigos somem em x ≤ 0 / > 275 e os do jogador em x < -34 / > 290). Só ~20–24 px por lado são viáveis sem inventar arte nem mudar o gameplay. A decisão inicial foi seguir com a visão estendida nesse limite (substituída depois das medições: descartada). Mudar limites de despawn para ampliar mais estava fora de escopo, porque alteraria o balanceamento.
- **Splash screens e menus:** o usuário perguntou se dava widescreen em splash e menus. Resposta: sim, preenchendo as laterais com uma cópia ampliada, desfocada e escurecida da própria tela; reorganizar cada menu para usar a largura exigiria refazer tela por tela, com ganho pequeno. O usuário preferiu depois conteúdo real onde viável (Vert-: ampliar a imagem até a largura e cortar em cima e embaixo, ~25 % da altura em 16:9; autorizado em splash screens, tela principal e menus do jogo), com um inventário decidindo tela por tela entre Vert-, extensão real/reorganização e desfoque. O inventário (`.worker-reports/screens-inventory.md`, ferramenta `tools/dump_screens.py`) mostrou que nenhuma imagem tem arte além de 320 px: o Vert- funciona no título (o corte leva só céu e a parte de baixo do planeta, e logo e menu são camadas separadas) e é aceitável na pic 2 dos menus; na pic 1 (loja, armamentos, dados) quebra a moldura e desalinha as janelas, nos logos corta arte e na história corta arte única. Mapa de navegação, ship specs, starfields e créditos ganham extensão real. A pic 2 perde a linha "AN EPIC MEGAGAMES PRODUCTION ©1994" embutida no rodapé. Para a pic 1, o usuário preferiu alargar o painel liso da direita repetindo uma faixa de colunas internas dele (arte original em 1×, sem desfoque; é só composição, o jogo segue desenhando 320 px), com a moldura da nave à esquerda.

### CI verde nas três plataformas; Fase 0 concluída
- O Actions foi disparado manualmente pela API (em forks o push não disparava na primeira vez; depois passou a disparar). Foram 4 rodadas até ficar tudo verde: (1) `libxtst-dev` faltando no Linux e banco MSYS2 desatualizado sem `sdl3-net` no Windows (`108e69a`); (2) `palette.c:87` (ponteiro-para-array `const`), que o GCC rejeita com `-pedantic` e o clang aceita; uma auditoria com GCC 16 nos 55 arquivos confirmou que era o único; (3) `PKG_CONFIG_PATH` só no passo de compilação, o que quebrava `make install`; (4) verde em Linux x86_64/arm64, macOS e Windows x86_64/arm64 (`e5a6d11`).
- Lição: auditar com `gcc-16` e as flags da CI toda mudança em `src/`. Complemento: a auditoria no macOS também não pega header padrão faltando (ex.: `snprintf` sem `<stdio.h>`), porque os headers do SDL no macOS já puxam esses headers; só a CI em Linux/Windows pega, então empurrar cedo e olhar a CI.
- **Fase 0 concluída.** A Fase 1 começou em paralelo: widescreen/PAR no checkout principal e gamepad numa worktree local.

### Pipeline moderno na CPU (Fase 0d) e primeira CI real
- Um agente novo entregou o pipeline moderno e o setting Classic/Modern, aprovado na primeira revisão. Os 63 casos passam. Sem passes, o Modern sai byte a byte igual ao scaler "None" do Classic; custo ~37 µs por quadro. Verificado com janela real em modo Modern. O contrato dos passes (`src/modern.h`): `void pass(ModernFrame *)`, rodam na ordem de registro, uma vez por quadro apresentado, sobre o canvas na grade lógica; determinísticos, sem RNG, sem tocar estado de jogo; podem ler `src` (índices de 8 bits) e `palette` e escrever `pixels`.
- 1ª rodada da CI: macOS verde; Linux quebrou por falta do `libxtst-dev` e Windows por um banco MSYS2 desatualizado sem `sdl3-net`. 2ª rodada: macOS e Windows arm64 (clang) verdes; Linux e Windows x86_64 (GCC) falhavam no `palette.c:87`. (Ver a entrada acima.)

### Decisão: composição moderna na CPU, sem backend GPU próprio
- O "backend GPU com paleta no shader" foi substituído por um pipeline de composição na CPU (decisão técnica do coordenador, revisável). Como todos os efeitos ficam na grade de 320×200, o custo de luz, bloom, partículas e widescreen na CPU é trivial: 64 mil pixels por quadro, bem menos de 1 ms. Fazer na CPU mantém tudo portátil, sem shaders para Metal/Vulkan/D3D, e determinístico, então a saída do Modern pode ser coberta por baselines de hash como o resto. O `SDL_Renderer` do SDL3 continua só para subir a textura e escalar com nearest. Se algum efeito exigir resolução de tela no futuro, reavaliamos.
- **Decisão do usuário (quadros acima de 35 Hz):** o usuário quer ao menos 60 fps, de preferência o refresh da tela, e a lógica não pode depender da taxa de quadros. Lógica no tick fixo original, desenho por interpolação a partir de uma lista de desenho gravada por tick (Fase 2), logo depois das telas de menu.

### CI e empacotamento em SDL3 (Fase 0c-3b) — `55baaae`
- Um agente novo migrou `make_macos.sh` (frameworks oficiais SDL3 3.4.16 e SDL3_net 3.2.0, fixados por SHA-256, app universal), `make_linux.sh` (SDL3 estático compilado do código-fonte, fixado e verificado), os 3 workflows, o projeto MSVC e o README. Aprovado na primeira revisão.
- Validado localmente: o app gerado é universal (x86_64 + arm64), carrega os frameworks de dentro do bundle (nenhum caminho do Homebrew), abre sem janela e chega à tela de título; `codesign --verify` passa; 53/53 no teste de regressão.
- O GitHub Actions nunca tinha rodado neste fork (0 runs, o padrão em forks); Linux, Windows e MSVC só seriam validados depois que o dono ligasse o Actions.

### Rede em SDL3_net (Fase 0c-3a)
- Um agente novo portou `network.c` para o SDL3_net com o mesmo protocolo, aprovado na primeira revisão. Duas instâncias headless em 127.0.0.1 completam o handshake CONNECT/ACK. O lock-step em jogo não foi exercitado, porque precisa de input dos dois lados.
- Mudança de comportamento intencional: um host que não resolve agora gera erro após 10 s; antes o jogo travava em silêncio.
- No mesmo pacote entraram os dois ajustes de áudio pendentes: buffer estático no callback e hint de 1024 frames. Os 53 casos continuam passando.
- Atenção: o `sdl3-net.pc` do Homebrew vem com `prefix=` vazio. Por isso o Makefile pega os caminhos de `sdl3` e só os nomes de biblioteca de `sdl3-net`, o que pode falhar em sistemas com os dois pacotes em prefixos diferentes, como a build estática do Linux. Instalado nesta máquina: `brew install sdl3_net` (3.2.0).
- Havia 38 chamadas `SDLNet_Read16/Write16` espalhadas por `mainint.c`, `tyrian2.c` e `game_menu.c`; hoje usam `network_read16/write16` (`src/network.h`). O protocolo é UDP ponto a ponto, 2 jogadores, em lock-step com o loop do jogo; o SDL3_net não tem bind, então o filtro por endereço e porta do oponente é explícito.

### Núcleo migrado para SDL3 (Fase 0c-2)
- Um agente novo portou os 44 arquivos para a API nativa do SDL3, aprovado na primeira revisão. Os 53 baselines passam sem regeneração, inclusive o áudio byte a byte (`SDL_ConvertAudioSamples` produz os mesmos bytes que o `SDL_AudioCVT` do sdl2-compat).
- Pontos de atenção tratados: checagens de erro invertidas (`bool`), `SDL_GetTicks()` truncado para Uint32, escala nearest explícita, text input por janela, fullscreen desktop via `SDL_DisplayID`, áudio via `SDL_AudioStream`, e drivers headless com os novos nomes (`SDL_VIDEO_DRIVER`/`SDL_AUDIO_DRIVER`).
- Verificado com janela real no macOS: a tela de título aparece com pixels nítidos e a paleta correta; o teclado navega nos menus; Alt+Enter alterna o fullscreen, ida e volta. Ainda não verificado de forma manual na época: digitação de texto (nome do high score e do save), mouse, joystick e o som num dispositivo real.
- A CI estava quebrada nesta branch até a Fase 0c-3, porque ainda instalava SDL2.

### Baseline de áudio entregue (Fase 0c-1)
- Um agente novo entregou `--regress-audio`, aprovado na primeira revisão. O callback de áudio foi extraído sem mudanças para `audio_mix()`, que o harness chama diretamente, sem abrir dispositivo. Os testes negativos (volume, taxa de conversão) quebram exatamente as linhas esperadas.
- A migração SDL3 foi dividida em três tarefas sequenciais: (1) baseline de áudio; (2) núcleo; (3) rede, CI e release.
- Achados: o pipeline de áudio é mono, S16, 44100 Hz (`11025 × OUTPUT_QUALITY`), buffer de 1024 amostras; a música é um emulador OPL mais o player LDS em C puro, sem SDL; os efeitos são 29 em `tyrian.snd` e 9 vozes em `voices.snd`, em 8 bits a 11025 Hz; a única reamostragem acontece em `loadSndFile()` (`src/nortsong.c`); a percussão do OPL usava `rand()` da libc, não `mt_rand`. Os baselines dependem da libm e do toolchain da máquina (`powf`/`pow`/`sin`, `cosf`/`sinf`), então valiam para esta máquina (macOS arm64, clang) até a CI ganhar baselines portáteis (ver 2026-09-28).

---

## 2026-09-26

### Cobertura dos smoothies entregue (Fase 0b)
- Um agente novo entregou 5 cenários sintéticos que cobrem os 6 caminhos de renderização que as demos não exercitavam; aprovado na primeira revisão (commit em seguida ao `d09f352`). Verificado: os 52 pares passam (~34 s), não há caminho fixo nos arquivos, e o scanner roda. O teste negativo (constante alterada em `water_filter`) derruba só os 4 cenários de água.
- Achados: as demos (`demo.1`–`5`) nunca ativam `smoothies[]`; mapa das fases que ligam cada smoothie (lava em E1/L16 e várias do E4; água em E1/L17 e muitas do E4; blur e iced blur só no E4; holofote em E1/L15 e E1/L16; flip vertical em E4/L12 e E4/L13; bits 7 e 8 nunca usados); os eventos de salto (tipo 54) e saltos condicionais (61/66/70/71) tornam a varredura estática não confiável (`tools/scan_smoothies.py` lista candidatos, só a execução confirma); `lvlPos` guarda duas entradas por fase e a última do episódio 4 é o bloco de itens. Sem invencibilidade, o jogador morre em 358–1185 quadros, antes da maioria dos eventos.
- **Incidente do coordenador:** um `git commit -a` para ajustar o plano publicou o trabalho do agente ainda sem revisão. Desfeito com reset e `--force-with-lease`. Regra adotada: só adicionar arquivos por caminho explícito.

### Decisões: efeitos na resolução original e arte nova só procedural
- O usuário decidiu que os efeitos modernos (VFX, luz, bloom) ficam na mesma resolução do jogo original, a grade de 320×200, calculados lá e ampliados com o mesmo scaling dos sprites. Isso removeu o risco de "mistura de resoluções" e a decisão estética prevista para a Fase 2.
- O projeto não terá artista: arte nova só se for gerada por código (ruído, gradientes, derivação/recoloração/composição dos sprites e tiles originais, partículas, shaders), na grade de 320×200. Nada desenhado à mão, nada de sprites redesenhados em alta resolução. Com isso, camadas extras de parallax (névoa, poeira, starfields, versões desfocadas ou escurecidas dos tiles existentes) viram viáveis. Exceção posterior: a arte dos painéis do launcher do 2000 (2026-09-29).
- Não restou nenhuma decisão em aberto.

### Teste de regressão entregue (Fase 0a) — `682abeb`
- O agente entregou o harness em duas rodadas. Na revisão da 1ª rodada foram devolvidos dois problemas: (1) ele tinha movido `JE_paramCheck()` para antes de `loadConfiguration()`, o que quebrava `--xmas`/`--no-xmas` fora do modo de teste; (2) com `processorType` fixo em 2, lava, água e blend `wild` ficavam de fora. A correção restaurou a ordem original (com um pré-scan dos argumentos) e passou a varrer os níveis de detalhe 1–6.
- Verificado: os 30 pares (demo × nível) passam, não sobrou sonda temporária no código, e o teste negativo (1 pixel alterado) falha em todos os pares a partir do quadro 51.
- Achados: o timing depende de `getFrameCount2Ticks()` como acumulador de fase (animação de aviso em `fonthand.c:260` e de respawn em `mainint.c:2410`); os arquivos de usuário ficam fora do diretório de dados; o padrão de `processorType` é inconsistente (sem config o motor usa 3, mas `JE_initProcessorType()` documenta 2; os níveis 2, 3 e 5 dão saída idêntica nas demos, porque `smoothScroll` é forçado para true depois).

### Início da Fase 0
- Decidido: Fase 0 aprovada, backend SDL3 (API de GPU/renderer com shaders e API nova de gamepad; SDL3 3.4.16 já instalado via Homebrew), e o fluxo de trabalho (agentes OpenCode com DeepSeek V4.1 Flash via Orca; o coordenador revisa, faz commit e push). A ordem da Fase 0 põe o teste de regressão por demos antes da migração para SDL3, porque é a rede de segurança dela.
- Branch `modernization` criada e publicada em `origin`. Orca run `run_e0cad877e2fd`; primeira tarefa: teste de regressão headless por demos (task `task_9f4f56a107b2`).
- Descoberta: o Homebrew desta máquina fornece `sdl2-compat` 2.32 sobre SDL3 3.4.16, ou seja, o build já rodava sobre SDL3 pela camada de compatibilidade; SDL2_net não estava instalado, então o build saía sem rede. As demos usam semente fixa (`src/demo.c:42`, `mt_srand(32402394)`), candidatas naturais a teste determinístico.

### Análise de viabilidade
- Analisado o código do OpenTyrian contra a proposta de modernização (renderização, loop principal, timing, RNG, backgrounds, HUD, input).
- Conclusão: viável como camada de apresentação, desde que se criem primeiro a fronteira gameplay/render (snapshot, eventos e tag buffer) e o teste de regressão por demos. A fronteira "gameplay × renderização" que a proposta pressupunha não existia no código.
- Descobertas relevantes (detalhadas em [MODERNIZATION.md](MODERNIZATION.md), "Notas técnicas do código"): renderização 100% em software, 8 bits indexado, 320×200 (`src/video.c:71`); lógica e desenho entrelaçados em `JE_main`, `JE_drawEnemy` faz bem mais que desenhar; `mt_rand` compartilhado com caminhos de desenho (explosões repetidas, `JE_doSP`), do qual dependem demos e netplay; lógica a ~34,8 Hz (PIT 0x4300, `frameCountMax = 2`), em 60 Hz a imagem trepida porque 35 não divide 60; playfield visível de 264×184 com parte do HUD desenhada dentro dele; o parallax já existe (3 camadas + pan horizontal, starfield e smoothies); já há holofote, filtros de cor por fase e `fade_white`; o original não tem screen shake; o OpenTyrian guarda `tyrian.cfg` e `tyrian.sav` em `~/.config/opentyrian` ou ao lado do executável (portátil), nunca ao lado dos dados do DOS.
- Plano criado. Pendente na época: decisão do backend (SDL3 × SDL2+OpenGL) e aval para a Fase 0.
- Itens que já existiam no jogo original: display e input (fullscreen desktop, janela, scalers e modos Center/Integer/8:5/4:3; remapeamento de teclado em `src/config.c:297`; joystick pela API legada `SDL_Joystick`, não `SDL_GameController`).
- Registro de riscos original: quebra de determinismo (demos e netplay) → teste de regressão e RNG separado; trepidação a 35 Hz em telas de 60 Hz → interpolação; mistura de resoluções → resolvido pela decisão da grade de 320×200; divergência do upstream OpenTyrian → concentrar mudanças em módulos novos; contradições da proposta (sprites em alta resolução, parallax extra) → resolvido por arte só procedural.
