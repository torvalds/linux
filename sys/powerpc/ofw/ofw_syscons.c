/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Peter Grehan
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
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/limits.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/malloc.h>
#include <sys/fbio.h>
#include <sys/consio.h>

#include <machine/bus.h>
#include <machine/sc_machdep.h>
#include <machine/vm.h>

#include <sys/rman.h>

#include <dev/fb/fbreg.h>
#include <dev/syscons/syscons.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_pci.h>
#include <powerpc/ofw/ofw_syscons.h>

static int ofwfb_ignore_mmap_checks = 1;
static int ofwfb_reset_on_switch = 1;
static SYSCTL_NODE(_hw, OID_AUTO, ofwfb, CTLFLAG_RD, 0, "ofwfb");
SYSCTL_INT(_hw_ofwfb, OID_AUTO, relax_mmap, CTLFLAG_RW,
    &ofwfb_ignore_mmap_checks, 0, "relaxed mmap bounds checking");
SYSCTL_INT(_hw_ofwfb, OID_AUTO, reset_on_mode_switch, CTLFLAG_RW,
    &ofwfb_reset_on_switch, 0, "reset the framebuffer driver on mode switch");

extern u_char dflt_font_16[];
extern u_char dflt_font_14[];
extern u_char dflt_font_8[];

static int ofwfb_configure(int flags);

static vi_probe_t ofwfb_probe;
static vi_init_t ofwfb_init;
static vi_get_info_t ofwfb_get_info;
static vi_query_mode_t ofwfb_query_mode;
static vi_set_mode_t ofwfb_set_mode;
static vi_save_font_t ofwfb_save_font;
static vi_load_font_t ofwfb_load_font;
static vi_show_font_t ofwfb_show_font;
static vi_save_palette_t ofwfb_save_palette;
static vi_load_palette_t ofwfb_load_palette;
static vi_set_border_t ofwfb_set_border;
static vi_save_state_t ofwfb_save_state;
static vi_load_state_t ofwfb_load_state;
static vi_set_win_org_t ofwfb_set_win_org;
static vi_read_hw_cursor_t ofwfb_read_hw_cursor;
static vi_set_hw_cursor_t ofwfb_set_hw_cursor;
static vi_set_hw_cursor_shape_t ofwfb_set_hw_cursor_shape;
static vi_blank_display_t ofwfb_blank_display;
static vi_mmap_t ofwfb_mmap;
static vi_ioctl_t ofwfb_ioctl;
static vi_clear_t ofwfb_clear;
static vi_fill_rect_t ofwfb_fill_rect;
static vi_bitblt_t ofwfb_bitblt;
static vi_diag_t ofwfb_diag;
static vi_save_cursor_palette_t ofwfb_save_cursor_palette;
static vi_load_cursor_palette_t ofwfb_load_cursor_palette;
static vi_copy_t ofwfb_copy;
static vi_putp_t ofwfb_putp;
static vi_putc_t ofwfb_putc;
static vi_puts_t ofwfb_puts;
static vi_putm_t ofwfb_putm;

static video_switch_t ofwfbvidsw = {
	.probe			= ofwfb_probe,
	.init			= ofwfb_init,
	.get_info		= ofwfb_get_info,
	.query_mode		= ofwfb_query_mode,
	.set_mode		= ofwfb_set_mode,
	.save_font		= ofwfb_save_font,
	.load_font		= ofwfb_load_font,
	.show_font		= ofwfb_show_font,
	.save_palette		= ofwfb_save_palette,
	.load_palette		= ofwfb_load_palette,
	.set_border		= ofwfb_set_border,
	.save_state		= ofwfb_save_state,
	.load_state		= ofwfb_load_state,
	.set_win_org		= ofwfb_set_win_org,
	.read_hw_cursor		= ofwfb_read_hw_cursor,
	.set_hw_cursor		= ofwfb_set_hw_cursor,
	.set_hw_cursor_shape	= ofwfb_set_hw_cursor_shape,
	.blank_display		= ofwfb_blank_display,
	.mmap			= ofwfb_mmap,
	.ioctl			= ofwfb_ioctl,
	.clear			= ofwfb_clear,
	.fill_rect		= ofwfb_fill_rect,
	.bitblt			= ofwfb_bitblt,
	.diag			= ofwfb_diag,
	.save_cursor_palette	= ofwfb_save_cursor_palette,
	.load_cursor_palette	= ofwfb_load_cursor_palette,
	.copy			= ofwfb_copy,
	.putp			= ofwfb_putp,
	.putc			= ofwfb_putc,
	.puts			= ofwfb_puts,
	.putm			= ofwfb_putm,
};

