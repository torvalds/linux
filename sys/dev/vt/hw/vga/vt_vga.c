/*-
 * Copyright (c) 2005 Marcel Moolenaar
 * All rights reserved.
 *
 * Copyright (c) 2009 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Ed Schouten
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

#include "opt_acpi.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/vt/vt.h>
#include <dev/vt/colors/vt_termcolors.h>
#include <dev/vt/hw/vga/vt_vga_reg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#if defined(__amd64__) || defined(__i386__)
#include <contrib/dev/acpica/include/acpi.h>
#include <machine/md_var.h>
#endif

struct vga_softc {
	bus_space_tag_t		 vga_fb_tag;
	bus_space_handle_t	 vga_fb_handle;
	bus_space_tag_t		 vga_reg_tag;
	bus_space_handle_t	 vga_reg_handle;
	int			 vga_wmode;
	term_color_t		 vga_curfg, vga_curbg;
	boolean_t		 vga_enabled;
};

/* Convenience macros. */
#define	MEM_READ1(sc, ofs) \
	bus_space_read_1(sc->vga_fb_tag, sc->vga_fb_handle, ofs)
#define	MEM_WRITE1(sc, ofs, val) \
	bus_space_write_1(sc->vga_fb_tag, sc->vga_fb_handle, ofs, val)
#define	MEM_WRITE2(sc, ofs, val) \
	bus_space_write_2(sc->vga_fb_tag, sc->vga_fb_handle, ofs, val)
#define	REG_READ1(sc, reg) \
	bus_space_read_1(sc->vga_reg_tag, sc->vga_reg_handle, reg)
#define	REG_WRITE1(sc, reg, val) \
	bus_space_write_1(sc->vga_reg_tag, sc->vga_reg_handle, reg, val)

#define	VT_VGA_WIDTH	640
#define	VT_VGA_HEIGHT	480
#define	VT_VGA_MEMSIZE	(VT_VGA_WIDTH * VT_VGA_HEIGHT / 8)

/*
 * VGA is designed to handle 8 pixels at a time (8 pixels in one byte of
 * memory).
 */
#define	VT_VGA_PIXELS_BLOCK	8

/*
 * We use an off-screen addresses to:
 *     o  store the background color;
 *     o  store pixels pattern.
 * Those addresses are then loaded in the latches once.
 */
#define	VT_VGA_BGCOLOR_OFFSET	VT_VGA_MEMSIZE

static vd_probe_t	vga_probe;
static vd_init_t	vga_init;
static vd_blank_t	vga_blank;
static vd_bitblt_text_t	vga_bitblt_text;
static vd_invalidate_text_t	vga_invalidate_text;
static vd_bitblt_bmp_t	vga_bitblt_bitmap;
static vd_drawrect_t	vga_drawrect;
static vd_setpixel_t	vga_setpixel;
static vd_postswitch_t	vga_postswitch;

static const struct vt_driver vt_vga_driver = {
	.vd_name	= "vga",
	.vd_probe	= vga_probe,
	.vd_init	= vga_init,
	.vd_blank	= vga_blank,
	.vd_bitblt_text	= vga_bitblt_text,
	.vd_invalidate_text = vga_invalidate_text,
	.vd_bitblt_bmp	= vga_bitblt_bitmap,
	.vd_drawrect	= vga_drawrect,
	.vd_setpixel	= vga_setpixel,
	.vd_postswitch	= vga_postswitch,
	.vd_priority	= VD_PRIORITY_GENERIC,
};

/*
 * Driver supports both text mode and graphics mode.  Make sure the
 * buffer is always big enough to support both.
 */
static struct vga_softc vga_conssoftc;
VT_DRIVER_DECLARE(vt_vga, vt_vga_driver);

static inline void
vga_setwmode(struct vt_device *vd, int wmode)
{
	struct vga_softc *sc = vd->vd_softc;

	if (sc->vga_wmode == wmode)
		return;

	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_MODE);
	REG_WRITE1(sc, VGA_GC_DATA, wmode);
	sc->vga_wmode = wmode;

	switch (wmode) {
	case 3:
		/* Re-enable all plans. */
		REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_MAP_MASK);
		REG_WRITE1(sc, VGA_SEQ_DATA, VGA_SEQ_MM_EM3 | VGA_SEQ_MM_EM2 |
		    VGA_SEQ_MM_EM1 | VGA_SEQ_MM_EM0);
		break;
	}
}

static inline void
vga_setfg(struct vt_device *vd, term_color_t color)
{
	struct vga_softc *sc = vd->vd_softc;

	vga_setwmode(vd, 3);

	if (sc->vga_curfg == color)
		return;

	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_SET_RESET);
	REG_WRITE1(sc, VGA_GC_DATA, cons_to_vga_colors[color]);
	sc->vga_curfg = color;
}

static inline void
vga_setbg(struct vt_device *vd, term_color_t color)
{
	struct vga_softc *sc = vd->vd_softc;

	vga_setwmode(vd, 3);

	if (sc->vga_curbg == color)
		return;

	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_SET_RESET);
	REG_WRITE1(sc, VGA_GC_DATA, cons_to_vga_colors[color]);

	/*
	 * Write 8 pixels using the background color to an off-screen
	 * byte in the video memory.
	 */
	MEM_WRITE1(sc, VT_VGA_BGCOLOR_OFFSET, 0xff);

	/*
	 * Read those 8 pixels back to load the background color in the
	 * latches register.
	 */
	MEM_READ1(sc, VT_VGA_BGCOLOR_OFFSET);

	sc->vga_curbg = color;

	/*
         * The Set/Reset register doesn't contain the fg color anymore,
         * store an invalid color.
	 */
	sc->vga_curfg = 0xff;
}

/*
 * Binary searchable table for Unicode to CP437 conversion.
 */

struct unicp437 {
	uint16_t	unicode_base;
	uint8_t		cp437_base;
	uint8_t		length;
};

