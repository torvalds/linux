/*	$OpenBSD: mgx.c,v 1.16 2022/07/15 17:57:27 kettenis Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Driver for the Southland Media Systems (now Quantum 3D) MGX and MGXPlus
 * frame buffers.
 *
 * This board is built of an Alliance Promotion AT24 chip, and a simple
 * SBus-PCI glue logic. It also sports an EEPROM to store configuration
 * parameters, which can be controlled from SunOS or Solaris with the
 * mgxconfig utility.
 *
 * We currently don't reprogram the video mode at all, so only the resolution
 * and depth set by the PROM (or mgxconfig) will be used.
 *
 * Also, interrupts are not handled.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/tty.h>
#include <sys/conf.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/pmap.h>
#include <machine/cpu.h>
#include <machine/conf.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>

#include <dev/ic/vgareg.h>
#include <dev/ic/atxxreg.h>

#include <dev/sbus/sbusvar.h>

/*
 * MGX PROM register layout
 *
 * The cards FCode registers 9 regions:
 *
 * region  offset     size    description
 *      0 00000000  00010000  FCode (32KB only)
 *      1 00100000  00010000  FCode, repeated
 *      2 00200000  00001000  unknown, repeats every 0x100
 *                            with little differences, could be the EEPROM image
 *      3 00400000  00001000  PCI configuration space
 *      4 00500000  00001000  CRTC
 *      5 00600000  00001000  AT24 registers (offset 0xb0000)
 *      6 00700000  00010000  unknown
 *      7 00800000  00800000  unknown
 *      8 01000000  00400000  video memory
 */

#define	MGX_NREG			9
#define	MGX_REG_CRTC			4	/* video control and ramdac */
#define	MGX_REG_ATREG			5	/* AT24 registers */
#define	MGX_REG_ATREG_OFFSET	0x000b0000
#define	MGX_REG_ATREG_SIZE	0x00000400
#define	MGX_REG_VRAM8			8	/* 8-bit memory space */

/*
 * MGX CRTC access
 *
 * The CRTC only answers to the following ``port'' locations:
 * - a subset of the VGA registers:
 *   3c0, 3c1 (ATC)
 *   3c4, 3c5 (TS sequencer)
 *   3c6-3c9 (DAC)
 *   3c2, 3cc (Misc)
 *   3ce, 3cf (GDC)
 *
 * - the CRTC (6845-style) registers:
 *   3d4 index register
 *   3d5 data register
 */

#define	VGA_BASE		0x03c0
#define	TS_INDEX		(VGA_BASE + VGA_TS_INDEX)
#define	TS_DATA			(VGA_BASE + VGA_TS_DATA)
#define	CD_DISABLEVIDEO	0x0020
#define	CMAP_WRITE_INDEX	(VGA_BASE + 0x08)
#define	CMAP_DATA		(VGA_BASE + 0x09)

/* per-display variables */
struct mgx_softc {
	struct	sunfb	sc_sunfb;		/* common base device */
	bus_space_tag_t	sc_bustag;
	bus_addr_t	sc_paddr;		/* for mmap() */
	u_int8_t	sc_cmap[256 * 3];	/* shadow colormap */
	vaddr_t		sc_vidc;		/* ramdac registers */
	vaddr_t		sc_xreg;		/* AT24 registers */
	uint32_t	sc_dec;			/* dec register template */
	int		sc_nscreens;
};

void	mgx_burner(void *, u_int ,u_int);
int	mgx_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	mgx_mmap(void *, off_t, int);

struct wsdisplay_accessops mgx_accessops = {
	.ioctl = mgx_ioctl,
	.mmap = mgx_mmap,
	.burn_screen = mgx_burner
};

int	mgx_getcmap(u_int8_t *, struct wsdisplay_cmap *);
void	mgx_loadcmap(struct mgx_softc *, int, int);
int	mgx_putcmap(u_int8_t *, struct wsdisplay_cmap *);
void	mgx_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

int	mgx_ras_copycols(void *, int, int, int, int);
int	mgx_ras_copyrows(void *, int, int, int);
int	mgx_ras_do_cursor(struct rasops_info *);
int	mgx_ras_erasecols(void *, int, int, int, uint32_t);
int	mgx_ras_eraserows(void *, int, int, uint32_t);
void	mgx_ras_init(struct mgx_softc *, uint);

uint8_t	mgx_read_1(vaddr_t, uint);
uint16_t mgx_read_2(vaddr_t, uint);
void	mgx_write_1(vaddr_t, uint, uint8_t);
void	mgx_write_4(vaddr_t, uint, uint32_t);

