/*	$OpenBSD: gfxp.c,v 1.16 2022/07/15 17:57:26 kettenis Exp $	*/

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

/*
 * The Permedia 2 provides two views into its 64k register file.  The
 * first view is little-endian, the second is big-endian and
 * immediately follows the little-endian view.  Since bus_space(9)
 * already does the byte order conversion for us, we use the
 * little-endian view.
 *
 * There are also little-endian and big-endian views into the
 * framebuffer.  These are made available through separate BARs.  We
 * use the big-endian view in this driver to avoid unnecessary byte
 * swapping in rasops(9).
 */
#define PM2_PCI_MMIO		0x10 	/* Registers */
#define PM2_PCI_MEM_LE		0x14 	/* Framebuffer (little-endian) */
#define PM2_PCI_MEM_BE		0x18	/* Framebuffer (big-endian) */

#define PM2_IN_FIFO_SPACE	0x0018
#define PM2_OUT_FIFO_SPACE	0x0020
#define PM2_DMA_COUNT		0x0030

#define PM2_OUT_FIFO		0x2000
#define  PM2_SYNC_TAG			0x00000188

#define PM2_PALETTE_WRITE_ADDR	0x4000
#define PM2_PALETTE_DATA	0x4008

#define PM2V_INDEX_LOW		0x4020
#define PM2V_INDEX_HIGH		0x4028
#define PM2V_INDEX_DATA		0x4030
#define  PM2V_CURSOR_MODE		0x0005
#define  PM2V_CURSOR_PATTERN		0x0400

#define PM2_RENDER		0x8038
#define  PM2_RENDER_FASTFILL		0x00000008
#define  PM2_RENDER_RECT		0x000000c0
#define  PM2_INCREASE_X			0x00200000
#define  PM2_INCREASE_Y			0x00400000
#define PM2_RECT_ORIG		0x80d0
#define PM2_RECT_SIZE		0x80d8

#define PM2_FB_READ_MODE	0x8a80
#define PM2_FB_BLOCK_COLOR	0x8ac8
#define PM2_FB_READ_PIXEL	0x8ad0

#define PM2_FILTER_MODE		0x8c00
#define  PM2_FM_PASS_SYNC_TAG		0x00000400
#define PM2_SYNC		0x8c40

#define PM2_FB_SRC_DELTA	0x8d88
#define PM2_CONFIG		0x8d90
#define  PM2_CONFIG_FB_READ_SRC_EN	0x00000001
#define  PM2_CONFIG_FB_WRITE_EN		0x00000008

#define PM2_COORDS(x, y)	((y) << 16 | (x))


#ifdef APERTURE
extern int allowaperture;
#endif

struct gfxp_softc {
	struct sunfb	sc_sunfb;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	bus_addr_t	sc_membase_le;
	bus_size_t	sc_memsize_le;
	bus_addr_t	sc_membase_be;
	bus_size_t	sc_memsize_be;

	bus_space_tag_t	sc_mmiot;
	bus_space_handle_t sc_mmioh;
	bus_addr_t	sc_mmiobase;
	bus_size_t	sc_mmiosize;

	pcitag_t	sc_pcitag;

	int		sc_mode;
	u_int8_t	sc_cmap_red[256];
	u_int8_t	sc_cmap_green[256];
	u_int8_t	sc_cmap_blue[256];

	/* Saved state to clean up after X11. */
	uint32_t	sc_read_mode;
	uint32_t	sc_read_pixel;
};

int	gfxp_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	gfxp_mmap(void *, off_t, int);

struct wsdisplay_accessops gfxp_accessops = {
	.ioctl = gfxp_ioctl,
	.mmap = gfxp_mmap
};

int	gfxp_match(struct device *, void *, void *);
void	gfxp_attach(struct device *, struct device *, void *);

const struct cfattach gfxp_ca = {
	sizeof(struct gfxp_softc), gfxp_match, gfxp_attach
};

struct cfdriver gfxp_cd = {
	NULL, "gfxp", DV_DULL
};

int	gfxp_is_console(int);
int	gfxp_getcmap(struct gfxp_softc *, struct wsdisplay_cmap *);
int	gfxp_putcmap(struct gfxp_softc *, struct wsdisplay_cmap *);
void	gfxp_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

int	gfxp_copycols(void *, int, int, int, int);
int	gfxp_erasecols(void *, int, int, int, uint32_t);
int	gfxp_copyrows(void *, int, int, int);
int	gfxp_eraserows(void *, int, int, uint32_t);

void	gfxp_init(struct gfxp_softc *);
void	gfxp_reinit(struct gfxp_softc *);

void	gfxp_indexed_write(struct gfxp_softc *, bus_size_t, uint32_t);
int	gfxp_wait(struct gfxp_softc *);
int	gfxp_wait_fifo(struct gfxp_softc *, int);
void	gfxp_copyrect(struct gfxp_softc *, int, int, int, int, int, int);
void	gfxp_fillrect(struct gfxp_softc *, int, int, int, int, int);

