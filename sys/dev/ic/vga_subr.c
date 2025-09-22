/* $OpenBSD: vga_subr.c,v 1.5 2015/07/18 00:48:05 miod Exp $ */
/* $NetBSD: vga_subr.c,v 1.6 2000/01/25 02:44:03 ad Exp $ */

/*
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <machine/bus.h>

#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#include <dev/ic/vgareg.h>

#include <dev/wscons/wsdisplayvar.h>
#include <dev/ic/vgavar.h>

static void fontram(struct vga_handle *);
static void textram(struct vga_handle *);

static void
fontram(struct vga_handle *vh)
{
	/* program sequencer to access character generator */

	vga_ts_write(vh, syncreset, 0x01);	/* synchronous reset */
	vga_ts_write(vh, wrplmask, 0x04);	/* write to map 2 */
	vga_ts_write(vh, memmode, 0x07);	/* sequential addressing */
	vga_ts_write(vh, syncreset, 0x03);	/* clear synchronous reset */

	/* program graphics controller to access character generator */

	vga_gdc_write(vh, rdplanesel, 0x02);	/* select map 2 for cpu reads */
	vga_gdc_write(vh, mode, 0x00);	/* disable odd-even addressing */
	vga_gdc_write(vh, misc, 0x04);	/* map starts at 0xA000 */
}

static void
textram(struct vga_handle *vh)
{
	/* program sequencer to access video ram */

	vga_ts_write(vh, syncreset, 0x01);	/* synchronous reset */
	vga_ts_write(vh, wrplmask, 0x03);	/* write to map 0 & 1 */
	vga_ts_write(vh, memmode, 0x03);	/* odd-even addressing */
	vga_ts_write(vh, syncreset, 0x03);	/* clear synchronous reset */

	/* program graphics controller for text mode */

	vga_gdc_write(vh, rdplanesel, 0x00);	/* select map 0 for cpu reads */
	vga_gdc_write(vh, mode, 0x10);		/* enable odd-even addressing */
	/* map starts at 0xb800 or 0xb000 (mono) */
	vga_gdc_write(vh, misc, (vh->vh_mono ? 0x0a : 0x0e));
}

void
vga_loadchars(struct vga_handle *vh, int fontset, int first, int num, int lpc,
    char *data)
{
	int offset, i, j, s;

	/* fontset number swizzle done in vga_setfontset() */
	offset = (fontset << 13) | (first << 5);

	s = splhigh();
	fontram(vh);

	for (i = 0; i < num; i++)
		for (j = 0; j < lpc; j++)
			bus_space_write_1(vh->vh_memt, vh->vh_allmemh,
					  offset + (i << 5) + j,
					  data[i * lpc + j]);

	textram(vh);
	splx(s);
}

void
vga_setfontset(struct vga_handle *vh, int fontset1, int fontset2)
{
	u_int8_t cmap;
	static const u_int8_t cmaptaba[] = {
		0x00, 0x10, 0x01, 0x11,
		0x02, 0x12, 0x03, 0x13
	};
	static const u_int8_t cmaptabb[] = {
		0x00, 0x20, 0x04, 0x24,
		0x08, 0x28, 0x0c, 0x2c
	};

	/* extended font if fontset1 != fontset2 */
	cmap = cmaptaba[fontset1] | cmaptabb[fontset2];

	vga_ts_write(vh, fontsel, cmap);
}

void
vga_setscreentype(struct vga_handle *vh, const struct wsscreen_descr *type)
{
	vga_6845_write(vh, maxrow, type->fontheight - 1);

	/* lo byte */
	vga_6845_write(vh, vde, type->fontheight * type->nrows - 1);

#ifndef PCDISPLAY_SOFTCURSOR
	/* set cursor to last 2 lines */
	vga_6845_write(vh, curstart, type->fontheight - 2);
	vga_6845_write(vh, curend, type->fontheight - 1);
#endif
	/*
	 * disable colour plane 3 if needed for font selection
	 */
	if (type->capabilities & WSSCREEN_HILIT) {
		/*
		 * these are the screens which don't support
		 * 512-character fonts
		 */
		vga_attr_write(vh, colplen, 0x0f);
	} else
		vga_attr_write(vh, colplen, 0x07);
}
