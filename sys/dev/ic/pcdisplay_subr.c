/* $OpenBSD: pcdisplay_subr.c,v 1.14 2020/05/25 09:55:48 jsg Exp $ */
/* $NetBSD: pcdisplay_subr.c,v 1.16 2000/06/08 07:01:19 cgd Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <machine/bus.h>

#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

void
pcdisplay_cursor_reset(struct pcdisplayscreen *scr)
{
#ifdef PCDISPLAY_SOFTCURSOR
	pcdisplay_6845_write(scr->hdl, curstart, 0x20);
	pcdisplay_6845_write(scr->hdl, curend, 0x00);
#endif
}

void
pcdisplay_cursor_init(struct pcdisplayscreen *scr, int existing)
{
#ifdef PCDISPLAY_SOFTCURSOR
	bus_space_tag_t memt;
	bus_space_handle_t memh;
	int off;
#endif

	pcdisplay_cursor_reset(scr);

#ifdef PCDISPLAY_SOFTCURSOR
	if (existing) {
		/*
		 * This is the first screen. At this point, scr->active is
		 * false and scr->mem is NULL (no backing store), so we
		 * can't use pcdisplay_cursor() to do this.
		 */
		memt = scr->hdl->ph_memt;
		memh = scr->hdl->ph_memh;
		off = (scr->vc_crow * scr->type->ncols + scr->vc_ccol) * 2 +
		    scr->dispoffset;

		scr->cursortmp = bus_space_read_2(memt, memh, off);
		bus_space_write_2(memt, memh, off, scr->cursortmp ^ 0x7700);
	} else
		scr->cursortmp = 0;
#endif
	scr->cursoron = 1;
}

int
pcdisplay_cursor(void *id, int on, int row, int col)
{
#ifdef PCDISPLAY_SOFTCURSOR
	struct pcdisplayscreen *scr = id;
	bus_space_tag_t memt = scr->hdl->ph_memt;
	bus_space_handle_t memh = scr->hdl->ph_memh;
	int off;
	int s = spltty();

	/* Remove old cursor image */
	if (scr->cursoron) {
		off = scr->vc_crow * scr->type->ncols + scr->vc_ccol;
		if (scr->active)
			bus_space_write_2(memt, memh, scr->dispoffset + off * 2,
			    scr->cursortmp);
		else
			scr->mem[off] = scr->cursortmp;
	}
		
	scr->vc_crow = row;
	scr->vc_ccol = col;

	if ((scr->cursoron = on) == 0)
		goto done;

	off = (scr->vc_crow * scr->type->ncols + scr->vc_ccol);
	if (scr->active) {
		off = off * 2 + scr->dispoffset;
		scr->cursortmp = bus_space_read_2(memt, memh, off);
		bus_space_write_2(memt, memh, off, scr->cursortmp ^ 0x7700);
	} else {
		scr->cursortmp = scr->mem[off];
		scr->mem[off] = scr->cursortmp ^ 0x7700;
	}

done:
	splx(s);
	return 0;
#else 	/* PCDISPLAY_SOFTCURSOR */
	struct pcdisplayscreen *scr = id;
	int pos;
	int s = spltty();

	scr->vc_crow = row;
	scr->vc_ccol = col;
	scr->cursoron = on;

	if (scr->active) {
		if (!on)
			pos = 0x1010;
		else
			pos = scr->dispoffset / 2
				+ row * scr->type->ncols + col;

		pcdisplay_6845_write(scr->hdl, cursorh, pos >> 8);
		pcdisplay_6845_write(scr->hdl, cursorl, pos);
	}

	splx(s);
	return 0;
#endif	/* PCDISPLAY_SOFTCURSOR */
}

int
pcdisplay_putchar(void *id, int row, int col, u_int c, uint32_t attr)
{
	struct pcdisplayscreen *scr = id;
	bus_space_tag_t memt = scr->hdl->ph_memt;
	bus_space_handle_t memh = scr->hdl->ph_memh;
	int off;
	int s;

	off = row * scr->type->ncols + col;

	s = spltty();
	if (scr->active)
		bus_space_write_2(memt, memh, scr->dispoffset + off * 2,
				  c | (attr << 8));
	else
		scr->mem[off] = c | (attr << 8);
	splx(s);

	return 0;
}

