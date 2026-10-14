# Modern Tyrian — Estado atual da modernização

Este documento descreve **como o projeto é hoje**, para o Tyrian 2.1 (freeware) e o Tyrian 2000 juntos. Não é um diário: o histórico (o que mudou, por quê, commits, causas de bugs, decisões datadas) está em [CHANGELOG.md](CHANGELOG.md). Regras para agentes e comandos de build/teste estão no [`AGENTS.md`](../AGENTS.md).

> **Princípio:** fazer o Tyrian parecer e se comportar como um jogo moderno sem virar um remake.
> **Fronteira arquitetural:** o gameplay conhece o playfield original; o render conhece o display moderno.

Documentos de apoio:

| Documento | Conteúdo |
|---|---|
| [t2000/fork-diff.md](t2000/fork-diff.md) | Classificação das mudanças do fork OpenTyrian2000 |
| [t2000/data-formats.md](t2000/data-formats.md) | Formatos, contagens e layout do save do 2000 |
| [t2000/integration.md](t2000/integration.md) | Pontos de integração, API da variante, suíte `regress-2000` |
| [CRT.md](CRT.md) | Filtro CRT (NTSC e scanlines): teoria e implementação |
| [STEAM_DECK.md](STEAM_DECK.md) | Instalação e solução de problemas no Steam Deck |
| [progress-runtime.md](progress-runtime.md), [progress-levels.md](progress-levels.md) | Barra PROGRESS: observador de runtime e inventário de níveis |

---

## 1. O que é o projeto

Um OpenTyrian modernizado (C99, SDL3) que joga **dois jogos com um só binário**:

- **Tyrian 2.1 Freeware**: os dados vão no pacote (`./data`, baixados por `get_data.sh`).
- **Tyrian 2000**: portado da funcionalidade do fork `KScl/opentyrian2000` (GPL-2.0, pino `aad5aca`, 2026-02-22). Os dados **nunca** são distribuídos pelo projeto: o usuário instala ou aponta.

Dois modos de apresentação, escolhidos em *Setup → Graphics → Presentation*:

- **Classic**: o caminho original, intocado (mesmo framebuffer de 8 bits e mesma ordem de RNG do upstream).
- **Modern**: camada de apresentação sobre o mesmo jogo (widescreen, HUD novo, interpolação, luz, VFX, profundidade, CRT…).

Branches: cada tarefa roda em seu próprio branch/worktree a partir do `master` (`vittau/<tarefa>`), é publicada, passa na CI nos três sistemas e o `master` avança por fast-forward até ela (o branch `modernization` é antigo e não é mais usado). O `origin` é `github.com/vittau/modern-tyrian`.

---

## 2. Regras e invariantes

1. **O 2.1 não regride.** O Classic é byte a byte igual ao upstream. Toda mudança de baseline do 2.1 precisa de um motivo explícito.
2. **A camada Modern é só apresentação.** Nunca escreve estado de jogo, nunca chama `mt_rand`/`mt_rand_1`/`mt_rand_lt1`; usa RNG próprio. Efeitos extras nunca passam por `JE_setupExplosion` nem `JE_doSP`. Passes do Modern (`src/modern.h`) são `void pass(ModernFrame *)`: determinísticos, em ordem de registro, uma vez por quadro apresentado, na grade lógica.
3. **Efeitos na resolução original.** VFX, luz, bloom, partículas e profundidade vivem na grade lógica de 320×200 (ampliada com o mesmo scaling dos sprites). Nada é desenhado em resolução de tela (a exceção é o launcher).
4. **Arte nova só procedural**: gerada por código (ruído, gradientes, derivação dos sprites e tiles originais). Nada de sprites redesenhados em alta resolução. Única exceção: a arte pintada dos painéis do launcher (gerada por um worker Codex, arte original, origem em `assets/launcher/SOURCES.md`).
5. **Dados do Tyrian 2000 nunca entram em git, releases, LFS, caches, logs ou qualquer hospedagem do projeto**: nem o zip, nem arquivos dele, nem sprite, texto, mapa ou captura extraídos. Testes commitam só hashes e metadados (nome, tamanho, CRC). `tools/check_no_t2000_data.sh` impõe isso no `make regress` e antes do empacotamento. Os dados não têm texto de licença: nunca presumir a licença do código.
6. **Diferenças de variante vivem em tabelas e hooks**, nunca em `if (variant)` espalhado. Nada é removido do 2.1 para acomodar o 2000, e cada versão mantém seu comportamento histórico.
7. **Portar o fork entendendo, não colando.** Nunca copiar arquivos do fork em bloco; citar o commit de origem na mensagem.
8. **Portabilidade:** sem caminhos absolutos de usuário em arquivos commitados; saídas de regressão abertas com `"wb"` (o CRLF do Windows já quebrou hashes); `.c/.h` novos entram em `visualc/opentyrian.vcxproj` e `.filters` (o `.vcxproj` é CRLF); `%zu` não passa no MinGW (usar `%lu` com cast).
9. **Auditar todo `.c` tocado** com `gcc-16 -std=iso9899:1999 -pedantic -Wall -Wextra -Wno-format-truncation -Wno-missing-field-initializers -Werror -fsyntax-only -DTARGET_UNIX -DWITH_NETWORK -I/opt/homebrew/include <arquivo>`. O clang do macOS aceita construções que o GCC rejeita e não acusa header padrão faltando; só a CI em Linux/Windows pega o segundo caso.
10. **A regressão nunca toca o usuário:** regress e selftest cortam o acesso ao diretório do usuário (`userFilesDisable()`); config e saves não são lidos nem gravados.
11. **Nunca rodar duas regressões no mesmo checkout** (cada execução faz `rm -rf test/regress/actual/`).

---

## 3. Arquitetura

### 3.1 Ordem de inicialização

