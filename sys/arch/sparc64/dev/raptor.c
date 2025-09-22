/*	$OpenBSD: raptor.c,v 1.12 2022/07/15 17:57:26 kettenis Exp $	*/

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

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

#include <dev/rasops/rasops.h>

#include <machine/fbvar.h>

/*
 * Tech Source uses the Raptor name for most of its graphics cards.
 * This driver supports the original Raptor GFX cards built around
 * the Number 9 Imagine-128 chips.
 *
 * Official documentation for the Imagine-128 isn't available.  The
 * information used for writing this driver comes mostly from the Xorg
 * i128 driver.
 */

#define I128_PCI_MW0		0x10
#define I128_PCI_MW1		0x14
#define I128_PCI_RBASE		0x20

#define I128_WR_ADR		0x0000
#define I128_PAL_DAT		0x0004
#define I128_PEL_MASK		0x0008

#define I128_INTM		0x4004
#define I128_FLOW		0x4008
#define  I128_FLOW_DEB		0x00000001
#define  I128_FLOW_MCB		0x00000002
#define  I128_FLOW_CLP		0x00000004
#define  I128_FLOW_PRV		0x00000008
#define  I128_FLOW_ACTIVE	0x0000000f
#define I128_BUSY		0x400c
#define  I128_BUSY_BUSY		0x00000001
#define I128_BUF_CTRL		0x4020
#define  I128_BC_PSIZ_8B	0x00000000
#define  I128_BC_PSIZ_16B	0x01000000
#define  I128_BC_PSIZ_32B	0x02000000
#define I128_DE_PGE		0x4024
#define I128_DE_SORG		0x4028
#define I128_DE_DORG		0x402c
#define I128_DE_MSRC		0x4030
#define I128_DE_WKEY		0x4038
#define I128_DE_ZPTCH		0x403c
#define I128_DE_SPTCH		0x4040
#define I128_DE_DPTCH		0x4044
#define I128_CMD		0x4048
#define I128_CMD_OPC		0x4050
#define  I128_CO_BITBLT		0x00000001
#define I128_CMD_ROP		0x4054
#define  I128_CR_COPY		0x0000000c
#define I128_CMD_STYLE		0x4058
#define  I128_CS_SOLID		0x00000001
#define I128_CMD_PATRN		0x405c
#define I128_CMD_CLP		0x4060
#define I128_CMD_HDF		0x4064
#define I128_FORE		0x4068
#define I128_MASK		0x4070
#define I128_RMSK		0x4074
#define I128_LPAT		0x4078
#define I128_PCTRL		0x407c
#define I128_CLPTL		0x4080
#define I128_CLPBR		0x4084
#define I128_XY0_SRC		0x4088
#define I128_XY1_DST		0x408c
#define I128_XY2_WH		0x4090
#define I128_XY3_DIR		0x4094
#define  I128_DIR_BT		0x00000001
#define  I128_DIR_RL		0x00000002
#define I128_XY4_ZM		0x4098
#define  I128_ZOOM_NONE		0x00000000
#define I128_ACNTRL		0x416c

#define I128_COORDS(x, y)	((x << 16) | (y))


#ifdef APERTURE
extern int allowaperture;
#endif

