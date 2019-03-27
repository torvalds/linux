/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Tycho Nightingale <tycho.nightingale@pluribusnetworks.com>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
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

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <machine/vmm.h>

#include "bhyvegc.h"
#include "console.h"
#include "inout.h"
#include "mem.h"
#include "vga.h"

#define	KB	(1024UL)
#define	MB	(1024 * 1024UL)

struct vga_softc {
	struct mem_range	mr;

	struct bhyvegc		*gc;
	int			gc_width;
	int			gc_height;
	struct bhyvegc_image	*gc_image;

	uint8_t			*vga_ram;

	/*
	 * General registers
	 */
	uint8_t			vga_misc;
	uint8_t			vga_sts1;

	/*
	 * Sequencer
	 */
	struct {
		int		seq_index;
		uint8_t		seq_reset;
		uint8_t		seq_clock_mode;
		int		seq_cm_dots;
		uint8_t		seq_map_mask;
		uint8_t		seq_cmap_sel;
		int		seq_cmap_pri_off;
		int		seq_cmap_sec_off;
		uint8_t		seq_mm;
	} vga_seq;

	/*
	 * CRT Controller
	 */
	struct {
		int		crtc_index;
		uint8_t		crtc_mode_ctrl;
		uint8_t		crtc_horiz_total;
		uint8_t		crtc_horiz_disp_end;
		uint8_t		crtc_start_horiz_blank;
		uint8_t		crtc_end_horiz_blank;
		uint8_t		crtc_start_horiz_retrace;
		uint8_t		crtc_end_horiz_retrace;
		uint8_t		crtc_vert_total;
		uint8_t		crtc_overflow;
		uint8_t		crtc_present_row_scan;
		uint8_t		crtc_max_scan_line;
		uint8_t		crtc_cursor_start;
		uint8_t		crtc_cursor_on;
		uint8_t		crtc_cursor_end;
		uint8_t		crtc_start_addr_high;
		uint8_t		crtc_start_addr_low;
		uint16_t	crtc_start_addr;
		uint8_t		crtc_cursor_loc_low;
		uint8_t		crtc_cursor_loc_high;
		uint16_t	crtc_cursor_loc;
		uint8_t		crtc_vert_retrace_start;
		uint8_t		crtc_vert_retrace_end;
		uint8_t		crtc_vert_disp_end;
		uint8_t		crtc_offset;
		uint8_t		crtc_underline_loc;
		uint8_t		crtc_start_vert_blank;
		uint8_t		crtc_end_vert_blank;
		uint8_t		crtc_line_compare;
	} vga_crtc;

	/*
	 * Graphics Controller
	 */
	struct {
		int		gc_index;
		uint8_t		gc_set_reset;
		uint8_t		gc_enb_set_reset;
		uint8_t		gc_color_compare;
		uint8_t		gc_rotate;
		uint8_t		gc_op;
		uint8_t		gc_read_map_sel;
		uint8_t		gc_mode;
		bool		gc_mode_c4;		/* chain 4 */
		bool		gc_mode_oe;		/* odd/even */
		uint8_t		gc_mode_rm;		/* read mode */
		uint8_t		gc_mode_wm;		/* write mode */
		uint8_t		gc_misc;
		uint8_t		gc_misc_gm;		/* graphics mode */
		uint8_t		gc_misc_mm;		/* memory map */
		uint8_t		gc_color_dont_care;
		uint8_t		gc_bit_mask;
		uint8_t		gc_latch0;
		uint8_t		gc_latch1;
		uint8_t		gc_latch2;
		uint8_t		gc_latch3;
	} vga_gc;

	/*
	 * Attribute Controller
	 */
	struct {
		int		atc_flipflop;
		int		atc_index;
		uint8_t		atc_palette[16];
		uint8_t		atc_mode;
		uint8_t		atc_overscan_color;
		uint8_t		atc_color_plane_enb;
		uint8_t		atc_horiz_pixel_panning;
		uint8_t		atc_color_select;
		uint8_t		atc_color_select_45;
		uint8_t		atc_color_select_67;
	} vga_atc;

	/*
	 * DAC
	 */
	struct {
		uint8_t		dac_state;
		uint8_t		dac_rd_index;
		uint8_t		dac_rd_subindex;
		uint8_t		dac_wr_index;
		uint8_t		dac_wr_subindex;
		uint8_t		dac_palette[3 * 256];
		uint32_t	dac_palette_rgb[256];
	} vga_dac;
};

static bool
vga_in_reset(struct vga_softc *sc)
{
	return (((sc->vga_seq.seq_clock_mode & SEQ_CM_SO) != 0) ||
	    ((sc->vga_seq.seq_reset & SEQ_RESET_ASYNC) == 0) ||
	    ((sc->vga_seq.seq_reset & SEQ_RESET_SYNC) == 0) ||
	    ((sc->vga_crtc.crtc_mode_ctrl & CRTC_MC_TE) == 0));
}

static void
vga_check_size(struct bhyvegc *gc, struct vga_softc *sc)
{
	int old_width, old_height;

	if (vga_in_reset(sc))
		return;

	//old_width = sc->gc_width;
	//old_height = sc->gc_height;
	old_width = sc->gc_image->width;
	old_height = sc->gc_image->height;

	/*
	 * Horizontal Display End: For text modes this is the number
	 * of characters.  For graphics modes this is the number of
	 * pixels per scanlines divided by the number of pixels per
	 * character clock.
	 */
	sc->gc_width = (sc->vga_crtc.crtc_horiz_disp_end + 1) *
	    sc->vga_seq.seq_cm_dots;

	sc->gc_height = (sc->vga_crtc.crtc_vert_disp_end |
	    (((sc->vga_crtc.crtc_overflow & CRTC_OF_VDE8) >> CRTC_OF_VDE8_SHIFT) << 8) |
	    (((sc->vga_crtc.crtc_overflow & CRTC_OF_VDE9) >> CRTC_OF_VDE9_SHIFT) << 9)) + 1;

	if (old_width != sc->gc_width || old_height != sc->gc_height)
		bhyvegc_resize(gc, sc->gc_width, sc->gc_height);
}

