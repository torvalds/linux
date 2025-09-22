/*	$OpenBSD: tvtwo.c,v 1.18 2022/07/15 17:57:27 kettenis Exp $	*/

/*
 * Copyright (c) 2003, 2006, 2008, Miodrag Vallat.
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
 * Driver for the Parallax XVideo and PowerVideo graphics boards.
 *
 * Some details about these board used to be available at:
 *   http://www.jlw.com/~woolsey/parallax/support/developers/xvideotech.html
 */

/*
 * The Parallax XVideo series frame buffers are 8/24-bit accelerated
 * frame buffers, with hardware MPEG capabilities using a CCube chipset.
 */

/*
 * Currently, this driver can only handle the 8-bit and 24-bit planes of the
 * frame buffer, in an unaccelerated mode.
 *
 * TODO:
 * - nvram handling
 * - use the accelerator
 * - interface to the c^3
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/conf.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/pmap.h>
#include <machine/cpu.h>
#include <machine/conf.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>
#include <dev/rasops/rasops.h>
#include <machine/fbvar.h>

#include <dev/sbus/sbusvar.h>

/*
 * The memory layout of the board is as follows:
 *
 *	PROM0		000000 - 00ffff
 *	overlay plane	010000 - 037fff
 *	registers	040000 - 0404d0
 *	CCube		050000 - 05ffff
 *	8-bit plane	080000 - 17ffff
 *	24-bit plane	200000 - 6fffff
 *	PROM1		7f0000 - 7fffff
 */

#define	PX_PROM0_OFFSET		0x000000
#define	PX_OVERLAY_OFFSET	0x010000
#define	PX_REG_OFFSET		0x040000
#define	PX_CCUBE_OFFSET		0x050000
#define	PX_PLANE8_OFFSET	0x080000
#define	PX_PLANE24_OFFSET	0x200000
#define	PX_PROM1_OFFSET		0x7f0000

#define	PX_MAP_SIZE		0x800000

/*
 * Partial registers layout
 */

#define	PX_REG_DISPKLUGE	0x00b8	/* write only */
#define	DISPKLUGE_DEFAULT	0xc41f
#define	DISPKLUGE_BLANK		(1 << 12)
#define	DISPKLUGE_SYNC		(1 << 13)

#define	PX_REG_BT463_RED	0x0480
#define	PX_REG_BT463_GREEN	0x0490
#define	PX_REG_BT463_BLUE	0x04a0
#define	PX_REG_BT463_ALL	0x04b0

#define	PX_REG_SIZE		0x04d0


/* per-display variables */
struct tvtwo_softc {
	struct	sunfb	sc_sunfb;	/* common base device */

	bus_space_tag_t	sc_bustag;
	bus_addr_t	sc_paddr;

	volatile u_int8_t *sc_m8;
	volatile u_int8_t *sc_m24;
	volatile u_int8_t *sc_regs;

	int	sc_nscreens;
};

int	tvtwo_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t	tvtwo_mmap(void *, off_t, int);
void	tvtwo_burner(void *, u_int, u_int);

struct wsdisplay_accessops tvtwo_accessops = {
	.ioctl = tvtwo_ioctl,
	.mmap = tvtwo_mmap,
	.burn_screen = tvtwo_burner
};

void	tvtwo_directcmap(struct tvtwo_softc *);
static __inline__
void	tvtwo_ramdac_wraddr(struct tvtwo_softc *, u_int32_t);
void	tvtwo_reset(struct tvtwo_softc *, u_int);
void	tvtwo_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);

int	tvtwomatch(struct device *, void *, void *);
void	tvtwoattach(struct device *, struct device *, void *);

const struct cfattach tvtwo_ca = {
	sizeof(struct tvtwo_softc), tvtwomatch, tvtwoattach
};

struct cfdriver tvtwo_cd = {
	NULL, "tvtwo", DV_DULL
};

/*
 * Default frame buffer resolution, depending upon the "freqcode"
 */
#define	NFREQCODE	5
const int defwidth[NFREQCODE] = { 1152, 1152, 1152, 1024, 640 };
const int defheight[NFREQCODE] = { 900, 900, 900, 768, 480 };

/*
 * Match an XVideo or PowerVideo card.
 */
int
tvtwomatch(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp(sa->sa_name, "PGI,tvtwo") == 0 ||
	    strcmp(sa->sa_name, "PGI,tvthree") == 0)
		return (1);

	return (0);
}

/*
 * Attach a display.
 */
