/*	$OpenBSD: machfb.c,v 1.13 2022/07/15 17:57:26 kettenis Exp $	*/

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

#define M64_PCI_MEM		0x10
#define M64_PCI_MMIO		0x18

#define M64_REG_OFF		0x007ffc00
#define M64_REG_SIZE		0x0400

#define M64_CRTC_INT_CNTL	0x0018

#define M64_BUS_CNTL		0x00a0
#define  M64_BUS_FIFO_ERR_ACK	0x00200000
#define  M64_BUS_HOST_ERR_ACK	0x00800000
#define  M64_BUS_APER_REG_DIS	0x00000010

#define M64_DAC_WINDEX		0x00c0
#define M64_DAC_DATA		0x00c1
#define M64_DAC_MASK		0x00c2
#define M64_DAC_RINDEX		0x00c3
#define M64_DAC_CNTL		0x00c4
#define  M64_DAC_8BIT_EN		0x00000100

#define M64_GEN_TEST_CNTL	0x00d0
#define  M64_GEN_GUI_EN			0x00000100

#define M64_DST_OFF_PITCH	0x0100
#define M64_DST_X		0x0104
#define M64_DST_Y		0x0108
#define M64_DST_Y_X		0x010c
#define M64_DST_WIDTH		0x0110
#define M64_DST_HEIGHT		0x0114
#define M64_DST_HEIGHT_WIDTH	0x0118
#define M64_DST_X_WIDTH		0x011c
#define M64_DST_BRES_LNTH	0x0120
#define M64_DST_BRES_ERR	0x0124
#define M64_DST_BRES_INC	0x0128
#define M64_DST_BRES_DEC	0x012c
#define M64_DST_CNTL		0x0130
#define  M64_DST_X_RIGHT_TO_LEFT	0x00000000
#define  M64_DST_X_LEFT_TO_RIGHT	0x00000001
#define  M64_DST_Y_BOTTOM_TO_TOP	0x00000000
#define  M64_DST_Y_TOP_TO_BOTTOM	0x00000002
#define  M64_DST_X_MAJOR		0x00000000
#define  M64_DST_Y_MAJOR		0x00000004
#define  M64_DST_X_TILE			0x00000008
#define  M64_DST_Y_TILE			0x00000010
#define  M64_DST_LAST_PEL		0x00000020
#define  M64_DST_POLYGON_EN		0x00000040
#define  M64_DST_24_ROT_EN		0x00000080

#define M64_SRC_OFF_PITCH	0x0180
#define M64_SRC_X		0x0184
#define M64_SRC_Y		0x0188
#define M64_SRC_Y_X		0x018c
#define M64_SRC_WIDTH1		0x0190
#define M64_SRC_HEIGHT1		0x0194
#define M64_SRC_HEIGHT1_WIDTH1	0x0198
#define M64_SRC_X_START		0x019c
#define M64_SRC_Y_START		0x01a0
#define M64_SRC_Y_X_START	0x01a4
#define M64_SRC_WIDTH2		0x01a8
#define M64_SRC_HEIGHT2		0x01ac
#define M64_SRC_HEIGHT2_WIDTH2	0x01b0
#define M64_SRC_CNTL		0x01b4
#define  M64_SRC_PATT_EN		0x00000001
#define  M64_SRC_PATT_ROT_EN		0x00000002
#define  M64_SRC_LINEAR_EN		0x00000004
#define  M64_SRC_BYTE_ALIGN 		0x00000008
#define  M64_SRC_LINE_X_RIGHT_TO_LEFT	0x00000000
#define  M64_SRC_LINE_X_LEFT_TO_RIGHT	0x00000010

#define M64_HOST_CNTL		0x0240

#define M64_PAT_REG0		0x0280
#define M64_PAT_REG1		0x0284
#define M64_PAT_CNTL		0x0288

#define M64_SC_LEFT		0x02a0
#define M64_SC_RIGHT		0x02a4
#define M64_SC_LEFT_RIGHT	0x02a8
#define M64_SC_TOP		0x02ac
#define M64_SC_BOTTOM		0x02b0
#define M64_SC_TOP_BOTTOM	0x02b4

#define M64_DP_BKGD_CLR		0x02c0
#define M64_DP_FRGD_CLR		0x02c4
#define M64_DP_WRITE_MASK	0x02c8