1. **`bootstrap.c`** (`gameBootstrapParse`), primeiro em `main`: flags iniciais (`--variant`, `--data`, regress/selftest). Arquivos de usuário ficam desligados até a variante ser conhecida.
2. **`game_variant.[ch]`**: `GameVariantDef` para 2.1 e 2000 (nomes, `save_namespace` `tyrian21`/`tyrian2000`, 4 ou 5 episódios, ponteiros para schema, UI e regras). `gameVariantCurrent()`.
3. **`launcher.[ch]`**: primeira tela do próprio binário, desenhada com SDL no tamanho físico da janela (ver 3.5).
4. **`game_data.[ch]`**: `GameDataProvider` localiza, valida (diagnósticos de variante errada nos dois sentidos) e abre dados só para leitura, sem fallback por arquivo entre pastas.
5. **`file.c`**: raiz do usuário, namespaces por variante e a migração única do 2.1 (`userPathsMigrateLegacy21`), antes de `loadSaves`.
6. **Loaders** leem `game_schema.[ch]` (`gameSchema()`, `gameStrings()`, rótulos semânticos `GAME_LABEL_*`): bancos de itens/inimigos, seções de strings, bancos de shapes (12/13), imagens e paletas, sfx e vozes, créditos, episódios. Regras vêm de `game_rules.[ch]` (`gameRules()`: eventos 58/59/68/83/84/85/99, launch/spawn, twiddles, naves do arcade, sidekicks com carga, Flying Punch, dinheiro, Timed Battle). UI vem de `gameUi()`. `highscores.[ch]` guarda os placares das duas variantes.
7. **Renderer Modern** (`modern*.c`, `vfx*.c`, `drawlist.c`, `interp.c`, `crt_filter.c`, `ntsc.c`): independente de variante. Luz e tags vêm do contexto do objeto, não do id do sprite; o HUD usa rótulos semânticos; as demos de atração seguem o HUD do modo ativo; modais sobre o layout pic-1 alargado usam `modern_dialog_begin/end`. Mudanças de janela precisam assentar com `SDL_SyncWindow` antes de ler a geometria (o Cocoa é assíncrono).

### 3.2 Fluxo de quadro (Modern)

```text
Tick de lógica original (~34,8 Hz, intocado)
   │  desenha no framebuffer 8 bits 320×200
   │  + tag buffer paralelo (categoria e emissão de cada pixel)
   │  + buffer de camada por pixel (qual camada pintou)
   │  + lista de desenho do tick (cada blit com identidade de objeto)
   ▼
Apresentação na taxa do monitor (interpolação por alpha entre ticks)
   ▼
Canvas XRGB na grade lógica (CPU)
   ├── paleta aplicada na conversão 8 bits → XRGB
   ├── recorte do playfield 264×184 + layout widescreen + painéis
   ├── passes: névoa → sombras → luz/bloom → VFX/ambiente → HUD
   └── filtro CRT (opcional, depois do canvas)
   ▼
Scaler "sharp bilinear" → janela
```

Composição na CPU, sem backend GPU próprio: com tudo na grade de 320×200 o custo é pequeno (64 mil pixels/quadro) e a saída é determinística e coberta por hashes. O `SDL_Renderer` só sobe a textura e escala.

### 3.3 Dados e instalador

- **Busca do 2.1:** `--data`, pasta do executável/bundle, `TYRIAN_DIR`, cwd.
- **Busca do 2000:** `--data`, `TYRIAN2000_DATA`, local do instalador, `tyrian2000/` ao lado do executável, `./tyrian2000`.
- **Arquivo oficial:** `https://www.camanis.net/tyrian/tyrian2000.zip`, 5.051.363 bytes, SHA-256 `348bc76e73514e452279b8730cf217daf0f70a282f07b6b94af653d87e921667`. Constantes em `installer.h`; manifesto de 79 arquivos em `test/regress-2000/data-manifest.txt` = `installer_manifest.h`.
- **`installer.[ch]`**: job não bloqueante em thread SDL. Detecta, baixa pelo `curl` do sistema (sem shell, só https), confere tamanho e hash, extrai com `third_party/miniz` (MIT, só inflate), valida pelo manifesto e instala por `rename` atômico (download em `.part`, extração em pasta temporária; falhas não instalam nada). Também instala a partir de zip ou pasta (GOG): dados válidos fora do formato canônico são aceitos e registrados no log. CLI headless: `--install-2000=download|PATH`. Falha de validação: "The downloaded Tyrian 2000 data could not be verified. Please try again or install the data manually."

| Plataforma | Local de instalação |
|---|---|
| Windows | `%APPDATA%\OpenTyrian\data-tyrian2000` |
| macOS | `~/Library/Application Support/OpenTyrian/data-tyrian2000` |
| Linux/SteamOS | `$XDG_DATA_HOME` ou `~/.local/share/opentyrian/data-tyrian2000` |
| Portátil (`opentyrian.cfg` ao lado do exe) | ao lado do exe |

### 3.4 Saves e arquivos de usuário

- **Raiz do usuário** (Windows `%APPDATA%\OpenTyrian`; macOS/Linux `$XDG_CONFIG_HOME/opentyrian` ou `~/.config/opentyrian`; portátil: ao lado do exe): `opentyrian.cfg`, `tyrian.cfg` (28 bytes: detalhe, gamma, teclas, joystick, volumes), `newsh$.shp` e o log. **Apresentação e controles são compartilhados entre as variantes.**
- **`tyrian21/`**: `tyrian.sav` (2.502 bytes) e demos gravadas (`demorec.N`). **`tyrian2000/`**: `tyrian.sav` (4.722 bytes = prefixo de 2.502 + placares sem criptografia; um campo de 4 bytes desconhecido é preservado). Uma variante nunca lê o save da outra.
- **Migração do 2.1** (antes de `loadSaves`, nunca em regress/selftest): copia o `tyrian.sav` da raiz só se tiver exatamente 2.502 bytes, via temporário exclusivo (`link`+`unlink` no POSIX, `rename` no Windows/FAT), sem sobrescrever e sem mexer no original. Se a cópia falhar, a sessão lê o save da raiz só para leitura e não grava nada. Um binário antigo continua no save da raiz e o progresso diverge dos dois lados.
- No Steam Deck, config e log ficam em `~/.config/opentyrian/`; `--log-file` funciona em qualquer sistema.

### 3.5 Launcher

