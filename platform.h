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

/* An opaque, platform-dependent "context" type associated with a hotkey event. */
typedef const struct hotkey_context* hotkey_context_t;
typedef void (*hotkey_callback_t)(hotkey_context_t ctx, void* arg);

int bind_hotkey(const char* keystr, hotkey_callback_t cb, void* arg);
keycode_t* get_hotkey_modifiers(hotkey_context_t ctx);

int grab_inputs(void);
void ungrab_inputs(void);

char* get_clipboard_text(void);
int set_clipboard_text(const char* text);

void process_events(void);

/*
 * Initialized by platform_init().
 */
extern struct xypoint screen_center;

int platform_init(int* fd);
void platform_exit(void);

#endif /* PLATFORM_H */