#define M64_DP_CHAIN_MASK	0x02cc
#define  M64_DP_CHAIN_8BPP		0x00008080
#define M64_DP_PIX_WIDTH	0x02d0
#define  M64_DST_8BPP			0x00000002
#define  M64_SRC_8BPP			0x00000200
#define  M64_HOST_8BPP			0x00020000
#define M64_DP_MIX		0x02d4
#define  M64_MIX_DST			0x00000003
#define  M64_MIX_SRC			0x00000007
#define M64_DP_SRC           0x02d8
#define  M64_BKGD_SRC_BKGD_CLR		0x00000000
#define  M64_BKGD_SRC_FRGD_CLR		0x00000001
#define  M64_BKGD_SRC_HOST		0x00000002
#define  M64_BKGD_SRC_BLIT		0x00000003
#define  M64_BKGD_SRC_PATTERN		0x00000004
#define  M64_FRGD_SRC_BKGD_CLR		0x00000000
#define  M64_FRGD_SRC_FRGD_CLR		0x00000100
#define  M64_FRGD_SRC_HOST		0x00000200
#define  M64_FRGD_SRC_BLIT		0x00000300
#define  M64_FRGD_SRC_PATTERN		0x00000400
#define  M64_MONO_SRC_ONE		0x00000000
#define  M64_MONO_SRC_PATTERN		0x00010000
#define  M64_MONO_SRC_HOST		0x00020000
#define  M64_MONO_SRC_BLIT		0x00030000

#define M64_CLR_CMP_CLR		0x0300
#define M64_CLR_CMP_MASK	0x0304
#define M64_CLR_CMP_CNTL	0x0308

#define M64_FIFO_STAT		0x0310
#define  M64_FIFO_STAT_MASK		0x0000ffff

#define M64_CONTEXT_MASK	0x0320

#define M64_GUI_TRAJ_CNTL	0x0330
#define M64_GUI_STAT		0x0338
#define  M64_GUI_ACTIVE		 0x00000001

#define M64_COORDS(x, y)	((x << 16) | (y))

#ifdef APERTURE
extern int allowaperture;
#endif

struct machfb_softc {
	struct sunfb	sc_sunfb;

	bus_space_tag_t sc_memt;
	bus_space_handle_t sc_memh;
	bus_addr_t	sc_membase;
	bus_size_t	sc_memsize;

	bus_space_tag_t	sc_regt;
	bus_space_handle_t sc_regh;

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

int	machfb_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	machfb_mmap(void *, off_t, int);

struct wsdisplay_accessops machfb_accessops = {
	.ioctl = machfb_ioctl,
	.mmap = machfb_mmap
};

int	machfb_match(struct device *, void *, void *);
void	machfb_attach(struct device *, struct device *, void *);

const struct cfattach machfb_ca = {
	sizeof(struct machfb_softc), machfb_match, machfb_attach
};

struct cfdriver machfb_cd = {
	NULL, "machfb", DV_DULL
};

int	machfb_is_console(int);
int	machfb_getcmap(struct machfb_softc *, struct wsdisplay_cmap *);
int	machfb_putcmap(struct machfb_softc *, struct wsdisplay_cmap *);
void	machfb_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

int	machfb_copycols(void *, int, int, int, int);
int	machfb_erasecols(void *, int, int, int, uint32_t);
int	machfb_copyrows(void *, int, int, int);
int	machfb_eraserows(void *, int, int, uint32_t);

void	machfb_init(struct machfb_softc *);
int	machfb_wait_fifo(struct machfb_softc *, int);
int	machfb_wait(struct machfb_softc *);
void	machfb_copyrect(struct machfb_softc *, int, int, int, int, int, int);
void	machfb_fillrect(struct machfb_softc *, int, int, int, int, int);

int
machfb_match(struct device *parent, void *cf, void *aux)
{
	struct pci_attach_args *pa = aux;
	char buf[32];
	int node;

	node = PCITAG_NODE(pa->pa_tag);
	OF_getprop(node, "name", buf, sizeof(buf));
	if (strcmp(buf, "SUNW,m64B") == 0)
		return (10);

	if (OF_getprop(node, "compatible", buf, sizeof(buf)) > 0 &&
	    strcmp(buf, "SUNW,m64B") == 0)
		return (10);

	return (0);
}

void
machfb_attach(struct device *parent, struct device *self, void *aux)
{
	struct machfb_softc *sc = (struct machfb_softc *)self;
	struct pci_attach_args *pa = aux;
	struct rasops_info *ri;
	int node, console;
	char *model;

	sc->sc_pcitag = pa->pa_tag;

	node = PCITAG_NODE(pa->pa_tag);
	console = machfb_is_console(node);

	printf("\n");

	model = getpropstring(node, "model");
	printf("%s: %s", self->dv_xname, model);

	if (pci_mapreg_map(pa, M64_PCI_MEM, PCI_MAPREG_TYPE_MEM,
	    BUS_SPACE_MAP_LINEAR, &sc->sc_memt, &sc->sc_memh,
	    &sc->sc_membase, &sc->sc_memsize, 0)) {
		printf("\n%s: can't map video memory\n", self->dv_xname);
		return;
	}

	sc->sc_regt = sc->sc_memt;
	if (bus_space_subregion(sc->sc_memt, sc->sc_memh,
	    M64_REG_OFF, M64_REG_SIZE, &sc->sc_regh)) {
		printf("\n%s: can't map registers\n", self->dv_xname);
		return;
	}

	if (pci_mapreg_map(pa, M64_PCI_MMIO, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_mmiot, &sc->sc_mmioh, &sc->sc_mmiobase,
	    &sc->sc_mmiosize, 0)) {
		printf("\n%s: can't map registers\n", self->dv_xname);
		return;
	}

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);
	if (sc->sc_sunfb.sf_depth == 24) {
		sc->sc_sunfb.sf_depth = 32;
		sc->sc_sunfb.sf_linebytes =
		    (sc->sc_sunfb.sf_depth / 8) * sc->sc_sunfb.sf_width;
		sc->sc_sunfb.sf_fbsize =
		    sc->sc_sunfb.sf_height * sc->sc_sunfb.sf_linebytes;
	}

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

#ifdef RAMDISK_HOOKS
	printf("%s: aperture needed\n", self->dv_xname);
#endif