/*
 * bitmap depth-specific routines
 */
static vi_blank_display_t ofwfb_blank_display8;
static vi_putc_t ofwfb_putc8;
static vi_putm_t ofwfb_putm8;
static vi_set_border_t ofwfb_set_border8;

static vi_blank_display_t ofwfb_blank_display32;
static vi_putc_t ofwfb_putc32;
static vi_putm_t ofwfb_putm32;
static vi_set_border_t ofwfb_set_border32;

VIDEO_DRIVER(ofwfb, ofwfbvidsw, ofwfb_configure);

extern sc_rndr_sw_t txtrndrsw;
RENDERER(ofwfb, 0, txtrndrsw, gfb_set);

RENDERER_MODULE(ofwfb, gfb_set);

/*
 * Define the iso6429-1983 colormap
 */
static struct {
	uint8_t	red;
	uint8_t	green;
	uint8_t	blue;
} ofwfb_cmap[16] = {		/*  #     R    G    B   Color */
				/*  -     -    -    -   ----- */
	{ 0x00, 0x00, 0x00 },	/*  0     0    0    0   Black */
	{ 0x00, 0x00, 0xaa },	/*  1     0    0  2/3   Blue  */
	{ 0x00, 0xaa, 0x00 },	/*  2     0  2/3    0   Green */
	{ 0x00, 0xaa, 0xaa },	/*  3     0  2/3  2/3   Cyan  */
	{ 0xaa, 0x00, 0x00 },	/*  4   2/3    0    0   Red   */
	{ 0xaa, 0x00, 0xaa },	/*  5   2/3    0  2/3   Magenta */
	{ 0xaa, 0x55, 0x00 },	/*  6   2/3  1/3    0   Brown */
	{ 0xaa, 0xaa, 0xaa },	/*  7   2/3  2/3  2/3   White */
        { 0x55, 0x55, 0x55 },	/*  8   1/3  1/3  1/3   Gray  */
	{ 0x55, 0x55, 0xff },	/*  9   1/3  1/3    1   Bright Blue  */
	{ 0x55, 0xff, 0x55 },	/* 10   1/3    1  1/3   Bright Green */
	{ 0x55, 0xff, 0xff },	/* 11   1/3    1    1   Bright Cyan  */
	{ 0xff, 0x55, 0x55 },	/* 12     1  1/3  1/3   Bright Red   */
	{ 0xff, 0x55, 0xff },	/* 13     1  1/3    1   Bright Magenta */
	{ 0xff, 0xff, 0x80 },	/* 14     1    1  1/3   Bright Yellow */
	{ 0xff, 0xff, 0xff }	/* 15     1    1    1   Bright White */
};

#define	TODO	printf("%s: unimplemented\n", __func__)

static u_int16_t ofwfb_static_window[ROW*COL];

static struct ofwfb_softc ofwfb_softc;

static __inline int
ofwfb_background(uint8_t attr)
{
	return (attr >> 4);
}

static __inline int
ofwfb_foreground(uint8_t attr)
{
	return (attr & 0x0f);
}

static u_int
ofwfb_pix32(struct ofwfb_softc *sc, int attr)
{
	u_int retval;

	if (sc->sc_tag == &bs_le_tag)
		retval = (ofwfb_cmap[attr].red << 16) |
			(ofwfb_cmap[attr].green << 8) |
			ofwfb_cmap[attr].blue;
	else
		retval = (ofwfb_cmap[attr].blue  << 16) |
			(ofwfb_cmap[attr].green << 8) |
			ofwfb_cmap[attr].red;

	return (retval);
}