static uint32_t
vga_get_pixel(struct vga_softc *sc, int x, int y)
{
	int offset;
	int bit;
	uint8_t data;
	uint8_t idx;

	offset = (y * sc->gc_width / 8) + (x / 8);
	bit = 7 - (x % 8);

	data = (((sc->vga_ram[offset + 0 * 64*KB] >> bit) & 0x1) << 0) |
		(((sc->vga_ram[offset + 1 * 64*KB] >> bit) & 0x1) << 1) |
		(((sc->vga_ram[offset + 2 * 64*KB] >> bit) & 0x1) << 2) |
		(((sc->vga_ram[offset + 3 * 64*KB] >> bit) & 0x1) << 3);

	data &= sc->vga_atc.atc_color_plane_enb;

	if (sc->vga_atc.atc_mode & ATC_MC_IPS) {
		idx = sc->vga_atc.atc_palette[data] & 0x0f;
		idx |= sc->vga_atc.atc_color_select_45;
	} else {
		idx = sc->vga_atc.atc_palette[data];
	}
	idx |= sc->vga_atc.atc_color_select_67;

	return (sc->vga_dac.dac_palette_rgb[idx]);
}

static void
vga_render_graphics(struct vga_softc *sc)
{
	int x, y;

	for (y = 0; y < sc->gc_height; y++) {
		for (x = 0; x < sc->gc_width; x++) {
			int offset;

			offset = y * sc->gc_width + x;
			sc->gc_image->data[offset] = vga_get_pixel(sc, x, y);
		}
	}
}

static uint32_t
vga_get_text_pixel(struct vga_softc *sc, int x, int y)
{
	int dots, offset, bit, font_offset;
	uint8_t ch, attr, font;
	uint8_t idx;

	dots = sc->vga_seq.seq_cm_dots;

	offset = 2 * sc->vga_crtc.crtc_start_addr;
	offset += (y / 16 * sc->gc_width / dots) * 2 + (x / dots) * 2;

	bit = 7 - (x % dots > 7 ? 7 : x % dots);

	ch = sc->vga_ram[offset + 0 * 64*KB];
	attr = sc->vga_ram[offset + 1 * 64*KB];

	if (sc->vga_crtc.crtc_cursor_on &&
	    (offset == (sc->vga_crtc.crtc_cursor_loc * 2)) &&
	    ((y % 16) >= (sc->vga_crtc.crtc_cursor_start & CRTC_CS_CS)) &&
	    ((y % 16) <= (sc->vga_crtc.crtc_cursor_end & CRTC_CE_CE))) {
		idx = sc->vga_atc.atc_palette[attr & 0xf];
		return (sc->vga_dac.dac_palette_rgb[idx]);
	}

	if ((sc->vga_seq.seq_mm & SEQ_MM_EM) &&
	    sc->vga_seq.seq_cmap_pri_off != sc->vga_seq.seq_cmap_sec_off) {
		if (attr & 0x8)
			font_offset = sc->vga_seq.seq_cmap_pri_off +
				(ch << 5) + y % 16;
		else
			font_offset = sc->vga_seq.seq_cmap_sec_off +
				(ch << 5) + y % 16;
		attr &= ~0x8;
	} else {
		font_offset = (ch << 5) + y % 16;
	}

	font = sc->vga_ram[font_offset + 2 * 64*KB];

	if (font & (1 << bit))
		idx = sc->vga_atc.atc_palette[attr & 0xf];
	else
		idx = sc->vga_atc.atc_palette[attr >> 4];

	return (sc->vga_dac.dac_palette_rgb[idx]);
}

static void
vga_render_text(struct vga_softc *sc)
{
	int x, y;

	for (y = 0; y < sc->gc_height; y++) {
		for (x = 0; x < sc->gc_width; x++) {
			int offset;

			offset = y * sc->gc_width + x;
			sc->gc_image->data[offset] = vga_get_text_pixel(sc, x, y);
		}
	}
}

void
vga_render(struct bhyvegc *gc, void *arg)
{
	struct vga_softc *sc = arg;

	vga_check_size(gc, sc);

	if (vga_in_reset(sc)) {
		memset(sc->gc_image->data, 0,
		    sc->gc_image->width * sc->gc_image->height *
		     sizeof (uint32_t));
		return;
	}

	if (sc->vga_gc.gc_misc_gm && (sc->vga_atc.atc_mode & ATC_MC_GA))
		vga_render_graphics(sc);
	else
		vga_render_text(sc);
}

static uint64_t
vga_mem_rd_handler(struct vmctx *ctx, uint64_t addr, void *arg1)
{
	struct vga_softc *sc = arg1;
	uint8_t map_sel;
	int offset;

	offset = addr;
	switch (sc->vga_gc.gc_misc_mm) {
	case 0x0:
		/*
		 * extended mode: base 0xa0000 size 128k
		 */
		offset -=0xa0000;
		offset &= (128 * KB - 1);
		break;
	case 0x1:
		/*
		 * EGA/VGA mode: base 0xa0000 size 64k
		 */
		offset -=0xa0000;
		offset &= (64 * KB - 1);
		break;
	case 0x2:
		/*
		 * monochrome text mode: base 0xb0000 size 32kb
		 */
		assert(0);
	case 0x3:
		/*
		 * color text mode and CGA: base 0xb8000 size 32kb
		 */
		offset -=0xb8000;
		offset &= (32 * KB - 1);
		break;
	}

	/* Fill latches. */
	sc->vga_gc.gc_latch0 = sc->vga_ram[offset + 0*64*KB];
	sc->vga_gc.gc_latch1 = sc->vga_ram[offset + 1*64*KB];
	sc->vga_gc.gc_latch2 = sc->vga_ram[offset + 2*64*KB];
	sc->vga_gc.gc_latch3 = sc->vga_ram[offset + 3*64*KB];

	if (sc->vga_gc.gc_mode_rm) {
		/* read mode 1 */
		assert(0);
	}

	map_sel = sc->vga_gc.gc_read_map_sel;
	if (sc->vga_gc.gc_mode_oe) {
		map_sel |= (offset & 1);
		offset &= ~1;
	}

	/* read mode 0: return the byte from the selected plane. */
	offset += map_sel * 64*KB;

	return (sc->vga_ram[offset]);
}

