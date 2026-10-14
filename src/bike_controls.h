/*
 * bike_controls.h - in-play controls for the exercise-bike integration:
 * the auto-fire toggle and its energy reserve (T3b.1).  See doc/ftms/design.md
 * section 6.  Nothing here adds a keySettings slot, and nothing acts while the
 * bike gate is closed, so network games, demos and 2P are never touched.
 */
#ifndef BIKE_CONTROLS_H
#define BIKE_CONTROLS_H

#include <stdbool.h>
#include <stdint.h>

/* Reserve percentages are of the 900-unit pool: held back while power < pct * 9. */
#define BIKE_POWER_MAX 900u

/* Once per level-loop pass (from bike_frame_tick): edge detection on the toggle
 * inputs and the injected --regress-bike-events.  Edges are ignored while the
 * gate is closed or auto-fire is not available. */
void bike_controls_frame(void);

/* The mainint.c hook: true when auto-fire should press the fire button now.
 * `power` is the generator pool.  Manual fire is OR-ed by the caller, so the
 * reserve never blocks it. */
bool bike_controls_autofire(unsigned power);

/* Regress: the injected manual fire button (events fire_on / fire_off).  Gate
 * closed -> false.  It is manual fire, so the reserve never holds it back. */
bool bike_controls_injected_fire(void);

/* Flip the latch (no-op unless auto-fire is available).  Used by the input
 * edges, the injected events and, later, touch zones. */
void bike_controls_toggle(void);

/* HUD accessors.  active = latch on and available; held_back = active but the
 * reserve is currently holding fire back.  Read-only. */
bool bike_autofire_active(void);
bool bike_autofire_held_back(void);

/* --regress-bike-events=FILE: lines "tick,action[,a[,b]]" (ticks ascending,
 * '#' comments).  Actions: autofire_toggle, autofire_on, autofire_off,
 * set (a = "key=value"), fire_on, fire_off (injected manual fire), mouse_move,dx,dy (one-shot
 * pointer motion), mouse_drive,dx,dy (per-frame motion until changed), mouse_down/mouse_up,l|r|m,
 * key_down/key_up,play_pause|next|prev (injected media keys), immortal (regress: youAreCheating), equip (regress only: special=N,
 * sidekicks=L/R, shield=N on player 1). */
bool bike_controls_events_load(const char *path);

/* Regress state: true once the latch was ever set, so
 * the state digest of cases that never use auto-fire is unchanged. */
bool bike_controls_digest_active(void);
uint64_t bike_controls_digest(void);

void bike_controls_log_coverage(void); /* one "Bike autofire:" line (regress coverage) */
void bike_controls_shutdown(void);

/* ---- Air mouse and remote buttons (T3b.2) --------------------------------
 * Everything below is inert unless master && air_mouse is on, so the stock
 * input paths stay byte-identical.  See MODERNIZATION section 4.14. */

/* master && air_mouse: the remote translation for menus and screens.  No gate:
 * it also applies in the setup menu, the shop and the title screen. */
bool bike_controls_remote(void);

/* remote() && the bike gate: single-player play only (never 2P, network, demo). */
bool bike_controls_air_play(void);

/* keyboard.c: the key an SDL mouse button becomes in remote mode: left (OK) ->
 * SDL_SCANCODE_RETURN, right (Back) -> SDL_SCANCODE_ESCAPE, any other button
 * -> -1 (dropped, so no menu reacts to it).  0 when remote mode is off (the
 * caller then keeps the stock button path). */
int bike_controls_remote_button_key(int sdl_button);

/* mainint.c, player 1, in play: scale a frame's relative pointer motion by the
 * sensitivity (100% = stock, remainders carried) and add the injected motion. */
void bike_controls_pointer(int *dx, int *dy);

/* mainint.c, in play: pointer buttons -> game buttons.  Left (OK) holds fire,
 * the middle button keeps its stock action; right (Back) is not a game button,
 * it opens the in-game menu through bike_controls_frame. */
void bike_controls_air_buttons(uint8_t buttons_down, bool button[4]);

/* mainint.c, in play: media next -> change rear fire mode, previous -> both
 * sidekicks (held, like a keyboard key).  Play/pause is an edge in the frame. */
void bike_controls_media_buttons(bool button[4]);

#endif