static int
ofwfb_configure(int flags)
{
	struct ofwfb_softc *sc;
        phandle_t chosen;
        ihandle_t stdout;
	phandle_t node;
	uint32_t fb_phys;
	int depth;
	int disable;
	int len;
	int i;
	char type[16];
	static int done = 0;

	disable = 0;
	TUNABLE_INT_FETCH("hw.syscons.disable", &disable);
	if (disable != 0)
		return (0);

	if (done != 0)
		return (0);
	done = 1;

	sc = &ofwfb_softc;

	chosen = OF_finddevice("/chosen");
	OF_getprop(chosen, "stdout", &stdout, sizeof(stdout));
        node = OF_instance_to_package(stdout);
	if (node == -1) {
		/*
		 * The "/chosen/stdout" does not exist try
		 * using "screen" directly.
		 */
		node = OF_finddevice("screen");
	}
	OF_getprop(node, "device_type", type, sizeof(type));
	if (strcmp(type, "display") != 0)
		return (0);

	/* Only support 8 and 32-bit framebuffers */
	OF_getprop(node, "depth", &depth, sizeof(depth));
	if (depth == 8) {
		sc->sc_blank = ofwfb_blank_display8;
		sc->sc_putc = ofwfb_putc8;
		sc->sc_putm = ofwfb_putm8;
		sc->sc_set_border = ofwfb_set_border8;
	} else if (depth == 32) {
		sc->sc_blank = ofwfb_blank_display32;
		sc->sc_putc = ofwfb_putc32;
		sc->sc_putm = ofwfb_putm32;
		sc->sc_set_border = ofwfb_set_border32;
	} else
		return (0);

	if (OF_getproplen(node, "height") != sizeof(sc->sc_height) ||
	    OF_getproplen(node, "width") != sizeof(sc->sc_width) ||
	    OF_getproplen(node, "linebytes") != sizeof(sc->sc_stride))
		return (0);

	sc->sc_depth = depth;
	sc->sc_node = node;
	sc->sc_console = 1;
	OF_getprop(node, "height", &sc->sc_height, sizeof(sc->sc_height));
	OF_getprop(node, "width", &sc->sc_width, sizeof(sc->sc_width));
	OF_getprop(node, "linebytes", &sc->sc_stride, sizeof(sc->sc_stride));

	/*
	 * Get the PCI addresses of the adapter. The node may be the
	 * child of the PCI device: in that case, try the parent for
	 * the assigned-addresses property.
	 */
	len = OF_getprop(node, "assigned-addresses", sc->sc_pciaddrs,
	          sizeof(sc->sc_pciaddrs));
	if (len == -1) {
		len = OF_getprop(OF_parent(node), "assigned-addresses",
		    sc->sc_pciaddrs, sizeof(sc->sc_pciaddrs));
	}
	if (len == -1)
		len = 0;
	sc->sc_num_pciaddrs = len / sizeof(struct ofw_pci_register);

	/*
	 * Grab the physical address of the framebuffer, and then map it
	 * into our memory space. If the MMU is not yet up, it will be
	 * remapped for us when relocation turns on.
	 *
	 * XXX We assume #address-cells is 1 at this point.
	 */
	if (OF_getproplen(node, "address") == sizeof(fb_phys)) {
		OF_getprop(node, "address", &fb_phys, sizeof(fb_phys));
		sc->sc_tag = &bs_be_tag;
		bus_space_map(sc->sc_tag, fb_phys, sc->sc_height *
		    sc->sc_stride, BUS_SPACE_MAP_PREFETCHABLE, &sc->sc_addr);
	} else {
		/*
		 * Some IBM systems don't have an address property. Try to
		 * guess the framebuffer region from the assigned addresses.
		 * This is ugly, but there doesn't seem to be an alternative.
		 * Linux does the same thing.
		 */

		fb_phys = sc->sc_num_pciaddrs;
		for (i = 0; i < sc->sc_num_pciaddrs; i++) {
			/* If it is too small, not the framebuffer */
			if (sc->sc_pciaddrs[i].size_lo <
			    sc->sc_stride*sc->sc_height)
				continue;
			/* If it is not memory, it isn't either */
			if (!(sc->sc_pciaddrs[i].phys_hi &
			    OFW_PCI_PHYS_HI_SPACE_MEM32))
				continue;

			/* This could be the framebuffer */
			fb_phys = i;

			/* If it is prefetchable, it certainly is */
			if (sc->sc_pciaddrs[i].phys_hi &
			    OFW_PCI_PHYS_HI_PREFETCHABLE)
				break;
		}
		if (fb_phys == sc->sc_num_pciaddrs)
			return (0);

		OF_decode_addr(node, fb_phys, &sc->sc_tag, &sc->sc_addr, NULL);
	}

	ofwfb_init(0, &sc->sc_va, 0);

	return (0);
}