int
gfxp_match(struct device *parent, void *cf, void *aux)
{
	struct pci_attach_args *pa = aux;
	int node;
	char *name;

	node = PCITAG_NODE(pa->pa_tag);
	name = getpropstring(node, "name");
	if (strcmp(name, "TECH-SOURCE,gfxp") == 0 ||
	    strcmp(name, "TSI,gfxp") == 0)
		return (10);

	return (0);
}

void
gfxp_attach(struct device *parent, struct device *self, void *aux)
{
	struct gfxp_softc *sc = (struct gfxp_softc *)self;
	struct pci_attach_args *pa = aux;
	struct rasops_info *ri;
	int node, console, flags;
	char *model;

	sc->sc_pcitag = pa->pa_tag;

	node = PCITAG_NODE(pa->pa_tag);
	console = gfxp_is_console(node);

	printf("\n");

	model = getpropstring(node, "model");
	printf("%s: %s", self->dv_xname, model);

	if (pci_mapreg_info(pa->pa_pc, pa->pa_tag, PM2_PCI_MEM_LE,
	    PCI_MAPREG_TYPE_MEM, &sc->sc_membase_le, &sc->sc_memsize_le, NULL))
		sc->sc_memsize_le = 0;

	if (pci_mapreg_map(pa, PM2_PCI_MEM_BE, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_memt, &sc->sc_memh,
	    &sc->sc_membase_be, &sc->sc_memsize_be, 0)) {
		printf("\n%s: can't map video memory\n", self->dv_xname);
		return;
	}

	if (pci_mapreg_map(pa, PM2_PCI_MMIO, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_mmiot, &sc->sc_mmioh, &sc->sc_mmiobase,
	    &sc->sc_mmiosize, 0)) {
		bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_memsize_be);
		printf("\n%s: can't map mmio\n", self->dv_xname);
		return;
	}

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	ri = &sc->sc_sunfb.sf_ro;
	ri->ri_bits = bus_space_vaddr(sc->sc_memt, sc->sc_memh);
	ri->ri_hw = sc;

	flags = RI_BSWAP;
	if (sc->sc_sunfb.sf_depth == 32) {
		ri->ri_rnum = 8;
		ri->ri_rpos = 16;
		ri->ri_gnum = 8;
		ri->ri_gpos = 8;
		ri->ri_bnum = 8;
		ri->ri_bpos = 0;
		flags &= ~RI_BSWAP;
	}

	fbwscons_init(&sc->sc_sunfb, flags, console);
	fbwscons_setcolormap(&sc->sc_sunfb, gfxp_setcolor);
	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	gfxp_init(sc);
	ri->ri_ops.copyrows = gfxp_copyrows;
	ri->ri_ops.copycols = gfxp_copycols;
	ri->ri_ops.eraserows = gfxp_eraserows;
	ri->ri_ops.erasecols = gfxp_erasecols;

	if (console)
		fbwscons_console_init(&sc->sc_sunfb, -1);
	fbwscons_attach(&sc->sc_sunfb, &gfxp_accessops, console);
}

int
gfxp_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct gfxp_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_GFXP;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL) {
			fbwscons_setcolormap(&sc->sc_sunfb, gfxp_setcolor);

			/* Clean up the mess left behind by X. */
			gfxp_reinit(sc);
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
		return gfxp_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return gfxp_putcmap(sc, (struct wsdisplay_cmap *)data);

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
gfxp_mmap(void *v, off_t off, int prot)
{
	struct gfxp_softc *sc = v;

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

		if (off >= sc->sc_membase_be &&
		    off < (sc->sc_membase_be + sc->sc_memsize_be))
			return (bus_space_mmap(sc->sc_memt,
			    sc->sc_membase_be, off - sc->sc_membase_be,
			    prot, BUS_SPACE_MAP_LINEAR));

		if (off >= sc->sc_mmiobase &&
		    off < (sc->sc_mmiobase + sc->sc_mmiosize))
			return (bus_space_mmap(sc->sc_mmiot,
			    sc->sc_mmiobase, off - sc->sc_mmiobase,
			    prot, BUS_SPACE_MAP_LINEAR));
		break;

	case WSDISPLAYIO_MODE_DUMBFB:
		if (off >= 0 && off < sc->sc_memsize_le)
			return (bus_space_mmap(sc->sc_memt, sc->sc_membase_le,
			    off, prot, BUS_SPACE_MAP_LINEAR));
		break;
	}

	return (-1);
}

int
gfxp_is_console(int node)
{
	extern int fbnode;

	return (fbnode == node);
}

int
gfxp_getcmap(struct gfxp_softc *sc, struct wsdisplay_cmap *cm)
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
gfxp_putcmap(struct gfxp_softc *sc, struct wsdisplay_cmap *cm)
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

	gfxp_wait_fifo(sc, 1);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2_PALETTE_WRITE_ADDR, index);
	for (i = 0; i < count; i++) {
		gfxp_wait_fifo(sc, 3);
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    PM2_PALETTE_DATA, *r);
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    PM2_PALETTE_DATA, *g);
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    PM2_PALETTE_DATA, *b);
		r++, g++, b++;
	}
	return (0);
}

