/* $OpenBSD: lib_bkgd.c,v 1.4 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2020,2021 Thomas E. Dickey                                *
 * Copyright 1998-2014,2016 Free Software Foundation, Inc.                  *
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
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Juergen Pfeifer                         1997                    *
 *     and: Sven Verdoolaege                        2000                    *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: lib_bkgd.c,v 1.4 2023/10/17 09:52:08 nicm Exp $")

static const NCURSES_CH_T blank = NewChar(BLANK_TEXT);

/*
 * Set the window's background information.
 */
#if USE_WIDEC_SUPPORT
NCURSES_EXPORT(void)
#else
static NCURSES_INLINE void
#endif
wbkgrndset(WINDOW *win, const ARG_CH_T ch)
{
    T((T_CALLED("wbkgrndset(%p,%s)"), (void *) win, _tracech_t(ch)));

    if (win) {
	attr_t off = AttrOf(win->_nc_bkgd);
	attr_t on = AttrOf(CHDEREF(ch));

	toggle_attr_off(WINDOW_ATTRS(win), off);
	toggle_attr_on(WINDOW_ATTRS(win), on);

#if NCURSES_EXT_COLORS
	{
	    int pair;

	    if (GetPair(win->_nc_bkgd) != 0)
		SET_WINDOW_PAIR(win, 0);
	    if ((pair = GetPair(CHDEREF(ch))) != 0)
		SET_WINDOW_PAIR(win, pair);
	}
#endif

	if (CharOf(CHDEREF(ch)) == L('\0')) {
	    SetChar(win->_nc_bkgd, BLANK_TEXT, AttrOf(CHDEREF(ch)));
	    if_EXT_COLORS(SetPair(win->_nc_bkgd, GetPair(CHDEREF(ch))));
	} else {
	    win->_nc_bkgd = CHDEREF(ch);
	}
#if USE_WIDEC_SUPPORT
	/*
	 * If we're compiled for wide-character support, _bkgrnd is the
	 * preferred location for the background information since it stores
	 * more than _bkgd.  Update _bkgd each time we modify _bkgrnd, so the
	 * macro getbkgd() will work.
	 */
	{
	    cchar_t wch;
	    int tmp;

	    memset(&wch, 0, sizeof(wch));
	    (void) wgetbkgrnd(win, &wch);
	    tmp = _nc_to_char((wint_t) CharOf(wch));

	    win->_bkgd = (((tmp == EOF) ? ' ' : (chtype) tmp)
			  | (AttrOf(wch) & ALL_BUT_COLOR)
			  | (chtype) ColorPair(GET_WINDOW_PAIR(win)));
	}
#endif
    }
    returnVoid;
}

NCURSES_EXPORT(void)
wbkgdset(WINDOW *win, chtype ch)
{
    NCURSES_CH_T wch;
    T((T_CALLED("wbkgdset(%p,%s)"), (void *) win, _tracechtype(ch)));
    SetChar2(wch, ch);
    wbkgrndset(win, CHREF(wch));
    returnVoid;
}

/*
 * Set the window's background information and apply it to each cell.
 */
