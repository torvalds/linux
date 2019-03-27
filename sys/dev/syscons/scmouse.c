/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_syscons.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/consio.h>
#include <sys/fbio.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mouse.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/signalvar.h>
#include <sys/tty.h>

#include <dev/syscons/syscons.h>

#ifdef SC_TWOBUTTON_MOUSE
#define SC_MOUSE_PASTEBUTTON	MOUSE_BUTTON3DOWN	/* right button */
#define SC_MOUSE_EXTENDBUTTON	MOUSE_BUTTON2DOWN	/* not really used */
#else
#define SC_MOUSE_PASTEBUTTON	MOUSE_BUTTON2DOWN	/* middle button */
#define SC_MOUSE_EXTENDBUTTON	MOUSE_BUTTON3DOWN	/* right button */
#endif /* SC_TWOBUTTON_MOUSE */

#define SC_WAKEUP_DELTA		20

/* for backward compatibility */
#define OLD_CONS_MOUSECTL	_IOWR('c', 10, old_mouse_info_t)

typedef struct old_mouse_data {
    int x;
    int y;
    int buttons;
} old_mouse_data_t;

typedef struct old_mouse_info {
    int operation;
    union {
	struct old_mouse_data data;
	struct mouse_mode mode;
    } u;
} old_mouse_info_t;

#ifndef SC_NO_SYSMOUSE

/* local variables */
#ifndef SC_NO_CUTPASTE
static int		cut_buffer_size;
static u_char		*cut_buffer;
#endif

/* local functions */
static void set_mouse_pos(scr_stat *scp);
#ifndef SC_NO_CUTPASTE
static int skip_spc_right(scr_stat *scp, int p);
static int skip_spc_left(scr_stat *scp, int p);
static void mouse_cut(scr_stat *scp);
static void mouse_cut_start(scr_stat *scp);
static void mouse_cut_end(scr_stat *scp);
static void mouse_cut_word(scr_stat *scp);
static void mouse_cut_line(scr_stat *scp);
static void mouse_cut_extend(scr_stat *scp);
#endif /* SC_NO_CUTPASTE */

#ifndef SC_NO_CUTPASTE
/* allocate a cut buffer */
void
sc_alloc_cut_buffer(scr_stat *scp, int wait)
{
    u_char *p;

    if ((cut_buffer == NULL)
	|| (cut_buffer_size < scp->xsize * scp->ysize + 1)) {
	p = cut_buffer;
	cut_buffer = NULL;
	if (p != NULL)
	    free(p, M_DEVBUF);
	cut_buffer_size = scp->xsize * scp->ysize + 1;
	p = (u_char *)malloc(cut_buffer_size, 
			     M_DEVBUF, (wait) ? M_WAITOK : M_NOWAIT);
	if (p != NULL)
	    p[0] = '\0';
	cut_buffer = p;
    }
}
#endif /* SC_NO_CUTPASTE */

static void
sc_mouse_input_button(scr_stat *scp, int button)
{
	char mouseb[6] = "\x1B[M";

	mouseb[3] = ' ' + button;
	mouseb[4] = '!' + scp->mouse_pos % scp->xsize;
	mouseb[5] = '!' + scp->mouse_pos / scp->xsize;
	sc_respond(scp, mouseb, sizeof mouseb, 1);
}

static void
sc_mouse_input(scr_stat *scp, mouse_info_t *mouse)
{

	switch (mouse->operation) {
	case MOUSE_BUTTON_EVENT:
		if (mouse->u.event.value > 0) {
			/* Mouse button pressed. */
			if (mouse->u.event.id & MOUSE_BUTTON1DOWN)
				sc_mouse_input_button(scp, 0);
			if (mouse->u.event.id & MOUSE_BUTTON2DOWN)
				sc_mouse_input_button(scp, 1);
			if (mouse->u.event.id & MOUSE_BUTTON3DOWN)
				sc_mouse_input_button(scp, 2);
		} else {
			/* Mouse button released. */
			sc_mouse_input_button(scp, 3);
		}
		break;
	case MOUSE_MOTION_EVENT:
		if (mouse->u.data.z < 0) {
			/* Scroll up. */
			sc_mouse_input_button(scp, 64);
		} else if (mouse->u.data.z > 0) {
			/* Scroll down. */
			sc_mouse_input_button(scp, 65);
		}
		break;
	}
}