struct raptor_softc {
	struct sunfb	sc_sunfb;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	bus_addr_t	sc_membase;
	bus_size_t	sc_memsize;

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

int	raptor_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	raptor_mmap(void *, off_t, int);

struct wsdisplay_accessops raptor_accessops = {
	.ioctl = raptor_ioctl,
	.mmap = raptor_mmap
};

int	raptor_match(struct device *, void *, void *);
void	raptor_attach(struct device *, struct device *, void *);

const struct cfattach raptor_ca = {
	sizeof(struct raptor_softc), raptor_match, raptor_attach
};

struct cfdriver raptor_cd = {
	NULL, "raptor", DV_DULL
};

int	raptor_is_console(int);
int	raptor_getcmap(struct raptor_softc *, struct wsdisplay_cmap *);
int	raptor_putcmap(struct raptor_softc *, struct wsdisplay_cmap *);
void	raptor_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

int	raptor_copycols(void *, int, int, int, int);
int	raptor_erasecols(void *, int, int, int, uint32_t);
int	raptor_copyrows(void *, int, int, int);
int	raptor_eraserows(void *, int, int, uint32_t);

void	raptor_init(struct raptor_softc *);
int	raptor_wait(struct raptor_softc *);
void	raptor_copyrect(struct raptor_softc *, int, int, int, int, int, int);
void	raptor_fillrect(struct raptor_softc *, int, int, int, int, int);

int
raptor_match(struct device *parent, void *cf, void *aux)
{
	struct pci_attach_args *pa = aux;
	int node;
	char *name;

	node = PCITAG_NODE(pa->pa_tag);
	name = getpropstring(node, "name");
	if (strcmp(name, "TECH-SOURCE,raptor") == 0 ||
	    strcmp(name, "TSI,raptor") == 0)
		return (10);

	return (0);
}

void
raptor_attach(struct device *parent, struct device *self, void *aux)
{
	struct raptor_softc *sc = (struct raptor_softc *)self;
	struct pci_attach_args *pa = aux;
	struct rasops_info *ri;
	int node, console;
	char *model;

	sc->sc_pcitag = pa->pa_tag;

	node = PCITAG_NODE(pa->pa_tag);
	console = raptor_is_console(node);

	printf("\n");

	model = getpropstring(node, "model");
	printf("%s: %s", self->dv_xname, model);

	if (pci_mapreg_map(pa, I128_PCI_MW0, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_memt, &sc->sc_memh,
	    &sc->sc_membase, &sc->sc_memsize, 0)) {
		printf("\n%s: can't map video memory\n", self->dv_xname);
		return;
	}

	if (pci_mapreg_map(pa, I128_PCI_RBASE, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_mmiot, &sc->sc_mmioh, &sc->sc_mmiobase,
	    &sc->sc_mmiosize, 0)) {
		bus_space_unmap(sc->sc_memt, sc->sc_memh, sc->sc_memsize);
		printf("\n%s: can't map mmio\n", self->dv_xname);
		return;
	}

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	ri = &sc->sc_sunfb.sf_ro;
	ri->ri_bits = bus_space_vaddr(sc->sc_memt, sc->sc_memh);
	ri->ri_hw = sc;

	fbwscons_init(&sc->sc_sunfb, RI_BSWAP, console);
	fbwscons_setcolormap(&sc->sc_sunfb, raptor_setcolor);
	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	raptor_init(sc);
	ri->ri_ops.copyrows = raptor_copyrows;
	ri->ri_ops.copycols = raptor_copycols;
	ri->ri_ops.eraserows = raptor_eraserows;
	ri->ri_ops.erasecols = raptor_erasecols;

	if (console)
		fbwscons_console_init(&sc->sc_sunfb, -1);
	fbwscons_attach(&sc->sc_sunfb, &raptor_accessops, console);
}

int
raptor_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct raptor_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_RAPTOR;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL)
			fbwscons_setcolormap(&sc->sc_sunfb, raptor_setcolor);
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (void *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width  = sc->sc_sunfb.sf_width;
		wdf->depth  = sc->sc_sunfb.sf_depth;
		wdf->stride = sc->sc_sunfb.sf_linebytes;
		wdf->offset = 0;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		return raptor_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return raptor_putcmap(sc, (struct wsdisplay_cmap *)data);

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
raptor_mmap(void *v, off_t off, int prot)
{
	struct raptor_softc *sc = v;

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
		if (off >= 0 && off < sc->sc_memsize)
			return (bus_space_mmap(sc->sc_memt, sc->sc_membase,
			    off, prot, BUS_SPACE_MAP_LINEAR));
		break;
	}

	return (-1);
}

int
raptor_is_console(int node)
{
	extern int fbnode;

	return (fbnode == node);
}

int
raptor_getcmap(struct raptor_softc *sc, struct wsdisplay_cmap *cm)
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
raptor_putcmap(struct raptor_softc *sc, struct wsdisplay_cmap *cm)
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

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_PEL_MASK, 0xff);
	for (i = 0; i < count; i++) {
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_WR_ADR, index);
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_PAL_DAT, *r);
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_PAL_DAT, *g);
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_PAL_DAT, *b);
		r++, g++, b++, index++;
	}
	return (0);
}

