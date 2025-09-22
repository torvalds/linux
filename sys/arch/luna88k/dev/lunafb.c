/* $OpenBSD: lunafb.c,v 1.32 2023/08/05 00:34:19 aoyama Exp $ */
/* $NetBSD: lunafb.c,v 1.7.6.1 2002/08/07 01:48:34 lukem Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tohru Nishimura.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/errno.h>
#include <sys/buf.h>

#include <uvm/uvm_extern.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#include <machine/autoconf.h>
#include <machine/board.h>
#include <machine/cpu.h>

struct bt454 {
	volatile u_int8_t bt_addr;	/* map address register */
	volatile u_int8_t bt_cmap;	/* colormap data register */
};

struct bt458 {
	volatile u_int8_t bt_addr;	/* map address register */
	unsigned :24;
	volatile u_int8_t bt_cmap;	/* colormap data register */
	unsigned :24;
	volatile u_int8_t bt_ctrl;	/* control register */
	unsigned :24;
	volatile u_int8_t bt_omap;	/* overlay (cursor) map register */
	unsigned :24;
};

#define	OMFB_RFCNT	BMAP_RFCNT	/* video h-origin/v-origin */
#define	OMFB_PLANEMASK	BMAP_BMSEL	/* planemask register */
#define	OMFB_FB_WADDR	(BMAP_BMP + 8)	/* common plane */
#define	OMFB_FB_RADDR	(BMAP_BMAP0 + 8)/* plane #0 */
#define OMFB_FB_PLANESIZE  0x40000	/* size of 1 plane, 2048 / 8 * 1024 */
#define	OMFB_ROPFUNC	BMAP_FN		/* ROP function code */
#define	OMFB_RAMDAC	BMAP_PALLET2	/* Bt454/Bt458 RAMDAC */
#define	OMFB_SIZE	(BMAP_FN0 - BMAP_BMP + PAGE_SIZE)

struct hwcmap {
#define CMAP_SIZE 256
	u_int8_t r[CMAP_SIZE];
	u_int8_t g[CMAP_SIZE];
	u_int8_t b[CMAP_SIZE];
};

struct om_hwdevconfig {
	int	dc_wid;			/* width of frame buffer */
	int	dc_ht;			/* height of frame buffer */
	int	dc_depth;		/* depth, bits per pixel */
	int	dc_rowbytes;		/* bytes in a FB scan line */
	int	dc_depth_checked;	/* depth is really checked or not */
	int	dc_cmsize;		/* colormap size */
	struct hwcmap dc_cmap;		/* software copy of colormap */
	vaddr_t	dc_videobase;		/* base of flat frame buffer */
	struct rasops_info dc_ri;	/* raster blitter variables */
};

struct omfb_softc {
	struct device sc_dev;		/* base device */
	struct om_hwdevconfig *sc_dc;	/* device configuration */
	int nscreens;
};

int  omgetcmap(struct omfb_softc *, struct wsdisplay_cmap *);
int  omsetcmap(struct omfb_softc *, struct wsdisplay_cmap *);

struct om_hwdevconfig omfb_console_dc;
void omfb_getdevconfig(paddr_t, struct om_hwdevconfig *);

/* in omrasops.c */
int	om_copycols(void *, int, int, int, int);
int	om_copyrows(void *, int, int, int num);
int	om_erasecols(void *, int, int, int, uint32_t);
int	om_eraserows(void *, int, int, uint32_t);
void	setup_omrasops1(struct rasops_info *);
void	setup_omrasops4(struct rasops_info *);

struct wsscreen_descr omfb_stdscreen = {
	"std"
};

const struct wsscreen_descr *_omfb_scrlist[] = {
	&omfb_stdscreen,
};

const struct wsscreen_list omfb_screenlist = {
	sizeof(_omfb_scrlist) / sizeof(struct wsscreen_descr *), _omfb_scrlist
};

int	omfbioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	omfbmmap(void *, off_t, int);
int	omfb_alloc_screen(void *, const struct wsscreen_descr *,
	    void **, int *, int *, uint32_t *);
void	omfb_free_screen(void *, void *);
int	omfb_show_screen(void *, void *, int, void (*) (void *, int, int),
	    void *);
int	omfb_load_font(void *, void *, struct wsdisplay_font *);
int	omfb_list_font(void *, struct wsdisplay_font *);
int	omfb_set_gfxmode(struct omfb_softc *, struct wsdisplay_gfx_mode *);

void	omfb_set_default_cmap(struct om_hwdevconfig *);
void	omfb_clear_framebuffer(struct om_hwdevconfig *);

