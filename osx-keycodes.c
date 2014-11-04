
/* TODO: figure out how to get just <HIToolbox/Events.h> */
#include <Carbon/Carbon.h>

#include "types.h"
#include "misc.h"
#include "osx-keycodes.h"

/*
 * The values in the table are shift by one so that the default zero value can
 * be used to indicate an unmapped keycode -- kVK_ANSI_A unfortunately uses
 * the value zero, so doing so directly would be problematic.
 */
#define kcshift(k) ((k)+1)
#define kcunshift(k) ((k)-1)

/*
 * Which of these have 'nobackmap' set is a little hocus-pocus at the
 * moment...would be nice to make it a bit more sensible/uniform.
 */
static struct {
	CGKeyCode kc;

	/*
	 * If set, don't insert the reverse correspondence into
	 * basic_fromcgkeycode when inverting.
	 */
	int nobackmap;
} basic_tocgkeycode[] = {

#define OSXKEY(k, nb) { .kc = kcshift(kVK_##k), .nobackmap = nb, }

	/* Lower-case */
	[ET_a] = OSXKEY(ANSI_A, 0),
	[ET_b] = OSXKEY(ANSI_B, 0),
	[ET_c] = OSXKEY(ANSI_C, 0),
	[ET_d] = OSXKEY(ANSI_D, 0),
	[ET_e] = OSXKEY(ANSI_E, 0),
	[ET_f] = OSXKEY(ANSI_F, 0),
	[ET_g] = OSXKEY(ANSI_G, 0),
	[ET_h] = OSXKEY(ANSI_H, 0),
	[ET_i] = OSXKEY(ANSI_I, 0),
	[ET_j] = OSXKEY(ANSI_J, 0),
	[ET_k] = OSXKEY(ANSI_K, 0),
	[ET_l] = OSXKEY(ANSI_L, 0),
	[ET_m] = OSXKEY(ANSI_M, 0),
	[ET_n] = OSXKEY(ANSI_N, 0),
	[ET_o] = OSXKEY(ANSI_O, 0),
	[ET_p] = OSXKEY(ANSI_P, 0),
	[ET_q] = OSXKEY(ANSI_Q, 0),
	[ET_r] = OSXKEY(ANSI_R, 0),
	[ET_s] = OSXKEY(ANSI_S, 0),
	[ET_t] = OSXKEY(ANSI_T, 0),
	[ET_u] = OSXKEY(ANSI_U, 0),
	[ET_v] = OSXKEY(ANSI_V, 0),
	[ET_w] = OSXKEY(ANSI_W, 0),
	[ET_x] = OSXKEY(ANSI_X, 0),
	[ET_y] = OSXKEY(ANSI_Y, 0),
	[ET_z] = OSXKEY(ANSI_Z, 0),

	/* Upper-case */
	[ET_A] = OSXKEY(ANSI_A, 0),
	[ET_B] = OSXKEY(ANSI_B, 0),
	[ET_C] = OSXKEY(ANSI_C, 0),
	[ET_D] = OSXKEY(ANSI_D, 0),
	[ET_E] = OSXKEY(ANSI_E, 0),
	[ET_F] = OSXKEY(ANSI_F, 0),
	[ET_G] = OSXKEY(ANSI_G, 0),
	[ET_H] = OSXKEY(ANSI_H, 0),
	[ET_I] = OSXKEY(ANSI_I, 0),
	[ET_J] = OSXKEY(ANSI_J, 0),
	[ET_K] = OSXKEY(ANSI_K, 0),
	[ET_L] = OSXKEY(ANSI_L, 0),
	[ET_M] = OSXKEY(ANSI_M, 0),
	[ET_N] = OSXKEY(ANSI_N, 0),
	[ET_O] = OSXKEY(ANSI_O, 0),
	[ET_P] = OSXKEY(ANSI_P, 0),
	[ET_Q] = OSXKEY(ANSI_Q, 0),
	[ET_R] = OSXKEY(ANSI_R, 0),
	[ET_S] = OSXKEY(ANSI_S, 0),
	[ET_T] = OSXKEY(ANSI_T, 0),
	[ET_U] = OSXKEY(ANSI_U, 0),
	[ET_V] = OSXKEY(ANSI_V, 0),
	[ET_W] = OSXKEY(ANSI_W, 0),
	[ET_X] = OSXKEY(ANSI_X, 0),
	[ET_Y] = OSXKEY(ANSI_Y, 0),
	[ET_Z] = OSXKEY(ANSI_Z, 0),

	/* Numerals */
	[ET_0] = OSXKEY(ANSI_0, 0),
	[ET_1] = OSXKEY(ANSI_1, 0),
	[ET_2] = OSXKEY(ANSI_2, 0),
	[ET_3] = OSXKEY(ANSI_3, 0),
	[ET_4] = OSXKEY(ANSI_4, 0),
	[ET_5] = OSXKEY(ANSI_5, 0),
	[ET_6] = OSXKEY(ANSI_6, 0),
	[ET_7] = OSXKEY(ANSI_7, 0),
	[ET_8] = OSXKEY(ANSI_8, 0),
	[ET_9] = OSXKEY(ANSI_9, 0),

	/* Punctuation */
	[ET_backtick]     = OSXKEY(ANSI_Grave, 0),
	[ET_tilde]        = OSXKEY(ANSI_Grave, 0),
	[ET_exclpt]       = OSXKEY(ANSI_1, 0),
	[ET_atsign]       = OSXKEY(ANSI_2, 0),
	[ET_numsign]      = OSXKEY(ANSI_3, 0),
	[ET_dollar]       = OSXKEY(ANSI_4, 0),
	[ET_percent]      = OSXKEY(ANSI_5, 0),
	[ET_caret]        = OSXKEY(ANSI_6, 0),
	[ET_ampersand]    = OSXKEY(ANSI_7, 0),
	[ET_asterisk]     = OSXKEY(ANSI_8, 0),
	[ET_leftparen]    = OSXKEY(ANSI_9, 1),
	[ET_rightparen]   = OSXKEY(ANSI_0, 1),
	[ET_dash]         = OSXKEY(ANSI_Minus, 0),
	[ET_underscore]   = OSXKEY(ANSI_Minus, 0),
	[ET_plus]         = OSXKEY(ANSI_Equal, 0),
	[ET_equal]        = OSXKEY(ANSI_Equal, 0),
	[ET_leftbracket]  = OSXKEY(ANSI_LeftBracket, 0),
	[ET_leftbrace]    = OSXKEY(ANSI_LeftBracket, 0),
	[ET_rightbracket] = OSXKEY(ANSI_RightBracket, 0),
	[ET_rightbrace]   = OSXKEY(ANSI_RightBracket, 0),
	[ET_backslash]    = OSXKEY(ANSI_Backslash, 0),
	[ET_pipe]         = OSXKEY(ANSI_Backslash, 0),
	[ET_semicolon]    = OSXKEY(ANSI_Semicolon, 0),
	[ET_colon]        = OSXKEY(ANSI_Semicolon, 0),
	[ET_singlequote]  = OSXKEY(ANSI_Quote, 0),
	[ET_doublequote]  = OSXKEY(ANSI_Quote, 0),
	[ET_comma]        = OSXKEY(ANSI_Comma, 0),
	[ET_lessthan]     = OSXKEY(ANSI_Comma, 1),
	[ET_period]       = OSXKEY(ANSI_Period, 0),
	[ET_greaterthan]  = OSXKEY(ANSI_Period, 0),
	[ET_slash]        = OSXKEY(ANSI_Slash, 0),
	[ET_qstmark]      = OSXKEY(ANSI_Slash, 0),

	/* Modifiers */
	[ET_leftcontrol]  = OSXKEY(Control, 0),
	[ET_rightcontrol] = OSXKEY(RightControl, 0),
	[ET_leftshift]    = OSXKEY(Shift, 0),
	[ET_rightshift]   = OSXKEY(RightShift, 0),
	[ET_leftmod2]     = OSXKEY(Command, 0),
	[ET_rightmod2]    = OSXKEY(Command, 0),
	[ET_leftmod3]     = OSXKEY(Option, 0),
	[ET_rightmod3]    = OSXKEY(RightOption, 0),

	/* Miscellaneous stuff */
	[ET_space]     = OSXKEY(Space, 0),
	[ET_return]    = OSXKEY(Return, 0),
	[ET_tab]       = OSXKEY(Tab, 0),
	[ET_escape]    = OSXKEY(Escape, 0),
	[ET_left]      = OSXKEY(LeftArrow, 0),
	[ET_right]     = OSXKEY(RightArrow, 0),
	[ET_up]        = OSXKEY(UpArrow, 0),
	[ET_down]      = OSXKEY(DownArrow, 0),
	[ET_backspace] = OSXKEY(Delete, 0),
	[ET_delete]    = OSXKEY(ForwardDelete, 0),
	[ET_home]      = OSXKEY(Home, 0),
	[ET_end]       = OSXKEY(End, 0),
	[ET_pageup]    = OSXKEY(PageUp, 0),
	[ET_pagedown]  = OSXKEY(PageDown, 0),

	/* Function keys */
	[ET_F1] = OSXKEY(F1, 0),
	[ET_F2] = OSXKEY(F2, 0),
	[ET_F3] = OSXKEY(F3, 0),
	[ET_F4] = OSXKEY(F4, 0),
	[ET_F5] = OSXKEY(F5, 0),
	[ET_F6] = OSXKEY(F6, 0),
	[ET_F7] = OSXKEY(F7, 0),
	[ET_F8] = OSXKEY(F8, 0),
	[ET_F9] = OSXKEY(F9, 0),
	[ET_F10] = OSXKEY(F10, 0),
	[ET_F11] = OSXKEY(F11, 0),
	[ET_F12] = OSXKEY(F12, 0),
	[ET_F13] = OSXKEY(F13, 0),
	[ET_F14] = OSXKEY(F14, 0),
	[ET_F15] = OSXKEY(F15, 0),
	[ET_F16] = OSXKEY(F16, 0),
	[ET_F17] = OSXKEY(F17, 0),
	[ET_F18] = OSXKEY(F18, 0),
	[ET_F19] = OSXKEY(F19, 0),
	[ET_F20] = OSXKEY(F20, 0),

	/* Keypad keys */
	[ET_KP_0] = OSXKEY(ANSI_Keypad0, 0),
	[ET_KP_1] = OSXKEY(ANSI_Keypad1, 0),
	[ET_KP_2] = OSXKEY(ANSI_Keypad2, 0),
	[ET_KP_3] = OSXKEY(ANSI_Keypad3, 0),
	[ET_KP_4] = OSXKEY(ANSI_Keypad4, 0),
	[ET_KP_5] = OSXKEY(ANSI_Keypad5, 0),
	[ET_KP_6] = OSXKEY(ANSI_Keypad6, 0),
	[ET_KP_7] = OSXKEY(ANSI_Keypad7, 0),
	[ET_KP_8] = OSXKEY(ANSI_Keypad8, 0),
	[ET_KP_9] = OSXKEY(ANSI_Keypad9, 0),
	[ET_KP_dot]      = OSXKEY(ANSI_KeypadDecimal, 0),
	[ET_KP_multiply] = OSXKEY(ANSI_KeypadMultiply, 0),
	[ET_KP_divide]   = OSXKEY(ANSI_KeypadDivide, 0),
	[ET_KP_add]      = OSXKEY(ANSI_KeypadPlus, 0),
	[ET_KP_subtract] = OSXKEY(ANSI_KeypadMinus, 0),
	[ET_KP_enter]    = OSXKEY(ANSI_KeypadEnter, 0),
	[ET_KP_equal]    = OSXKEY(ANSI_KeypadEquals, 0),

#undef OSXKEY
};

