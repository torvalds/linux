/* $OpenBSD: lib_color.c,v 1.12 2023/10/17 09:52:08 nicm Exp $ */

/****************************************************************************
 * Copyright 2018-2021,2022 Thomas E. Dickey                                *
 * Copyright 1998-2016,2017 Free Software Foundation, Inc.                  *
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
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/* lib_color.c
 *
 * Handles color emulation of SYS V curses
 */

#define NEW_PAIR_INTERNAL 1

#include <curses.priv.h>
#include <new_pair.h>
#include <tic.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_color.c,v 1.12 2023/10/17 09:52:08 nicm Exp $")

#ifdef USE_TERM_DRIVER
#define CanChange      InfoOf(SP_PARM).canchange
#define DefaultPalette InfoOf(SP_PARM).defaultPalette
#define HasColor       InfoOf(SP_PARM).hascolor
#define InitColor      InfoOf(SP_PARM).initcolor
#define MaxColors      InfoOf(SP_PARM).maxcolors
#define MaxPairs       InfoOf(SP_PARM).maxpairs
#define UseHlsPalette  (DefaultPalette == _nc_hls_palette)
#else
#define CanChange      can_change
#define DefaultPalette (hue_lightness_saturation ? hls_palette : cga_palette)
#define HasColor       has_color
#define InitColor      initialize_color
#define MaxColors      max_colors
#define MaxPairs       max_pairs
#define UseHlsPalette  (hue_lightness_saturation)
#endif

#ifndef USE_TERM_DRIVER
/*
 * These should be screen structure members.  They need to be globals for
 * historical reasons.  So we assign them in start_color() and also in
 * set_term()'s screen-switching logic.
 */
#if USE_REENTRANT
NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(COLOR_PAIRS) (void)
{
    return SP ? SP->_pair_count : -1;
}
NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(COLORS) (void)
{
    return SP ? SP->_color_count : -1;
}
#else
NCURSES_EXPORT_VAR(int) COLOR_PAIRS = 0;
NCURSES_EXPORT_VAR(int) COLORS = 0;
#endif
#endif /* !USE_TERM_DRIVER */

#define DATA(r,g,b) {r,g,b, 0,0,0, 0}

#define MAX_PALETTE	8

#define OkColorHi(n)	(((n) < COLORS) && ((n) < maxcolors))
#define InPalette(n)	((n) >= 0 && (n) < MAX_PALETTE)

/*
 * Given a RGB range of 0..1000, we'll normally set the individual values
 * to about 2/3 of the maximum, leaving full-range for bold/bright colors.
 */
#define RGB_ON  680
#define RGB_OFF 0
/* *INDENT-OFF* */
static const color_t cga_palette[] =
{
    /*  R               G               B */
    DATA(RGB_OFF,	RGB_OFF,	RGB_OFF),	/* COLOR_BLACK */
    DATA(RGB_ON,	RGB_OFF,	RGB_OFF),	/* COLOR_RED */
    DATA(RGB_OFF,	RGB_ON,		RGB_OFF),	/* COLOR_GREEN */
    DATA(RGB_ON,	RGB_ON,		RGB_OFF),	/* COLOR_YELLOW */
    DATA(RGB_OFF,	RGB_OFF,	RGB_ON),	/* COLOR_BLUE */
    DATA(RGB_ON,	RGB_OFF,	RGB_ON),	/* COLOR_MAGENTA */
    DATA(RGB_OFF,	RGB_ON,		RGB_ON),	/* COLOR_CYAN */
    DATA(RGB_ON,	RGB_ON,		RGB_ON),	/* COLOR_WHITE */
};

static const color_t hls_palette[] =
{
    /*  	H       L       S */
    DATA(	0,	0,	0),		/* COLOR_BLACK */
    DATA(	120,	50,	100),		/* COLOR_RED */
    DATA(	240,	50,	100),		/* COLOR_GREEN */
    DATA(	180,	50,	100),		/* COLOR_YELLOW */
    DATA(	330,	50,	100),		/* COLOR_BLUE */
    DATA(	60,	50,	100),		/* COLOR_MAGENTA */
    DATA(	300,	50,	100),		/* COLOR_CYAN */
    DATA(	0,	50,	100),		/* COLOR_WHITE */
};

#ifdef USE_TERM_DRIVER
NCURSES_EXPORT_VAR(const color_t*) _nc_cga_palette = cga_palette;
NCURSES_EXPORT_VAR(const color_t*) _nc_hls_palette = hls_palette;
#endif