int	mgx_wait_engine(struct mgx_softc *);
int	mgx_wait_fifo(struct mgx_softc *, uint);

/*
 * Attachment Glue
 */

int mgxmatch(struct device *, void *, void *);
void mgxattach(struct device *, struct device *, void *);

const struct cfattach mgx_ca = {
	sizeof(struct mgx_softc), mgxmatch, mgxattach
};

struct cfdriver mgx_cd = {
	NULL, "mgx", DV_DULL
};

/*
 * Match an MGX or MGX+ card.
 */
int
mgxmatch(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp(sa->sa_name, "SMSI,mgx") != 0 &&
	    strcmp(sa->sa_name, "mgx") != 0)
		return (0);

	return (1);
}

/*
 * Attach an MGX frame buffer.
 * This will keep the frame buffer in the actual PROM mode, and attach
 * a wsdisplay child device to itself.
 */
void
mgxattach(struct device *parent, struct device *self, void *args)
{
	struct mgx_softc *sc = (struct mgx_softc *)self;
	struct sbus_attach_args *sa = args;
	bus_space_tag_t bt;
	bus_space_handle_t bh;
	int node, fbsize;
	int isconsole;
	uint16_t chipid;

	bt = sa->sa_bustag;
	node = sa->sa_node;

	printf(": %s", getpropstring(node, "model"));

	isconsole = node == fbnode;

	/* Check registers */
	if (sa->sa_nreg < MGX_NREG) {
		printf("\n%s: expected %d registers, got %d\n",
		    self->dv_xname, MGX_NREG, sa->sa_nreg);
		return;
	}

	sc->sc_bustag = bt;
	if (sbus_bus_map(bt, sa->sa_reg[MGX_REG_CRTC].sbr_slot,
	    sa->sa_reg[MGX_REG_CRTC].sbr_offset, PAGE_SIZE,
	    BUS_SPACE_MAP_LINEAR, 0, &bh) != 0) {
		printf("\n%s: couldn't map crtc registers\n", self->dv_xname);
		return;
	}
	sc->sc_vidc = (vaddr_t)bus_space_vaddr(bt, bh);

	sc->sc_bustag = bt;
	if (sbus_bus_map(bt, sa->sa_reg[MGX_REG_ATREG].sbr_slot,
	    sa->sa_reg[MGX_REG_ATREG].sbr_offset + MGX_REG_ATREG_OFFSET,
	    MGX_REG_ATREG_SIZE, BUS_SPACE_MAP_LINEAR, 0, &bh) != 0) {
		printf("\n%s: couldn't map crtc registers\n", self->dv_xname);
		/* XXX unmap vidc */
		return;
	}
	sc->sc_xreg = (vaddr_t)bus_space_vaddr(bt, bh);

	/*
	 * Check the chip ID. If it's not an AT24, prefer not to access
	 * the extended registers at all.
	 */
	chipid = mgx_read_2(sc->sc_xreg, ATR_ID);
	if (chipid != ID_AT24) {
		sc->sc_xreg = (vaddr_t)0;
	}

	/* enable video */
	mgx_burner(sc, 1, 0);

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);

	/* Sanity check frame buffer memory */
	fbsize = getpropint(node, "fb_size", 0);
	if (fbsize != 0 && sc->sc_sunfb.sf_fbsize > fbsize) {
		printf("\n%s: expected at least %d bytes of vram, but card "
		    "only provides %d\n",
		    self->dv_xname, sc->sc_sunfb.sf_fbsize, fbsize);
		return;
	}

	/* Map the frame buffer memory area we're interested in */
	sc->sc_paddr = sbus_bus_addr(bt, sa->sa_reg[MGX_REG_VRAM8].sbr_slot,
	    sa->sa_reg[MGX_REG_VRAM8].sbr_offset);
	if (sbus_bus_map(bt, sa->sa_reg[MGX_REG_VRAM8].sbr_slot,
	    sa->sa_reg[MGX_REG_VRAM8].sbr_offset,
	    round_page(sc->sc_sunfb.sf_fbsize),
	    BUS_SPACE_MAP_LINEAR, 0, &bh) != 0) {
		printf("\n%s: couldn't map video memory\n", self->dv_xname);
		/* XXX unmap vidc and xreg */
		return;
	}
	sc->sc_sunfb.sf_ro.ri_bits = bus_space_vaddr(bt, bh);
	sc->sc_sunfb.sf_ro.ri_hw = sc;

	printf(", %dx%d\n",
	    sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	fbwscons_init(&sc->sc_sunfb, 0, isconsole);

	bzero(sc->sc_cmap, sizeof(sc->sc_cmap));
	fbwscons_setcolormap(&sc->sc_sunfb, mgx_setcolor);

	if (chipid != ID_AT24) {
		printf("%s: unexpected engine id %04x\n",
		    self->dv_xname, chipid);
	}

	mgx_ras_init(sc, chipid);

	if (isconsole)
		fbwscons_console_init(&sc->sc_sunfb, -1);

	fbwscons_attach(&sc->sc_sunfb, &mgx_accessops, isconsole);
}