static NCURSES_INLINE int
_nc_background(WINDOW *win, const ARG_CH_T ch, bool narrow)
{
#undef  SP_PARM
#define SP_PARM SP		/* to use Charable() */
    int code = ERR;

#if USE_WIDEC_SUPPORT
    T((T_CALLED("%s(%p,%s)"),
       narrow ? "wbkgd" : "wbkgrnd",
       (void *) win,
       _tracecchar_t(ch)));
#define TraceChar(c) _tracecchar_t2(1, &(c))
#else
    T((T_CALLED("%s(%p,%s)"),
       "wbkgd",
       (void *) win,
       _tracech_t(ch)));
    (void) narrow;
#define TraceChar(c) _tracechar(CharOf(c))
#endif

    if (SP == 0) {
	;
    } else if (win) {
	NCURSES_CH_T new_bkgd = CHDEREF(ch);
	NCURSES_CH_T old_bkgd;
	int y;
	NCURSES_CH_T old_char;
	attr_t old_attr;
	int old_pair;
	NCURSES_CH_T new_char;
	attr_t new_attr;
	int new_pair;

	/* SVr4 trims color info if non-color terminal */
	if (!SP->_pair_limit) {
	    RemAttr(new_bkgd, A_COLOR);
	    SetPair(new_bkgd, 0);
	}

	/* avoid setting background-character to a null */
	if (CharOf(new_bkgd) == 0) {
	    NCURSES_CH_T tmp_bkgd = blank;
	    SetAttr(tmp_bkgd, AttrOf(new_bkgd));
	    SetPair(tmp_bkgd, GetPair(new_bkgd));
	    new_bkgd = tmp_bkgd;
	}

	memset(&old_bkgd, 0, sizeof(old_bkgd));
	(void) wgetbkgrnd(win, &old_bkgd);

	if (!memcmp(&old_bkgd, &new_bkgd, sizeof(new_bkgd))) {
	    T(("...unchanged"));
	    returnCode(OK);
	}

	old_char = old_bkgd;
	RemAttr(old_char, ~A_CHARTEXT);
	old_attr = AttrOf(old_bkgd);
	old_pair = GetPair(old_bkgd);

	if (!(old_attr & A_COLOR)) {
	    old_pair = 0;
	}
	T(("... old background char %s, attr %s, pair %d",
	   TraceChar(old_char), _traceattr(old_attr), old_pair));

	new_char = new_bkgd;
	RemAttr(new_char, ~A_CHARTEXT);
	new_attr = AttrOf(new_bkgd);
	new_pair = GetPair(new_bkgd);

	/* SVr4 limits background character to printable 7-bits */
	if (
#if USE_WIDEC_SUPPORT
	       narrow &&
#endif
	       !Charable(new_bkgd)) {
	    new_char = old_char;
	}
	if (!(new_attr & A_COLOR)) {
	    new_pair = 0;
	}
	T(("... new background char %s, attr %s, pair %d",
	   TraceChar(new_char), _traceattr(new_attr), new_pair));

	(void) wbkgrndset(win, CHREF(new_bkgd));

	/* SVr4 updates color pair if old/new match, otherwise just attrs */
	if ((new_pair != 0) && (new_pair == old_pair)) {
	    WINDOW_ATTRS(win) = new_attr;
	    SET_WINDOW_PAIR(win, new_pair);
	} else {
	    WINDOW_ATTRS(win) = new_attr;
	}

	for (y = 0; y <= win->_maxy; y++) {
	    int x;

	    for (x = 0; x <= win->_maxx; x++) {
		NCURSES_CH_T *cp = &(win->_line[y].text[x]);
		int tmp_pair = GetPair(*cp);
		attr_t tmp_attr = AttrOf(*cp);

		if (CharEq(*cp, old_bkgd)) {
#if USE_WIDEC_SUPPORT
		    if (!narrow) {
			if (Charable(new_bkgd)) {
			    SetChar2(*cp, CharOf(new_char));
			} else {
			    SetChar(*cp, L' ', AttrOf(new_char));
			}
			memcpy(cp->chars,
			       new_char.chars,
			       CCHARW_MAX * sizeof(cp->chars[0]));
		    } else
#endif
			SetChar2(*cp, CharOf(new_char));
		}
		if (tmp_pair != 0) {
		    if (tmp_pair == old_pair) {
			SetAttr(*cp, (tmp_attr & ~old_attr) | new_attr);
			SetPair(*cp, new_pair);
		    } else {
			SetAttr(*cp,
				(tmp_attr & (~old_attr | A_COLOR))
				| (new_attr & ALL_BUT_COLOR));
		    }
		} else {
		    SetAttr(*cp, (tmp_attr & ~old_attr) | new_attr);
		    SetPair(*cp, new_pair);
		}
	    }
	}
	touchwin(win);
	_nc_synchook(win);
	code = OK;
    }
    returnCode(code);
}

#if USE_WIDEC_SUPPORT
NCURSES_EXPORT(int)
wbkgrnd(WINDOW *win, const ARG_CH_T ch)
{
    return _nc_background(win, ch, FALSE);
}
#endif

NCURSES_EXPORT(int)
wbkgd(WINDOW *win, chtype ch)
{
    NCURSES_CH_T wch;
    SetChar2(wch, ch);
    return _nc_background(win, CHREF(wch), TRUE);
}
