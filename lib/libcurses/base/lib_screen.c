/* $OpenBSD: lib_screen.c,v 1.5 2023/10/17 09:52:09 nicm Exp $ */

/****************************************************************************
 * Copyright 2019-2021,2023 Thomas E. Dickey                                *
 * Copyright 1998-2017,2018 Free Software Foundation, Inc.                  *
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
 *     and: Thomas E. Dickey                        1996 on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

#include <curses.priv.h>

#include <ctype.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_screen.c,v 1.5 2023/10/17 09:52:09 nicm Exp $")

#define MAX_SIZE 0x3fff		/* 16k is big enough for a window or pad */

#define MARKER '\\'
#define APPEND '+'
#define GUTTER '|'
#define L_CURL '{'
#define R_CURL '}'

#if USE_STRING_HACKS && HAVE_SNPRINTF
#define ARG_SLIMIT(name) size_t name,
#else
#define ARG_SLIMIT(name)	/* nothing */
#endif

#define CUR_SLIMIT _nc_SLIMIT(limit - (size_t) (target - base))
#define TOP_SLIMIT _nc_SLIMIT(sizeof(buffer))

/*
 * Use 0x888888 as the magic number for new-format files, since it cannot be
 * mistaken for the _cury/_curx pair of 16-bit numbers which start the old
 * format.  It happens to be unused in the file 5.22 database (2015/03/07).
 */
static const char my_magic[] =
{'\210', '\210', '\210', '\210', 0};

#if NCURSES_EXT_PUTWIN
typedef enum {
    pINT			/* int */
    ,pSHORT			/* short */
    ,pBOOL			/* bool */
    ,pATTR			/* attr_t */
    ,pCHAR			/* chtype */
    ,pSIZE			/* NCURSES_SIZE_T */
#if NCURSES_WIDECHAR
    ,pCCHAR			/* cchar_t */
#endif
} PARAM_TYPE;

typedef struct {
    const char name[11];
    attr_t attr;
} SCR_ATTRS;

typedef struct {
    const char name[17];
    PARAM_TYPE type;
    size_t offset;
} SCR_PARAMS;

#define DATA(name) { { #name }, A_##name }
static const SCR_ATTRS scr_attrs[] =
{
    DATA(NORMAL),
    DATA(STANDOUT),
    DATA(UNDERLINE),
    DATA(REVERSE),
    DATA(BLINK),
    DATA(DIM),
    DATA(BOLD),
    DATA(ALTCHARSET),
    DATA(INVIS),
    DATA(PROTECT),
    DATA(HORIZONTAL),
    DATA(LEFT),
    DATA(LOW),
    DATA(RIGHT),
    DATA(TOP),
    DATA(VERTICAL),

#ifdef A_ITALIC
    DATA(ITALIC),
#endif
};
#undef DATA

#define DATA(name, type) { { #name }, type, offsetof(WINDOW, name) }

static const SCR_PARAMS scr_params[] =
{
    DATA(_cury, pSIZE),
    DATA(_curx, pSIZE),
    DATA(_maxy, pSIZE),
    DATA(_maxx, pSIZE),
    DATA(_begy, pSIZE),
    DATA(_begx, pSIZE),
    DATA(_flags, pSHORT),
    DATA(_attrs, pATTR),
    DATA(_bkgd, pCHAR),
    DATA(_notimeout, pBOOL),
    DATA(_clear, pBOOL),
    DATA(_leaveok, pBOOL),
    DATA(_scroll, pBOOL),
    DATA(_idlok, pBOOL),
    DATA(_idcok, pBOOL),
    DATA(_immed, pBOOL),
    DATA(_sync, pBOOL),
    DATA(_use_keypad, pBOOL),
    DATA(_delay, pINT),
    DATA(_regtop, pSIZE),
    DATA(_regbottom, pSIZE),
    DATA(_pad._pad_y, pSIZE),
    DATA(_pad._pad_x, pSIZE),
    DATA(_pad._pad_top, pSIZE),
    DATA(_pad._pad_left, pSIZE),
    DATA(_pad._pad_bottom, pSIZE),
    DATA(_pad._pad_right, pSIZE),
    DATA(_yoffset, pSIZE),
#if NCURSES_WIDECHAR
    DATA(_bkgrnd, pCCHAR),
#if NCURSES_EXT_COLORS
    DATA(_color, pINT),
#endif
#endif
};
#undef DATA

