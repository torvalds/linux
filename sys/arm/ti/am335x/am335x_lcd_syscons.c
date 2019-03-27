/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/fbio.h>
#include <sys/consio.h>

#include <sys/kdb.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fb/fbreg.h>
#include <dev/syscons/syscons.h>

#include "am335x_lcd.h"

struct video_adapter_softc {
	/* Videoadpater part */
	video_adapter_t	va;
	int		console;

	intptr_t	fb_addr;
	intptr_t	fb_paddr;
	unsigned int	fb_size;

	unsigned int	height;
	unsigned int	width;
	unsigned int	depth;
	unsigned int	stride;

	unsigned int	xmargin;
	unsigned int	ymargin;

	unsigned char	*font;
	int		initialized;
};

struct argb {
	uint8_t		a;
	uint8_t		r;
	uint8_t		g;
	uint8_t		b;
};

static struct argb am335x_syscons_palette[16] = {
	{0x00, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0xaa},
	{0x00, 0x00, 0xaa, 0x00},
	{0x00, 0x00, 0xaa, 0xaa},
	{0x00, 0xaa, 0x00, 0x00},
	{0x00, 0xaa, 0x00, 0xaa},
	{0x00, 0xaa, 0x55, 0x00},
	{0x00, 0xaa, 0xaa, 0xaa},
	{0x00, 0x55, 0x55, 0x55},
	{0x00, 0x55, 0x55, 0xff},
	{0x00, 0x55, 0xff, 0x55},
	{0x00, 0x55, 0xff, 0xff},
	{0x00, 0xff, 0x55, 0x55},
	{0x00, 0xff, 0x55, 0xff},
	{0x00, 0xff, 0xff, 0x55},
	{0x00, 0xff, 0xff, 0xff}
};

/* mouse pointer from dev/syscons/scgfbrndr.c */
static u_char mouse_pointer[16] = {
        0x00, 0x40, 0x60, 0x70, 0x78, 0x7c, 0x7e, 0x68,
        0x0c, 0x0c, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00
};

#define	AM335X_FONT_HEIGHT	16

#define FB_WIDTH		640
#define FB_HEIGHT		480
#define FB_DEPTH		24

static struct video_adapter_softc va_softc;

static int am335x_syscons_configure(int flags);

/*
 * Video driver routines and glue.
 */
static vi_probe_t		am335x_syscons_probe;
static vi_init_t		am335x_syscons_init;
static vi_get_info_t		am335x_syscons_get_info;
static vi_query_mode_t		am335x_syscons_query_mode;
static vi_set_mode_t		am335x_syscons_set_mode;
static vi_save_font_t		am335x_syscons_save_font;
static vi_load_font_t		am335x_syscons_load_font;
static vi_show_font_t		am335x_syscons_show_font;
static vi_save_palette_t	am335x_syscons_save_palette;
static vi_load_palette_t	am335x_syscons_load_palette;
static vi_set_border_t		am335x_syscons_set_border;
static vi_save_state_t		am335x_syscons_save_state;
static vi_load_state_t		am335x_syscons_load_state;
static vi_set_win_org_t		am335x_syscons_set_win_org;
static vi_read_hw_cursor_t	am335x_syscons_read_hw_cursor;
static vi_set_hw_cursor_t	am335x_syscons_set_hw_cursor;
static vi_set_hw_cursor_shape_t	am335x_syscons_set_hw_cursor_shape;
static vi_blank_display_t	am335x_syscons_blank_display;
static vi_mmap_t		am335x_syscons_mmap;
static vi_ioctl_t		am335x_syscons_ioctl;
static vi_clear_t		am335x_syscons_clear;
static vi_fill_rect_t		am335x_syscons_fill_rect;
static vi_bitblt_t		am335x_syscons_bitblt;
static vi_diag_t		am335x_syscons_diag;
static vi_save_cursor_palette_t	am335x_syscons_save_cursor_palette;
static vi_load_cursor_palette_t	am335x_syscons_load_cursor_palette;
static vi_copy_t		am335x_syscons_copy;
static vi_putp_t		am335x_syscons_putp;
static vi_putc_t		am335x_syscons_putc;
static vi_puts_t		am335x_syscons_puts;
static vi_putm_t		am335x_syscons_putm;

