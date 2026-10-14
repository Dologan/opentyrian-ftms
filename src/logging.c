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
#include "logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *log_file = NULL;

// The output function in effect before logOpenFile(), so messages keep going
// to stderr as well.
static SDL_LogOutputFunction default_log_output = NULL;
static void *default_log_userdata = NULL;

// Every SDL log message goes through here once a log file is open.  Chaining
// to the previous handler instead of replacing it leaves the terminal output
// (and SDL's own stderr diagnostics) untouched; the file is flushed per line
// so a crash still leaves a complete log.
static void logOutputToFile(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
	(void)userdata;

	if (default_log_output != NULL)
		default_log_output(default_log_userdata, category, priority, message);

	if (log_file != NULL)
	{
		fprintf(log_file, "%s\n", message);
		fflush(log_file);
	}
}

bool logOpenFile(const char *path)
{
	FILE *file = fopen(path, "w");
	if (file == NULL)
		return false;

	if (log_file != NULL)
		fclose(log_file);

	log_file = file;

	if (default_log_output == NULL)
	{
		SDL_GetLogOutputFunction(&default_log_output, &default_log_userdata);
		SDL_SetLogOutputFunction(logOutputToFile, NULL);
	}

	return true;
}

bool logFileIsOpen(void)
{
	return log_file != NULL;
}

const char *logFileFromArgs(int argc, char *argv[])
{
	static const char option[] = "--log-file";
	const size_t option_len = sizeof(option) - 1;

	for (int i = 1; i < argc; ++i)
	{
		if (strncmp(argv[i], option, option_len) != 0)
			continue;

		if (argv[i][option_len] == '=')
			return &argv[i][option_len + 1];

		if (argv[i][option_len] == '\0' && i + 1 < argc)
			return argv[i + 1];
	}

	return NULL;
}

SDL_PRINTF_VARARG_FUNC(1)
void logFatal(SDL_PRINTF_FORMAT_STRING const char *fmt, ...)
{
	char buffer[256];
	char *bufferPtr = buffer;
	size_t bufferSize = sizeof buffer;

	// Format the message once, then hand the same text to every destination
	// (a va_list cannot be consumed twice).
	for (;;)
	{
		va_list ap;
		va_start(ap, fmt);
		int len = SDL_vsnprintf(bufferPtr, bufferSize, fmt, ap);
		va_end(ap);

		if (len < 0)
		{
			bufferPtr[0] = '\0';
			break;
		}

		if ((size_t)len < bufferSize)
			break;

		// Truncated: retry once with an exact-size heap buffer.  If that is
		// not possible, keep the truncated text.
		if (bufferPtr != buffer)
			break;

		char *bigger = SDL_malloc((size_t)len + 1);
		if (bigger == NULL)
			break;

		bufferPtr = bigger;
		bufferSize = (size_t)len + 1;
	}

	SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_CRITICAL, "%s", bufferPtr);

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", bufferPtr, NULL);

	if (bufferPtr != buffer)
		SDL_free(bufferPtr);
}