	ri = &sc->sc_sunfb.sf_ro;
	ri->ri_bits = bus_space_vaddr(sc->sc_memt, sc->sc_memh);
	ri->ri_hw = sc;

	fbwscons_init(&sc->sc_sunfb, RI_BSWAP, console);
	fbwscons_setcolormap(&sc->sc_sunfb, machfb_setcolor);
	sc->sc_mode = WSDISPLAYIO_MODE_EMUL;

	machfb_init(sc);
	ri->ri_ops.copyrows = machfb_copyrows;
	ri->ri_ops.copycols = machfb_copycols;
	ri->ri_ops.eraserows = machfb_eraserows;
	ri->ri_ops.erasecols = machfb_erasecols;

	if (console)
		fbwscons_console_init(&sc->sc_sunfb, -1);
	fbwscons_attach(&sc->sc_sunfb, &machfb_accessops, console);
}

int
machfb_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct machfb_softc *sc = v;
	struct wsdisplay_fbinfo *wdf;
	struct pcisel *sel;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_MACHFB;
		break;
	case WSDISPLAYIO_SMODE:
		sc->sc_mode = *(u_int *)data;
		if (sc->sc_mode == WSDISPLAYIO_MODE_EMUL) {
			struct rasops_info *ri = &sc->sc_sunfb.sf_ro;

			/* Restore colormap. */
			fbwscons_setcolormap(&sc->sc_sunfb, machfb_setcolor);

			/* Clear screen. */
			machfb_fillrect(sc, 0, 0, ri->ri_width, ri->ri_height,
			    ri->ri_devcmap[WSCOL_WHITE]);
		}
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
		return machfb_getcmap(sc, (struct wsdisplay_cmap *)data);
	case WSDISPLAYIO_PUTCMAP:
		return machfb_putcmap(sc, (struct wsdisplay_cmap *)data);

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
machfb_mmap(void *v, off_t off, int prot)
{
	struct machfb_softc *sc = v;

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
machfb_is_console(int node)
{
	extern int fbnode;

	return (fbnode == node);
}

int
machfb_getcmap(struct machfb_softc *sc, struct wsdisplay_cmap *cm)
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
machfb_putcmap(struct machfb_softc *sc, struct wsdisplay_cmap *cm)
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

	bus_space_write_1(sc->sc_regt, sc->sc_regh, M64_DAC_MASK, 0xff);
	bus_space_write_1(sc->sc_regt, sc->sc_regh, M64_DAC_WINDEX, index);
	for (i = 0; i < count; i++) {
		bus_space_write_1(sc->sc_regt, sc->sc_regh,
		    M64_DAC_DATA, *r);
		bus_space_write_1(sc->sc_regt, sc->sc_regh,
		    M64_DAC_DATA, *g);
		bus_space_write_1(sc->sc_regt, sc->sc_regh,
		    M64_DAC_DATA, *b);
		r++, g++, b++;
	}
	return (0);
}

