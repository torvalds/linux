/* $OpenBSD: lib_cchar.c,v 1.2 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2019-2021,2022 Thomas E. Dickey                                *
 * Copyright 2001-2016,2017 Free Software Foundation, Inc.                  *
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

/*
**	lib_cchar.c
**
**	The routines setcchar() and getcchar().
**
*/

#include <curses.priv.h>
#include <wchar.h>

MODULE_ID("$Id: lib_cchar.c,v 1.2 2023/10/17 09:52:09 nicm Exp $")

/*
 * The SuSv2 description leaves some room for interpretation.  We'll assume wch
 * points to a string which is L'\0' terminated, contains at least one
 * character with strictly positive width, which must be the first, and
 * contains no characters of negative width.
 */
NCURSES_EXPORT(int)
setcchar(cchar_t *wcval,
	 const wchar_t *wch,
	 const attr_t attrs,
	 NCURSES_PAIRS_T pair_arg,
	 const void *opts)
{
    int code = OK;
    int color_pair = pair_arg;
    unsigned len;

    TR(TRACE_CCALLS, (T_CALLED("setcchar(%p,%s,attrs=%lu,pair=%d,%p)"),
		      (void *) wcval, _nc_viswbuf(wch),
		      (unsigned long) attrs, color_pair, opts));

    set_extended_pair(opts, color_pair);
    if (wch == NULL
	|| ((len = (unsigned) wcslen(wch)) > 1 && _nc_wacs_width(wch[0]) < 0)
	|| color_pair < 0) {
	code = ERR;
    } else {
	unsigned i;

	if (len > CCHARW_MAX)
	    len = CCHARW_MAX;

	/*
	 * If we have a following spacing-character, stop at that point.  We
	 * are only interested in adding non-spacing characters.
	 */
	for (i = 1; i < len; ++i) {
	    if (_nc_wacs_width(wch[i]) != 0) {
		len = i;
		break;
	    }
	}

	memset(wcval, 0, sizeof(*wcval));

	if (len != 0) {
	    SetAttr(*wcval, attrs);
	    SetPair(CHDEREF(wcval), color_pair);
	    memcpy(&wcval->chars, wch, len * sizeof(wchar_t));
	    TR(TRACE_CCALLS, ("copy %d wchars, first is %s", len,
			      _tracecchar_t(wcval)));
	}
    }

    TR(TRACE_CCALLS, (T_RETURN("%d"), code));
    return (code);
}

NCURSES_EXPORT(int)
getcchar(const cchar_t *wcval,
	 wchar_t *wch,
	 attr_t *attrs,
	 NCURSES_PAIRS_T *pair_arg,
	 void *opts)
{
    int code = ERR;

    TR(TRACE_CCALLS, (T_CALLED("getcchar(%p,%p,%p,%p,%p)"),
		      (const void *) wcval,
		      (void *) wch,
		      (void *) attrs,
		      (void *) pair_arg,
		      opts));

#if !NCURSES_EXT_COLORS
    if (opts != NULL) {
	;			/* empty */
    } else
#endif
    if (wcval != NULL) {
	wchar_t *wp;
	int len;

#if HAVE_WMEMCHR
	len = ((wp = wmemchr(wcval->chars, L'\0', (size_t) CCHARW_MAX))
	       ? (int) (wp - wcval->chars)
	       : CCHARW_MAX);
#else
	len = wcsnlen(wcval->chars, CCHARW_MAX);
#endif
	if (wch == NULL) {
	    /*
	     * If the value is a null, set the length to 1.
	     * If the value is not a null, return the length plus 1 for null.
	     */
	    code = (len < CCHARW_MAX) ? (len + 1) : CCHARW_MAX;
	} else if (attrs == 0 || pair_arg == 0) {
	    code = ERR;
	} else if (len >= 0) {
	    int color_pair;

	    TR(TRACE_CCALLS, ("copy %d wchars, first is %s", len,
			      _tracecchar_t(wcval)));
	    *attrs = AttrOf(*wcval) & A_ATTRIBUTES;
	    color_pair = GetPair(*wcval);
	    get_extended_pair(opts, color_pair);
	    *pair_arg = limit_PAIRS(color_pair);
	    wmemcpy(wch, wcval->chars, (size_t) len);
	    wch[len] = L'\0';
	    if (*pair_arg >= 0)
		code = OK;
	}
    }

    TR(TRACE_CCALLS, (T_RETURN("%d"), code));
    return (code);
}