static int
ofwfb_probe(int unit, video_adapter_t **adp, void *arg, int flags)
{
	TODO;
	return (0);
}

static int
ofwfb_init(int unit, video_adapter_t *adp, int flags)
{
	struct ofwfb_softc *sc;
	video_info_t *vi;
	int cborder;
	int font_height;

	sc = (struct ofwfb_softc *)adp;
	vi = &adp->va_info;

	vid_init_struct(adp, "ofwfb", -1, unit);

	/* The default font size can be overridden by loader */
	font_height = 16;
	TUNABLE_INT_FETCH("hw.syscons.fsize", &font_height);
	if (font_height == 8) {
		sc->sc_font = dflt_font_8;
		sc->sc_font_height = 8;
	} else if (font_height == 14) {
		sc->sc_font = dflt_font_14;
		sc->sc_font_height = 14;
	} else {
		/* default is 8x16 */
		sc->sc_font = dflt_font_16;
		sc->sc_font_height = 16;
	}

	/* The user can set a border in chars - default is 1 char width */
	cborder = 1;
	TUNABLE_INT_FETCH("hw.syscons.border", &cborder);

	vi->vi_cheight = sc->sc_font_height;
	vi->vi_width = sc->sc_width/8 - 2*cborder;
	vi->vi_height = sc->sc_height/sc->sc_font_height - 2*cborder;
	vi->vi_cwidth = 8;

	/*
	 * Clamp width/height to syscons maximums
	 */
	if (vi->vi_width > COL)
		vi->vi_width = COL;
	if (vi->vi_height > ROW)
		vi->vi_height = ROW;

	sc->sc_xmargin = (sc->sc_width - (vi->vi_width * vi->vi_cwidth)) / 2;
	sc->sc_ymargin = (sc->sc_height - (vi->vi_height * vi->vi_cheight))/2;

	/*
	 * Avoid huge amounts of conditional code in syscons by
	 * defining a dummy h/w text display buffer.
	 */
	adp->va_window = (vm_offset_t) ofwfb_static_window;

	/*
	 * Enable future font-loading and flag color support, as well as
	 * adding V_ADP_MODECHANGE so that we ofwfb_set_mode() gets called
	 * when the X server shuts down. This enables us to get the console
	 * back when X disappears.
	 */
	adp->va_flags |= V_ADP_FONT | V_ADP_COLOR | V_ADP_MODECHANGE;

	ofwfb_set_mode(&sc->sc_va, 0);

	vid_register(&sc->sc_va);

	return (0);
}

static int
ofwfb_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{
	bcopy(&adp->va_info, info, sizeof(*info));
	return (0);
}

static int
ofwfb_query_mode(video_adapter_t *adp, video_info_t *info)
{
	TODO;
	return (0);
}

static int
ofwfb_set_mode(video_adapter_t *adp, int mode)
{
	struct ofwfb_softc *sc;
	char name[64];
	ihandle_t ih;
	int i, retval;

	sc = (struct ofwfb_softc *)adp;

	if (ofwfb_reset_on_switch) {
		/*
		 * Open the display device, which will initialize it.
		 */

		memset(name, 0, sizeof(name));
		OF_package_to_path(sc->sc_node, name, sizeof(name));
		ih = OF_open(name);

		if (sc->sc_depth == 8) {
			/*
			 * Install the ISO6429 colormap - older OFW systems
			 * don't do this by default
			 */
			for (i = 0; i < 16; i++) {
				OF_call_method("color!", ih, 4, 1,
						   ofwfb_cmap[i].red,
						   ofwfb_cmap[i].green,
						   ofwfb_cmap[i].blue,
						   i,
						   &retval);
			}
		}
	}

	ofwfb_blank_display(&sc->sc_va, V_DISPLAY_ON);

	return (0);
}