/* move mouse */
void
sc_mouse_move(scr_stat *scp, int x, int y)
{
    int s;

    s = spltty();
    scp->mouse_xpos = scp->mouse_oldxpos = x;
    scp->mouse_ypos = scp->mouse_oldypos = y;
    if (scp->font_size <= 0 || scp->font_width <= 0)
	scp->mouse_pos = scp->mouse_oldpos = 0;
    else
	scp->mouse_pos = scp->mouse_oldpos = 
	    (y/scp->font_size - scp->yoff)*scp->xsize + x/scp->font_width -
	    scp->xoff;
    scp->status |= MOUSE_MOVED;
    splx(s);
}

/* adjust mouse position */
static void
set_mouse_pos(scr_stat *scp)
{
    if (scp->mouse_xpos < scp->xoff*scp->font_width)
	scp->mouse_xpos = scp->xoff*scp->font_width;
    if (scp->mouse_ypos < scp->yoff*scp->font_size)
	scp->mouse_ypos = scp->yoff*scp->font_size;
    if (ISGRAPHSC(scp)) {
        if (scp->mouse_xpos > scp->xpixel-1)
	    scp->mouse_xpos = scp->xpixel-1;
        if (scp->mouse_ypos > scp->ypixel-1)
	    scp->mouse_ypos = scp->ypixel-1;
	return;
    } else {
	if (scp->mouse_xpos > (scp->xsize + scp->xoff)*scp->font_width - 1)
	    scp->mouse_xpos = (scp->xsize + scp->xoff)*scp->font_width - 1;
	if (scp->mouse_ypos > (scp->ysize + scp->yoff)*scp->font_size - 1)
	    scp->mouse_ypos = (scp->ysize + scp->yoff)*scp->font_size - 1;
    }

    if ((scp->mouse_xpos != scp->mouse_oldxpos || scp->mouse_ypos != scp->mouse_oldypos)
	&& (scp->font_size != 0 && scp->font_width != 0)) {
	scp->status |= MOUSE_MOVED;
    	scp->mouse_pos =
	    (scp->mouse_ypos/scp->font_size - scp->yoff)*scp->xsize 
		+ scp->mouse_xpos/scp->font_width - scp->xoff;
#ifndef SC_NO_CUTPASTE
	if ((scp->status & MOUSE_VISIBLE) && (scp->status & MOUSE_CUTTING))
	    mouse_cut(scp);
#endif
    }
}

#ifndef SC_NO_CUTPASTE

void
sc_draw_mouse_image(scr_stat *scp)
{
    if (ISGRAPHSC(scp))
	return;

    SC_VIDEO_LOCK(scp->sc);
    (*scp->rndr->draw_mouse)(scp, scp->mouse_xpos, scp->mouse_ypos, TRUE);
    scp->mouse_oldpos = scp->mouse_pos;
    scp->mouse_oldxpos = scp->mouse_xpos;
    scp->mouse_oldypos = scp->mouse_ypos;
    scp->status |= MOUSE_VISIBLE;
    SC_VIDEO_UNLOCK(scp->sc);
}