/*
 * Register Access
 *
 * On big-endian systems such as the sparc, it is necessary to flip
 * the low-order bits of the addresses to reach the right register.
 */

uint8_t
mgx_read_1(vaddr_t regs, uint offs)
{
#if _BYTE_ORDER == _LITTLE_ENDIAN
	return *(volatile uint8_t *)(regs + offs);
#else
	return *(volatile uint8_t *)(regs + (offs ^ 3));
#endif
}

uint16_t
mgx_read_2(vaddr_t regs, uint offs)
{
#if _BYTE_ORDER == _LITTLE_ENDIAN
	return *(volatile uint16_t *)(regs + offs);
#else
	return *(volatile uint16_t *)(regs + (offs ^ 2));
#endif
}

void
mgx_write_1(vaddr_t regs, uint offs, uint8_t val)
{
#if _BYTE_ORDER == _LITTLE_ENDIAN
	*(volatile uint8_t *)(regs + offs) = val;
#else
	*(volatile uint8_t *)(regs + (offs ^ 3)) = val;
#endif
}

void
mgx_write_4(vaddr_t regs, uint offs, uint32_t val)
{
	*(volatile uint32_t *)(regs + offs) = val;
}

/*
 * Wsdisplay Operations
 */

int
mgx_ioctl(void *dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct mgx_softc *sc = dev;
	struct wsdisplay_cmap *cm;
	struct wsdisplay_fbinfo *wdf;
	int error;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_MGX;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width = sc->sc_sunfb.sf_width;
		wdf->depth = sc->sc_sunfb.sf_depth;
		wdf->stride = sc->sc_sunfb.sf_linebytes;
		wdf->offset = 0;
		wdf->cmsize = 256;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes;
		break;

	case WSDISPLAYIO_GETCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = mgx_getcmap(sc->sc_cmap, cm);
		if (error != 0)
			return (error);
		break;
	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = mgx_putcmap(sc->sc_cmap, cm);
		if (error != 0)
			return (error);
		mgx_loadcmap(sc, cm->index, cm->count);
		break;

	case WSDISPLAYIO_SVIDEO:
	case WSDISPLAYIO_GVIDEO:
		break;

	default:
		return (-1);
	}

	return (0);
}

paddr_t
mgx_mmap(void *v, off_t offset, int prot)
{
	struct mgx_softc *sc = v;

	if (offset & PGOFSET)
		return (-1);

	/* Allow mapping as a dumb framebuffer from offset 0 */
	if (offset >= 0 && offset < sc->sc_sunfb.sf_fbsize) {
		return (bus_space_mmap(sc->sc_bustag, sc->sc_paddr,
		    offset, prot, BUS_SPACE_MAP_LINEAR));
	}

	return (-1);
}

void
mgx_burner(void *v, u_int on, u_int flags)
{
	struct mgx_softc *sc = v;
	uint mode;

#ifdef notyet
	if (sc->sc_xreg != 0) {
		mode = mgx_read_1(sc->sc_xreg, ATR_DPMS);
		if (on)
			CLR(mode, DPMS_HSYNC_DISABLE | DPMS_VSYNC_DISABLE);
		else {
			SET(mode, DPMS_HSYNC_DISABLE);
#if 0	/* needs ramdac reprogramming on resume */
			if (flags & WSDISPLAY_BURN_VBLANK)
				SET(mode, DPMS_VSYNC_DISABLE);
#endif
		}
		mgx_write_1(sc->sc_xreg, ATR_DPMS, mode);
		return;
	}
#endif

	mgx_write_1(sc->sc_vidc, TS_INDEX, 1);	/* TS mode register */
	mode = mgx_read_1(sc->sc_vidc, TS_DATA);
	if (on)
		mode &= ~CD_DISABLEVIDEO;
	else
		mode |= CD_DISABLEVIDEO;
	mgx_write_1(sc->sc_vidc, TS_DATA, mode);
}

