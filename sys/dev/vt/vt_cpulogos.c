/*-
 * Copyright (c) 2015 Conrad Meyer <cse.cem@gmail.com>
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
#include <sys/callout.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/smp.h>
#include <sys/systm.h>
#include <sys/terminal.h>

#include <dev/vt/vt.h>

extern const unsigned char vt_beastie_vga16[];
extern const unsigned char vt_beastie2_vga16[];
extern const unsigned char vt_orb_vga16[];

static struct callout vt_splash_cpu_callout;

static inline unsigned char
vt_vga2bsd(unsigned char vga)
{
	static const unsigned char lut[8] = {
		0,
		4,	/* 1 and 4 swap */
		2,
		6,	/* 3 and 6 swap */
		1,	/* 4 and 1 swap */
		5,
		3,	/* 6 and 3 swap */
		7,
	};
	unsigned int bright;

	bright = (vga & 0x8);
	return (lut[vga & 0x7] | bright);
}

static void
vt_draw_2_vga16_px(struct vt_device *vd, vt_axis_t x, vt_axis_t y,
    unsigned char color)
{

	vd->vd_driver->vd_setpixel(vd, x, y, vt_vga2bsd(color >> 4));
	vd->vd_driver->vd_setpixel(vd, x + 1, y, vt_vga2bsd(color & 0xf));
}

static void
vt_draw_1_logo(struct vt_device *vd, vt_axis_t top, vt_axis_t left)
{
	const unsigned char rle_sent = 0x16, *data;
	unsigned int xy, run, runcolor, i;

	switch (vt_splash_cpu_style) {
	case VT_LOGOS_DRAW_ALT_BEASTIE:
		data = vt_beastie2_vga16;
		break;
	case VT_LOGOS_DRAW_ORB:
		data = vt_orb_vga16;
		break;
	case VT_LOGOS_DRAW_BEASTIE:
		/* FALLTHROUGH */
	default:
		data = vt_beastie_vga16;
		break;
	}

	/* Decode basic RLE (gets us to 30-40% of uncompressed data size): */
	for (i = 0, xy = 0; xy < vt_logo_sprite_height * vt_logo_sprite_width;) {
		if (data[i] == rle_sent) {
			runcolor = data[i + 1];
			run = data[i + 2];

			for (; run; run--, xy += 2)
				vt_draw_2_vga16_px(vd,
				    left + (xy % vt_logo_sprite_width),
				    top + (xy / vt_logo_sprite_width),
				    runcolor);

			i += 3;
		} else {
			vt_draw_2_vga16_px(vd, left + (xy % vt_logo_sprite_width),
			    top + (xy / vt_logo_sprite_width), data[i]);

			i++;
			xy += 2;
		}
	}
}

void
vtterm_draw_cpu_logos(struct vt_device *vd)
{
	unsigned int ncpu, i;
	vt_axis_t left;

	if (vt_splash_ncpu)
		ncpu = vt_splash_ncpu;
	else {
		ncpu = mp_ncpus;
		if (ncpu < 1)
			ncpu = 1;
	}

	if (vd->vd_driver->vd_drawrect)
		vd->vd_driver->vd_drawrect(vd, 0, 0, vd->vd_width,
		    vt_logo_sprite_height, 1, TC_BLACK);
	/*
	 * Blank is okay because we only ever draw beasties on full screen
	 * refreshes.
	 */
	else if (vd->vd_driver->vd_blank)
		vd->vd_driver->vd_blank(vd, TC_BLACK);

	ncpu = MIN(ncpu, vd->vd_width / vt_logo_sprite_width);
	for (i = 0, left = 0; i < ncpu; left += vt_logo_sprite_width, i++)
		vt_draw_1_logo(vd, 0, left);
}

static void
vt_fini_logos(void *dummy __unused)
{
	struct vt_device *vd;
	struct vt_window *vw;
	struct terminal *tm;
	struct vt_font *vf;
	struct winsize wsz;
	term_pos_t size;
	unsigned int i;

	if (!vt_draw_logo_cpus)
		return;
	if (!vty_enabled(VTY_VT))
		return;
	if (!vt_splash_cpu)
		return;

	vd = &vt_consdev;
	VT_LOCK(vd);
	if ((vd->vd_flags & (VDF_DEAD | VDF_TEXTMODE)) != 0) {
		VT_UNLOCK(vd);
		return;
	}
	vt_draw_logo_cpus = 0;
	VT_UNLOCK(vd);

	for (i = 0; i < VT_MAXWINDOWS; i++) {
		vw = vd->vd_windows[i];
		if (vw == NULL)
			continue;
		tm = vw->vw_terminal;
		vf = vw->vw_font;
		if (vf == NULL)
			continue;

		vt_termsize(vd, vf, &size);
		vt_winsize(vd, vf, &wsz);

		/* Resize screen buffer and terminal. */
		terminal_mute(tm, 1);
		vtbuf_grow(&vw->vw_buf, &size, vw->vw_buf.vb_history_size);
		terminal_set_winsize_blank(tm, &wsz, 0, NULL);
		terminal_set_cursor(tm, &vw->vw_buf.vb_cursor);
		terminal_mute(tm, 0);

		VT_LOCK(vd);
		vt_compute_drawable_area(vw);

		if (vd->vd_curwindow == vw) {
			vd->vd_flags |= VDF_INVALID;
			vt_resume_flush_timer(vw, 0);
		}
		VT_UNLOCK(vd);
	}
}

static void
vt_init_logos(void *dummy)
{
	struct vt_device *vd;
	struct vt_window *vw;
	struct terminal *tm;
	struct vt_font *vf;
	struct winsize wsz;
	term_pos_t size;

	if (!vty_enabled(VTY_VT))
		return;
	if (!vt_splash_cpu)
		return;

	tm = &vt_consterm;
	vw = tm->tm_softc;
	if (vw == NULL)
		return;
	vd = vw->vw_device;
	if (vd == NULL)
		return;
	vf = vw->vw_font;
	if (vf == NULL)
		return;

	VT_LOCK(vd);
	KASSERT((vd->vd_flags & VDF_INITIALIZED) != 0,
	    ("vd %p not initialized", vd));

	if ((vd->vd_flags & (VDF_DEAD | VDF_TEXTMODE)) != 0)
		goto out;
	if (vd->vd_height <= vt_logo_sprite_height)
		goto out;

	vt_draw_logo_cpus = 1;
	VT_UNLOCK(vd);

	vt_termsize(vd, vf, &size);
	vt_winsize(vd, vf, &wsz);

	/* Resize screen buffer and terminal. */
	terminal_mute(tm, 1);
	vtbuf_grow(&vw->vw_buf, &size, vw->vw_buf.vb_history_size);
	terminal_set_winsize_blank(tm, &wsz, 0, NULL);
	terminal_set_cursor(tm, &vw->vw_buf.vb_cursor);
	terminal_mute(tm, 0);

	VT_LOCK(vd);
	vt_compute_drawable_area(vw);

	if (vd->vd_curwindow == vw) {
		vd->vd_flags |= VDF_INVALID;
		vt_resume_flush_timer(vw, 0);
	}

	callout_init(&vt_splash_cpu_callout, 1);
	callout_reset(&vt_splash_cpu_callout, vt_splash_cpu_duration * hz,
	    vt_fini_logos, NULL);

out:
	VT_UNLOCK(vd);
}
SYSINIT(vt_logos, SI_SUB_CPU + 1, SI_ORDER_ANY, vt_init_logos, NULL);
