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
#include "video.h"

#include "crt_filter.h"
#include "drawlist.h"
#include "keyboard.h"
#include "logging.h"
#include "modern.h"
#include "opentyr.h"
#include "regress.h"
#include "video_scale.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

const char *const scaling_mode_names[ScalingMode_MAX] = {
	"Center",
	"Integer",
	"Fit",
};

int fullscreen_display;
ScalingMode scaling_mode = SCALE_INTEGER;
static SDL_Rect last_output_rect = { 0, 0, vga_width, vga_height };
// Geometry of the last presented output, for mouse mapping.  Classic always
// presents the 320x200 8-bit frame (canvas == frame, offset 0); Modern can
// present a wider canvas with the game frame centered in it.
static int last_output_canvas_w = vga_width;
static int last_output_canvas_h = vga_height;
static int last_output_frame_x = 0;
static int last_output_frame_y = 0;
// Piecewise mapping for the widened pic-1 layout (see
// video_set_last_output_rect_split); false unless that call was the last one.
static bool last_output_split = false;
static int last_output_split_l = 0, last_output_split_r = 0;
static int last_output_insert_l = 0, last_output_insert_r = 0;

SDL_Surface *VGAScreen, *VGAScreenSeg;
SDL_Surface *VGAScreen2;
SDL_Surface *game_screen;

SDL_Window *main_window = NULL;
static SDL_Renderer *main_window_renderer = NULL;
const SDL_PixelFormatDetails *main_window_tex_format = NULL;
static SDL_Texture *main_window_texture = NULL;

// Intermediate render target for the sharp-bilinear path (see
// present_sharp_bilinear).  Recreated only when its size changes, never per
// frame.
static SDL_Texture *scaled_target = NULL;
static int scaled_target_w = 0, scaled_target_h = 0;

static void init_renderer(void);
static void deinit_renderer(void);
static void init_texture(void);
static void deinit_texture(void);

static SDL_DisplayID window_get_display(void);
static void window_center_in_display(SDL_DisplayID display_id);
static void scale_and_flip(SDL_Surface *);

void init_video(void)
{
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
	{
		logFatal("Failed to initialize SDL video: %s", SDL_GetError());
		exit(EXIT_FAILURE);
	}

	// Create the software surfaces that the game renders to. These are all 320x200x8 regardless
	// of the window size or monitor resolution.
	VGAScreen = VGAScreenSeg = SDL_CreateSurface(vga_width, vga_height, SDL_PIXELFORMAT_INDEX8);
	VGAScreen2 = SDL_CreateSurface(vga_width, vga_height, SDL_PIXELFORMAT_INDEX8);
	game_screen = SDL_CreateSurface(vga_width, vga_height, SDL_PIXELFORMAT_INDEX8);

	// The game code writes to surface->pixels directly without locking, so make sure that we
	// indeed created software surfaces that support this.
	assert(!SDL_MUSTLOCK(VGAScreen));
	assert(!SDL_MUSTLOCK(VGAScreen2));
	assert(!SDL_MUSTLOCK(game_screen));

	JE_clr256(VGAScreen);

	// Create the window with a temporary initial size, hidden until we set up the
	// scaler and find the true window size.  HIGH_PIXEL_DENSITY makes the
	// backbuffer the display's native pixels on a HiDPI (Retina) screen, so the
	// presentation runs at the physical resolution instead of being upscaled by
	// the OS; on other displays it is a no-op.
	main_window = SDL_CreateWindow("OpenTyrian",
		vga_width, vga_height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);

	if (main_window == NULL)
	{
		logFatal("Failed to create window: %s", SDL_GetError());
		exit(EXIT_FAILURE);
	}

	if (regress_active())
		logInfo("Regression: SDL video driver is '%s'.", SDL_GetCurrentVideoDriver());

	reinit_fullscreen(fullscreen_display);
	init_renderer();
	init_texture();
	modern_init();
	if (regress_active() && regress_crt_window_w > 0)
	{
		SDL_SetWindowSize(main_window, regress_crt_window_w, regress_crt_window_h);
		SDL_SyncWindow(main_window);
		modern_update_canvas_size();
	}

	SDL_ShowWindow(main_window);

	SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
	SDL_RenderClear(main_window_renderer);
	SDL_RenderPresent(main_window_renderer);
}

