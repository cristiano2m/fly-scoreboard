# Fly Scoreboard: análisis y plan para publicidad (sin implementar)

Clon: `C:\Users\cmiranda\Downloads\obs-plugins\fly-scoreboard` (main @ f697414, versión 5.1.0, último commit 2026-10-26).

## Análisis

- **Tipo**: plugin nativo de OBS en **C++20 + Qt 6.10**, construido con el obs-plugintemplate (CMake). Requiere OBS 33+.
- **Licencia**: **GPL-2.0**. Puedes modificarlo y usarlo libremente. Si distribuyes el binario modificado, tienes que publicar también el código fuente bajo GPL. Para uso propio no hay ninguna obligación.
- **Cómo funciona**:
  1. **Dock Qt** (`src/fly_score_dock.cpp`, unas 2.200 líneas, la clase principal) con pestañas Team Stats / Single Stats / Timers / Events y diálogos aparte (teams, fields, timers, hotkeys).
  2. **Estado** `FlyState` (`src/include/fly_score_state.hpp` y `src/fly_score_state.cpp`). Se guarda en `plugin.json` dentro de la carpeta de plantilla que elige el usuario.
  3. **Servidor WebSocket propio** (QTcpServer, solo escucha en `127.0.0.1:4457`) en `src/fly_score_websocket_server.cpp`. Envía el estado completo `{type:"state", state}` en cada cambio y recibe comandos JSON `{type:"bump_score",...}`. Esos comandos se procesan en `FlyScoreDock::handleRemoteCommand` con una cadena de `if`.
  4. **Overlay HTML** (`data/overlay/*.html`, `script.js`, `style.css`). Cada HTML se agrega a mano como Browser Source de archivo local. `script.js` incluye un mini motor de plantillas (`{{expr}}`, `fs-if`). Recibe el estado por WebSocket y, si no hay conexión, lee `plugin.json`.
