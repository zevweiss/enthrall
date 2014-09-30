
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

static CGKeyCode basic_tocgkeycode[] = {
	/* Lower-case */
	[ET_a] = kcshift(kVK_ANSI_A),
	[ET_b] = kcshift(kVK_ANSI_B),
	[ET_c] = kcshift(kVK_ANSI_C),
	[ET_d] = kcshift(kVK_ANSI_D),
	[ET_e] = kcshift(kVK_ANSI_E),
	[ET_f] = kcshift(kVK_ANSI_F),
	[ET_g] = kcshift(kVK_ANSI_G),
	[ET_h] = kcshift(kVK_ANSI_H),
	[ET_i] = kcshift(kVK_ANSI_I),
	[ET_j] = kcshift(kVK_ANSI_J),
	[ET_k] = kcshift(kVK_ANSI_K),
	[ET_l] = kcshift(kVK_ANSI_L),
	[ET_m] = kcshift(kVK_ANSI_M),
	[ET_n] = kcshift(kVK_ANSI_N),
	[ET_o] = kcshift(kVK_ANSI_O),
	[ET_p] = kcshift(kVK_ANSI_P),
	[ET_q] = kcshift(kVK_ANSI_Q),
	[ET_r] = kcshift(kVK_ANSI_R),
	[ET_s] = kcshift(kVK_ANSI_S),
	[ET_t] = kcshift(kVK_ANSI_T),
	[ET_u] = kcshift(kVK_ANSI_U),
	[ET_v] = kcshift(kVK_ANSI_V),
	[ET_w] = kcshift(kVK_ANSI_W),
	[ET_x] = kcshift(kVK_ANSI_X),
	[ET_y] = kcshift(kVK_ANSI_Y),
	[ET_z] = kcshift(kVK_ANSI_Z),

	/* Upper-case */
	[ET_A] = kcshift(kVK_ANSI_A),
	[ET_B] = kcshift(kVK_ANSI_B),
	[ET_C] = kcshift(kVK_ANSI_C),
	[ET_D] = kcshift(kVK_ANSI_D),
	[ET_E] = kcshift(kVK_ANSI_E),
	[ET_F] = kcshift(kVK_ANSI_F),
	[ET_G] = kcshift(kVK_ANSI_G),
	[ET_H] = kcshift(kVK_ANSI_H),
	[ET_I] = kcshift(kVK_ANSI_I),
	[ET_J] = kcshift(kVK_ANSI_J),
	[ET_K] = kcshift(kVK_ANSI_K),
	[ET_L] = kcshift(kVK_ANSI_L),
	[ET_M] = kcshift(kVK_ANSI_M),
	[ET_N] = kcshift(kVK_ANSI_N),
	[ET_O] = kcshift(kVK_ANSI_O),
	[ET_P] = kcshift(kVK_ANSI_P),
	[ET_Q] = kcshift(kVK_ANSI_Q),
	[ET_R] = kcshift(kVK_ANSI_R),
	[ET_S] = kcshift(kVK_ANSI_S),
	[ET_T] = kcshift(kVK_ANSI_T),
	[ET_U] = kcshift(kVK_ANSI_U),
	[ET_V] = kcshift(kVK_ANSI_V),
	[ET_W] = kcshift(kVK_ANSI_W),
	[ET_X] = kcshift(kVK_ANSI_X),
	[ET_Y] = kcshift(kVK_ANSI_Y),
	[ET_Z] = kcshift(kVK_ANSI_Z),

	/* Numerals */
	[ET_0] = kcshift(kVK_ANSI_0),
	[ET_1] = kcshift(kVK_ANSI_1),
	[ET_2] = kcshift(kVK_ANSI_2),
	[ET_3] = kcshift(kVK_ANSI_3),
	[ET_4] = kcshift(kVK_ANSI_4),
	[ET_5] = kcshift(kVK_ANSI_5),
	[ET_6] = kcshift(kVK_ANSI_6),
	[ET_7] = kcshift(kVK_ANSI_7),
	[ET_8] = kcshift(kVK_ANSI_8),
	[ET_9] = kcshift(kVK_ANSI_9),

	/* Punctuation */
	[ET_backtick]     = kcshift(kVK_ANSI_Grave),
	[ET_tilde]        = kcshift(kVK_ANSI_Grave),
	[ET_exclpt]       = kcshift(kVK_ANSI_1),
	[ET_atsign]       = kcshift(kVK_ANSI_2),
	[ET_numsign]      = kcshift(kVK_ANSI_3),
	[ET_dollar]       = kcshift(kVK_ANSI_4),
	[ET_percent]      = kcshift(kVK_ANSI_5),
	[ET_caret]        = kcshift(kVK_ANSI_6),
	[ET_ampersand]    = kcshift(kVK_ANSI_7),
	[ET_asterisk]     = kcshift(kVK_ANSI_8),
	[ET_leftparen]    = kcshift(kVK_ANSI_9),
	[ET_rightparen]   = kcshift(kVK_ANSI_0),
	[ET_dash]         = kcshift(kVK_ANSI_Minus),
	[ET_underscore]   = kcshift(kVK_ANSI_Minus),
	[ET_plus]         = kcshift(kVK_ANSI_Equal),
	[ET_equal]        = kcshift(kVK_ANSI_Equal),
	[ET_leftbracket]  = kcshift(kVK_ANSI_LeftBracket),
	[ET_leftbrace]    = kcshift(kVK_ANSI_LeftBracket),
	[ET_rightbracket] = kcshift(kVK_ANSI_RightBracket),
	[ET_rightbrace]   = kcshift(kVK_ANSI_RightBracket),
	[ET_backslash]    = kcshift(kVK_ANSI_Backslash),
	[ET_pipe]         = kcshift(kVK_ANSI_Backslash),
	[ET_semicolon]    = kcshift(kVK_ANSI_Semicolon),
	[ET_colon]        = kcshift(kVK_ANSI_Semicolon),
	[ET_singlequote]  = kcshift(kVK_ANSI_Quote),
	[ET_doublequote]  = kcshift(kVK_ANSI_Quote),
	[ET_comma]        = kcshift(kVK_ANSI_Comma),
	[ET_lessthan]     = kcshift(kVK_ANSI_Comma),
	[ET_period]       = kcshift(kVK_ANSI_Period),
	[ET_greaterthan]  = kcshift(kVK_ANSI_Period),
	[ET_slash]        = kcshift(kVK_ANSI_Slash),
	[ET_qstmark]      = kcshift(kVK_ANSI_Slash),

	/* Modifiers */
	[ET_leftcontrol]  = kcshift(kVK_Control),
	[ET_rightcontrol] = kcshift(kVK_RightControl),
	[ET_leftshift]    = kcshift(kVK_Shift),
	[ET_rightshift]   = kcshift(kVK_RightShift),
	[ET_leftmod2]     = kcshift(kVK_Command),
	[ET_rightmod2]    = kcshift(kVK_Command),
	[ET_leftmod3]     = kcshift(kVK_Option),
	[ET_rightmod3]    = kcshift(kVK_RightOption),

	/* Miscellaneous stuff */
	[ET_space]     = kcshift(kVK_Space),
	[ET_return]    = kcshift(kVK_Return),
	[ET_tab]       = kcshift(kVK_Tab),
	[ET_escape]    = kcshift(kVK_Escape),
	[ET_left]      = kcshift(kVK_LeftArrow),
	[ET_right]     = kcshift(kVK_RightArrow),
	[ET_up]        = kcshift(kVK_UpArrow),
	[ET_down]      = kcshift(kVK_DownArrow),
	[ET_backspace] = kcshift(kVK_Delete),
	[ET_delete]    = kcshift(kVK_ForwardDelete),
	[ET_home]      = kcshift(kVK_Home),
	[ET_end]       = kcshift(kVK_End),
	[ET_pageup]    = kcshift(kVK_PageUp),
	[ET_pagedown]  = kcshift(kVK_PageDown),

	/* Function keys */
	[ET_F1] = kcshift(kVK_F1),
	[ET_F2] = kcshift(kVK_F2),
	[ET_F3] = kcshift(kVK_F3),
	[ET_F4] = kcshift(kVK_F4),
	[ET_F5] = kcshift(kVK_F5),
	[ET_F6] = kcshift(kVK_F6),
	[ET_F7] = kcshift(kVK_F7),
	[ET_F8] = kcshift(kVK_F8),
	[ET_F9] = kcshift(kVK_F9),
	[ET_F10] = kcshift(kVK_F10),
	[ET_F11] = kcshift(kVK_F11),
	[ET_F12] = kcshift(kVK_F12),
	[ET_F13] = kcshift(kVK_F13),
	[ET_F14] = kcshift(kVK_F14),
	[ET_F15] = kcshift(kVK_F15),
	[ET_F16] = kcshift(kVK_F16),
	[ET_F17] = kcshift(kVK_F17),
	[ET_F18] = kcshift(kVK_F18),
	[ET_F19] = kcshift(kVK_F19),
	[ET_F20] = kcshift(kVK_F20),

	[ET_KP_0] = kcshift(kVK_ANSI_Keypad0),
	[ET_KP_1] = kcshift(kVK_ANSI_Keypad1),
	[ET_KP_2] = kcshift(kVK_ANSI_Keypad2),
	[ET_KP_3] = kcshift(kVK_ANSI_Keypad3),
	[ET_KP_4] = kcshift(kVK_ANSI_Keypad4),
	[ET_KP_5] = kcshift(kVK_ANSI_Keypad5),
	[ET_KP_6] = kcshift(kVK_ANSI_Keypad6),
	[ET_KP_7] = kcshift(kVK_ANSI_Keypad7),
	[ET_KP_8] = kcshift(kVK_ANSI_Keypad8),
	[ET_KP_9] = kcshift(kVK_ANSI_Keypad9),
	[ET_KP_dot] = kcshift(kVK_ANSI_KeypadDecimal),
	[ET_KP_multiply] = kcshift(kVK_ANSI_KeypadMultiply),
	[ET_KP_divide] = kcshift(kVK_ANSI_KeypadDivide),
	[ET_KP_add] = kcshift(kVK_ANSI_KeypadPlus),
	[ET_KP_subtract] = kcshift(kVK_ANSI_KeypadMinus),
	[ET_KP_enter] = kcshift(kVK_ANSI_KeypadEnter),
	[ET_KP_equal] = kcshift(kVK_ANSI_KeypadEquals),
};

CGKeyCode keycode_to_cgkeycode(keycode_t kc)
{
	if (kc >= ARR_LEN(basic_tocgkeycode))
		return kVK_NULL;
	else if (!basic_tocgkeycode[kc])
		return kVK_NULL;
	else
		return kcunshift(basic_tocgkeycode[kc]);
}