void deinit_video(void)
{
	modern_deinit();
	crt_filter_quit();
	deinit_texture();

	if (scaled_target != NULL)
	{
		SDL_DestroyTexture(scaled_target);
		scaled_target = NULL;
	}
	scaled_target_w = 0;
	scaled_target_h = 0;

	deinit_renderer();
	SDL_DestroyWindow(main_window);

	SDL_DestroySurface(VGAScreenSeg);
	SDL_DestroySurface(VGAScreen2);
	SDL_DestroySurface(game_screen);

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static void init_renderer(void)
{
	main_window_renderer = SDL_CreateRenderer(main_window, NULL);

	if (main_window_renderer == NULL)
	{
		logFatal("Failed to create renderer: %s", SDL_GetError());
		exit(EXIT_FAILURE);
	}
}

static void deinit_renderer(void)
{
	if (main_window_renderer != NULL)
	{
		SDL_DestroyRenderer(main_window_renderer);
		main_window_renderer = NULL;
	}
}

static void init_texture(void)
{
	assert(main_window_renderer != NULL);

	SDL_PixelFormat format = SDL_PIXELFORMAT_XRGB8888;

	main_window_tex_format = SDL_GetPixelFormatDetails(format);

	// The Classic frame is converted to this 320x200 texture; the GPU scales it
	// to the window (see video_present_texture).
	main_window_texture = SDL_CreateTexture(main_window_renderer, format, SDL_TEXTUREACCESS_STREAMING, vga_width, vga_height);

	if (main_window_texture == NULL)
	{
		logFatal("Failed to create the frame texture (%dx%dx%s): %s", vga_width, vga_height, SDL_GetPixelFormatName(format), SDL_GetError());
		exit(EXIT_FAILURE);
	}

	// The 1x frame must stay crisp when it is drawn; the sharp-bilinear path
	// flips this to LINEAR only for its final fractional pass.
	SDL_SetTextureScaleMode(main_window_texture, SDL_SCALEMODE_NEAREST);
}

static void deinit_texture(void)
{
	if (main_window_texture != NULL)
	{
		SDL_DestroyTexture(main_window_texture);
		main_window_texture = NULL;
	}

	main_window_tex_format = NULL;
}

static SDL_DisplayID window_get_display(void)
{
	return SDL_GetDisplayForWindow(main_window);
}

// Presentation is done in the window's native pixels, so all the fit/integer/
// center rects, the sharp-bilinear prescale and its cached target are sized for
// the display's physical resolution.  On a HiDPI (Retina) screen that is twice
// the point size SDL_GetWindowSize reports, and the OS no longer upscales the
// result; on every other display the two are identical.
static void window_size_in_pixels(int *out_w, int *out_h)
{
	int w = 0, h = 0;

	if (!SDL_GetWindowSizeInPixels(main_window, &w, &h) || w <= 0 || h <= 0)
		SDL_GetWindowSize(main_window, &w, &h);

	*out_w = w;
	*out_h = h;
}

// SDL reports mouse coordinates in window points, not native pixels.  Convert
// a point coordinate to the pixel space of the presentation rects (and back);
// both are the identity when the window is not on a HiDPI display.
static void window_points_to_pixels(Sint32 *x, Sint32 *y)
{
	int pt_w = 0, pt_h = 0, px_w = 0, px_h = 0;

	SDL_GetWindowSize(main_window, &pt_w, &pt_h);
	window_size_in_pixels(&px_w, &px_h);

	if (pt_w > 0 && pt_h > 0 && px_w > 0 && px_h > 0)
	{
		*x = (Sint32)lroundf((float)*x * (float)px_w / (float)pt_w);
		*y = (Sint32)lroundf((float)*y * (float)px_h / (float)pt_h);
	}
}

static void window_pixels_to_points(Sint32 *x, Sint32 *y)
{
	int pt_w = 0, pt_h = 0, px_w = 0, px_h = 0;

	SDL_GetWindowSize(main_window, &pt_w, &pt_h);
	window_size_in_pixels(&px_w, &px_h);

	if (pt_w > 0 && pt_h > 0 && px_w > 0 && px_h > 0)
	{
		*x = (Sint32)lroundf((float)*x * (float)pt_w / (float)px_w);
		*y = (Sint32)lroundf((float)*y * (float)pt_h / (float)px_h);
	}
}

static void window_center_in_display(SDL_DisplayID display_id)
{
	int win_w, win_h;
	SDL_GetWindowSize(main_window, &win_w, &win_h);

	SDL_Rect bounds;
	SDL_GetDisplayBounds(display_id, &bounds);

	SDL_SetWindowPosition(main_window, bounds.x + (bounds.w - win_w) / 2, bounds.y + (bounds.h - win_h) / 2);
}

// The on-screen width/height of the presented content for the current mode,
// used to shape the window.  Modern uses its target aspect (or the window's own
// for "auto"); Classic uses the 320x200 frame's display aspect, selected by the
// pixel aspect (4:3 for original, 8:5 for square).
static float content_aspect_for_mode(void)
{
	if (presentation == PRESENTATION_MODERN)
	{
		if (modern_aspect == MODERN_ASPECT_AUTO)
		{
			// Only the ratio matters, so points and pixels give the same value.
			int w = 0, h = 0;
			window_size_in_pixels(&w, &h);
			if (w > 0 && h > 0)
				return (float)w / (float)h;
		}
		return modern_aspect_ratio();
	}

	return modern_pixel_aspect == PIXEL_ASPECT_SQUARE ? (8.f / 5.f) : (4.f / 3.f);
}

// The windowed size for the current mode: the largest integer multiple of the
// 200 logical rows that fits in ~80% of the usable desktop, shaped like the
// content aspect.  The repo stores no window size, so this is also the startup
// default.  Without the old software scalers the Classic window no longer
// shrinks to 320x200.
static void windowed_size_for_mode(int *out_w, int *out_h)
{
	const float aspect = content_aspect_for_mode();
	int scale = 2;

	SDL_Rect usable;
	if (SDL_GetDisplayUsableBounds(window_get_display(), &usable) && usable.w > 0 && usable.h > 0)
	{
		const float fill = 0.8f;
		const float max_w = (float)usable.w * fill;
		const float max_h = (float)usable.h * fill;

		scale = (int)floorf(max_h / (float)vga_height);
		const int width_scale = (int)floorf(max_w / (aspect * (float)vga_height));
		if (width_scale < scale)
			scale = width_scale;
		if (scale < 1)
			scale = 1;
	}

	*out_w = (int)lroundf(aspect * (float)vga_height * (float)scale);
	*out_h = vga_height * scale;
}

static void set_windowed_size_for_mode(void)
{
	int w, h;
	windowed_size_for_mode(&w, &h);
	SDL_SetWindowSize(main_window, w, h);
	SDL_SyncWindow(main_window);
	window_center_in_display(window_get_display());
}

void video_fit_launcher_window(void)
{
	// Fullscreen entry and window resizes are asynchronous on Cocoa. The first
	// launcher frame must use the settled native output, including HiDPI.
	// A compositor that owns the window size (gamescope in Steam Deck Game
	// Mode) never applies the request, so the sync times out; the launcher
	// then draws at the size it got and follows later resize events.
	// SDL_SyncWindow sets no error on a timeout, so SDL_GetError() is stale.
	if (!SDL_SyncWindow(main_window))
		logWarn("The window did not settle before the launcher; using its current size.");
	if (fullscreen_display != -1)
		return;

	// The largest 16:9 window inside ~80% of the usable desktop.
	int w = 1280, h = 720;
	SDL_Rect usable;
	if (SDL_GetDisplayUsableBounds(window_get_display(), &usable) && usable.w > 0 && usable.h > 0)
	{
		h = (int)floorf((float)usable.h * 0.8f);
		w = h * 16 / 9;
		if (w > (int)((float)usable.w * 0.8f))
		{
			w = (int)floorf((float)usable.w * 0.8f);
			h = w * 9 / 16;
		}
		if (h < vga_height)
		{
			w = vga_width * 2;
			h = w * 9 / 16;
		}
	}

	if (!SDL_SetWindowSize(main_window, w, h) || !SDL_SyncWindow(main_window))
		logWarn("The launcher window resize did not settle; using the size the window manager chose.");
	window_center_in_display(window_get_display());
	modern_update_canvas_size();
}

void video_apply_display_settings(void)
{
	// Called when the presentation/aspect/pixel-aspect settings change at
	// runtime.  Refit the windowed window to the new mode, then let the Modern
	// canvas follow the new window and geometry.  Classically this is a no-op
	// beyond re-centering at the scaler size.
	if (fullscreen_display == -1)
		set_windowed_size_for_mode();

	modern_update_canvas_size();
}

void reinit_fullscreen(int new_display)
{
	int display_count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&display_count);

	fullscreen_display = new_display;

	if (displays == NULL)
	{
		fullscreen_display = -1;
	}
	else if (fullscreen_display >= display_count)
	{
		fullscreen_display = 0;
	}

	// Preserve Auto's current aspect before leaving fullscreen: SDL may report
	// the old restored window (initially 320x200) during the transition.
	int w, h;
	windowed_size_for_mode(&w, &h);
	SDL_SetWindowFullscreen(main_window, false);
	SDL_SyncWindow(main_window);
	SDL_SetWindowSize(main_window, w, h);
	SDL_SyncWindow(main_window);

	if (fullscreen_display == -1)
	{
		window_center_in_display(window_get_display());
	}
	else
	{
		window_center_in_display(displays[fullscreen_display]);

		// SDL3 has no SDL_WINDOW_FULLSCREEN_DESKTOP flag: leaving the window's
		// fullscreen mode unset (NULL) selects the borderless desktop mode.
		if (!SDL_SetWindowFullscreen(main_window, true))
		{
			SDL_free(displays);
			reinit_fullscreen(-1);
			return;
		}
	}

	SDL_free(displays);

	// Settle the display transition before allocating the canvas/HUD surfaces.
	SDL_SyncWindow(main_window);
	modern_update_canvas_size();
}

