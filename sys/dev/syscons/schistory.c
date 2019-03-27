/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999 Kazutaka YOKOTA <yokota@zodiac.mech.utsunomiya-u.ac.jp>
 * Copyright (c) 1992-1998 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifndef SC_NO_HISTORY

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/consio.h>
#include <sys/tty.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#if defined(__arm__) || defined(__mips__) || \
	defined(__powerpc__) || defined(__sparc64__)
#include <machine/sc_machdep.h>
#else
#include <machine/pc/display.h>
#endif

#include <dev/syscons/syscons.h>

/*
 * XXX Placeholder.
 * This calculations should be dynamically scaled by number of separate sc
 * devices.  A base value of 'extra_history_size' should be defined for
 * each syscons unit, and added and subtracted from the dynamic
 * 'extra_history_size' as units are added and removed.  This way, each time
 * a new syscons unit goes online, extra_history_size is automatically bumped.
 */
#define	MAXSC	1

#if !defined(SC_MAX_HISTORY_SIZE)
#define SC_MAX_HISTORY_SIZE	(1000 * MAXCONS * MAXSC)
#endif

#if !defined(SC_HISTORY_SIZE)
#define SC_HISTORY_SIZE		(ROW * 4)
#endif

#if (SC_HISTORY_SIZE * MAXCONS * MAXSC) > SC_MAX_HISTORY_SIZE
#undef SC_MAX_HISTORY_SIZE
#define SC_MAX_HISTORY_SIZE	(SC_HISTORY_SIZE * MAXCONS * MAXSC)
#endif

/* local variables */
static int		extra_history_size
				= SC_MAX_HISTORY_SIZE - SC_HISTORY_SIZE*MAXCONS;

/* local functions */
static void copy_history(sc_vtb_t *from, sc_vtb_t *to);
static void history_to_screen(scr_stat *scp);

/* allocate a history buffer */
int
sc_alloc_history_buffer(scr_stat *scp, int lines, int prev_ysize, int wait)
{
	/*
	 * syscons unconditionally allocates buffers up to 
	 * SC_HISTORY_SIZE lines or scp->ysize lines, whichever 
	 * is larger. A value greater than that is allowed, 
	 * subject to extra_history_size.
	 */
	sc_vtb_t *history;
	sc_vtb_t *prev_history;
	int cur_lines;				/* current buffer size */
	int min_lines;				/* guaranteed buffer size */
	int delta;				/* lines to put back */

	if (lines <= 0)
		lines = SC_HISTORY_SIZE;	/* use the default value */

	/* make it at least as large as the screen size */
	lines = imax(lines, scp->ysize);

	/* remove the history buffer while we update it */
	history = prev_history = scp->history;
	scp->history = NULL;

	/* calculate the amount of lines to put back to extra_history_size */
	delta = 0;
	if (prev_history) {
		cur_lines = sc_vtb_rows(history);
		min_lines = imax(SC_HISTORY_SIZE, prev_ysize);
		if (cur_lines > min_lines)
			delta = cur_lines - min_lines;
	}

	/* lines up to min_lines are always allowed. */
	min_lines = imax(SC_HISTORY_SIZE, scp->ysize);
	if (lines > min_lines) {
		if (lines - min_lines > extra_history_size + delta) {
			/* too many lines are requested */
			scp->history = prev_history;
			return EINVAL;
		}
	}

	/* allocate a new buffer */
	history = (sc_vtb_t *)malloc(sizeof(*history),
				     M_DEVBUF,
				     (wait) ? M_WAITOK : M_NOWAIT);
	if (history != NULL) {
		if (lines > min_lines)
			extra_history_size -= lines - min_lines;
		/* XXX error check? */
		sc_vtb_init(history, VTB_RINGBUFFER, scp->xsize, lines,
			    NULL, wait);
		/* FIXME: XXX no good? */
		sc_vtb_clear(history, scp->sc->scr_map[0x20],
			     SC_NORM_ATTR << 8);
		if (prev_history != NULL)
			copy_history(prev_history, history);
		scp->history_pos = sc_vtb_tail(history);
	} else {
		scp->history_pos = 0;
	}

	/* destroy the previous buffer */
	if (prev_history != NULL) {
		extra_history_size += delta;
		sc_vtb_destroy(prev_history);
		free(prev_history, M_DEVBUF);
	}

	scp->history = history;

	return 0;
}

