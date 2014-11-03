
#ifndef OSX_KEYCODES_H
#define OSX_KEYCODES_H

#include <CoreGraphics/CGEvent.h>

#include "keycodes.h"

#define kVK_NULL ((CGKeyCode)(-1))

void osx_keycodes_init(void);
void osx_keycodes_exit(void);

CGKeyCode etkeycode_to_cgkeycode(keycode_t kc);
keycode_t cgkeycode_to_etkeycode(CGKeyCode kc);

int parse_keystring(const char* ks, CGKeyCode *kc, CGEventFlags* modmask);

keycode_t* modmask_to_etkeycodes(uint32_t modmask);

struct modifiers {
	unsigned int num;
	struct {
		const char* name;
		CGEventFlags mask;
		keycode_t etkey;
	} keys[];
};

extern const struct modifiers osx_modifiers;
#endif /* OSX_KEYCODES_H */