void
sc_remove_mouse_image(scr_stat *scp)
{
    int cols, i, rows;

    if (ISGRAPHSC(scp))
	return;

    SC_VIDEO_LOCK(scp->sc);
    (*scp->rndr->draw_mouse)(scp, scp->mouse_oldxpos, scp->mouse_oldypos,
			     FALSE);
    /*
     * To simplify the renderer and ensure undrawing with correct
     * attributes, mark for update a region containing the cursor
     * (usually 2x2 character cells joined by almost a full line o
     * character cells).
     *
     * The renderer should only undraw any pixels outside of the text
     * window (e.g., ones in borders and hardware cursors).
     */
    i = scp->mouse_oldpos;
    mark_for_update(scp, i);
    mark_for_update(scp, i);
    cols = 1 + howmany(10 - 1, scp->font_width); /* up to VGA cursor width 9 */
    cols = imax(cols, 2);	/* in case it is text mode 2x2 char cells */
    cols = imin(cols, scp->xsize - i % scp->xsize);
    rows = 1 + howmany(16 - 1, scp->font_size);	/* up to VGA cursor height 16 */
    rows = imax(rows, 2);	/* don't bother reducing 3 to 2 if text */
    rows = imin(rows, scp->ysize - i / scp->xsize);
    mark_for_update(scp, i + (rows - 1) * scp->xsize + cols - 1);
    scp->status &= ~MOUSE_VISIBLE;
    SC_VIDEO_UNLOCK(scp->sc);
}

int
sc_inside_cutmark(scr_stat *scp, int pos)
{
    int start;
    int end;

    if (scp->mouse_cut_end < 0)
	return FALSE;
    if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	start = scp->mouse_cut_start;
	end = scp->mouse_cut_end;
    } else {
	start = scp->mouse_cut_end;
	end = scp->mouse_cut_start - 1;
    }
    return ((start <= pos) && (pos <= end));
}

void
sc_remove_cutmarking(scr_stat *scp)
{
    int s;

    s = spltty();
    if (scp->mouse_cut_end >= 0) {
	mark_for_update(scp, scp->mouse_cut_start);
	mark_for_update(scp, scp->mouse_cut_end);
    }
    scp->mouse_cut_start = scp->xsize*scp->ysize;
    scp->mouse_cut_end = -1;
    splx(s);
    scp->status &= ~MOUSE_CUTTING;
}

void
sc_remove_all_cutmarkings(sc_softc_t *sc)
{
    scr_stat *scp;
    int i;

    /* delete cut markings in all vtys */
    for (i = 0; i < sc->vtys; ++i) {
	scp = SC_STAT(sc->dev[i]);
	if (scp == NULL)
	    continue;
	sc_remove_cutmarking(scp);
    }
}

void
sc_remove_all_mouse(sc_softc_t *sc)
{
    scr_stat *scp;
    int i;

    for (i = 0; i < sc->vtys; ++i) {
	scp = SC_STAT(sc->dev[i]);
	if (scp == NULL)
	    continue;
	if (scp->status & MOUSE_VISIBLE) {
	    scp->status &= ~MOUSE_VISIBLE;
	    mark_all(scp);
	}
    }
}

#define IS_SPACE_CHAR(c)	(((c) & 0xff) == ' ')

#ifdef SC_CUT_SPACES2TABS
#define IS_BLANK_CHAR(c)	(((c) & 0xff) == ' ' || ((c) & 0xff) == '\t')
#else
#define IS_BLANK_CHAR(c)	IS_SPACE_CHAR(c)
#endif /* SC_CUT_SPACES2TABS */

#ifdef SC_CUT_SEPCHARS
#define IS_SEP_CHAR(c)		(index(SC_CUT_SEPCHARS, (c) & 0xff) != NULL)
#else
#define IS_SEP_CHAR(c)		IS_SPACE_CHAR(c)
#endif /* SC_CUT_SEPCHARS */

/* skip spaces to right */
static int
skip_spc_right(scr_stat *scp, int p)
{
    int c;
    int i;

    for (i = p % scp->xsize; i < scp->xsize; ++i) {
	c = sc_vtb_getc(&scp->vtb, p);
	if (!IS_SPACE_CHAR(c))
	    break;
	++p;
    }
    return i;
}

