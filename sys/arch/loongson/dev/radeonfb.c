/*	$OpenBSD: radeonfb.c,v 1.5 2022/07/15 17:57:26 kettenis Exp $	*/

/*
 * Copyright (c) 2009 Mark Kettenis.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/pciio.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>

#define RADEON_PCI_MEM		0x10
#define RADEON_PCI_MMIO		0x18

#define RADEON_CRTC_OFFSET		0x0224

#define RADEON_RBBM_STATUS		0x0e40
#define  RADEON_RBBM_FIFOCNT_MASK	0x0000007f
#define  RADEON_RBBM_ACTIVE		0x80000000

#define RADEON_SRC_Y_X			0x1434
#define RADEON_DST_Y_X			0x1438
#define RADEON_DST_HEIGHT_WIDTH		0x143c

#define RADEON_DP_GUI_MASTER_CNTL	0x146c
#define  RADEON_GMC_DST_8BPP		0x00000200
#define  RADEON_GMC_DST_16BPP		0x00000400
#define  RADEON_GMC_DST_32BPP		0x00000600
#define  RADEON_GMC_BRUSH_NONE		0x000000e0
#define  RADEON_GMC_BRUSH_SOLID_COLOR	0x000000d0
#define  RADEON_GMC_SRC_DATATYPE_COLOR	0x00003000
#define  RADEON_GMC_SRC_SOURCE_MEMORY	0x02000000
#define  RADEON_ROP3_S			0x00cc0000
#define  RADEON_ROP3_P			0x00f00000
#define  RADEON_GMC_CLR_CMP_CNTL_DIS    0x10000000

#define RADEON_DP_BRUSH_BKGD_CLR	0x1478
#define RADEON_DP_BRUSH_FRGD_CLR	0x147c

#define RADEON_DP_CNTL			0x16c0
#define  RADEON_DST_X_LEFT_TO_RIGHT	0x00000001
#define  RADEON_DST_Y_TOP_TO_BOTTOM	0x00000002
#define RADEON_DP_WRITE_MASK		0x16cc

#define RADEON_DEFAULT_PITCH_OFFSET	0x16e0
#define RADEON_DEFAULT_SC_BOTTOM_RIGHT	0x16e8

#define RADEON_WAIT_UNTIL		0x1720
#define  RADEON_WAIT_2D_IDLECLEAN	0x00010000
#define  RADEON_WAIT_3D_IDLECLEAN	0x00020000
#define  RADEON_WAIT_HOST_IDLECLEAN	0x00040000

#define RADEON_RB3D_DSTCACHE_CTLSTAT	0x325c
#define  RADEON_RB3D_DC_FLUSH_ALL	0x0000000f
#define  RADEON_RB3D_DC_BUSY		0x80000000

#define RADEON_COORDS(x, y)		((y << 16) | (x))

#ifdef APERTURE
extern int allowaperture;
#endif

struct radeonfb_softc;

/* minimal frame buffer information, suitable for early console */
struct radeonfb {
	struct radeonfb_softc	*sc;
	struct rasops_info	 ri;

	bus_space_tag_t		 fbt;
	bus_space_handle_t	 fbh;

	bus_space_tag_t		 mmiot;
	bus_space_handle_t	 mmioh;
	bus_size_t		 memoff;

	struct wsscreen_descr	 wsd;
};

struct radeonfb_softc {
	struct device		 sc_dev;
	struct radeonfb		*sc_fb;
	struct radeonfb		 sc_fb_store;

	struct wsscreen_list	 sc_wsl;
	struct wsscreen_descr	*sc_scrlist[1];
	int			 sc_nscr;

	bus_addr_t	sc_membase;
	bus_size_t	sc_memsize;

	bus_addr_t	sc_mmiobase;
	bus_size_t	sc_mmiosize;

	pcitag_t	sc_pcitag;

	int		sc_mode;
};

int	radeonfb_alloc_screen(void *, const struct wsscreen_descr *, void **,
	    int *, int *, uint32_t *);
void	radeonfb_free_screen(void *, void *);
int	radeonfb_ioctl(void *, u_long, caddr_t, int, struct proc *);
int	radeonfb_list_font(void *, struct wsdisplay_font *);
int	radeonfb_load_font(void *, void *, struct wsdisplay_font *);
paddr_t	radeonfb_mmap(void *, off_t, int);
int	radeonfb_show_screen(void *, void *, int, void (*)(void *, int, int),
	    void *);