- Abre em **todo** início normal (inclusive no Steam Deck); `--variant=2.1|2000`, regress e selftest o pulam. `launcher/last_variant` em `opentyrian.cfg` só pré-seleciona o painel.
- Dois painéis (2.1 azul, 2000 laranja) com arte embutida no binário (`tools/embed_assets.sh` → `obj/launcher_art.c`; VS: `visualc/embed_assets.ps1`), desenhados no tamanho físico da janela, legíveis em 1280×800. Texto em fonte de debug do SDL em escala inteira. Texto lista só o que existe (não há arena).
- Controles: `←/→` escolhe, `Enter/A` confirma, `Esc/B` sai; teclado, mouse e gamepad. Mostra se os dados do 2000 estão instalados; quando faltam, o botão vira **INSTALL**, que dirige o instalador (com tela "coloque os arquivos" quando não há seletor de arquivos: Game Mode do Deck, `OPENTYRIAN_NO_DIALOGS`).
- Se o compositor ignora o resize (gamescope), o launcher desenha no tamanho que a janela tem e segue os eventos de resize; nunca encerra o jogo por isso.
- Identificação da variante: "TYRIAN 2.1"/"TYRIAN 2000" discreto no menu, pausa e About; o log de início traz `Game Variant`, `Data Path` e `Data Validation: OK|FAILED`.

### 3.6 Notas técnicas do código (fatos que guiam mudanças)

- Renderização 100% em software, 8 bits indexado, 320×200; paleta em 16 matizes × 16 brilhos, blends e filtros operam nesses nibbles.
- Lógica a ~34,8 Hz (`frameCountMax = 2`); `getFrameCount2Ticks()` é acumulador de fase (aviso em `fonthand.c`, respawn em `mainint.c`) e precisa ser preservado por qualquer mudança de ritmo.
- Playfield visível 264×184 (offset +24); dinheiro, vidas e superbombs são desenhados dentro dele. `JE_inGameDisplays` escreve em `tempW` (lido pelo pan) e `draw_boss_bar` atualiza `boss_bar[]` ao desenhar: desenho e lógica não são separáveis sem cuidado.
- `JE_drawEnemy` só anima e desenha inimigos em x ∈ (-29,300). `blit_sprite2*` usam o pitch da superfície de destino. `draw_background_2/3` avançam o scroll dentro da função de desenho e não podem ser chamadas duas vezes por quadro.
- Os smoothies (lava, água, blur, iced blur, holofote, flip) ligam por eventos de fase (tipo 64); só a execução confirma quais rodam (`tools/scan_smoothies.py` lista candidatos). `lvlPos` tem duas entradas por fase.
- Áudio: mono S16 44100 Hz, buffer de 1024; música = Nuked OPL3 (modo OPL2, 49716 Hz) + player LDS; efeitos 8 bits a 11025 Hz convertidos por sinc polifásico (Kaiser) próprio em inteiros, idêntico em qualquer plataforma. Sem `rand()` da libc no áudio.
- Rede: UDP ponto a ponto, 2 jogadores, em lock-step; `network_read16/write16` em `src/network.h`; SDL3_net sem bind, filtro por endereço/porta do oponente explícito. O lock-step em jogo nunca foi exercitado; só o handshake. O fork do 2000 também não tem arena nem rede testada.
- SDL3: `SDL_EVENT_GAMEPAD_ADDED/REMOVED` chegam mesmo com eventos desabilitados, mas `SDL_EVENT_JOYSTICK_*` não; um gamepad gera os dois pares, deduplicar por instance id.
- Dados do teste: o zip oficial do `get_data.sh` é a referência (`newsh9.shp` de 38.831 bytes); uma cópia local diferente diverge no quadro 855 do cenário `flip`.

---

## 4. Apresentação: Classic e Modern

### 4.1 Classic
O caminho original. Mantém detalhe escolhido pelo jogador (`tyrian.cfg`; instalação nova começa em Pentium), os scalers de software foram removidos (quadro convertido em 1× e escalado pela GPU). Estrelas a 25 % da velocidade original (`--starfield-speed=PERCENT`, 10–100, chave `starfield_speed_percent`; `100` devolve o ritmo upstream). Fit usa o mesmo caminho "sharp bilinear" do Modern, e a janela usa múltiplos inteiros.

### 4.2 Opções do Modern (Setup → Graphics)

Opções valem na hora e ficam esmaecidas no Classic.

| Opção | Valores | Padrão | Config / CLI |
|---|---|---|---|
| Presentation | Classic, Modern | Classic (Deck: Modern) | `presentation`, `--presentation=` |
| Aspect | 4:3, 16:10, 16:9, 21:9, 32:9, auto | 4:3 (Deck: auto) | `aspect`, `--aspect=` |
| Smooth Motion | on/off | on | `smooth_motion`, `--smooth-motion=` |
| CRT Filter | Off, Scanlines, NTSC, Scanl + NTSC | Off | `crt_filter` |
| Lighting (bloom + luz) | Off, Low, High | Low | `lighting`, `--lighting=` (`bloom` é alias lido) |
| Depth | Off, On | On | `modern_depth` (`low`/`high` legados leem On) |
| Effects (VFX e ambiente) | Off, Low, High | Low | `vfx`, `--vfx=` |

`Pixel Aspect` (`pixel_aspect`, `--pixel-aspect=`) existe em config/CLI mas está escondido no menu: o Modern usa sempre o original (1,2). O nível de detalhe é **fixo em Pentium** no Modern (SuperWild com a trapaça) e some do menu; as flags de detalhe são só visuais. O Modern abre janela no formato escolhido, a ~80 % da área útil.

### 4.3 Escala, aspecto e janela
- Canvas de largura `round(200 × PAR × aspect)`, com o quadro original centralizado; PAR original 1,2. **Sharp bilinear**: pré-escala inteira com nearest num render target em cache e depois passe linear (pixels uniformes, sem linhas de 4 e 5 px que tremem no scroll). Menus, loja e cutscenes (320×200) ficam centralizados com moldura.
- O mapeamento do mouse leva em conta o deslocamento do quadro e as faixas inseridas.
- Pendente: `SDL_WINDOW_HIGH_PIXEL_DENSITY` não é pedido (ver §8).