static void
vga_mem_wr_handler(struct vmctx *ctx, uint64_t addr, uint8_t val, void *arg1)
{
	struct vga_softc *sc = arg1;
	uint8_t c0, c1, c2, c3;
	uint8_t m0, m1, m2, m3;
	uint8_t set_reset;
	uint8_t enb_set_reset;
	uint8_t	mask;
	int offset;

	offset = addr;
	switch (sc->vga_gc.gc_misc_mm) {
	case 0x0:
		/*
		 * extended mode: base 0xa0000 size 128kb
		 */
		offset -=0xa0000;
		offset &= (128 * KB - 1);
		break;
	case 0x1:
		/*
		 * EGA/VGA mode: base 0xa0000 size 64kb
		 */
		offset -=0xa0000;
		offset &= (64 * KB - 1);
		break;
	case 0x2:
		/*
		 * monochrome text mode: base 0xb0000 size 32kb
		 */
		assert(0);
	case 0x3:
		/*
		 * color text mode and CGA: base 0xb8000 size 32kb
		 */
		offset -=0xb8000;
		offset &= (32 * KB - 1);
		break;
	}

	set_reset = sc->vga_gc.gc_set_reset;
	enb_set_reset = sc->vga_gc.gc_enb_set_reset;

	c0 = sc->vga_gc.gc_latch0;
	c1 = sc->vga_gc.gc_latch1;
	c2 = sc->vga_gc.gc_latch2;
	c3 = sc->vga_gc.gc_latch3;

	switch (sc->vga_gc.gc_mode_wm) {
	case 0:
		/* write mode 0 */
		mask = sc->vga_gc.gc_bit_mask;

		val = (val >> sc->vga_gc.gc_rotate) |
		    (val << (8 - sc->vga_gc.gc_rotate));

		switch (sc->vga_gc.gc_op) {
		case 0x00:		/* replace */
			m0 = (set_reset & 1) ? mask : 0x00;
			m1 = (set_reset & 2) ? mask : 0x00;
			m2 = (set_reset & 4) ? mask : 0x00;
			m3 = (set_reset & 8) ? mask : 0x00;

			c0 = (enb_set_reset & 1) ? (c0 & ~mask) : (val & mask);
			c1 = (enb_set_reset & 2) ? (c1 & ~mask) : (val & mask);
			c2 = (enb_set_reset & 4) ? (c2 & ~mask) : (val & mask);
			c3 = (enb_set_reset & 8) ? (c3 & ~mask) : (val & mask);

			c0 |= m0;
			c1 |= m1;
			c2 |= m2;
			c3 |= m3;
			break;
		case 0x08:		/* AND */
			m0 = set_reset & 1 ? 0xff : ~mask;
			m1 = set_reset & 2 ? 0xff : ~mask;
			m2 = set_reset & 4 ? 0xff : ~mask;
			m3 = set_reset & 8 ? 0xff : ~mask;

			c0 = enb_set_reset & 1 ? c0 & m0 : val & m0;
			c1 = enb_set_reset & 2 ? c1 & m1 : val & m1;
			c2 = enb_set_reset & 4 ? c2 & m2 : val & m2;
			c3 = enb_set_reset & 8 ? c3 & m3 : val & m3;
			break;
		case 0x10:		/* OR */
			m0 = set_reset & 1 ? mask : 0x00;
			m1 = set_reset & 2 ? mask : 0x00;
			m2 = set_reset & 4 ? mask : 0x00;
			m3 = set_reset & 8 ? mask : 0x00;

			c0 = enb_set_reset & 1 ? c0 | m0 : val | m0;
			c1 = enb_set_reset & 2 ? c1 | m1 : val | m1;
			c2 = enb_set_reset & 4 ? c2 | m2 : val | m2;
			c3 = enb_set_reset & 8 ? c3 | m3 : val | m3;
			break;
		case 0x18:		/* XOR */
			m0 = set_reset & 1 ? mask : 0x00;
			m1 = set_reset & 2 ? mask : 0x00;
			m2 = set_reset & 4 ? mask : 0x00;
			m3 = set_reset & 8 ? mask : 0x00;

			c0 = enb_set_reset & 1 ? c0 ^ m0 : val ^ m0;
			c1 = enb_set_reset & 2 ? c1 ^ m1 : val ^ m1;
			c2 = enb_set_reset & 4 ? c2 ^ m2 : val ^ m2;
			c3 = enb_set_reset & 8 ? c3 ^ m3 : val ^ m3;
			break;
		}
		break;
	case 1:
		/* write mode 1 */
		break;
	case 2:
		/* write mode 2 */
		mask = sc->vga_gc.gc_bit_mask;

		switch (sc->vga_gc.gc_op) {
		case 0x00:		/* replace */
			m0 = (val & 1 ? 0xff : 0x00) & mask;
			m1 = (val & 2 ? 0xff : 0x00) & mask;
			m2 = (val & 4 ? 0xff : 0x00) & mask;
			m3 = (val & 8 ? 0xff : 0x00) & mask;

			c0 &= ~mask;
			c1 &= ~mask;
			c2 &= ~mask;
			c3 &= ~mask;

			c0 |= m0;
			c1 |= m1;
			c2 |= m2;
			c3 |= m3;
			break;
		case 0x08:		/* AND */
			m0 = (val & 1 ? 0xff : 0x00) | ~mask;
			m1 = (val & 2 ? 0xff : 0x00) | ~mask;
			m2 = (val & 4 ? 0xff : 0x00) | ~mask;
			m3 = (val & 8 ? 0xff : 0x00) | ~mask;

			c0 &= m0;
			c1 &= m1;
			c2 &= m2;
			c3 &= m3;
			break;
		case 0x10:		/* OR */
			m0 = (val & 1 ? 0xff : 0x00) & mask;
			m1 = (val & 2 ? 0xff : 0x00) & mask;
			m2 = (val & 4 ? 0xff : 0x00) & mask;
			m3 = (val & 8 ? 0xff : 0x00) & mask;

			c0 |= m0;
			c1 |= m1;
			c2 |= m2;
			c3 |= m3;
			break;
		case 0x18:		/* XOR */
			m0 = (val & 1 ? 0xff : 0x00) & mask;
			m1 = (val & 2 ? 0xff : 0x00) & mask;
			m2 = (val & 4 ? 0xff : 0x00) & mask;
			m3 = (val & 8 ? 0xff : 0x00) & mask;

			c0 ^= m0;
			c1 ^= m1;
			c2 ^= m2;
			c3 ^= m3;
			break;
		}
		break;
	case 3:
		/* write mode 3 */
		mask = sc->vga_gc.gc_bit_mask & val;

		val = (val >> sc->vga_gc.gc_rotate) |
		    (val << (8 - sc->vga_gc.gc_rotate));

		switch (sc->vga_gc.gc_op) {
		case 0x00:		/* replace */
			m0 = (set_reset & 1 ? 0xff : 0x00) & mask;
			m1 = (set_reset & 2 ? 0xff : 0x00) & mask;
			m2 = (set_reset & 4 ? 0xff : 0x00) & mask;
			m3 = (set_reset & 8 ? 0xff : 0x00) & mask;

			c0 &= ~mask;
			c1 &= ~mask;
			c2 &= ~mask;
			c3 &= ~mask;

			c0 |= m0;
			c1 |= m1;
			c2 |= m2;
			c3 |= m3;
			break;
		case 0x08:		/* AND */
			m0 = (set_reset & 1 ? 0xff : 0x00) | ~mask;
			m1 = (set_reset & 2 ? 0xff : 0x00) | ~mask;
			m2 = (set_reset & 4 ? 0xff : 0x00) | ~mask;
			m3 = (set_reset & 8 ? 0xff : 0x00) | ~mask;

			c0 &= m0;
			c1 &= m1;
			c2 &= m2;
			c3 &= m3;
			break;
		case 0x10:		/* OR */
			m0 = (set_reset & 1 ? 0xff : 0x00) & mask;
			m1 = (set_reset & 2 ? 0xff : 0x00) & mask;
			m2 = (set_reset & 4 ? 0xff : 0x00) & mask;
			m3 = (set_reset & 8 ? 0xff : 0x00) & mask;

			c0 |= m0;
			c1 |= m1;
			c2 |= m2;
			c3 |= m3;
			break;
		case 0x18:		/* XOR */
			m0 = (set_reset & 1 ? 0xff : 0x00) & mask;
			m1 = (set_reset & 2 ? 0xff : 0x00) & mask;
			m2 = (set_reset & 4 ? 0xff : 0x00) & mask;
			m3 = (set_reset & 8 ? 0xff : 0x00) & mask;

			c0 ^= m0;
			c1 ^= m1;
			c2 ^= m2;
			c3 ^= m3;
			break;
		}
		break;
	}

	if (sc->vga_gc.gc_mode_oe) {
		if (offset & 1) {
			offset &= ~1;
			if (sc->vga_seq.seq_map_mask & 2)
				sc->vga_ram[offset + 1*64*KB] = c1;
			if (sc->vga_seq.seq_map_mask & 8)
				sc->vga_ram[offset + 3*64*KB] = c3;
		} else {
			if (sc->vga_seq.seq_map_mask & 1)
				sc->vga_ram[offset + 0*64*KB] = c0;
			if (sc->vga_seq.seq_map_mask & 4)
				sc->vga_ram[offset + 2*64*KB] = c2;
		}
	} else {
		if (sc->vga_seq.seq_map_mask & 1)
			sc->vga_ram[offset + 0*64*KB] = c0;
		if (sc->vga_seq.seq_map_mask & 2)
			sc->vga_ram[offset + 1*64*KB] = c1;
		if (sc->vga_seq.seq_map_mask & 4)
			sc->vga_ram[offset + 2*64*KB] = c2;
		if (sc->vga_seq.seq_map_mask & 8)
			sc->vga_ram[offset + 3*64*KB] = c3;
	}
}