/* skip spaces to left */
static int
skip_spc_left(scr_stat *scp, int p)
{
    int c;
    int i;

    for (i = p-- % scp->xsize - 1; i >= 0; --i) {
	c = sc_vtb_getc(&scp->vtb, p);
	if (!IS_SPACE_CHAR(c))
	    break;
	--p;
    }
    return i;
}

static void
mouse_do_cut(scr_stat *scp, int from, int to)
{
    int blank;
    int i;
    int leadspaces;
    int p;
    int s;

    for (p = from, i = blank = leadspaces = 0; p <= to; ++p) {
	cut_buffer[i] = sc_vtb_getc(&scp->vtb, p);
	/* Be prepared that sc_vtb_getc() can return '\0' */
	if (cut_buffer[i] == '\0')
	    cut_buffer[i] = ' ';
#ifdef SC_CUT_SPACES2TABS
	if (leadspaces != -1) {
	    if (IS_SPACE_CHAR(cut_buffer[i])) {
		leadspaces++;
		/* Check that we are at tabstop position */
		if ((p % scp->xsize) % 8 == 7) {
		    i -= leadspaces - 1;
		    cut_buffer[i] = '\t';
		    leadspaces = 0;
		}
	    } else {
		leadspaces = -1;
	    }
	}
#endif /* SC_CUT_SPACES2TABS */
	/* remember the position of the last non-space char */
	if (!IS_BLANK_CHAR(cut_buffer[i]))
	    blank = i + 1;	/* the first space after the last non-space */
	++i;
	/* trim trailing blank when crossing lines */
	if ((p % scp->xsize) == (scp->xsize - 1)) {
	    cut_buffer[blank++] = '\r';
	    i = blank;
	    leadspaces = 0;
	}
    }
    cut_buffer[i] = '\0';

    /* remove the current marking */
    s = spltty();
    if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	mark_for_update(scp, scp->mouse_cut_start);
	mark_for_update(scp, scp->mouse_cut_end);
    } else if (scp->mouse_cut_end >= 0) {
	mark_for_update(scp, scp->mouse_cut_end);
	mark_for_update(scp, scp->mouse_cut_start);
    }

    /* mark the new region */
    scp->mouse_cut_start = from;
    scp->mouse_cut_end = to;
    mark_for_update(scp, from);
    mark_for_update(scp, to);
    splx(s);
}

/* copy marked region to the cut buffer */
static void
mouse_cut(scr_stat *scp)
{
    int start;
    int end;
    int from;
    int to;
    int c;
    int p;
    int s;
    int i;

    start = scp->mouse_cut_start;
    end = scp->mouse_cut_end;
    if (scp->mouse_pos >= start) {
	from = start;
	to = end = scp->mouse_pos;
    } else {
	from = end = scp->mouse_pos;
	to = start - 1;
    }
    p = to;
    for (i = p % scp->xsize; i < scp->xsize; ++i) {
	c = sc_vtb_getc(&scp->vtb, p);
	if (!IS_SPACE_CHAR(c))
	    break;
	++p;
    }
    /* if there is nothing but blank chars, trim them, but mark towards eol */
    if (i == scp->xsize) {
	if (end >= start)
	    to = end = p - 1;
	else
	    to = start = p;
    }
    mouse_do_cut(scp, from, to);
    s = spltty();
    scp->mouse_cut_start = start;
    scp->mouse_cut_end = end;
    splx(s);
}