struct wsdisplay_accessops radeonfb_accessops = {
	.ioctl = radeonfb_ioctl,
	.mmap = radeonfb_mmap,
	.alloc_screen = radeonfb_alloc_screen,
	.free_screen = radeonfb_free_screen,
	.show_screen = radeonfb_show_screen,
	.load_font = radeonfb_load_font,
	.list_font = radeonfb_list_font
};

int	radeonfb_match(struct device *, void *, void *);
void	radeonfb_attach(struct device *, struct device *, void *);

const struct cfattach radeonfb_ca = {
	sizeof(struct radeonfb_softc), radeonfb_match, radeonfb_attach
};

struct cfdriver radeonfb_cd = {
	NULL, "radeonfb", DV_DULL
};

int	radeonfb_copycols(void *, int, int, int, int);
int	radeonfb_erasecols(void *, int, int, int, uint32_t);
int	radeonfb_copyrows(void *, int, int, int);
int	radeonfb_eraserows(void *, int, int, uint32_t);

int	radeonfb_setup(struct radeonfb *);
void	radeonfb_wait_fifo(struct radeonfb *, int);
void	radeonfb_wait(struct radeonfb *);
void	radeonfb_copyrect(struct radeonfb *, int, int, int, int, int, int);
void	radeonfb_fillrect(struct radeonfb *, int, int, int, int, int);

static struct radeonfb radeonfbcn;

const struct pci_matchid radeonfb_devices[] = {
	{ PCI_VENDOR_ATI, 0x9615 }
};

int
radeonfb_match(struct device *parent, void *cf, void *aux)
{
	struct pci_attach_args *pa = aux;

	return pci_matchbyid(pa, radeonfb_devices, nitems(radeonfb_devices));
}

void
radeonfb_attach(struct device *parent, struct device *self, void *aux)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct wsemuldisplaydev_attach_args waa;
	bus_space_tag_t fbt, mmiot;
	bus_space_handle_t fbh, mmioh;
#if 0
	bus_size_t fbsize, mmiosize;
#endif
	struct radeonfb *fb;
	int console;

	sc->sc_pcitag = pa->pa_tag;

	if (pci_mapreg_map(pa, RADEON_PCI_MEM, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &fbt, &fbh,
	    &sc->sc_membase, &sc->sc_memsize, 0)) {
		printf(": can't map video memory\n");
		return;
	}

	if (pci_mapreg_map(pa, RADEON_PCI_MMIO, PCI_MAPREG_TYPE_MEM, 0,
	    &mmiot, &mmioh, &sc->sc_mmiobase,
	    &sc->sc_mmiosize, 0)) {
		printf(": can't map registers\n");
		return;
	}

	console = radeonfbcn.ri.ri_hw != NULL;

	if (console)
		fb = &radeonfbcn;
	else
		fb = &sc->sc_fb_store;

	fb->sc = sc;
	fb->fbt = fbt;
	fb->fbh = fbh;
	fb->mmiot = mmiot;
	fb->mmioh = mmioh;
	sc->sc_fb = fb;

	if (!console) {
		if (radeonfb_setup(fb) != 0) {
			printf(": can't setup frame buffer\n");
			return;
		}
	}

	printf(": %dx%d, %dbpp\n",
	    fb->ri.ri_width, fb->ri.ri_height, fb->ri.ri_depth);

	sc->sc_scrlist[0] = &fb->wsd;
	sc->sc_wsl.nscreens = 1;
	sc->sc_wsl.screens = (const struct wsscreen_descr **)sc->sc_scrlist;

	waa.console = console;
	waa.scrdata = &sc->sc_wsl;
	waa.accessops = &radeonfb_accessops;
	waa.accesscookie = sc;
	waa.defaultscreens = 0;

	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	config_found(self, &waa, wsemuldisplaydevprint);
}

/*
 * wsdisplay accessops
 */

int
radeonfb_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *attrp)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	if (sc->sc_nscr > 0)
		return ENOMEM;

	*cookiep = ri;
	*curxp = *curyp = 0;
	ri->ri_ops.pack_attr(ri, 0, 0, 0, attrp);
	sc->sc_nscr++;

	return 0;
}

