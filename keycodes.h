
#ifndef KEYCODES_H
#define KEYCODES_H

enum {
	ET_null = 0,

	/* Lower-case letters */
	ET_a,
	ET_b,
	ET_c,
	ET_d,
	ET_e,
	ET_f,
	ET_g,
	ET_h,
	ET_i,
	ET_j,
	ET_k,
	ET_l,
	ET_m,
	ET_n,
	ET_o,
	ET_p,
	ET_q,
	ET_r,
	ET_s,
	ET_t,
	ET_u,
	ET_v,
	ET_w,
	ET_x,
	ET_y,
	ET_z,

	/* Upper-case letters */
	ET_A,
	ET_B,
	ET_C,
	ET_D,
	ET_E,
	ET_F,
	ET_G,
	ET_H,
	ET_I,
	ET_J,
	ET_K,
	ET_L,
	ET_M,
	ET_N,
	ET_O,
	ET_P,
	ET_Q,
	ET_R,
	ET_S,
	ET_T,
	ET_U,
	ET_V,
	ET_W,
	ET_X,
	ET_Y,
	ET_Z,

	/* Numerals */
	ET_0,
	ET_1,
	ET_2,
	ET_3,
	ET_4,
	ET_5,
	ET_6,
	ET_7,
	ET_8,
	ET_9,

	/* Various punctuation bits and pieces */
	ET_backtick,     /* ` */
	ET_tilde,        /* ~ */
	ET_exclpt,       /* ! */
	ET_atsign,       /* @ */
	ET_numsign,      /* # */
	ET_dollar,       /* $ */
	ET_percent,      /* % */
	ET_caret,        /* ^ */
	ET_ampersand,    /* & */
	ET_asterisk,     /* * */
	ET_leftparen,    /* ( */
	ET_rightparen,   /* ) */
	ET_dash,         /* - */
	ET_underscore,   /* _ */
	ET_plus,         /* + */
	ET_equal,        /* = */
	ET_leftbracket,  /* [ */
	ET_leftbrace,    /* { */
	ET_rightbracket, /* ] */
	ET_rightbrace,   /* } */
	ET_backslash,    /* \ */
	ET_pipe,         /* | */
	ET_semicolon,    /* ; */
	ET_colon,        /* : */
	ET_singlequote,  /* ' */
	ET_doublequote,  /* " */
	ET_comma,        /* , */
	ET_lessthan,     /* < */
	ET_period,       /* . */
	ET_greaterthan,  /* > */
	ET_slash,        /* / */
	ET_qstmark,      /* ? */

	/* Modifier keys */
	ET_leftshift,
	ET_rightshift,
	ET_leftcontrol,
	ET_rightcontrol,
	/*
	 * modN correspond to command/alt/option/meta/super/hyper etc. that
	 * aren't as universal as shift and control
	 */
	ET_leftmod1,
	ET_rightmod1,
	ET_leftmod2,
	ET_rightmod2,
	ET_leftmod3,
	ET_rightmod3,
	ET_leftmod4,
	ET_rightmod4,
	ET_leftmod5,
	ET_rightmod5,

	ET__MODIFIER_MIN_ = ET_leftshift,
	ET__MODIFIER_MAX_ = ET_rightmod5,

	/* Miscellaneous stuff */
	ET_space,
	ET_return,
	ET_tab,
	ET_escape,
	ET_left,
	ET_right,
	ET_up,
	ET_down,
	ET_backspace,
	ET_delete,
	ET_home,
	ET_end,
	ET_pageup,
	ET_pagedown,

	/* Keypad keys */
	ET_KP_0,
	ET_KP_1,
	ET_KP_2,
	ET_KP_3,
	ET_KP_4,
	ET_KP_5,
	ET_KP_6,
	ET_KP_7,
	ET_KP_8,
	ET_KP_9,
	ET_KP_equal,
	ET_KP_divide,
	ET_KP_multiply,
	ET_KP_subtract,
	ET_KP_add,
	ET_KP_enter,
	ET_KP_dot,

	ET__KP_MIN_ = ET_KP_0,
	ET__KP_MAX_ = ET_KP_dot,

	/* Function keys */
	ET_F1,
	ET_F2,
	ET_F3,
	ET_F4,
	ET_F5,
	ET_F6,
	ET_F7,
	ET_F8,
	ET_F9,
	ET_F10,
	ET_F11,
	ET_F12,
	ET_F13,
	ET_F14,
	ET_F15,
	ET_F16,
	ET_F17,
	ET_F18,
	ET_F19,
	ET_F20,
	ET_F21,
	ET_F22,
	ET_F23,
	ET_F24,
	ET_F25,
	ET_F26,
	ET_F27,
	ET_F28,
	ET_F29,
	ET_F30,

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
};

static inline int is_modifier_key(keycode_t k)
{
	return k >= ET__MODIFIER_MIN_ && k <= ET__MODIFIER_MAX_;
}

static inline int is_keypad_key(keycode_t k)
{
	return k >= ET__KP_MIN_ && k <= ET__KP_MAX_;
}

#endif /* KEYCODES_H */