/* a mouse button is pressed, start cut operation */
static void
mouse_cut_start(scr_stat *scp)
{
    int i;
    int s;

    if (scp->status & MOUSE_VISIBLE) {
	sc_remove_all_cutmarkings(scp->sc);
	if ((scp->mouse_pos == scp->mouse_cut_start) &&
	    (scp->mouse_pos == scp->mouse_cut_end)) {
	    cut_buffer[0] = '\0';
	    return;
	} else if (skip_spc_right(scp, scp->mouse_pos) >= scp->xsize) {
	    /* if the pointer is on trailing blank chars, mark towards eol */
	    i = skip_spc_left(scp, scp->mouse_pos) + 1;
	    s = spltty();
	    scp->mouse_cut_start =
	        rounddown(scp->mouse_pos, scp->xsize) + i;
	    scp->mouse_cut_end =
	        (scp->mouse_pos / scp->xsize + 1) * scp->xsize - 1;
	    splx(s);
	    cut_buffer[0] = '\r';
	} else {
	    s = spltty();
	    scp->mouse_cut_start = scp->mouse_pos;
	    scp->mouse_cut_end = scp->mouse_cut_start;
	    splx(s);
	    cut_buffer[0] = sc_vtb_getc(&scp->vtb, scp->mouse_cut_start);
	}
	cut_buffer[1] = '\0';
	scp->status |= MOUSE_CUTTING;
    	mark_all(scp);	/* this is probably overkill XXX */
    }
}

/* end of cut operation */
static void
mouse_cut_end(scr_stat *scp) 
{
    if (scp->status & MOUSE_VISIBLE)
	scp->status &= ~MOUSE_CUTTING;
}

/* copy a word under the mouse pointer */
static void
mouse_cut_word(scr_stat *scp)
{
    int start;
    int end;
    int sol;
    int eol;
    int c;
    int j;
    int len;

    /*
     * Because we don't have locale information in the kernel,
     * we only distinguish space char and non-space chars.  Punctuation
     * chars, symbols and other regular chars are all treated alike
     * unless user specified SC_CUT_SEPCHARS in his kernel config file.
     */
    if (scp->status & MOUSE_VISIBLE) {
	sol = rounddown(scp->mouse_pos, scp->xsize);
	eol = sol + scp->xsize;
	c = sc_vtb_getc(&scp->vtb, scp->mouse_pos);
	if (IS_SEP_CHAR(c)) {
	    /* blank space */
	    for (j = scp->mouse_pos; j >= sol; --j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (!IS_SEP_CHAR(c))
		    break;
	    }
	    start = ++j;
	    for (j = scp->mouse_pos; j < eol; ++j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (!IS_SEP_CHAR(c))
		    break;
	    }
	    end = j - 1;
	} else {
	    /* non-space word */
	    for (j = scp->mouse_pos; j >= sol; --j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (IS_SEP_CHAR(c))
		    break;
	    }
	    start = ++j;
	    for (j = scp->mouse_pos; j < eol; ++j) {
		c = sc_vtb_getc(&scp->vtb, j);
	        if (IS_SEP_CHAR(c))
		    break;
	    }
	    end = j - 1;
	}

	/* copy the found word */
	mouse_do_cut(scp, start, end);
	len = strlen(cut_buffer);
	if (cut_buffer[len - 1] == '\r')
	    cut_buffer[len - 1] = '\0';
    }
}

/* copy a line under the mouse pointer */
static void
mouse_cut_line(scr_stat *scp)
{
    int len;
    int from;

    if (scp->status & MOUSE_VISIBLE) {
	from = rounddown(scp->mouse_pos, scp->xsize);
	mouse_do_cut(scp, from, from + scp->xsize - 1);
	len = strlen(cut_buffer);
	if (cut_buffer[len - 1] == '\r')
	    cut_buffer[len - 1] = '\0';
	scp->status |= MOUSE_CUTTING;
    }
}