static const NCURSES_CH_T blank = NewChar(BLANK_TEXT);

/*
 * Allocate and read a line of text.  Caller must free it.
 */
static char *
read_txt(FILE *fp)
{
    size_t limit = 1024;
    char *result = malloc(limit);
    char *buffer;

    if (result != 0) {
	int ch = 0;
	size_t used = 0;

	clearerr(fp);
	result[used] = '\0';
	do {
	    if (used + 2 >= limit) {
		limit += 1024;
		buffer = realloc(result, limit);
		if (buffer == 0) {
		    free(result);
		    result = 0;
		    break;
		}
		result = buffer;
	    }
	    ch = fgetc(fp);
	    if (ch == EOF)
		break;
	    result[used++] = (char) ch;
	    result[used] = '\0';
	} while (ch != '\n');

	if (ch == '\n') {
	    result[--used] = '\0';
	    TR(TRACE_IEVENT, ("READ:%s", result));
	} else if (used == 0) {
	    free(result);
	    result = 0;
	}
    }
    return result;
}

static char *
decode_attr(char *source, attr_t *target, int *color)
{
    bool found = FALSE;

    TR(TRACE_IEVENT, ("decode_attr   '%s'", source));

    while (*source) {
	if (source[0] == MARKER && source[1] == L_CURL) {
	    source += 2;
	    found = TRUE;
	} else if (source[0] == R_CURL) {
	    source++;
	    found = FALSE;
	} else if (found) {
	    size_t n;
	    char *next = source;

	    if (source[0] == GUTTER) {
		++next;
	    } else if (*next == 'C') {
		int value = 0;
		unsigned pair;
		next++;
		while (isdigit(UChar(*next))) {
		    value = value * 10 + (*next++ - '0');
		}
		*target &= ~A_COLOR;
		pair = (unsigned) ((value > 256)
				   ? COLOR_PAIR(255)
				   : COLOR_PAIR(value));
		*target |= pair;
		*color = value;
	    } else {
		while (isalnum(UChar(*next))) {
		    ++next;
		}
		for (n = 0; n < SIZEOF(scr_attrs); ++n) {
		    if ((size_t) (next - source) == strlen(scr_attrs[n].name)) {
			if (scr_attrs[n].attr) {
			    *target |= scr_attrs[n].attr;
			} else {
			    *target = A_NORMAL;
			}
			break;
		    }
		}
	    }
	    source = next;
	} else {
	    break;
	}
    }
    return source;
}

static char *
decode_char(char *source, int *target)
{
    int limit = 0;
    int base = 16;
    const char digits[] = "0123456789abcdef";

    TR(TRACE_IEVENT, ("decode_char   '%s'", source));
    *target = ' ';
    switch (*source) {
    case MARKER:
	switch (*++source) {
	case APPEND:
	    break;
	case MARKER:
	    *target = MARKER;
	    ++source;
	    break;
	case 's':
	    *target = ' ';
	    ++source;
	    break;
	case '0':
	case '1':
	case '2':
	case '3':
	    base = 8;
	    limit = 3;
	    break;
	case 'u':
	    limit = 4;
	    ++source;
	    break;
	case 'U':
	    limit = 8;
	    ++source;
	    break;
	}
	if (limit) {
	    *target = 0;
	    while (limit-- > 0) {
		char *find = strchr(digits, *source++);
		int ch = (find != 0) ? (int) (find - digits) : -1;
		*target *= base;
		if (ch >= 0 && ch < base) {
		    *target += ch;
		}
	    }
	}
	break;
    default:
	*target = *source++;
	break;
    }
    return source;
}

static char *
decode_chtype(char *source, chtype fillin, chtype *target)
{
    attr_t attr = ChAttrOf(fillin);
    int color = PAIR_NUMBER((int) attr);
    int value;

    TR(TRACE_IEVENT, ("decode_chtype '%s'", source));
    source = decode_attr(source, &attr, &color);
    source = decode_char(source, &value);
    *target = (ChCharOf(value) | attr | (chtype) COLOR_PAIR(color));
    /* FIXME - ignore combining characters */
    return source;
}