void
tvtwoattach(struct device *parent, struct device *self, void *args)
{
	struct tvtwo_softc *sc = (struct tvtwo_softc *)self;
	struct sbus_attach_args *sa = args;
	bus_space_tag_t bt;
	bus_space_handle_t bh;
	int node, width, height, freqcode;
	int isconsole;
	char *freqstring, *revision;

	bt = sa->sa_bustag;
	node = sa->sa_node;

	printf(": %s", getpropstring(node, "model"));
	revision = getpropstring(node, "revision");
	if (*revision != '\0')
		printf(", revision %s", revision);

	/* Older XVideo provide two sets of SBus registers:
	 *	R0		040000 - 040800
	 *	R1		080000 - 17d200
	 * While the more recent revisions provide only one register:
	 *	R0		000000 - 7fffff
	 *
	 * We'll simply ``rewrite'' R0 on older boards and handle them as
	 * recent boards.
	 */
	if (sa->sa_nreg > 1) {
		sa->sa_offset -= PX_REG_OFFSET;
		sa->sa_size = PX_MAP_SIZE;
	}

	isconsole = node == fbnode;

	/* Map registers. */
	sc->sc_bustag = bt;
	if (sbus_bus_map(bt, sa->sa_slot, sa->sa_offset + PX_REG_OFFSET,
	    PX_REG_SIZE, BUS_SPACE_MAP_LINEAR, 0, &bh) != 0) {
		printf("%s: couldn't map registers\n", self->dv_xname);
		return;
	}
	sc->sc_regs = bus_space_vaddr(bt, bh);

	/*
	 * Compute framebuffer size.
	 * Older boards do not have the ``freqcode'' property and are
	 * restricted to 1152x900.
	 */
	freqstring = getpropstring(node, "freqcode");
	if (*freqstring != '\0') {
		freqcode = (int)*freqstring;
		if (freqcode == 'g') {
			width = height = 1024;
		} else {
			if (freqcode < '1' || freqcode > '6')
				freqcode = 0;
			else
				freqcode -= '1';
			width = defwidth[freqcode];
			height = defheight[freqcode];

			/* in case our table is wrong or incomplete... */
			width = getpropint(node, "hres", width);
			height = getpropint(node, "vres", height);
		}
	} else {
		width = 1152;
		height = 900;
	}

	/*
	 * Since the depth property is missing, we could do
	 * fb_setsize(&sc->sc_sunfb, 8, width, height, node, 0);
	 * but for safety in case it would exist and be set to 32, do it
	 * manually...
	 */
	sc->sc_sunfb.sf_depth = 8;
	sc->sc_sunfb.sf_width = width;
	sc->sc_sunfb.sf_height = height;
	sc->sc_sunfb.sf_linebytes = width >= 1024 ? width : 1024;
	sc->sc_sunfb.sf_fbsize = sc->sc_sunfb.sf_linebytes * height;

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	/* Map the frame buffer memory area we're interested in. */
	sc->sc_paddr = sbus_bus_addr(bt, sa->sa_slot, sa->sa_offset);
	if (sbus_bus_map(bt, sa->sa_slot, sa->sa_offset + PX_PLANE8_OFFSET,
	    round_page(sc->sc_sunfb.sf_fbsize), BUS_SPACE_MAP_LINEAR, 0,
	    &bh) != 0) {
		printf("%s: couldn't map 8-bit video plane\n", self->dv_xname);
		return;
	}
	sc->sc_m8 = bus_space_vaddr(bt, bh);
	if (sbus_bus_map(bt, sa->sa_slot, sa->sa_offset + PX_PLANE24_OFFSET,
	    round_page(4 * sc->sc_sunfb.sf_fbsize), BUS_SPACE_MAP_LINEAR, 0,
	    &bh) != 0) {
		printf("%s: couldn't map 32-bit video plane\n", self->dv_xname);
		return;
	}
	sc->sc_m24 = bus_space_vaddr(bt, bh);

	/* Enable video. */
	tvtwo_burner(sc, 1, 0);

	sc->sc_sunfb.sf_ro.ri_hw = sc;
	sc->sc_sunfb.sf_ro.ri_bits = (u_char *)sc->sc_m8;

	fbwscons_init(&sc->sc_sunfb, 0, isconsole);
	fbwscons_setcolormap(&sc->sc_sunfb, tvtwo_setcolor);

	if (isconsole)
		fbwscons_console_init(&sc->sc_sunfb, -1);

	fbwscons_attach(&sc->sc_sunfb, &tvtwo_accessops, isconsole);
}

