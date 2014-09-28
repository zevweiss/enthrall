
#include <X11/X.h>
#include <X11/keysym.h>

#include "types.h"
#include "misc.h"
#include "x11-keycodes.h"

static keycode_t basic_fromkeysym[] = {
	/* Lower-case letters */
	[XK_a] = ET_a,
	[XK_b] = ET_b,
	[XK_c] = ET_c,
	[XK_d] = ET_d,
	[XK_e] = ET_e,
	[XK_f] = ET_f,
	[XK_g] = ET_g,
	[XK_h] = ET_h,
	[XK_i] = ET_i,
	[XK_j] = ET_j,
	[XK_k] = ET_k,
	[XK_l] = ET_l,
	[XK_m] = ET_m,
	[XK_n] = ET_n,
	[XK_o] = ET_o,
	[XK_p] = ET_p,
	[XK_q] = ET_q,
	[XK_r] = ET_r,
	[XK_s] = ET_s,
	[XK_t] = ET_t,
	[XK_u] = ET_u,
	[XK_v] = ET_v,
	[XK_w] = ET_w,
	[XK_x] = ET_x,
	[XK_y] = ET_y,
	[XK_z] = ET_z,

	/* Upper-case letters */
	[XK_A] = ET_A,
	[XK_B] = ET_B,
	[XK_C] = ET_C,
	[XK_D] = ET_D,
	[XK_E] = ET_E,
	[XK_F] = ET_F,
	[XK_G] = ET_G,
	[XK_H] = ET_H,
	[XK_I] = ET_I,
	[XK_J] = ET_J,
	[XK_K] = ET_K,
	[XK_L] = ET_L,
	[XK_M] = ET_M,
	[XK_N] = ET_N,
	[XK_O] = ET_O,
	[XK_P] = ET_P,
	[XK_Q] = ET_Q,
	[XK_R] = ET_R,
	[XK_S] = ET_S,
	[XK_T] = ET_T,
	[XK_U] = ET_U,
	[XK_V] = ET_V,
	[XK_W] = ET_W,
	[XK_X] = ET_X,
	[XK_Y] = ET_Y,
	[XK_Z] = ET_Z,

	/* Numerals */
	[XK_0] = ET_0,
	[XK_1] = ET_1,
	[XK_2] = ET_2,
	[XK_3] = ET_3,
	[XK_4] = ET_4,
	[XK_5] = ET_5,
	[XK_6] = ET_6,
	[XK_7] = ET_7,
	[XK_8] = ET_8,
	[XK_9] = ET_9,

	/* Various punctuation bits and pieces */
	[XK_grave]        = ET_backtick,
	[XK_asciitilde]   = ET_tilde,
	[XK_exclam]       = ET_exclpt,
	[XK_at]           = ET_atsign,
	[XK_numbersign]   = ET_numsign,
	[XK_dollar]       = ET_dollar,
	[XK_percent]      = ET_percent,
	[XK_asciicircum]  = ET_caret,
	[XK_ampersand]    = ET_ampersand,
	[XK_asterisk]     = ET_asterisk,
	[XK_parenleft]    = ET_leftparen,
	[XK_parenright]   = ET_rightparen,
	[XK_minus]        = ET_dash,
	[XK_underscore]   = ET_underscore,
	[XK_plus]         = ET_plus,
	[XK_equal]        = ET_equal,
	[XK_bracketleft]  = ET_leftbracket,
	[XK_braceleft]    = ET_leftbrace,
	[XK_bracketright] = ET_rightbracket,
	[XK_braceright]   = ET_rightbrace,
	[XK_backslash]    = ET_backslash,
	[XK_bar]          = ET_pipe,
	[XK_semicolon]    = ET_semicolon,
	[XK_colon]        = ET_colon,
	[XK_apostrophe]   = ET_singlequote,
	[XK_quotedbl]     = ET_doublequote,
	[XK_comma]        = ET_comma,
	[XK_less]         = ET_lessthan,
	[XK_period]       = ET_period,
	[XK_greater]      = ET_greaterthan,
	[XK_slash]        = ET_slash,
	[XK_question]     = ET_qstmark,

	/* Modifier keys */
	[XK_Shift_L] = ET_leftshift,
	[XK_Shift_R] = ET_rightshift,
	[XK_Control_L] = ET_leftcontrol,
	[XK_Control_R] = ET_rightcontrol,
	/*
	 * modN correspond to command/alt/option/meta/super/hyper etc. that
	 * aren't as universal as shift and control
	 */
	[XK_Meta_L] = ET_leftmod1,
	[XK_Meta_R] = ET_rightmod1,
	[XK_Alt_L] = ET_leftmod2,
	[XK_Alt_R] = ET_rightmod2,
	[XK_Super_L] = ET_leftmod3,
	[XK_Super_R] = ET_rightmod3,
	[XK_Hyper_L] = ET_leftmod4,
	[XK_Hyper_R] = ET_rightmod4,
	/* ET_leftmod5, */
	/* ET_rightmod5, */

	/* Miscellaneous stuff */
	[XK_space]     = ET_space,
	[XK_Return]    = ET_return,
	[XK_Tab]       = ET_tab,
	[XK_Escape]    = ET_escape,
	[XK_Left]      = ET_left,
	[XK_Right]     = ET_right,
	[XK_Up]        = ET_up,
	[XK_Down]      = ET_down,
	[XK_BackSpace] = ET_backspace,
	[XK_Delete]    = ET_delete,
	[XK_Home]      = ET_home,
	[XK_End]       = ET_end,
	[XK_Page_Up]   = ET_pageup,
	[XK_Page_Down] = ET_pagedown,

	/* Function keys */
	[XK_F1] = ET_F1,
	[XK_F2] = ET_F2,
	[XK_F3] = ET_F3,
	[XK_F4] = ET_F4,
	[XK_F5] = ET_F5,
	[XK_F6] = ET_F6,
	[XK_F7] = ET_F7,
	[XK_F8] = ET_F8,
	[XK_F9] = ET_F9,
	[XK_F10] = ET_F10,
	[XK_F11] = ET_F11,
	[XK_F12] = ET_F12,
	[XK_F13] = ET_F13,
	[XK_F14] = ET_F14,
	[XK_F15] = ET_F15,
	[XK_F16] = ET_F16,
	[XK_F17] = ET_F17,
	[XK_F18] = ET_F18,
	[XK_F19] = ET_F19,
	[XK_F20] = ET_F20,
	[XK_F21] = ET_F21,
	[XK_F22] = ET_F22,
	[XK_F23] = ET_F23,
	[XK_F24] = ET_F24,
	[XK_F25] = ET_F25,
	[XK_F26] = ET_F26,
	[XK_F27] = ET_F27,
	[XK_F28] = ET_F28,
	[XK_F29] = ET_F29,
	[XK_F30] = ET_F30,

	/* Keypad keys */
	[XK_KP_0]         = ET_KP_0,
	[XK_KP_1]         = ET_KP_1,
	[XK_KP_2]         = ET_KP_2,
	[XK_KP_3]         = ET_KP_3,
	[XK_KP_4]         = ET_KP_4,
	[XK_KP_5]         = ET_KP_5,
	[XK_KP_6]         = ET_KP_6,
	[XK_KP_7]         = ET_KP_7,
	[XK_KP_8]         = ET_KP_8,
	[XK_KP_9]         = ET_KP_9,
	[XK_KP_Equal]     = ET_KP_equal,
	[XK_KP_Divide]    = ET_KP_divide,
	[XK_KP_Multiply]  = ET_KP_multiply,
	[XK_KP_Subtract]  = ET_KP_subtract,
	[XK_KP_Add]       = ET_KP_add,
	[XK_KP_Enter]     = ET_KP_enter,
	[XK_KP_Separator] = ET_KP_dot,

#if 0
	/* "Magic" special-function keys */
	ET_eject,
	ET_volumeup,
	ET_volumedown,
	ET_mute,
	ET_playpause,
	ET_fastforward,
	ET_rewind,
	ET_brightnessup,
	ET_brightnessdown,
#endif
};

keycode_t keysym_to_keycode(KeySym sym)
{
	if (sym >= ARR_LEN(basic_fromkeysym))
		return ET_null;
	else
		return basic_fromkeysym[sym];
}
