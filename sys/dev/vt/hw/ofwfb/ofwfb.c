/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Nathan Whitehorn
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
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/fbio.h>

#include <dev/vt/vt.h>
#include <dev/vt/hw/fb/vt_fb.h>
#include <dev/vt/colors/vt_termcolors.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#ifdef __sparc64__
#include <machine/bus_private.h>
#endif
#include <machine/cpu.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_pci.h>

struct ofwfb_softc {
	struct fb_info	fb;

	phandle_t	sc_node;
	ihandle_t	sc_handle;
	bus_space_tag_t	sc_memt;
	int		iso_palette;
};

static void ofwfb_initialize(struct vt_device *vd);
static vd_probe_t	ofwfb_probe;
static vd_init_t	ofwfb_init;
static vd_bitblt_text_t	ofwfb_bitblt_text;
static vd_bitblt_bmp_t	ofwfb_bitblt_bitmap;

static const struct vt_driver vt_ofwfb_driver = {
	.vd_name	= "ofwfb",
	.vd_probe	= ofwfb_probe,
	.vd_init	= ofwfb_init,
	.vd_blank	= vt_fb_blank,
	.vd_bitblt_text	= ofwfb_bitblt_text,
	.vd_bitblt_bmp	= ofwfb_bitblt_bitmap,
	.vd_fb_ioctl	= vt_fb_ioctl,
	.vd_fb_mmap	= vt_fb_mmap,
	.vd_priority	= VD_PRIORITY_GENERIC+1,
};

static unsigned char ofw_colors[16] = {
	/* See "16-color Text Extension" Open Firmware document, page 4 */
	0, 4, 2, 6, 1, 5, 3, 7,
	8, 12, 10, 14, 9, 13, 11, 15
};

static struct ofwfb_softc ofwfb_conssoftc;
VT_DRIVER_DECLARE(vt_ofwfb, vt_ofwfb_driver);

static int
ofwfb_probe(struct vt_device *vd)
{
	phandle_t chosen, node;
	ihandle_t stdout;
	char buf[64];

	chosen = OF_finddevice("/chosen");
	if (chosen == -1)
		return (CN_DEAD);

	node = -1;
	if (OF_getprop(chosen, "stdout", &stdout, sizeof(stdout)) ==
	    sizeof(stdout))
		node = OF_instance_to_package(stdout);
	if (node == -1)
		if (OF_getprop(chosen, "stdout-path", buf, sizeof(buf)) > 0)
			node = OF_finddevice(buf);
	if (node == -1) {
		/*
		 * The "/chosen/stdout" does not exist try
		 * using "screen" directly.
		 */
		node = OF_finddevice("screen");
	}
	OF_getprop(node, "device_type", buf, sizeof(buf));
	if (strcmp(buf, "display") != 0)
		return (CN_DEAD);

	/* Looks OK... */
	return (CN_INTERNAL);
}