static struct {
	keycode_t* table;
	unsigned int numents;
} basic_fromcgkeycode;

void osx_keycodes_init(void)
{
	int i;
	size_t sz;
	CGKeyCode cgkc;
	unsigned int maxcgkc = 0;

	/* Invert basic_tocgkeycode into basic_fromcgkeycode */

	for (i = 0; i < ARR_LEN(basic_tocgkeycode); i++) {
		cgkc = basic_tocgkeycode[i].kc;
		if (cgkc) {
			cgkc = kcunshift(cgkc);
			if (cgkc > maxcgkc)
				maxcgkc = cgkc;
		}
	}

	basic_fromcgkeycode.numents = maxcgkc + 1;
	sz = basic_fromcgkeycode.numents * sizeof(*basic_fromcgkeycode.table);
	basic_fromcgkeycode.table = xcalloc(sz);

	for (i = 0; i < ARR_LEN(basic_tocgkeycode); i++) {
		if (basic_tocgkeycode[i].kc && !basic_tocgkeycode[i].nobackmap)
			basic_fromcgkeycode.table[kcunshift(basic_tocgkeycode[i].kc)] = i;
	}
}

void osx_keycodes_exit(void)
{
	xfree(basic_fromcgkeycode.table);
}

CGKeyCode etkeycode_to_cgkeycode(keycode_t kc)
{
	if (kc >= ARR_LEN(basic_tocgkeycode))
		return kVK_NULL;
	else if (!basic_tocgkeycode[kc].kc)
		return kVK_NULL;
	else
		return kcunshift(basic_tocgkeycode[kc].kc);
}

