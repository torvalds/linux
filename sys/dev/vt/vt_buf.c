/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Ed Schouten under sponsorship from the
 * FreeBSD Foundation.
 *
 * Portions of this software were developed by Oleksandr Rybalko
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/reboot.h>
#include <sys/systm.h>

#include <dev/vt/vt.h>

static MALLOC_DEFINE(M_VTBUF, "vtbuf", "vt buffer");

#define	VTBUF_LOCK(vb)		mtx_lock_spin(&(vb)->vb_lock)
#define	VTBUF_UNLOCK(vb)	mtx_unlock_spin(&(vb)->vb_lock)

#define POS_INDEX(c, r) (((r) << 12) + (c))
#define	POS_COPY(d, s)	do {	\
	(d).tp_col = (s).tp_col;	\
	(d).tp_row = (s).tp_row;	\
} while (0)

#ifndef SC_NO_CUTPASTE
static int vtbuf_htw(const struct vt_buf *vb, int row);
static int vtbuf_wth(const struct vt_buf *vb, int row);
static int vtbuf_in_this_range(int begin, int test, int end, int sz);
#endif

/*
 * line4
 * line5 <--- curroffset (terminal output to that line)
 * line0
 * line1                  <--- roffset (history display from that point)
 * line2
 * line3
 */
int
vthistory_seek(struct vt_buf *vb, int offset, int whence)
{
	int diff, top, bottom, roffset;

	/* No scrolling if not enabled. */
	if ((vb->vb_flags & VBF_SCROLL) == 0) {
		if (vb->vb_roffset != vb->vb_curroffset) {
			vb->vb_roffset = vb->vb_curroffset;
			return (0xffff);
		}
		return (0); /* No changes */
	}

	/* "top" may be a negative integer. */
	bottom = vb->vb_curroffset;
	top = (vb->vb_flags & VBF_HISTORY_FULL) ?
	    bottom + vb->vb_scr_size.tp_row - vb->vb_history_size :
	    0;

	roffset = 0; /* Make gcc happy. */
	switch (whence) {
	case VHS_SET:
		if (offset < 0)
			offset = 0;
		roffset = top + offset;
		break;
	case VHS_CUR:
		/*
		 * Operate on copy of offset value, since it temporary
		 * can be bigger than amount of rows in buffer.
		 */
		roffset = vb->vb_roffset;
		if (roffset >= bottom + vb->vb_scr_size.tp_row)
			roffset -= vb->vb_history_size;

		roffset += offset;
		roffset = MAX(roffset, top);
		roffset = MIN(roffset, bottom);

		if (roffset < 0)
			roffset = vb->vb_history_size + roffset;

		break;
	case VHS_END:
		/* Go to current offset. */
		roffset = vb->vb_curroffset;
		break;
	}

	diff = vb->vb_roffset != roffset;
	vb->vb_roffset = roffset;

	return (diff);
}

void
vthistory_addlines(struct vt_buf *vb, int offset)
{
#ifndef SC_NO_CUTPASTE
	int cur, sz;
#endif

	vb->vb_curroffset += offset;
	if (vb->vb_curroffset + vb->vb_scr_size.tp_row >= vb->vb_history_size) {
		vb->vb_flags |= VBF_HISTORY_FULL;
		vb->vb_curroffset %= vb->vb_history_size;
	}
	if ((vb->vb_flags & VBF_SCROLL) == 0) {
		vb->vb_roffset = vb->vb_curroffset;
	}

#ifndef SC_NO_CUTPASTE
	sz = vb->vb_history_size;
	cur = vb->vb_roffset + vb->vb_scr_size.tp_row + sz - 1;
	if (vtbuf_in_this_range(cur, vb->vb_mark_start.tp_row, cur + offset, sz) ||
	    vtbuf_in_this_range(cur, vb->vb_mark_end.tp_row, cur + offset, sz)) {
		/* clear screen selection */
		vb->vb_mark_start.tp_row = vb->vb_mark_end.tp_row;
		vb->vb_mark_start.tp_col = vb->vb_mark_end.tp_col;
	}
#endif
}

void
vthistory_getpos(const struct vt_buf *vb, unsigned int *offset)
{

	*offset = vb->vb_roffset;
}

