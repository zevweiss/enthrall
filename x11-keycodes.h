
#ifndef X11_KEYCODES_H
#define X11_KEYCODES_H

#include <X11/X.h>
#include <X11/Xlib.h>

#include "keycodes.h"
#include "types.h"

void x11_keycodes_init(void);
void x11_keycodes_exit(void);

keycode_t keysym_to_keycode(KeySym sym);
KeyCode keycode_to_xkeycode(Display* disp, keycode_t kc);

#endif /* X11_KEYCODES_H */