void
machfb_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct machfb_softc *sc = v;

	sc->sc_cmap_red[index] = r;
	sc->sc_cmap_green[index] = g;
	sc->sc_cmap_blue[index] = b;

	bus_space_write_1(sc->sc_regt, sc->sc_regh, M64_DAC_MASK, 0xff);
	bus_space_write_1(sc->sc_regt, sc->sc_regh, M64_DAC_WINDEX, index);
	bus_space_write_1(sc->sc_regt, sc->sc_regh, M64_DAC_DATA, r);
	bus_space_write_1(sc->sc_regt, sc->sc_regh, M64_DAC_DATA, g);
	bus_space_write_1(sc->sc_regt, sc->sc_regh, M64_DAC_DATA, b);
}

/*
 * Accelerated routines.
 */

int
machfb_copycols(void *cookie, int row, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct machfb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	dst *= ri->ri_font->fontwidth;
	row *= ri->ri_font->fontheight;

	machfb_copyrect(sc, ri->ri_xorigin + src, ri->ri_yorigin + row,
	    ri->ri_xorigin + dst, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight);

	return 0;
}

int
machfb_erasecols(void *cookie, int row, int col, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct machfb_softc *sc = ri->ri_hw;
	int bg, fg;

	ri->ri_ops.unpack_attr(cookie, attr, &fg, &bg, NULL);

	row *= ri->ri_font->fontheight;
	col *= ri->ri_font->fontwidth;
	num *= ri->ri_font->fontwidth;

	machfb_fillrect(sc, ri->ri_xorigin + col, ri->ri_yorigin + row,
	    num, ri->ri_font->fontheight, ri->ri_devcmap[bg]);

	return 0;
}

int
machfb_copyrows(void *cookie, int src, int dst, int num)
{
	struct rasops_info *ri = cookie;
	struct machfb_softc *sc = ri->ri_hw;

	num *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	dst *= ri->ri_font->fontheight;

	machfb_copyrect(sc, ri->ri_xorigin, ri->ri_yorigin + src,
	    ri->ri_xorigin, ri->ri_yorigin + dst, ri->ri_emuwidth, num);

	return 0;
}

int
machfb_eraserows(void *cookie, int row, int num, uint32_t attr)
{
	struct rasops_info *ri = cookie;
	struct machfb_softc *sc = ri->ri_hw;
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
	machfb_fillrect(sc, x, y, w, num, ri->ri_devcmap[bg]);

	return 0;
}

void
machfb_init(struct machfb_softc *sc)
{
	uint32_t reg;

        /* Reset engine. */
	reg = bus_space_read_4(sc->sc_regt, sc->sc_regh, M64_GEN_TEST_CNTL);
	reg &= ~M64_GEN_GUI_EN;
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_GEN_TEST_CNTL, reg);

        /* Enable engine. */
	reg = bus_space_read_4(sc->sc_regt, sc->sc_regh, M64_GEN_TEST_CNTL);
	reg &= M64_GEN_GUI_EN;
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_GEN_TEST_CNTL, reg);

        /* Clearing any FIFO or host errors. */
	reg = bus_space_read_4(sc->sc_regt, sc->sc_regh, M64_BUS_CNTL);
	reg |= M64_BUS_HOST_ERR_ACK | M64_BUS_FIFO_ERR_ACK;
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_BUS_CNTL, reg);

	machfb_wait_fifo(sc, 14);
	
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_CONTEXT_MASK, 0xffffffff);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_OFF_PITCH,
	    (sc->sc_sunfb.sf_linebytes / 8) << 22);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_Y_X, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_HEIGHT, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_BRES_ERR, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_BRES_INC, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_BRES_DEC, 0);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_CNTL,
	    M64_DST_LAST_PEL | M64_DST_X_LEFT_TO_RIGHT |
	    M64_DST_Y_TOP_TO_BOTTOM);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SRC_OFF_PITCH,
	    (sc->sc_sunfb.sf_linebytes / 8) << 22);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SRC_Y_X, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_SRC_HEIGHT1_WIDTH1, 1);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SRC_Y_X_START, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_SRC_HEIGHT2_WIDTH2, 1);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SRC_CNTL,
	    M64_SRC_LINE_X_LEFT_TO_RIGHT);

	machfb_wait_fifo(sc, 13);

	/* Host attributes. */
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_HOST_CNTL, 0);

	/* Pattern attributes. */
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_PAT_REG0, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_PAT_REG1, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_PAT_CNTL, 0);

	/* Scissors. */
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SC_LEFT, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SC_TOP, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SC_BOTTOM,
	    sc->sc_sunfb.sf_height - 1);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SC_RIGHT,
	    sc->sc_sunfb.sf_linebytes - 1);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DP_BKGD_CLR, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_FRGD_CLR, 0xffffffff);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_WRITE_MASK, 0xffffffff);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_MIX, (M64_MIX_SRC << 16) | M64_MIX_DST);

	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_SRC, M64_FRGD_SRC_FRGD_CLR);

	machfb_wait_fifo(sc, 3);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_CLR_CMP_CLR, 0);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_CLR_CMP_MASK, 0xffffffff);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_CLR_CMP_CNTL, 0);

	machfb_wait_fifo(sc, 3);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DP_PIX_WIDTH,
	    M64_HOST_8BPP | M64_SRC_8BPP | M64_DST_8BPP);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DP_CHAIN_MASK,
	    M64_DP_CHAIN_8BPP);

	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_GUI_TRAJ_CNTL,
	    M64_DST_X_LEFT_TO_RIGHT | M64_DST_Y_TOP_TO_BOTTOM);

	machfb_wait(sc);
}