static const struct unicp437 cp437table[] = {
	{ 0x0020, 0x20, 0x5e }, { 0x00a0, 0x20, 0x00 },
	{ 0x00a1, 0xad, 0x00 }, { 0x00a2, 0x9b, 0x00 },
	{ 0x00a3, 0x9c, 0x00 }, { 0x00a5, 0x9d, 0x00 },
	{ 0x00a6, 0x7c, 0x00 },
	{ 0x00a7, 0x15, 0x00 }, { 0x00aa, 0xa6, 0x00 },
	{ 0x00ab, 0xae, 0x00 }, { 0x00ac, 0xaa, 0x00 },
	{ 0x00b0, 0xf8, 0x00 }, { 0x00b1, 0xf1, 0x00 },
	{ 0x00b2, 0xfd, 0x00 }, { 0x00b5, 0xe6, 0x00 },
	{ 0x00b6, 0x14, 0x00 }, { 0x00b7, 0xfa, 0x00 },
	{ 0x00ba, 0xa7, 0x00 }, { 0x00bb, 0xaf, 0x00 },
	{ 0x00bc, 0xac, 0x00 }, { 0x00bd, 0xab, 0x00 },
	{ 0x00bf, 0xa8, 0x00 }, { 0x00c4, 0x8e, 0x01 },
	{ 0x00c6, 0x92, 0x00 }, { 0x00c7, 0x80, 0x00 },
	{ 0x00c9, 0x90, 0x00 }, { 0x00d1, 0xa5, 0x00 },
	{ 0x00d6, 0x99, 0x00 }, { 0x00dc, 0x9a, 0x00 },
	{ 0x00df, 0xe1, 0x00 }, { 0x00e0, 0x85, 0x00 },
	{ 0x00e1, 0xa0, 0x00 }, { 0x00e2, 0x83, 0x00 },
	{ 0x00e4, 0x84, 0x00 }, { 0x00e5, 0x86, 0x00 },
	{ 0x00e6, 0x91, 0x00 }, { 0x00e7, 0x87, 0x00 },
	{ 0x00e8, 0x8a, 0x00 }, { 0x00e9, 0x82, 0x00 },
	{ 0x00ea, 0x88, 0x01 }, { 0x00ec, 0x8d, 0x00 },
	{ 0x00ed, 0xa1, 0x00 }, { 0x00ee, 0x8c, 0x00 },
	{ 0x00ef, 0x8b, 0x00 }, { 0x00f0, 0xeb, 0x00 },
	{ 0x00f1, 0xa4, 0x00 }, { 0x00f2, 0x95, 0x00 },
	{ 0x00f3, 0xa2, 0x00 }, { 0x00f4, 0x93, 0x00 },
	{ 0x00f6, 0x94, 0x00 }, { 0x00f7, 0xf6, 0x00 },
	{ 0x00f8, 0xed, 0x00 }, { 0x00f9, 0x97, 0x00 },
	{ 0x00fa, 0xa3, 0x00 }, { 0x00fb, 0x96, 0x00 },
	{ 0x00fc, 0x81, 0x00 }, { 0x00ff, 0x98, 0x00 },
	{ 0x0192, 0x9f, 0x00 }, { 0x0393, 0xe2, 0x00 },
	{ 0x0398, 0xe9, 0x00 }, { 0x03a3, 0xe4, 0x00 },
	{ 0x03a6, 0xe8, 0x00 }, { 0x03a9, 0xea, 0x00 },
	{ 0x03b1, 0xe0, 0x01 }, { 0x03b4, 0xeb, 0x00 },
	{ 0x03b5, 0xee, 0x00 }, { 0x03bc, 0xe6, 0x00 },
	{ 0x03c0, 0xe3, 0x00 }, { 0x03c3, 0xe5, 0x00 },
	{ 0x03c4, 0xe7, 0x00 }, { 0x03c6, 0xed, 0x00 },
	{ 0x03d5, 0xed, 0x00 }, { 0x2010, 0x2d, 0x00 },
	{ 0x2013, 0x2d, 0x00 },
	{ 0x2014, 0x2d, 0x00 }, { 0x2018, 0x60, 0x00 },
	{ 0x2019, 0x27, 0x00 }, { 0x201c, 0x22, 0x00 },
	{ 0x201d, 0x22, 0x00 }, { 0x2022, 0x07, 0x00 },
	{ 0x203c, 0x13, 0x00 }, { 0x207f, 0xfc, 0x00 },
	{ 0x20a7, 0x9e, 0x00 }, { 0x20ac, 0xee, 0x00 },
	{ 0x2126, 0xea, 0x00 }, { 0x2190, 0x1b, 0x00 },
	{ 0x2191, 0x18, 0x00 }, { 0x2192, 0x1a, 0x00 },
	{ 0x2193, 0x19, 0x00 }, { 0x2194, 0x1d, 0x00 },
	{ 0x2195, 0x12, 0x00 }, { 0x21a8, 0x17, 0x00 },
	{ 0x2202, 0xeb, 0x00 }, { 0x2208, 0xee, 0x00 },
	{ 0x2211, 0xe4, 0x00 }, { 0x2212, 0x2d, 0x00 },
	{ 0x2219, 0xf9, 0x00 }, { 0x221a, 0xfb, 0x00 },
	{ 0x221e, 0xec, 0x00 }, { 0x221f, 0x1c, 0x00 },
	{ 0x2229, 0xef, 0x00 }, { 0x2248, 0xf7, 0x00 },
	{ 0x2261, 0xf0, 0x00 }, { 0x2264, 0xf3, 0x00 },
	{ 0x2265, 0xf2, 0x00 }, { 0x2302, 0x7f, 0x00 },
	{ 0x2310, 0xa9, 0x00 }, { 0x2320, 0xf4, 0x00 },
	{ 0x2321, 0xf5, 0x00 }, { 0x2500, 0xc4, 0x00 },
	{ 0x2502, 0xb3, 0x00 }, { 0x250c, 0xda, 0x00 },
	{ 0x2510, 0xbf, 0x00 }, { 0x2514, 0xc0, 0x00 },
	{ 0x2518, 0xd9, 0x00 }, { 0x251c, 0xc3, 0x00 },
	{ 0x2524, 0xb4, 0x00 }, { 0x252c, 0xc2, 0x00 },
	{ 0x2534, 0xc1, 0x00 }, { 0x253c, 0xc5, 0x00 },
	{ 0x2550, 0xcd, 0x00 }, { 0x2551, 0xba, 0x00 },
	{ 0x2552, 0xd5, 0x00 }, { 0x2553, 0xd6, 0x00 },
	{ 0x2554, 0xc9, 0x00 }, { 0x2555, 0xb8, 0x00 },
	{ 0x2556, 0xb7, 0x00 }, { 0x2557, 0xbb, 0x00 },
	{ 0x2558, 0xd4, 0x00 }, { 0x2559, 0xd3, 0x00 },
	{ 0x255a, 0xc8, 0x00 }, { 0x255b, 0xbe, 0x00 },
	{ 0x255c, 0xbd, 0x00 }, { 0x255d, 0xbc, 0x00 },
	{ 0x255e, 0xc6, 0x01 }, { 0x2560, 0xcc, 0x00 },
	{ 0x2561, 0xb5, 0x00 }, { 0x2562, 0xb6, 0x00 },
	{ 0x2563, 0xb9, 0x00 }, { 0x2564, 0xd1, 0x01 },
	{ 0x2566, 0xcb, 0x00 }, { 0x2567, 0xcf, 0x00 },
	{ 0x2568, 0xd0, 0x00 }, { 0x2569, 0xca, 0x00 },
	{ 0x256a, 0xd8, 0x00 }, { 0x256b, 0xd7, 0x00 },
	{ 0x256c, 0xce, 0x00 }, { 0x2580, 0xdf, 0x00 },
	{ 0x2584, 0xdc, 0x00 }, { 0x2588, 0xdb, 0x00 },
	{ 0x258c, 0xdd, 0x00 }, { 0x2590, 0xde, 0x00 },
	{ 0x2591, 0xb0, 0x02 }, { 0x25a0, 0xfe, 0x00 },
	{ 0x25ac, 0x16, 0x00 }, { 0x25b2, 0x1e, 0x00 },
	{ 0x25ba, 0x10, 0x00 }, { 0x25bc, 0x1f, 0x00 },
	{ 0x25c4, 0x11, 0x00 }, { 0x25cb, 0x09, 0x00 },
	{ 0x25d8, 0x08, 0x00 }, { 0x25d9, 0x0a, 0x00 },
	{ 0x263a, 0x01, 0x01 }, { 0x263c, 0x0f, 0x00 },
	{ 0x2640, 0x0c, 0x00 }, { 0x2642, 0x0b, 0x00 },
	{ 0x2660, 0x06, 0x00 }, { 0x2663, 0x05, 0x00 },
	{ 0x2665, 0x03, 0x01 }, { 0x266a, 0x0d, 0x00 },
	{ 0x266c, 0x0e, 0x00 }, { 0x2713, 0xfb, 0x00 },
	{ 0x27e8, 0x3c, 0x00 }, { 0x27e9, 0x3e, 0x00 },
};

