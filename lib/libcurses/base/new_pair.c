/****************************************************************************
 * Copyright 2018-2020,2021 Thomas E. Dickey                                *
 * Copyright 2017 Free Software Foundation, Inc.                            *
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
 *  Author: Thomas E. Dickey                                                *
 ****************************************************************************/

/* new_pair.c
 *
 * New color-pair functions, alloc_pair and free_pair
 */

#define NEW_PAIR_INTERNAL 1
#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

#ifdef USE_TERM_DRIVER
#define MaxColors      InfoOf(SP_PARM).maxcolors
#else
#define MaxColors      max_colors
#endif

#if NCURSES_EXT_COLORS

/* fix redefinition versys tic.h */
#undef entry
#define entry my_entry
#undef ENTRY
#define ENTRY my_ENTRY

#include <search.h>

#endif

MODULE_ID("$Id: new_pair.c,v 1.1 2023/10/17 09:52:09 nicm Exp $")

#if NCURSES_EXT_COLORS

#ifdef NEW_PAIR_DEBUG

static int
prev_len(SCREEN *sp, int pair)
{
    int result = 1;
    int base = pair;
    colorpair_t *list = sp->_color_pairs;
    while (list[pair].prev != base) {
	result++;
	pair = list[pair].prev;
    }
    return result;
}

static int
next_len(SCREEN *sp, int pair)
{
    int result = 1;
    int base = pair;
    colorpair_t *list = sp->_color_pairs;
    while (list[pair].next != base) {
	result++;
	pair = list[pair].next;
    }
    return result;
}

/*
 * Trace the contents of LRU color-pairs.
 */
static void
dumpit(SCREEN *sp, int pair, const char *tag)
{
    colorpair_t *list = sp->_color_pairs;
    char bigbuf[256 * 20];
    char *p = bigbuf;
    int n;
    size_t have = sizeof(bigbuf);

    _nc_STRCPY(p, tag, have);
    for (n = 0; n < sp->_pair_alloc; ++n) {
	if (list[n].mode != cpFREE) {
	    p += strlen(p);
	    if ((size_t) (p - bigbuf) + 50 > have)
		break;
	    _nc_SPRINTF(p, _nc_SLIMIT(have - (p - bigbuf))
			" %d%c(%d,%d)",
			n, n == pair ? '@' : ':', list[n].next, list[n].prev);
	}
    }
    T(("(%d/%d) %ld - %s",
       next_len(sp, 0),
       prev_len(sp, 0),
       strlen(bigbuf), bigbuf));

    if (next_len(sp, 0) != prev_len(sp, 0)) {
	endwin();
	ExitProgram(EXIT_FAILURE);
    }
}
#else
#define dumpit(sp, pair, tag)	/* nothing */
#endif

static int
compare_data(const void *a, const void *b)
{
    const colorpair_t *p = (const colorpair_t *) a;
    const colorpair_t *q = (const colorpair_t *) b;
    return ((p->fg == q->fg)
	    ? (p->bg - q->bg)
	    : (p->fg - q->fg));
}

static int
_nc_find_color_pair(SCREEN *sp, int fg, int bg)
{
    colorpair_t find;
    int result = -1;

    find.fg = fg;
    find.bg = bg;
    if (sp != 0) {
	void *pp;
	if ((pp = tfind(&find, &sp->_ordered_pairs, compare_data)) != 0) {
	    colorpair_t *temp = *(colorpair_t **) pp;
	    result = (int) (temp - sp->_color_pairs);
	}
    }
    return result;
}

static void
delink_color_pair(SCREEN *sp, int pair)
{
    colorpair_t *list = sp->_color_pairs;
    int prev = list[pair].prev;
    int next = list[pair].next;

    /* delink this from its current location */
    if (list[prev].next == pair &&
	list[next].prev == pair) {
	list[prev].next = next;
	list[next].prev = prev;
	dumpit(sp, pair, "delinked");
    }
}

/*
 * Discard all nodes in the fast-index.
 */
NCURSES_EXPORT(void)
_nc_free_ordered_pairs(SCREEN *sp)
{
    if (sp && sp->_ordered_pairs && sp->_pair_alloc) {
	int n;
	for (n = 0; n < sp->_pair_alloc; ++n) {
	    tdelete(&sp->_color_pairs[n], &sp->_ordered_pairs, compare_data);
	}
    }
}

/*
 * Use this call to update the fast-index when modifying an entry in the color
 * pair table.
 */
NCURSES_EXPORT(void)
_nc_reset_color_pair(SCREEN *sp, int pair, colorpair_t * next)
{
    colorpair_t *last;

    if (ValidPair(sp, pair)) {
	bool used;

	ReservePairs(sp, pair);
	last = &(sp->_color_pairs[pair]);
	delink_color_pair(sp, pair);
	if (last->mode > cpFREE &&
	    (last->fg != next->fg || last->bg != next->bg)) {
	    /* remove the old entry from fast index */
	    tdelete(last, &sp->_ordered_pairs, compare_data);
	    used = FALSE;
	} else {
	    used = (last->mode != cpFREE);
	}
	if (!used) {
	    /* create a new entry in fast index */
	    *last = *next;
	    tsearch(last, &sp->_ordered_pairs, compare_data);
	}
    }
}

