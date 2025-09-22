/*	$OpenBSD: radeonfb.c,v 1.8 2022/07/15 17:57:26 kettenis Exp $	*/

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

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/openfirm.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <dev/rasops/rasops.h>

#include <machine/fbvar.h>

#define RADEON_PCI_MEM		0x10
#define RADEON_PCI_MMIO		0x18

#define RADEON_PALETTE_INDEX		0x00b0
#define RADEON_PALETTE_DATA		0x00b4

#define RADEON_CRTC_OFFSET		0x0224

#define RADEON_SURFACE_CNTL		0x0b00
#define  RADEON_NONSURF_AP0_SWP_16BPP	0x00100000
#define  RADEON_NONSURF_AP0_SWP_32BPP	0x00200000
#define  RADEON_NONSURF_AP1_SWP_16BPP	0x00400000
#define  RADEON_NONSURF_AP1_SWP_32BPP	0x00800000

#define RADEON_RBBM_STATUS		0x0e40
#define  RADEON_RBBM_FIFOCNT_MASK	0x0000007f
#define  RADEON_RBBM_ACTIVE		0x80000000

#define RADEON_SRC_Y_X			0x1434
#define RADEON_DST_Y_X			0x1438
#define RADEON_DST_HEIGHT_WIDTH		0x143c

#define RADEON_DP_GUI_MASTER_CNTL	0x146c
#define  RADEON_GMC_DST_8BPP		0x00000200
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

#define RADEON_COORDS(x, y)	((y << 16) | (x))

#ifdef APERTURE
extern int allowaperture;
#endif

struct radeonfb_softc {
	struct sunfb	sc_sunfb;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	bus_addr_t	sc_membase;
	bus_size_t	sc_memsize;
	bus_size_t	sc_memoff;

	bus_space_tag_t	sc_mmiot;
	bus_space_handle_t sc_mmioh;
	bus_addr_t	sc_mmiobase;
	bus_size_t	sc_mmiosize;

	pcitag_t	sc_pcitag;

	int		sc_mode;
	u_int8_t	sc_cmap_red[256];
	u_int8_t	sc_cmap_green[256];
	u_int8_t	sc_cmap_blue[256];
};

int	radeonfb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	radeonfb_mmap(void *, off_t, int);

struct wsdisplay_accessops radeonfb_accessops = {
	.ioctl = radeonfb_ioctl,
	.mmap = radeonfb_mmap
};

int	radeonfb_match(struct device *, void *, void *);
void	radeonfb_attach(struct device *, struct device *, void *);

const struct cfattach radeonfb_ca = {
	sizeof(struct radeonfb_softc), radeonfb_match, radeonfb_attach
};

struct cfdriver radeonfb_cd = {
	NULL, "radeonfb", DV_DULL
};

int	radeonfb_is_console(int);
int	radeonfb_getcmap(struct radeonfb_softc *, struct wsdisplay_cmap *);
int	radeonfb_putcmap(struct radeonfb_softc *, struct wsdisplay_cmap *);
void	radeonfb_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

int	radeonfb_copycols(void *, int, int, int, int);
int	radeonfb_erasecols(void *, int, int, int, uint32_t);
int	radeonfb_copyrows(void *, int, int, int);
int	radeonfb_eraserows(void *, int, int, uint32_t);

void	radeonfb_init(struct radeonfb_softc *);
void	radeonfb_wait_fifo(struct radeonfb_softc *, int);
void	radeonfb_wait(struct radeonfb_softc *);
void	radeonfb_copyrect(struct radeonfb_softc *, int, int, int, int, int, int);
void	radeonfb_fillrect(struct radeonfb_softc *, int, int, int, int, int);

int
radeonfb_match(struct device *parent, void *cf, void *aux)
{
	struct pci_attach_args *pa = aux;
	char buf[32];
	int node;

	node = PCITAG_NODE(pa->pa_tag);
	OF_getprop(node, "name", buf, sizeof(buf));
	if (strcmp(buf, "SUNW,XVR-100") == 0 ||
	    strcmp(buf, "SUNW,XVR-300") == 0)
		return (10);

	return (0);
}