#ifndef SC_NO_CUTPASTE	/* Only mouse support use it now. */
/* Translate history row to current view row number. */
static int
vtbuf_htw(const struct vt_buf *vb, int row)
{

	/*
	 * total 1000 rows.
	 * History offset	roffset	winrow
	 *	205		200	((205 - 200 + 1000) % 1000) = 5
	 *	90		990	((90 - 990 + 1000) % 1000) = 100
	 */
	return ((row - vb->vb_roffset + vb->vb_history_size) %
	    vb->vb_history_size);
}

/* Translate current view row number to history row. */
static int
vtbuf_wth(const struct vt_buf *vb, int row)
{

	return ((vb->vb_roffset + row) % vb->vb_history_size);
}

/*
 * Test if an index in a circular buffer is within a range.
 *
 * begin - start index
 * end - end index
 * test - test index
 * sz - size of circular buffer when it turns over
 */
static int
vtbuf_in_this_range(int begin, int test, int end, int sz)
{

	begin %= sz;
	end %= sz;

	/* check for inversion */
	if (begin > end)
		return (test >= begin || test < end);
	else
		return (test >= begin && test < end);
}
#endif

int
vtbuf_iscursor(const struct vt_buf *vb, int row, int col)
{
#ifndef SC_NO_CUTPASTE
	int sc, sr, sz, ec, er, tmp;
#endif

	if ((vb->vb_flags & (VBF_CURSOR|VBF_SCROLL)) == VBF_CURSOR &&
	    (vb->vb_cursor.tp_row == row) && (vb->vb_cursor.tp_col == col))
		return (1);

#ifndef SC_NO_CUTPASTE
	/* Mark cut/paste region. */
	if (vb->vb_mark_start.tp_col == vb->vb_mark_end.tp_col &&
	    vb->vb_mark_start.tp_row == vb->vb_mark_end.tp_row)
		return (0);

	sc = vb->vb_mark_start.tp_col;
	sr = vb->vb_mark_start.tp_row;
	ec = vb->vb_mark_end.tp_col;
	er = vb->vb_mark_end.tp_row;

	/*
	 * Information about if the selection was made bottom-top or
	 * top-bottom is lost due to modulo arithmetics and needs to
	 * be recovered:
	 */
	sz = vb->vb_history_size;
	tmp = (sz + er - sr) % sz;
	row = vtbuf_wth(vb, row);

	/* Swap start and end if start > end */
	if ((2 * tmp) > sz || (tmp == 0 && sc > ec)) {
		tmp = sc; sc = ec; ec = tmp;
		tmp = sr; sr = er; er = tmp;
	}

	if (vtbuf_in_this_range(POS_INDEX(sc, sr), POS_INDEX(col, row),
	    POS_INDEX(ec, er), POS_INDEX(0, sz)))
		return (1);
#endif

	return (0);
}

void
vtbuf_lock(struct vt_buf *vb)
{

	VTBUF_LOCK(vb);
}

void
vtbuf_unlock(struct vt_buf *vb)
{

	VTBUF_UNLOCK(vb);
}

void
vtbuf_dirty(struct vt_buf *vb, const term_rect_t *area)
{

	if (vb->vb_dirtyrect.tr_begin.tp_row > area->tr_begin.tp_row)
		vb->vb_dirtyrect.tr_begin.tp_row = area->tr_begin.tp_row;
	if (vb->vb_dirtyrect.tr_begin.tp_col > area->tr_begin.tp_col)
		vb->vb_dirtyrect.tr_begin.tp_col = area->tr_begin.tp_col;
	if (vb->vb_dirtyrect.tr_end.tp_row < area->tr_end.tp_row)
		vb->vb_dirtyrect.tr_end.tp_row = area->tr_end.tp_row;
	if (vb->vb_dirtyrect.tr_end.tp_col < area->tr_end.tp_col)
		vb->vb_dirtyrect.tr_end.tp_col = area->tr_end.tp_col;
}

static inline void
vtbuf_dirty_cell(struct vt_buf *vb, const term_pos_t *p)
{
	term_rect_t area;

	area.tr_begin = *p;
	area.tr_end.tp_row = p->tp_row + 1;
	area.tr_end.tp_col = p->tp_col + 1;
	vtbuf_dirty(vb, &area);
}

static void
vtbuf_make_undirty(struct vt_buf *vb)
{

	vb->vb_dirtyrect.tr_begin = vb->vb_scr_size;
	vb->vb_dirtyrect.tr_end.tp_row = vb->vb_dirtyrect.tr_end.tp_col = 0;
}