static uint8_t
vga_get_cp437(term_char_t c)
{
	int min, mid, max;

	min = 0;
	max = nitems(cp437table) - 1;

	if (c < cp437table[0].unicode_base ||
	    c > cp437table[max].unicode_base + cp437table[max].length)
		return '?';

	while (max >= min) {
		mid = (min + max) / 2;
		if (c < cp437table[mid].unicode_base)
			max = mid - 1;
		else if (c > cp437table[mid].unicode_base +
		    cp437table[mid].length)
			min = mid + 1;
		else
			return (c - cp437table[mid].unicode_base +
			    cp437table[mid].cp437_base);
	}

	return '?';
}

static void
vga_blank(struct vt_device *vd, term_color_t color)
{
	struct vga_softc *sc = vd->vd_softc;
	u_int ofs;

	vga_setfg(vd, color);
	for (ofs = 0; ofs < VT_VGA_MEMSIZE; ofs++)
		MEM_WRITE1(sc, ofs, 0xff);
}

static inline void
vga_bitblt_put(struct vt_device *vd, u_long dst, term_color_t color,
    uint8_t v)
{
	struct vga_softc *sc = vd->vd_softc;

	/* Skip empty writes, in order to avoid palette changes. */
	if (v != 0x00) {
		vga_setfg(vd, color);
		/*
		 * When this MEM_READ1() gets disabled, all sorts of
		 * artifacts occur.  This is because this read loads the
		 * set of 8 pixels that are about to be changed.  There
		 * is one scenario where we can avoid the read, namely
		 * if all pixels are about to be overwritten anyway.
		 */
		if (v != 0xff) {
			MEM_READ1(sc, dst);

			/* The bg color was trashed by the reads. */
			sc->vga_curbg = 0xff;
		}
		MEM_WRITE1(sc, dst, v);
	}
}

static void
vga_setpixel(struct vt_device *vd, int x, int y, term_color_t color)
{

	if (vd->vd_flags & VDF_TEXTMODE)
		return;

	vga_bitblt_put(vd, (y * VT_VGA_WIDTH / 8) + (x / 8), color,
	    0x80 >> (x % 8));
}

static void
vga_drawrect(struct vt_device *vd, int x1, int y1, int x2, int y2, int fill,
    term_color_t color)
{
	int x, y;

	if (vd->vd_flags & VDF_TEXTMODE)
		return;

	for (y = y1; y <= y2; y++) {
		if (fill || (y == y1) || (y == y2)) {
			for (x = x1; x <= x2; x++)
				vga_setpixel(vd, x, y, color);
		} else {
			vga_setpixel(vd, x1, y, color);
			vga_setpixel(vd, x2, y, color);
		}
	}
}

static void
vga_compute_shifted_pattern(const uint8_t *src, unsigned int bytes,
    unsigned int src_x, unsigned int x_count, unsigned int dst_x,
    uint8_t *pattern, uint8_t *mask)
{
	unsigned int n;

	n = src_x / 8;

	/*
	 * This mask has bits set, where a pixel (ether 0 or 1)
	 * comes from the source bitmap.
	 */
	if (mask != NULL) {
		*mask = (0xff
		    >> (8 - x_count))
		    << (8 - x_count - dst_x);
	}

	if (n == (src_x + x_count - 1) / 8) {
		/* All the pixels we want are in the same byte. */
		*pattern = src[n];
		if (dst_x >= src_x)
			*pattern >>= (dst_x - src_x % 8);
		else
			*pattern <<= (src_x % 8 - dst_x);
	} else {
		/* The pixels we want are split into two bytes. */
		if (dst_x >= src_x % 8) {
			*pattern =
			    src[n] << (8 - dst_x - src_x % 8) |
			    src[n + 1] >> (dst_x - src_x % 8);
		} else {
			*pattern =
			    src[n] << (src_x % 8 - dst_x) |
			    src[n + 1] >> (8 - src_x % 8 - dst_x);
		}
	}
}

static void
vga_copy_bitmap_portion(uint8_t *pattern_2colors, uint8_t *pattern_ncolors,
    const uint8_t *src, const uint8_t *src_mask, unsigned int src_width,
    unsigned int src_x, unsigned int dst_x, unsigned int x_count,
    unsigned int src_y, unsigned int dst_y, unsigned int y_count,
    term_color_t fg, term_color_t bg, int overwrite)
{
	unsigned int i, bytes;
	uint8_t pattern, relevant_bits, mask;

	bytes = (src_width + 7) / 8;

	for (i = 0; i < y_count; ++i) {
		vga_compute_shifted_pattern(src + (src_y + i) * bytes,
		    bytes, src_x, x_count, dst_x, &pattern, &relevant_bits);

		if (src_mask == NULL) {
			/*
			 * No src mask. Consider that all wanted bits
			 * from the source are "authoritative".
			 */
			mask = relevant_bits;
		} else {
			/*
			 * There's an src mask. We shift it the same way
			 * we shifted the source pattern.
			 */
			vga_compute_shifted_pattern(
			    src_mask + (src_y + i) * bytes,
			    bytes, src_x, x_count, dst_x,
			    &mask, NULL);

			/* Now, only keep the wanted bits among them. */
			mask &= relevant_bits;
		}

		/*
		 * Clear bits from the pattern which must be
		 * transparent, according to the source mask.
		 */
		pattern &= mask;

		/* Set the bits in the 2-colors array. */
		if (overwrite)
			pattern_2colors[dst_y + i] &= ~mask;
		pattern_2colors[dst_y + i] |= pattern;

		if (pattern_ncolors == NULL)
			continue;

		/*
		 * Set the same bits in the n-colors array. This one
		 * supports transparency, when a given bit is cleared in
		 * all colors.
		 */
		if (overwrite) {
			/*
			 * Ensure that the pixels used by this bitmap are
			 * cleared in other colors.
			 */
			for (int j = 0; j < 16; ++j)
				pattern_ncolors[(dst_y + i) * 16 + j] &=
				    ~mask;
		}
		pattern_ncolors[(dst_y + i) * 16 + fg] |= pattern;
		pattern_ncolors[(dst_y + i) * 16 + bg] |= (~pattern & mask);
	}
}