static video_switch_t am335x_sysconsvidsw = {
	.probe			= am335x_syscons_probe,
	.init			= am335x_syscons_init,
	.get_info		= am335x_syscons_get_info,
	.query_mode		= am335x_syscons_query_mode,
	.set_mode		= am335x_syscons_set_mode,
	.save_font		= am335x_syscons_save_font,
	.load_font		= am335x_syscons_load_font,
	.show_font		= am335x_syscons_show_font,
	.save_palette		= am335x_syscons_save_palette,
	.load_palette		= am335x_syscons_load_palette,
	.set_border		= am335x_syscons_set_border,
	.save_state		= am335x_syscons_save_state,
	.load_state		= am335x_syscons_load_state,
	.set_win_org		= am335x_syscons_set_win_org,
	.read_hw_cursor		= am335x_syscons_read_hw_cursor,
	.set_hw_cursor		= am335x_syscons_set_hw_cursor,
	.set_hw_cursor_shape	= am335x_syscons_set_hw_cursor_shape,
	.blank_display		= am335x_syscons_blank_display,
	.mmap			= am335x_syscons_mmap,
	.ioctl			= am335x_syscons_ioctl,
	.clear			= am335x_syscons_clear,
	.fill_rect		= am335x_syscons_fill_rect,
	.bitblt			= am335x_syscons_bitblt,
	.diag			= am335x_syscons_diag,
	.save_cursor_palette	= am335x_syscons_save_cursor_palette,
	.load_cursor_palette	= am335x_syscons_load_cursor_palette,
	.copy			= am335x_syscons_copy,
	.putp			= am335x_syscons_putp,
	.putc			= am335x_syscons_putc,
	.puts			= am335x_syscons_puts,
	.putm			= am335x_syscons_putm,
};

VIDEO_DRIVER(am335x_syscons, am335x_sysconsvidsw, am335x_syscons_configure);

static vr_init_t am335x_rend_init;
static vr_clear_t am335x_rend_clear;
static vr_draw_border_t am335x_rend_draw_border;
static vr_draw_t am335x_rend_draw;
static vr_set_cursor_t am335x_rend_set_cursor;
static vr_draw_cursor_t am335x_rend_draw_cursor;
static vr_blink_cursor_t am335x_rend_blink_cursor;
static vr_set_mouse_t am335x_rend_set_mouse;
static vr_draw_mouse_t am335x_rend_draw_mouse;

/*
 * We use our own renderer; this is because we must emulate a hardware
 * cursor.
 */
static sc_rndr_sw_t am335x_rend = {
	am335x_rend_init,
	am335x_rend_clear,
	am335x_rend_draw_border,
	am335x_rend_draw,
	am335x_rend_set_cursor,
	am335x_rend_draw_cursor,
	am335x_rend_blink_cursor,
	am335x_rend_set_mouse,
	am335x_rend_draw_mouse
};

RENDERER(am335x_syscons, 0, am335x_rend, gfb_set);
RENDERER_MODULE(am335x_syscons, gfb_set);

static void
am335x_rend_init(scr_stat* scp)
{
}

static void
am335x_rend_clear(scr_stat* scp, int c, int attr)
{
}

static void
am335x_rend_draw_border(scr_stat* scp, int color)
{
}

static void
am335x_rend_draw(scr_stat* scp, int from, int count, int flip)
{
	video_adapter_t* adp = scp->sc->adp;
	int i, c, a;

	if (!flip) {
		/* Normal printing */
		vidd_puts(adp, from, (uint16_t*)sc_vtb_pointer(&scp->vtb, from), count);
	} else {	
		/* This is for selections and such: invert the color attribute */
		for (i = count; i-- > 0; ++from) {
			c = sc_vtb_getc(&scp->vtb, from);
			a = sc_vtb_geta(&scp->vtb, from) >> 8;
			vidd_putc(adp, from, c, (a >> 4) | ((a & 0xf) << 4));
		}
	}
}

static void
am335x_rend_set_cursor(scr_stat* scp, int base, int height, int blink)
{
}