keycode_t cgkeycode_to_etkeycode(CGKeyCode kc)
{
	if (kc >= basic_fromcgkeycode.numents)
		return ET_null;
	else
		return basic_fromcgkeycode.table[kc];
}

/*
 * OSX apparently offers nothing analogous to XStringToKeysym() as far as I
 * can tell.  So here's a kludged up manual one.  Sigh.
 */
static int osx_string_to_keycode(const char* str, uint32_t* kc)
{
	int i, idx;
	uint32_t shifted_code;
	static const int singlechars[] = {
		['0'] = kcshift(kVK_ANSI_0),
		['1'] = kcshift(kVK_ANSI_1),
		['2'] = kcshift(kVK_ANSI_2),
		['3'] = kcshift(kVK_ANSI_3),
		['4'] = kcshift(kVK_ANSI_4),
		['5'] = kcshift(kVK_ANSI_5),
		['6'] = kcshift(kVK_ANSI_6),
		['7'] = kcshift(kVK_ANSI_7),
		['8'] = kcshift(kVK_ANSI_8),
		['9'] = kcshift(kVK_ANSI_9),
		['a'] = kcshift(kVK_ANSI_A),
		['b'] = kcshift(kVK_ANSI_B),
		['c'] = kcshift(kVK_ANSI_C),
		['d'] = kcshift(kVK_ANSI_D),
		['e'] = kcshift(kVK_ANSI_E),
		['f'] = kcshift(kVK_ANSI_F),
		['g'] = kcshift(kVK_ANSI_G),
		['h'] = kcshift(kVK_ANSI_H),
		['i'] = kcshift(kVK_ANSI_I),
		['j'] = kcshift(kVK_ANSI_J),
		['k'] = kcshift(kVK_ANSI_K),
		['l'] = kcshift(kVK_ANSI_L),
		['m'] = kcshift(kVK_ANSI_M),
		['n'] = kcshift(kVK_ANSI_N),
		['o'] = kcshift(kVK_ANSI_O),
		['p'] = kcshift(kVK_ANSI_P),
		['q'] = kcshift(kVK_ANSI_Q),
		['r'] = kcshift(kVK_ANSI_R),
		['s'] = kcshift(kVK_ANSI_S),
		['t'] = kcshift(kVK_ANSI_T),
		['u'] = kcshift(kVK_ANSI_U),
		['v'] = kcshift(kVK_ANSI_V),
		['w'] = kcshift(kVK_ANSI_W),
		['x'] = kcshift(kVK_ANSI_X),
		['y'] = kcshift(kVK_ANSI_Y),
		['z'] = kcshift(kVK_ANSI_Z),
	};
	static const struct {
		const char* str;
		int code;
	} keys[] = {
#define K(name) { .str = #name, .code = kVK_##name, }
#define KA(name) { .str = #name, .code = kVK_ANSI_##name, }
#define KP(name) { .str = "KP"#name, .code = kVK_ANSI_Keypad##name, }
		KA(Equal),
		KA(Minus),
		KA(RightBracket),
		KA(LeftBracket),
		KA(Quote),
		KA(Semicolon),
		KA(Backslash),
		KA(Comma),
		KA(Slash),
		KA(Period),
		KA(Grave),

		KP(Decimal),
		KP(Multiply),
		KP(Plus),
		KP(Clear),
		KP(Divide),
		KP(Enter),
		KP(Minus),
		KP(Equals),
		KP(0),
		KP(1),
		KP(2),
		KP(3),
		KP(4),
		KP(5),
		KP(6),
		KP(7),
		KP(8),
		KP(9),

		K(Return),
		K(Tab),
		K(Space),
		K(Delete),
		K(Escape),
		K(Command),
		K(Shift),
		K(CapsLock),
		K(Option),
		K(Control),
		K(RightShift),
		K(RightOption),
		K(RightControl),
		K(Function),
		K(VolumeUp),
		K(VolumeDown),
		K(Mute),
		K(Help),
		K(Home),
		K(PageUp),
		K(ForwardDelete),
		K(End),
		K(PageDown),
		K(LeftArrow),
		K(RightArrow),
		K(DownArrow),
		K(UpArrow),

		K(F1),
		K(F2),
		K(F3),
		K(F4),
		K(F5),
		K(F6),
		K(F7),
		K(F8),
		K(F9),
		K(F10),
		K(F11),
		K(F12),
		K(F13),
		K(F14),
		K(F15),
		K(F16),
		K(F17),
		K(F18),
		K(F19),
		K(F20),
#undef K
#undef KA
#undef KP
	};

	if (strlen(str) == 1) {
		idx = str[0];
		if (idx < ARR_LEN(singlechars)) {
			shifted_code = singlechars[idx];
			if (shifted_code) {
				*kc = kcunshift(shifted_code);
				return 0;
			}
		}
		return -1;
	}

	for (i = 0; i < ARR_LEN(keys); i++) {
		if (!strcmp(str, keys[i].str)) {
			*kc = keys[i].code;
			return 0;
		}
	}
	return -1;
}