void video_on_win_resize(void)
{
	int w, h;

	// Tell video to reinit if the window was manually resized by the user.
	// Also enforce a minimum size on the window (the logical frame).  This is a
	// window geometry limit, so it stays in points (the unit SDL_SetWindowSize
	// takes) to keep the same on-screen minimum as before; the pixel backbuffer
	// is at least as large, so the logical frame always fits.

	SDL_GetWindowSize(main_window, &w, &h);

	if (w < vga_width || h < vga_height)
	{
		w = w < vga_width ? vga_width : w;
		h = h < vga_height ? vga_height : h;

		SDL_SetWindowSize(main_window, w, h);
		SDL_SyncWindow(main_window);
	}

	// "auto" derives the Modern canvas width from the window size.
	modern_update_canvas_size();
}

void toggle_fullscreen(void)
{
	if (fullscreen_display != -1)
	{
		reinit_fullscreen(-1);
	}
	else
	{
		// Go fullscreen on the display the window is currently on.  The config
		// stores an index into the display list, so map the window's display ID
		// back to its index.
		int display_count = 0;
		SDL_DisplayID *displays = SDL_GetDisplays(&display_count);
		SDL_DisplayID current = window_get_display();

		int index = 0;
		for (int i = 0; displays != NULL && i < display_count; ++i)
		{
			if (displays[i] == current)
			{
				index = i;
				break;
			}
		}

		SDL_free(displays);

		reinit_fullscreen(index);
	}
}