static void
am335x_rend_draw_cursor(scr_stat* scp, int off, int blink, int on, int flip)
{
	video_adapter_t* adp = scp->sc->adp;
	struct video_adapter_softc *sc;
	int row, col;
	uint8_t *addr;
	int i, j, bytes;

	sc = (struct video_adapter_softc *)adp;

	if (scp->curs_attr.height <= 0)
		return;

	if (sc->fb_addr == 0)
		return;

	if (off >= adp->va_info.vi_width * adp->va_info.vi_height)
		return;

	/* calculate the coordinates in the video buffer */
	row = (off / adp->va_info.vi_width) * adp->va_info.vi_cheight;
	col = (off % adp->va_info.vi_width) * adp->va_info.vi_cwidth;

	addr = (uint8_t *)sc->fb_addr
	    + (row + sc->ymargin)*(sc->stride)
	    + (sc->depth/8) * (col + sc->xmargin);

	bytes = sc->depth/8;

	/* our cursor consists of simply inverting the char under it */
	for (i = 0; i < adp->va_info.vi_cheight; i++) {
		for (j = 0; j < adp->va_info.vi_cwidth; j++) {
			switch (sc->depth) {
			case 32:
			case 24:
				addr[bytes*j + 2] ^= 0xff;
				/* FALLTHROUGH */
			case 16:
				addr[bytes*j + 1] ^= 0xff;
				addr[bytes*j] ^= 0xff;
				break;
			default:
				break;
			}
		}

		addr += sc->stride;
	}
}

static void
am335x_rend_blink_cursor(scr_stat* scp, int at, int flip)
{
}

static void
am335x_rend_set_mouse(scr_stat* scp)
{
}

static void
am335x_rend_draw_mouse(scr_stat* scp, int x, int y, int on)
{
	vidd_putm(scp->sc->adp, x, y, mouse_pointer, 0xffffffff, 16, 8);
}

static uint16_t am335x_syscons_static_window[ROW*COL];
extern u_char dflt_font_16[];

/*
 * Update videoadapter settings after changing resolution
 */
static void
am335x_syscons_update_margins(video_adapter_t *adp)
{
	struct video_adapter_softc *sc;
	video_info_t *vi;

	sc = (struct video_adapter_softc *)adp;
	vi = &adp->va_info;

	sc->xmargin = (sc->width - (vi->vi_width * vi->vi_cwidth)) / 2;
	sc->ymargin = (sc->height - (vi->vi_height * vi->vi_cheight))/2;
}

static phandle_t
am335x_syscons_find_panel_node(phandle_t start)
{
	phandle_t child;
	phandle_t result;

	for (child = OF_child(start); child != 0; child = OF_peer(child)) {
		if (ofw_bus_node_is_compatible(child, "ti,am335x-lcd"))
			return (child);
		if ((result = am335x_syscons_find_panel_node(child)))
			return (result);
	}

	return (0);
}

static int
am335x_syscons_configure(int flags)
{
	struct video_adapter_softc *va_sc;

	va_sc = &va_softc;
	phandle_t display, root;
	pcell_t cell;

	if (va_sc->initialized)
		return (0);

	va_sc->width = 0;
	va_sc->height = 0;

	/*
	 * It seems there is no way to let syscons framework know
	 * that framebuffer resolution has changed. So just try
	 * to fetch data from FDT and go with defaults if failed
	 */
	root = OF_finddevice("/");
	if ((root != -1) && 
	    (display = am335x_syscons_find_panel_node(root))) {
		if ((OF_getencprop(display, "panel_width", &cell,
		    sizeof(cell))) > 0)
			va_sc->width = cell;

		if ((OF_getencprop(display, "panel_height", &cell,
		    sizeof(cell))) > 0)
			va_sc->height = cell;
	}

	if (va_sc->width == 0)
		va_sc->width = FB_WIDTH;
	if (va_sc->height == 0)
		va_sc->height = FB_HEIGHT;

	am335x_syscons_init(0, &va_sc->va, 0);

	va_sc->initialized = 1;

	return (0);
}

static int
am335x_syscons_probe(int unit, video_adapter_t **adp, void *arg, int flags)
{

	return (0);
}