static void
ofwfb_bitblt_bitmap(struct vt_device *vd, const struct vt_window *vw,
    const uint8_t *pattern, const uint8_t *mask,
    unsigned int width, unsigned int height,
    unsigned int x, unsigned int y, term_color_t fg, term_color_t bg)
{
	struct fb_info *sc = vd->vd_softc;
	u_long line;
	uint32_t fgc, bgc;
	int c, l;
	uint8_t b, m;
	union {
		uint32_t l;
		uint8_t	 c[4];
	} ch1, ch2;

#ifdef __powerpc__
	/* Deal with unmapped framebuffers */
	if (sc->fb_flags & FB_FLAG_NOWRITE) {
		if (pmap_bootstrapped) {
			sc->fb_flags &= ~FB_FLAG_NOWRITE;
			ofwfb_initialize(vd);
			vd->vd_driver->vd_blank(vd, TC_BLACK);
		} else {
			return;
		}
	}
#endif

	fgc = sc->fb_cmap[fg];
	bgc = sc->fb_cmap[bg];
	b = m = 0;

	if (((struct ofwfb_softc *)vd->vd_softc)->iso_palette) {
		fg = ofw_colors[fg];
		bg = ofw_colors[bg];
	}

	line = (sc->fb_stride * y) + x * sc->fb_bpp/8;
	if (mask == NULL && sc->fb_bpp == 8 && (width % 8 == 0)) {
		/* Don't try to put off screen pixels */
		if (((x + width) > vd->vd_width) || ((y + height) >
		    vd->vd_height))
			return;

		for (; height > 0; height--) {
			for (c = 0; c < width; c += 8) {
				b = *pattern++;

				/*
				 * Assume that there is more background than
				 * foreground in characters and init accordingly
				 */
				ch1.l = ch2.l = (bg << 24) | (bg << 16) |
				    (bg << 8) | bg;

				/*
				 * Calculate 2 x 4-chars at a time, and then
				 * write these out.
				 */
				if (b & 0x80) ch1.c[0] = fg;
				if (b & 0x40) ch1.c[1] = fg;
				if (b & 0x20) ch1.c[2] = fg;
				if (b & 0x10) ch1.c[3] = fg;

				if (b & 0x08) ch2.c[0] = fg;
				if (b & 0x04) ch2.c[1] = fg;
				if (b & 0x02) ch2.c[2] = fg;
				if (b & 0x01) ch2.c[3] = fg;

				*(uint32_t *)(sc->fb_vbase + line + c) = ch1.l;
				*(uint32_t *)(sc->fb_vbase + line + c + 4) =
				    ch2.l;
			}
			line += sc->fb_stride;
		}
	} else {
		for (l = 0;
		    l < height && y + l < vw->vw_draw_area.tr_end.tp_row;
		    l++) {
			for (c = 0;
			    c < width && x + c < vw->vw_draw_area.tr_end.tp_col;
			    c++) {
				if (c % 8 == 0)
					b = *pattern++;
				else
					b <<= 1;
				if (mask != NULL) {
					if (c % 8 == 0)
						m = *mask++;
					else
						m <<= 1;
					/* Skip pixel write, if mask not set. */
					if ((m & 0x80) == 0)
						continue;
				}
				switch(sc->fb_bpp) {
				case 8:
					*(uint8_t *)(sc->fb_vbase + line + c) =
					    b & 0x80 ? fg : bg;
					break;
				case 32:
					*(uint32_t *)(sc->fb_vbase + line + 4*c)
					    = (b & 0x80) ? fgc : bgc;
					break;
				default:
					/* panic? */
					break;
				}
			}
			line += sc->fb_stride;
		}
	}
}

void
ofwfb_bitblt_text(struct vt_device *vd, const struct vt_window *vw,
    const term_rect_t *area)
{
	unsigned int col, row, x, y;
	struct vt_font *vf;
	term_char_t c;
	term_color_t fg, bg;
	const uint8_t *pattern;

	vf = vw->vw_font;

	for (row = area->tr_begin.tp_row; row < area->tr_end.tp_row; ++row) {
		for (col = area->tr_begin.tp_col; col < area->tr_end.tp_col;
		    ++col) {
			x = col * vf->vf_width +
			    vw->vw_draw_area.tr_begin.tp_col;
			y = row * vf->vf_height +
			    vw->vw_draw_area.tr_begin.tp_row;

			c = VTBUF_GET_FIELD(&vw->vw_buf, row, col);
			pattern = vtfont_lookup(vf, c);
			vt_determine_colors(c,
			    VTBUF_ISCURSOR(&vw->vw_buf, row, col), &fg, &bg);

			ofwfb_bitblt_bitmap(vd, vw,
			    pattern, NULL, vf->vf_width, vf->vf_height,
			    x, y, fg, bg);
		}
	}

#ifndef SC_NO_CUTPASTE
	if (!vd->vd_mshown)
		return;

	term_rect_t drawn_area;

	drawn_area.tr_begin.tp_col = area->tr_begin.tp_col * vf->vf_width;
	drawn_area.tr_begin.tp_row = area->tr_begin.tp_row * vf->vf_height;
	drawn_area.tr_end.tp_col = area->tr_end.tp_col * vf->vf_width;
	drawn_area.tr_end.tp_row = area->tr_end.tp_row * vf->vf_height;

	if (vt_is_cursor_in_area(vd, &drawn_area)) {
		ofwfb_bitblt_bitmap(vd, vw,
		    vd->vd_mcursor->map, vd->vd_mcursor->mask,
		    vd->vd_mcursor->width, vd->vd_mcursor->height,
		    vd->vd_mx_drawn + vw->vw_draw_area.tr_begin.tp_col,
		    vd->vd_my_drawn + vw->vw_draw_area.tr_begin.tp_row,
		    vd->vd_mcursor_fg, vd->vd_mcursor_bg);
	}
#endif
}