void
gfxp_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct gfxp_softc *sc = v;

	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;

	gfxp_wait_fifo(sc, 4);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2_PALETTE_WRITE_ADDR, index);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_PALETTE_DATA, r);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_PALETTE_DATA, g);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_PALETTE_DATA, b);
}

/*
 * Accelerated routines.
 */

int
gfxp_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct gfxp_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	gfxp_copyrect(sc, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight);

	return 0;
}

int
gfxp_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct gfxp_softc *sc = ri->ri_hw;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= ri->ri_font->fontheight;
	col *= ri->ri_font->fontwidth;
	num *= ri->ri_font->fontwidth;

	gfxp_fillrect(sc, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight, ri->ri_devcmap[bg]);

	return 0;
}

int
gfxp_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct gfxp_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	gfxp_copyrect(sc, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);

	return 0;
}

int
gfxp_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct gfxp_softc *sc = ri->ri_hw;
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
	gfxp_fillrect(sc, x, y, w, num, ri->ri_devcmap[bg]);

	return 0;
}

void
gfxp_init(struct gfxp_softc *sc)
{
	/* XXX Save. */
	sc->sc_read_mode = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2_FB_READ_MODE);
	sc->sc_read_pixel = bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2_FB_READ_PIXEL);
}

void
gfxp_reinit(struct gfxp_softc *sc)
{
	struct rasops_info *ri = &sc->sc_sunfb.sf_ro;
	int i;

	/* XXX Restore. */
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2_FB_READ_MODE, sc->sc_read_mode);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2_FB_READ_PIXEL, sc->sc_read_pixel);

	/* Disable cursor. */
	gfxp_indexed_write(sc, PM2V_CURSOR_MODE, 0x10);

	/* Clear cursor image. */
	for (i = 0; i < 1024; i++)
		gfxp_indexed_write(sc, PM2V_CURSOR_PATTERN + i, 0x00);

	/* Clear screen. */
	gfxp_fillrect(sc, 0, 0, ri->ri_width, ri->ri_height,
	    ri->ri_devcmap[WSCOL_WHITE]);
}

void
gfxp_indexed_write(struct gfxp_softc *sc, bus_size_t offset, uint32_t value)
{
	gfxp_wait_fifo(sc, 3);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2V_INDEX_HIGH, offset >> 8);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2V_INDEX_LOW, offset & 0xff);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2V_INDEX_DATA, value);
}

int
gfxp_wait_fifo(struct gfxp_softc *sc, int n)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		if (bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		     PM2_IN_FIFO_SPACE) >= n)
			break;
		DELAY(1);
	}

	return i;
}

int
gfxp_wait(struct gfxp_softc *sc)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		if (bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		    PM2_DMA_COUNT) == 0)
			break;
		DELAY(1);
	}

	/*
	 * Insert a sync into the FIFO...
	 */
	gfxp_wait_fifo(sc, 2);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
	    PM2_FILTER_MODE, PM2_FM_PASS_SYNC_TAG);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_SYNC, 0);

	/*
	 * ...and wait for it to appear on the other end, indicating
	 * completion of the operations before it.
	 */
	for (i = 1000000; i != 0; i--) {
		if (bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		    PM2_OUT_FIFO_SPACE) > 0 &&
		    bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		    PM2_OUT_FIFO) == PM2_SYNC_TAG)
			break;
		DELAY(1);
	}

	return i;
}

void
gfxp_copyrect(struct gfxp_softc *sc, int sx, int sy, int dx, int dy,
    int w, int h)
{
	int dir = 0;

	if (sx > dx)
		dir |= PM2_INCREASE_X;
	if (sy > dy)
		dir |= PM2_INCREASE_Y;

	gfxp_wait_fifo(sc, 5);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_CONFIG,
	    PM2_CONFIG_FB_WRITE_EN | PM2_CONFIG_FB_READ_SRC_EN);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_FB_SRC_DELTA,
	    PM2_COORDS((sx - dx) & 0xffff, (sy - dy) & 0xffff));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_RECT_ORIG,
	    PM2_COORDS(dx, dy));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_RECT_SIZE,
	    PM2_COORDS(w, h));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_RENDER,
	    PM2_RENDER_RECT | dir);

	gfxp_wait(sc);
}

void
gfxp_fillrect(struct gfxp_softc *sc, int x, int y, int w, int h, int color)
{
	gfxp_wait_fifo(sc, 5);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_CONFIG,
	    PM2_CONFIG_FB_WRITE_EN);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_RECT_ORIG,
	    PM2_COORDS(x, y));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_RECT_SIZE,
	    PM2_COORDS(w, h));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_FB_BLOCK_COLOR,
	    color);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, PM2_RENDER,
	    PM2_RENDER_RECT | PM2_RENDER_FASTFILL);

	gfxp_wait(sc);
}