void
vtbuf_undirty(struct vt_buf *vb, term_rect_t *r)
{

	*r = vb->vb_dirtyrect;
	vtbuf_make_undirty(vb);
}

void
vtbuf_copy(struct vt_buf *vb, const term_rect_t *r, const term_pos_t *p2)
{
	const term_pos_t *p1 = &r->tr_begin;
	term_rect_t area;
	unsigned int rows, cols;
	int pr, rdiff;

	KASSERT(r->tr_begin.tp_row < vb->vb_scr_size.tp_row,
	    ("vtbuf_copy begin.tp_row %d must be less than screen width %d",
		r->tr_begin.tp_row, vb->vb_scr_size.tp_row));
	KASSERT(r->tr_begin.tp_col < vb->vb_scr_size.tp_col,
	    ("vtbuf_copy begin.tp_col %d must be less than screen height %d",
		r->tr_begin.tp_col, vb->vb_scr_size.tp_col));

	KASSERT(r->tr_end.tp_row <= vb->vb_scr_size.tp_row,
	    ("vtbuf_copy end.tp_row %d must be less than screen width %d",
		r->tr_end.tp_row, vb->vb_scr_size.tp_row));
	KASSERT(r->tr_end.tp_col <= vb->vb_scr_size.tp_col,
	    ("vtbuf_copy end.tp_col %d must be less than screen height %d",
		r->tr_end.tp_col, vb->vb_scr_size.tp_col));

	KASSERT(p2->tp_row < vb->vb_scr_size.tp_row,
	    ("vtbuf_copy tp_row %d must be less than screen width %d",
		p2->tp_row, vb->vb_scr_size.tp_row));
	KASSERT(p2->tp_col < vb->vb_scr_size.tp_col,
	    ("vtbuf_copy tp_col %d must be less than screen height %d",
		p2->tp_col, vb->vb_scr_size.tp_col));

	rows = r->tr_end.tp_row - r->tr_begin.tp_row;
	rdiff = r->tr_begin.tp_row - p2->tp_row;
	cols = r->tr_end.tp_col - r->tr_begin.tp_col;
	if (r->tr_begin.tp_row > p2->tp_row && r->tr_begin.tp_col == 0 &&
	    r->tr_end.tp_col == vb->vb_scr_size.tp_col && /* Full row. */
	    (rows + rdiff) == vb->vb_scr_size.tp_row && /* Whole screen. */
	    rdiff > 0) { /* Only forward direction. Do not eat history. */
		vthistory_addlines(vb, rdiff);
	} else if (p2->tp_row < p1->tp_row) {
		/* Handle overlapping copies of line segments. */
		/* Move data up. */
		for (pr = 0; pr < rows; pr++)
			memmove(
			    &VTBUF_FIELD(vb, p2->tp_row + pr, p2->tp_col),
			    &VTBUF_FIELD(vb, p1->tp_row + pr, p1->tp_col),
			    cols * sizeof(term_char_t));
	} else {
		/* Move data down. */
		for (pr = rows - 1; pr >= 0; pr--)
			memmove(
			    &VTBUF_FIELD(vb, p2->tp_row + pr, p2->tp_col),
			    &VTBUF_FIELD(vb, p1->tp_row + pr, p1->tp_col),
			    cols * sizeof(term_char_t));
	}

	area.tr_begin = *p2;
	area.tr_end.tp_row = MIN(p2->tp_row + rows, vb->vb_scr_size.tp_row);
	area.tr_end.tp_col = MIN(p2->tp_col + cols, vb->vb_scr_size.tp_col);
	vtbuf_dirty(vb, &area);
}

static void
vtbuf_do_fill(struct vt_buf *vb, const term_rect_t *r, term_char_t c)
{
	unsigned int pr, pc;
	term_char_t *row;

	for (pr = r->tr_begin.tp_row; pr < r->tr_end.tp_row; pr++) {
		row = vb->vb_rows[(vb->vb_curroffset + pr) %
		    VTBUF_MAX_HEIGHT(vb)];
		for (pc = r->tr_begin.tp_col; pc < r->tr_end.tp_col; pc++) {
			row[pc] = c;
		}
	}
}