const struct wsdisplay_accessops omfb_accessops = {
	.ioctl = omfbioctl,
	.mmap = omfbmmap,
	.alloc_screen = omfb_alloc_screen,
	.free_screen = omfb_free_screen,
	.show_screen = omfb_show_screen,
	.load_font = omfb_load_font,
	.list_font = omfb_list_font
};

int  omfbmatch(struct device *, void *, void *);
void omfbattach(struct device *, struct device *, void *);

const struct cfattach fb_ca = {
	sizeof(struct omfb_softc), omfbmatch, omfbattach
};

struct cfdriver fb_cd = {
	NULL, "fb", DV_DULL
};

/* hardware plane bits; retrieved at boot, will be updated */
extern int hwplanebits;

int omfb_console;
int omfb_cnattach(void);

int
omfbmatch(struct device *parent, void *cf, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, fb_cd.cd_name))
		return (0);
#if 0	/* XXX badaddr() bombs if no framebuffer is installed */
	if (badaddr((caddr_t)ma->ma_addr, 4))
		return (0);
#else
	if (hwplanebits == 0)
		return (0);
#endif
	return (1);
}

void
omfbattach(struct device *parent, struct device *self, void *args)
{
	struct omfb_softc *sc = (struct omfb_softc *)self;
	struct wsemuldisplaydev_attach_args waa;

	if (omfb_console) {
		sc->sc_dc = &omfb_console_dc;
		sc->nscreens = 1;
	} else {
		sc->sc_dc = (struct om_hwdevconfig *)
		    malloc(sizeof(struct om_hwdevconfig), M_DEVBUF,
			M_WAITOK | M_ZERO);
		omfb_getdevconfig(OMFB_FB_WADDR, sc->sc_dc);
	}
	printf(": %dx%d, %dbpp\n", sc->sc_dc->dc_wid, sc->sc_dc->dc_ht,
	    hwplanebits);

	waa.console = omfb_console;
	waa.scrdata = &omfb_screenlist;
	waa.accessops = &omfb_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 0;

	config_found(self, &waa, wsemuldisplaydevprint);
}

/* EXPORT */ int
omfb_cnattach(void)
{
	struct om_hwdevconfig *dc = &omfb_console_dc;
	struct rasops_info *ri = &dc->dc_ri;
	uint32_t defattr;

	omfb_getdevconfig(OMFB_FB_WADDR, dc);
	ri->ri_ops.pack_attr(ri, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&omfb_stdscreen, ri, 0, 0, defattr);
	omfb_console = 1;
	return (0);
}

int
omfbioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct omfb_softc *sc = v;
	struct om_hwdevconfig *dc = sc->sc_dc;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_LUNA;
		break;

	case WSDISPLAYIO_GINFO:
#define	wsd_fbip ((struct wsdisplay_fbinfo *)data)
		wsd_fbip->height = dc->dc_ht;
		wsd_fbip->width = dc->dc_wid;
		wsd_fbip->depth = dc->dc_depth;
		wsd_fbip->stride = dc->dc_rowbytes;
		wsd_fbip->offset = 8;
		wsd_fbip->cmsize = dc->dc_cmsize;
#undef wsd_fbip
		break;

	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = dc->dc_rowbytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		return omgetcmap(sc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_PUTCMAP:
		return omsetcmap(sc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		if (dc->dc_depth == 8)
			*(u_int *)data =
			    WSDISPLAYIO_DEPTH_8 | WSDISPLAYIO_DEPTH_1;
		else
			*(u_int *)data = WSDISPLAYIO_DEPTH_1;
		break;

	case WSDISPLAYIO_SETGFXMODE:
		return omfb_set_gfxmode(sc, (struct wsdisplay_gfx_mode *)data);

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return (-1);
	}

	return (0);
}

/*
 * Return the address that would map the given device at the given
 * offset, allowing for the given protection, or return -1 for error.
 */

paddr_t
omfbmmap(void *v, off_t offset, int prot)
{
	struct omfb_softc *sc = v;
	struct om_hwdevconfig *dc = sc->sc_dc;
	paddr_t cookie = -1;

	if ((offset & PAGE_MASK) != 0)
		return (-1);

#if 0	/* Workaround for making Xorg mono server work */
	if (offset >= 0 && offset < OMFB_SIZE)
		cookie = (paddr_t)(trunc_page(dc->dc_videobase) + offset);
#else
	if (offset >= 0 &&
	    offset < dc->dc_rowbytes * dc->dc_ht * hwplanebits + PAGE_SIZE)
		cookie = (paddr_t)(trunc_page(OMFB_FB_RADDR) + offset);
#endif
	return cookie;
}