### 4.4 Widescreen e HUD
- **Laterais em jogo:** playfield de 264×184 recortado; a barra lateral e a faixa de baixo originais continuam sendo desenhadas no quadro de 8 bits (lógica intacta) mas **não entram no canvas**. O HUD novo ocupa os painéis (`MODERN_HUD_MIN_PANEL_WIDTH` = 51 px; 81/82 px em 16:9, 60 px em 16:10; em 4:3 não há painéis e vale o fallback).
- **Layout:** armas e sidekicks à esquerda; escudo, armadura e gerador/força à direita, em barras **verticais** com gradiente (78 px no 1P/arcade; no 2P compacto 39/35/29 px conforme 21:9–32:9/16:9/16:10). Colunas do bloco de vitais com no máximo 36 px (`VIT_COL_MAX_W`) alinhadas pela borda externa. Painéis de "vidro" (fundo desfocado 68 %–32 %, sombra de 1 px sob o texto), moldura chanfrada em volta do playfield (filete duplo, glints diferentes por lado com bloom no filete, sombra interna). Tudo na grade lógica e nos hashes.
- **Conteúdo:** dinheiro, vidas (só arcade e 2P, como no original), nomes de jogador **só no 2P**, superbombs, arma especial, barra de chefe, energia das armas (`PWR`), timer, aviso de trapaça (1–3 linhas), nome da fase e mensagem do jogo na faixa sob o playfield. Vidas/dinheiro em linhas separadas; nomes longos são truncados.
- **PROGRESS:** barra horizontal contínua com paleta de SHIELD; observador somente de apresentação. Marco estimado pelo maior tempo positivo de ação 11/36, avanço por trecho até 99 %, congelada durante boss reconhecido; saída antecipada pode terminar abaixo de 100 %; 1P/arcade à esquerda (rótulo y=142, barra y=150), 2P na linha de título; omitida sem painéis. Não há identificação universal de boss final nem promessa de porcentagem precisa em todos os níveis/modos (inventário de 62/70 níveis; ver [progress-runtime.md](progress-runtime.md) e [progress-levels.md](progress-levels.md)).
- **Ultrawide:** painéis de 21:9/32:9 usam as larguras limitadas acima.
- **Telas segurando o jogo** (pausa P, menu ESC, ajuda F1) mantêm o HUD nas laterais, com "PAUSED" centralizado e o efeito dos passes preservado (ver 4.9).
- **Quit e diálogos** (inclusive nome de recorde) são centralizados; a sombra fica pendurada como no original, cortada no limite do painel alargado.

### 4.5 Telas fora do jogo (widescreen)
- **Vert-** (fundo ampliado ~1,33× e cortado em cima e embaixo, menus nítidos em 1×): título (pic 4) e menus sobre a pic 2 (seleção de jogo/episódio/dificuldade, setup, ajuda, load/save, recordes).
- **Painel direito alargado** (pic 1: loja, armamentos, dados, opções, data cubes, teclado, joystick, load/save, Quit, Ship Specs, mapa de navegação): uma faixa de colunas internas do painel é repetida até preencher o canvas; o corte é decidido pelo conteúdo (logo depois do elemento mais à direita, y < 184, com a faixa de baixo à parte); o cabeçalho (y ≤ 33) repete a coluna 310 para não repetir o bisel; o título da caixa não é recentralizado. Nada é redesenhado: o jogo segue desenhando 320 px. O simulador de armas continua na largura original.
- **Extensão real** no canvas inteiro: jukebox, ship specs e créditos (superfície de 8 bits da largura do canvas, `modern_screen_begin()`), starfields.
- **Preenchimento sólido** onde a borda é lisa (pic 5, pic 11/Destruct, telas pretas) e **desfoque escurecido da própria tela** como fallback (logos de abertura, imagens da história, animação final).
- Rótulos de remapeamento longos viram abreviações (`A/RB`, `LY-/UP`, `L Shift`…) para caber em x ≤ 310; glyphs que cruzam a borda são cortados.

### 4.6 Taxa de quadros e interpolação
- Lógica no tick fixo original; o Modern em jogo apresenta na **taxa do monitor** (≥ 60 Hz, com vsync; sem vsync dorme até o refresh) a `alpha = decorrido / período do tick`, via `interp_present_gameplay()` (`src/interp.c`) e a lista de desenho por tick (`src/drawlist.c`).
- Interpola posições (nave, sidekicks, inimigos inclusive de chão, tiros, explosões que seguem objetos, scroll das três camadas, starfield, superpixels); quadros de animação não são misturados. Casamento por identidade e ordem; snap em nascimento, salto > 64 px e troca de sheet. Linhas de fundo casam pela linha do mapa (`row_key`).
- Efeitos sobre o quadro interpolado: fades de paleta e barras de escudo/armadura/força/chefe com comprimento interpolado. Ficam por tick: a rampa de brilho de 4 bits do filtro de fase, gauges/pips discretos e textos. Filtros de água/lava/blur/iced são aplicados sobre quadros intermediários; o quadro do tick é exato.
- Custo ≈ 1 tick (~28 ms) de atraso de imagem; ~1,1 ms por quadro apresentado em 16:9; medido ~120 fps em painel de 120 Hz. Provas: `--regress-replay-check`, `--regress-interp-check`, `--regress-interp-smoothness`, `--regress-smooth-effects-check`, `--regress-gameplay-check`.