int
machfb_wait_fifo(struct machfb_softc *sc, int v)
{
	int i;

	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(sc->sc_regt, sc->sc_regh,
		    M64_FIFO_STAT) & M64_FIFO_STAT_MASK) <= (0x8000 >> v))
			break;
		DELAY(1);
	}

	return i;
}

int
machfb_wait(struct machfb_softc *sc)
{
	int i;

	machfb_wait_fifo(sc, 16);
	for (i = 1000000; i != 0; i--) {
		if ((bus_space_read_4(sc->sc_regt, sc->sc_regh,
		    M64_GUI_STAT) & M64_GUI_ACTIVE) == 0)
			break;
		DELAY(1);
	}

	return i;
}

void
machfb_copyrect(struct machfb_softc *sc, int sx, int sy, int dx, int dy,
    int w, int h)
{
	uint32_t dest_ctl = 0;

	machfb_wait_fifo(sc, 10);

	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_WRITE_MASK, 0xff);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_SRC, M64_FRGD_SRC_BLIT);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_MIX, M64_MIX_SRC << 16);
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_CLR_CMP_CNTL, 0);
	if (dy < sy) {
		dest_ctl = M64_DST_Y_TOP_TO_BOTTOM;
	} else {
		sy += h - 1;
		dy += h - 1;
		dest_ctl = M64_DST_Y_BOTTOM_TO_TOP;
	}
	if (dx < sx) {
		dest_ctl |= M64_DST_X_LEFT_TO_RIGHT;
		bus_space_write_4(sc->sc_regt, sc->sc_regh,
		    M64_SRC_CNTL, M64_SRC_LINE_X_LEFT_TO_RIGHT);
	} else {
		dest_ctl |= M64_DST_X_RIGHT_TO_LEFT;
		sx += w - 1;
		dx += w - 1;
		bus_space_write_4(sc->sc_regt, sc->sc_regh,
		    M64_SRC_CNTL, M64_SRC_LINE_X_RIGHT_TO_LEFT);
	}
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DST_CNTL, dest_ctl);

	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_SRC_Y_X, M64_COORDS(sx, sy));
	bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SRC_WIDTH1, w);
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DST_Y_X, M64_COORDS(dx, dy));
	bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DST_HEIGHT_WIDTH, M64_COORDS(w, h));

	machfb_wait(sc);
}

void
machfb_fillrect(struct machfb_softc *sc, int x, int y, int w, int h, int color)
{
	machfb_wait_fifo(sc, 11);

        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_WRITE_MASK, 0xff);
        bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_DP_FRGD_CLR, color);
        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_SRC, M64_FRGD_SRC_FRGD_CLR);
        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DP_MIX, M64_MIX_SRC << 16);
        bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_CLR_CMP_CNTL, 0);
        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_SRC_CNTL, M64_SRC_LINE_X_LEFT_TO_RIGHT);
        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DST_CNTL, M64_DST_X_LEFT_TO_RIGHT | M64_DST_Y_TOP_TO_BOTTOM);

        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_SRC_Y_X, M64_COORDS(x, y));
        bus_space_write_4(sc->sc_regt, sc->sc_regh, M64_SRC_WIDTH1, w);
        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DST_Y_X, M64_COORDS(x, y));
        bus_space_write_4(sc->sc_regt, sc->sc_regh,
	    M64_DST_HEIGHT_WIDTH, M64_COORDS(w, h));

        machfb_wait(sc);
}
