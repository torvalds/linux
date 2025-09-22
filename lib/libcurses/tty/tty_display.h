/* $OpenBSD: tty_display.h,v 1.4 2010/01/12 23:22:07 nicm Exp $ */

/****************************************************************************
 * Copyright (c) 1998-2003,2004 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ************************************************************************** */

#ifndef TTY_DISPLAY_H
#define TTY_DISPLAY_H 1

/*
 * $Id: tty_display.h,v 1.4 2010/01/12 23:22:07 nicm Exp $
 */
extern NCURSES_EXPORT(bool) _nc_tty_beep (void);
extern NCURSES_EXPORT(bool) _nc_tty_check_resize (void);
extern NCURSES_EXPORT(bool) _nc_tty_cursor (int);
extern NCURSES_EXPORT(bool) _nc_tty_flash (void);
extern NCURSES_EXPORT(bool) _nc_tty_init_color (int,int,int,int);
extern NCURSES_EXPORT(bool) _nc_tty_init_pair (int,int,int);
extern NCURSES_EXPORT(bool) _nc_tty_slk_hide (bool);
extern NCURSES_EXPORT(bool) _nc_tty_slk_update (int,const char *);
extern NCURSES_EXPORT(bool) _nc_tty_start_color (void);
extern NCURSES_EXPORT(void) _nc_tty_display_resume (void);
extern NCURSES_EXPORT(void) _nc_tty_display_suspend (void);
extern NCURSES_EXPORT(void) _nc_tty_dispose (void);	/* frees SP->_term */
extern NCURSES_EXPORT(void) _nc_tty_switch_to (void);
extern NCURSES_EXPORT(void) _nc_tty_update (void);

struct tty_display_data {
	int             _fifohold;      /* set if breakout marked           */
	unsigned long   _current_attr;  /* terminal attribute current set   */
	int             _cursrow;       /* physical cursor row (-1=unknown) */
	int             _curscol;       /* physical cursor column           */

	/* cursor movement costs; units are 10ths of milliseconds */
	int             _char_padding;  /* cost of character put            */
	int             _cr_cost;       /* cost of (carriage_return)        */
	int             _cup_cost;      /* cost of (cursor_address)         */
	int             _home_cost;     /* cost of (cursor_home)            */
	int             _ll_cost;       /* cost of (cursor_to_ll)           */
#if USE_HARD_TABS
	int             _ht_cost;       /* cost of (tab)                    */
	int             _cbt_cost;      /* cost of (backtab)                */
#endif /* USE_HARD_TABS */
	int             _cub1_cost;     /* cost of (cursor_left)            */
	int             _cuf1_cost;     /* cost of (cursor_right)           */
	int             _cud1_cost;     /* cost of (cursor_down)            */
	int             _cuu1_cost;     /* cost of (cursor_up)              */
	int             _cub_cost;      /* cost of (parm_cursor_left)       */
	int             _cuf_cost;      /* cost of (parm_cursor_right)      */
	int             _cud_cost;      /* cost of (parm_cursor_down)       */
	int             _cuu_cost;      /* cost of (parm_cursor_up)         */
	int             _hpa_cost;      /* cost of (column_address)         */
	int             _vpa_cost;      /* cost of (row_address)            */
	/* used in lib_doupdate.c, must be chars */
	int             _ed_cost;       /* cost of (clr_eos)                */
	int             _el_cost;       /* cost of (clr_eol)                */
	int             _el1_cost;      /* cost of (clr_bol)                */
	int             _dch1_cost;     /* cost of (delete_character)       */
	int             _ich1_cost;     /* cost of (insert_character)       */
	int             _dch_cost;      /* cost of (parm_dch)               */
	int             _ich_cost;      /* cost of (parm_ich)               */
	int             _ech_cost;      /* cost of (erase_chars)            */
	int             _rep_cost;      /* cost of (repeat_char)            */
	int             _hpa_ch_cost;   /* cost of (column_address)         */
	int             _cup_ch_cost;   /* cost of (cursor_address)         */
	int             _smir_cost;	/* cost of (enter_insert_mode)      */
	int             _rmir_cost;	/* cost of (exit_insert_mode)       */
	int             _ip_cost;       /* cost of (insert_padding)         */
	/* used in lib_mvcur.c */
	char *          _address_cursor;
	int             _carriage_return_length;
	int             _cursor_home_length;
	int             _cursor_to_ll_length;

	chtype          _xmc_suppress;  /* attributes to suppress if xmc     */
	chtype          _xmc_triggers;  /* attributes to process if xmc      */

	bool            _sig_winch;
};


#define DelCharCost(count) \
		((parm_dch != 0) \
		? D->_dch_cost \
		: ((delete_character != 0) \
			? (D->_dch1_cost * count) \
			: INFINITY))

#define InsCharCost(count) \
		((parm_ich != 0) \
		? D->_ich_cost \
		: ((enter_insert_mode && exit_insert_mode) \
		  ? D->_smir_cost + D->_rmir_cost + (D->_ip_cost * count) \
		  : ((insert_character != 0) \
		    ? ((D->_ich1_cost + D->_ip_cost) * count) \
		    : INFINITY)))

#if USE_XMC_SUPPORT
#define UpdateAttrs(c)	if (!SameAttrOf(D->_current_attr, AttrOf(c))) { \
				attr_t chg = D->_current_attr; \
				vidattr(AttrOf(c)); \
				if (magic_cookie_glitch > 0 \
				 && XMC_CHANGES((chg ^ D->_current_attr))) { \
					T(("%s @%d before glitch %d,%d", \
						__FILE__, __LINE__, \
						D->_cursrow, \
						D->_curscol)); \
					_nc_do_xmc_glitch(chg); \
				} \
			}
#else
#define UpdateAttrs(c)	if (!SameAttrOf(D->_current_attr, AttrOf(c))) \
				vidattr(AttrOf(c));
#endif

#define XMC_CHANGES(c) ((c) & D->_xmc_suppress)

#endif /* TTY_DISPLAY_H */
