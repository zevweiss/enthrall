
#ifndef X11_KEYCODES_H
#define X11_KEYCODES_H

#include <X11/X.h>

#include "keycodes.h"
#include "types.h"

keycode_t keysym_to_keycode(KeySym sym);

#endif /* X11_KEYCODES_H */