static int
vga_mem_handler(struct vmctx *ctx, int vcpu, int dir, uint64_t addr,
		int size, uint64_t *val, void *arg1, long arg2)
{
	if (dir == MEM_F_WRITE) {
		switch (size) {
		case 1:
			vga_mem_wr_handler(ctx, addr, *val, arg1);
			break;
		case 2:
			vga_mem_wr_handler(ctx, addr, *val, arg1);
			vga_mem_wr_handler(ctx, addr + 1, *val >> 8, arg1);
			break;
		case 4:
			vga_mem_wr_handler(ctx, addr, *val, arg1);
			vga_mem_wr_handler(ctx, addr + 1, *val >> 8, arg1);
			vga_mem_wr_handler(ctx, addr + 2, *val >> 16, arg1);
			vga_mem_wr_handler(ctx, addr + 3, *val >> 24, arg1);
			break;
		case 8:
			vga_mem_wr_handler(ctx, addr, *val, arg1);
			vga_mem_wr_handler(ctx, addr + 1, *val >> 8, arg1);
			vga_mem_wr_handler(ctx, addr + 2, *val >> 16, arg1);
			vga_mem_wr_handler(ctx, addr + 3, *val >> 24, arg1);
			vga_mem_wr_handler(ctx, addr + 4, *val >> 32, arg1);
			vga_mem_wr_handler(ctx, addr + 5, *val >> 40, arg1);
			vga_mem_wr_handler(ctx, addr + 6, *val >> 48, arg1);
			vga_mem_wr_handler(ctx, addr + 7, *val >> 56, arg1);
			break;
		}
	} else {
		switch (size) {
		case 1:
			*val = vga_mem_rd_handler(ctx, addr, arg1);
			break;
		case 2:
			*val = vga_mem_rd_handler(ctx, addr, arg1);
			*val |= vga_mem_rd_handler(ctx, addr + 1, arg1) << 8;
			break;
		case 4:
			*val = vga_mem_rd_handler(ctx, addr, arg1);
			*val |= vga_mem_rd_handler(ctx, addr + 1, arg1) << 8;
			*val |= vga_mem_rd_handler(ctx, addr + 2, arg1) << 16;
			*val |= vga_mem_rd_handler(ctx, addr + 3, arg1) << 24;
			break;
		case 8:
			*val = vga_mem_rd_handler(ctx, addr, arg1);
			*val |= vga_mem_rd_handler(ctx, addr + 1, arg1) << 8;
			*val |= vga_mem_rd_handler(ctx, addr + 2, arg1) << 16;
			*val |= vga_mem_rd_handler(ctx, addr + 3, arg1) << 24;
			*val |= vga_mem_rd_handler(ctx, addr + 4, arg1) << 32;
			*val |= vga_mem_rd_handler(ctx, addr + 5, arg1) << 40;
			*val |= vga_mem_rd_handler(ctx, addr + 6, arg1) << 48;
			*val |= vga_mem_rd_handler(ctx, addr + 7, arg1) << 56;
			break;
		}
	}

	return (0);
}