static int
am335x_syscons_init(int unit, video_adapter_t *adp, int flags)
{
	struct video_adapter_softc *sc;
	video_info_t *vi;

	sc = (struct video_adapter_softc *)adp;
	vi = &adp->va_info;

	vid_init_struct(adp, "am335x_syscons", -1, unit);

	sc->font = dflt_font_16;
	vi->vi_cheight = AM335X_FONT_HEIGHT;
	vi->vi_cwidth = 8;

	vi->vi_width = sc->width/8;
	vi->vi_height = sc->height/vi->vi_cheight;

	/*
	 * Clamp width/height to syscons maximums
	 */
	if (vi->vi_width > COL)
		vi->vi_width = COL;
	if (vi->vi_height > ROW)
		vi->vi_height = ROW;

	sc->xmargin = (sc->width - (vi->vi_width * vi->vi_cwidth)) / 2;
	sc->ymargin = (sc->height - (vi->vi_height * vi->vi_cheight))/2;


	adp->va_window = (vm_offset_t) am335x_syscons_static_window;
	adp->va_flags |= V_ADP_FONT /* | V_ADP_COLOR | V_ADP_MODECHANGE */;

	vid_register(&sc->va);

	return (0);
}

static int
am335x_syscons_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{
	bcopy(&adp->va_info, info, sizeof(*info));
	return (0);
}

static int
am335x_syscons_query_mode(video_adapter_t *adp, video_info_t *info)
{
	return (0);
}

static int
am335x_syscons_set_mode(video_adapter_t *adp, int mode)
{
	return (0);
}

static int
am335x_syscons_save_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{
	return (0);
}

static int
am335x_syscons_load_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{
	struct video_adapter_softc *sc = (struct video_adapter_softc *)adp;

	sc->font = data;

	return (0);
}

static int
am335x_syscons_show_font(video_adapter_t *adp, int page)
{
	return (0);
}

static int
am335x_syscons_save_palette(video_adapter_t *adp, u_char *palette)
{
	return (0);
}

static int
am335x_syscons_load_palette(video_adapter_t *adp, u_char *palette)
{
	return (0);
}

static int
am335x_syscons_set_border(video_adapter_t *adp, int border)
{
	return (am335x_syscons_blank_display(adp, border));
}

static int
am335x_syscons_save_state(video_adapter_t *adp, void *p, size_t size)
{
	return (0);
}

static int
am335x_syscons_load_state(video_adapter_t *adp, void *p)
{
	return (0);
}

static int
am335x_syscons_set_win_org(video_adapter_t *adp, off_t offset)
{
	return (0);
}

static int
am335x_syscons_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{
	*col = *row = 0;

	return (0);
}

static int
am335x_syscons_set_hw_cursor(video_adapter_t *adp, int col, int row)
{
	return (0);
}

static int
am335x_syscons_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
    int celsize, int blink)
{
	return (0);
}

static int
am335x_syscons_blank_display(video_adapter_t *adp, int mode)
{

	struct video_adapter_softc *sc;

	sc = (struct video_adapter_softc *)adp;
	if (sc && sc->fb_addr)
		memset((void*)sc->fb_addr, 0, sc->fb_size);

	return (0);
}

static int
am335x_syscons_mmap(video_adapter_t *adp, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	struct video_adapter_softc *sc;

	sc = (struct video_adapter_softc *)adp;

	/*
	 * This might be a legacy VGA mem request: if so, just point it at the
	 * framebuffer, since it shouldn't be touched
	 */
	if (offset < sc->stride*sc->height) {
		*paddr = sc->fb_paddr + offset;
		return (0);
	}

	return (EINVAL);
}

static int
am335x_syscons_ioctl(video_adapter_t *adp, u_long cmd, caddr_t data)
{
	struct video_adapter_softc *sc;
	struct fbtype *fb;

	sc = (struct video_adapter_softc *)adp;

	switch (cmd) {
	case FBIOGTYPE:
		fb = (struct fbtype *)data;
		fb->fb_type = FBTYPE_PCIMISC;
		fb->fb_height = sc->height;
		fb->fb_width = sc->width;
		fb->fb_depth = sc->depth;
		if (sc->depth <= 1 || sc->depth > 8)
			fb->fb_cmsize = 0;
		else
			fb->fb_cmsize = 1 << sc->depth;
		fb->fb_size = sc->fb_size;
		break;
	default:
		return (fb_commonioctl(adp, cmd, data));
	}

	return (0);
}

static int
am335x_syscons_clear(video_adapter_t *adp)
{

	return (am335x_syscons_blank_display(adp, 0));
}

static int
am335x_syscons_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{

	return (0);
}

static int
am335x_syscons_bitblt(video_adapter_t *adp, ...)
{

	return (0);
}