static void
vga_bitblt_pixels_block_2colors(struct vt_device *vd, const uint8_t *masks,
    term_color_t fg, term_color_t bg,
    unsigned int x, unsigned int y, unsigned int height)
{
	unsigned int i, offset;
	struct vga_softc *sc;

	/*
	 * The great advantage of Write Mode 3 is that we just need
	 * to load the foreground in the Set/Reset register, load the
	 * background color in the latches register (this is done
	 * through a write in offscreen memory followed by a read of
	 * that data), then write the pattern to video memory. This
	 * pattern indicates if the pixel should use the foreground
	 * color (bit set) or the background color (bit cleared).
	 */

	vga_setbg(vd, bg);
	vga_setfg(vd, fg);

	sc = vd->vd_softc;
	offset = (VT_VGA_WIDTH * y + x) / 8;

	for (i = 0; i < height; ++i, offset += VT_VGA_WIDTH / 8) {
		MEM_WRITE1(sc, offset, masks[i]);
	}
}

static void
vga_bitblt_pixels_block_ncolors(struct vt_device *vd, const uint8_t *masks,
    unsigned int x, unsigned int y, unsigned int height)
{
	unsigned int i, j, plan, color, offset;
	struct vga_softc *sc;
	uint8_t mask, plans[height * 4];

	sc = vd->vd_softc;

	memset(plans, 0, sizeof(plans));

	/*
         * To write a group of pixels using 3 or more colors, we select
         * Write Mode 0 and write one byte to each plan separately.
	 */

	/*
	 * We first compute each byte: each plan contains one bit of the
	 * color code for each of the 8 pixels.
	 *
	 * For example, if the 8 pixels are like this:
	 *     GBBBBBBY
	 * where:
	 *     G (gray)   = 0b0111
	 *     B (black)  = 0b0000
	 *     Y (yellow) = 0b0011
	 *
	 * The corresponding for bytes are:
	 *             GBBBBBBY
	 *     Plan 0: 10000001 = 0x81
	 *     Plan 1: 10000001 = 0x81
	 *     Plan 2: 10000000 = 0x80
	 *     Plan 3: 00000000 = 0x00
	 *             |  |   |
	 *             |  |   +-> 0b0011 (Y)
	 *             |  +-----> 0b0000 (B)
	 *             +--------> 0b0111 (G)
	 */

	for (i = 0; i < height; ++i) {
		for (color = 0; color < 16; ++color) {
			mask = masks[i * 16 + color];
			if (mask == 0x00)
				continue;

			for (j = 0; j < 8; ++j) {
				if (!((mask >> (7 - j)) & 0x1))
					continue;

				/* The pixel "j" uses color "color". */
				for (plan = 0; plan < 4; ++plan)
					plans[i * 4 + plan] |=
					    ((color >> plan) & 0x1) << (7 - j);
			}
		}
	}

	/*
	 * The bytes are ready: we now switch to Write Mode 0 and write
	 * all bytes, one plan at a time.
	 */
	vga_setwmode(vd, 0);

	REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_MAP_MASK);
	for (plan = 0; plan < 4; ++plan) {
		/* Select plan. */
		REG_WRITE1(sc, VGA_SEQ_DATA, 1 << plan);

		/* Write all bytes for this plan, from Y to Y+height. */
		for (i = 0; i < height; ++i) {
			offset = (VT_VGA_WIDTH * (y + i) + x) / 8;
			MEM_WRITE1(sc, offset, plans[i * 4 + plan]);
		}
	}
}