static int
vga_port_in_handler(struct vmctx *ctx, int in, int port, int bytes,
		    uint8_t *val, void *arg)
{
	struct vga_softc *sc = arg;

	switch (port) {
	case CRTC_IDX_MONO_PORT:
	case CRTC_IDX_COLOR_PORT:
		*val = sc->vga_crtc.crtc_index;
		break;
	case CRTC_DATA_MONO_PORT:
	case CRTC_DATA_COLOR_PORT:
		switch (sc->vga_crtc.crtc_index) {
		case CRTC_HORIZ_TOTAL:
			*val = sc->vga_crtc.crtc_horiz_total;
			break;
		case CRTC_HORIZ_DISP_END:
			*val = sc->vga_crtc.crtc_horiz_disp_end;
			break;
		case CRTC_START_HORIZ_BLANK:
			*val = sc->vga_crtc.crtc_start_horiz_blank;
			break;
		case CRTC_END_HORIZ_BLANK:
			*val = sc->vga_crtc.crtc_end_horiz_blank;
			break;
		case CRTC_START_HORIZ_RETRACE:
			*val = sc->vga_crtc.crtc_start_horiz_retrace;
			break;
		case CRTC_END_HORIZ_RETRACE:
			*val = sc->vga_crtc.crtc_end_horiz_retrace;
			break;
		case CRTC_VERT_TOTAL:
			*val = sc->vga_crtc.crtc_vert_total;
			break;
		case CRTC_OVERFLOW:
			*val = sc->vga_crtc.crtc_overflow;
			break;
		case CRTC_PRESET_ROW_SCAN:
			*val = sc->vga_crtc.crtc_present_row_scan;
			break;
		case CRTC_MAX_SCAN_LINE:
			*val = sc->vga_crtc.crtc_max_scan_line;
			break;
		case CRTC_CURSOR_START:
			*val = sc->vga_crtc.crtc_cursor_start;
			break;
		case CRTC_CURSOR_END:
			*val = sc->vga_crtc.crtc_cursor_end;
			break;
		case CRTC_START_ADDR_HIGH:
			*val = sc->vga_crtc.crtc_start_addr_high;
			break;
		case CRTC_START_ADDR_LOW:
			*val = sc->vga_crtc.crtc_start_addr_low;
			break;
		case CRTC_CURSOR_LOC_HIGH:
			*val = sc->vga_crtc.crtc_cursor_loc_high;
			break;
		case CRTC_CURSOR_LOC_LOW:
			*val = sc->vga_crtc.crtc_cursor_loc_low;
			break;
		case CRTC_VERT_RETRACE_START:
			*val = sc->vga_crtc.crtc_vert_retrace_start;
			break;
		case CRTC_VERT_RETRACE_END:
			*val = sc->vga_crtc.crtc_vert_retrace_end;
			break;
		case CRTC_VERT_DISP_END:
			*val = sc->vga_crtc.crtc_vert_disp_end;
			break;
		case CRTC_OFFSET:
			*val = sc->vga_crtc.crtc_offset;
			break;
		case CRTC_UNDERLINE_LOC:
			*val = sc->vga_crtc.crtc_underline_loc;
			break;
		case CRTC_START_VERT_BLANK:
			*val = sc->vga_crtc.crtc_start_vert_blank;
			break;
		case CRTC_END_VERT_BLANK:
			*val = sc->vga_crtc.crtc_end_vert_blank;
			break;
		case CRTC_MODE_CONTROL:
			*val = sc->vga_crtc.crtc_mode_ctrl;
			break;
		case CRTC_LINE_COMPARE:
			*val = sc->vga_crtc.crtc_line_compare;
			break;
		default:
			//printf("XXX VGA CRTC: inb 0x%04x at index %d\n", port, sc->vga_crtc.crtc_index);
			assert(0);
			break;
		}
		break;
	case ATC_IDX_PORT:
		*val = sc->vga_atc.atc_index;
		break;
	case ATC_DATA_PORT:
		switch (sc->vga_atc.atc_index) {
		case ATC_PALETTE0 ... ATC_PALETTE15:
			*val = sc->vga_atc.atc_palette[sc->vga_atc.atc_index];
			break;
		case ATC_MODE_CONTROL:
			*val = sc->vga_atc.atc_mode;
			break;
		case ATC_OVERSCAN_COLOR:
			*val = sc->vga_atc.atc_overscan_color;
			break;
		case ATC_COLOR_PLANE_ENABLE:
			*val = sc->vga_atc.atc_color_plane_enb;
			break;
		case ATC_HORIZ_PIXEL_PANNING:
			*val = sc->vga_atc.atc_horiz_pixel_panning;
			break;
		case ATC_COLOR_SELECT:
			*val = sc->vga_atc.atc_color_select;
			break;
		default:
			//printf("XXX VGA ATC inb 0x%04x at index %d\n", port , sc->vga_atc.atc_index);
			assert(0);
			break;
		}
		break;
	case SEQ_IDX_PORT:
		*val = sc->vga_seq.seq_index;
		break;
	case SEQ_DATA_PORT:
		switch (sc->vga_seq.seq_index) {
		case SEQ_RESET:
			*val = sc->vga_seq.seq_reset;
			break;
		case SEQ_CLOCKING_MODE:
			*val = sc->vga_seq.seq_clock_mode;
			break;
		case SEQ_MAP_MASK:
			*val = sc->vga_seq.seq_map_mask;
			break;
		case SEQ_CHAR_MAP_SELECT:
			*val = sc->vga_seq.seq_cmap_sel;
			break;
		case SEQ_MEMORY_MODE:
			*val = sc->vga_seq.seq_mm;
			break;
		default:
			//printf("XXX VGA SEQ: inb 0x%04x at index %d\n", port, sc->vga_seq.seq_index);
			assert(0);
			break;
		}
		break;
	case DAC_DATA_PORT:
		*val = sc->vga_dac.dac_palette[3 * sc->vga_dac.dac_rd_index +
					       sc->vga_dac.dac_rd_subindex];
		sc->vga_dac.dac_rd_subindex++;
		if (sc->vga_dac.dac_rd_subindex == 3) {
			sc->vga_dac.dac_rd_index++;
			sc->vga_dac.dac_rd_subindex = 0;
		}
		break;
	case GC_IDX_PORT:
		*val = sc->vga_gc.gc_index;
		break;
	case GC_DATA_PORT:
		switch (sc->vga_gc.gc_index) {
		case GC_SET_RESET:
			*val = sc->vga_gc.gc_set_reset;
			break;
		case GC_ENABLE_SET_RESET:
			*val = sc->vga_gc.gc_enb_set_reset;
			break;
		case GC_COLOR_COMPARE:
			*val = sc->vga_gc.gc_color_compare;
			break;
		case GC_DATA_ROTATE:
			*val = sc->vga_gc.gc_rotate;
			break;
		case GC_READ_MAP_SELECT:
			*val = sc->vga_gc.gc_read_map_sel;
			break;
		case GC_MODE:
			*val = sc->vga_gc.gc_mode;
			break;
		case GC_MISCELLANEOUS:
			*val = sc->vga_gc.gc_misc;
			break;
		case GC_COLOR_DONT_CARE:
			*val = sc->vga_gc.gc_color_dont_care;
			break;
		case GC_BIT_MASK:
			*val = sc->vga_gc.gc_bit_mask;
			break;
		default:
			//printf("XXX VGA GC: inb 0x%04x at index %d\n", port, sc->vga_crtc.crtc_index);
			assert(0);
			break;
		}
		break;
	case GEN_MISC_OUTPUT_PORT:
		*val = sc->vga_misc;
		break;
	case GEN_INPUT_STS0_PORT:
		assert(0);
		break;
	case GEN_INPUT_STS1_MONO_PORT:
	case GEN_INPUT_STS1_COLOR_PORT:
		sc->vga_atc.atc_flipflop = 0;
		sc->vga_sts1 = GEN_IS1_VR | GEN_IS1_DE;
		//sc->vga_sts1 ^= (GEN_IS1_VR | GEN_IS1_DE);
		*val = sc->vga_sts1;
		break;
	case GEN_FEATURE_CTRL_PORT:
		// OpenBSD calls this with bytes = 1
		//assert(0);
		*val = 0;
		break;
	case 0x3c3:
		*val = 0;
		break;
	default:
		printf("XXX vga_port_in_handler() unhandled port 0x%x\n", port);
		//assert(0);
		return (-1);
	}

	return (0);
}