### 4.7 Luz, bloom e VFX
- **Lighting** (`src/modern_bloom.c`): bloom e mapa de luz dinâmico juntos, só no playfield dos quadros de jogo. A luz parte dos pixels emissivos marcados pelo tag buffer e leva a **cor do objeto** (tom saturado da matiz dominante do sprite). Planos Q8 de 16 bits, limitador suave (joelho 150, assíntota 216). Ganhos atuais: **High 1997, Low 1198** (Low = 0,6× High). Tiros grandes do jogador têm a emissão escalada por `(S_ref/S)^0.75` (pegada emissiva no byte de tag; referência 32 pixels brilhantes), mantendo-os em ~1,4–2,1× o tiro pequeno. Custo < 0,5 ms.
- **Effects** (`vfx*.c`): faíscas, destroços, fumaça e anéis (blend/darken de nibble do motor), shockwave, trilhas, muzzle flash (centrado no sprite visível) e impactos; Low/High (`medium` é alias de high). **Ambiente** (`vfx_ambient.c`): brasas na lava, neve no gelo, névoa na água e no desfoque, poeira fina no espaço e poeira no resto; só clareiam ou mesclam, não emitem luz, seguem o Effects. VFX e ambiente usam RNG próprio de ordem fixa.
- Textos desenhados com contexto próprio (`DL_OBJ_HUD`: INSERT COIN, janela de mensagens, rastros no fim da fase) não herdam emissão de tiro.

### 4.8 Profundidade (Depth)
- **Buffer de camada por pixel** (byte paralelo ao quadro, carimbado pelas mesmas primitivas que carimbam o tag): bg1, bg2 (translúcido ou não), bg3, inimigos por faixa de slot, itens, nave, sidekicks, tiros, explosões, estrelas, superpixels. O quadro de 8 bits e os blends não são alterados. Ordem de desenho por tick (muda por nível).
- **Sombras projetadas** (`modern_depth.c`; luz fixa do alto à esquerda): cada camada projeta sobre as anteriores e mais baixas, com deslocamento (dx,dy) proporcional à altura: bg2 (4,6), inimigos do céu (8,12), nave/sidekicks (9,14), bg3 (10,15), inimigos do topo (12,18), inimigos de chão (2,2); bg2 translúcido com 31 % do peso. Silhueta deslocada, desfoque separável em ponto fixo, escurecimento 78/256. Tiros, explosões, estrelas, superpixels, texto e pixels de VFX não projetam nem recebem. Fases de espaço (`starActive`) não têm sombra, névoa nem luz por camada. ~0,3 ms/quadro.
- **Névoa no bg1**: mistura 30/256 (`MD_FOG_STRENGTH_DEFAULT`) em direção a uma cor por quadro (média do bg1, meio dessaturada, ~35 % mais clara); a linha translúcida do bg2 recebe metade; sem ruído animado. **Luz por camada** (Q8, `md_light_q8[]`): terreno e inimigos de chão 256, inimigos do céu e do topo 160, nave e sidekicks 144, bg3 24, tiros/explosões/VFX 256; bloom não é pesado. Ordem dos passes: névoa → sombras → luz. ~0,16 ms adicionais.
- Com `Depth: Off` a saída é idêntica bit a bit à sem profundidade. A regressão pina Depth em Off (baselines antigos intactos); casos `depth-*` provam Off × On com estado/RNG idênticos.

### 4.9 Telas seguradas (pausa, menu ESC, ajuda)
`src/modern_held.[ch]`: apresentam o playfield congelado reutilizando a camada, o tag, o ranking e as cores de luz do último quadro realmente apresentado, menos uma máscara de overlay (pixels que diferem do snapshot não recebem sombra, luz nem emissão). `--regress-held-check` compara o quadro seguro com o último vivo fora do overlay.

### 4.10 Filtro CRT
Só no Modern, depois dos passes e da captura do canvas em `modern_present_frame` (inclusive quadros interpolados e modais); o launcher não passa por ele. **Scanlines** (bandas de meia linha lógica, luminância limitada a 216, altura `2×src` só quando `dst % (2×src) == 0`, senão altura nativa do Fit; abaixo de `2×src` mantém `src`) e **NTSC** (kernel escalar do `snes_ntsc 0.2.2`, Shay Green, LGPL 2.1+; composite, três fases de burst; tabela de ~16 MiB criada na primeira ativação e liberada no shutdown; a largura de saída acompanha o NTSC). Sem curvatura nem máscara. Buffer e textura só são recriados quando as dimensões mudam; read-only sobre o canvas, com o retângulo Fit e o mapa de mouse originais. Custo (A18 Pro, canvas 427×200, 1080/2160 linhas): Scanlines 0,38/0,49 ms, NTSC 0,31/0,31 ms, combinado 0,85/1,33 ms. Detalhes em [CRT.md](CRT.md).

### 4.11 Controle, gamepad e Steam Deck
- **Gamepad** pela API do SDL3 com mapeamento padrão, hot-plug e remapeamento por nome no config; botões padrão estilo Xbox/Deck, todos remapeáveis e persistidos (`--selftest-gamepad` com gamepad virtual). Telas de remapeamento cabem dois mapeamentos por ação.
- **Analógico (só Modern):** zona morta radial de 0–20 % (padrão 10 %, por controle, `--deadzone`); curva linear até 75 % de input com velocidade crescente até os 8 px/tick originais; acima disso, máximo. O alvo de momento é projetado por eixo e o stick a até 5° de um eixo encaixa nele (D-pad e ruído de repouso não geram movimento diagonal); abaixo do passo máximo só passo sub-pixel. Stick todo inclinado = trajetória do analógico original; a rede segue consistente (accel transmitido ±1). Diagonal radial máx. 9,7 px/tick (o analógico antigo chegava a 11,3). Classic não muda.
- **Steam Deck:** detecção (`SteamDeck=1` ou placa Jupiter/Galileo); primeira execução em tela cheia, Modern, aspecto auto (16:10 em 1280×800). Pacote Linux `.tar.gz` com binário estático em SDL3/SDL3_net (só glibc/libm; roda em SteamOS 3.5+); HIDAPI/libusb no SDL3 estático; leva os dados do Tyrian 2.1 freeware (nunca o 2000). Guia em [STEAM_DECK.md](STEAM_DECK.md). Validado num Deck físico: launcher em Game Mode e o analógico corrigido.

### 4.12 Áudio
Nuked OPL3 (modo OPL2, 49716 Hz, integer-only, LFSR de ruído próprio) convertido a 44,1 kHz por resampler polifásico contínuo; efeitos por sinc Kaiser em inteiros (plano até 4,98 kHz, rejeição > 90 dB). Não serão feitos: estéreo posicional, limitador suave, trilha OGG do usuário.