static void
ofwfb_initialize(struct vt_device *vd)
{
	struct ofwfb_softc *sc = vd->vd_softc;
	int i, err;
	cell_t retval;
	uint32_t oldpix;

	sc->fb.fb_cmsize = 16;

	if (sc->fb.fb_flags & FB_FLAG_NOWRITE)
		return;

	/*
	 * Set up the color map
	 */

	sc->iso_palette = 0;
	switch (sc->fb.fb_bpp) {
	case 8:
		vt_generate_cons_palette(sc->fb.fb_cmap, COLOR_FORMAT_RGB, 255,
		    16, 255, 8, 255, 0);

		for (i = 0; i < 16; i++) {
			err = OF_call_method("color!", sc->sc_handle, 4, 1,
			    (cell_t)((sc->fb.fb_cmap[i] >> 16) & 0xff),
			    (cell_t)((sc->fb.fb_cmap[i] >> 8) & 0xff),
			    (cell_t)((sc->fb.fb_cmap[i] >> 0) & 0xff),
			    (cell_t)i, &retval);
			if (err)
				break;
		}
		if (i != 16)
			sc->iso_palette = 1;

		break;

	case 32:
		/*
		 * We bypass the usual bus_space_() accessors here, mostly
		 * for performance reasons. In particular, we don't want
		 * any barrier operations that may be performed and handle
		 * endianness slightly different. Figure out the host-view
		 * endianness of the frame buffer.
		 */
		oldpix = bus_space_read_4(sc->sc_memt, sc->fb.fb_vbase, 0);
		bus_space_write_4(sc->sc_memt, sc->fb.fb_vbase, 0, 0xff000000);
		if (*(uint8_t *)(sc->fb.fb_vbase) == 0xff)
			vt_generate_cons_palette(sc->fb.fb_cmap,
			    COLOR_FORMAT_RGB, 255, 0, 255, 8, 255, 16);
		else
			vt_generate_cons_palette(sc->fb.fb_cmap,
			    COLOR_FORMAT_RGB, 255, 16, 255, 8, 255, 0);
		bus_space_write_4(sc->sc_memt, sc->fb.fb_vbase, 0, oldpix);
		break;

	default:
		panic("Unknown color space depth %d", sc->fb.fb_bpp);
		break;
        }
}