void
radeonfb_attach(struct device *parent, struct device *self, void *aux)
{
	struct radeonfb_softc *sc = (struct radeonfb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct rasops_info *ri;
	int node, console, flags;
	char *model;

	sc->sc_pcitag = pa->pa_tag;

	node = PCITAG_NODE(pa->pa_tag);
	console = radeonfb_is_console(node);

	printf("\n");

	model = getpropstring(node, "model");
	printf("%s: %s", self->dv_xname, model);

	if (pci_mapreg_map(pa, RADEON_PCI_MEM, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_memt, &sc->sc_memh,
	    &sc->sc_membase, &sc->sc_memsize, 0)) {
		printf("\n%s: can't map video memory\n", self->dv_xname);
		return;
	}

	if (pci_mapreg_map(pa, RADEON_PCI_MMIO, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_mmiot, &sc->sc_mmioh, &sc->sc_mmiobase,
	    &sc->sc_mmiosize, 0)) {
		printf("\n%s: can't map registers\n", self->dv_xname);
		return;
	}

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	/*
	 * The firmware sets up the framebuffer such that at starts at
	 * an offset from the start of video memory.
	 */
	sc->sc_memoff =
	    bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh, RADEON_CRTC_OFFSET);

	ri = &sc->sc_sunfb.sf_ro;
	ri->ri_bits = bus_space_vaddr(sc->sc_memt, sc->sc_memh);
	ri->ri_bits += sc->sc_memoff;
	ri->ri_hw = sc;

	if (sc->sc_sunfb.sf_depth == 32)
		flags = 0;
	else
		flags = RI_BSWAP;

	fbwscons_init(&sc->sc_sunfb, flags, console);
	fbwscons_setcolormap(&sc->sc_sunfb, radeonfb_setcolor);
	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	radeonfb_init(sc);
	ri->ri_ops.copyrows = radeonfb_copyrows;
	ri->ri_ops.copycols = radeonfb_copycols;
	ri->ri_ops.eraserows = radeonfb_eraserows;
	ri->ri_ops.erasecols = radeonfb_erasecols;

	if (console)
		fbwscons_console_init(&sc->sc_sunfb, -1);
	fbwscons_attach(&sc->sc_sunfb, &radeonfb_accessops, console);
}

int
radeonfb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct radeonfb_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_RADEONFB;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL) {
			struct rasops_info *ri = &sc->sc_sunfb.sf_ro;

			/* Restore colormap. */
			fbwscons_setcolormap(&sc->sc_sunfb, radeonfb_setcolor);

			/* Clear screen. */
			radeonfb_init(sc);
			radeonfb_fillrect(sc, 0, 0, ri->ri_width,
			    ri->ri_height, ri->ri_devcmap[WSCOL_WHITE]);
		} 
		break;

	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->stride = sc->sc_sunfb.sf_linebytes;
		wdf->offset = 0;
		if (sc->sc_sunfb.sf_depth == 32)
			wdf->cmsize = 0;
		else
			wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		if (sc->sc_sunfb.sf_depth == 32)
			*(u_int *)data = WSDISPLAYIO_DEPTH_24_32;
		else
			return (-1);
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		return radeonfb_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return radeonfb_putcmap(sc, (struct wsdisplay_cmap *)data);

	case WSDISPLAYIO_GPCIID:
		sel = (struct pcisel *)data;
		sel->pc_bus = PCITAG_BUS(sc->sc_pcitag);
		sel->pc_dev = PCITAG_DEV(sc->sc_pcitag);
		sel->pc_func = PCITAG_FUN(sc->sc_pcitag);
		break;

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	case WSDISPLAYIO_GCURPOS:
	case WSDISPLAYIO_SCURPOS:
	case WSDISPLAYIO_GCURMAX:
	case WSDISPLAYIO_GCURSOR:
	case WSDISPLAYIO_SCURSOR:
	default:
		return -1; /* not supported yet */
        }

	return (0);
}