static int
am335x_syscons_diag(video_adapter_t *adp, int level)
{

	return (0);
}

static int
am335x_syscons_save_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
am335x_syscons_load_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
am335x_syscons_copy(video_adapter_t *adp, vm_offset_t src, vm_offset_t dst, int n)
{

	return (0);
}

static int
am335x_syscons_putp(video_adapter_t *adp, vm_offset_t off, uint32_t p, uint32_t a,
    int size, int bpp, int bit_ltor, int byte_ltor)
{

	return (0);
}

static int
am335x_syscons_putc(video_adapter_t *adp, vm_offset_t off, uint8_t c, uint8_t a)
{
	struct video_adapter_softc *sc;
	int row;
	int col;
	int i, j, k;
	uint8_t *addr;
	u_char *p;
	uint8_t fg, bg, color;
	uint16_t rgb;

	sc = (struct video_adapter_softc *)adp;

	if (sc->fb_addr == 0)
		return (0);

	row = (off / adp->va_info.vi_width) * adp->va_info.vi_cheight;
	col = (off % adp->va_info.vi_width) * adp->va_info.vi_cwidth;
	p = sc->font + c*AM335X_FONT_HEIGHT;
	addr = (uint8_t *)sc->fb_addr
	    + (row + sc->ymargin)*(sc->stride)
	    + (sc->depth/8) * (col + sc->xmargin);

	fg = a & 0xf ;
	bg = (a >> 4) & 0xf;

	for (i = 0; i < AM335X_FONT_HEIGHT; i++) {
		for (j = 0, k = 7; j < 8; j++, k--) {
			if ((p[i] & (1 << k)) == 0)
				color = bg;
			else
				color = fg;

			switch (sc->depth) {
			case 32:
				addr[4*j+0] = am335x_syscons_palette[color].r;
				addr[4*j+1] = am335x_syscons_palette[color].g;
				addr[4*j+2] = am335x_syscons_palette[color].b;
				addr[4*j+3] = am335x_syscons_palette[color].a;
				break;
			case 24:
				addr[3*j] = am335x_syscons_palette[color].r;
				addr[3*j+1] = am335x_syscons_palette[color].g;
				addr[3*j+2] = am335x_syscons_palette[color].b;
				break;
			case 16:
				rgb = (am335x_syscons_palette[color].r >> 3) << 11;
				rgb |= (am335x_syscons_palette[color].g >> 2) << 5;
				rgb |= (am335x_syscons_palette[color].b >> 3);
				addr[2*j] = rgb & 0xff;
				addr[2*j + 1] = (rgb >> 8) & 0xff;
			default:
				/* Not supported yet */
				break;
			}
		}

		addr += (sc->stride);
	}

        return (0);
}

static int
am335x_syscons_puts(video_adapter_t *adp, vm_offset_t off, u_int16_t *s, int len)
{
	int i;

	for (i = 0; i < len; i++) 
		am335x_syscons_putc(adp, off + i, s[i] & 0xff, (s[i] & 0xff00) >> 8);

	return (0);
}

static int
am335x_syscons_putm(video_adapter_t *adp, int x, int y, uint8_t *pixel_image,
    uint32_t pixel_mask, int size, int width)
{

	return (0);
}

/* Initialization function */
int am335x_lcd_syscons_setup(vm_offset_t vaddr, vm_paddr_t paddr,
    struct panel_info *panel)
{
	struct video_adapter_softc *va_sc = &va_softc;

	va_sc->fb_addr = vaddr;
	va_sc->fb_paddr = paddr;
	va_sc->depth = panel->bpp;
	va_sc->stride = panel->bpp*panel->panel_width/8;

	va_sc->width = panel->panel_width;
	va_sc->height = panel->panel_height;
	va_sc->fb_size = va_sc->width * va_sc->height
	    * va_sc->depth/8;
	am335x_syscons_update_margins(&va_sc->va);

	return (0);
}

/*
 * Define a stub keyboard driver in case one hasn't been
 * compiled into the kernel
 */
#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>

static int dummy_kbd_configure(int flags);

keyboard_switch_t am335x_dummysw;

static int
dummy_kbd_configure(int flags)
{

	return (0);
}
KEYBOARD_DRIVER(am335x_dummy, am335x_dummysw, dummy_kbd_configure);