static int
ofwfb_init(struct vt_device *vd)
{
	struct ofwfb_softc *sc;
	char buf[64];
	phandle_t chosen;
	phandle_t node;
	uint32_t depth, height, width, stride;
	uint32_t fb_phys;
	int i, len;
#ifdef __sparc64__
	static struct bus_space_tag ofwfb_memt[1];
	bus_addr_t phys;
	int space;
#endif

	/* Initialize softc */
	vd->vd_softc = sc = &ofwfb_conssoftc;

	node = -1;
	chosen = OF_finddevice("/chosen");
	if (OF_getprop(chosen, "stdout", &sc->sc_handle,
	    sizeof(ihandle_t)) == sizeof(ihandle_t))
		node = OF_instance_to_package(sc->sc_handle);
	if (node == -1)
		/* Try "/chosen/stdout-path" now */
		if (OF_getprop(chosen, "stdout-path", buf, sizeof(buf)) > 0) {
			node = OF_finddevice(buf);
			if (node != -1)
				sc->sc_handle = OF_open(buf);
		}
	if (node == -1) {
		/*
		 * The "/chosen/stdout" does not exist try
		 * using "screen" directly.
		 */
		node = OF_finddevice("screen");
		sc->sc_handle = OF_open("screen");
	}
	OF_getprop(node, "device_type", buf, sizeof(buf));
	if (strcmp(buf, "display") != 0)
		return (CN_DEAD);

	/* Keep track of the OF node */
	sc->sc_node = node;

	/*
	 * Try to use a 32-bit framebuffer if possible. This may be
	 * unimplemented and fail. That's fine -- it just means we are
	 * stuck with the defaults.
	 */
	OF_call_method("set-depth", sc->sc_handle, 1, 1, (cell_t)32, &i);

	/* Make sure we have needed properties */
	if (OF_getproplen(node, "height") != sizeof(height) ||
	    OF_getproplen(node, "width") != sizeof(width) ||
	    OF_getproplen(node, "depth") != sizeof(depth))
		return (CN_DEAD);

	/* Only support 8 and 32-bit framebuffers */
	OF_getprop(node, "depth", &depth, sizeof(depth));
	if (depth != 8 && depth != 32)
		return (CN_DEAD);
	sc->fb.fb_bpp = sc->fb.fb_depth = depth;

	OF_getprop(node, "height", &height, sizeof(height));
	OF_getprop(node, "width", &width, sizeof(width));
	if (OF_getprop(node, "linebytes", &stride, sizeof(stride)) !=
	    sizeof(stride))
		stride = width*depth/8;

	sc->fb.fb_height = height;
	sc->fb.fb_width = width;
	sc->fb.fb_stride = stride;
	sc->fb.fb_size = sc->fb.fb_height * sc->fb.fb_stride;

	/*
	 * Grab the physical address of the framebuffer, and then map it
	 * into our memory space. If the MMU is not yet up, it will be
	 * remapped for us when relocation turns on.
	 */
	if (OF_getproplen(node, "address") == sizeof(fb_phys)) {
		/* XXX We assume #address-cells is 1 at this point. */
		OF_getprop(node, "address", &fb_phys, sizeof(fb_phys));

	#if defined(__powerpc__)
		sc->sc_memt = &bs_be_tag;
		bus_space_map(sc->sc_memt, fb_phys, sc->fb.fb_size,
		    BUS_SPACE_MAP_PREFETCHABLE, &sc->fb.fb_vbase);
	#elif defined(__sparc64__)
		OF_decode_addr(node, 0, &space, &phys);
		sc->sc_memt = &ofwfb_memt[0];
		sc->fb.fb_vbase =
		    sparc64_fake_bustag(space, fb_phys, sc->sc_memt);
	#elif defined(__arm__)
		sc->sc_memt = fdtbus_bs_tag;
		bus_space_map(sc->sc_memt, sc->fb.fb_pbase, sc->fb.fb_size,
		    BUS_SPACE_MAP_PREFETCHABLE,
		    (bus_space_handle_t *)&sc->fb.fb_vbase);
	#else
		#error Unsupported platform!
	#endif

		sc->fb.fb_pbase = fb_phys;
	} else {
		/*
		 * Some IBM systems don't have an address property. Try to
		 * guess the framebuffer region from the assigned addresses.
		 * This is ugly, but there doesn't seem to be an alternative.
		 * Linux does the same thing.
		 */

		struct ofw_pci_register pciaddrs[8];
		int num_pciaddrs = 0;

		/*
		 * Get the PCI addresses of the adapter, if present. The node
		 * may be the child of the PCI device: in that case, try the
		 * parent for the assigned-addresses property.
		 */
		len = OF_getprop(node, "assigned-addresses", pciaddrs,
		    sizeof(pciaddrs));
		if (len == -1) {
			len = OF_getprop(OF_parent(node), "assigned-addresses",
			    pciaddrs, sizeof(pciaddrs));
		}
		if (len == -1)
			len = 0;
		num_pciaddrs = len / sizeof(struct ofw_pci_register);

		fb_phys = num_pciaddrs;
		for (i = 0; i < num_pciaddrs; i++) {
			/* If it is too small, not the framebuffer */
			if (pciaddrs[i].size_lo < sc->fb.fb_stride * height)
				continue;
			/* If it is not memory, it isn't either */
			if (!(pciaddrs[i].phys_hi &
			    OFW_PCI_PHYS_HI_SPACE_MEM32))
				continue;

			/* This could be the framebuffer */
			fb_phys = i;

			/* If it is prefetchable, it certainly is */
			if (pciaddrs[i].phys_hi & OFW_PCI_PHYS_HI_PREFETCHABLE)
				break;
		}

		if (fb_phys == num_pciaddrs) /* No candidates found */
			return (CN_DEAD);

	#if defined(__powerpc__)
		OF_decode_addr(node, fb_phys, &sc->sc_memt, &sc->fb.fb_vbase,
		    NULL);
		sc->fb.fb_pbase = sc->fb.fb_vbase & ~DMAP_BASE_ADDRESS;
	#else
		/* No ability to interpret assigned-addresses otherwise */
		return (CN_DEAD);
	#endif
        }


	#if defined(__powerpc__)
	/*
	 * If we are running on PowerPC in real mode (supported only on AIM
	 * CPUs), the frame buffer may be inaccessible (real mode does not
	 * necessarily cover all RAM) and may also be mapped with the wrong
	 * cache properties (all real mode accesses are assumed cacheable).
	 * Just don't write to it for the time being.
	 */
	if (!(cpu_features & PPC_FEATURE_BOOKE) && !(mfmsr() & PSL_DR))
		sc->fb.fb_flags |= FB_FLAG_NOWRITE;
	#endif
	ofwfb_initialize(vd);
	vt_fb_init(vd);

	return (CN_INTERNAL);
}