void
radeonfb_free_screen(void *v, void *cookie)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)v;

	sc->sc_nscr--;
}

int
radeonfb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)v;
	struct radeonfb *fb = sc->sc_fb;
	struct rasops_info *ri = &fb->ri;
	struct wsdisplay_fbinfo *wdf;
#if 0
	struct pcisel *sel;
#endif

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_RADEONFB;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL) {
			/* Clear screen. */
			radeonfb_setup(fb);
			radeonfb_fillrect(fb, 0, 0, ri->ri_width,
			    ri->ri_height, ri->ri_devcmap[WSCOL_BLACK]);
		} 
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->width = ri->ri_width;
		wdf->height = ri->ri_height;
		wdf->depth = ri->ri_depth;
		wdf->stride = ri->ri_stride;
		wdf->offset = 0;
		wdf->cmsize = 0;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = ri->ri_stride;
		break;

#if 0
	case WSDISPLAYIO_GPCIID:
		sel = (struct pcisel *)data;
		sel->pc_bus = PCITAG_BUS(sc->sc_pcitag);
		sel->pc_dev = PCITAG_DEV(sc->sc_pcitag);
		sel->pc_func = PCITAG_FUN(sc->sc_pcitag);
		break;
#endif

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	default:
		return -1;
        }

	return 0;
}

int
radeonfb_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
	return 0;
}

paddr_t
radeonfb_mmap(void *v, off_t off, int prot)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)v;
	struct radeonfb *fb = sc->sc_fb;

	if ((off & PAGE_MASK) != 0)
		return -1;

	switch (sc->sc_mode) {
	case WSDISPLAYIO_MODE_MAPPED:
#ifdef APERTURE
		if (allowaperture == 0)
			return (-1);
#endif

		if (sc->sc_mmiosize == 0)
			return (-1);

		if (off >= sc->sc_membase &&
		    off < (sc->sc_membase + sc->sc_memsize))
			return (bus_space_mmap(fb->fbt,
			    sc->sc_membase, off - sc->sc_membase,
			    prot, BUS_SPACE_MAP_LINEAR));

		if (off >= sc->sc_mmiobase &&
		    off < (sc->sc_mmiobase + sc->sc_mmiosize))
			return (bus_space_mmap(fb->mmiot,
			    sc->sc_mmiobase, off - sc->sc_mmiobase,
			    prot, BUS_SPACE_MAP_LINEAR));
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		/*
		 * Don't allow mmap if the frame buffer area is not page aligned.
		 * XXX we should reprogram it to a page aligned boundary at attach
		 * XXX time if this isn't the case.
		 */
		if ((fb->memoff % PAGE_SIZE) != 0)
			return (-1);

		if (off >= 0 && off < sc->sc_memsize / 2) {
			bus_addr_t base = sc->sc_membase + fb->memoff;

			return (bus_space_mmap(fb->fbt, base, off,
			    prot, BUS_SPACE_MAP_LINEAR));
		}
		break;
	}

	return (-1);
}

int
radeonfb_load_font(void *v, void *emulcookie, struct wsdisplay_font *font)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	return rasops_load_font(ri, emulcookie, font);
}

int
radeonfb_list_font(void *v, struct wsdisplay_font *font)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)v;
	struct rasops_info *ri = &sc->sc_fb->ri;

	return rasops_list_font(ri, font);
}

/*
 * Accelerated routines.
 */

int
radeonfb_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct radeonfb *fb = ri->ri_hw;

	num *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	radeonfb_copyrect(fb, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight);

	return 0;
}

int
radeonfb_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct radeonfb *fb = ri->ri_hw;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= ri->ri_font->fontheight;
	col *= ri->ri_font->fontwidth;
	num *= ri->ri_font->fontwidth;

	radeonfb_fillrect(fb, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight, ri->ri_devcmap[bg]);

	return 0;
}

int
radeonfb_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct radeonfb *fb = ri->ri_hw;

	num *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	radeonfb_copyrect(fb, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);

	return 0;
}

int
radeonfb_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct radeonfb *fb = ri->ri_hw;
	int bg, fg;
	int x, y, w;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	if ((num == ri->ri_rows) && ISSET(ri->ri_flg, RI_FULLCLEAR)) {
		num = ri->ri_height;
		x = y = 0;
		w = ri->ri_width;
	} else {
		num *= ri->ri_font->fontheight;
		x = ri->ri_xorigin;
		y = ri->ri_yorigin + row * ri->ri_font->fontheight;
		w = ri->ri_emuwidth;
	}
	radeonfb_fillrect(fb, x, y, w, num, ri->ri_devcmap[bg]);

	return 0;
}