### 4.13 Modo Natal e demais
`--xmas`/`--no-xmas` (fork do 2000, comum às duas variantes). As demos de atração seguem o HUD do modo ativo.

### 4.14 Bike (FTMS), núcleo
Projeto em `doc/ftms/design.md`. Tudo é opcional e **desligado por padrão**: com o interruptor mestre (`[ftms] enabled`) desligado, ou sem fonte de dados, o jogo é idêntico ao stock e os 207 baselines não mudam.
- **Módulos:** `bike_effort` (motor de esforço em ponto fixo Q16.16, sem `float`), `bike_feed` (feeds CSV e `const:W[,RPM]`), `bike` (settings, fonte, portão, tick por quadro), `bike_rules` (mapeamento C: `renda = clamp(G·(bias + gain·n), F, G·cap)` com acumulador de resto exato, e mistura de volta ao `powerAdd` stock após 1,5 s sem link, a 1/2 por segundo; nunca zera).
- **Portão** (`bike_gate_open`): mestre ligado, dentro de uma fase, e não 2P, não `onePlayerAction`, não rede, não demo (tocar/gravar), não galaga, não `constantPlay`. O gerador ainda exige fonte e ao menos uma amostra.
- **Ganchos no jogo:** `bike_frame_tick()` uma vez por passagem de `level_loop`; o reabastecimento da barra de energia usa `bike_rules_power_add(powerAdd)` só quando o gerador da bike está ativo; `bike_service()` em `handleSdlEvents()` (ainda sem transporte).
- **Configuração:** seção `[ftms]` do `opentyrian.cfg` (não grava nada se tudo estiver no padrão); CLI `--ftms-bridge=HOST[:PORT]`, `--ftms-ftp=W`, `--ftms-off`; regress fixa tudo desligado (`--regress-bike-feed`, `--regress-bike-set`, `--regress-bike-log`).
- **Estado dos hashes:** `power`, `lastPower` e o digest da bike só entram no hash de estado quando há feed ou o mestre está fixado ligado.
- **Testes:** `tools/check_bike.sh` (`make regress-bike`; no quick só o subconjunto flag-off, tudo-desligado e determinismo a 150 W), com digests em `test/bike/regress.txt`.
- **Efeitos de surge (T3.1):** cada um é um toggle próprio sob o mestre, só com o portão aberto e fonte ativa; `surge_superbomb` vem ligado, os outros desligados. Todos em inteiros, em `bike_rules`.
  - `surge_superbomb`: +1 `player[0].superbombs` a cada evento overdrive-ready (teto 10, como o pickup de `mainint.c`). Em surge sustentado o evento sai a cada ~5,3 s, então o teto é o limite real.
  - `surge_sidekick`: o reinício de `charge_ticks` cai de 20 para 10 enquanto em surge (carga 2x mais rápida; `mainint.c`).
  - `surge_special`: um decremento extra de `specialWait` por quadro em surge (`varz.c`).
  - `surge_shield`: o intervalo de regen do escudo vai de 15 para `15 - round(7·n)` quadros (n = esforço normalizado, 8 no máximo) em surge; o custo `shieldT` por ponto não muda, então nunca fica pior que o stock (`tyrian2.c`).
- **Auto-fire (T3b.1), `src/bike_controls.[ch]`:** a opção `autofire = off|toggle` só decide se o recurso existe; o liga/desliga é durante o jogo, com um aperto, sem menu.
  - **Entradas** (tabela pequena no topo de `bike_controls.c`, fácil de trocar depois da sonda T3b.0; nenhuma é slot de `keySettings`): `autofire_key` (padrão `A`) e `autofire_key2` (padrão `Application`, a tecla Menu do controle remoto), mais o clique do analógico direito do gamepad. O clique direito do mouse não é usado: é ação configurável do jogo e pertence ao T3b.2. Flanco de subida inverte o trinco; flancos com o portão fechado são ignorados.
  - **Estado:** o trinco começa desligado a cada execução e **sobrevive entre fases** (decisão do design §6); desligar a opção `autofire` o zera. Fora do portão (rede, demo, 2P, arcade) nada acontece.
  - **Gancho:** `mainint.c`, depois do bloco de teclado e antes do empacotamento de rede: `button[0] = true` se o trinco está ligado e `power >= reserva·9` (reserva = Off/25 %/50 % de 900). O fogo manual entra antes (OR) e ignora a reserva. Sem histerese: o tiro passa a ser limitado pela renda no nível da reserva. 25 % = 225 > maior `shieldT` stock (180), então o escudo continua regenerando.
  - **HUD:** `bike_autofire_active()` e `bike_autofire_held_back()` (também em `BikeHud.autofire_on/held`).
