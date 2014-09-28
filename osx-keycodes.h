
#ifndef OSX_KEYCODES_H
#define OSX_KEYCODES_H

#include <CoreGraphics/CGEvent.h>

#include "keycodes.h"

#define kVK_NULL ((CGKeyCode)(-1))

CGKeyCode keycode_to_cgkeycode(keycode_t kc);

#endif /* OSX_KEYCODES_H */