/* extend the marked region to the mouse pointer position */
static void
mouse_cut_extend(scr_stat *scp) 
{
    int start;
    int end;
    int s;

    if ((scp->status & MOUSE_VISIBLE) && !(scp->status & MOUSE_CUTTING)
	&& (scp->mouse_cut_end >= 0)) {
	if (scp->mouse_cut_start <= scp->mouse_cut_end) {
	    start = scp->mouse_cut_start;
	    end = scp->mouse_cut_end;
	} else {
	    start = scp->mouse_cut_end;
	    end = scp->mouse_cut_start - 1;
	}
	s = spltty();
	if (scp->mouse_pos > end) {
	    scp->mouse_cut_start = start;
	    scp->mouse_cut_end = end;
	} else if (scp->mouse_pos < start) {
	    scp->mouse_cut_start = end + 1;
	    scp->mouse_cut_end = start;
	} else {
	    if (scp->mouse_pos - start > end + 1 - scp->mouse_pos) {
		scp->mouse_cut_start = start;
		scp->mouse_cut_end = end;
	    } else {
		scp->mouse_cut_start = end + 1;
		scp->mouse_cut_end = start;
	    }
	}
	splx(s);
	mouse_cut(scp);
	scp->status |= MOUSE_CUTTING;
    }
}

/* paste cut buffer contents into the current vty */
void
sc_mouse_paste(scr_stat *scp)
{
    sc_paste(scp, cut_buffer, strlen(cut_buffer));
}

#endif /* SC_NO_CUTPASTE */