static int
ofwfb_save_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{
	TODO;
	return (0);
}

static int
ofwfb_load_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{
	struct ofwfb_softc *sc;

	sc = (struct ofwfb_softc *)adp;

	/*
	 * syscons code has already determined that current width/height
	 * are unchanged for this new font
	 */
	sc->sc_font = data;
	return (0);
}

static int
ofwfb_show_font(video_adapter_t *adp, int page)
{

	return (0);
}

static int
ofwfb_save_palette(video_adapter_t *adp, u_char *palette)
{
	/* TODO; */
	return (0);
}

static int
ofwfb_load_palette(video_adapter_t *adp, u_char *palette)
{
	/* TODO; */
	return (0);
}

static int
ofwfb_set_border8(video_adapter_t *adp, int border)
{
	struct ofwfb_softc *sc;
	int i, j;
	uint8_t *addr;
	uint8_t bground;

	sc = (struct ofwfb_softc *)adp;

	bground = ofwfb_background(border);

	/* Set top margin */
	addr = (uint8_t *) sc->sc_addr;
	for (i = 0; i < sc->sc_ymargin; i++) {
		for (j = 0; j < sc->sc_width; j++) {
			*(addr + j) = bground;
		}
		addr += sc->sc_stride;
	}

	/* bottom margin */
	addr = (uint8_t *) sc->sc_addr + (sc->sc_height - sc->sc_ymargin)*sc->sc_stride;
	for (i = 0; i < sc->sc_ymargin; i++) {
		for (j = 0; j < sc->sc_width; j++) {
			*(addr + j) = bground;
		}
		addr += sc->sc_stride;
	}

	/* remaining left and right borders */
	addr = (uint8_t *) sc->sc_addr + sc->sc_ymargin*sc->sc_stride;
	for (i = 0; i < sc->sc_height - 2*sc->sc_xmargin; i++) {
		for (j = 0; j < sc->sc_xmargin; j++) {
			*(addr + j) = bground;
			*(addr + j + sc->sc_width - sc->sc_xmargin) = bground;
		}
		addr += sc->sc_stride;
	}

	return (0);
}

static int
ofwfb_set_border32(video_adapter_t *adp, int border)
{
	/* XXX Be lazy for now and blank entire screen */
	return (ofwfb_blank_display32(adp, border));
}

static int
ofwfb_set_border(video_adapter_t *adp, int border)
{
	struct ofwfb_softc *sc;

	sc = (struct ofwfb_softc *)adp;

	return ((*sc->sc_set_border)(adp, border));
}

static int
ofwfb_save_state(video_adapter_t *adp, void *p, size_t size)
{
	TODO;
	return (0);
}

static int
ofwfb_load_state(video_adapter_t *adp, void *p)
{
	TODO;
	return (0);
}

static int
ofwfb_set_win_org(video_adapter_t *adp, off_t offset)
{
	TODO;
	return (0);
}

static int
ofwfb_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{
	*col = 0;
	*row = 0;

	return (0);
}

static int
ofwfb_set_hw_cursor(video_adapter_t *adp, int col, int row)
{

	return (0);
}

static int
ofwfb_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
    int celsize, int blink)
{
	return (0);
}

static int
ofwfb_blank_display8(video_adapter_t *adp, int mode)
{
	struct ofwfb_softc *sc;
	int i;
	uint32_t *addr;
	uint32_t color;
	uint32_t end;

	sc = (struct ofwfb_softc *)adp;
	addr = (uint32_t *) sc->sc_addr;
	end = (sc->sc_stride/4) * sc->sc_height;

	/* Splat 4 pixels at once. */
	color = (ofwfb_background(SC_NORM_ATTR) << 24) |
	    (ofwfb_background(SC_NORM_ATTR) << 16) |
	    (ofwfb_background(SC_NORM_ATTR) << 8) |
	    (ofwfb_background(SC_NORM_ATTR));

	for (i = 0; i < end; i++)
		*(addr + i) = color;

	return (0);
}