bool set_scaling_mode_by_name(const char *name)
{
	for (int i = 0; i < ScalingMode_MAX; ++i)
	{
		 if (strcmp(name, scaling_mode_names[i]) == 0)
		 {
			 scaling_mode = i;
			 return true;
		 }
	}
	return false;
}

void JE_clr256(SDL_Surface *screen)
{
	drawlist_record_fill_full(screen);
	SDL_FillSurfaceRect(screen, NULL, 0);

	// A cleared presented screen no longer shows the tracked backdrop; the
	// compositor falls back to the flat-edge / blurred fill until the next
	// JE_loadPic.  Temp buffers (ship specs) do not affect it.
	if (screen == VGAScreen)
		modern_backdrop_clear();
}

void JE_showVGA(void)
{
	scale_and_flip(VGAScreen);
}

SDL_Renderer *video_renderer(void)
{
	return main_window_renderer;
}

void video_set_last_output_rect(const SDL_Rect *rect)
{
	last_output_rect = *rect;
	last_output_canvas_w = vga_width;
	last_output_canvas_h = vga_height;
	last_output_frame_x = 0;
	last_output_frame_y = 0;
	last_output_split = false;
}

void video_set_last_output_rect_ex(const SDL_Rect *rect, int canvas_w, int canvas_h, int frame_x, int frame_y)
{
	last_output_rect = *rect;
	last_output_canvas_w = canvas_w;
	last_output_canvas_h = canvas_h;
	last_output_frame_x = frame_x;
	last_output_frame_y = frame_y;
	last_output_split = false;
}

