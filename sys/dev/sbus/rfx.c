/*	$OpenBSD: rfx.c,v 1.15 2022/07/15 17:57:27 kettenis Exp $	*/

/*
 * Copyright (c) 2004, Miodrag Vallat.
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
 * Driver for the Vitec RasterFlex family of frame buffers.
 * It should support RasterFlex-24, RasterFlex-32 and RasterFlex-HR.
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
#include <machine/openfirm.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>

#include <dev/sbus/sbusvar.h>

#include <dev/ic/bt463reg.h>

/*
 * Configuration structure
 */
struct rfx_config {
	u_int16_t	unknown;
	u_int16_t	version;
	u_int32_t	scanline;
	u_int32_t	maxwidth;	/* unsure */
	u_int32_t	maxheight;	/* unsure */
	u_int32_t	width;
	u_int32_t	height;
};

/*
 * In-memory offsets
 */

#define	RFX_RAMDAC_ADDR		0x00020000
#define	RFX_RAMDAC_SIZE		0x00000004

#define	RFX_CONTROL_ADDR	0x00040000
#define	RFX_CONTROL_SIZE	0x000000e0

#define	RFX_INIT_ADDR		0x00018000
#define	RFX_INIT_OFFSET		0x0000001c
#define	RFX_INIT_SIZE		0x00008000

#define	RFX_VRAM_ADDR		0x00100000

/*
 * Control registers
 */

#define	RFX_VIDCTRL_REG		0x10
#define	RFX_VSYNC_ENABLE	0x00000001
#define	RFX_VIDEO_DISABLE	0x00000002

/*
 * Shadow colormap
 */
struct rfx_cmap {
	u_int8_t	red[256];
	u_int8_t	green[256];
	u_int8_t	blue[256];
};

struct rfx_softc {
	struct	sunfb		 sc_sunfb;

	bus_space_tag_t		 sc_bustag;
	bus_addr_t		 sc_paddr;

	struct	intrhand	 sc_ih;

	struct rfx_cmap		 sc_cmap;
	volatile u_int8_t	*sc_ramdac;
	volatile u_int32_t	*sc_ctrl;

	int			 sc_nscreens;
};

void	rfx_burner(void *, u_int, u_int);
int	rfx_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	rfx_mmap(void *, off_t, int);

int	rfx_getcmap(struct rfx_cmap *, struct wsdisplay_cmap *);
int	rfx_initialize(struct rfx_softc *, struct sbus_attach_args *,
	    struct rfx_config *);
int	rfx_intr(void *);
void	rfx_loadcmap(struct rfx_softc *, int, int);
int	rfx_putcmap(struct rfx_cmap *, struct wsdisplay_cmap *);
void	rfx_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

struct wsdisplay_accessops rfx_accessops = {
	.ioctl = rfx_ioctl,
	.mmap = rfx_mmap,
	.burn_screen = rfx_burner
};

int	rfxmatch(struct device *, void *, void *);
void	rfxattach(struct device *, struct device *, void *);

const struct cfattach rfx_ca = {
	sizeof (struct rfx_softc), rfxmatch, rfxattach
};

struct cfdriver rfx_cd = {
	NULL, "rfx", DV_DULL
};

/*
 * Match a supported RasterFlex card.
 */
int
rfxmatch(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;
	const char *device = sa->sa_name;

	/* skip vendor name (could be CWARE, VITec, ...) */
	while (*device != ',' && *device != '\0')
		device++;
	if (*device == '\0')
		device = sa->sa_name;
	else
		device++;

	if (strncmp(device, "RasterFLEX", strlen("RasterFLEX")) != 0)
		return (0);

	/* RasterVideo and RasterFlex-TV are frame grabbers */
	if (strcmp(device, "RasterFLEX-TV") == 0)
		return (0);

	return (1);
}

/*
 * Attach and initialize a rfx display, as well as a child wsdisplay.
 */