static int
ofwfb_blank_display32(video_adapter_t *adp, int mode)
{
	struct ofwfb_softc *sc;
	int i;
	uint32_t *addr, blank;

	sc = (struct ofwfb_softc *)adp;
	addr = (uint32_t *) sc->sc_addr;
	blank = ofwfb_pix32(sc, ofwfb_background(SC_NORM_ATTR));

	for (i = 0; i < (sc->sc_stride/4)*sc->sc_height; i++)
		*(addr + i) = blank;

	return (0);
}

static int
ofwfb_blank_display(video_adapter_t *adp, int mode)
{
	struct ofwfb_softc *sc;

	sc = (struct ofwfb_softc *)adp;

	return ((*sc->sc_blank)(adp, mode));
}

static int
ofwfb_mmap(video_adapter_t *adp, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	struct ofwfb_softc *sc;
	int i;

	sc = (struct ofwfb_softc *)adp;

	/*
	 * Make sure the requested address lies within the PCI device's
	 * assigned addrs
	 */
	for (i = 0; i < sc->sc_num_pciaddrs; i++)
	  if (offset >= sc->sc_pciaddrs[i].phys_lo &&
	    offset < (sc->sc_pciaddrs[i].phys_lo + sc->sc_pciaddrs[i].size_lo))
		{
			/*
			 * If this is a prefetchable BAR, we can (and should)
			 * enable write-combining.
			 */
			if (sc->sc_pciaddrs[i].phys_hi &
			    OFW_PCI_PHYS_HI_PREFETCHABLE)
				*memattr = VM_MEMATTR_WRITE_COMBINING;

			*paddr = offset;
			return (0);
		}

	/*
	 * Hack for Radeon...
	 */
	if (ofwfb_ignore_mmap_checks) {
		*paddr = offset;
		return (0);
	}

	/*
	 * This might be a legacy VGA mem request: if so, just point it at the
	 * framebuffer, since it shouldn't be touched
	 */
	if (offset < sc->sc_stride*sc->sc_height) {
		*paddr = sc->sc_addr + offset;
		return (0);
	}

	/*
	 * Error if we didn't have a better idea.
	 */
	if (sc->sc_num_pciaddrs == 0)
		return (ENOMEM);

	return (EINVAL);
}

static int
ofwfb_ioctl(video_adapter_t *adp, u_long cmd, caddr_t data)
{

	return (0);
}

static int
ofwfb_clear(video_adapter_t *adp)
{
	TODO;
	return (0);
}

static int
ofwfb_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{
	TODO;
	return (0);
}

static int
ofwfb_bitblt(video_adapter_t *adp, ...)
{
	TODO;
	return (0);
}

static int
ofwfb_diag(video_adapter_t *adp, int level)
{
	TODO;
	return (0);
}

static int
ofwfb_save_cursor_palette(video_adapter_t *adp, u_char *palette)
{
	TODO;
	return (0);
}

static int
ofwfb_load_cursor_palette(video_adapter_t *adp, u_char *palette)
{
	TODO;
	return (0);
}

static int
ofwfb_copy(video_adapter_t *adp, vm_offset_t src, vm_offset_t dst, int n)
{
	TODO;
	return (0);
}

static int
ofwfb_putp(video_adapter_t *adp, vm_offset_t off, uint32_t p, uint32_t a,
    int size, int bpp, int bit_ltor, int byte_ltor)
{
	TODO;
	return (0);
}