void
vtbuf_fill(struct vt_buf *vb, const term_rect_t *r, term_char_t c)
{

	KASSERT(r->tr_begin.tp_row < vb->vb_scr_size.tp_row,
	    ("vtbuf_fill begin.tp_row %d must be < screen height %d",
		r->tr_begin.tp_row, vb->vb_scr_size.tp_row));
	KASSERT(r->tr_begin.tp_col < vb->vb_scr_size.tp_col,
	    ("vtbuf_fill begin.tp_col %d must be < screen width %d",
		r->tr_begin.tp_col, vb->vb_scr_size.tp_col));

	KASSERT(r->tr_end.tp_row <= vb->vb_scr_size.tp_row,
	    ("vtbuf_fill end.tp_row %d must be <= screen height %d",
		r->tr_end.tp_row, vb->vb_scr_size.tp_row));
	KASSERT(r->tr_end.tp_col <= vb->vb_scr_size.tp_col,
	    ("vtbuf_fill end.tp_col %d must be <= screen width %d",
		r->tr_end.tp_col, vb->vb_scr_size.tp_col));

	vtbuf_do_fill(vb, r, c);
	vtbuf_dirty(vb, r);
}

static void
vtbuf_init_rows(struct vt_buf *vb)
{
	int r;

	vb->vb_history_size = MAX(vb->vb_history_size, vb->vb_scr_size.tp_row);

	for (r = 0; r < vb->vb_history_size; r++)
		vb->vb_rows[r] = &vb->vb_buffer[r * vb->vb_scr_size.tp_col];
}

void
vtbuf_init_early(struct vt_buf *vb)
{
	term_rect_t rect;

	vb->vb_flags |= VBF_CURSOR;
	vb->vb_roffset = 0;
	vb->vb_curroffset = 0;
	vb->vb_mark_start.tp_row = 0;
	vb->vb_mark_start.tp_col = 0;
	vb->vb_mark_end.tp_row = 0;
	vb->vb_mark_end.tp_col = 0;

	vtbuf_init_rows(vb);
	rect.tr_begin.tp_row = rect.tr_begin.tp_col = 0;
	rect.tr_end.tp_col = vb->vb_scr_size.tp_col;
	rect.tr_end.tp_row = vb->vb_history_size;
	vtbuf_do_fill(vb, &rect, VTBUF_SPACE_CHAR(TERMINAL_NORM_ATTR));
	vtbuf_make_undirty(vb);
	if ((vb->vb_flags & VBF_MTX_INIT) == 0) {
		mtx_init(&vb->vb_lock, "vtbuf", NULL, MTX_SPIN);
		vb->vb_flags |= VBF_MTX_INIT;
	}
}

void
vtbuf_init(struct vt_buf *vb, const term_pos_t *p)
{
	int sz;

	vb->vb_scr_size = *p;
	vb->vb_history_size = VBF_DEFAULT_HISTORY_SIZE;

	if ((vb->vb_flags & VBF_STATIC) == 0) {
		sz = vb->vb_history_size * p->tp_col * sizeof(term_char_t);
		vb->vb_buffer = malloc(sz, M_VTBUF, M_WAITOK | M_ZERO);

		sz = vb->vb_history_size * sizeof(term_char_t *);
		vb->vb_rows = malloc(sz, M_VTBUF, M_WAITOK | M_ZERO);
	}

	vtbuf_init_early(vb);
}

void
vtbuf_sethistory_size(struct vt_buf *vb, unsigned int size)
{
	term_pos_t p;

	/* With same size */
	p.tp_row = vb->vb_scr_size.tp_row;
	p.tp_col = vb->vb_scr_size.tp_col;
	vtbuf_grow(vb, &p, size);
}