/*
 * Colormap Handling Routines
 */

void
mgx_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct mgx_softc *sc = v;
	u_int i = index * 3;

	sc->sc_cmap[i++] = r;
	sc->sc_cmap[i++] = g;
	sc->sc_cmap[i] = b;

	mgx_loadcmap(sc, index, 1);
}

void
mgx_loadcmap(struct mgx_softc *sc, int start, int ncolors)
{
	u_int8_t *color;
	int i;

	mgx_write_1(sc->sc_vidc, CMAP_WRITE_INDEX, start);
	color = sc->sc_cmap + start * 3;
	for (i = ncolors * 3; i != 0; i--)
		mgx_write_1(sc->sc_vidc, CMAP_DATA, *color++);
}

int
mgx_getcmap(u_int8_t *cm, struct wsdisplay_cmap *rcm)
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	index *= 3;
	for (i = 0; i < count; i++) {
		if ((error =
		    copyout(cm + index++, &rcm->red[i], 1)) != 0)
			return (error);
		if ((error =
		    copyout(cm + index++, &rcm->green[i], 1)) != 0)
			return (error);
		if ((error =
		    copyout(cm + index++, &rcm->blue[i], 1)) != 0)
			return (error);
	}

	return (0);
}

int
mgx_putcmap(u_int8_t *cm, struct wsdisplay_cmap *rcm)
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	index *= 3;
	for (i = 0; i < count; i++) {
		if ((error =
		    copyin(&rcm->red[i], cm + index++, 1)) != 0)
			return (error);
		if ((error =
		    copyin(&rcm->green[i], cm + index++, 1)) != 0)
			return (error);
		if ((error =
		    copyin(&rcm->blue[i], cm + index++, 1)) != 0)
			return (error);
	}

	return (0);
}

/*
 * Accelerated Text Console Code
 *
 * The X driver makes sure there are at least as many FIFOs available as
 * registers to write. They can thus be considered as write slots.
 *
 * The code below expects to run on at least an AT24 chip, and does not
 * care for the AP6422 which has fewer FIFOs; some operations would need
 * to be done in two steps to support this chip.
 */

int
mgx_wait_engine(struct mgx_softc *sc)
{
	uint i;
	uint stat;

	for (i = 10000; i != 0; i--) {
		stat = mgx_read_1(sc->sc_xreg, ATR_BLT_STATUS);
		if (!ISSET(stat, BLT_HOST_BUSY | BLT_ENGINE_BUSY))
			break;
	}

	return i;
}

int
mgx_wait_fifo(struct mgx_softc *sc, uint nfifo)
{
	uint i;
	uint stat;

	for (i = 10000; i != 0; i--) {
		stat = (mgx_read_1(sc->sc_xreg, ATR_FIFO_STATUS) & FIFO_MASK) >>
		    FIFO_SHIFT;
		if (stat >= nfifo)
			break;
		mgx_write_1(sc->sc_xreg, ATR_FIFO_STATUS, 0);
	}

	return i;
}