static int
ofwfb_putc8(video_adapter_t *adp, vm_offset_t off, uint8_t c, uint8_t a)
{
	struct ofwfb_softc *sc;
	int row;
	int col;
	int i;
	uint32_t *addr;
	u_char *p, fg, bg;
	union {
		uint32_t l;
		uint8_t  c[4];
	} ch1, ch2;


	sc = (struct ofwfb_softc *)adp;
        row = (off / adp->va_info.vi_width) * adp->va_info.vi_cheight;
        col = (off % adp->va_info.vi_width) * adp->va_info.vi_cwidth;
	p = sc->sc_font + c*sc->sc_font_height;
	addr = (u_int32_t *)((uintptr_t)sc->sc_addr
		+ (row + sc->sc_ymargin)*sc->sc_stride
		+ col + sc->sc_xmargin);

	fg = ofwfb_foreground(a);
	bg = ofwfb_background(a);

	for (i = 0; i < sc->sc_font_height; i++) {
		u_char fline = p[i];

		/*
		 * Assume that there is more background than foreground
		 * in characters and init accordingly
		 */
		ch1.l = ch2.l = (bg << 24) | (bg << 16) | (bg << 8) | bg;

		/*
		 * Calculate 2 x 4-chars at a time, and then
		 * write these out.
		 */
		if (fline & 0x80) ch1.c[0] = fg;
		if (fline & 0x40) ch1.c[1] = fg;
		if (fline & 0x20) ch1.c[2] = fg;
		if (fline & 0x10) ch1.c[3] = fg;

		if (fline & 0x08) ch2.c[0] = fg;
		if (fline & 0x04) ch2.c[1] = fg;
		if (fline & 0x02) ch2.c[2] = fg;
		if (fline & 0x01) ch2.c[3] = fg;

		addr[0] = ch1.l;
		addr[1] = ch2.l;
		addr += (sc->sc_stride / sizeof(u_int32_t));
	}

	return (0);
}

static int
ofwfb_putc32(video_adapter_t *adp, vm_offset_t off, uint8_t c, uint8_t a)
{
	struct ofwfb_softc *sc;
	int row;
	int col;
	int i, j, k;
	uint32_t *addr, fg, bg;
	u_char *p;

	sc = (struct ofwfb_softc *)adp;
        row = (off / adp->va_info.vi_width) * adp->va_info.vi_cheight;
        col = (off % adp->va_info.vi_width) * adp->va_info.vi_cwidth;
	p = sc->sc_font + c*sc->sc_font_height;
	addr = (uint32_t *)sc->sc_addr
		+ (row + sc->sc_ymargin)*(sc->sc_stride/4)
		+ col + sc->sc_xmargin;

	fg = ofwfb_pix32(sc, ofwfb_foreground(a));
	bg = ofwfb_pix32(sc, ofwfb_background(a));

	for (i = 0; i < sc->sc_font_height; i++) {
		for (j = 0, k = 7; j < 8; j++, k--) {
			if ((p[i] & (1 << k)) == 0)
				*(addr + j) = bg;
			else
				*(addr + j) = fg;
		}
		addr += (sc->sc_stride/4);
	}

	return (0);
}

static int
ofwfb_putc(video_adapter_t *adp, vm_offset_t off, uint8_t c, uint8_t a)
{
	struct ofwfb_softc *sc;

	sc = (struct ofwfb_softc *)adp;

	return ((*sc->sc_putc)(adp, off, c, a));
}

static int
ofwfb_puts(video_adapter_t *adp, vm_offset_t off, u_int16_t *s, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		ofwfb_putc(adp, off + i, s[i] & 0xff, (s[i] & 0xff00) >> 8);
	}
	return (0);
}

static int
ofwfb_putm(video_adapter_t *adp, int x, int y, uint8_t *pixel_image,
    uint32_t pixel_mask, int size, int width)
{
	struct ofwfb_softc *sc;

	sc = (struct ofwfb_softc *)adp;

	return ((*sc->sc_putm)(adp, x, y, pixel_image, pixel_mask, size,
	    width));
}

static int
ofwfb_putm8(video_adapter_t *adp, int x, int y, uint8_t *pixel_image,
    uint32_t pixel_mask, int size, int width)
{
	struct ofwfb_softc *sc;
	int i, j, k;
	uint8_t *addr;
	u_char fg, bg;

	sc = (struct ofwfb_softc *)adp;
	addr = (u_int8_t *)((uintptr_t)sc->sc_addr
		+ (y + sc->sc_ymargin)*sc->sc_stride
		+ x + sc->sc_xmargin);

	fg = ofwfb_foreground(SC_NORM_ATTR);
	bg = ofwfb_background(SC_NORM_ATTR);

	for (i = 0; i < size && i+y < sc->sc_height - 2*sc->sc_ymargin; i++) {
		/*
		 * Calculate 2 x 4-chars at a time, and then
		 * write these out.
		 */
		for (j = 0, k = width; j < 8; j++, k--) {
			if (x + j >= sc->sc_width - 2*sc->sc_xmargin)
				continue;

			if (pixel_image[i] & (1 << k))
				addr[j] = (addr[j] == fg) ? bg : fg;
		}

		addr += (sc->sc_stride / sizeof(u_int8_t));
	}

	return (0);
}

