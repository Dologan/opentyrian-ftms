/*
 * bike_menu.h - the "Bike and Controls" setup menu (T2.5).
 *
 * One scrolling list of rows over bike_settings.  Every change is written to
 * bike_settings at once (nothing is cached elsewhere), so it takes effect on
 * the next frame tick and is saved with the rest of the configuration.
 * Presentation only: the menu never touches game state.
 */
#ifndef BIKE_MENU_H
#define BIKE_MENU_H

#include <stdbool.h>

/* Runs the menu until Done / Esc.  `in_level` draws it as a shaded overlay on the
 * frozen game screen (the caller has set VGAScreen), and restores VGAScreen and
 * VGAScreen2 on exit; otherwise it paints the Setup background itself. */
void bike_menu(bool in_level);

/* True once the menu was opened from a level in this run, so the in-level entry
 * stays available after the master switch is turned off there. */
bool bike_menu_in_level_latched(void);

/* Regress only: queues a scripted key sequence for the next bike_menu() call.
 * ^ _ [ ] = arrows, # = Enter, ! = Esc, < = Backspace, any other char = text. */
void bike_menu_regress_keys(const char *keys);

#endif