- **Regress:** `--regress-bike-events=FILE` (id 443; linhas `tick,ação[,arg]`): `autofire_toggle|autofire_on|autofire_off`, `set,K=V`, `fire_on|fire_off` (fogo manual injetado), e só para teste `immortal` e `equip,special=N|sidekicks=L/R|shield=N`. `check_bike.sh` ganhou os casos de surge (cada efeito ligado contra todos desligados), auto-fire ligado/desligado no meio da fase, reserva sob renda baixa e fogo manual ignorando a reserva; linhas de cobertura `Bike effects:`, `Bike autofire:` e `Bike end state:`. Os casos antigos fixam `surge_superbomb=0` (o padrão é ligado), então seus digests não mudaram; o digest de estado só inclui o trinco depois de ele ser usado.
- **Menu "Bike and Controls"** (`src/bike_menu.[ch]`, T2.5): Setup ganha a linha "Bike and Controls..." (`SETUP_MENU_BIKE`); a lista rola (a Graphics está cheia) e tem Mestre, Bike Power, Bridge (caixa de texto host:port, padrão 127.0.0.1:8787), FTP (50 a 500 W, passo 5), Strength, Floor, os quatro surges, Auto-fire (Off/Toggle), Auto-fire Reserve (Off/25%/50%), Air Mouse, Pointer Speed, Touch Controls e Bike HUD. A linha de estado fixa mostra o link (Off, Connecting, Connected, Streaming), watts, rpm e idade da última amostra. Setas, Enter e Esc bastam (mouse também); esquerda/direita aceleram enquanto a tecla é mantida. Cada mudança grava em `bike_settings` na hora (nada é guardado em cache), vale no próximo quadro e é salva com a configuração. Os recursos ainda não implementados (surges, auto-fire, mouse aéreo, toque) já persistem o valor. O jogo em si mostra "Bike and Controls" no menu do jogo (ESC) quando o mestre está ligado (ou depois de abrir o menu uma vez na sessão), nunca em jogo em rede; com o mestre desligado esse menu fica idêntico ao stock.
- **HUD** (T2.6): só quando mestre e "Bike HUD" estão ligados e o portão está aberto (`bike_hud_visible()`). Modern: no painel de status, acima do nome do gerador, ponto de link (verde streaming, amarelo conectado, laranja conectando, vermelho link perdido, cinza desligado), watts e rpm, barra de esforço, medidor de surge e linha AUTO/HOLD/SURGE (AUTO lê `bike_autofire_state()`, hoje um stub que devolve falso até o T3b.1). Classic e Modern 4:3: apenas ponto de link e watts, acima das superbombas. Só leitura de `bike_hud()`: nunca escreve estado, configurações nem RNG; `tools/check_bike.sh` prova que o HUD ligado não muda o hash de estado.

---

## 5. Tyrian 2000

- **Conteúdo:** 5 episódios, naves e armas novas, Timed Battle, arcade de 9 naves, Super Tyrian, Destruct, modo Natal; 13 bancos de shapes, 31 efeitos sonoros (vozes deslocadas), 14 imagens e 24 paletas, 126 créditos, 74 fases (E1–E5). As cinco demos são idênticas às do 2.1 e só jogam o episódio 1.
- **Regras por variante** (`game_rules.[ch]`): eventos de fase 58/59/68/83/84/85/99 (o 68 é explosão aleatória no 2.1 e substituição de inimigo no 2000), sidekicks com carga que não disparam sozinhos, rear "None", twiddles do 2000, rastro do Flying Punch, high score do Hazudra Fodder, Timed Battle (cronômetros, bônus e roteamento do fork; sem save ao iniciar a batalha).
- **Renderer Modern no 2000:** tudo o que o 2.1 tem (widescreen, HUD novo com rótulos semânticos, interpolação, luz, VFX, Depth, CRT, held); a auditoria não achou defeito no renderer porque tags e luz vêm do contexto do objeto.
- **Placares:** `src/highscores.[ch]` unifica os das duas variantes.
- **Fidelidade:** o fork marca eventos novos e o rastro do Flying Punch como aproximações; a comparação com o DOS é manual (ver §8).

---

## 6. Testes, regressão e CI

Comandos e opções completos: [`AGENTS.md`](../AGENTS.md). Resumo do sistema:

- **Harness headless** (`SDL_VIDEO_DRIVER=dummy`, relógio virtual): hashes por tick de quadro (8 bits + paleta e canvas Modern) e de estado (RNG, jogadores, inimigos, tiros, barras de chefe, `tempW`, eventos, explosões; escalares little-endian de largura fixa). Em modo regress só `SDL_EVENT_QUIT` passa, joysticks ficam inertes, o foco é fixo, os handlers de sinal do SDL ficam desligados (o harness reporta "killed by signal"), e no macOS o processo sobe com `SDL_HINT_MAC_BACKGROUND_APP`. Cenários jogam fases reais com a nave invencível (`youAreCheating`), então "Cheaters always prosper." aparece nos baselines.
- **Suítes:** `make regress` (2.1: 207 casos + guards, portão da CI); `make regress-2000` (183 casos, dados necessários, paralela, manifesto e baselines em `test/regress-2000/`); `make regress-quick` (subconjunto crítico + guards baratos + 2000 opcional, ~20–25 s); `-j`/`REGRESS_JOBS`; `REGRESS_ONLY`/`--case=REGEX`. Casos Modern de gameplay rodam em `-d4`. Os baselines do 2.1 são gerados no Mac mas batem entre plataformas (todos os hashes de quadro bateram nos cinco runners).
- **Cobertura:** demos × níveis de detalhe; 5 cenários sintéticos para os smoothies; áudio offline (`--regress-audio`); telas (`--regress-screen`, `--regress-menu`); flow com teclado virtual (`--regress-flow`); fixtures de regras; matriz final (§6.1); Depth/camadas/held/CRT/PROGRESS com checks próprios; `--regress-demo-hud-check`; stick virtual (`--regress-stick`).
- **Guards:** `check_no_t2000_data.sh`, `check_variant_bootstrap.sh`, `check_user_paths.sh`, `check_game_rules.sh`, `check_bike_effort.sh`, `check_bike.sh`, `check_installer.sh` (zips sintéticos e `tools/curl_stub.c`, nunca chega à rede), `check_final_regression.sh`, `check_display.sh`, `check_progress.sh`, `check_depth_*.sh`, `check_rng_order.sh`.
- **Varreduras longas** (`--interp-check`/`--smoothness-check`/`--replay-check` completas) só no workflow manual `regress-full.yml`; nunca localmente.
- **Processo de teste do coordenador:** só o quick localmente; push do branch e a CI como portão completo. Visual: o usuário prefere testar no jogo; capturas só a pedido.

### 6.1 Matriz de testes (§7 do antigo plano 2000)

| Área | 2.1 | 2000 |
|---|:---:|:---:|
| Launcher | ✓ | ✓ |
| Detecção de dados | ✓ | ✓ |
| Instalação de dados | N/A | ✓ |
| Menu principal | ✓ | ✓ |
| Episódios 1–4 | ✓ | ✓ |
| Episódio 5 | N/A | ✓ |
| Timed Battle | N/A | ✓ |
| Naves, armas, inimigos, bosses | ✓ | ✓ |
| Áudio | ✓ | ✓ |
| Save/Load | ✓ | ✓ |
| Widescreen, renderer moderno | ✓ | ✓ |
| Pause, controle | ✓ | ✓ |

