/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2017 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/fbio.h>
#include <sys/consio.h>
#include <sys/kdb.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/fb/fbreg.h>
#include <dev/syscons/syscons.h>

#include <arm/versatile/versatile_scm.h>

#include <machine/bus.h>

#define	PL110_VENDOR_ARM926PXP	1

#define	CLCD_MODE_RGB888	0x0
#define	CLCD_MODE_RGB555	0x01
#define	CLCD_MODE_RBG565	0x02
#define	CLCD_MODE_RGB565	0x03

#define	CLCDC_TIMING0		0x00
#define	CLCDC_TIMING1		0x04
#define	CLCDC_TIMING2		0x08
#define	CLCDC_TIMING3		0x0C
#define	CLCDC_TIMING3		0x0C
#define	CLCDC_UPBASE		0x10
#define	CLCDC_LPBASE		0x14
#ifdef PL110_VENDOR_ARM926PXP
#define	CLCDC_CONTROL		0x18
#define	CLCDC_IMSC		0x1C
#else
#define	CLCDC_IMSC		0x18
#define	CLCDC_CONTROL		0x1C
#endif
#define		CONTROL_WATERMARK	(1 << 16)
#define		CONTROL_VCOMP_VS	(0 << 12)
#define		CONTROL_VCOMP_BP	(1 << 12)
#define		CONTROL_VCOMP_SAV	(2 << 12)
#define		CONTROL_VCOMP_FP	(3 << 12)
#define		CONTROL_PWR		(1 << 11)
#define		CONTROL_BEPO		(1 << 10)
#define		CONTROL_BEBO		(1 << 9)
#define		CONTROL_BGR		(1 << 8)
#define		CONTROL_DUAL		(1 << 7)
#define		CONTROL_MONO8		(1 << 6)
#define		CONTROL_TFT		(1 << 5)
#define		CONTROL_BW		(1 << 4)
#define		CONTROL_BPP1		(0x00 << 1)
#define		CONTROL_BPP2		(0x01 << 1)
#define		CONTROL_BPP4		(0x02 << 1)
#define		CONTROL_BPP8		(0x03 << 1)
#define		CONTROL_BPP16		(0x04 << 1)
#define		CONTROL_BPP24		(0x05 << 1)
#define		CONTROL_EN	(1 << 0)
#define	CLCDC_RIS		0x20
#define	CLCDC_MIS		0x24
#define		INTR_MBERR		(1 << 4)
#define		INTR_VCOMP		(1 << 3)
#define		INTR_LNB		(1 << 2)
#define		INTR_FUF		(1 << 1)
#define	CLCDC_ICR		0x28