void
rfxattach(struct device *parent, struct device *self, void *args)
{
	struct rfx_softc *sc = (struct rfx_softc *)self;
	struct sbus_attach_args *sa = args;
	const char *device = sa->sa_name;
	struct rfx_config cf;
	bus_space_tag_t bt;
	bus_space_handle_t bh;
	int node, cflen, isconsole = 0;

	/* skip vendor name (could be CWARE, VITec, ...) */
	while (*device != ',' && *device != '\0')
		device++;
	if (*device == '\0')
		device = sa->sa_name;
	else
		device++;

	printf(": %s", device);

	if (sa->sa_nreg == 0) {
		printf("\n%s: no SBus registers!\n", self->dv_xname);
		return;
	}

	bt = sa->sa_bustag;
	node = sa->sa_node;
	isconsole = node == fbnode;

	/*
	 * Parse configuration structure
	 */
	cflen = getproplen(node, "configuration");
	if (cflen != sizeof cf) {
		printf(", unknown %d bytes conf. structure", cflen);
		/* fill in default values */
		cf.version = 0;
		cf.scanline = 2048;
		cf.width = 1152;
		cf.height = 900;
	} else {
		OF_getprop(node, "configuration", &cf, cflen);
		printf(", revision %d", cf.version);
	}

	/*
	 * Map registers
	 */

	sc->sc_bustag = bt;
	sc->sc_paddr = sbus_bus_addr(bt, sa->sa_slot, sa->sa_offset);

	if (sbus_bus_map(bt, sa->sa_slot, sa->sa_offset + RFX_RAMDAC_ADDR,
	    RFX_RAMDAC_SIZE, BUS_SPACE_MAP_LINEAR, 0, &bh) != 0) {
		printf("\n%s: couldn't map ramdac registers\n", self->dv_xname);
		return;
	}
	sc->sc_ramdac = (u_int8_t *)bus_space_vaddr(bt, bh);

	if (sbus_bus_map(bt, sa->sa_slot, sa->sa_offset + RFX_CONTROL_ADDR,
	    RFX_CONTROL_SIZE, BUS_SPACE_MAP_LINEAR, 0, &bh) != 0) {
		printf("\n%s: couldn't map control registers\n", self->dv_xname);
		return;
	}
	sc->sc_ctrl = (u_int32_t *)bus_space_vaddr(bt, bh);

#if 0	/* not yet */
	sc->sc_ih.ih_fun = rfx_intr;
	sc->sc_ih.ih_arg = sc;
	intr_establish(ca->ca_ra.ra_intr[0].int_pri, &sc->sc_ih, IPL_FB);
#endif

	/*
	 * The following is an equivalent for
	 *   fb_setsize(&sc->sc_sunfb, 8, cf.width, cf.height,
	 *     node, ca->ca_bustype);
	 * forcing the correct scan line value. Since the usual frame buffer
	 * properties are missing on this card, no need to go through
	 * fb_setsize()...
	 */
	sc->sc_sunfb.sf_depth = 8;
	sc->sc_sunfb.sf_width = cf.width;
	sc->sc_sunfb.sf_height = cf.height;
	sc->sc_sunfb.sf_linebytes = cf.scanline;
	sc->sc_sunfb.sf_fbsize = cf.height * cf.scanline;

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	if (sbus_bus_map(bt, sa->sa_slot, sa->sa_offset + RFX_VRAM_ADDR,
	    round_page(sc->sc_sunfb.sf_fbsize), BUS_SPACE_MAP_LINEAR,
	    0, &bh) != 0) {
		printf("\n%s: couldn't map video memory\n", self->dv_xname);
		return;
	}
	sc->sc_sunfb.sf_ro.ri_bits = bus_space_vaddr(bt, bh);
	sc->sc_sunfb.sf_ro.ri_hw = sc;

	/*
	 * If we are not the console, the frame buffer has not been
	 * initialized by the PROM - do this ourselves.
	 */
	if (!isconsole) {
		if (rfx_initialize(sc, sa, &cf) != 0)
			return;
	}

	fbwscons_init(&sc->sc_sunfb, 0, isconsole);

	bzero(&sc->sc_cmap, sizeof(sc->sc_cmap));
	fbwscons_setcolormap(&sc->sc_sunfb, rfx_setcolor);

	if (isconsole)
		fbwscons_console_init(&sc->sc_sunfb, -1);

	/* enable video */
	rfx_burner(sc, 1, 0);

	fbwscons_attach(&sc->sc_sunfb, &rfx_accessops, isconsole);
}

/*
 * Common wsdisplay operations
 */

int
rfx_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct rfx_softc *sc = v;
	struct wsdisplay_cmap *cm;
	struct wsdisplay_fbinfo *wdf;
	int error;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_RFLEX;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
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
		cm = (struct wsdisplay_cmap *)data;
		error = rfx_getcmap(&sc->sc_cmap, cm);
		if (error != 0)
			return (error);
		break;
	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = rfx_putcmap(&sc->sc_cmap, cm);
		if (error != 0)
			return (error);
		rfx_loadcmap(sc, cm->index, cm->count);
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
rfx_mmap(void *v, off_t offset, int prot)
{
	struct rfx_softc *sc = v;

	if (offset & PGOFSET)
		return (-1);

	if (offset >= 0 && offset < sc->sc_sunfb.sf_fbsize) {
		return (bus_space_mmap(sc->sc_bustag, sc->sc_paddr,
		    RFX_VRAM_ADDR + offset, prot, BUS_SPACE_MAP_LINEAR));
	}

	return (-1);
}