paddr_t
radeonfb_mmap(void *v, off_t off, int prot)
{
	struct radeonfb_softc *sc = v;

	if (off & PGOFSET)
		return (-1);

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
			return (bus_space_mmap(sc->sc_memt,
			    sc->sc_membase, off - sc->sc_membase,
			    prot, BUS_SPACE_MAP_LINEAR));

		if (off >= sc->sc_mmiobase &&
		    off < (sc->sc_mmiobase + sc->sc_mmiosize))
			return (bus_space_mmap(sc->sc_mmiot,
			    sc->sc_mmiobase, off - sc->sc_mmiobase,
			    prot, BUS_SPACE_MAP_LINEAR));
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		if ((sc->sc_memoff % PAGE_SIZE) != 0)
			return (-1);

		if (off >= 0 && off < sc->sc_memsize / 2) {
			bus_addr_t base = sc->sc_membase + sc->sc_memoff;

			/*
			 * In 32bpp mode, use the second aperture,
			 * which has been set up by the firmware to do
			 * proper byte swapping.
			 */
			if (sc->sc_sunfb.sf_depth == 32)
				base += sc->sc_memsize / 2;

			return (bus_space_mmap(sc->sc_memt, base, off,
			    prot, BUS_SPACE_MAP_LINEAR));
		}
		break;
	}

	return (-1);
}

int
radeonfb_is_console(int node)
{
	extern int fbnode;

	return (fbnode == node);
}

int
radeonfb_getcmap(struct radeonfb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	error = copyout(&sc->sc_cmap_red[index], cm->red, count);
	if (error)
		return (error);
	error = copyout(&sc->sc_cmap_green[index], cm->green, count);
	if (error)
		return (error);
	error = copyout(&sc->sc_cmap_blue[index], cm->blue, count);
	if (error)
		return (error);
	return (0);
}

int
radeonfb_putcmap(struct radeonfb_softc *sc, struct wsdisplay_cmap *cm)
{
	u_int index = cm->index;
	u_int count = cm->count;
	u_int i;
	int error;
	u_char *r, *g, *b;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	if ((error = copyin(cm->red, &sc->sc_cmap_red[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->green, &sc->sc_cmap_green[index], count)) != 0)
		return (error);
	if ((error = copyin(cm->blue, &sc->sc_cmap_blue[index], count)) != 0)
		return (error);

	r = &sc->sc_cmap_red[index];
	g = &sc->sc_cmap_green[index];
	b = &sc->sc_cmap_blue[index];

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_PALETTE_INDEX, index);
	for (i = 0; i < count; i++) {
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    RADEON_PALETTE_DATA, (*r << 16) | (*g << 8) | *b);
		r++, g++, b++;
	}
	return (0);
}

void
radeonfb_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct radeonfb_softc *sc = v;

	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_PALETTE_INDEX, index);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_PALETTE_DATA, (r << 16) | (g << 8) | b);
}

/*
 * Accelerated routines.
 */

int
radeonfb_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct radeonfb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	radeonfb_copyrect(sc, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight);

	return 0;
}

int
radeonfb_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct radeonfb_softc *sc = ri->ri_hw;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= ri->ri_font->fontheight;
	col *= ri->ri_font->fontwidth;
	num *= ri->ri_font->fontwidth;

	radeonfb_fillrect(sc, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight, ri->ri_devcmap[bg]);

	return 0;
}

int
radeonfb_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct radeonfb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	radeonfb_copyrect(sc, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);

	return 0;
}

int
radeonfb_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct radeonfb_softc *sc = ri->ri_hw;
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
	radeonfb_fillrect(sc, x, y, w, num, ri->ri_devcmap[bg]);

	return 0;
}