static int
ofwfb_putm32(video_adapter_t *adp, int x, int y, uint8_t *pixel_image,
    uint32_t pixel_mask, int size, int width)
{
	struct ofwfb_softc *sc;
	int i, j, k;
	uint32_t fg, bg;
	uint32_t *addr;

	sc = (struct ofwfb_softc *)adp;
	addr = (uint32_t *)sc->sc_addr
		+ (y + sc->sc_ymargin)*(sc->sc_stride/4)
		+ x + sc->sc_xmargin;

	fg = ofwfb_pix32(sc, ofwfb_foreground(SC_NORM_ATTR));
	bg = ofwfb_pix32(sc, ofwfb_background(SC_NORM_ATTR));

	for (i = 0; i < size && i+y < sc->sc_height - 2*sc->sc_ymargin; i++) {
		for (j = 0, k = width; j < 8; j++, k--) {
			if (x + j >= sc->sc_width - 2*sc->sc_xmargin)
				continue;

			if (pixel_image[i] & (1 << k))
				*(addr + j) = (*(addr + j) == fg) ? bg : fg;
		}
		addr += (sc->sc_stride/4);
	}

	return (0);
}

/*
 * Define the syscons nexus device attachment
 */
static void
ofwfb_scidentify(driver_t *driver, device_t parent)
{
	device_t child;

	/*
	 * Add with a priority guaranteed to make it last on
	 * the device list
	 */
	child = BUS_ADD_CHILD(parent, INT_MAX, SC_DRIVER_NAME, 0);
}

static int
ofwfb_scprobe(device_t dev)
{
	int error;

	device_set_desc(dev, "System console");

	error = sc_probe_unit(device_get_unit(dev),
	    device_get_flags(dev) | SC_AUTODETECT_KBD);
	if (error != 0)
		return (error);

	/* This is a fake device, so make sure we added it ourselves */
	return (BUS_PROBE_NOWILDCARD);
}

static int
ofwfb_scattach(device_t dev)
{
	return (sc_attach_unit(device_get_unit(dev),
	    device_get_flags(dev) | SC_AUTODETECT_KBD));
}

static device_method_t ofwfb_sc_methods[] = {
  	DEVMETHOD(device_identify,	ofwfb_scidentify),
	DEVMETHOD(device_probe,		ofwfb_scprobe),
	DEVMETHOD(device_attach,	ofwfb_scattach),
	{ 0, 0 }
};

static driver_t ofwfb_sc_driver = {
	SC_DRIVER_NAME,
	ofwfb_sc_methods,
	sizeof(sc_softc_t),
};

static devclass_t	sc_devclass;

DRIVER_MODULE(ofwfb, nexus, ofwfb_sc_driver, sc_devclass, 0, 0);

/*
 * Define a stub keyboard driver in case one hasn't been
 * compiled into the kernel
 */
#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>

static int dummy_kbd_configure(int flags);

keyboard_switch_t dummysw;

static int
dummy_kbd_configure(int flags)
{

	return (0);
}
KEYBOARD_DRIVER(dummy, dummysw, dummy_kbd_configure);

/*
 * Utility routines from <dev/fb/fbreg.h>
 */
void
ofwfb_bcopy(const void *s, void *d, size_t c)
{
	bcopy(s, d, c);
}

void
ofwfb_bzero(void *d, size_t c)
{
	bzero(d, c);
}

void
ofwfb_fillw(int pat, void *base, size_t cnt)
{
	u_int16_t *bptr = base;

	while (cnt--)
		*bptr++ = pat;
}

u_int16_t
ofwfb_readw(u_int16_t *addr)
{
	return (*addr);
}

void
ofwfb_writew(u_int16_t *addr, u_int16_t val)
{
	*addr = val;
}