static int
vga_port_out_handler(struct vmctx *ctx, int in, int port, int bytes,
		     uint8_t val, void *arg)
{
	struct vga_softc *sc = arg;

	switch (port) {
	case CRTC_IDX_MONO_PORT:
	case CRTC_IDX_COLOR_PORT:
		sc->vga_crtc.crtc_index = val;
		break;
	case CRTC_DATA_MONO_PORT:
	case CRTC_DATA_COLOR_PORT:
		switch (sc->vga_crtc.crtc_index) {
		case CRTC_HORIZ_TOTAL:
			sc->vga_crtc.crtc_horiz_total = val;
			break;
		case CRTC_HORIZ_DISP_END:
			sc->vga_crtc.crtc_horiz_disp_end = val;
			break;
		case CRTC_START_HORIZ_BLANK:
			sc->vga_crtc.crtc_start_horiz_blank = val;
			break;
		case CRTC_END_HORIZ_BLANK:
			sc->vga_crtc.crtc_end_horiz_blank = val;
			break;
		case CRTC_START_HORIZ_RETRACE:
			sc->vga_crtc.crtc_start_horiz_retrace = val;
			break;
		case CRTC_END_HORIZ_RETRACE:
			sc->vga_crtc.crtc_end_horiz_retrace = val;
			break;
		case CRTC_VERT_TOTAL:
			sc->vga_crtc.crtc_vert_total = val;
			break;
		case CRTC_OVERFLOW:
			sc->vga_crtc.crtc_overflow = val;
			break;
		case CRTC_PRESET_ROW_SCAN:
			sc->vga_crtc.crtc_present_row_scan = val;
			break;
		case CRTC_MAX_SCAN_LINE:
			sc->vga_crtc.crtc_max_scan_line = val;
			break;
		case CRTC_CURSOR_START:
			sc->vga_crtc.crtc_cursor_start = val;
			sc->vga_crtc.crtc_cursor_on = (val & CRTC_CS_CO) == 0;
			break;
		case CRTC_CURSOR_END:
			sc->vga_crtc.crtc_cursor_end = val;
			break;
		case CRTC_START_ADDR_HIGH:
			sc->vga_crtc.crtc_start_addr_high = val;
			sc->vga_crtc.crtc_start_addr &= 0x00ff;
			sc->vga_crtc.crtc_start_addr |= (val << 8);
			break;
		case CRTC_START_ADDR_LOW:
			sc->vga_crtc.crtc_start_addr_low = val;
			sc->vga_crtc.crtc_start_addr &= 0xff00;
			sc->vga_crtc.crtc_start_addr |= (val & 0xff);
			break;
		case CRTC_CURSOR_LOC_HIGH:
			sc->vga_crtc.crtc_cursor_loc_high = val;
			sc->vga_crtc.crtc_cursor_loc &= 0x00ff;
			sc->vga_crtc.crtc_cursor_loc |= (val << 8);
			break;
		case CRTC_CURSOR_LOC_LOW:
			sc->vga_crtc.crtc_cursor_loc_low = val;
			sc->vga_crtc.crtc_cursor_loc &= 0xff00;
			sc->vga_crtc.crtc_cursor_loc |= (val & 0xff);
			break;
		case CRTC_VERT_RETRACE_START:
			sc->vga_crtc.crtc_vert_retrace_start = val;
			break;
		case CRTC_VERT_RETRACE_END:
			sc->vga_crtc.crtc_vert_retrace_end = val;
			break;
		case CRTC_VERT_DISP_END:
			sc->vga_crtc.crtc_vert_disp_end = val;
			break;
		case CRTC_OFFSET:
			sc->vga_crtc.crtc_offset = val;
			break;
		case CRTC_UNDERLINE_LOC:
			sc->vga_crtc.crtc_underline_loc = val;
			break;
		case CRTC_START_VERT_BLANK:
			sc->vga_crtc.crtc_start_vert_blank = val;
			break;
		case CRTC_END_VERT_BLANK:
			sc->vga_crtc.crtc_end_vert_blank = val;
			break;
		case CRTC_MODE_CONTROL:
			sc->vga_crtc.crtc_mode_ctrl = val;
			break;
		case CRTC_LINE_COMPARE:
			sc->vga_crtc.crtc_line_compare = val;
			break;
		default:
			//printf("XXX VGA CRTC: outb 0x%04x, 0x%02x at index %d\n", port, val, sc->vga_crtc.crtc_index);
			assert(0);
			break;
		}
		break;
	case ATC_IDX_PORT:
		if (sc->vga_atc.atc_flipflop == 0) {
			if (sc->vga_atc.atc_index & 0x20)
				assert(0);
			sc->vga_atc.atc_index = val & ATC_IDX_MASK;
		} else {
			switch (sc->vga_atc.atc_index) {
			case ATC_PALETTE0 ... ATC_PALETTE15:
				sc->vga_atc.atc_palette[sc->vga_atc.atc_index] = val & 0x3f;
				break;
			case ATC_MODE_CONTROL:
				sc->vga_atc.atc_mode = val;
				break;
			case ATC_OVERSCAN_COLOR:
				sc->vga_atc.atc_overscan_color = val;
				break;
			case ATC_COLOR_PLANE_ENABLE:
				sc->vga_atc.atc_color_plane_enb = val;
				break;
			case ATC_HORIZ_PIXEL_PANNING:
				sc->vga_atc.atc_horiz_pixel_panning = val;
				break;
			case ATC_COLOR_SELECT:
				sc->vga_atc.atc_color_select = val;
				sc->vga_atc.atc_color_select_45 =
					(val & ATC_CS_C45) << 4;
				sc->vga_atc.atc_color_select_67 =
					((val & ATC_CS_C67) >> 2) << 6;
				break;
			default:
				//printf("XXX VGA ATC: outb 0x%04x, 0x%02x at index %d\n", port, val, sc->vga_atc.atc_index);
				assert(0);
				break;
			}
		}
		sc->vga_atc.atc_flipflop ^= 1;
		break;
	case ATC_DATA_PORT:
		break;
	case SEQ_IDX_PORT:
		sc->vga_seq.seq_index = val & 0x1f;
		break;
	case SEQ_DATA_PORT:
		switch (sc->vga_seq.seq_index) {
		case SEQ_RESET:
			sc->vga_seq.seq_reset = val;
			break;
		case SEQ_CLOCKING_MODE:
			sc->vga_seq.seq_clock_mode = val;
			sc->vga_seq.seq_cm_dots = (val & SEQ_CM_89) ? 8 : 9;
			break;
		case SEQ_MAP_MASK:
			sc->vga_seq.seq_map_mask = val;
			break;
		case SEQ_CHAR_MAP_SELECT:
			sc->vga_seq.seq_cmap_sel = val;

			sc->vga_seq.seq_cmap_pri_off = ((((val & SEQ_CMS_SA) >> SEQ_CMS_SA_SHIFT) * 2) + ((val & SEQ_CMS_SAH) >> SEQ_CMS_SAH_SHIFT)) * 8 * KB;
			sc->vga_seq.seq_cmap_sec_off = ((((val & SEQ_CMS_SB) >> SEQ_CMS_SB_SHIFT) * 2) + ((val & SEQ_CMS_SBH) >> SEQ_CMS_SBH_SHIFT)) * 8 * KB;
			break;
		case SEQ_MEMORY_MODE:
			sc->vga_seq.seq_mm = val;
			/* Windows queries Chain4 */
			//assert((sc->vga_seq.seq_mm & SEQ_MM_C4) == 0);
			break;
		default:
			//printf("XXX VGA SEQ: outb 0x%04x, 0x%02x at index %d\n", port, val, sc->vga_seq.seq_index);
			assert(0);
			break;
		}
		break;
	case DAC_MASK:
		break;
	case DAC_IDX_RD_PORT:
		sc->vga_dac.dac_rd_index = val;
		sc->vga_dac.dac_rd_subindex = 0;
		break;
	case DAC_IDX_WR_PORT:
		sc->vga_dac.dac_wr_index = val;
		sc->vga_dac.dac_wr_subindex = 0;
		break;
	case DAC_DATA_PORT:
		sc->vga_dac.dac_palette[3 * sc->vga_dac.dac_wr_index +
					sc->vga_dac.dac_wr_subindex] = val;
		sc->vga_dac.dac_wr_subindex++;
		if (sc->vga_dac.dac_wr_subindex == 3) {
			sc->vga_dac.dac_palette_rgb[sc->vga_dac.dac_wr_index] =
				((((sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 0] << 2) |
				   ((sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 0] & 0x1) << 1) |
				   (sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 0] & 0x1)) << 16) |
				 (((sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 1] << 2) |
				   ((sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 1] & 0x1) << 1) |
				   (sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 1] & 0x1)) << 8) |
				 (((sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 2] << 2) |
				   ((sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 2] & 0x1) << 1) |
				   (sc->vga_dac.dac_palette[3*sc->vga_dac.dac_wr_index + 2] & 0x1)) << 0));

			sc->vga_dac.dac_wr_index++;
			sc->vga_dac.dac_wr_subindex = 0;
		}
		break;
	case GC_IDX_PORT:
		sc->vga_gc.gc_index = val;
		break;
	case GC_DATA_PORT:
		switch (sc->vga_gc.gc_index) {
		case GC_SET_RESET:
			sc->vga_gc.gc_set_reset = val;
			break;
		case GC_ENABLE_SET_RESET:
			sc->vga_gc.gc_enb_set_reset = val;
			break;
		case GC_COLOR_COMPARE:
			sc->vga_gc.gc_color_compare = val;
			break;
		case GC_DATA_ROTATE:
			sc->vga_gc.gc_rotate = val;
			sc->vga_gc.gc_op = (val >> 3) & 0x3;
			break;
		case GC_READ_MAP_SELECT:
			sc->vga_gc.gc_read_map_sel = val;
			break;
		case GC_MODE:
			sc->vga_gc.gc_mode = val;
			sc->vga_gc.gc_mode_c4 = (val & GC_MODE_C4) != 0;
			assert(!sc->vga_gc.gc_mode_c4);
			sc->vga_gc.gc_mode_oe = (val & GC_MODE_OE) != 0;
			sc->vga_gc.gc_mode_rm = (val >> 3) & 0x1;
			sc->vga_gc.gc_mode_wm = val & 0x3;

			if (sc->gc_image)
				sc->gc_image->vgamode = 1;
			break;
		case GC_MISCELLANEOUS:
			sc->vga_gc.gc_misc = val;
			sc->vga_gc.gc_misc_gm = val & GC_MISC_GM;
			sc->vga_gc.gc_misc_mm = (val & GC_MISC_MM) >>
			    GC_MISC_MM_SHIFT;
			break;
		case GC_COLOR_DONT_CARE:
			sc->vga_gc.gc_color_dont_care = val;
			break;
		case GC_BIT_MASK:
			sc->vga_gc.gc_bit_mask = val;
			break;
		default:
			//printf("XXX VGA GC: outb 0x%04x, 0x%02x at index %d\n", port, val, sc->vga_gc.gc_index);
			assert(0);
			break;
		}
		break;
	case GEN_INPUT_STS0_PORT:
		/* write to Miscellaneous Output Register */
		sc->vga_misc = val;
		break;
	case GEN_INPUT_STS1_MONO_PORT:
	case GEN_INPUT_STS1_COLOR_PORT:
		/* write to Feature Control Register */
		break;
