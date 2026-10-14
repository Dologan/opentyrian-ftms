/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef OPENTYR_H
#define OPENTYR_H

#include <SDL3/SDL_stdinc.h>

// SDL2's SDL.h pulled in the C standard library headers below transitively;
// SDL3's SDL_stdinc.h no longer does, so include them explicitly here for the
// whole project (as it previously relied on SDL.h to provide them).
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#ifndef COUNTOF
#define COUNTOF(x) (sizeof(x) / sizeof *(x))  // use only on arrays!
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#ifndef M_PI
#define M_PI    3.14159265358979323846  // pi
#endif
#ifndef M_PI_2
#define M_PI_2  1.57079632679489661923  // pi/2
#endif
#ifndef M_PI_4
#define M_PI_4  0.78539816339744830962  // pi/4
#endif

typedef unsigned int uint;
typedef unsigned long ulong;

// Pascal types, yuck.
typedef Sint32 JE_longint;
typedef Sint16 JE_integer;
typedef Sint8  JE_shortint;
typedef Uint16 JE_word;
typedef Uint8  JE_byte;
typedef bool   JE_boolean;
typedef char   JE_char;
typedef float  JE_real;

extern const char *opentyrian_str;
extern const char *opentyrian_version;

// Screens of the in-game Setup menu.  Exposed so the regress harness
// (--regress-screen) can open one directly; setupMenu() uses the same values.
enum
{
	SETUP_MENU_NONE = 0,
	SETUP_MENU_SETUP = 1,
	SETUP_MENU_GRAPHICS = 2,
	SETUP_MENU_SOUND = 3,
	SETUP_MENU_BIKE = 4,
};

void setupMenu(void);

// Regress hook: the next setupMenu() opens on `menu` (a SETUP_MENU_* value)
// instead of the top level.  Does not change normal behaviour.
void setupMenuStartAt(int menu);

#endif /* OPENTYR_H */