int
omgetcmap(struct omfb_softc *sc, struct wsdisplay_cmap *p)
{
	u_int index = p->index, count = p->count;
	unsigned int cmsize;
	int error;

	cmsize = sc->sc_dc->dc_cmsize;

	/* Don't touch colormap when we use 1bpp */
	if (cmsize == 0)
		return (0);

	if (index >= cmsize || count > cmsize - index)
		return (EINVAL);

	error = copyout(&sc->sc_dc->dc_cmap.r[index], p->red, count);
	if (error != 0)
		return (error);
	error = copyout(&sc->sc_dc->dc_cmap.g[index], p->green, count);
	if (error != 0)
		return (error);
	error = copyout(&sc->sc_dc->dc_cmap.b[index], p->blue, count);
	if (error != 0)
		return (error);

	return (0);
}

int
omsetcmap(struct omfb_softc *sc, struct wsdisplay_cmap *p)
{
	struct hwcmap cmap;
	u_int index = p->index, count = p->count;
	unsigned int cmsize, i;
	int error;

	cmsize = sc->sc_dc->dc_cmsize;

	/* Don't touch colormap when we use 1bpp */
	if (cmsize == 0)
		return (0);

	if (index >= cmsize || count > cmsize - index)
		return (EINVAL);

	error = copyin(p->red, &cmap.r[index], count);
	if (error != 0)
		return (error);
	error = copyin(p->green, &cmap.g[index], count);
	if (error != 0)
		return (error);
	error = copyin(p->blue, &cmap.b[index], count);
	if (error != 0)
		return (error);

	memcpy(&sc->sc_dc->dc_cmap.r[index], &cmap.r[index], count);
	memcpy(&sc->sc_dc->dc_cmap.g[index], &cmap.g[index], count);
	memcpy(&sc->sc_dc->dc_cmap.b[index], &cmap.b[index], count);

	if (hwplanebits == 4) {
		struct bt454 *odac = (struct bt454 *)OMFB_RAMDAC;
		odac->bt_addr = (u_int8_t)index;
		for (i = index; i < index + count; i++) {
			odac->bt_cmap = sc->sc_dc->dc_cmap.r[i];
			odac->bt_cmap = sc->sc_dc->dc_cmap.g[i];
			odac->bt_cmap = sc->sc_dc->dc_cmap.b[i];
		}
	}
	else if (hwplanebits == 8) {
		struct bt458 *ndac = (struct bt458 *)OMFB_RAMDAC;
		ndac->bt_addr = (u_int8_t)index;
		for (i = index; i < index + count; i++) {
			ndac->bt_cmap = sc->sc_dc->dc_cmap.r[i];
			ndac->bt_cmap = sc->sc_dc->dc_cmap.g[i];
			ndac->bt_cmap = sc->sc_dc->dc_cmap.b[i];
		}
	}
	return (0);
}

void
omfb_getdevconfig(paddr_t paddr, struct om_hwdevconfig *dc)
{
	struct rasops_info *ri;
	union {
		struct { short h, v; } p;
		u_int32_t u;
	} rfcnt;

	/*
	 * If this is the first time call, check how many planes we really
	 * have.  This method is for 1, 4, and 8 bpp boards, must be checked
	 * different way for 24 bpp board...
	 */
	if ((hwplanebits > 0) && (dc->dc_depth_checked == 0)) {
		int i;
		u_int32_t *max, save;

		for (i = 0; i < 8; i++) {
			max = (u_int32_t *)trunc_page(OMFB_FB_RADDR
			    + OMFB_FB_PLANESIZE * i);
			save = *max;
			*(volatile uint32_t *)max = 0x5a5a5a5a;
			if (*max != 0x5a5a5a5a)
				break;
			*max = save;
		}
		hwplanebits = i;	/* should be 1, 4, or 8 */

		dc->dc_depth_checked = 1;
	}

	dc->dc_wid = 1280;
	dc->dc_ht = 1024;
	dc->dc_depth = hwplanebits;
	dc->dc_rowbytes = 2048 / 8;
	dc->dc_cmsize = (hwplanebits == 1) ? 0 : 1 << hwplanebits;
	dc->dc_videobase = paddr;

	/* set default colormap */
	omfb_set_default_cmap(dc);

	/* adjust h/v origin on screen */
	rfcnt.p.h = 7;
	rfcnt.p.v = -27;
	/* single write of 0x007ffe6 */
	*(volatile u_int32_t *)OMFB_RFCNT = rfcnt.u;

	/* clear the screen */
	omfb_clear_framebuffer(dc);

	/* initialize the raster */
	ri = &dc->dc_ri;
	ri->ri_width = dc->dc_wid;
	ri->ri_height = dc->dc_ht;
	ri->ri_depth = 1;	/* since planes are independently addressed */
	ri->ri_stride = dc->dc_rowbytes;
	ri->ri_bits = (void *)dc->dc_videobase;
	ri->ri_flg = RI_CENTER;
	ri->ri_hw = dc;

	rasops_init(ri, 35, 80);

	ri->ri_ops.copycols = om_copycols;
	ri->ri_ops.erasecols = om_erasecols;
	ri->ri_ops.copyrows = om_copyrows;
	ri->ri_ops.eraserows = om_eraserows;
	omfb_stdscreen.ncols = ri->ri_cols;
	omfb_stdscreen.nrows = ri->ri_rows;
	omfb_stdscreen.textops = &ri->ri_ops;
	omfb_stdscreen.fontwidth = ri->ri_font->fontwidth;
	omfb_stdscreen.fontheight = ri->ri_font->fontheight;

	/* set up depth-depend functions and so on */
	if ((hwplanebits == 4) || (hwplanebits == 8)) {
		setup_omrasops4(ri);
	} else {
		setup_omrasops1(ri);
	}
}