//	case 0x3c3:
//		break;
	default:
		printf("XXX vga_port_out_handler() unhandled port 0x%x, val 0x%x\n", port, val);
		//assert(0);
		return (-1);
	}
	return (0);
}

static int
vga_port_handler(struct vmctx *ctx, int vcpu, int in, int port, int bytes,
		 uint32_t *eax, void *arg)
{
	uint8_t val;
	int error;

	switch (bytes) {
	case 1:
		if (in) {
			*eax &= ~0xff;
			error = vga_port_in_handler(ctx, in, port, 1,
						    &val, arg);
			if (!error) {
				*eax |= val & 0xff;
			}
		} else {
			val = *eax & 0xff;
			error = vga_port_out_handler(ctx, in, port, 1,
						     val, arg);
		}
		break;
	case 2:
		if (in) {
			*eax &= ~0xffff;
			error = vga_port_in_handler(ctx, in, port, 1,
						    &val, arg);
			if (!error) {
				*eax |= val & 0xff;
			}
			error = vga_port_in_handler(ctx, in, port + 1, 1,
						    &val, arg);
			if (!error) {
				*eax |= (val & 0xff) << 8;
			}
		} else {
			val = *eax & 0xff;
			error = vga_port_out_handler(ctx, in, port, 1,
						     val, arg);
			val = (*eax >> 8) & 0xff;
			error =vga_port_out_handler(ctx, in, port + 1, 1,
						    val, arg);
		}
		break;
	default:
		assert(0);
		return (-1);
	}

	return (error);
}