#if NCURSES_WIDECHAR
static char *
decode_cchar(char *source, cchar_t *fillin, cchar_t *target)
{
    int color;
    attr_t attr = fillin->attr;
    wchar_t chars[CCHARW_MAX];
    int append = 0;
    int value = 0;

    TR(TRACE_IEVENT, ("decode_cchar  '%s'", source));
    *target = blank;
#if NCURSES_EXT_COLORS
    color = fillin->ext_color;
#else
    color = (int) PAIR_NUMBER(attr);
#endif
    source = decode_attr(source, &attr, &color);
    memset(chars, 0, sizeof(chars));
    source = decode_char(source, &value);
    chars[0] = (wchar_t) value;
    /* handle combining characters */
    while (source[0] == MARKER && source[1] == APPEND) {
	source += 2;
	source = decode_char(source, &value);
	if (++append < CCHARW_MAX) {
	    chars[append] = (wchar_t) value;
	}
    }
    setcchar(target, chars, attr, (short) color, &color);
    return source;
}
#endif

static int
read_win(WINDOW *win, FILE *fp)
{
    int code = ERR;
    size_t n;
    int color;
#if NCURSES_WIDECHAR
    NCURSES_CH_T prior;
#endif
    chtype prior2;

    memset(win, 0, sizeof(WINDOW));
    for (;;) {
	char *name;
	char *value;
	char *txt = read_txt(fp);

	if (txt == 0)
	    break;
	if (!strcmp(txt, "rows:")) {
	    free(txt);
	    code = OK;
	    break;
	}
	if ((value = strchr(txt, '=')) == 0) {
	    free(txt);
	    continue;
	}
	*value++ = '\0';
	name = !strcmp(txt, "flag") ? value : txt;
	for (n = 0; n < SIZEOF(scr_params); ++n) {
	    if (!strcmp(name, scr_params[n].name)) {
		void *data = (void *) ((char *) win + scr_params[n].offset);

		switch (scr_params[n].type) {
		case pATTR:
		    (void) decode_attr(value, data, &color);
		    break;
		case pBOOL:
		    *(bool *) data = TRUE;
		    break;
		case pCHAR:
		    prior2 = ' ';
		    decode_chtype(value, prior2, data);
		    break;
		case pINT:
		    *(int *) data = atoi(value);
		    break;
		case pSHORT:
		    *(short *) data = (short) atoi(value);
		    break;
		case pSIZE:
		    *(NCURSES_SIZE_T *) data = (NCURSES_SIZE_T) atoi(value);
		    break;
#if NCURSES_WIDECHAR
		case pCCHAR:
		    prior = blank;
		    decode_cchar(value, &prior, data);
		    break;
#endif
		}
		break;
	    }
	}
	free(txt);
    }
    return code;
}

static int
read_row(char *source, NCURSES_CH_T *prior, NCURSES_CH_T *target, int length)
{
    while (*source != '\0' && length > 0) {
#if NCURSES_WIDECHAR
	int len;

	source = decode_cchar(source, prior, target);
	len = _nc_wacs_width(target->chars[0]);
	if (len > 1) {
	    int n;

	    SetWidecExt(CHDEREF(target), 0);
	    for (n = 1; n < len; ++n) {
		target[n] = target[0];
		SetWidecExt(CHDEREF(target), n);
	    }
	    target += (len - 1);
	    length -= (len - 1);
	}
#else
	source = decode_chtype(source, *prior, target);
#endif
	*prior = *target;
	++target;
	--length;
    }
    while (length-- > 0) {
	*target++ = blank;
    }
    /* FIXME - see what error conditions should apply if I need to return ERR */
    return 0;
}
#endif /* NCURSES_EXT_PUTWIN */

/*
 * Originally, getwin/putwin used fread/fwrite, because they used binary data.
 * The new format uses printable ASCII, which does not have as predictable
 * sizes.  Consequently, we use buffered I/O, e.g., fgetc/fprintf, which need
 * special handling if we want to read screen dumps from an older library.
 */
static int
read_block(void *target, size_t length, FILE *fp)
{
    int result = 0;
    char *buffer = target;

    clearerr(fp);
    while (length-- != 0) {
	int ch = fgetc(fp);
	if (ch == EOF) {
	    result = -1;
	    break;
	}
	*buffer++ = (char) ch;
    }
    return result;
}