void video_set_last_output_rect_split(const SDL_Rect *rect, int canvas_w, int canvas_h,
                                      int split_l, int split_r, int insert_l, int insert_r)
{
	last_output_rect = *rect;
	last_output_canvas_w = canvas_w;
	last_output_canvas_h = canvas_h;
	last_output_frame_x = 0;
	last_output_frame_y = 0;
	last_output_split = true;
	last_output_split_l = split_l;
	last_output_split_r = split_r;
	last_output_insert_l = insert_l;
	last_output_insert_r = insert_r;
}

// Canvas x -> game x for the widened layout: the frame is at the canvas left
// edge and the extra columns are the bands [split_l, split_l+insert_l) and
// [split_r+insert_l, split_r+insert_l+insert_r).  A point inside a band maps to
// its split column.
static Sint32 split_canvas_to_game_x(Sint32 cx)
{
	if (cx < last_output_split_l)
		return cx;

	if (cx < last_output_split_l + last_output_insert_l)
		return last_output_split_l;

	cx -= last_output_insert_l;

	if (cx < last_output_split_r)
		return cx;

	if (cx < last_output_split_r + last_output_insert_r)
		return last_output_split_r;

	return cx - last_output_insert_r;
}

// Game x -> canvas x, the inverse of split_canvas_to_game_x (game columns at or
// right of a split move with the columns inserted before them).
static Sint32 split_game_to_canvas_x(Sint32 gx)
{
	if (gx < last_output_split_l)
		return gx;

	if (gx < last_output_split_r)
		return gx + last_output_insert_l;

	return gx + last_output_insert_l + last_output_insert_r;
}

// Fits a `content_aspect`-shaped image inside win_w x win_h, centered.
static SDL_Rect fit_rect(int win_w, int win_h, float content_aspect)
{
	SDL_Rect r;
	const float maxh_width = win_h * content_aspect;
	const float maxw_height = win_w / content_aspect;

	if (maxh_width > win_w)
	{
		r.w = win_w;
		r.h = (int)maxw_height;
	}
	else
	{
		r.w = (int)maxh_width;
		r.h = win_h;
	}

	r.x = (win_w - r.w) / 2;
	r.y = (win_h - r.h) / 2;
	return r;
}

// Largest whole multiple of src_w x src_h that fits the window, centered.
static SDL_Rect integer_rect(int win_w, int win_h, int src_w, int src_h)
{
	SDL_Rect r = { 0, 0, src_w, src_h };

	while (r.w + src_w <= win_w && r.h + src_h <= win_h)
	{
		r.w += src_w;
		r.h += src_h;
	}

	r.x = (win_w - r.w) / 2;
	r.y = (win_h - r.h) / 2;
	return r;
}