static void
vga_bitblt_one_text_pixels_block(struct vt_device *vd,
    const struct vt_window *vw, unsigned int x, unsigned int y)
{
	const struct vt_buf *vb;
	const struct vt_font *vf;
	unsigned int i, col, row, src_x, x_count;
	unsigned int used_colors_list[16], used_colors;
	uint8_t pattern_2colors[vw->vw_font->vf_height];
	uint8_t pattern_ncolors[vw->vw_font->vf_height * 16];
	term_char_t c;
	term_color_t fg, bg;
	const uint8_t *src;

	vb = &vw->vw_buf;
	vf = vw->vw_font;

	/*
	 * The current pixels block.
	 *
	 * We fill it with portions of characters, because both "grids"
	 * may not match.
	 *
	 * i is the index in this pixels block.
	 */

	i = x;
	used_colors = 0;
	memset(used_colors_list, 0, sizeof(used_colors_list));
	memset(pattern_2colors, 0, sizeof(pattern_2colors));
	memset(pattern_ncolors, 0, sizeof(pattern_ncolors));

	if (i < vw->vw_draw_area.tr_begin.tp_col) {
		/*
		 * i is in the margin used to center the text area on
		 * the screen.
		 */

		i = vw->vw_draw_area.tr_begin.tp_col;
	}

	while (i < x + VT_VGA_PIXELS_BLOCK &&
	    i < vw->vw_draw_area.tr_end.tp_col) {
		/*
		 * Find which character is drawn on this pixel in the
		 * pixels block.
		 *
		 * While here, record what colors it uses.
		 */

		col = (i - vw->vw_draw_area.tr_begin.tp_col) / vf->vf_width;
		row = (y - vw->vw_draw_area.tr_begin.tp_row) / vf->vf_height;

		c = VTBUF_GET_FIELD(vb, row, col);
		src = vtfont_lookup(vf, c);

		vt_determine_colors(c, VTBUF_ISCURSOR(vb, row, col), &fg, &bg);
		if ((used_colors_list[fg] & 0x1) != 0x1)
			used_colors++;
		if ((used_colors_list[bg] & 0x2) != 0x2)
			used_colors++;
		used_colors_list[fg] |= 0x1;
		used_colors_list[bg] |= 0x2;

		/*
		 * Compute the portion of the character we want to draw,
		 * because the pixels block may start in the middle of a
		 * character.
		 *
		 * The first pixel to draw in the character is
		 *     the current position -
		 *     the start position of the character
		 *
		 * The last pixel to draw is either
		 *     - the last pixel of the character, or
		 *     - the pixel of the character matching the end of
		 *       the pixels block
		 * whichever comes first. This position is then
		 * changed to be relative to the start position of the
		 * character.
		 */

		src_x = i -
		    (col * vf->vf_width + vw->vw_draw_area.tr_begin.tp_col);
		x_count = min(min(
		    (col + 1) * vf->vf_width +
		    vw->vw_draw_area.tr_begin.tp_col,
		    x + VT_VGA_PIXELS_BLOCK),
		    vw->vw_draw_area.tr_end.tp_col);
		x_count -= col * vf->vf_width +
		    vw->vw_draw_area.tr_begin.tp_col;
		x_count -= src_x;

		/* Copy a portion of the character. */
		vga_copy_bitmap_portion(pattern_2colors, pattern_ncolors,
		    src, NULL, vf->vf_width,
		    src_x, i % VT_VGA_PIXELS_BLOCK, x_count,
		    0, 0, vf->vf_height, fg, bg, 0);

		/* We move to the next portion. */
		i += x_count;
	}

#ifndef SC_NO_CUTPASTE
	/*
	 * Copy the mouse pointer bitmap if it's over the current pixels
	 * block.
	 *
	 * We use the saved cursor position (saved in vt_flush()), because
	 * the current position could be different than the one used
	 * to mark the area dirty.
	 */
	term_rect_t drawn_area;

	drawn_area.tr_begin.tp_col = x;
	drawn_area.tr_begin.tp_row = y;
	drawn_area.tr_end.tp_col = x + VT_VGA_PIXELS_BLOCK;
	drawn_area.tr_end.tp_row = y + vf->vf_height;
	if (vd->vd_mshown && vt_is_cursor_in_area(vd, &drawn_area)) {
		struct vt_mouse_cursor *cursor;
		unsigned int mx, my;
		unsigned int dst_x, src_y, dst_y, y_count;

		cursor = vd->vd_mcursor;
		mx = vd->vd_mx_drawn + vw->vw_draw_area.tr_begin.tp_col;
		my = vd->vd_my_drawn + vw->vw_draw_area.tr_begin.tp_row;

		/* Compute the portion of the cursor we want to copy. */
		src_x = x > mx ? x - mx : 0;
		dst_x = mx > x ? mx - x : 0;
		x_count = min(min(min(
		    cursor->width - src_x,
		    x + VT_VGA_PIXELS_BLOCK - mx),
		    vw->vw_draw_area.tr_end.tp_col - mx),
		    VT_VGA_PIXELS_BLOCK);

		/*
		 * The cursor isn't aligned on the Y-axis with
		 * characters, so we need to compute the vertical
		 * start/count.
		 */
		src_y = y > my ? y - my : 0;
		dst_y = my > y ? my - y : 0;
		y_count = min(
		    min(cursor->height - src_y, y + vf->vf_height - my),
		    vf->vf_height);

		/* Copy the cursor portion. */
		vga_copy_bitmap_portion(pattern_2colors, pattern_ncolors,
		    cursor->map, cursor->mask, cursor->width,
		    src_x, dst_x, x_count, src_y, dst_y, y_count,
		    vd->vd_mcursor_fg, vd->vd_mcursor_bg, 1);

		if ((used_colors_list[vd->vd_mcursor_fg] & 0x1) != 0x1)
			used_colors++;
		if ((used_colors_list[vd->vd_mcursor_bg] & 0x2) != 0x2)
			used_colors++;
	}
#endif

	/*
	 * The pixels block is completed, we can now draw it on the
	 * screen.
	 */
	if (used_colors == 2)
		vga_bitblt_pixels_block_2colors(vd, pattern_2colors, fg, bg,
		    x, y, vf->vf_height);
	else
		vga_bitblt_pixels_block_ncolors(vd, pattern_ncolors,
		    x, y, vf->vf_height);
}

static void
vga_bitblt_text_gfxmode(struct vt_device *vd, const struct vt_window *vw,
    const term_rect_t *area)
{
	const struct vt_font *vf;
	unsigned int col, row;
	unsigned int x1, y1, x2, y2, x, y;

	vf = vw->vw_font;

	/*
	 * Compute the top-left pixel position aligned with the video
	 * adapter pixels block size.
	 *
	 * This is calculated from the top-left column of te dirty area:
	 *
	 *     1. Compute the top-left pixel of the character:
	 *        col * font width + x offset
	 *
	 *        NOTE: x offset is used to center the text area on the
	 *        screen. It's expressed in pixels, not in characters
	 *        col/row!
	 *
	 *     2. Find the pixel further on the left marking the start of
	 *        an aligned pixels block (eg. chunk of 8 pixels):
	 *        character's x / blocksize * blocksize
	 *
	 *        The division, being made on integers, achieves the
	 *        alignment.
	 *
	 * For the Y-axis, we need to compute the character's y
	 * coordinate, but we don't need to align it.
	 */

	col = area->tr_begin.tp_col;
	row = area->tr_begin.tp_row;
	x1 = (int)((col * vf->vf_width + vw->vw_draw_area.tr_begin.tp_col)
	     / VT_VGA_PIXELS_BLOCK)
	    * VT_VGA_PIXELS_BLOCK;
	y1 = row * vf->vf_height + vw->vw_draw_area.tr_begin.tp_row;

	/*
	 * Compute the bottom right pixel position, again, aligned with
	 * the pixels block size.
	 *
	 * The same rules apply, we just add 1 to base the computation
	 * on the "right border" of the dirty area.
	 */

	col = area->tr_end.tp_col;
	row = area->tr_end.tp_row;
	x2 = (int)howmany(col * vf->vf_width + vw->vw_draw_area.tr_begin.tp_col,
	    VT_VGA_PIXELS_BLOCK)
	    * VT_VGA_PIXELS_BLOCK;
	y2 = row * vf->vf_height + vw->vw_draw_area.tr_begin.tp_row;

	/* Clip the area to the screen size. */
	x2 = min(x2, vw->vw_draw_area.tr_end.tp_col);
	y2 = min(y2, vw->vw_draw_area.tr_end.tp_row);

	/*
	 * Now, we take care of N pixels line at a time (the first for
	 * loop, N = font height), and for these lines, draw one pixels
	 * block at a time (the second for loop), not a character at a
	 * time.
	 *
	 * Therefore, on the X-axis, characters my be drawn partially if
	 * they are not aligned on 8-pixels boundary.
	 *
	 * However, the operation is repeated for the full height of the
	 * font before moving to the next character, because it allows
	 * to keep the color settings and write mode, before perhaps
	 * changing them with the next one.
	 */

	for (y = y1; y < y2; y += vf->vf_height) {
		for (x = x1; x < x2; x += VT_VGA_PIXELS_BLOCK) {
			vga_bitblt_one_text_pixels_block(vd, vw, x, y);
		}
	}
}