void
raptor_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct raptor_softc *sc = v;

	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_PEL_MASK, 0xff);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_WR_ADR, index);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_PAL_DAT, r);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_PAL_DAT, g);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_PAL_DAT, b);
}

/*
 * Accelerated routines.
 */

int
raptor_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct raptor_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	raptor_copyrect(sc, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight);

	return 0;
}

int
raptor_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct raptor_softc *sc = ri->ri_hw;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= ri->ri_font->fontheight;
	col *= ri->ri_font->fontwidth;
	num *= ri->ri_font->fontwidth;

	raptor_fillrect(sc, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight, ri->ri_devcmap[bg]);

	return 0;
}

int
raptor_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct raptor_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	raptor_copyrect(sc, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);

	return 0;
}

int
raptor_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct raptor_softc *sc = ri->ri_hw;
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
	raptor_fillrect(sc, x, y, w, num, ri->ri_devcmap[bg]);

	return 0;
}

void
raptor_init(struct raptor_softc *sc)
{
	/* Configure pixel format based on depth. */
	switch(sc->sc_sunfb.sf_depth) {
	case 8:
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_BUF_CTRL, I128_BC_PSIZ_8B);
		break;
	case 16:
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_BUF_CTRL, I128_BC_PSIZ_16B);
		break;
	case 24:
	case 32:
		bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_BUF_CTRL, I128_BC_PSIZ_32B);
		break;
	default:
		panic("unsupported depth");
		break;
	}

	/* Mostly magic. */
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_PGE, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_SORG, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_DORG, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_MSRC, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_WKEY, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_SPTCH,
	    sc->sc_sunfb.sf_linebytes);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_DPTCH,
	    sc->sc_sunfb.sf_linebytes);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_DE_ZPTCH,
	    sc->sc_sunfb.sf_linebytes);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_RMSK, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY4_ZM,
	    I128_ZOOM_NONE);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_LPAT,
	    0xffffffff);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_PCTRL, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_CLPTL, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_CLPBR,
	    I128_COORDS(4095, 2047));
#if 0
	/* XXX For some reason this makes schizo(4) freak out. */
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_ACNTRL, 0);
#endif
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_INTM, 3);
}

int
raptor_wait(struct raptor_softc *sc)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
		    I128_FLOW) & I128_FLOW_ACTIVE) == 0)
			break;
		DELAY(1);
	}

	return i;
}

void
raptor_copyrect(struct raptor_softc *sc, int sx, int sy, int dx, int dy,
    int w, int h)
{
	int dir = 0;

	while (bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    I128_BUSY) & I128_BUSY_BUSY)
		DELAY(1);

	if (sx < dx) {
		sx += w - 1;
		dx += w - 1;
		dir |= I128_DIR_RL;
	}
	if (sy < dy) {
		sy += h - 1;
		dy += h - 1;
		dir |= I128_DIR_BT;
	}

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_CMD,
	    I128_CR_COPY << 8 | I128_CO_BITBLT);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY3_DIR, dir);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY2_WH,
	    I128_COORDS(w , h));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY0_SRC,
	    I128_COORDS(sx, sy));
	/* Must be last; triggers operation. */
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY1_DST,
	    I128_COORDS(dx, dy));

	raptor_wait(sc);
}

void
raptor_fillrect(struct raptor_softc *sc, int x, int y, int w, int h, int color)
{
	while (bus_space_read_4(sc->sc_mmiot, sc->sc_mmioh,
	    I128_BUSY) & I128_BUSY_BUSY)
		DELAY(1);

	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_CMD,
	    I128_CS_SOLID << 16 | I128_CR_COPY << 8 | I128_CO_BITBLT);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_FORE, color);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY3_DIR, 0);
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY2_WH,
	    I128_COORDS(w, h));
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY0_SRC, 0);
	/* Must be last; triggers operation. */
	bus_space_write_4(sc->sc_mmiot, sc->sc_mmioh, I128_XY1_DST,
	    I128_COORDS(x, y));

	raptor_wait(sc);
}