void
radeonfb_wait_fifo(struct radeonfb *fb, int n)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(fb->mmiot, fb->mmioh,
		    RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK) >= n)
			break;
		DELAY(1);
	}
}

void
radeonfb_wait(struct radeonfb *fb)
{
	int i;

	radeonfb_wait_fifo(fb, 64);

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(fb->mmiot, fb->mmioh,
		    RADEON_RBBM_STATUS) & RADEON_RBBM_ACTIVE) == 0)
			break;
		DELAY(1);
	}

	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_RB3D_DSTCACHE_CTLSTAT, RADEON_RB3D_DC_FLUSH_ALL);

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(fb->mmiot, fb->mmioh,
		    RADEON_RB3D_DSTCACHE_CTLSTAT) & RADEON_RB3D_DC_BUSY) == 0)
			break;
		DELAY(1);
	}
}

void
radeonfb_copyrect(struct radeonfb *fb, int sx, int sy, int dx, int dy,
    int w, int h)
{
	uint32_t gmc;
	uint32_t dir;

	radeonfb_wait_fifo(fb, 1);
	bus_space_write_4(fb->mmiot, fb->mmioh, RADEON_WAIT_UNTIL,
	    RADEON_WAIT_HOST_IDLECLEAN | RADEON_WAIT_2D_IDLECLEAN);

	if (dy < sy) {
		dir = RADEON_DST_Y_TOP_TO_BOTTOM;
	} else {
		sy += h - 1;
		dy += h - 1;
		dir = 0;
	}
	if (dx < sx) {
		dir |= RADEON_DST_X_LEFT_TO_RIGHT;
	} else {
		sx += w - 1;
		dx += w - 1;
	}

	radeonfb_wait_fifo(fb, 6);

	gmc = RADEON_GMC_DST_16BPP;
	gmc |= RADEON_GMC_BRUSH_NONE;
	gmc |= RADEON_GMC_SRC_DATATYPE_COLOR;
	gmc |= RADEON_GMC_SRC_SOURCE_MEMORY;
	gmc |= RADEON_ROP3_S;
	gmc |= RADEON_GMC_CLR_CMP_CNTL_DIS;
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DP_GUI_MASTER_CNTL, gmc);
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DP_WRITE_MASK, 0xffffffff);
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DP_CNTL, dir);

	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_SRC_Y_X, RADEON_COORDS(sx, sy));
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DST_Y_X, RADEON_COORDS(dx, dy));
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DST_HEIGHT_WIDTH, RADEON_COORDS(w, h));

	radeonfb_wait(fb);
}

void
radeonfb_fillrect(struct radeonfb *fb, int x, int y, int w, int h, int color)
{
	uint32_t gmc;

	radeonfb_wait_fifo(fb, 1);
	bus_space_write_4(fb->mmiot, fb->mmioh, RADEON_WAIT_UNTIL,
	    RADEON_WAIT_HOST_IDLECLEAN | RADEON_WAIT_2D_IDLECLEAN);

	radeonfb_wait_fifo(fb, 6);

	gmc = RADEON_GMC_DST_16BPP;
	gmc |= RADEON_GMC_BRUSH_SOLID_COLOR;
	gmc |= RADEON_GMC_SRC_DATATYPE_COLOR;
	gmc |= RADEON_ROP3_P;
	gmc |= RADEON_GMC_CLR_CMP_CNTL_DIS;
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DP_GUI_MASTER_CNTL, gmc);
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DP_BRUSH_FRGD_CLR, color);
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DP_WRITE_MASK, 0xffffffff);
	bus_space_write_4(fb->mmiot, fb->mmioh, RADEON_DP_CNTL,
	    RADEON_DST_Y_TOP_TO_BOTTOM | RADEON_DST_X_LEFT_TO_RIGHT);

	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DST_Y_X, RADEON_COORDS(x, y));
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DST_HEIGHT_WIDTH, RADEON_COORDS(w, h));

        radeonfb_wait(fb);
}

/*
 * Frame buffer initialization.
 */