static void
vga_bitblt_text_txtmode(struct vt_device *vd, const struct vt_window *vw,
    const term_rect_t *area)
{
	struct vga_softc *sc;
	const struct vt_buf *vb;
	unsigned int col, row;
	term_char_t c;
	term_color_t fg, bg;
	uint8_t ch, attr;
	size_t z;

	sc = vd->vd_softc;
	vb = &vw->vw_buf;

	for (row = area->tr_begin.tp_row; row < area->tr_end.tp_row; ++row) {
		for (col = area->tr_begin.tp_col;
		    col < area->tr_end.tp_col;
		    ++col) {
			/*
			 * Get next character and its associated fg/bg
			 * colors.
			 */
			c = VTBUF_GET_FIELD(vb, row, col);
			vt_determine_colors(c, VTBUF_ISCURSOR(vb, row, col),
			    &fg, &bg);

			z = row * PIXEL_WIDTH(VT_FB_MAX_WIDTH) + col;
			if (vd->vd_drawn && (vd->vd_drawn[z] == c) &&
			    vd->vd_drawnfg && (vd->vd_drawnfg[z] == fg) &&
			    vd->vd_drawnbg && (vd->vd_drawnbg[z] == bg))
				continue;

			/*
			 * Convert character to CP437, which is the
			 * character set used by the VGA hardware by
			 * default.
			 */
			ch = vga_get_cp437(TCHAR_CHARACTER(c));

			/* Convert colors to VGA attributes. */
			attr =
			    cons_to_vga_colors[bg] << 4 |
			    cons_to_vga_colors[fg];

			MEM_WRITE2(sc, (row * 80 + col) * 2 + 0,
			    ch + ((uint16_t)(attr) << 8));

			if (vd->vd_drawn)
				vd->vd_drawn[z] = c;
			if (vd->vd_drawnfg)
				vd->vd_drawnfg[z] = fg;
			if (vd->vd_drawnbg)
				vd->vd_drawnbg[z] = bg;
		}
	}
}

static void
vga_bitblt_text(struct vt_device *vd, const struct vt_window *vw,
    const term_rect_t *area)
{

	if (!(vd->vd_flags & VDF_TEXTMODE)) {
		vga_bitblt_text_gfxmode(vd, vw, area);
	} else {
		vga_bitblt_text_txtmode(vd, vw, area);
	}
}

void
vga_invalidate_text(struct vt_device *vd, const term_rect_t *area)
{
	unsigned int col, row;
	size_t z;

	for (row = area->tr_begin.tp_row; row < area->tr_end.tp_row; ++row) {
		for (col = area->tr_begin.tp_col;
		    col < area->tr_end.tp_col;
		    ++col) {
			z = row * PIXEL_WIDTH(VT_FB_MAX_WIDTH) + col;
			if (vd->vd_drawn)
				vd->vd_drawn[z] = 0;
			if (vd->vd_drawnfg)
				vd->vd_drawnfg[z] = 0;
			if (vd->vd_drawnbg)
				vd->vd_drawnbg[z] = 0;
		}
	}
}

static void
vga_bitblt_bitmap(struct vt_device *vd, const struct vt_window *vw,
    const uint8_t *pattern, const uint8_t *mask,
    unsigned int width, unsigned int height,
    unsigned int x, unsigned int y, term_color_t fg, term_color_t bg)
{
	unsigned int x1, y1, x2, y2, i, j, src_x, dst_x, x_count;
	uint8_t pattern_2colors;

	/* Align coordinates with the 8-pxels grid. */
	x1 = rounddown(x, VT_VGA_PIXELS_BLOCK);
	y1 = y;

	x2 = roundup(x + width, VT_VGA_PIXELS_BLOCK);
	y2 = y + height;
	x2 = min(x2, vd->vd_width - 1);
	y2 = min(y2, vd->vd_height - 1);

	for (j = y1; j < y2; ++j) {
		src_x = 0;
		dst_x = x - x1;
		x_count = VT_VGA_PIXELS_BLOCK - dst_x;

		for (i = x1; i < x2; i += VT_VGA_PIXELS_BLOCK) {
			pattern_2colors = 0;

			vga_copy_bitmap_portion(
			    &pattern_2colors, NULL,
			    pattern, mask, width,
			    src_x, dst_x, x_count,
			    j - y1, 0, 1, fg, bg, 0);

			vga_bitblt_pixels_block_2colors(vd,
			    &pattern_2colors, fg, bg,
			    i, j, 1);

			src_x += x_count;
			dst_x = (dst_x + x_count) % VT_VGA_PIXELS_BLOCK;
			x_count = min(width - src_x, VT_VGA_PIXELS_BLOCK);
		}
	}
}