const struct modifiers osx_modifiers = {
	.num = 4,
	.keys = {
		{ .name = "command", .mask = kCGEventFlagMaskCommand,   .etkey = ET_leftmod2,    },
		{ .name = "shift",   .mask = kCGEventFlagMaskShift,     .etkey = ET_leftshift,   },
		{ .name = "option",  .mask = kCGEventFlagMaskAlternate, .etkey = ET_leftmod3,    },
		{ .name = "control", .mask = kCGEventFlagMaskControl,   .etkey = ET_leftcontrol, },
	},
};

int parse_keystring(const char* ks, CGKeyCode *kc, CGEventFlags* modmask)
{
	size_t klen;
	int i, status;
	uint32_t code;
	const char* k = ks;
	/* Scratch string buffer large enough to hold any substring of 'ks' */
	char* tmp = xmalloc(strlen(ks)+1);

	*kc = 0;
	*modmask = 0;

	while (*k) {
		klen = strcspn(k, "+");
		memcpy(tmp, k, klen);
		tmp[klen] = '\0';

		for (i = 0; i < osx_modifiers.num; i++) {
			if (!strcasecmp(osx_modifiers.keys[i].name, tmp)) {
				*modmask |= osx_modifiers.keys[i].mask;
				break;
			}
		}
		/* If we found a modifier key, move on to the next key */
		if (i < osx_modifiers.num)
			goto next;

		if (osx_string_to_keycode(tmp, &code)) {
			elog("Invalid key: '%s'\n", tmp);
			status = -1;
			goto out;
		}

		if (*kc) {
			elog("Invalid hotkey '%s': multiple non-modifier keys\n", ks);
			status = -1;
			goto out;
		}

		*kc = kcshift(code);

	next:
		k += klen;
		if (*k) {
			assert(*k == '+');
			k += 1;
		}
	}

	status = 0;

out:
	if (*kc)
		*kc = kcunshift(*kc);
	xfree(tmp);
	return status;
}

keycode_t* modmask_to_etkeycodes(uint32_t modmask)
{
	int i;
	unsigned int modcount = 0;
	keycode_t* modkeys = xmalloc((osx_modifiers.num + 1) * sizeof(modkeys));

	for (i = 0; i < osx_modifiers.num; i++) {
		if (modmask & osx_modifiers.keys[i].mask)
			modkeys[modcount++] = osx_modifiers.keys[i].etkey;
	}

	modkeys[modcount] = ET_null;

	return modkeys;
}
