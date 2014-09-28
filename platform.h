/*
 * Interfaces that must be implemented to support a platform.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include "types.h"

/*
 * gettimeofday() is sufficiently portable, but sadly non-monotonic.
 * clock_gettime() is monotonic (or at least can be), but sadly does not exist
 * on OSX, despite being in POSIX.1-2001.  So instead we have this, a
 * platform-specific microsecond-resolution monotonic time-since-an-epoch
 * function.
 */
uint64_t get_microtime(void);

struct xypoint get_mousepos(void);
void set_mousepos(struct xypoint pos);
void move_mousepos(int32_t dx, int32_t dy);

void do_clickevent(mousebutton_t button, pressrel_t pr);
void do_keyevent(keycode_t key, pressrel_t pr);

int grab_inputs(void);
void ungrab_inputs(void);

char* get_clipboard_text(void);
int set_clipboard_text(const char* text);

void process_events(void);

int platform_init(void);
void platform_exit(void);

#endif /* PLATFORM_H */