static void
vga_initialize_graphics(struct vt_device *vd)
{
	struct vga_softc *sc = vd->vd_softc;

	/* Clock select. */
	REG_WRITE1(sc, VGA_GEN_MISC_OUTPUT_W, VGA_GEN_MO_VSP | VGA_GEN_MO_HSP |
	    VGA_GEN_MO_PB | VGA_GEN_MO_ER | VGA_GEN_MO_IOA);
	/* Set sequencer clocking and memory mode. */
	REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_CLOCKING_MODE);
	REG_WRITE1(sc, VGA_SEQ_DATA, VGA_SEQ_CM_89);
	REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_MEMORY_MODE);
	REG_WRITE1(sc, VGA_SEQ_DATA, VGA_SEQ_MM_OE | VGA_SEQ_MM_EM);

	/* Set the graphics controller in graphics mode. */
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_MISCELLANEOUS);
	REG_WRITE1(sc, VGA_GC_DATA, 0x04 + VGA_GC_MISC_GA);
	/* Program the CRT controller. */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_HORIZ_TOTAL);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x5f);			/* 760 */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_HORIZ_DISP_END);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x4f);			/* 640 - 8 */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_START_HORIZ_BLANK);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x50);			/* 640 */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_END_HORIZ_BLANK);
	REG_WRITE1(sc, VGA_CRTC_DATA, VGA_CRTC_EHB_CR + 2);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_START_HORIZ_RETRACE);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x54);			/* 672 */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_END_HORIZ_RETRACE);
	REG_WRITE1(sc, VGA_CRTC_DATA, VGA_CRTC_EHR_EHB + 0);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_VERT_TOTAL);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x0b);			/* 523 */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_OVERFLOW);
	REG_WRITE1(sc, VGA_CRTC_DATA, VGA_CRTC_OF_VT9 | VGA_CRTC_OF_LC8 |
	    VGA_CRTC_OF_VBS8 | VGA_CRTC_OF_VRS8 | VGA_CRTC_OF_VDE8);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_MAX_SCAN_LINE);
	REG_WRITE1(sc, VGA_CRTC_DATA, VGA_CRTC_MSL_LC9);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_VERT_RETRACE_START);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0xea);			/* 480 + 10 */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_VERT_RETRACE_END);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x0c);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_VERT_DISPLAY_END);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0xdf);			/* 480 - 1*/
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_OFFSET);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x28);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_START_VERT_BLANK);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0xe7);			/* 480 + 7 */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_END_VERT_BLANK);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x04);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_MODE_CONTROL);
	REG_WRITE1(sc, VGA_CRTC_DATA, VGA_CRTC_MC_WB | VGA_CRTC_MC_AW |
	    VGA_CRTC_MC_SRS | VGA_CRTC_MC_CMS);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_LINE_COMPARE);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0xff);			/* 480 + 31 */

	REG_WRITE1(sc, VGA_GEN_FEATURE_CTRL_W, 0);

	REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_MAP_MASK);
	REG_WRITE1(sc, VGA_SEQ_DATA, VGA_SEQ_MM_EM3 | VGA_SEQ_MM_EM2 |
	    VGA_SEQ_MM_EM1 | VGA_SEQ_MM_EM0);
	REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_CHAR_MAP_SELECT);
	REG_WRITE1(sc, VGA_SEQ_DATA, 0);

	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_SET_RESET);
	REG_WRITE1(sc, VGA_GC_DATA, 0);
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_ENABLE_SET_RESET);
	REG_WRITE1(sc, VGA_GC_DATA, 0x0f);
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_COLOR_COMPARE);
	REG_WRITE1(sc, VGA_GC_DATA, 0);
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_DATA_ROTATE);
	REG_WRITE1(sc, VGA_GC_DATA, 0);
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_READ_MAP_SELECT);
	REG_WRITE1(sc, VGA_GC_DATA, 0);
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_MODE);
	REG_WRITE1(sc, VGA_GC_DATA, 0);
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_COLOR_DONT_CARE);
	REG_WRITE1(sc, VGA_GC_DATA, 0x0f);
	REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_BIT_MASK);
	REG_WRITE1(sc, VGA_GC_DATA, 0xff);
}

static int
vga_initialize(struct vt_device *vd, int textmode)
{
	struct vga_softc *sc = vd->vd_softc;
	uint8_t x;
	int timeout;

	/* Make sure the VGA adapter is not in monochrome emulation mode. */
	x = REG_READ1(sc, VGA_GEN_MISC_OUTPUT_R);
	REG_WRITE1(sc, VGA_GEN_MISC_OUTPUT_W, x | VGA_GEN_MO_IOA);

	/* Unprotect CRTC registers 0-7. */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_VERT_RETRACE_END);
	x = REG_READ1(sc, VGA_CRTC_DATA);
	REG_WRITE1(sc, VGA_CRTC_DATA, x & ~VGA_CRTC_VRE_PR);

	/*
	 * Wait for the vertical retrace.
	 * NOTE: this code reads the VGA_GEN_INPUT_STAT_1 register, which has
	 * the side-effect of clearing the internal flip-flip of the attribute
	 * controller's write register. This means that because this code is
	 * here, we know for sure that the first write to the attribute
	 * controller will be a write to the address register. Removing this
	 * code therefore also removes that guarantee and appropriate measures
	 * need to be taken.
	 */
	timeout = 10000;
	do {
		DELAY(10);
		x = REG_READ1(sc, VGA_GEN_INPUT_STAT_1);
		x &= VGA_GEN_IS1_VR | VGA_GEN_IS1_DE;
	} while (x != (VGA_GEN_IS1_VR | VGA_GEN_IS1_DE) && --timeout != 0);
	if (timeout == 0) {
		printf("Timeout initializing vt_vga\n");
		return (ENXIO);
	}

	/* Now, disable the sync. signals. */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_MODE_CONTROL);
	x = REG_READ1(sc, VGA_CRTC_DATA);
	REG_WRITE1(sc, VGA_CRTC_DATA, x & ~VGA_CRTC_MC_HR);

	/* Asynchronous sequencer reset. */
	REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_RESET);
	REG_WRITE1(sc, VGA_SEQ_DATA, VGA_SEQ_RST_SR);

	if (!textmode)
		vga_initialize_graphics(vd);

	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_PRESET_ROW_SCAN);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_CURSOR_START);
	REG_WRITE1(sc, VGA_CRTC_DATA, VGA_CRTC_CS_COO);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_CURSOR_END);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_START_ADDR_HIGH);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_START_ADDR_LOW);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_CURSOR_LOC_HIGH);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_CURSOR_LOC_LOW);
	REG_WRITE1(sc, VGA_CRTC_DATA, 0x59);
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_UNDERLINE_LOC);
	REG_WRITE1(sc, VGA_CRTC_DATA, VGA_CRTC_UL_UL);

	if (textmode) {
		/* Set the attribute controller to blink disable. */
		REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_MODE_CONTROL);
		REG_WRITE1(sc, VGA_AC_WRITE, 0);
	} else {
		/* Set the attribute controller in graphics mode. */
		REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_MODE_CONTROL);
		REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_MC_GA);
		REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_HORIZ_PIXEL_PANNING);
		REG_WRITE1(sc, VGA_AC_WRITE, 0);
	}
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(0));
	REG_WRITE1(sc, VGA_AC_WRITE, 0);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(1));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_B);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(2));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_G);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(3));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_G | VGA_AC_PAL_B);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(4));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_R);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(5));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_R | VGA_AC_PAL_B);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(6));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SG | VGA_AC_PAL_R);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(7));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_R | VGA_AC_PAL_G | VGA_AC_PAL_B);

	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(8));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(9));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB | VGA_AC_PAL_B);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(10));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB | VGA_AC_PAL_G);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(11));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB | VGA_AC_PAL_G | VGA_AC_PAL_B);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(12));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB | VGA_AC_PAL_R);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(13));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB | VGA_AC_PAL_R | VGA_AC_PAL_B);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(14));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB | VGA_AC_PAL_R | VGA_AC_PAL_G);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PALETTE(15));
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_PAL_SR | VGA_AC_PAL_SG |
	    VGA_AC_PAL_SB | VGA_AC_PAL_R | VGA_AC_PAL_G | VGA_AC_PAL_B);

	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_OVERSCAN_COLOR);
	REG_WRITE1(sc, VGA_AC_WRITE, 0);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_COLOR_PLANE_ENABLE);
	REG_WRITE1(sc, VGA_AC_WRITE, 0x0f);
	REG_WRITE1(sc, VGA_AC_WRITE, VGA_AC_COLOR_SELECT);
	REG_WRITE1(sc, VGA_AC_WRITE, 0);

	if (!textmode) {
		u_int ofs;

		/*
		 * Done.  Clear the frame buffer.  All bit planes are
		 * enabled, so a single-paged loop should clear all
		 * planes.
		 */
		for (ofs = 0; ofs < VT_VGA_MEMSIZE; ofs++) {
			MEM_WRITE1(sc, ofs, 0);
		}
	}

	/* Re-enable the sequencer. */
	REG_WRITE1(sc, VGA_SEQ_ADDRESS, VGA_SEQ_RESET);
	REG_WRITE1(sc, VGA_SEQ_DATA, VGA_SEQ_RST_SR | VGA_SEQ_RST_NAR);
	/* Re-enable the sync signals. */
	REG_WRITE1(sc, VGA_CRTC_ADDRESS, VGA_CRTC_MODE_CONTROL);
	x = REG_READ1(sc, VGA_CRTC_DATA);
	REG_WRITE1(sc, VGA_CRTC_DATA, x | VGA_CRTC_MC_HR);

	if (!textmode) {
		/* Switch to write mode 3, because we'll mainly do bitblt. */
		REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_MODE);
		REG_WRITE1(sc, VGA_GC_DATA, 3);
		sc->vga_wmode = 3;

		/*
		 * In Write Mode 3, Enable Set/Reset is ignored, but we
		 * use Write Mode 0 to write a group of 8 pixels using
		 * 3 or more colors. In this case, we want to disable
		 * Set/Reset: set Enable Set/Reset to 0.
		 */
		REG_WRITE1(sc, VGA_GC_ADDRESS, VGA_GC_ENABLE_SET_RESET);
		REG_WRITE1(sc, VGA_GC_DATA, 0x00);

		/*
		 * Clear the colors we think are loaded into Set/Reset or
		 * the latches.
		 */
		sc->vga_curfg = sc->vga_curbg = 0xff;
	}

	return (0);
}