NCURSES_EXPORT(WINDOW *)
NCURSES_SP_NAME(getwin) (NCURSES_SP_DCLx FILE *filep)
{
    WINDOW tmp, *nwin;
    bool old_format = FALSE;

    T((T_CALLED("getwin(%p)"), (void *) filep));

    if (filep == 0) {
	returnWin(0);
    }

    /*
     * Read the first 4 bytes to determine first if this is an old-format
     * screen-dump, or new-format.
     */
    if (read_block(&tmp, (size_t) 4, filep) < 0) {
	returnWin(0);
    }
    /*
     * If this is a new-format file, and we do not support it, give up.
     */
    if (!memcmp(&tmp, my_magic, (size_t) 4)) {
#if NCURSES_EXT_PUTWIN
	if (read_win(&tmp, filep) < 0)
#endif
	    returnWin(0);
    } else if (read_block(((char *) &tmp) + 4, sizeof(WINDOW) - 4, filep) < 0) {
	returnWin(0);
    } else {
	old_format = TRUE;
    }

    /*
     * Check the window-size:
     */
    if (tmp._maxy == 0 ||
	tmp._maxy > MAX_SIZE ||
	tmp._maxx == 0 ||
	tmp._maxx > MAX_SIZE) {
	returnWin(0);
    }

    if (IS_PAD(&tmp)) {
	nwin = NCURSES_SP_NAME(newpad) (NCURSES_SP_ARGx
					tmp._maxy + 1,
					tmp._maxx + 1);
    } else {
	nwin = NCURSES_SP_NAME(newwin) (NCURSES_SP_ARGx
					tmp._maxy + 1,
					tmp._maxx + 1, 0, 0);
    }

    /*
     * We deliberately do not restore the _parx, _pary, or _parent
     * fields, because the window hierarchy within which they
     * made sense is probably gone.
     */
    if (nwin != 0) {
	int n;
	size_t linesize = sizeof(NCURSES_CH_T) * (size_t) (tmp._maxx + 1);

	nwin->_curx = tmp._curx;
	nwin->_cury = tmp._cury;
	nwin->_maxy = tmp._maxy;
	nwin->_maxx = tmp._maxx;
	nwin->_begy = tmp._begy;
	nwin->_begx = tmp._begx;
	nwin->_yoffset = tmp._yoffset;
	nwin->_flags = tmp._flags & ~(_SUBWIN);

	WINDOW_ATTRS(nwin) = WINDOW_ATTRS(&tmp);
	nwin->_nc_bkgd = tmp._nc_bkgd;

	nwin->_notimeout = tmp._notimeout;
	nwin->_clear = tmp._clear;
	nwin->_leaveok = tmp._leaveok;
	nwin->_idlok = tmp._idlok;
	nwin->_idcok = tmp._idcok;
	nwin->_immed = tmp._immed;
	nwin->_scroll = tmp._scroll;
	nwin->_sync = tmp._sync;
	nwin->_use_keypad = tmp._use_keypad;
	nwin->_delay = tmp._delay;

	nwin->_regtop = tmp._regtop;
	nwin->_regbottom = tmp._regbottom;

	if (IS_PAD(&tmp))
	    nwin->_pad = tmp._pad;

	if (old_format) {
	    T(("reading old-format screen dump"));
	    for (n = 0; n <= nwin->_maxy; n++) {
		if (read_block(nwin->_line[n].text, linesize, filep) < 0) {
		    delwin(nwin);
		    returnWin(0);
		}
	    }
	}
#if NCURSES_EXT_PUTWIN
	else {
	    char *txt = 0;
	    bool success = TRUE;
	    NCURSES_CH_T prior = blank;

	    T(("reading new-format screen dump"));
	    for (n = 0; n <= nwin->_maxy; n++) {
		long row;
		char *next;

		if ((txt = read_txt(filep)) == 0) {
		    T(("...failed to read string for row %d", n + 1));
		    success = FALSE;
		    break;
		}
		row = strtol(txt, &next, 10);
		if (row != (n + 1) || *next != ':') {
		    T(("...failed to read row-number %d", n + 1));
		    success = FALSE;
		    break;
		}

		if (read_row(++next, &prior, nwin->_line[n].text, tmp._maxx
			     + 1) < 0) {
		    T(("...failed to read cells for row %d", n + 1));
		    success = FALSE;
		    break;
		}
		free(txt);
		txt = 0;
	    }

	    if (!success) {
		free(txt);
		delwin(nwin);
		returnWin(0);
	    }
	}
#endif
	touchwin(nwin);
    }
    returnWin(nwin);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(WINDOW *)
getwin(FILE *filep)
{
    return NCURSES_SP_NAME(getwin) (CURRENT_SCREEN, filep);
}
#endif

#if NCURSES_EXT_PUTWIN
static void
encode_attr(char *target, ARG_SLIMIT(limit)
	    attr_t source,
	    attr_t prior,
	    int source_color,
	    int prior_color)
{
#if USE_STRING_HACKS && HAVE_SNPRINTF
    char *base = target;
#endif
    source &= ~A_CHARTEXT;
    prior &= ~A_CHARTEXT;

    *target = '\0';
    if ((source != prior) || (source_color != prior_color)) {
	size_t n;
	bool first = TRUE;

	*target++ = MARKER;
	*target++ = L_CURL;

	for (n = 0; n < SIZEOF(scr_attrs); ++n) {
	    if ((source & scr_attrs[n].attr) != 0 ||
		((source & ALL_BUT_COLOR) == 0 &&
		 (scr_attrs[n].attr == A_NORMAL))) {
		if (first) {
		    first = FALSE;
		} else {
		    *target++ = '|';
		}
		_nc_STRCPY(target, scr_attrs[n].name, limit);
		target += strlen(target);
	    }
	}
	if (source_color != prior_color) {
	    if (!first)
		*target++ = '|';
	    _nc_SPRINTF(target, CUR_SLIMIT "C%d", source_color);
	    target += strlen(target);
	}

	*target++ = R_CURL;
	*target = '\0';
    }
}

static void
encode_cell(char *target, ARG_SLIMIT(limit) CARG_CH_T source, CARG_CH_T previous)
{
#if USE_STRING_HACKS && HAVE_SNPRINTF
    char *base = target;
#endif
#if NCURSES_WIDECHAR
    size_t n;
    int source_pair = GetPair(*source);
    int previous_pair = GetPair(*previous);

    *target = '\0';
    if ((previous->attr != source->attr) || (previous_pair != source_pair)) {
	encode_attr(target, CUR_SLIMIT
		    source->attr,
		    previous->attr,
		    source_pair,
		    previous_pair);
    }
    target += strlen(target);
#if NCURSES_EXT_COLORS
    if (previous->ext_color != source->ext_color) {
	_nc_SPRINTF(target, CUR_SLIMIT
		    "%c%cC%d%c", MARKER, L_CURL, source->ext_color, R_CURL);
    }
#endif
    for (n = 0; n < SIZEOF(source->chars); ++n) {
	unsigned uch = (unsigned) source->chars[n];
	if (uch == 0)
	    continue;
	if (n) {
	    *target++ = MARKER;
	    *target++ = APPEND;
	}
	*target++ = MARKER;
	if (uch > 0xffff) {
	    _nc_SPRINTF(target, CUR_SLIMIT "U%08x", uch);
	} else if (uch > 0xff) {
	    _nc_SPRINTF(target, CUR_SLIMIT "u%04x", uch);
	} else if (uch < 32 || uch >= 127) {
	    _nc_SPRINTF(target, CUR_SLIMIT "%03o", uch & 0xff);
	} else {
	    switch (uch) {
	    case ' ':
		_nc_STRCPY(target, "s", limit);
		break;
	    case MARKER:
		*target++ = MARKER;
		*target = '\0';
		break;
	    default:
		--target;
		_nc_SPRINTF(target, CUR_SLIMIT "%c", uch);
		break;
	    }
	}
	target += strlen(target);
    }
#else
    chtype ch = CharOfD(source);

    *target = '\0';
    if (AttrOfD(previous) != AttrOfD(source)) {
	encode_attr(target, CUR_SLIMIT
		    AttrOfD(source),
		    AttrOfD(previous),
		    GetPair(source),
		    GetPair(previous));
    }
    target += strlen(target);
    *target++ = MARKER;
    if (ch < 32 || ch >= 127) {
	_nc_SPRINTF(target, CUR_SLIMIT "%03o", UChar(ch));
    } else {
	switch (ch) {
	case ' ':
	    _nc_STRCPY(target, "s", limit);
	    break;
	case MARKER:
	    *target++ = MARKER;
	    *target = '\0';
	    break;
	default:
	    --target;
	    _nc_SPRINTF(target, CUR_SLIMIT "%c", UChar(ch));
	    break;
	}
    }
#endif
}
#endif

NCURSES_EXPORT(int)
putwin(WINDOW *win, FILE *filep)
{
    int code = ERR;

    T((T_CALLED("putwin(%p,%p)"), (void *) win, (void *) filep));

#if NCURSES_EXT_PUTWIN
    if (win != 0) {
	const char *version = curses_version();
	char buffer[1024];
	NCURSES_CH_T last_cell;
	int y;

	memset(&last_cell, 0, sizeof(last_cell));

	clearerr(filep);

	/*
	 * Our magic number is technically nonprinting, but aside from that,
	 * all of the file is printable ASCII.
	 */
#define PUTS(s) if (fputs(s, filep) == EOF || ferror(filep)) returnCode(code)
	PUTS(my_magic);
	PUTS(version);
	PUTS("\n");
	for (y = 0; y < (int) SIZEOF(scr_params); ++y) {
	    const char *name = scr_params[y].name;
	    const char *data = (char *) win + scr_params[y].offset;
	    const void *dp = (const void *) data;
	    attr_t attr;

	    *buffer = '\0';
	    if (!strncmp(name, "_pad.", (size_t) 5) && !IS_PAD(win)) {
		continue;
	    }
	    switch (scr_params[y].type) {
	    case pATTR:
		attr = (*(const attr_t *) dp) & ~A_CHARTEXT;
		encode_attr(buffer, TOP_SLIMIT
			    (*(const attr_t *) dp) & ~A_CHARTEXT,
			    A_NORMAL,
			    COLOR_PAIR((int) attr),
			    0);
		break;
	    case pBOOL:
		if (!(*(const bool *) data)) {
		    continue;
		}
		_nc_STRCPY(buffer, name, sizeof(buffer));
		name = "flag";
		break;
	    case pCHAR:
		attr = (*(const attr_t *) dp);
		encode_attr(buffer, TOP_SLIMIT
			    * (const attr_t *) dp,
			    A_NORMAL,
			    COLOR_PAIR((int) attr),
			    0);
		break;
	    case pINT:
		if (!(*(const int *) dp))
		    continue;
		_nc_SPRINTF(buffer, TOP_SLIMIT
			    "%d", *(const int *) dp);
		break;
	    case pSHORT:
		if (!(*(const short *) dp))
		    continue;
		_nc_SPRINTF(buffer, TOP_SLIMIT
			    "%d", *(const short *) dp);
		break;
	    case pSIZE:
		if (!(*(const NCURSES_SIZE_T *) dp))
		    continue;
		_nc_SPRINTF(buffer, TOP_SLIMIT
			    "%d", *(const NCURSES_SIZE_T *) dp);
		break;
#if NCURSES_WIDECHAR
	    case pCCHAR:
		encode_cell(buffer, TOP_SLIMIT
			    (CARG_CH_T) dp, CHREF(last_cell));
		break;
#endif
	    }
	    /*
	     * Only write non-default data.
	     */
	    if (*buffer != '\0') {
		if (fprintf(filep, "%s=%s\n", name, buffer) <= 0
		    || ferror(filep))
		    returnCode(code);
	    }
	}
	/* Write row-data */
	fprintf(filep, "rows:\n");
	for (y = 0; y <= win->_maxy; y++) {
	    NCURSES_CH_T *data = win->_line[y].text;
	    int x;
	    if (fprintf(filep, "%d:", y + 1) <= 0
		|| ferror(filep))
		returnCode(code);
	    for (x = 0; x <= win->_maxx; x++) {
#if NCURSES_WIDECHAR
		int len = _nc_wacs_width(data[x].chars[0]);
		encode_cell(buffer, TOP_SLIMIT CHREF(data[x]), CHREF(last_cell));
		last_cell = data[x];
		PUTS(buffer);
		if (len > 1)
		    x += (len - 1);
#else
		encode_cell(buffer, TOP_SLIMIT CHREF(data[x]), CHREF(last_cell));
		last_cell = data[x];
		PUTS(buffer);
#endif
	    }
	    PUTS("\n");
	}
	code = OK;
    }
#else
    /*
     * This is the original putwin():
     * A straight binary dump is simple, but its format can depend on whether
     * ncurses is compiled with wide-character support, and also may depend
     * on the version of ncurses, e.g., if the WINDOW structure is extended.
     */
    if (win != 0) {
	size_t len = (size_t) (win->_maxx + 1);
	int y;

	clearerr(filep);
	if (fwrite(win, sizeof(WINDOW), (size_t) 1, filep) != 1
	    || ferror(filep))
	      returnCode(code);

	for (y = 0; y <= win->_maxy; y++) {
	    if (fwrite(win->_line[y].text,
		       sizeof(NCURSES_CH_T), len, filep) != len
		|| ferror(filep)) {
		returnCode(code);
	    }
	}
	code = OK;
    }
#endif
    returnCode(code);
}

/*
 * Replace a window covering the whole screen, i.e., newscr or curscr.
 */
static WINDOW *
replace_window(WINDOW *target, FILE *source)
{
    WINDOW *result = getwin(source);
#if NCURSES_EXT_FUNCS
    if (result != NULL) {
	if (getmaxx(result) != getmaxx(target)
	    || getmaxy(result) != getmaxy(target)) {
	    int code = wresize(result,
			       1 + getmaxy(target),
			       1 + getmaxx(target));
	    if (code != OK) {
		delwin(result);
		result = NULL;
	    }
	}
    }
#endif
    delwin(target);
    return result;
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(scr_restore) (NCURSES_SP_DCLx const char *file)
{
    FILE *fp = 0;
    int code = ERR;

    T((T_CALLED("scr_restore(%p,%s)"), (void *) SP_PARM, _nc_visbuf(file)));

    if (_nc_access(file, R_OK) >= 0
	&& (fp = safe_fopen(file, BIN_R)) != 0) {
	NewScreen(SP_PARM) = replace_window(NewScreen(SP_PARM), fp);
#if !USE_REENTRANT
	newscr = NewScreen(SP_PARM);
#endif
	(void) fclose(fp);
	if (NewScreen(SP_PARM) != 0) {
	    code = OK;
	}
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
scr_restore(const char *file)
{
    return NCURSES_SP_NAME(scr_restore) (CURRENT_SCREEN, file);
}
#endif

NCURSES_EXPORT(int)
scr_dump(const char *file)
{
    int result;
    FILE *fp = 0;

    T((T_CALLED("scr_dump(%s)"), _nc_visbuf(file)));

    if (_nc_access(file, W_OK) < 0
	|| (fp = safe_fopen(file, BIN_W)) == 0) {
	result = ERR;
    } else {
	(void) putwin(newscr, fp);
	(void) fclose(fp);
	result = OK;
    }
    returnCode(result);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(scr_init) (NCURSES_SP_DCLx const char *file)
{
    int code = ERR;

    T((T_CALLED("scr_init(%p,%s)"), (void *) SP_PARM, _nc_visbuf(file)));

    if (SP_PARM != 0 &&
#ifdef USE_TERM_DRIVER
	InfoOf(SP_PARM).caninit
#else
	!(exit_ca_mode && non_rev_rmcup)
#endif
	) {
	FILE *fp = 0;

	if (_nc_access(file, R_OK) >= 0
	    && (fp = safe_fopen(file, BIN_R)) != 0) {
	    CurScreen(SP_PARM) = replace_window(CurScreen(SP_PARM), fp);
#if !USE_REENTRANT
	    curscr = CurScreen(SP_PARM);
#endif
	    (void) fclose(fp);
	    if (CurScreen(SP_PARM) != 0) {
		code = OK;
	    }
	}
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
scr_init(const char *file)
{
    return NCURSES_SP_NAME(scr_init) (CURRENT_SCREEN, file);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(scr_set) (NCURSES_SP_DCLx const char *file)
{
    int code = ERR;

    T((T_CALLED("scr_set(%p,%s)"), (void *) SP_PARM, _nc_visbuf(file)));

    if (NCURSES_SP_NAME(scr_init) (NCURSES_SP_ARGx file) == OK) {
	delwin(NewScreen(SP_PARM));
	NewScreen(SP_PARM) = dupwin(curscr);
#if !USE_REENTRANT
	newscr = NewScreen(SP_PARM);
#endif
	if (NewScreen(SP_PARM) != 0) {
	    code = OK;
	}
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
scr_set(const char *file)
{
    return NCURSES_SP_NAME(scr_set) (CURRENT_SCREEN, file);
}
#endif