#ifdef DEBUG
#define dprintf(fmt, args...) do { printf("%s(): ", __func__);   \
    printf(fmt,##args); } while (0)
#else
#define dprintf(fmt, args...)
#endif

#define	versatile_clcdc_read_4(sc, reg)	\
	bus_read_4((sc)->mem_res, (reg))
#define	versatile_clcdc_write_4(sc, reg, val)	\
	bus_write_4((sc)->mem_res, (reg), (val))

struct versatile_clcdc_softc {
	struct resource*	mem_res;

	struct mtx		mtx;

	int			width;
	int			height;
	int			mode;

	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_addr_t		fb_phys;
	uint8_t			*fb_base;

};

struct video_adapter_softc {
	/* Videoadpater part */
	video_adapter_t	va;
	int		console;

	intptr_t	fb_addr;
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

static struct argb versatilefb_palette[16] = {
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

#define FB_WIDTH		640
#define FB_HEIGHT		480
#define FB_DEPTH		16

#define	VERSATILE_FONT_HEIGHT	16

static struct video_adapter_softc va_softc;

static int versatilefb_configure(int);
static void versatilefb_update_margins(video_adapter_t *adp);

static void
versatile_fb_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int err)
{
	bus_addr_t *addr;

	if (err)
		return;

	addr = (bus_addr_t*)arg;
	*addr = segs[0].ds_addr;
}

static int
versatile_clcdc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "arm,pl110")) {
		device_set_desc(dev, "PL110 CLCD controller");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
versatile_clcdc_attach(device_t dev)
{
	struct versatile_clcdc_softc *sc = device_get_softc(dev);
	struct video_adapter_softc *va_sc = &va_softc;
	int err, rid;
	uint32_t reg;
	int clcdid;
	int dma_size;

	/* Request memory resources */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resources\n");
		return (ENXIO);
	}

	err = versatile_scm_reg_read_4(SCM_CLCD, &reg);
	if (err) {
		device_printf(dev, "failed to read SCM register\n");
		goto fail;
	}
	clcdid = (reg >> SCM_CLCD_CLCDID_SHIFT) & SCM_CLCD_CLCDID_MASK;
	switch (clcdid) {
		case 31:
			device_printf(dev, "QEMU VGA 640x480\n");
			sc->width = 640;
			sc->height = 480;
			break;
		default:
			device_printf(dev, "Unsupported: %d\n", clcdid);
			goto fail;
	}

	reg &= ~SCM_CLCD_LCD_MODE_MASK;
	reg |= CLCD_MODE_RGB565;
	sc->mode = CLCD_MODE_RGB565;
	versatile_scm_reg_write_4(SCM_CLCD, reg);
 	dma_size = sc->width*sc->height*2;
 
 	/*
	 * Power on LCD
	 */
	reg |= SCM_CLCD_PWR3V5VSWITCH | SCM_CLCD_NLCDIOON;
	versatile_scm_reg_write_4(SCM_CLCD, reg);

	/*
	 * XXX: hardcoded timing for VGA. For other modes/panels
	 * we need to keep table of timing register values
	 */
	/*
	 * XXX: set SYS_OSC1 
	 */
	versatile_clcdc_write_4(sc, CLCDC_TIMING0, 0x3F1F3F9C);
	versatile_clcdc_write_4(sc, CLCDC_TIMING1, 0x090B61DF);
	versatile_clcdc_write_4(sc, CLCDC_TIMING2, 0x067F1800);
	/* XXX: timing 3? */

	/*
	 * Now allocate framebuffer memory
	 */
	err = bus_dma_tag_create(
	    bus_get_dma_tag(dev),
	    4, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    dma_size, 1,		/* maxsize, nsegments */
	    dma_size, 0,		/* maxsegsize, flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->dma_tag);

	err = bus_dmamem_alloc(sc->dma_tag, (void **)&sc->fb_base,
	    0, &sc->dma_map);
	if (err) {
		device_printf(dev, "cannot allocate framebuffer\n");
		goto fail;
	}

	err = bus_dmamap_load(sc->dma_tag, sc->dma_map, sc->fb_base,
	    dma_size, versatile_fb_dmamap_cb, &sc->fb_phys, BUS_DMA_NOWAIT);

	if (err) {
		device_printf(dev, "cannot load DMA map\n");
		goto fail;
	}

	/* Make sure it's blank */
	memset(sc->fb_base, 0x00, dma_size);

	versatile_clcdc_write_4(sc, CLCDC_UPBASE, sc->fb_phys);

	err = (sc_attach_unit(device_get_unit(dev),
	    device_get_flags(dev) | SC_AUTODETECT_KBD));

	if (err) {
		device_printf(dev, "failed to attach syscons\n");
		goto fail;
	}

	/*
	 * XXX: hardcoded for VGA
	 */
	reg = CONTROL_VCOMP_BP | CONTROL_TFT | CONTROL_BGR | CONTROL_EN;
	reg |= CONTROL_BPP16;
	versatile_clcdc_write_4(sc, CLCDC_CONTROL, reg);
	DELAY(20);
	reg |= CONTROL_PWR;
	versatile_clcdc_write_4(sc, CLCDC_CONTROL, reg);

	va_sc->fb_addr = (vm_offset_t)sc->fb_base;
	va_sc->fb_size = dma_size;
	va_sc->width = sc->width;
	va_sc->height = sc->height;
	va_sc->depth = 16;
	va_sc->stride = sc->width * 2;
	versatilefb_update_margins(&va_sc->va);

	return (0);

fail:
	if (sc->fb_base)
		bus_dmamem_free(sc->dma_tag, sc->fb_base, sc->dma_map);
	if (sc->dma_tag)
		bus_dma_tag_destroy(sc->dma_tag);
	return (err);
}

static device_method_t versatile_clcdc_methods[] = {
	DEVMETHOD(device_probe,		versatile_clcdc_probe),
	DEVMETHOD(device_attach,	versatile_clcdc_attach),

	DEVMETHOD_END
};

static driver_t versatile_clcdc_driver = {
	"clcdc",
	versatile_clcdc_methods,
	sizeof(struct versatile_clcdc_softc),
};

static devclass_t versatile_clcdc_devclass;

DRIVER_MODULE(versatile_clcdc, simplebus, versatile_clcdc_driver, versatile_clcdc_devclass, 0, 0);

/*
 * Video driver routines and glue.
 */
static vi_probe_t		versatilefb_probe;
static vi_init_t		versatilefb_init;
static vi_get_info_t		versatilefb_get_info;
static vi_query_mode_t		versatilefb_query_mode;
static vi_set_mode_t		versatilefb_set_mode;
static vi_save_font_t		versatilefb_save_font;
static vi_load_font_t		versatilefb_load_font;
static vi_show_font_t		versatilefb_show_font;
static vi_save_palette_t	versatilefb_save_palette;
static vi_load_palette_t	versatilefb_load_palette;
static vi_set_border_t		versatilefb_set_border;
static vi_save_state_t		versatilefb_save_state;
static vi_load_state_t		versatilefb_load_state;
static vi_set_win_org_t		versatilefb_set_win_org;
static vi_read_hw_cursor_t	versatilefb_read_hw_cursor;
static vi_set_hw_cursor_t	versatilefb_set_hw_cursor;
static vi_set_hw_cursor_shape_t	versatilefb_set_hw_cursor_shape;
static vi_blank_display_t	versatilefb_blank_display;
static vi_mmap_t		versatilefb_mmap;
static vi_ioctl_t		versatilefb_ioctl;
static vi_clear_t		versatilefb_clear;
static vi_fill_rect_t		versatilefb_fill_rect;
static vi_bitblt_t		versatilefb_bitblt;
static vi_diag_t		versatilefb_diag;
static vi_save_cursor_palette_t	versatilefb_save_cursor_palette;
static vi_load_cursor_palette_t	versatilefb_load_cursor_palette;
static vi_copy_t		versatilefb_copy;
static vi_putp_t		versatilefb_putp;
static vi_putc_t		versatilefb_putc;
static vi_puts_t		versatilefb_puts;
static vi_putm_t		versatilefb_putm;

static video_switch_t versatilefbvidsw = {
	.probe			= versatilefb_probe,
	.init			= versatilefb_init,
	.get_info		= versatilefb_get_info,
	.query_mode		= versatilefb_query_mode,
	.set_mode		= versatilefb_set_mode,
	.save_font		= versatilefb_save_font,
	.load_font		= versatilefb_load_font,
	.show_font		= versatilefb_show_font,
	.save_palette		= versatilefb_save_palette,
	.load_palette		= versatilefb_load_palette,
	.set_border		= versatilefb_set_border,
	.save_state		= versatilefb_save_state,
	.load_state		= versatilefb_load_state,
	.set_win_org		= versatilefb_set_win_org,
	.read_hw_cursor		= versatilefb_read_hw_cursor,
	.set_hw_cursor		= versatilefb_set_hw_cursor,
	.set_hw_cursor_shape	= versatilefb_set_hw_cursor_shape,
	.blank_display		= versatilefb_blank_display,
	.mmap			= versatilefb_mmap,
	.ioctl			= versatilefb_ioctl,
	.clear			= versatilefb_clear,
	.fill_rect		= versatilefb_fill_rect,
	.bitblt			= versatilefb_bitblt,
	.diag			= versatilefb_diag,
	.save_cursor_palette	= versatilefb_save_cursor_palette,
	.load_cursor_palette	= versatilefb_load_cursor_palette,
	.copy			= versatilefb_copy,
	.putp			= versatilefb_putp,
	.putc			= versatilefb_putc,
	.puts			= versatilefb_puts,
	.putm			= versatilefb_putm,
};

VIDEO_DRIVER(versatilefb, versatilefbvidsw, versatilefb_configure);

static vr_init_t clcdr_init;
static vr_clear_t clcdr_clear;
static vr_draw_border_t clcdr_draw_border;
static vr_draw_t clcdr_draw;
static vr_set_cursor_t clcdr_set_cursor;
static vr_draw_cursor_t clcdr_draw_cursor;
static vr_blink_cursor_t clcdr_blink_cursor;
static vr_set_mouse_t clcdr_set_mouse;
static vr_draw_mouse_t clcdr_draw_mouse;

/*
 * We use our own renderer; this is because we must emulate a hardware
 * cursor.
 */
static sc_rndr_sw_t clcdrend = {
	clcdr_init,
	clcdr_clear,
	clcdr_draw_border,
	clcdr_draw,
	clcdr_set_cursor,
	clcdr_draw_cursor,
	clcdr_blink_cursor,
	clcdr_set_mouse,
	clcdr_draw_mouse
};

RENDERER(versatilefb, 0, clcdrend, gfb_set);
RENDERER_MODULE(versatilefb, gfb_set);

static void
clcdr_init(scr_stat* scp)
{
}

static void
clcdr_clear(scr_stat* scp, int c, int attr)
{
}

static void
clcdr_draw_border(scr_stat* scp, int color)
{
}

static void
clcdr_draw(scr_stat* scp, int from, int count, int flip)
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
clcdr_set_cursor(scr_stat* scp, int base, int height, int blink)
{
}

static void
clcdr_draw_cursor(scr_stat* scp, int off, int blink, int on, int flip)
{
	video_adapter_t* adp = scp->sc->adp;
	struct video_adapter_softc *sc;
	int row, col;
	uint8_t *addr;
	int i,j;

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

	/* our cursor consists of simply inverting the char under it */
	for (i = 0; i < adp->va_info.vi_cheight; i++) {
		for (j = 0; j < adp->va_info.vi_cwidth; j++) {

			addr[2*j] ^= 0xff;
			addr[2*j + 1] ^= 0xff;
		}

		addr += sc->stride;
	}
}

static void
clcdr_blink_cursor(scr_stat* scp, int at, int flip)
{
}

static void
clcdr_set_mouse(scr_stat* scp)
{
}

static void
clcdr_draw_mouse(scr_stat* scp, int x, int y, int on)
{
	vidd_putm(scp->sc->adp, x, y, mouse_pointer, 0xffffffff, 16, 8);

}

static uint16_t versatilefb_static_window[ROW*COL];
extern u_char dflt_font_16[];

/*
 * Update videoadapter settings after changing resolution
 */
static void
versatilefb_update_margins(video_adapter_t *adp)
{
	struct video_adapter_softc *sc;
	video_info_t *vi;

	sc = (struct video_adapter_softc *)adp;
	vi = &adp->va_info;

	sc->xmargin = (sc->width - (vi->vi_width * vi->vi_cwidth)) / 2;
	sc->ymargin = (sc->height - (vi->vi_height * vi->vi_cheight))/2;
}

static int
versatilefb_configure(int flags)
{
	struct video_adapter_softc *va_sc;

	va_sc = &va_softc;

	if (va_sc->initialized)
		return (0);

	va_sc->width = FB_WIDTH;
	va_sc->height = FB_HEIGHT;
	va_sc->depth = FB_DEPTH;

	versatilefb_init(0, &va_sc->va, 0);

	va_sc->initialized = 1;

	return (0);
}

static int
versatilefb_probe(int unit, video_adapter_t **adp, void *arg, int flags)
{

	return (0);
}

static int
versatilefb_init(int unit, video_adapter_t *adp, int flags)
{
	struct video_adapter_softc *sc;
	video_info_t *vi;

	sc = (struct video_adapter_softc *)adp;
	vi = &adp->va_info;

	vid_init_struct(adp, "versatilefb", -1, unit);

	sc->font = dflt_font_16;
	vi->vi_cheight = VERSATILE_FONT_HEIGHT;
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

	adp->va_window = (vm_offset_t) versatilefb_static_window;
	adp->va_flags |= V_ADP_FONT /* | V_ADP_COLOR | V_ADP_MODECHANGE */;

	vid_register(&sc->va);

	return (0);
}

static int
versatilefb_get_info(video_adapter_t *adp, int mode, video_info_t *info)
{
	bcopy(&adp->va_info, info, sizeof(*info));
	return (0);
}

static int
versatilefb_query_mode(video_adapter_t *adp, video_info_t *info)
{
	return (0);
}

static int
versatilefb_set_mode(video_adapter_t *adp, int mode)
{
	return (0);
}

static int
versatilefb_save_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{
	return (0);
}

static int
versatilefb_load_font(video_adapter_t *adp, int page, int size, int width,
    u_char *data, int c, int count)
{
	struct video_adapter_softc *sc = (struct video_adapter_softc *)adp;

	sc->font = data;

	return (0);
}

static int
versatilefb_show_font(video_adapter_t *adp, int page)
{
	return (0);
}

static int
versatilefb_save_palette(video_adapter_t *adp, u_char *palette)
{
	return (0);
}

static int
versatilefb_load_palette(video_adapter_t *adp, u_char *palette)
{
	return (0);
}

static int
versatilefb_set_border(video_adapter_t *adp, int border)
{
	return (versatilefb_blank_display(adp, border));
}

static int
versatilefb_save_state(video_adapter_t *adp, void *p, size_t size)
{
	return (0);
}

static int
versatilefb_load_state(video_adapter_t *adp, void *p)
{
	return (0);
}

static int
versatilefb_set_win_org(video_adapter_t *adp, off_t offset)
{
	return (0);
}

static int
versatilefb_read_hw_cursor(video_adapter_t *adp, int *col, int *row)
{
	*col = *row = 0;

	return (0);
}

static int
versatilefb_set_hw_cursor(video_adapter_t *adp, int col, int row)
{

	return (0);
}

static int
versatilefb_set_hw_cursor_shape(video_adapter_t *adp, int base, int height,
    int celsize, int blink)
{
	return (0);
}

static int
versatilefb_blank_display(video_adapter_t *adp, int mode)
{

	struct video_adapter_softc *sc;

	sc = (struct video_adapter_softc *)adp;
	if (sc && sc->fb_addr)
		memset((void*)sc->fb_addr, 0, sc->fb_size);

	return (0);
}

static int
versatilefb_mmap(video_adapter_t *adp, vm_ooffset_t offset, vm_paddr_t *paddr,
    int prot, vm_memattr_t *memattr)
{
	struct video_adapter_softc *sc;

	sc = (struct video_adapter_softc *)adp;

	/*
	 * This might be a legacy VGA mem request: if so, just point it at the
	 * framebuffer, since it shouldn't be touched
	 */
	if (offset < sc->stride*sc->height) {
		*paddr = sc->fb_addr + offset;
		return (0);
	}

	return (EINVAL);
}

static int
versatilefb_ioctl(video_adapter_t *adp, u_long cmd, caddr_t data)
{

	return (0);
}

static int
versatilefb_clear(video_adapter_t *adp)
{

	return (versatilefb_blank_display(adp, 0));
}

static int
versatilefb_fill_rect(video_adapter_t *adp, int val, int x, int y, int cx, int cy)
{

	return (0);
}

static int
versatilefb_bitblt(video_adapter_t *adp, ...)
{

	return (0);
}

static int
versatilefb_diag(video_adapter_t *adp, int level)
{

	return (0);
}

static int
versatilefb_save_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
versatilefb_load_cursor_palette(video_adapter_t *adp, u_char *palette)
{

	return (0);
}

static int
versatilefb_copy(video_adapter_t *adp, vm_offset_t src, vm_offset_t dst, int n)
{

	return (0);
}

static int
versatilefb_putp(video_adapter_t *adp, vm_offset_t off, uint32_t p, uint32_t a,
    int size, int bpp, int bit_ltor, int byte_ltor)
{

	return (0);
}

static int
versatilefb_putc(video_adapter_t *adp, vm_offset_t off, uint8_t c, uint8_t a)
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

	if (off >= adp->va_info.vi_width * adp->va_info.vi_height)
		return (0);

	row = (off / adp->va_info.vi_width) * adp->va_info.vi_cheight;
	col = (off % adp->va_info.vi_width) * adp->va_info.vi_cwidth;
	p = sc->font + c*VERSATILE_FONT_HEIGHT;
	addr = (uint8_t *)sc->fb_addr
	    + (row + sc->ymargin)*(sc->stride)
	    + (sc->depth/8) * (col + sc->xmargin);

	fg = a & 0xf ;
	bg = (a >> 4) & 0xf;

	for (i = 0; i < VERSATILE_FONT_HEIGHT; i++) {
		for (j = 0, k = 7; j < 8; j++, k--) {
			if ((p[i] & (1 << k)) == 0)
				color = bg;
			else
				color = fg;

			switch (sc->depth) {
			case 16:
				rgb = (versatilefb_palette[color].r >> 3) << 11;
				rgb |= (versatilefb_palette[color].g >> 2) << 5;
				rgb |= (versatilefb_palette[color].b >> 3);
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
versatilefb_puts(video_adapter_t *adp, vm_offset_t off, u_int16_t *s, int len)
{
	int i;

	for (i = 0; i < len; i++) 
		versatilefb_putc(adp, off + i, s[i] & 0xff, (s[i] & 0xff00) >> 8);

	return (0);
}

static int
versatilefb_putm(video_adapter_t *adp, int x, int y, uint8_t *pixel_image,
    uint32_t pixel_mask, int size, int width)
{

	return (0);
}

/*
 * Define a stub keyboard driver in case one hasn't been
 * compiled into the kernel
 */
#include <sys/kbio.h>
#include <dev/kbd/kbdreg.h>

static int dummy_kbd_configure(int flags);

keyboard_switch_t bcmdummysw;

static int
dummy_kbd_configure(int flags)
{

	return (0);
}
KEYBOARD_DRIVER(bcmdummy, bcmdummysw, dummy_kbd_configure);