void
rfx_burner(void *v, u_int on, u_int flags)
{
	struct rfx_softc *sc = v;

	if (on) {
		sc->sc_ctrl[RFX_VIDCTRL_REG] &= ~RFX_VIDEO_DISABLE;
		sc->sc_ctrl[RFX_VIDCTRL_REG] |= RFX_VSYNC_ENABLE;
	} else {
		sc->sc_ctrl[RFX_VIDCTRL_REG] |= RFX_VIDEO_DISABLE;
		if (flags & WSDISPLAY_BURN_VBLANK)
			sc->sc_ctrl[RFX_VIDCTRL_REG] &= ~RFX_VSYNC_ENABLE;
	}
}

/*
 * Colormap helper functions
 */

void
rfx_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct rfx_softc *sc = v;

	sc->sc_cmap.red[index] = r;
	sc->sc_cmap.green[index] = g;
	sc->sc_cmap.blue[index] = b;

	rfx_loadcmap(sc, index, 1);
}

int
rfx_getcmap(struct rfx_cmap *cm, struct wsdisplay_cmap *rcm)
{
	u_int index = rcm->index, count = rcm->count;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	if ((error = copyout(cm->red + index, rcm->red, count)) != 0)
		return (error);
	if ((error = copyout(cm->green + index, rcm->green, count)) != 0)
		return (error);
	if ((error = copyout(cm->blue + index, rcm->blue, count)) != 0)
		return (error);

	return (0);
}

int
rfx_putcmap(struct rfx_cmap *cm, struct wsdisplay_cmap *rcm)
{
	u_int index = rcm->index, count = rcm->count;
	u_int8_t red[256], green[256], blue[256];
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	if ((error = copyin(rcm->red, red, count)) != 0)
		return (error);
	if ((error = copyin(rcm->green, green, count)) != 0)
		return (error);
	if ((error = copyin(rcm->blue, blue, count)) != 0)
		return (error);

	bcopy(red, cm->red + index, count);
	bcopy(green, cm->green + index, count);
	bcopy(blue, cm->blue + index, count);

	return (0);
}

void
rfx_loadcmap(struct rfx_softc *sc, int start, int ncolors)
{
	u_int8_t *r, *g, *b;

	r = sc->sc_cmap.red + start;
	g = sc->sc_cmap.green + start;
	b = sc->sc_cmap.blue + start;

	start += BT463_IREG_CPALETTE_RAM;
	sc->sc_ramdac[BT463_REG_ADDR_LOW] = start & 0xff;
	sc->sc_ramdac[BT463_REG_ADDR_HIGH] = (start >> 8) & 0xff;

	while (ncolors-- != 0) {
		sc->sc_ramdac[BT463_REG_CMAP_DATA] = *r++;
		sc->sc_ramdac[BT463_REG_CMAP_DATA] = *g++;
		sc->sc_ramdac[BT463_REG_CMAP_DATA] = *b++;
	}
}

/*
 * Initialization code parser
 */

int
rfx_initialize(struct rfx_softc *sc, struct sbus_attach_args *sa,
    struct rfx_config *cf)
{
	u_int32_t *data, offset, value;
	size_t cnt;
	bus_space_handle_t bh;
	int error;

	/*
	 * Map the initialization data
	 */
	if ((error = sbus_bus_map(sa->sa_bustag, sa->sa_slot, sa->sa_offset +
	    RFX_INIT_ADDR, RFX_INIT_SIZE, BUS_SPACE_MAP_LINEAR, 0, &bh)) != 0) {
		printf("\n%s: couldn't map initialization data\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
		return error;
	}
	data = (u_int32_t *)bus_space_vaddr(sa->sa_bustag, bh);

	/*
	 * Skip copyright notice
	 */
	data += RFX_INIT_OFFSET / sizeof(u_int32_t);
	cnt = (RFX_INIT_SIZE - RFX_INIT_OFFSET) / sizeof(u_int32_t);
	cnt >>= 1;

	/*
	 * Parse and apply settings
	 */
	while (cnt != 0) {
		offset = *data++;
		value = *data++;

		if (offset == (u_int32_t)-1 && value == (u_int32_t)-1)
			break;

		/* Old PROM are little-endian */
		if (cf->version <= 1) {
			offset = letoh32(offset);
			value = letoh32(offset);
		}

		if (offset & (1U << 31)) {
			offset = (offset & ~(1U << 31)) - RFX_RAMDAC_ADDR;
			if (offset < RFX_RAMDAC_SIZE)
				sc->sc_ramdac[offset] = value >> 24;
		} else {
			offset -= RFX_CONTROL_ADDR;
			if (offset < RFX_CONTROL_SIZE)
				sc->sc_ctrl[offset >> 2] = value;
		}

		cnt--;
	}

#ifdef DEBUG
	if (cnt != 0)
		printf("%s: incoherent initialization data!\n",
		    sc->sc_sunfb.sf_dev.dv_xname);
#endif

	bus_space_unmap(sa->sa_bustag, bh, RFX_INIT_SIZE);

	return 0;
}