### 6.2 CI e releases
- `.github/workflows/{linux,macos,windows}.yml` rodam em push/PR de qualquer branch: build, `make regress`, `tools/fetch_t2000_data.sh` (camanis.net, tamanho + SHA-256 exatos, em `$RUNNER_TEMP`, **sem cache do Actions** porque o cache de um repositório público é legível por PRs de fork) e `make regress-2000`. Na falha sobem só `*.txt`/`*.log`. Runners: Linux x86_64/arm64, macOS, Windows x86_64 (MSYS2; **sem Windows arm64**, que roda o pacote x86_64 pela emulação).
- **Job de decisão** (`.github/ci/decide.cjs`): pula pushes só de Markdown, `docs/` ou `.worker-reports/` e pushes cujo SHA já passou no mesmo workflow em outro branch; `workflow_dispatch` roda sempre.
- **Release:** reaproveita os pacotes do push verde do mesmo SHA e só anexa os bytes originais (recompila só se não houver push verde; falha alto se expirados). Pacotes: Linux x86_64/arm64, Windows x86_64, macOS universal; levam só dados 2.1. Versões publicadas até agora: v0.1.0 … v0.6.0 (a última: depth shadows).
- Windows/MSYS2: sem scripts de shell via `CreateProcess`, usar `cygpath` para caminhos e `python` instalado.
- Empacotamento: `make_macos.sh` (frameworks SDL3/SDL3_net fixados por SHA-256, app universal), `make_linux.sh` (SDL3 estático do código-fonte, fixado e verificado).

---

## 7. Decisões em vigor

| Decisão | Motivo |
|---|---|
| Backend SDL3; composição Modern na CPU | efeitos na grade de 320×200 custam < 1 ms; portátil e determinístico |
| Efeitos na grade lógica; arte só procedural | sem artista; evita mistura de resoluções |
| Lógica no tick fixo; desenho interpolado ≥ 60 Hz | preserva demos, rede e regressão; acaba a trepidação de 35 Hz |
| Modern: só o HUD novo; HUD nos painéis laterais | o original é redundante; usa o espaço do widescreen |
| Visão estendida descartada | só ~24 px de arte real por lado e mostrar objetos daria informação que o original não dava |
| Pixel aspect Original (1,2) e escala sharp bilinear fixos no Modern; scalers de software removidos | "só queremos pixels perfeitos"; sem escolha da pior opção |
| Detalhe Pentium fixo no Modern | não oferecer versão pior |
| Lighting Off/Low/High com seletor único; Depth Off/On | simplicidade; o usuário aprovou High 1997/Low 1198 e névoa 30 jogando |
| Áudio: só Nuked OPL3 | única melhoria aceita |
| Dados de teste = zip oficial do `get_data.sh`; CI roda a regressão | clone novo reproduz os baselines |
| 2000: fork no `master` (`aad5aca`); download da Camanis com hash fixo; instalação manual/GOG | fonte do autor, nada hospedado |
| Apresentação/controle compartilhados; saves, placares e progresso por variante | evita sobrescrever saves |
| Launcher sempre abre, dentro do binário; `--variant=` só para regress/automação | decisão do usuário |
| Pacote de release leva só dados 2.1 (freeware) | evita problema jurídico |
| Windows arm64 fora da CI/releases | pouca adoção e runner lento/instável |
| Analógico/zona morta/curva só no Modern; Classic idêntico | byte-exact |
| Regressão local enxuta; CI como portão | suítes longas; só o coordenador roda o quick |
| Workers não fazem commit/push/troca de branch; coordenador revisa e commita só por caminho explícito | evita publicar trabalho sem revisão |
| Tarefa nova → agente novo (contexto limpo); correção da mesma tarefa volta ao mesmo agente | sem contexto poluído |
| Workers hoje: Codex GPT-6.1 Sol (medium) | escolha do usuário |

---

## 8. Pendente, em aberto e limitações

**Conferir no DOS original** (aproximações do fork): eventos 58/59/68, trilha 198, roteamento e bônus do Timed Battle (inclui overflow de 16 bits do bônus de tempo, mapeamento batalha→episódio, seção 44 do E1 com shapes inexistentes, bônus de vidas, música e cancelamento do placar), estado 8 do Super Tyrian, sprite do Pretzel Pete. Em aberto também: 77 bytes sobrando no fim de cada bloco de itens do 2000.

**Só manual:** Steam Deck físico para o 2000 e desempenho a 60/90 Hz, seletor de arquivos nativo e download no Windows, ultrawide/HiDPI físicos no Cocoa; comparação visual com o 2000 original no Classic.

**Funcionalidade:**
- Nitidez em Retina: `SDL_WINDOW_HIGH_PIXEL_DENSITY` não é pedido (medir se melhora).
- Acessibilidade (menos flashes e partículas, cores alternativas de tiros, intensidade ajustável): futuro.
- HUD expandido do plano original (nomes e nível das armas, munição e carga dos sidekicks, numéricos): só parcialmente atendido pelo HUD atual.
- PROGRESS: sem grafo por trechos, perfis certificados, bosses sem barra ou cobertura de todos os caminhos/modos.
- Fase 8 do 2000 (regressão final): a matriz está coberta por `check_final_regression.sh`; "o 2000 não depende por acidente dos dados do 2.1" e "instalador em todos os cenários" seguem como critério de rotina, sem item formal de fechamento.
- Janela do Modern tem tamanho próprio, mas o modo Integer com PAR 1,2 em janelas pequenas (640×400) fica quase quadrado (visto antes da reformulação de escala; reconferir).
- Rede: o lock-step em jogo nunca foi exercitado.
- Projeto Visual Studio (`visualc/`): lista todos os fontes, mas a compilação nativa não é testada pela CI (que usa MSYS2).
- Os baselines Classic dependem de libm; valem enquanto os hashes bateram nos cinco runners.
- O fork do 2000 não tem arena multiplayer; a rede do 2000 nunca foi testada.
- A release que reaproveita pacotes só foi exercitada de verdade a partir da v0.6.0.
