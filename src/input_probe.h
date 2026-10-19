#ifndef INPUT_PROBE_H
#define INPUT_PROBE_H

#include <stdbool.h>

// Set by --input-probe (params.c, CLI id 454).
extern bool input_probe_requested;

// Input diagnostic (T3b.0): opens the normal window and logs every SDL event
// it receives (raw SDL_PollEvent, no game handlers) to input-probe.log in the
// user directory and to stderr.  Returns on Esc pressed three times within
// two seconds, on window close, or after five minutes.  The caller owns the
// video and joystick setup and teardown.
void input_probe_run(void);

#endif