/* *INDENT-ON* */
#if NCURSES_EXT_FUNCS
/*
 * These are called from _nc_do_color(), which in turn is called from
 * vidattr - so we have to assume that sp may be null.
 */
static int
default_fg(NCURSES_SP_DCL0)
{
    return (SP_PARM != 0) ? SP_PARM->_default_fg : COLOR_WHITE;
}

static int
default_bg(NCURSES_SP_DCL0)
{
    return SP_PARM != 0 ? SP_PARM->_default_bg : COLOR_BLACK;
}
#else
#define default_fg(sp) COLOR_WHITE
#define default_bg(sp) COLOR_BLACK
#endif

#ifndef USE_TERM_DRIVER
/*
 * SVr4 curses is known to interchange color codes (1,4) and (3,6), possibly
 * to maintain compatibility with a pre-ANSI scheme.  The same scheme is
 * also used in the FreeBSD syscons.
 */
static int
toggled_colors(int c)
{
    if (c < 16) {
	static const int table[] =
	{0, 4, 2, 6, 1, 5, 3, 7,
	 8, 12, 10, 14, 9, 13, 11, 15};
	c = table[c];
    }
    return c;
}
#endif

static void
set_background_color(NCURSES_SP_DCLx int bg, NCURSES_SP_OUTC outc)
{
#ifdef USE_TERM_DRIVER
    CallDriver_3(SP_PARM, td_color, FALSE, bg, outc);
#else
    if (set_a_background) {
	TPUTS_TRACE("set_a_background");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TIPARM_1(set_a_background, bg),
				1, outc);
    } else {
	TPUTS_TRACE("set_background");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TIPARM_1(set_background, toggled_colors(bg)),
				1, outc);
    }
#endif
}

static void
set_foreground_color(NCURSES_SP_DCLx int fg, NCURSES_SP_OUTC outc)
{
#ifdef USE_TERM_DRIVER
    CallDriver_3(SP_PARM, td_color, TRUE, fg, outc);
#else
    if (set_a_foreground) {
	TPUTS_TRACE("set_a_foreground");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TIPARM_1(set_a_foreground, fg),
				1, outc);
    } else {
	TPUTS_TRACE("set_foreground");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TIPARM_1(set_foreground, toggled_colors(fg)),
				1, outc);
    }
#endif
}

static void
init_color_table(NCURSES_SP_DCL0)
{
    const color_t *tp = DefaultPalette;
    int n;

    assert(tp != 0);

    for (n = 0; n < COLORS; n++) {
	if (InPalette(n)) {
	    SP_PARM->_color_table[n] = tp[n];
	} else {
	    SP_PARM->_color_table[n] = tp[n % MAX_PALETTE];
	    if (UseHlsPalette) {
		SP_PARM->_color_table[n].green = 100;
	    } else {
		if (SP_PARM->_color_table[n].red)
		    SP_PARM->_color_table[n].red = 1000;
		if (SP_PARM->_color_table[n].green)
		    SP_PARM->_color_table[n].green = 1000;
		if (SP_PARM->_color_table[n].blue)
		    SP_PARM->_color_table[n].blue = 1000;
	    }
	}
    }
}

static bool
init_direct_colors(NCURSES_SP_DCL0)
{
    static NCURSES_CONST char name[] = "RGB";

    rgb_bits_t *result = &(SP_PARM->_direct_color);

    result->value = 0;

    if (COLORS >= 8) {
	int n;
	const char *s;
	int width;

	/* find the number of bits needed for the maximum color value */
	for (width = 0; (1 << width) - 1 < (COLORS - 1); ++width) {
	    ;
	}

	if (tigetflag(name) > 0) {
	    n = (width + 2) / 3;
	    result->bits.red = UChar(n);
	    result->bits.green = UChar(n);
	    result->bits.blue = UChar(width - (2 * n));
	} else if ((n = tigetnum(name)) > 0) {
	    result->bits.red = UChar(n);
	    result->bits.green = UChar(n);
	    result->bits.blue = UChar(n);
	} else if ((s = tigetstr(name)) != 0 && VALID_STRING(s)) {
	    int red = n;
	    int green = n;
	    int blue = width - (2 * n);

	    switch (sscanf(s, "%d/%d/%d", &red, &green, &blue)) {
	    default:
		blue = width - (2 * n);
		/* FALLTHRU */
	    case 1:
		green = n;
		/* FALLTHRU */
	    case 2:
		red = n;
		/* FALLTHRU */
	    case 3:
		/* okay */
		break;
	    }
	    result->bits.red = UChar(red);
	    result->bits.green = UChar(green);
	    result->bits.blue = UChar(blue);
	}
    }
    return (result->value != 0);
}