int
pcdisplay_getchar(void *id, int row, int col, struct wsdisplay_charcell *cell)
{
	struct pcdisplayscreen *scr = id;
	bus_space_tag_t memt = scr->hdl->ph_memt;
	bus_space_handle_t memh = scr->hdl->ph_memh;
	int off;
	int s;
	u_int16_t data;
	
	off = row * scr->type->ncols + col;
	/* XXX bounds check? */
	
	s = spltty();
	if (scr->active)
		data = (bus_space_read_2(memt, memh, 
					scr->dispoffset + off * 2));
	else
		data = (scr->mem[off]);
	splx(s);

	cell->uc = data & 0xff;
	cell->attr = data >> 8;

	return (0);
}

int
pcdisplay_copycols(void *id, int row, int srccol, int dstcol, int ncols)
{
	struct pcdisplayscreen *scr = id;
	bus_space_tag_t memt = scr->hdl->ph_memt;
	bus_space_handle_t memh = scr->hdl->ph_memh;
	bus_size_t srcoff, dstoff;
	int s;

	srcoff = dstoff = row * scr->type->ncols;
	srcoff += srccol;
	dstoff += dstcol;

	s = spltty();
	if (scr->active)
		bus_space_copy_2(memt, memh,
					scr->dispoffset + srcoff * 2,
					memh, scr->dispoffset + dstoff * 2,
					ncols);
	else
		bcopy(&scr->mem[srcoff], &scr->mem[dstoff], ncols * 2);
	splx(s);

	return 0;
}

int
pcdisplay_erasecols(void *id, int row, int startcol, int ncols, uint32_t fillattr)
{
	struct pcdisplayscreen *scr = id;
	bus_space_tag_t memt = scr->hdl->ph_memt;
	bus_space_handle_t memh = scr->hdl->ph_memh;
	bus_size_t off;
	u_int16_t val;
	int i;
	int s;

	off = row * scr->type->ncols + startcol;
	val = (fillattr << 8) | ' ';

	s = spltty();
	if (scr->active)
		bus_space_set_region_2(memt, memh, scr->dispoffset + off * 2,
				       val, ncols);
	else
		for (i = 0; i < ncols; i++)
			scr->mem[off + i] = val;
	splx(s);

	return 0;
}

int
pcdisplay_copyrows(void *id, int srcrow, int dstrow, int nrows)
{
	struct pcdisplayscreen *scr = id;
	bus_space_tag_t memt = scr->hdl->ph_memt;
	bus_space_handle_t memh = scr->hdl->ph_memh;
	int ncols = scr->type->ncols;
	bus_size_t srcoff, dstoff;
	int s;

	srcoff = srcrow * ncols + 0;
	dstoff = dstrow * ncols + 0;

	s = spltty();
	if (scr->active)
		bus_space_copy_2(memt, memh,
					scr->dispoffset + srcoff * 2,
					memh, scr->dispoffset + dstoff * 2,
					nrows * ncols);
	else
		bcopy(&scr->mem[srcoff], &scr->mem[dstoff],
		      nrows * ncols * 2);
	splx(s);

	return 0;
}

int
pcdisplay_eraserows(void *id, int startrow, int nrows, uint32_t fillattr)
{
	struct pcdisplayscreen *scr = id;
	bus_space_tag_t memt = scr->hdl->ph_memt;
	bus_space_handle_t memh = scr->hdl->ph_memh;
	bus_size_t off, count, n;
	u_int16_t val;
	int s;

	off = startrow * scr->type->ncols;
	count = nrows * scr->type->ncols;
	val = (fillattr << 8) | ' ';

	s = spltty();
	if (scr->active)
		bus_space_set_region_2(memt, memh, scr->dispoffset + off * 2,
				       val, count);
	else
		for (n = 0; n < count; n++)
			scr->mem[off + n] = val;
	splx(s);

	return 0;
}