void
vtbuf_grow(struct vt_buf *vb, const term_pos_t *p, unsigned int history_size)
{
	term_char_t *old, *new, **rows, **oldrows, **copyrows, *row, *oldrow;
	unsigned int w, h, c, r, old_history_size;
	size_t bufsize, rowssize;
	int history_full;

	history_size = MAX(history_size, p->tp_row);

	/* Allocate new buffer. */
	bufsize = history_size * p->tp_col * sizeof(term_char_t);
	new = malloc(bufsize, M_VTBUF, M_WAITOK | M_ZERO);
	rowssize = history_size * sizeof(term_pos_t *);
	rows = malloc(rowssize, M_VTBUF, M_WAITOK | M_ZERO);

	/* Toggle it. */
	VTBUF_LOCK(vb);
	old = vb->vb_flags & VBF_STATIC ? NULL : vb->vb_buffer;
	oldrows = vb->vb_flags & VBF_STATIC ? NULL : vb->vb_rows;
	copyrows = vb->vb_rows;

	w = vb->vb_scr_size.tp_col;
	h = vb->vb_scr_size.tp_row;
	old_history_size = vb->vb_history_size;
	history_full = vb->vb_flags & VBF_HISTORY_FULL ||
	    vb->vb_curroffset + h >= history_size;

	vb->vb_history_size = history_size;
	vb->vb_buffer = new;
	vb->vb_rows = rows;
	vb->vb_flags &= ~VBF_STATIC;
	vb->vb_scr_size = *p;
	vtbuf_init_rows(vb);

	/*
	 * Copy rows to the new buffer. The first row in the history
	 * is back to index 0, ie. the new buffer doesn't cycle.
	 */
	if (history_size > old_history_size) {
		for (r = 0; r < old_history_size; r ++) {
			row = rows[r];

			/* Compute the corresponding row in the old buffer. */
			if (history_full)
				/*
				 * The buffer is full, the "top" row is
				 * the one just after the viewable area
				 * (curroffset + viewable height) in the
				 * cycling buffer. The corresponding row
				 * is computed from this top row.
				 */
				oldrow = copyrows[
				    (vb->vb_curroffset + h + r) %
				    old_history_size];
			else
				/*
				 * The buffer is not full, therefore,
				 * we didn't cycle already. The
				 * corresponding rows are the same in
				 * both buffers.
				 */
				oldrow = copyrows[r];

			memmove(row, oldrow,
			    MIN(p->tp_col, w) * sizeof(term_char_t));

			/*
			 * XXX VTBUF_SPACE_CHAR(TERMINAL_NORM_ATTR) will
			 * extended lines of kernel text using the wrong
			 * background color.
			 */
			for (c = MIN(p->tp_col, w); c < p->tp_col; c++) {
				row[c] = VTBUF_SPACE_CHAR(TERMINAL_NORM_ATTR);
			}
		}

		/* Fill remaining rows. */
		for (r = old_history_size; r < history_size; r++) {
			row = rows[r];
			for (c = MIN(p->tp_col, w); c < p->tp_col; c++) {
				row[c] = VTBUF_SPACE_CHAR(TERMINAL_NORM_ATTR);
			}
		}

		vb->vb_flags &= ~VBF_HISTORY_FULL;

		/*
		 * If the screen is already filled (there are non-visible lines
		 * above the current viewable area), adjust curroffset to the
		 * new viewable area.
		 *
		 * If the old buffer was full, set curroffset to the
		 * <h>th most recent line of history in the new, non-cycled
		 * buffer. Otherwise, it didn't cycle, so the old curroffset
		 * is the same in the new buffer.
		 */
		if (history_full)
			vb->vb_curroffset = old_history_size - h;
	} else {
		/*
		 * (old_history_size - history_size) lines of history are
		 * dropped.
		 */
		for (r = 0; r < history_size; r ++) {
			row = rows[r];

			/*
			 * Compute the corresponding row in the old buffer.
			 *
			 * See the equivalent if{} block above for an
			 * explanation.
			 */
			if (history_full)
				oldrow = copyrows[
				    (vb->vb_curroffset + h + r +
				     (old_history_size - history_size)) %
				    old_history_size];
			else
				oldrow = copyrows[r];

			memmove(row, oldrow,
			    MIN(p->tp_col, w) * sizeof(term_char_t));

			/*
			 * XXX VTBUF_SPACE_CHAR(TERMINAL_NORM_ATTR) will
			 * extended lines of kernel text using the wrong
			 * background color.
			 */
			for (c = MIN(p->tp_col, w); c < p->tp_col; c++) {
				row[c] = VTBUF_SPACE_CHAR(TERMINAL_NORM_ATTR);
			}
		}

		if (history_full) {
			vb->vb_curroffset = history_size - h;
			vb->vb_flags |= VBF_HISTORY_FULL;
		}
	}

	vb->vb_roffset = vb->vb_curroffset;

	/* Adjust cursor position. */
	if (vb->vb_cursor.tp_col > p->tp_col - 1)
		/*
		 * Move cursor to the last column, in case its previous
		 * position is outside of the new screen area.
		 */
		vb->vb_cursor.tp_col = p->tp_col - 1;

	if (vb->vb_curroffset > 0 || vb->vb_cursor.tp_row > p->tp_row - 1)
		/* Move cursor to the last line on the screen. */
		vb->vb_cursor.tp_row = p->tp_row - 1;

	VTBUF_UNLOCK(vb);

	/* Deallocate old buffer. */
	free(old, M_VTBUF);
	free(oldrows, M_VTBUF);
}