void
radeonfb_init(struct radeonfb_softc *sc)
{
	radeonfb_wait_fifo(sc, 2);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DEFAULT_PITCH_OFFSET,
	    ((sc->sc_sunfb.sf_linebytes >> 6) << 22) | (sc->sc_memoff >> 10));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DEFAULT_SC_BOTTOM_RIGHT, 0x1fff1fff);
}

void
radeonfb_wait_fifo(struct radeonfb_softc *sc, int n)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		    RADEON_RBBM_STATUS) & RADEON_RBBM_FIFOCNT_MASK) >= n)
			break;
		DELAY(1);
	}
}

void
radeonfb_wait(struct radeonfb_softc *sc)
{
	int i;

	radeonfb_wait_fifo(sc, 64);

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		    RADEON_RBBM_STATUS) & RADEON_RBBM_ACTIVE) == 0)
			break;
		DELAY(1);
	}

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_RB3D_DSTCACHE_CTLSTAT, RADEON_RB3D_DC_FLUSH_ALL);

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		    RADEON_RB3D_DSTCACHE_CTLSTAT) & RADEON_RB3D_DC_BUSY) == 0)
			break;
		DELAY(1);
	}
}

void
radeonfb_copyrect(struct radeonfb_softc *sc, int sx, int sy, int dx, int dy,
    int w, int h)
{
	uint32_t gmc;
	uint32_t dir;

	radeonfb_wait_fifo(sc, 1);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, RADEON_WAIT_UNTIL,
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

	radeonfb_wait_fifo(sc, 6);

	if (sc->sc_sunfb.sf_depth == 32)
		gmc = RADEON_GMC_DST_32BPP;
	else
		gmc = RADEON_GMC_DST_8BPP;
	gmc |= RADEON_GMC_BRUSH_NONE;
	gmc |= RADEON_GMC_SRC_DATATYPE_COLOR;
	gmc |= RADEON_GMC_SRC_SOURCE_MEMORY;
	gmc |= RADEON_ROP3_S;
	gmc |= RADEON_GMC_CLR_CMP_CNTL_DIS;
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DP_GUI_MASTER_CNTL, gmc);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DP_WRITE_MASK, 0xffffffff);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DP_CNTL, dir);

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_SRC_Y_X, RADEON_COORDS(sx, sy));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DST_Y_X, RADEON_COORDS(dx, dy));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DST_HEIGHT_WIDTH, RADEON_COORDS(w, h));

	radeonfb_wait(sc);
}

void
radeonfb_fillrect(struct radeonfb_softc *sc, int x, int y, int w, int h,
    int color)
{
	uint32_t gmc;

	radeonfb_wait_fifo(sc, 1);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, RADEON_WAIT_UNTIL,
	    RADEON_WAIT_HOST_IDLECLEAN | RADEON_WAIT_2D_IDLECLEAN);

	radeonfb_wait_fifo(sc, 6);

	if (sc->sc_sunfb.sf_depth == 32)
		gmc = RADEON_GMC_DST_32BPP;
	else
		gmc = RADEON_GMC_DST_8BPP;
	gmc |= RADEON_GMC_BRUSH_SOLID_COLOR;
	gmc |= RADEON_GMC_SRC_DATATYPE_COLOR;
	gmc |= RADEON_ROP3_P;
	gmc |= RADEON_GMC_CLR_CMP_CNTL_DIS;
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DP_GUI_MASTER_CNTL, gmc);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DP_BRUSH_FRGD_CLR, color);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DP_WRITE_MASK, 0xffffffff);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, RADEON_DP_CNTL,
	    RADEON_DST_Y_TOP_TO_BOTTOM | RADEON_DST_X_LEFT_TO_RIGHT);

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DST_Y_X, RADEON_COORDS(x, y));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    RADEON_DST_HEIGHT_WIDTH, RADEON_COORDS(w, h));

        radeonfb_wait(sc);
}