static bool
vga_acpi_disabled(void)
{
#if defined(__amd64__) || defined(__i386__)
	uint16_t flags;
	int ignore;

	ignore = 0;
	TUNABLE_INT_FETCH("hw.vga.acpi_ignore_no_vga", &ignore);
	if (ignore || !acpi_get_fadt_bootflags(&flags))
 		return (false);
	return ((flags & ACPI_FADT_NO_VGA) != 0);
#else
	return (false);
#endif
}

static int
vga_probe(struct vt_device *vd)
{

	return (vga_acpi_disabled() ? CN_DEAD : CN_INTERNAL);
}

static int
vga_init(struct vt_device *vd)
{
	struct vga_softc *sc;
	int textmode;

	if (vd->vd_softc == NULL)
		vd->vd_softc = (void *)&vga_conssoftc;
	sc = vd->vd_softc;

	if (vd->vd_flags & VDF_DOWNGRADE && vd->vd_video_dev != NULL)
		vga_pci_repost(vd->vd_video_dev);

#if defined(__amd64__) || defined(__i386__)
	sc->vga_fb_tag = X86_BUS_SPACE_MEM;
	sc->vga_reg_tag = X86_BUS_SPACE_IO;
#else
# error "Architecture not yet supported!"
#endif

	bus_space_map(sc->vga_reg_tag, VGA_REG_BASE, VGA_REG_SIZE, 0,
	    &sc->vga_reg_handle);

	/*
	 * If "hw.vga.textmode" is not set and we're running on hypervisor,
	 * we use text mode by default, this is because when we're on
	 * hypervisor, vt(4) is usually much slower in graphics mode than
	 * in text mode, especially when we're on Hyper-V.
	 */
	textmode = vm_guest != VM_GUEST_NO;
	TUNABLE_INT_FETCH("hw.vga.textmode", &textmode);
	if (textmode) {
		vd->vd_flags |= VDF_TEXTMODE;
		vd->vd_width = 80;
		vd->vd_height = 25;
		bus_space_map(sc->vga_fb_tag, VGA_TXT_BASE, VGA_TXT_SIZE, 0,
		    &sc->vga_fb_handle);
	} else {
		vd->vd_width = VT_VGA_WIDTH;
		vd->vd_height = VT_VGA_HEIGHT;
		bus_space_map(sc->vga_fb_tag, VGA_MEM_BASE, VGA_MEM_SIZE, 0,
		    &sc->vga_fb_handle);
	}
	if (vga_initialize(vd, textmode) != 0)
		return (CN_DEAD);
	sc->vga_enabled = true;

	return (CN_INTERNAL);
}

static void
vga_postswitch(struct vt_device *vd)
{

	/* Reinit VGA mode, to restore view after app which change mode. */
	vga_initialize(vd, (vd->vd_flags & VDF_TEXTMODE));
	/* Ask vt(9) to update chars on visible area. */
	vd->vd_flags |= VDF_INVALID;
}

/* Dummy NewBus functions to reserve the resources used by the vt_vga driver */
static void
vtvga_identify(driver_t *driver, device_t parent)
{

	if (!vga_conssoftc.vga_enabled)
		return;

	if (BUS_ADD_CHILD(parent, 0, driver->name, 0) == NULL)
		panic("Unable to attach vt_vga console");
}

static int
vtvga_probe(device_t dev)
{

	device_set_desc(dev, "VT VGA driver");

	return (BUS_PROBE_NOWILDCARD);
}

static int
vtvga_attach(device_t dev)
{
	struct resource *pseudo_phys_res;
	int res_id;

	res_id = 0;
	pseudo_phys_res = bus_alloc_resource(dev, SYS_RES_MEMORY,
	    &res_id, VGA_MEM_BASE, VGA_MEM_BASE + VGA_MEM_SIZE - 1,
	    VGA_MEM_SIZE, RF_ACTIVE);
	if (pseudo_phys_res == NULL)
		panic("Unable to reserve vt_vga memory");
	return (0);
}

/*-------------------- Private Device Attachment Data  -----------------------*/
static device_method_t vtvga_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	vtvga_identify),
	DEVMETHOD(device_probe,         vtvga_probe),
	DEVMETHOD(device_attach,        vtvga_attach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(vtvga, vtvga_driver, vtvga_methods, 0);
devclass_t vtvga_devclass;

DRIVER_MODULE(vtvga, nexus, vtvga_driver, vtvga_devclass, NULL, NULL);