void
vtbuf_putchar(struct vt_buf *vb, const term_pos_t *p, term_char_t c)
{
	term_char_t *row;

	KASSERT(p->tp_row < vb->vb_scr_size.tp_row,
	    ("vtbuf_putchar tp_row %d must be less than screen width %d",
		p->tp_row, vb->vb_scr_size.tp_row));
	KASSERT(p->tp_col < vb->vb_scr_size.tp_col,
	    ("vtbuf_putchar tp_col %d must be less than screen height %d",
		p->tp_col, vb->vb_scr_size.tp_col));

	row = vb->vb_rows[(vb->vb_curroffset + p->tp_row) %
	    VTBUF_MAX_HEIGHT(vb)];
	if (row[p->tp_col] != c) {
		row[p->tp_col] = c;
		vtbuf_dirty_cell(vb, p);
	}
}

void
vtbuf_cursor_position(struct vt_buf *vb, const term_pos_t *p)
{
	if (vb->vb_flags & VBF_CURSOR) {
		vtbuf_dirty_cell(vb, &vb->vb_cursor);
		vb->vb_cursor = *p;
		vtbuf_dirty_cell(vb, &vb->vb_cursor);
	} else {
		vb->vb_cursor = *p;
	}
}

#ifndef SC_NO_CUTPASTE
static void
vtbuf_flush_mark(struct vt_buf *vb)
{
	term_rect_t area;
	int s, e;

	/* Notify renderer to update marked region. */
	if ((vb->vb_mark_start.tp_col != vb->vb_mark_end.tp_col) ||
	    (vb->vb_mark_start.tp_row != vb->vb_mark_end.tp_row)) {

		s = vtbuf_htw(vb, vb->vb_mark_start.tp_row);
		e = vtbuf_htw(vb, vb->vb_mark_end.tp_row);

		area.tr_begin.tp_col = 0;
		area.tr_begin.tp_row = MIN(s, e);

		area.tr_end.tp_col = vb->vb_scr_size.tp_col;
		area.tr_end.tp_row = MAX(s, e) + 1;

		VTBUF_LOCK(vb);
		vtbuf_dirty(vb, &area);
		VTBUF_UNLOCK(vb);
	}
}

int
vtbuf_get_marked_len(struct vt_buf *vb)
{
	int ei, si, sz;
	term_pos_t s, e;

	/* Swap according to window coordinates. */
	if (POS_INDEX(vtbuf_htw(vb, vb->vb_mark_start.tp_row),
	    vb->vb_mark_start.tp_col) >
	    POS_INDEX(vtbuf_htw(vb, vb->vb_mark_end.tp_row),
	    vb->vb_mark_end.tp_col)) {
		POS_COPY(e, vb->vb_mark_start);
		POS_COPY(s, vb->vb_mark_end);
	} else {
		POS_COPY(s, vb->vb_mark_start);
		POS_COPY(e, vb->vb_mark_end);
	}

	si = s.tp_row * vb->vb_scr_size.tp_col + s.tp_col;
	ei = e.tp_row * vb->vb_scr_size.tp_col + e.tp_col;

	/* Number symbols and number of rows to inject \n */
	sz = ei - si + ((e.tp_row - s.tp_row) * 2);

	return (sz * sizeof(term_char_t));
}

