/*
 * Interfaces that must be implemented to support a platform.
 */

#ifndef PLATFORM_H
#define PLATFORM_H

#include "types.h"

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
keycode_t* get_current_modifiers(void);

int grab_inputs(void);
void ungrab_inputs(int restore_mousepos);

char* get_clipboard_text(void);
int set_clipboard_text(const char* text);

void set_display_brightness(float f);

/*
 * Initialized by platform_init().
 */
extern struct xypoint screen_center;

void get_screen_dimensions(struct rectangle* d);

typedef void (mousepos_handler_t)(struct xypoint pt);

int platform_init(struct kvmap* params, mousepos_handler_t* edge_handler);
void platform_exit(void);

#endif /* PLATFORM_H */