void
mgx_ras_init(struct mgx_softc *sc, uint chipid)
{
	/*
	 * Check the chip ID. If it's not a 6424, do not plug the
	 * accelerated routines.
	 */

	if (chipid != ID_AT24)
		return;

	/*
	 * Wait until the chip is completely idle.
	 */

	if (mgx_wait_engine(sc) == 0)
		return;
	if (mgx_wait_fifo(sc, FIFO_AT24) == 0)
		return;

	/*
	 * Compute the invariant bits of the DEC register.
	 */

	switch (sc->sc_sunfb.sf_depth) {
	case 8:
		sc->sc_dec = DEC_DEPTH_8 << DEC_DEPTH_SHIFT;
		break;
	case 15:
	case 16:
		sc->sc_dec = DEC_DEPTH_16 << DEC_DEPTH_SHIFT;
		break;
	case 32:
		sc->sc_dec = DEC_DEPTH_32 << DEC_DEPTH_SHIFT;
		break;
	default:
		return;	/* not supported */
	}

	switch (sc->sc_sunfb.sf_width) {
	case 640:
		sc->sc_dec |= DEC_WIDTH_640 << DEC_WIDTH_SHIFT;
		break;
	case 800:
		sc->sc_dec |= DEC_WIDTH_800 << DEC_WIDTH_SHIFT;
		break;
	case 1024:
		sc->sc_dec |= DEC_WIDTH_1024 << DEC_WIDTH_SHIFT;
		break;
	case 1152:
		sc->sc_dec |= DEC_WIDTH_1152 << DEC_WIDTH_SHIFT;
		break;
	case 1280:
		sc->sc_dec |= DEC_WIDTH_1280 << DEC_WIDTH_SHIFT;
		break;
	case 1600:
		sc->sc_dec |= DEC_WIDTH_1600 << DEC_WIDTH_SHIFT;
		break;
	default:
		return;	/* not supported */
	}

	sc->sc_sunfb.sf_ro.ri_ops.copycols = mgx_ras_copycols;
	sc->sc_sunfb.sf_ro.ri_ops.copyrows = mgx_ras_copyrows;
	sc->sc_sunfb.sf_ro.ri_ops.erasecols = mgx_ras_erasecols;
	sc->sc_sunfb.sf_ro.ri_ops.eraserows = mgx_ras_eraserows;
	sc->sc_sunfb.sf_ro.ri_do_cursor = mgx_ras_do_cursor;

#ifdef notneeded
	mgx_write_1(sc->sc_xreg, ATR_CLIP_CONTROL, 1);
	mgx_write_4(sc->sc_xreg, ATR_CLIP_LEFTTOP, ATR_DUAL(0, 0));
	mgx_write_4(sc->sc_xreg, ATR_CLIP_RIGHTBOTTOM,
	    ATR_DUAL(sc->sc_sunfb.sf_width - 1, sc->sc_sunfb.sf_depth - 1));
#else
	mgx_write_1(sc->sc_xreg, ATR_CLIP_CONTROL, 0);
#endif
	mgx_write_1(sc->sc_xreg, ATR_BYTEMASK, 0xff);
}

int
mgx_ras_copycols(void *v, int row, int src, int dst, int n)
{
	struct rasops_info *ri = v;
	struct mgx_softc *sc = ri->ri_hw;
	uint dec = sc->sc_dec;

	n *= ri->ri_font->fontwidth;
	src *= ri->ri_font->fontwidth;
	src += ri->ri_xorigin;
	dst *= ri->ri_font->fontwidth;
	dst += ri->ri_xorigin;
	row *= ri->ri_font->fontheight;
	row += ri->ri_yorigin;

	dec |= (DEC_COMMAND_BLT << DEC_COMMAND_SHIFT) |
	    (DEC_START_DIMX << DEC_START_SHIFT);
	if (src < dst) {
		src += n - 1;
		dst += n - 1;
		dec |= DEC_DIR_X_REVERSE;
	}
	mgx_wait_fifo(sc, 5);
	mgx_write_1(sc->sc_xreg, ATR_ROP, ROP_SRC);
	mgx_write_4(sc->sc_xreg, ATR_DEC, dec);
	mgx_write_4(sc->sc_xreg, ATR_SRC_XY, ATR_DUAL(row, src));
	mgx_write_4(sc->sc_xreg, ATR_DST_XY, ATR_DUAL(row, dst));
	mgx_write_4(sc->sc_xreg, ATR_WH, ATR_DUAL(ri->ri_font->fontheight, n));
	mgx_wait_engine(sc);

	return 0;
}

int
mgx_ras_copyrows(void *v, int src, int dst, int n)
{
	struct rasops_info *ri = v;
	struct mgx_softc *sc = ri->ri_hw;
	uint dec = sc->sc_dec;

	n *= ri->ri_font->fontheight;
	src *= ri->ri_font->fontheight;
	src += ri->ri_yorigin;
	dst *= ri->ri_font->fontheight;
	dst += ri->ri_yorigin;

	dec |= (DEC_COMMAND_BLT << DEC_COMMAND_SHIFT) |
	    (DEC_START_DIMX << DEC_START_SHIFT);
	if (src < dst) {
		src += n - 1;
		dst += n - 1;
		dec |= DEC_DIR_Y_REVERSE;
	}
	mgx_wait_fifo(sc, 5);
	mgx_write_1(sc->sc_xreg, ATR_ROP, ROP_SRC);
	mgx_write_4(sc->sc_xreg, ATR_DEC, dec);
	mgx_write_4(sc->sc_xreg, ATR_SRC_XY, ATR_DUAL(src, ri->ri_xorigin));
	mgx_write_4(sc->sc_xreg, ATR_DST_XY, ATR_DUAL(dst, ri->ri_xorigin));
	mgx_write_4(sc->sc_xreg, ATR_WH, ATR_DUAL(n, ri->ri_emuwidth));
	mgx_wait_engine(sc);

	return 0;
}