- **Hotkeys**: usan `QShortcut` con `Qt::ApplicationShortcut` y se guardan en un JSON propio. **Solo funcionan con la ventana de OBS enfocada**, porque no son hotkeys globales de OBS (`obs_hotkey_register_frontend`).
- **Archivos multimedia**: los logos se copian dentro de la carpeta de plantilla (`fly_copy_logo_to_overlay`) para que la Browser Source pueda cargarlos con rutas relativas. Las imágenes y videos de los anuncios se manejarían igual.
- **Build en Windows**: preset `windows-x64` / `windows-vs2026-x64` (Visual Studio 18 2026). El preset descarga el SDK de OBS 33 y Qt6 desde `buildspec.json`. El CI (`.github/workflows/build-project.yaml`, `windows-2025-vs2026`) genera el ZIP y el instalador NSIS. Para instalar, se extrae en `C:\ProgramData\obs-studio\plugins\fly-scoreboard\`.
- **Puntos de extensión**: `FlyState` y su (de)serialización, `handleRemoteCommand` (comandos WS), `init()` del dock (`addTab(...)`), `buildDefaultHotkeyBindings`/`applyHotkeyBindings`, `renderFrame()` en `script.js` (view model) y los HTML modulares (un HTML por panel, cada uno es su propia Browser Source).

## Enfoque recomendado: dos fases

### Fase 1: solo overlay, sin compilar (unas 0,5 a 1 día)
Se agrega `ads.html` y `ads.js`, más un `ads.json` con la playlist, a la carpeta de plantilla:
- Rotación de imágenes, videos y banners con orden y duración por anuncio, modo intervalo (por ejemplo, mostrar 15 s cada 5 min) y transición de entrada y salida.
- Posición y tamaño por CSS y parámetros de URL (`ads.html?pos=bottom-right&w=480`), o directamente moviendo la Browser Source en OBS.
- El control se hace con la visibilidad de la fuente en OBS, con hotkeys globales nativas de OBS ("Mostrar/Ocultar" de la fuente) y con "Refresh" de la Browser Source.
- Funciona con el plugin original, sin fork. Limitación: no hay controles en el dock de Fly y los cambios de playlist se hacen editando `ads.json`.

### Fase 2: integración nativa en el dock (unos 3 a 5 días)
| Archivo | Cambio |
|---|---|
| `src/include/fly_score_state.hpp` / `src/fly_score_state.cpp` | Nuevas estructuras `FlyAd {id, file, type(image/video/html), duration_ms, enabled}` y `FlyAdsConfig {items, mode(manual/loop/interval), interval_ms, show_ms, position, scale, visible, current_index, started_ms}`, con (de)serialización en `plugin.json` bajo `"ads"` |
| `src/include/fly_score_ads_dialog.hpp/.cpp` (nuevo) | Editor de la playlist: agregar archivos (se copian a `ads/` en la plantilla, como los logos), reordenar, duración, habilitar y posición |
| `src/fly_score_dock.cpp/.hpp` | Pestaña "Publicidad" con lista, ▶/⏸, Siguiente, Mostrar/Ocultar y modo; un `QTimer` que avanza la rotación y programa intervalos (el estado lo decide C++, así que todas las Browser Sources quedan sincronizadas) |
| `handleRemoteCommand` | Comandos `ads_play`, `ads_stop`, `ads_next`, `ads_prev`, `ads_show {index}`, `ads_hide`, `ads_set {config}` |
| Hotkeys | Acciones `ads_toggle`, `ads_next` y `ads_play_pause`. **Recomiendo** registrarlas con `obs_hotkey_register_frontend` para que sean globales y aparezcan en Configuración → Atajos de OBS |
| `src/fly_score_event_log.cpp` | Registrar "Ad: X mostrado" en el log de eventos, útil como comprobante para los anunciantes |
| `data/overlay/ads.html` + extensión de `script.js` | El panel lee `ads` del estado, elige el anuncio actual (`current_index`, `started_ms`) y maneja video con autoplay/muted y fade |
| `CMakeLists.txt` | Agregar los nuevos .cpp y `ads.html` a `EMBED_TEXT_FILES` |
| `data/locale/en-US.ini`, `ro-RO.ini` (+ `es-ES.ini` opcional) | Textos nuevos |
| `tests/` | Prueba de la lógica de rotación del overlay (como `overlay-timer.test.cjs`) |

Build: hacer un fork en GitHub y dejar que Actions compile el ZIP de Windows (no hace falta instalar VS). La alternativa es compilar localmente con VS 2026, el preset `windows-vs2026-x64` y `cmake --build --preset windows-x64`.

## Actualización (nuevos requisitos): auspiciantes, avisos de jugador, hotkeys y escudos

### Qué ya existe
- **Escudo/logo de cada equipo: YA EXISTE.** Está en el diálogo *Teams*: para local y visita se elige un archivo de imagen, que se copia a la carpeta de plantilla (`fly_copy_logo_to_overlay`). El overlay lo muestra con `{{team_x.logo}}` y `{{team_y.logo}}`, y también hay título, subtítulo y color. Solo haría mejoras menores y opcionales: mostrar el escudo dentro del aviso de gol o tarjeta.
- **Contadores de goles, amarillas y rojas: existen** como filas de estadísticas (`fields_by_key.score`, `yellow_cards`, `red_cards`) y se ven en `index.html` y `cards.html`. **No hay jugadores ni avisos**; en el código no aparece ninguna plantilla de jugadores.
- **Hotkeys: existen, pero son `QShortcut`**, así que solo funcionan con OBS enfocado. No cubren nada de esto.

### Qué hay que construir
1. **Auspiciantes** (aprox. 1 día, junto con la publicidad).
   - Lista `sponsors[] {name, logo}` en el estado. Los logos se copian a `sponsors/` en la plantilla.
   - Diálogo para agregar, quitar y ordenar.
   - Panel `sponsors.html` con un banner tipo franja: todos fijos o rotando uno a la vez, con la duración configurable.
   - Se muestra u oculta desde el dock o con un hotkey.
   - Es un caso particular del motor de anuncios y comparte código con él. Alternativa sin compilar: `sponsors.html` con una lista en JSON.
2. **Plantel de jugadores** (aprox. 1 día).
   - `FlyPlayer {number, name}` en `home.players[]` y `away.players[]`, guardado en `plugin.json`.
   - Se edita en el diálogo *Teams*, en una pestaña "Jugadores" por equipo.
   - Se puede pegar una lista con el formato "10 Juan Pérez".
3. **Avisos de gol, amarilla y roja** (aprox. 1,5 a 2 días).
   - Nueva pestaña "Novedades" en el dock: se elige el equipo y luego el jugador en un desplegable con número y nombre, y se pulsa ⚽ Gol, 🟨 Amarilla o 🟥 Roja.
   - Cada botón hace tres cosas a la vez:
     - suma el contador que corresponde (marcador o tarjetas), así ya no hay que hacerlo a mano;
     - muestra el aviso en pantalla durante N segundos (`alert {type, team, player, minute, started_ms}` en el estado);
     - lo anota en el log de eventos con el minuto del timer 0.
   - Botones extra: "Ocultar aviso" y "Deshacer último" (revierte el contador).
   - Overlay `alerts.html` con una animación por tipo (gol, amarilla, roja) que muestra el escudo del equipo, el número y el nombre del jugador. Es una Browser Source aparte, así que se posiciona como cualquier fuente.
   - Comandos WS `alert_goal`, `alert_card {team, player, color}` y `alert_hide`, para controlarlo desde el móvil u otro panel.
4. **Hotkeys globales de OBS** (aprox. 1 día).
   - Se registran con `obs_hotkey_register_frontend`, aparecen en *Configuración → Atajos* y funcionan aunque OBS no esté enfocado.
   - Elegir un jugador con el teclado no es práctico, así que los hotkeys usan el **jugador seleccionado en el dock**. Si no hay ninguno, el aviso sale sin nombre.
   - Acciones:
     - Gol local / Gol visita
     - Amarilla local / Amarilla visita
     - Roja local / Roja visita
     - Ocultar aviso
     - Siguiente jugador / anterior (para preseleccionar sin mouse)
     - Mostrar/ocultar auspiciantes
     - Mostrar/ocultar publicidad y siguiente anuncio
   - Los hotkeys actuales (marcador, timers) también se pueden migrar al sistema de OBS. Es opcional.

5. **Tanda de penales** (≈1,5 días).
   - Estado `shootout {active, rounds(5 por defecto), kicks_home[], kicks_away[], first_team}`. Cada tiro guarda `{result: goal|miss, player}`.
   - Pestaña "Penales" en el dock con un botón *Iniciar tanda* que la activa y muestra el panel.
   - Por cada equipo hay botones ✔ *Acierta* y ✘ *Falla*. Se puede elegir el pateador del plantel (opcional).
   - Botones *Deshacer último* y *Terminar tanda*.
   - Lógica automática:
     - marca de quién es el turno;
     - cuenta aciertos y fallos;
     - pasa a **muerte súbita** si siguen empatados después de 5 tiros;
     - **detecta el ganador anticipado**, cuando el otro equipo ya no puede alcanzarlo.
   - Overlay `penalties.html`, una Browser Source aparte:
     - fila por equipo con escudo y nombre;
     - círculos ✔ verde / ✘ rojo / ○ pendiente;
     - total de aciertos y fallos de cada equipo, por ejemplo "4 – 3";
     - aviso de ganador al final.
   - Los goles de la tanda **no se suman al marcador** del partido; se muestran aparte, como en la TV.
   - Hotkeys de OBS: acierta/falla local, acierta/falla visita, deshacer y mostrar/ocultar penales.
   - Comandos WS `shootout_start`, `shootout_kick {team, result, player}`, `shootout_undo` y `shootout_end`.
   - Cada tiro se anota en el log de eventos.

### Archivos adicionales a tocar
| Archivo | Cambio |
|---|---|
| `fly_score_state.hpp/.cpp` | `FlyPlayer`, `players[]` en `FlyTeam`, `sponsors[]` y `alert` en `FlyState` (más su JSON) |
| `fly_score_teams_dialog.cpp/.hpp` | Pestaña de jugadores por equipo |
| nuevo `fly_score_alerts_panel.cpp/.hpp` | Pestaña "Novedades" del dock |
| nuevo `fly_score_shootout_panel.cpp/.hpp` + `data/overlay/penalties.html` | Pestaña "Penales" y panel de la tanda; `shootout` en `FlyState` |
| nuevo `fly_score_sponsors_dialog.cpp/.hpp` | Editor de auspiciantes |
| nuevo `fly_score_obs_hotkeys.cpp/.hpp` | Registro y guardado de los hotkeys de OBS (`obs_hotkey_save/load` en la configuración del plugin) |
| `fly_score_dock.cpp` | Integrar las pestañas nuevas y los comandos WS nuevos en `handleRemoteCommand` |
| `data/overlay/alerts.html`, `sponsors.html`, `ads.html`, `style.css`, `script.js` | Paneles nuevos; exponer `alert` y `sponsors` en el view model |
| `CMakeLists.txt`, locales (agregar `es-ES.ini`), `tests/` | Integración, traducción al español y pruebas de la lógica del overlay |

### Esfuerzo total revisado
Aprox. **7,5 a 10,5 días** con todo: publicidad, auspiciantes, jugadores, avisos, penales y hotkeys globales.

Orden sugerido:
1. Jugadores y avisos con hotkeys, que es lo más útil en el partido.
2. Penales.
3. Auspiciantes y publicidad.

Todo esto requiere compilar el plugin modificado (fork y GitHub Actions, o VS 2026 local). Solo los auspiciantes y la publicidad se pueden adelantar sin compilar (Fase 1).

### Riesgos
- `fly_score_dock.cpp` ya es muy grande. Conviene sacar la pestaña de anuncios a su propio archivo (`fly_score_ads_panel.cpp`) para no complicar los merges con el upstream.
- Videos en Browser Source: usar mp4 (H.264) o webm, `muted` si no deben llevar audio. Un video con audio necesita "Control audio via OBS".
- Hay que elegir el contenido con cuidado: las plataformas de streaming tienen reglas sobre anuncios sobreimpuestos.

## Estado de implementación (rama `feat/auspiciantes-avisos`)

Hecho (pendiente de compilar y probar en OBS):
- **Plantel de jugadores**: en *Equipos*, un cuadro por equipo con un jugador por línea ("10 Juan Pérez"). Se guarda en `plugin.json` (`home.players` / `away.players`).
- **Pestaña Novedades**: jugador local o visita y botones Gol, Amarilla y Roja.
  - Cada botón suma el contador (fila 1: goles, 3: amarillas, 4: rojas), muestra el aviso y lo anota en el registro.
  - Se puede configurar cuántos segundos queda en pantalla, ocultarlo o deshacer el último.
  - Overlay: `alerts.html`.
- **Pestaña Penales**:
  - quién patea primero, cantidad de tiros por equipo e Iniciar/Terminar tanda;
  - pateador opcional, botones Acierta y Falla, y Deshacer;
  - mostrar u ocultar la tanda en pantalla.
  - Overlay: `penalties.html`, con aciertos y fallos por equipo, de quién es el turno, muerte súbita y ganador anticipado. Los goles de la tanda no tocan el marcador.
- **Pestaña Auspiciantes**:
  - agregar logos (se copian a la plantilla), renombrar con doble clic, ordenar y quitar;
  - modo uno a la vez (cada N s) o todos juntos.
  - Overlay: `sponsors.html`.
- **Atajos globales de OBS** (Configuración → Atajos → "Fly Score: …"):
  - gol, amarilla y roja por equipo, con el jugador elegido;
  - ocultar y deshacer la novedad;
  - jugador siguiente o anterior por equipo;
  - penal acierta o falla por equipo, deshacer penal y mostrar/ocultar penales;
  - mostrar/ocultar auspiciantes y mostrar/ocultar marcador.
  - Se guardan con la colección de escenas.
- **Comandos WebSocket**:
  - `alert` (`kind`) / `alert_goal` / `alert_yellow` / `alert_red`, con `{team, player | player_number, player_name}`;
  - `alert_hide`, `alert_undo` y `set_players`;
  - `shootout_start`, `shootout_kick {team, result: goal|miss}`, `shootout_undo`, `shootout_end` y `shootout_visibility`;
  - `sponsors_visibility`.
- **Idioma**: `es-ES.ini` completo, y los textos nuevos también en `en-US.ini`.
- **Pruebas**: `tests/overlay-match.test.cjs` cubre avisos, penales (ganador anticipado, muerte súbita, lados invertidos) y rotación de auspiciantes.

Pendiente:
- Publicidad en video/imagen con programación por intervalos (`ads.html`).
- Compilar: en esta PC no hay compilador C++. Opciones:
  - un fork en GitHub con Actions (requiere hacer push);
  - instalar el componente "Desarrollo para el escritorio con C++" de Visual Studio 2026 y usar `cmake --preset windows-x64`.

Uso en OBS: después de instalar, copia de nuevo la plantilla "Modular Football" (o copia `alerts.html`, `penalties.html` y `sponsors.html`, junto con `script.js` y `style.css` actualizados, a tu carpeta de plantilla). Luego agrega cada HTML como Browser Source.