int
tvtwo_ioctl(void *dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct tvtwo_softc *sc = dev;
	struct wsdisplay_fbinfo *wdf;

	/*
	 * Note that, although the emulation (text) mode is running in a
	 * 8-bit plane, we advertise the frame buffer as 32-bit.
	 */
	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_XVIDEO;
		break;
	case WSDISPLAYIO_GINFO:
		wdf = (struct wsdisplay_fbinfo *)data;
		wdf->height = sc->sc_sunfb.sf_height;
		wdf->width = sc->sc_sunfb.sf_width;
		wdf->depth = 32;
		wdf->stride = sc->sc_sunfb.sf_linebytes * 4;
		wdf->offset = 0;
		wdf->cmsize = 0;
		break;
	case WSDISPLAYIO_GETSUPPORTEDDEPTH:
		*(u_int *)data = WSDISPLAYIO_DEPTH_24_32;
		break;
	case WSDISPLAYIO_LINEBYTES:
		*(u_int *)data = sc->sc_sunfb.sf_linebytes * 4;
		break;

	case WSDISPLAYIO_GETCMAP:
	case WSDISPLAYIO_PUTCMAP:
		break;

	case WSDISPLAYIO_SMODE:
		if (*(int *)data == WSDISPLAYIO_MODE_EMUL) {
			/* Back from X11 to text mode */
			tvtwo_reset(sc, 8);
		} else {
			/* Starting X11, initialize 32-bit mode */
			tvtwo_reset(sc, 32);
		}
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
		return (-1);
	}

	return (0);
}

/*
 * Return the address that would map the given device at the given
 * offset, allowing for the given protection, or return -1 for error.
 */
paddr_t
tvtwo_mmap(void *v, off_t offset, int prot)
{
	struct tvtwo_softc *sc = v;

	if (offset & PGOFSET)
		return (-1);

	/* Allow mapping as a dumb framebuffer from offset 0 */
	if (offset >= 0 && offset < sc->sc_sunfb.sf_fbsize * 4) {
		return (bus_space_mmap(sc->sc_bustag, sc->sc_paddr,
		    PX_PLANE24_OFFSET + offset, prot, BUS_SPACE_MAP_LINEAR));
	}

	return (-1);
}

void
tvtwo_burner(void *v, u_int on, u_int flags)
{
	struct tvtwo_softc *sc = v;
	u_int32_t dispkluge;

	if (on)
		dispkluge = DISPKLUGE_DEFAULT & ~DISPKLUGE_BLANK;
	else {
		dispkluge = DISPKLUGE_DEFAULT | DISPKLUGE_BLANK;
		if (flags & WSDISPLAY_BURN_VBLANK)
			dispkluge |= DISPKLUGE_SYNC;
	}

	*(volatile u_int32_t *)(sc->sc_regs + PX_REG_DISPKLUGE) = dispkluge;
}

void
tvtwo_reset(struct tvtwo_softc *sc, u_int depth)
{
	if (depth == 32) {
		/* Initialize a direct color map. */
		tvtwo_directcmap(sc);
	} else {
		fbwscons_setcolormap(&sc->sc_sunfb, tvtwo_setcolor);
	}
}

/*
 * Simple Bt463 programming routines.
 */

static __inline__ void
tvtwo_ramdac_wraddr(struct tvtwo_softc *sc, u_int32_t addr)
{
	volatile u_int32_t *dac = (u_int32_t *)(sc->sc_regs + PX_REG_BT463_RED);

	dac[0] = (addr & 0xff);		/* lo addr */
	dac[1] = ((addr >> 8) & 0xff);	/* hi addr */
}

void
tvtwo_directcmap(struct tvtwo_softc *sc)
{
	volatile u_int32_t *dac = (u_int32_t *)(sc->sc_regs + PX_REG_BT463_RED);
	u_int32_t c;

	tvtwo_ramdac_wraddr(sc, 0);
	for (c = 0; c < 256; c++) {
		dac[3] = c;	/* R */
		dac[3] = c;	/* G */
		dac[3] = c;	/* B */
	}
}

void
tvtwo_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct tvtwo_softc *sc = v;
	volatile u_int32_t *dac = (u_int32_t *)(sc->sc_regs + PX_REG_BT463_RED);

	tvtwo_ramdac_wraddr(sc, index);
	dac[3] = r;
	dac[3] = g;
	dac[3] = b;
}