int
omfb_alloc_screen(void *v, const struct wsscreen_descr *type, void **cookiep,
    int *curxp, int *curyp, uint32_t *attrp)
{
	struct omfb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_dc->dc_ri;

	if (sc->nscreens > 0)
		return (ENOMEM);

	*cookiep = ri;
	*curxp = 0;
	*curyp = 0;
	ri->ri_ops.pack_attr(ri, 0, 0, 0, attrp);
	sc->nscreens++;
	return (0);
}

void
omfb_free_screen(void *v, void *cookie)
{
	struct omfb_softc *sc = v;

	sc->nscreens--;
}

int
omfb_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	return 0;
}

int
omfb_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct omfb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_dc->dc_ri;

	return rasops_load_font(ri, emulcookie, font);
}

int
omfb_list_font(void *v, struct wsdisplay_font *font)
{
	struct omfb_softc *sc = v;
	struct rasops_info *ri = &sc->sc_dc->dc_ri;

	return rasops_list_font(ri, font);
}

/*
 * Change `pseudo' depth, set the default colormap, and clear frame buffer.
 * Note: when called with depth == 0, change to the original hardware depth. 
 */
int
omfb_set_gfxmode(struct omfb_softc *sc, struct wsdisplay_gfx_mode *wsd_gfxmode)
{
	/* LUNA's fb is fixed size */
	if ((wsd_gfxmode->width != sc->sc_dc->dc_wid)
	    || (wsd_gfxmode->height != sc->sc_dc->dc_ht))
		return -1;

	/* if depth == 0, set the original hardware depth */
	if (wsd_gfxmode->depth == 0)
		wsd_gfxmode->depth = hwplanebits;

	switch (wsd_gfxmode->depth) {
	case 1:
		/* all frame buffer support this */
		sc->sc_dc->dc_depth = 1;
		sc->sc_dc->dc_cmsize = 0;
		setup_omrasops1(&sc->sc_dc->dc_ri);
		break;
	case 4:
		if ((hwplanebits == 4) || (hwplanebits == 8)) {
			sc->sc_dc->dc_depth = 4;
			sc->sc_dc->dc_cmsize = 16;
			setup_omrasops4(&sc->sc_dc->dc_ri);
			break;
		} else
			return -1;
	case 8:
		if (hwplanebits == 8) {
			sc->sc_dc->dc_depth = 8;
			sc->sc_dc->dc_cmsize = 256;
			setup_omrasops4(&sc->sc_dc->dc_ri);
			break;
		} else
			return -1;
	default:
		return -1;
	}

	omfb_set_default_cmap(sc->sc_dc);
	omfb_clear_framebuffer(sc->sc_dc);

	return 0;
}

/*
 * Clear all planes of frame buffer
 */
void
omfb_clear_framebuffer(struct om_hwdevconfig *dc)
{
	int i;

	*(volatile u_int32_t *)OMFB_PLANEMASK = 0xff;	/* all planes */
	((volatile u_int32_t *)OMFB_ROPFUNC)[5] = ~0;	/* ROP copy */
	for (i = 0; i < dc->dc_ht * dc->dc_rowbytes / sizeof(u_int32_t); i++)
		*((volatile u_int32_t *)dc->dc_videobase + i) = 0;
	*(volatile u_int32_t *)OMFB_PLANEMASK = 0x01;	/* plane #0 only */
}