/*
 * Reset the color pair, e.g., to whatever color pair 0 is.
 */
static bool
reset_color_pair(NCURSES_SP_DCL0)
{
#ifdef USE_TERM_DRIVER
    return CallDriver(SP_PARM, td_rescol);
#else
    bool result = FALSE;

    (void) SP_PARM;
    if (orig_pair != 0) {
	(void) NCURSES_PUTP2("orig_pair", orig_pair);
	result = TRUE;
    }
    return result;
#endif
}

/*
 * Reset color pairs and definitions.  Actually we do both more to accommodate
 * badly-written terminal descriptions than for the relatively rare case where
 * someone has changed the color definitions.
 */
NCURSES_EXPORT(bool)
NCURSES_SP_NAME(_nc_reset_colors) (NCURSES_SP_DCL0)
{
    int result = FALSE;

    T((T_CALLED("_nc_reset_colors(%p)"), (void *) SP_PARM));
    if (SP_PARM->_color_defs > 0)
	SP_PARM->_color_defs = -(SP_PARM->_color_defs);
    if (reset_color_pair(NCURSES_SP_ARG))
	result = TRUE;

#ifdef USE_TERM_DRIVER
    result = CallDriver(SP_PARM, td_rescolors);
#else
    if (orig_colors != 0) {
	NCURSES_PUTP2("orig_colors", orig_colors);
	result = TRUE;
    }
#endif
    returnBool(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
_nc_reset_colors(void)
{
    return NCURSES_SP_NAME(_nc_reset_colors) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(start_color) (NCURSES_SP_DCL0)
{
    int result = ERR;

    T((T_CALLED("start_color(%p)"), (void *) SP_PARM));

    if (SP_PARM == 0) {
	result = ERR;
    } else if (SP_PARM->_coloron) {
	result = OK;
    } else {
	int maxpairs = MaxPairs;
	int maxcolors = MaxColors;
	if (reset_color_pair(NCURSES_SP_ARG) != TRUE) {
	    set_foreground_color(NCURSES_SP_ARGx
				 default_fg(NCURSES_SP_ARG),
				 NCURSES_SP_NAME(_nc_outch));
	    set_background_color(NCURSES_SP_ARGx
				 default_bg(NCURSES_SP_ARG),
				 NCURSES_SP_NAME(_nc_outch));
	}
#if !NCURSES_EXT_COLORS
	/*
	 * Without ext-colors, we cannot represent more than 256 color pairs.
	 */
	if (maxpairs > 256)
	    maxpairs = 256;
#endif

	if (maxpairs > 0 && maxcolors > 0) {
	    SP_PARM->_pair_limit = maxpairs;

#if NCURSES_EXT_FUNCS
	    /*
	     * If using default colors, allocate extra space in table to
	     * allow for default-color as a component of a color-pair.
	     */
	    SP_PARM->_pair_limit += (1 + (2 * maxcolors));
#if !NCURSES_EXT_COLORS
	    SP_PARM->_pair_limit = limit_PAIRS(SP_PARM->_pair_limit);
#endif
#endif /* NCURSES_EXT_FUNCS */
	    SP_PARM->_pair_count = maxpairs;
	    SP_PARM->_color_count = maxcolors;
#if !USE_REENTRANT
	    COLOR_PAIRS = maxpairs;
	    COLORS = maxcolors;
#endif

	    ReservePairs(SP_PARM, 16);
	    if (SP_PARM->_color_pairs != 0) {
		if (init_direct_colors(NCURSES_SP_ARG)) {
		    result = OK;
		} else {
		    TYPE_CALLOC(color_t, maxcolors, SP_PARM->_color_table);
		    if (SP_PARM->_color_table != 0) {
			MakeColorPair(SP_PARM->_color_pairs[0],
				      default_fg(NCURSES_SP_ARG),
				      default_bg(NCURSES_SP_ARG));
			init_color_table(NCURSES_SP_ARG);

			result = OK;
		    }
		}
		if (result == OK) {
		    T(("started color: COLORS = %d, COLOR_PAIRS = %d",
		       COLORS, COLOR_PAIRS));

		    SP_PARM->_coloron = 1;
		} else if (SP_PARM->_color_pairs != 0) {
		    FreeAndNull(SP_PARM->_color_pairs);
		}
	    }
	} else {
	    result = OK;
	}
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
start_color(void)
{
    return NCURSES_SP_NAME(start_color) (CURRENT_SCREEN);
}
#endif

/* This function was originally written by Daniel Weaver <danw@znyx.com> */
static void
rgb2hls(int r, int g, int b, int *h, int *l, int *s)
/* convert RGB to HLS system */
{
    int min, max, t;

    if ((min = g < r ? g : r) > b)
	min = b;
    if ((max = g > r ? g : r) < b)
	max = b;

    /* calculate lightness */
    *l = ((min + max) / 20);

    if (min == max) {		/* black, white and all shades of gray */
	*h = 0;
	*s = 0;
	return;
    }

    /* calculate saturation */
    if (*l < 50)
	*s = (((max - min) * 100) / (max + min));
    else
	*s = (((max - min) * 100) / (2000 - max - min));

    /* calculate hue */
    if (r == max)
	t = (120 + ((g - b) * 60) / (max - min));
    else if (g == max)
	t = (240 + ((b - r) * 60) / (max - min));
    else
	t = (360 + ((r - g) * 60) / (max - min));

    *h = (t % 360);
}

/*
 * Change all cells which use(d) a given color pair to force a repaint.
 */
NCURSES_EXPORT(void)
_nc_change_pair(SCREEN *sp, int pair)
{
    int y, x;

    if (CurScreen(sp)->_clear)
	return;
#if NO_LEAKS
    if (_nc_globals.leak_checking)
	return;
#endif

    for (y = 0; y <= CurScreen(sp)->_maxy; y++) {
	struct ldat *ptr = &(CurScreen(sp)->_line[y]);
	bool changed = FALSE;
	for (x = 0; x <= CurScreen(sp)->_maxx; x++) {
	    if (GetPair(ptr->text[x]) == pair) {
		/* Set the old cell to zero to ensure it will be
		   updated on the next doupdate() */
		SetChar(ptr->text[x], 0, 0);
		CHANGED_CELL(ptr, x);
		changed = TRUE;
	    }
	}
	if (changed)
	    NCURSES_SP_NAME(_nc_make_oldhash) (NCURSES_SP_ARGx y);
    }
}

NCURSES_EXPORT(void)
_nc_reserve_pairs(SCREEN *sp, int want)
{
    int have = sp->_pair_alloc;

    if (have == 0)
	have = 1;
    while (have <= want)
	have *= 2;
    if (have > sp->_pair_limit)
	have = sp->_pair_limit;

    if (sp->_color_pairs == 0) {
	TYPE_CALLOC(colorpair_t, have, sp->_color_pairs);
    } else if (have > sp->_pair_alloc) {
#if NCURSES_EXT_COLORS
	colorpair_t *next;

	if ((next = typeCalloc(colorpair_t, have)) == 0)
	    _nc_err_abort(MSG_NO_MEMORY);
	memcpy(next, sp->_color_pairs, (size_t) sp->_pair_alloc * sizeof(*next));
	_nc_copy_pairs(sp, next, sp->_color_pairs, sp->_pair_alloc);
	free(sp->_color_pairs);
	sp->_color_pairs = next;
#else
	TYPE_REALLOC(colorpair_t, have, sp->_color_pairs);
	if (sp->_color_pairs != 0) {
	    memset(sp->_color_pairs + sp->_pair_alloc, 0,
		   sizeof(colorpair_t) * (size_t) (have - sp->_pair_alloc));
	}
#endif
    }
    if (sp->_color_pairs != 0) {
	sp->_pair_alloc = have;
    }
}

/*
 * Extension (1997/1/18) - Allow negative f/b values to set default color
 * values.
 */
NCURSES_EXPORT(int)
_nc_init_pair(SCREEN *sp, int pair, int f, int b)
{
    static colorpair_t null_pair;
    colorpair_t result = null_pair;
    colorpair_t previous;
    int maxcolors;

    T((T_CALLED("init_pair(%p,%d,%d,%d)"), (void *) sp, pair, f, b));

    if (!ValidPair(sp, pair))
	returnCode(ERR);

    maxcolors = MaxColors;

    ReservePairs(sp, pair);
    previous = sp->_color_pairs[pair];
#if NCURSES_EXT_FUNCS
    if (sp->_default_color || sp->_assumed_color) {
	bool isDefault = FALSE;
	bool wasDefault = FALSE;
	int default_pairs = sp->_default_pairs;

	/*
	 * Map caller's color number, e.g., -1, 0, 1, .., 7, etc., into
	 * internal unsigned values which we will store in the _color_pairs[]
	 * table.
	 */
	if (isDefaultColor(f)) {
	    f = COLOR_DEFAULT;
	    isDefault = TRUE;
	} else if (!OkColorHi(f)) {
	    returnCode(ERR);
	}

	if (isDefaultColor(b)) {
	    b = COLOR_DEFAULT;
	    isDefault = TRUE;
	} else if (!OkColorHi(b)) {
	    returnCode(ERR);
	}

	/*
	 * Check if the table entry that we are going to init/update used
	 * default colors.
	 */
	if (isDefaultColor(FORE_OF(previous))
	    || isDefaultColor(BACK_OF(previous)))
	    wasDefault = TRUE;

	/*
	 * Keep track of the number of entries in the color pair table which
	 * used a default color.
	 */
	if (isDefault && !wasDefault) {
	    ++default_pairs;
	} else if (wasDefault && !isDefault) {
	    --default_pairs;
	}

	/*
	 * As an extension, ncurses allows the pair number to exceed the
	 * terminal's color_pairs value for pairs using a default color.
	 *
	 * Note that updating a pair which used a default color with one
	 * that does not will decrement the count - and possibly interfere
	 * with sequentially adding new pairs.
	 */
	if (pair > (sp->_pair_count + default_pairs)) {
	    returnCode(ERR);
	}
	sp->_default_pairs = default_pairs;
    } else
#endif
    {
	if ((f < 0) || !OkColorHi(f)
	    || (b < 0) || !OkColorHi(b)
	    || (pair < 1)) {
	    returnCode(ERR);
	}
    }

    /*
     * When a pair's content is changed, replace its colors (if pair was
     * initialized before a screen update is performed replacing original
     * pair colors with the new ones).
     */
    MakeColorPair(result, f, b);
    if ((FORE_OF(previous) != 0
	 || BACK_OF(previous) != 0)
	&& !isSamePair(previous, result)) {
	_nc_change_pair(sp, pair);
    }

    _nc_reset_color_pair(sp, pair, &result);
    sp->_color_pairs[pair] = result;
    _nc_set_color_pair(sp, pair, cpINIT);

    if (GET_SCREEN_PAIR(sp) == pair)
	SET_SCREEN_PAIR(sp, (int) (~0));	/* force attribute update */

#ifdef USE_TERM_DRIVER
    CallDriver_3(sp, td_initpair, pair, f, b);
#else
    if (initialize_pair && InPalette(f) && InPalette(b)) {
	const color_t *tp = DefaultPalette;

	TR(TRACE_ATTRS,
	   ("initializing pair: pair = %d, fg=(%d,%d,%d), bg=(%d,%d,%d)",
	    (int) pair,
	    (int) tp[f].red, (int) tp[f].green, (int) tp[f].blue,
	    (int) tp[b].red, (int) tp[b].green, (int) tp[b].blue));

	NCURSES_PUTP2("initialize_pair",
		      TIPARM_7(initialize_pair,
			       pair,
			       (int) tp[f].red,
			       (int) tp[f].green,
			       (int) tp[f].blue,
			       (int) tp[b].red,
			       (int) tp[b].green,
			       (int) tp[b].blue));
    }
#endif

    returnCode(OK);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(init_pair) (NCURSES_SP_DCLx
			    NCURSES_PAIRS_T pair,
			    NCURSES_COLOR_T f,
			    NCURSES_COLOR_T b)
{
    return _nc_init_pair(SP_PARM, pair, f, b);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
init_pair(NCURSES_COLOR_T pair, NCURSES_COLOR_T f, NCURSES_COLOR_T b)
{
    return NCURSES_SP_NAME(init_pair) (CURRENT_SCREEN, pair, f, b);
}
#endif

#define okRGB(n) ((n) >= 0 && (n) <= 1000)

NCURSES_EXPORT(int)
_nc_init_color(SCREEN *sp, int color, int r, int g, int b)
{
    int result = ERR;
    int maxcolors;

    T((T_CALLED("init_color(%p,%d,%d,%d,%d)"),
       (void *) sp,
       color,
       r, g, b));

    if (sp == 0 || sp->_direct_color.value)
	returnCode(result);

    maxcolors = MaxColors;

    if (InitColor
	&& sp->_coloron
	&& (color >= 0 && OkColorHi(color))
	&& (okRGB(r) && okRGB(g) && okRGB(b))) {

	sp->_color_table[color].init = 1;
	sp->_color_table[color].r = r;
	sp->_color_table[color].g = g;
	sp->_color_table[color].b = b;

	if (UseHlsPalette) {
	    rgb2hls(r, g, b,
		    &sp->_color_table[color].red,
		    &sp->_color_table[color].green,
		    &sp->_color_table[color].blue);
	} else {
	    sp->_color_table[color].red = r;
	    sp->_color_table[color].green = g;
	    sp->_color_table[color].blue = b;
	}

#ifdef USE_TERM_DRIVER
	CallDriver_4(sp, td_initcolor, color, r, g, b);
#else
	NCURSES_PUTP2("initialize_color",
		      TIPARM_4(initialize_color, color, r, g, b));
#endif
	sp->_color_defs = max(color + 1, sp->_color_defs);

	result = OK;
    }
    returnCode(result);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(init_color) (NCURSES_SP_DCLx
			     NCURSES_COLOR_T color,
			     NCURSES_COLOR_T r,
			     NCURSES_COLOR_T g,
			     NCURSES_COLOR_T b)
{
    return _nc_init_color(SP_PARM, color, r, g, b);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
init_color(NCURSES_COLOR_T color,
	   NCURSES_COLOR_T r,
	   NCURSES_COLOR_T g,
	   NCURSES_COLOR_T b)
{
    return NCURSES_SP_NAME(init_color) (CURRENT_SCREEN, color, r, g, b);
}
#endif

NCURSES_EXPORT(bool)
NCURSES_SP_NAME(can_change_color) (NCURSES_SP_DCL)
{
    int result = FALSE;

    T((T_CALLED("can_change_color(%p)"), (void *) SP_PARM));

    if (HasTerminal(SP_PARM) && (CanChange != 0)) {
	result = TRUE;
    }

    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
can_change_color(void)
{
    return NCURSES_SP_NAME(can_change_color) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(bool)
NCURSES_SP_NAME(has_colors) (NCURSES_SP_DCL0)
{
    int code = FALSE;

    (void) SP_PARM;
    T((T_CALLED("has_colors(%p)"), (void *) SP_PARM));
    if (HasTerminal(SP_PARM)) {
#ifdef USE_TERM_DRIVER
	code = HasColor;
#else
	code = ((VALID_NUMERIC(max_colors) && VALID_NUMERIC(max_pairs)
		 && (((set_foreground != NULL)
		      && (set_background != NULL))
		     || ((set_a_foreground != NULL)
			 && (set_a_background != NULL))
		     || set_color_pair)) ? TRUE : FALSE);
#endif
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
has_colors(void)
{
    return NCURSES_SP_NAME(has_colors) (CURRENT_SCREEN);
}
#endif

static int
_nc_color_content(SCREEN *sp, int color, int *r, int *g, int *b)
{
    int result = ERR;

    T((T_CALLED("color_content(%p,%d,%p,%p,%p)"),
       (void *) sp,
       color,
       (void *) r,
       (void *) g,
       (void *) b));

    if (sp != 0) {
	int maxcolors = MaxColors;

	if (color >= 0 && OkColorHi(color) && sp->_coloron) {
	    int c_r, c_g, c_b;

	    if (sp->_direct_color.value) {
		rgb_bits_t *work = &(sp->_direct_color);

#define max_direct_color(name)	((1 << work->bits.name) - 1)
#define value_direct_color(max) (1000 * ((color >> bitoff) & max)) / max

		int max_r = max_direct_color(red);
		int max_g = max_direct_color(green);
		int max_b = max_direct_color(blue);

		int bitoff = 0;

		c_b = value_direct_color(max_b);
		bitoff += work->bits.blue;

		c_g = value_direct_color(max_g);
		bitoff += work->bits.green;

		c_r = value_direct_color(max_r);

	    } else {
		c_r = sp->_color_table[color].red;
		c_g = sp->_color_table[color].green;
		c_b = sp->_color_table[color].blue;
	    }

	    if (r)
		*r = c_r;
	    if (g)
		*g = c_g;
	    if (b)
		*b = c_b;

	    TR(TRACE_ATTRS, ("...color_content(%d,%d,%d,%d)",
			     color, c_r, c_g, c_b));
	    result = OK;
	}
    }
    if (result != OK) {
	if (r)
	    *r = 0;
	if (g)
	    *g = 0;
	if (b)
	    *b = 0;
    }
    returnCode(result);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(color_content) (NCURSES_SP_DCLx
				NCURSES_COLOR_T color,
				NCURSES_COLOR_T *r,
				NCURSES_COLOR_T *g,
				NCURSES_COLOR_T *b)
{
    int my_r, my_g, my_b;
    int rc = _nc_color_content(SP_PARM, color, &my_r, &my_g, &my_b);
    if (rc == OK) {
	*r = limit_COLOR(my_r);
	*g = limit_COLOR(my_g);
	*b = limit_COLOR(my_b);
    }
    return rc;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
color_content(NCURSES_COLOR_T color,
	      NCURSES_COLOR_T *r,
	      NCURSES_COLOR_T *g,
	      NCURSES_COLOR_T *b)
{
    return NCURSES_SP_NAME(color_content) (CURRENT_SCREEN, color, r, g, b);
}
#endif

NCURSES_EXPORT(int)
_nc_pair_content(SCREEN *sp, int pair, int *f, int *b)
{
    int result;

    T((T_CALLED("pair_content(%p,%d,%p,%p)"),
       (void *) sp,
       (int) pair,
       (void *) f,
       (void *) b));

    if (!ValidPair(sp, pair)) {
	result = ERR;
    } else {
	int fg;
	int bg;

	ReservePairs(sp, pair);
	fg = FORE_OF(sp->_color_pairs[pair]);
	bg = BACK_OF(sp->_color_pairs[pair]);
#if NCURSES_EXT_FUNCS
	if (isDefaultColor(fg))
	    fg = -1;
	if (isDefaultColor(bg))
	    bg = -1;
#endif

	if (f)
	    *f = fg;
	if (b)
	    *b = bg;

	TR(TRACE_ATTRS, ("...pair_content(%p,%d,%d,%d)",
			 (void *) sp,
			 (int) pair,
			 (int) fg, (int) bg));
	result = OK;
    }
    returnCode(result);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(pair_content) (NCURSES_SP_DCLx
			       NCURSES_PAIRS_T pair,
			       NCURSES_COLOR_T *f,
			       NCURSES_COLOR_T *b)
{
    int my_f, my_b;
    int rc = _nc_pair_content(SP_PARM, pair, &my_f, &my_b);
    if (rc == OK) {
	*f = limit_COLOR(my_f);
	*b = limit_COLOR(my_b);
    }
    return rc;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
pair_content(NCURSES_COLOR_T pair, NCURSES_COLOR_T *f, NCURSES_COLOR_T *b)
{
    return NCURSES_SP_NAME(pair_content) (CURRENT_SCREEN, pair, f, b);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_do_color) (NCURSES_SP_DCLx
			       int old_pair,
			       int pair,
			       int reverse,
			       NCURSES_SP_OUTC outc)
{
#ifdef USE_TERM_DRIVER
    CallDriver_4(SP_PARM, td_docolor, old_pair, pair, reverse, outc);
#else
    int fg = COLOR_DEFAULT;
    int bg = COLOR_DEFAULT;
    int old_fg = -1;
    int old_bg = -1;

    if (!ValidPair(SP_PARM, pair)) {
	return;
    } else if (pair != 0) {
	if (set_color_pair) {
	    TPUTS_TRACE("set_color_pair");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    TIPARM_1(set_color_pair, pair),
				    1, outc);
	    return;
	} else if (SP_PARM != 0) {
	    if (_nc_pair_content(SP_PARM, pair, &fg, &bg) == ERR)
		return;
	}
    }

    if (old_pair >= 0
	&& SP_PARM != 0
	&& _nc_pair_content(SP_PARM, old_pair, &old_fg, &old_bg) != ERR) {
	if ((isDefaultColor(fg) && !isDefaultColor(old_fg))
	    || (isDefaultColor(bg) && !isDefaultColor(old_bg))) {
#if NCURSES_EXT_FUNCS
	    /*
	     * A minor optimization - but extension.  If "AX" is specified in
	     * the terminal description, treat it as screen's indicator of ECMA
	     * SGR 39 and SGR 49, and assume the two sequences are independent.
	     */
	    if (SP_PARM->_has_sgr_39_49
		&& isDefaultColor(old_bg)
		&& !isDefaultColor(old_fg)) {
		NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx "\033[39m", 1, outc);
	    } else if (SP_PARM->_has_sgr_39_49
		       && isDefaultColor(old_fg)
		       && !isDefaultColor(old_bg)) {
		NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx "\033[49m", 1, outc);
	    } else
#endif
		reset_color_pair(NCURSES_SP_ARG);
	}
    } else {
	reset_color_pair(NCURSES_SP_ARG);
	if (old_pair < 0 && pair <= 0)
	    return;
    }

#if NCURSES_EXT_FUNCS
    if (isDefaultColor(fg))
	fg = default_fg(NCURSES_SP_ARG);
    if (isDefaultColor(bg))
	bg = default_bg(NCURSES_SP_ARG);
#endif

    if (reverse) {
	int xx = fg;
	fg = bg;
	bg = xx;
    }

    TR(TRACE_ATTRS, ("setting colors: pair = %d, fg = %d, bg = %d", pair,
		     fg, bg));

    if (!isDefaultColor(fg)) {
	set_foreground_color(NCURSES_SP_ARGx fg, outc);
    }
    if (!isDefaultColor(bg)) {
	set_background_color(NCURSES_SP_ARGx bg, outc);
    }
#endif
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_do_color(int old_pair, int pair, int reverse, NCURSES_OUTC outc)
{
    SetSafeOutcWrapper(outc);
    NCURSES_SP_NAME(_nc_do_color) (CURRENT_SCREEN,
				   old_pair,
				   pair,
				   reverse,
				   _nc_outc_wrapper);
}
#endif

#if NCURSES_EXT_COLORS
NCURSES_EXPORT(int)
NCURSES_SP_NAME(init_extended_pair) (NCURSES_SP_DCLx int pair, int f, int b)
{
    return _nc_init_pair(SP_PARM, pair, f, b);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(init_extended_color) (NCURSES_SP_DCLx
				      int color,
				      int r, int g, int b)
{
    return _nc_init_color(SP_PARM, color, r, g, b);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(extended_color_content) (NCURSES_SP_DCLx
					 int color,
					 int *r, int *g, int *b)
{
    return _nc_color_content(SP_PARM, color, r, g, b);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(extended_pair_content) (NCURSES_SP_DCLx
					int pair,
					int *f, int *b)
{
    return _nc_pair_content(SP_PARM, pair, f, b);
}

NCURSES_EXPORT(void)
NCURSES_SP_NAME(reset_color_pairs) (NCURSES_SP_DCL0)
{
    if (SP_PARM != 0) {
	if (SP_PARM->_color_pairs) {
	    _nc_free_ordered_pairs(SP_PARM);
	    free(SP_PARM->_color_pairs);
	    SP_PARM->_color_pairs = 0;
	    SP_PARM->_pair_alloc = 0;
	    ReservePairs(SP_PARM, 16);
	    clearok(CurScreen(SP_PARM), TRUE);
	    touchwin(StdScreen(SP_PARM));
	}
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
init_extended_pair(int pair, int f, int b)
{
    return NCURSES_SP_NAME(init_extended_pair) (CURRENT_SCREEN, pair, f, b);
}

NCURSES_EXPORT(int)
init_extended_color(int color, int r, int g, int b)
{
    return NCURSES_SP_NAME(init_extended_color) (CURRENT_SCREEN,
						 color,
						 r, g, b);
}

NCURSES_EXPORT(int)
extended_color_content(int color, int *r, int *g, int *b)
{
    return NCURSES_SP_NAME(extended_color_content) (CURRENT_SCREEN,
						    color,
						    r, g, b);
}

NCURSES_EXPORT(int)
extended_pair_content(int pair, int *f, int *b)
{
    return NCURSES_SP_NAME(extended_pair_content) (CURRENT_SCREEN, pair, f, b);
}

NCURSES_EXPORT(void)
reset_color_pairs(void)
{
    NCURSES_SP_NAME(reset_color_pairs) (CURRENT_SCREEN);
}
#endif /* NCURSES_SP_FUNCS */
#endif /* NCURSES_EXT_COLORS */
