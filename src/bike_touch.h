/*
 * bike_touch.h - touchscreen controls for the exercise-bike integration
 * (T3b.3): relative drag-to-steer anywhere on the playfield, tap zones on the
 * side panels and a two-finger tap for the in-game menu.  See MODERNIZATION
 * section 4.14 and doc/ftms/design.md.
 *
 * Everything here is inert unless master && touch is on, and acts only while
 * the bike gate is open (single-player play; never 2P, network or demo), so the
 * stock input paths stay byte-identical.  Menus keep SDL's tap-as-click.
 */
#ifndef BIKE_TOUCH_H
#define BIKE_TOUCH_H

#include <SDL3/SDL.h>

#include <stdbool.h>
#include <stdint.h>

/* master && touch: touch is switched on (menus included). */
bool bike_touch_on(void);

/* on && the bike gate: single-player level play. */
bool bike_touch_play(void);

/* keyboard.c, first thing for every polled event.  Consumes FINGER events and
 * returns true for them; also returns true for a mouse event SDL synthesized
 * from a finger that started in play (it would double-count or click-fire).
 * False means the caller handles the event as before. */
bool bike_touch_filter(const SDL_Event *ev);

/* keyboard.c: a mouse event with this id came from a finger while touch is on;
 * the caller then skips the air-mouse translation (taps in menus act on the row
 * under the finger). */
bool bike_touch_is_touch_mouse(SDL_MouseID which);

/* Once per level-loop pass (from bike_controls_frame): pending tap-zone
 * edges (auto-fire toggle, pause, in-game menu, two-finger tap). */
void bike_touch_frame(void);

/* mainint.c, player 1, in play: add this frame's drag to the mouse delta path
 * (mouseXC/YC, scaled by the pointer sensitivity) and hold the game buttons of
 * the zones being touched.  Gate closed or touch off: does nothing. */
void bike_touch_input(int *dx, int *dy, bool button[4]);

/* Modern HUD (presentation only): the tap zones drawn on the side panels. */
typedef struct
{
	bool right;       /* right panel (else left) */
	int y0, y1;       /* rows [y0, y1) of the 200-row panel */
	const char *label;
	bool lit;         /* held now, or the toggle is on */
	bool dim;         /* unavailable (auto-fire setting off) */
} BikeTouchZone;
int bike_touch_zone_count(void);
bool bike_touch_zone_info(int index, BikeTouchZone *out);
/* The HUD draws the zones only while touch is on in single-player play. */
bool bike_touch_zones_visible(void);

/* --regress-bike-events: "finger_down|finger_move|finger_up", arg "id,x,y"
 * (x, y in 1/1000 of the window; up takes only the id).  Same handlers as the
 * real SDL events; `tick` is the frame tick (14 ms each) for the two-finger
 * timing. */
bool bike_touch_inject(const char *action, const char *arg, uint32_t tick);

void bike_touch_log_coverage(void); /* one "Bike touch:" line (regress coverage) */

#endif