/*
 * Set default colormap; white on black for 1bpp, ANSI 16 colors for 4/8 bpp.
 *
 * Note about workaround for 8bpp frame buffer:
 *
 * Current LUNA wscons touches only first 4 planes (plane #0-#3), but
 * other program (e.g. mlterm-fb) can use all 8 planes on an 8bpp
 * frame buffer.  When such program exits, it may not clear all planes,
 * so there may be some visible garbage data on the other 4 planes
 * (plane #4-#7) when we use default 256 rasops_cmap directly.
 *
 * We should manage all 8 planes on LUNA, but that will be too much
 * overhead for 16 colors wscons.  So, by repeating 16 colors in 256
 * colormap, we can ignore the values on the other 4 planes.
 */
void
omfb_set_default_cmap(struct om_hwdevconfig *dc)
{
	int i;

	if (hwplanebits == 1) {
		struct bt454 *odac = (struct bt454 *)OMFB_RAMDAC;
		/*
		 * On 1bpp framebuffer, only plane P0 has framebuffer memory
		 * and other planes seems pulled up, i.e. always 1.
		 * Set white only for a palette (P0,P1,P2,P3) = (1,1,1,1).
		 */
		odac->bt_addr = 0;
		for (i = 0; i < 15; i++) {
			odac->bt_cmap = dc->dc_cmap.r[i] = 0;
			odac->bt_cmap = dc->dc_cmap.g[i] = 0;
			odac->bt_cmap = dc->dc_cmap.b[i] = 0;
		}
		/*
		 * The B/W video connector is connected to IOG of Bt454,
		 * and IOR and IOB are unused.
		 */
		odac->bt_cmap = dc->dc_cmap.r[15] = 0;
		odac->bt_cmap = dc->dc_cmap.g[15] = 255;
		odac->bt_cmap = dc->dc_cmap.b[15] = 0;
	} else if (hwplanebits == 4) {
		struct bt454 *odac = (struct bt454 *)OMFB_RAMDAC;

		odac->bt_addr = 0;
		if (dc->dc_depth == 1) {
			/* white on black */
			for (i = 0; i < 16; i++) {
				u_int8_t val = i % 2 ? 255 : 0;

				odac->bt_cmap = dc->dc_cmap.r[i] = val;
				odac->bt_cmap = dc->dc_cmap.g[i] = val;
				odac->bt_cmap = dc->dc_cmap.b[i] = val;
			}
		} else {
			for (i = 0; i < 16; i++) {
			/* Set ANSI 16 colors */
				odac->bt_cmap = dc->dc_cmap.r[i]
				    = rasops_cmap[i * 3];
				odac->bt_cmap = dc->dc_cmap.g[i]
				    = rasops_cmap[i * 3 + 1];
				odac->bt_cmap = dc->dc_cmap.b[i]
				    = rasops_cmap[i * 3 + 2];
			}
		}
	} else if (hwplanebits == 8) {
		struct bt458 *ndac = (struct bt458 *)OMFB_RAMDAC;

		/*
		 * Initialize the Bt458.  When we write to control registers,
		 * the address is not incremented automatically. So we specify
		 * it ourselves for each control register.
		 */
		ndac->bt_addr = 0x04;
		ndac->bt_ctrl = 0xff; /* all planes will be read */
		ndac->bt_addr = 0x05;
		ndac->bt_ctrl = 0x00; /* all planes have non-blink */
		ndac->bt_addr = 0x06;
		ndac->bt_ctrl = 0x40; /* palette enabled, ovly plane disabled */
		ndac->bt_addr = 0x07;
		ndac->bt_ctrl = 0x00; /* no test mode */

		ndac->bt_addr = 0;
		if (dc->dc_depth == 1) {
			/* white on black */
			for (i = 0; i < 256; i++) {
				u_int8_t val = i % 2 ? 255 : 0;

				ndac->bt_cmap = dc->dc_cmap.r[i] = val;
				ndac->bt_cmap = dc->dc_cmap.g[i] = val;
				ndac->bt_cmap = dc->dc_cmap.b[i] = val;
			}
		} else {
			/*
			 * Set ANSI 16 colors.  On 8bpp frame buffer, repeat
			 * 16 colors in 256 colormap as described above.
			 */
			for (i = 0; i < 256; i++) {
				int index = i % 16;
				ndac->bt_cmap = dc->dc_cmap.r[i]
				    = rasops_cmap[index * 3];
				ndac->bt_cmap = dc->dc_cmap.g[i]
				    = rasops_cmap[index * 3 + 1];
				ndac->bt_cmap = dc->dc_cmap.b[i]
				    = rasops_cmap[index * 3 + 2];
			}
		}
	}
}