// (Re)creates the intermediate render target only when its size changed.
static bool ensure_scaled_target(int w, int h)
{
	if (scaled_target != NULL && scaled_target_w == w && scaled_target_h == h)
		return true;

	if (scaled_target != NULL)
	{
		SDL_DestroyTexture(scaled_target);
		scaled_target = NULL;
	}

	scaled_target = SDL_CreateTexture(main_window_renderer, SDL_PIXELFORMAT_XRGB8888,
	                                  SDL_TEXTUREACCESS_TARGET, w, h);
	if (scaled_target == NULL)
	{
		logError("Failed to create the sharp-bilinear target (%dx%d): %s", w, h, SDL_GetError());
		scaled_target_w = 0;
		scaled_target_h = 0;
		return false;
	}

	scaled_target_w = w;
	scaled_target_h = h;
	return true;
}

// Sharp bilinear: a nearest-neighbour integer prescale into an intermediate
// render target, then one linear pass to the final rect.  The integer prescale
// is the largest that does not exceed the output, chosen per axis, so the
// intermediate keeps the source pixel grid and the linear pass only resolves
// the fractional remainder.  Every source pixel then lands on screen with the
// same width and height, instead of the uneven 4/5 px rows and columns a direct
// nearest-neighbour fractional fit produces (which shimmer while scrolling).
static void present_sharp_bilinear(SDL_Texture *texture, int src_w, int src_h, const SDL_Rect *dst)
{
	int kx = dst->w / src_w;
	int ky = dst->h / src_h;
	if (kx < 1)
		kx = 1;
	if (ky < 1)
		ky = 1;

	if (!ensure_scaled_target(src_w * kx, src_h * ky))
	{
		// Out of memory: fall back to a direct nearest blit, never drop the
		// frame.
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
		SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
		SDL_RenderClear(main_window_renderer);
		const SDL_FRect dst_frect = { (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h };
		SDL_RenderTexture(main_window_renderer, texture, NULL, &dst_frect);
		return;
	}

	// Integer nearest prescale into the target.
	SDL_SetRenderTarget(main_window_renderer, scaled_target);
	SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
	SDL_RenderClear(main_window_renderer);
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
	SDL_RenderTexture(main_window_renderer, texture, NULL, NULL);

	// Fractional linear pass to the window.
	SDL_SetRenderTarget(main_window_renderer, NULL);
	SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
	SDL_RenderClear(main_window_renderer);
	SDL_SetTextureScaleMode(scaled_target, SDL_SCALEMODE_LINEAR);
	const SDL_FRect dst_frect = { (float)dst->x, (float)dst->y, (float)dst->w, (float)dst->h };
	SDL_RenderTexture(main_window_renderer, scaled_target, NULL, &dst_frect);
}

SDL_Rect video_fit_rect(float content_aspect)
{
	int w, h;
	window_size_in_pixels(&w, &h);
	return fit_rect(w, h, content_aspect);
}

SDL_Rect video_present_texture(SDL_Texture *texture, int src_w, int src_h, float content_aspect, ScalingMode mode)
{
	// The window size in native pixels: on HiDPI this is what makes the final
	// linear pass of the sharp-bilinear path (and Center/Integer) land 1:1 on
	// the display instead of on a point-sized backbuffer the OS then upscales.
	int win_w, win_h;
	window_size_in_pixels(&win_w, &win_h);

	SDL_Rect dst;

	switch (mode)
	{
	case SCALE_CENTER:
		dst.w = src_w;
		dst.h = src_h;
		dst.x = (win_w - dst.w) / 2;
		dst.y = (win_h - dst.h) / 2;

		SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
		SDL_RenderClear(main_window_renderer);
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
		{
			const SDL_FRect dst_frect = { (float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h };
			SDL_RenderTexture(main_window_renderer, texture, NULL, &dst_frect);
		}
		break;

	case SCALE_INTEGER:
		dst = integer_rect(win_w, win_h, src_w, src_h);

		SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
		SDL_RenderClear(main_window_renderer);
		SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
		{
			const SDL_FRect dst_frect = { (float)dst.x, (float)dst.y, (float)dst.w, (float)dst.h };
			SDL_RenderTexture(main_window_renderer, texture, NULL, &dst_frect);
		}
		break;

	case SCALE_FIT:
	default:
		dst = video_fit_rect(content_aspect);
		present_sharp_bilinear(texture, src_w, src_h, &dst);
		break;
	}

	regress_capture_presented_frame(main_window_renderer);
	SDL_RenderPresent(main_window_renderer);
	return dst;
}

static void scale_and_flip(SDL_Surface *src_surface)
{
	assert(SDL_BITSPERPIXEL(src_surface->format) == 8);

	if (presentation == PRESENTATION_MODERN)
	{
		// CPU-composited canvas at the logical resolution: convert, run the
		// effect passes, then upload and present.  The GPU does the scaling.
		modern_build_frame(src_surface);

		if (regress_active())
			regress_capture_modern_frame();

		modern_present_frame();
		return;
	}

	if (regress_active())
		regress_capture_frame(src_surface);

	// Convert the 8-bit frame through the palette, 1x; the presentation path
	// scales it to the window.
	video_convert_frame(src_surface, main_window_texture);

	// Classic's 320x200 frame: the pixel aspect selects the display aspect used
	// by Fit (the original 1.2 gives 4:3, square gives 8:5; exactly the old
	// "Fit 4:3" and "Fit 8:5" rects).  Center and Integer ignore it.
	const float content_aspect = modern_pixel_aspect == PIXEL_ASPECT_SQUARE
		? (8.f / 5.f)
		: (4.f / 3.f);

	SDL_Rect dst_rect = video_present_texture(main_window_texture, src_surface->w, src_surface->h,
	                                          content_aspect, scaling_mode);

	// Save output rect to be used by mouse functions
	last_output_rect = dst_rect;
}

/** Maps a specified point in game screen coordinates to window points. */
void mapScreenPointToWindow(Sint32 *const inout_x, Sint32 *const inout_y)
{
	// The game frame is `last_output_frame_x/y` pixels into the presented
	// canvas, or its columns are the source of the widened canvas's pieces.
	Sint32 cx = last_output_split ? split_game_to_canvas_x(*inout_x)
	                              : *inout_x + last_output_frame_x;

	// last_output_rect is in native pixels; SDL's window coordinate is a point.
	*inout_x = (2 * cx + 1) * last_output_rect.w / (2 * last_output_canvas_w) + last_output_rect.x;
	*inout_y = (2 * (*inout_y + last_output_frame_y) + 1) * last_output_rect.h / (2 * last_output_canvas_h) + last_output_rect.y;

	window_pixels_to_points(inout_x, inout_y);
}

/** Maps a specified point in window coordinates to game screen coordinates. */
void mapWindowPointToScreen(Sint32 *const inout_x, Sint32 *const inout_y)
{
	// SDL hands mouse events in points; the mapping below is in pixels.
	window_points_to_pixels(inout_x, inout_y);

	Sint32 cx = (2 * (*inout_x - last_output_rect.x) + 1) * last_output_canvas_w / (2 * last_output_rect.w);

	*inout_x = last_output_split ? split_canvas_to_game_x(cx) : cx - last_output_frame_x;
	*inout_y = (2 * (*inout_y - last_output_rect.y) + 1) * last_output_canvas_h / (2 * last_output_rect.h) - last_output_frame_y;
}

/** Scales a distance in window coordinates to game screen coordinates. */
void scaleWindowDistanceToScreen(Sint32 *const inout_x, Sint32 *const inout_y)
{
	// The relative mouse deltas are in points too.
	window_points_to_pixels(inout_x, inout_y);

	*inout_x = (2 * *inout_x + 1) * last_output_canvas_w / (2 * last_output_rect.w);
	*inout_y = (2 * *inout_y + 1) * last_output_canvas_h / (2 * last_output_rect.h);
}