int
radeonfb_setup(struct radeonfb *fb)
{
	struct rasops_info *ri;
	uint width, height, bpp;

	/*
	 * The firmware sets up the framebuffer such that at starts at
	 * an offset from the start of video memory.
	 */
	fb->memoff =
	    bus_space_read_4(fb->mmiot, fb->mmioh, RADEON_CRTC_OFFSET);

	width = 800;	/* XXX */
	height = 600;	/* XXX */
	bpp = 16;

	ri = &fb->ri;
	ri->ri_width = width;
	ri->ri_height = height;
	ri->ri_depth = bpp;
	ri->ri_stride = (ri->ri_width * ri->ri_depth) / 8;
	ri->ri_flg = RI_CENTER | RI_CLEAR | RI_FULLCLEAR;
	ri->ri_bits = (void *)(bus_space_vaddr(fb->fbt, fb->fbh) + fb->memoff);
	ri->ri_hw = fb;

	ri->ri_rnum = 5;
	ri->ri_rpos = 11;
	ri->ri_gnum = 6;
	ri->ri_gpos = 5;
	ri->ri_bnum = 5;
	ri->ri_bpos = 0;

	radeonfb_wait_fifo(fb, 2);
	bus_space_write_4(fb->mmiot, fb->mmioh, RADEON_DEFAULT_PITCH_OFFSET,
	    ((ri->ri_stride >> 6) << 22) | (fb->memoff >> 10));
	bus_space_write_4(fb->mmiot, fb->mmioh,
	    RADEON_DEFAULT_SC_BOTTOM_RIGHT, 0x1fff1fff);

	rasops_init(ri, 160, 160);

	strlcpy(fb->wsd.name, "std", sizeof(fb->wsd.name));
	fb->wsd.ncols = ri->ri_cols;
	fb->wsd.nrows = ri->ri_rows;
	fb->wsd.textops = &ri->ri_ops;
	fb->wsd.fontwidth = ri->ri_font->fontwidth;
	fb->wsd.fontheight = ri->ri_font->fontheight;
	fb->wsd.capabilities = ri->ri_caps;

#if 0
	ri->ri_ops.copyrows = radeonfb_copyrows;
	ri->ri_ops.copycols = radeonfb_copycols;
	ri->ri_ops.eraserows = radeonfb_eraserows;
	ri->ri_ops.erasecols = radeonfb_erasecols;
#endif

	return 0;
}

/*
 * Early console code
 */

int radeonfb_cnattach(bus_space_tag_t, bus_space_tag_t, pcitag_t, pcireg_t);

int
radeonfb_cnattach(bus_space_tag_t memt, bus_space_tag_t iot, pcitag_t tag,
    pcireg_t id)
{
	uint32_t defattr;
	struct rasops_info *ri;
	pcireg_t bar;
	int rc;

	/* filter out unrecognized devices */
	switch (id) {
	default:
		return ENODEV;
	case PCI_ID_CODE(PCI_VENDOR_ATI, 0x9615):
		break;
	}

	bar = pci_conf_read_early(tag, RADEON_PCI_MEM);
	if (PCI_MAPREG_TYPE(bar) != PCI_MAPREG_TYPE_MEM)
		return EINVAL;
	radeonfbcn.fbt = memt;
	rc = bus_space_map(memt, PCI_MAPREG_MEM_ADDR(bar), 1 /* XXX */,
	    BUS_SPACE_MAP_LINEAR, &radeonfbcn.fbh);
	if (rc != 0)
		return rc;

	bar = pci_conf_read_early(tag, RADEON_PCI_MMIO);
	if (PCI_MAPREG_TYPE(bar) != PCI_MAPREG_TYPE_MEM)
		return EINVAL;
	radeonfbcn.mmiot = memt;
	rc = bus_space_map(memt, PCI_MAPREG_MEM_ADDR(bar), 1 /* XXX */,
	    BUS_SPACE_MAP_LINEAR, &radeonfbcn.mmioh);
	if (rc != 0)
		return rc;

	rc = radeonfb_setup(&radeonfbcn);
	if (rc != 0)
		return rc;

	ri = &radeonfbcn.ri;
	ri->ri_ops.pack_attr(ri, 0, 0, 0, &defattr);
	wsdisplay_cnattach(&radeonfbcn.wsd, ri, 0, 0, defattr);

	return 0;
}