void
vtbuf_extract_marked(struct vt_buf *vb, term_char_t *buf, int sz)
{
	int i, r, c, cs, ce;
	term_pos_t s, e;

	/* Swap according to window coordinates. */
	if (POS_INDEX(vtbuf_htw(vb, vb->vb_mark_start.tp_row),
	    vb->vb_mark_start.tp_col) >
	    POS_INDEX(vtbuf_htw(vb, vb->vb_mark_end.tp_row),
	    vb->vb_mark_end.tp_col)) {
		POS_COPY(e, vb->vb_mark_start);
		POS_COPY(s, vb->vb_mark_end);
	} else {
		POS_COPY(s, vb->vb_mark_start);
		POS_COPY(e, vb->vb_mark_end);
	}

	i = 0;
	for (r = s.tp_row; r <= e.tp_row; r ++) {
		cs = (r == s.tp_row)?s.tp_col:0;
		ce = (r == e.tp_row)?e.tp_col:vb->vb_scr_size.tp_col;
		for (c = cs; c < ce; c ++) {
			buf[i++] = vb->vb_rows[r][c];
		}
		/* Add new line for all rows, but not for last one. */
		if (r != e.tp_row) {
			buf[i++] = '\r';
			buf[i++] = '\n';
		}
	}
}

int
vtbuf_set_mark(struct vt_buf *vb, int type, int col, int row)
{
	term_char_t *r;
	int i;

	switch (type) {
	case VTB_MARK_END:	/* B1 UP */
		if (vb->vb_mark_last != VTB_MARK_MOVE)
			return (0);
		/* FALLTHROUGH */
	case VTB_MARK_MOVE:
	case VTB_MARK_EXTEND:
		vtbuf_flush_mark(vb); /* Clean old mark. */
		vb->vb_mark_end.tp_col = col;
		vb->vb_mark_end.tp_row = vtbuf_wth(vb, row);
		break;
	case VTB_MARK_START:
		vtbuf_flush_mark(vb); /* Clean old mark. */
		vb->vb_mark_start.tp_col = col;
		vb->vb_mark_start.tp_row = vtbuf_wth(vb, row);
		/* Start again, so clear end point. */
		vb->vb_mark_end.tp_col = col;
		vb->vb_mark_end.tp_row = vtbuf_wth(vb, row);
		break;
	case VTB_MARK_WORD:
		vtbuf_flush_mark(vb); /* Clean old mark. */
		vb->vb_mark_start.tp_row = vb->vb_mark_end.tp_row =
		    vtbuf_wth(vb, row);
		r = vb->vb_rows[vb->vb_mark_start.tp_row];
		for (i = col; i >= 0; i --) {
			if (TCHAR_CHARACTER(r[i]) == ' ') {
				vb->vb_mark_start.tp_col = i + 1;
				break;
			}
		}
		for (i = col; i < vb->vb_scr_size.tp_col; i ++) {
			if (TCHAR_CHARACTER(r[i]) == ' ') {
				vb->vb_mark_end.tp_col = i;
				break;
			}
		}
		if (vb->vb_mark_start.tp_col > vb->vb_mark_end.tp_col)
			vb->vb_mark_start.tp_col = vb->vb_mark_end.tp_col;
		break;
	case VTB_MARK_ROW:
		vtbuf_flush_mark(vb); /* Clean old mark. */
		vb->vb_mark_start.tp_col = 0;
		vb->vb_mark_end.tp_col = vb->vb_scr_size.tp_col;
		vb->vb_mark_start.tp_row = vb->vb_mark_end.tp_row =
		    vtbuf_wth(vb, row);
		break;
	case VTB_MARK_NONE:
		vb->vb_mark_last = type;
		/* FALLTHROUGH */
	default:
		/* panic? */
		return (0);
	}

	vb->vb_mark_last = type;
	/* Draw new marked region. */
	vtbuf_flush_mark(vb);
	return (1);
}
#endif

void
vtbuf_cursor_visibility(struct vt_buf *vb, int yes)
{
	int oflags, nflags;

	oflags = vb->vb_flags;
	if (yes)
		vb->vb_flags |= VBF_CURSOR;
	else
		vb->vb_flags &= ~VBF_CURSOR;
	nflags = vb->vb_flags;

	if (oflags != nflags)
		vtbuf_dirty_cell(vb, &vb->vb_cursor);
}

void
vtbuf_scroll_mode(struct vt_buf *vb, int yes)
{
	int oflags, nflags;

	VTBUF_LOCK(vb);
	oflags = vb->vb_flags;
	if (yes)
		vb->vb_flags |= VBF_SCROLL;
	else
		vb->vb_flags &= ~VBF_SCROLL;
	nflags = vb->vb_flags;

	if (oflags != nflags)
		vtbuf_dirty_cell(vb, &vb->vb_cursor);
	VTBUF_UNLOCK(vb);
}