int
mgx_ras_erasecols(void *v, int row, int col, int n, uint32_t attr)
{
	struct rasops_info *ri = v;
	struct mgx_softc *sc = ri->ri_hw;
	int fg, bg;
	uint dec = sc->sc_dec;

	ri->ri_ops.unpack_attr(v, attr, &fg, &bg, NULL);
	bg = ri->ri_devcmap[bg];

	n *= ri->ri_font->fontwidth;
	col *= ri->ri_font->fontwidth;
	col += ri->ri_xorigin;
	row *= ri->ri_font->fontheight;
	row += ri->ri_yorigin;

	dec |= (DEC_COMMAND_RECT << DEC_COMMAND_SHIFT) |
	    (DEC_START_DIMX << DEC_START_SHIFT);
	mgx_wait_fifo(sc, 5);
	mgx_write_1(sc->sc_xreg, ATR_ROP, ROP_SRC);
	mgx_write_4(sc->sc_xreg, ATR_FG, bg);
	mgx_write_4(sc->sc_xreg, ATR_DEC, dec);
	mgx_write_4(sc->sc_xreg, ATR_DST_XY, ATR_DUAL(row, col));
	mgx_write_4(sc->sc_xreg, ATR_WH, ATR_DUAL(ri->ri_font->fontheight, n));
	mgx_wait_engine(sc);

	return 0;
}

int
mgx_ras_eraserows(void *v, int row, int n, uint32_t attr)
{
	struct rasops_info *ri = v;
	struct mgx_softc *sc = ri->ri_hw;
	int fg, bg;
	uint dec = sc->sc_dec;

	ri->ri_ops.unpack_attr(v, attr, &fg, &bg, NULL);
	bg = ri->ri_devcmap[bg];

	dec |= (DEC_COMMAND_RECT << DEC_COMMAND_SHIFT) |
	    (DEC_START_DIMX << DEC_START_SHIFT);
	mgx_wait_fifo(sc, 5);
	mgx_write_1(sc->sc_xreg, ATR_ROP, ROP_SRC);
	mgx_write_4(sc->sc_xreg, ATR_FG, bg);
	mgx_write_4(sc->sc_xreg, ATR_DEC, dec);
	if (n == ri->ri_rows && ISSET(ri->ri_flg, RI_FULLCLEAR)) {
		mgx_write_4(sc->sc_xreg, ATR_DST_XY, ATR_DUAL(0, 0));
		mgx_write_4(sc->sc_xreg, ATR_WH,
		    ATR_DUAL(ri->ri_height, ri->ri_width));
	} else {
		n *= ri->ri_font->fontheight;
		row *= ri->ri_font->fontheight;
		row += ri->ri_yorigin;

		mgx_write_4(sc->sc_xreg, ATR_DST_XY,
		    ATR_DUAL(row, ri->ri_xorigin));
		mgx_write_4(sc->sc_xreg, ATR_WH, ATR_DUAL(n, ri->ri_emuwidth));
	}
	mgx_wait_engine(sc);

	return 0;
}

int
mgx_ras_do_cursor(struct rasops_info *ri)
{
	struct mgx_softc *sc = ri->ri_hw;
	int row, col;
	uint dec = sc->sc_dec;

	row = ri->ri_crow * ri->ri_font->fontheight + ri->ri_yorigin;
	col = ri->ri_ccol * ri->ri_font->fontwidth + ri->ri_xorigin;

	dec |= (DEC_COMMAND_BLT << DEC_COMMAND_SHIFT) |
	    (DEC_START_DIMX << DEC_START_SHIFT);
	mgx_wait_fifo(sc, 5);
	mgx_write_1(sc->sc_xreg, ATR_ROP, (uint8_t)~ROP_SRC);
	mgx_write_4(sc->sc_xreg, ATR_DEC, dec);
	mgx_write_4(sc->sc_xreg, ATR_SRC_XY, ATR_DUAL(row, col));
	mgx_write_4(sc->sc_xreg, ATR_DST_XY, ATR_DUAL(row, col));
	mgx_write_4(sc->sc_xreg, ATR_WH,
	    ATR_DUAL(ri->ri_font->fontheight, ri->ri_font->fontwidth));
	mgx_wait_engine(sc);

	return 0;
}