int
sc_mouse_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
    mouse_info_t *mouse;
    mouse_info_t buf;
    scr_stat *cur_scp;
    scr_stat *scp;
    struct proc *p1;
    int s;
    int f;

    scp = SC_STAT(tp);

    switch (cmd) {

    case CONS_MOUSECTL:		/* control mouse arrow */
    case OLD_CONS_MOUSECTL:

	mouse = (mouse_info_t*)data;

	random_harvest_queue(mouse, sizeof(mouse_info_t), RANDOM_MOUSE);

	if (cmd == OLD_CONS_MOUSECTL) {
	    static u_char swapb[] = { 0, 4, 2, 6, 1, 5, 3, 7 };
	    old_mouse_info_t *old_mouse = (old_mouse_info_t *)data;

	    mouse = &buf;
	    mouse->operation = old_mouse->operation;
	    switch (mouse->operation) {
	    case MOUSE_MODE:
		mouse->u.mode = old_mouse->u.mode;
		break;
	    case MOUSE_SHOW:
	    case MOUSE_HIDE:
		break;
	    case MOUSE_MOVEABS:
	    case MOUSE_MOVEREL:
	    case MOUSE_ACTION:
		mouse->u.data.x = old_mouse->u.data.x;
		mouse->u.data.y = old_mouse->u.data.y;
		mouse->u.data.z = 0;
		mouse->u.data.buttons = swapb[old_mouse->u.data.buttons & 0x7];
		break;
	    case MOUSE_GETINFO:
		old_mouse->u.data.x = scp->mouse_xpos;
		old_mouse->u.data.y = scp->mouse_ypos;
		old_mouse->u.data.buttons = swapb[scp->mouse_buttons & 0x7];
		return 0;
	    default:
		return EINVAL;
	    }
	}

	cur_scp = scp->sc->cur_scp;

	switch (mouse->operation) {
	case MOUSE_MODE:
	    if (ISSIGVALID(mouse->u.mode.signal)) {
		scp->mouse_signal = mouse->u.mode.signal;
		scp->mouse_proc = td->td_proc;
		scp->mouse_pid = td->td_proc->p_pid;
	    }
	    else {
		scp->mouse_signal = 0;
		scp->mouse_proc = NULL;
		scp->mouse_pid = 0;
	    }
	    return 0;

	case MOUSE_SHOW:
	    s = spltty();
	    if (!(scp->sc->flags & SC_MOUSE_ENABLED)) {
		scp->sc->flags |= SC_MOUSE_ENABLED;
		cur_scp->status &= ~MOUSE_HIDDEN;
		if (!ISGRAPHSC(cur_scp))
		    mark_all(cur_scp);
	    }
	    splx(s);
	    return 0;
	    /* NOTREACHED */

	case MOUSE_HIDE:
	    s = spltty();
	    if (scp->sc->flags & SC_MOUSE_ENABLED) {
		scp->sc->flags &= ~SC_MOUSE_ENABLED;
		sc_remove_all_mouse(scp->sc);
	    }
	    splx(s);
	    return 0;
	    /* NOTREACHED */

	case MOUSE_MOVEABS:
	    s = spltty();
	    scp->mouse_xpos = mouse->u.data.x;
	    scp->mouse_ypos = mouse->u.data.y;
	    set_mouse_pos(scp);
	    splx(s);
	    break;

	case MOUSE_MOVEREL:
	    s = spltty();
	    scp->mouse_xpos += mouse->u.data.x;
	    scp->mouse_ypos += mouse->u.data.y;
	    set_mouse_pos(scp);
	    splx(s);
	    break;

	case MOUSE_GETINFO:
	    mouse->u.data.x = scp->mouse_xpos;
	    mouse->u.data.y = scp->mouse_ypos;
	    mouse->u.data.z = 0;
	    mouse->u.data.buttons = scp->mouse_buttons;
	    return 0;

	case MOUSE_ACTION:
	case MOUSE_MOTION_EVENT:
	    /* send out mouse event on /dev/sysmouse */
#if 0
	    /* this should maybe only be settable from /dev/consolectl SOS */
	    if (SC_VTY(tp->t_dev) != SC_CONSOLECTL)
		return ENOTTY;
#endif
	    s = spltty();
	    if (mouse->u.data.x != 0 || mouse->u.data.y != 0) {
		cur_scp->mouse_xpos += mouse->u.data.x;
		cur_scp->mouse_ypos += mouse->u.data.y;
		set_mouse_pos(cur_scp);
	    }
	    f = 0;
	    if (mouse->operation == MOUSE_ACTION) {
		f = cur_scp->mouse_buttons ^ mouse->u.data.buttons;
		cur_scp->mouse_buttons = mouse->u.data.buttons;
	    }
	    splx(s);

	    if (sysmouse_event(mouse) == 0)
		return 0;

	    /* 
	     * If any buttons are down or the mouse has moved a lot, 
	     * stop the screen saver.
	     */
	    if (((mouse->operation == MOUSE_ACTION) && mouse->u.data.buttons)
		|| (mouse->u.data.x*mouse->u.data.x
			+ mouse->u.data.y*mouse->u.data.y
			>= SC_WAKEUP_DELTA*SC_WAKEUP_DELTA)) {
		sc_touch_scrn_saver();
	    }

	    cur_scp->status &= ~MOUSE_HIDDEN;

	    if (cur_scp->mouse_level > 0) {
	    	sc_mouse_input(scp, mouse);
		break;
	    }

	    if (cur_scp->mouse_signal && cur_scp->mouse_proc) {
    		/* has controlling process died? */
		if (cur_scp->mouse_proc != (p1 = pfind(cur_scp->mouse_pid))) {
		    	cur_scp->mouse_signal = 0;
			cur_scp->mouse_proc = NULL;
			cur_scp->mouse_pid = 0;
			if (p1)
			    PROC_UNLOCK(p1);
		} else {
		    kern_psignal(cur_scp->mouse_proc, cur_scp->mouse_signal);
		    PROC_UNLOCK(cur_scp->mouse_proc);
		    break;
		}
	    }

#ifndef SC_NO_CUTPASTE
	    if (ISGRAPHSC(cur_scp) || (cut_buffer == NULL))
		break;

	    if ((mouse->operation == MOUSE_ACTION) && f) {
		/* process button presses */
		if (cur_scp->mouse_buttons & MOUSE_BUTTON1DOWN)
		    mouse_cut_start(cur_scp);
		else
		    mouse_cut_end(cur_scp);
		if (cur_scp->mouse_buttons & MOUSE_BUTTON2DOWN ||
		    cur_scp->mouse_buttons & MOUSE_BUTTON3DOWN)
		    sc_mouse_paste(cur_scp);
	    }
#endif /* SC_NO_CUTPASTE */
	    break;

	case MOUSE_BUTTON_EVENT:
	    if ((mouse->u.event.id & MOUSE_BUTTONS) == 0)
		return EINVAL;
	    if (mouse->u.event.value < 0)
		return EINVAL;
#if 0
	    /* this should maybe only be settable from /dev/consolectl SOS */
	    if (SC_VTY(tp->t_dev) != SC_CONSOLECTL)
		return ENOTTY;
#endif
	    if (mouse->u.event.value > 0)
		cur_scp->mouse_buttons |= mouse->u.event.id;
	    else
		cur_scp->mouse_buttons &= ~mouse->u.event.id;

	    if (sysmouse_event(mouse) == 0)
		return 0;

	    /* if a button is held down, stop the screen saver */
	    if (mouse->u.event.value > 0)
		sc_touch_scrn_saver();

	    cur_scp->status &= ~MOUSE_HIDDEN;

	    if (cur_scp->mouse_level > 0) {
	    	sc_mouse_input(scp, mouse);
		break;
	    }

	    if (cur_scp->mouse_signal && cur_scp->mouse_proc) {
		if (cur_scp->mouse_proc != (p1 = pfind(cur_scp->mouse_pid))){
		    	cur_scp->mouse_signal = 0;
			cur_scp->mouse_proc = NULL;
			cur_scp->mouse_pid = 0;
			if (p1)
			    PROC_UNLOCK(p1);
		} else {
		    kern_psignal(cur_scp->mouse_proc, cur_scp->mouse_signal);
		    PROC_UNLOCK(cur_scp->mouse_proc);
		    break;
		}
	    }

#ifndef SC_NO_CUTPASTE
	    if (ISGRAPHSC(cur_scp) || (cut_buffer == NULL))
		break;

	    switch (mouse->u.event.id) {
	    case MOUSE_BUTTON1DOWN:
	        switch (mouse->u.event.value % 4) {
		case 0:	/* up */
		    mouse_cut_end(cur_scp);
		    break;
		case 1: /* single click: start cut operation */
		    mouse_cut_start(cur_scp);
		    break;
		case 2:	/* double click: cut a word */
		    mouse_cut_word(cur_scp);
		    mouse_cut_end(cur_scp);
		    break;
		case 3:	/* triple click: cut a line */
		    mouse_cut_line(cur_scp);
		    mouse_cut_end(cur_scp);
		    break;
		}
		break;
	    case SC_MOUSE_PASTEBUTTON:
	        switch (mouse->u.event.value) {
		case 0:	/* up */
		    break;
		default:
		    sc_mouse_paste(cur_scp);
		    break;
		}
		break;
	    case SC_MOUSE_EXTENDBUTTON:
	        switch (mouse->u.event.value) {
		case 0:	/* up */
		    if (!(cur_scp->mouse_buttons & MOUSE_BUTTON1DOWN))
		        mouse_cut_end(cur_scp);
		    break;
		default:
		    mouse_cut_extend(cur_scp);
		    break;
		}
		break;
	    }
#endif /* SC_NO_CUTPASTE */
	    break;

	case MOUSE_MOUSECHAR:
	    if (mouse->u.mouse_char < 0) {
		mouse->u.mouse_char = scp->sc->mouse_char;
	    } else {
		if (mouse->u.mouse_char > UCHAR_MAX - 3)
		    return EINVAL;
		s = spltty();
		sc_remove_all_mouse(scp->sc);
#ifndef SC_NO_FONT_LOADING
		if (ISTEXTSC(cur_scp) && (cur_scp->font != NULL))
		    sc_load_font(cur_scp, 0, cur_scp->font_size,
				 cur_scp->font_width,
				 cur_scp->font + cur_scp->font_size
				 * cur_scp->sc->mouse_char,
				 cur_scp->sc->mouse_char, 4);
#endif
		scp->sc->mouse_char = mouse->u.mouse_char;
		splx(s);
	    }
	    break;

	default:
	    return EINVAL;
	}

	return 0;
    }

    return ENOIOCTL;
}

#endif /* SC_NO_SYSMOUSE */