static void
copy_history(sc_vtb_t *from, sc_vtb_t *to)
{
	int lines;
	int cols;
	int cols1;
	int cols2;
	int pos;
	int i;

	lines = sc_vtb_rows(from);
	cols1 = sc_vtb_cols(from);
	cols2 = sc_vtb_cols(to);
	cols = imin(cols1, cols2);
	pos = sc_vtb_tail(from);
	for (i = 0; i < lines; ++i) {
		sc_vtb_append(from, pos, to, cols);
		if (cols < cols2)
			sc_vtb_seek(to, sc_vtb_pos(to, 
						   sc_vtb_tail(to), 
						   cols2 - cols));
		pos = sc_vtb_pos(from, pos, cols1);
	}
}

void
sc_free_history_buffer(scr_stat *scp, int prev_ysize)
{
	sc_vtb_t *history;
	int cur_lines;				/* current buffer size */
	int min_lines;				/* guaranteed buffer size */

	history = scp->history;
	scp->history = NULL;
	if (history == NULL)
		return;

	cur_lines = sc_vtb_rows(history);
	min_lines = imax(SC_HISTORY_SIZE, prev_ysize);
	extra_history_size += (cur_lines > min_lines) ? 
				  cur_lines - min_lines : 0;

	sc_vtb_destroy(history);
	free(history, M_DEVBUF);
}

/* copy entire screen into the top of the history buffer */
void
sc_hist_save(scr_stat *scp)
{
	sc_vtb_append(&scp->vtb, 0, scp->history, scp->xsize*scp->ysize);
	scp->history_pos = sc_vtb_tail(scp->history);
}

/* restore the screen by copying from the history buffer */
int
sc_hist_restore(scr_stat *scp)
{
	int ret;

	if (scp->history_pos != sc_vtb_tail(scp->history)) {
		scp->history_pos = sc_vtb_tail(scp->history);
		history_to_screen(scp);
		ret =  0;
	} else {
		ret = 1;
	}
	sc_vtb_seek(scp->history, sc_vtb_pos(scp->history, 
					     sc_vtb_tail(scp->history),
					     -scp->xsize*scp->ysize));
	return ret;
}

/* copy screen-full of saved lines */
static void
history_to_screen(scr_stat *scp)
{
	int pos;
	int i;

	pos = scp->history_pos;
	for (i = 1; i <= scp->ysize; ++i) {
		pos = sc_vtb_pos(scp->history, pos, -scp->xsize);
		sc_vtb_copy(scp->history, pos,
			    &scp->vtb, scp->xsize*(scp->ysize - i),
			    scp->xsize);
	}
	mark_all(scp);
}

/* go to the tail of the history buffer */
void
sc_hist_home(scr_stat *scp)
{
	scp->history_pos = sc_vtb_tail(scp->history);
	history_to_screen(scp);
}

/* go to the top of the history buffer */
void
sc_hist_end(scr_stat *scp)
{
	scp->history_pos = sc_vtb_pos(scp->history, sc_vtb_tail(scp->history),
				      scp->xsize*scp->ysize);
	history_to_screen(scp);
}

/* move one line up */
int
sc_hist_up_line(scr_stat *scp)
{
	if (sc_vtb_pos(scp->history, scp->history_pos, -(scp->xsize*scp->ysize))
	    == sc_vtb_tail(scp->history))
		return -1;
	scp->history_pos = sc_vtb_pos(scp->history, scp->history_pos,
				      -scp->xsize);
	history_to_screen(scp);
	return 0;
}

/* move one line down */
int
sc_hist_down_line(scr_stat *scp)
{
	if (scp->history_pos == sc_vtb_tail(scp->history))
		return -1;
	scp->history_pos = sc_vtb_pos(scp->history, scp->history_pos,
				      scp->xsize);
	history_to_screen(scp);
	return 0;
}

int
sc_hist_ioctl(struct tty *tp, u_long cmd, caddr_t data, struct thread *td)
{
	scr_stat *scp;
	int error;

	switch (cmd) {

	case CONS_HISTORY:  	/* set history size */
		scp = SC_STAT(tp);
		if (*(int *)data <= 0)
			return EINVAL;
		if (scp->status & BUFFER_SAVED)
			return EBUSY;
		DPRINTF(5, ("lines:%d, ysize:%d, pool:%d\n",
			    *(int *)data, scp->ysize, extra_history_size));
		error = sc_alloc_history_buffer(scp, 
					       imax(*(int *)data, scp->ysize),
					       scp->ysize, TRUE);
		DPRINTF(5, ("error:%d, rows:%d, pool:%d\n", error,
			    sc_vtb_rows(scp->history), extra_history_size));
		return error;

	case CONS_CLRHIST:
		scp = SC_STAT(tp);
		sc_vtb_clear(scp->history, scp->sc->scr_map[0x20],
		    SC_NORM_ATTR << 8);
		return 0;
	}

	return ENOIOCTL;
}

#endif /* SC_NO_HISTORY */