/*
 * Use this call to relink the newest pair to the front of the list, keeping
 * "0" first.
 */
NCURSES_EXPORT(void)
_nc_set_color_pair(SCREEN *sp, int pair, int mode)
{
    if (ValidPair(sp, pair)) {
	colorpair_t *list = sp->_color_pairs;
	dumpit(sp, pair, "SET_PAIR");
	list[0].mode = cpKEEP;
	if (list[pair].mode <= cpFREE)
	    sp->_pairs_used++;
	list[pair].mode = mode;
	if (list[0].next != pair) {
	    /* link it at the front of the list */
	    list[pair].next = list[0].next;
	    list[list[pair].next].prev = pair;
	    list[pair].prev = 0;
	    list[0].next = pair;
	}
	dumpit(sp, pair, "...after");
    }
}

/*
 * If we reallocate the color-pair array, we have to adjust the fast-index.
 */
NCURSES_EXPORT(void)
_nc_copy_pairs(SCREEN *sp, colorpair_t * target, colorpair_t * source, int length)
{
    int n;
    for (n = 0; n < length; ++n) {
	void *find = tfind(source + n, &sp->_ordered_pairs, compare_data);
	if (find != 0) {
	    tdelete(source + n, &sp->_ordered_pairs, compare_data);
	    tsearch(target + n, &sp->_ordered_pairs, compare_data);
	}
    }
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(alloc_pair) (NCURSES_SP_DCLx int fg, int bg)
{
    int pair;

    T((T_CALLED("alloc_pair(%d,%d)"), fg, bg));
    if (SP_PARM == 0) {
	pair = -1;
    } else if ((pair = _nc_find_color_pair(SP_PARM, fg, bg)) < 0) {
	/*
	 * Check if all of the slots have been used.  If not, find one and
	 * use that.
	 */
	if (SP_PARM->_pairs_used + 1 < SP_PARM->_pair_limit) {
	    bool found = FALSE;
	    int hint = SP_PARM->_recent_pair;

	    /*
	     * The linear search is done to allow mixing calls to init_pair()
	     * and alloc_pair().  The former can make gaps...
	     */
	    for (pair = hint + 1; pair < SP_PARM->_pair_alloc; pair++) {
		if (SP_PARM->_color_pairs[pair].mode == cpFREE) {
		    T(("found gap %d", pair));
		    found = TRUE;
		    break;
		}
	    }
	    if (!found && (SP_PARM->_pair_alloc < SP_PARM->_pair_limit)) {
		pair = SP_PARM->_pair_alloc;
		ReservePairs(SP_PARM, pair);
		if (SP_PARM->_color_pairs == 0) {
		    pair = -1;
		} else {
		    found = TRUE;
		}
	    }
	    if (!found && SP_PARM->_color_pairs != NULL) {
		for (pair = 1; pair <= hint; pair++) {
		    if (SP_PARM->_color_pairs[pair].mode == cpFREE) {
			T(("found gap %d", pair));
			found = TRUE;
			break;
		    }
		}
	    }
	    if (found) {
		SP_PARM->_recent_pair = pair;
	    } else {
		pair = ERR;
	    }
	} else {
	    /* reuse the oldest one */
	    pair = SP_PARM->_color_pairs[0].prev;
	    T(("reusing %d", pair));
	}

	if (_nc_init_pair(SP_PARM, pair, fg, bg) == ERR)
	    pair = ERR;
    }
    returnCode(pair);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(find_pair) (NCURSES_SP_DCLx int fg, int bg)
{
    int pair;

    T((T_CALLED("find_pair(%d,%d)"), fg, bg));
    pair = _nc_find_color_pair(SP_PARM, fg, bg);
    returnCode(pair);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(free_pair) (NCURSES_SP_DCLx int pair)
{
    int result = ERR;
    T((T_CALLED("free_pair(%d)"), pair));
    if (ValidPair(SP_PARM, pair) && pair < SP_PARM->_pair_alloc) {
	colorpair_t *cp = &(SP_PARM->_color_pairs[pair]);
	if (pair != 0) {
	    _nc_change_pair(SP_PARM, pair);
	    delink_color_pair(SP_PARM, pair);
	    tdelete(cp, &SP_PARM->_ordered_pairs, compare_data);
	    cp->mode = cpFREE;
	    result = OK;
	    SP_PARM->_pairs_used--;
	}
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
alloc_pair(int f, int b)
{
    return NCURSES_SP_NAME(alloc_pair) (CURRENT_SCREEN, f, b);
}

NCURSES_EXPORT(int)
find_pair(int f, int b)
{
    return NCURSES_SP_NAME(find_pair) (CURRENT_SCREEN, f, b);
}

NCURSES_EXPORT(int)
free_pair(int pair)
{
    return NCURSES_SP_NAME(free_pair) (CURRENT_SCREEN, pair);
}
#endif

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_new_pair_leaks(SCREEN *sp)
{
    if (sp->_color_pairs) {
	while (sp->_color_pairs[0].next) {
	    free_pair(sp->_color_pairs[0].next);
	}
    }
}
#endif

#else
void _nc_new_pair(void);
void
_nc_new_pair(void)
{
}
#endif /* NCURSES_EXT_COLORS */