void *
vga_init(int io_only)
{
	struct inout_port iop;
	struct vga_softc *sc;
	int port, error;

	sc = calloc(1, sizeof(struct vga_softc));

	bzero(&iop, sizeof(struct inout_port));
	iop.name = "VGA";
	for (port = VGA_IOPORT_START; port <= VGA_IOPORT_END; port++) {
		iop.port = port;
		iop.size = 1;
		iop.flags = IOPORT_F_INOUT;
		iop.handler = vga_port_handler;
		iop.arg = sc;

		error = register_inout(&iop);
		assert(error == 0);
	}

	sc->gc_image = console_get_image();

	/* only handle io ports; vga graphics is disabled */
	if (io_only)
		return(sc);

	sc->mr.name = "VGA memory";
	sc->mr.flags = MEM_F_RW;
	sc->mr.base = 640 * KB;
	sc->mr.size = 128 * KB;
	sc->mr.handler = vga_mem_handler;
	sc->mr.arg1 = sc;
	error = register_mem_fallback(&sc->mr);
	assert(error == 0);

	sc->vga_ram = malloc(256 * KB);
	memset(sc->vga_ram, 0, 256 * KB);

	{
		static uint8_t palette[] = {
			0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x2a,0x00, 0x00,0x2a,0x2a,
			0x2a,0x00,0x00, 0x2a,0x00,0x2a, 0x2a,0x2a,0x00, 0x2a,0x2a,0x2a,
			0x00,0x00,0x15, 0x00,0x00,0x3f, 0x00,0x2a,0x15, 0x00,0x2a,0x3f,
			0x2a,0x00,0x15, 0x2a,0x00,0x3f, 0x2a,0x2a,0x15, 0x2a,0x2a,0x3f,
		};
		int i;

		memcpy(sc->vga_dac.dac_palette, palette, 16 * 3 * sizeof (uint8_t));
		for (i = 0; i < 16; i++) {
			sc->vga_dac.dac_palette_rgb[i] =
				((((sc->vga_dac.dac_palette[3*i + 0] << 2) |
				   ((sc->vga_dac.dac_palette[3*i + 0] & 0x1) << 1) |
				   (sc->vga_dac.dac_palette[3*i + 0] & 0x1)) << 16) |
				 (((sc->vga_dac.dac_palette[3*i + 1] << 2) |
				   ((sc->vga_dac.dac_palette[3*i + 1] & 0x1) << 1) |
				   (sc->vga_dac.dac_palette[3*i + 1] & 0x1)) << 8) |
				 (((sc->vga_dac.dac_palette[3*i + 2] << 2) |
				   ((sc->vga_dac.dac_palette[3*i + 2] & 0x1) << 1) |
				   (sc->vga_dac.dac_palette[3*i + 2] & 0x1)) << 0));
		}
	}

	return (sc);
}
