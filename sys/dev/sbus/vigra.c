/*	$OpenBSD: vigra.c,v 1.14 2022/07/15 17:57:27 kettenis Exp $	*/

/*
 * Copyright (c) 2002, 2003, Miodrag Vallat.
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
 * Driver for the Vigra VS series of SBus framebuffers.
 *
 * The VS10, VS11 and VS12 models are supported. VS10-EK is handled by the
 * regular cgthree driver.
 *
 * The monochrome VS14, 16 grays VS15, and color VS18 are not supported.
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
 * The hardware information below has been gathered through experiments, as
 * well as the debug information of the SunOS 4.x vigfb driver.
 */

/*
 * Control and status registers
 */

struct csregs {
	u_int32_t	sosr;
	u_int32_t	g3rr;
	u_int32_t	bcr;	/* board control register */
	u_int32_t	spr;
	u_int32_t	g3sr;	/* ramdac status register */
#define	STATUS_INTR	0x0001
	u_int32_t	imr;	/* interrupt mode register */
	u_int32_t	ewcr;
	u_int32_t	ssr;
};

/*
 * G300 layout
 */

struct g300dac {
	u_int32_t	cmap[256];
	u_int32_t	g3null;
	u_int32_t	unused1[32];
	u_int32_t	half_sync;
	u_int32_t	back_porch;
	u_int32_t	display;
	u_int32_t	short_display;
	u_int32_t	broad_pulse;
	u_int32_t	vsync;
	u_int32_t	vblank;
	u_int32_t	vdisplay;
	u_int32_t	line_time;
	u_int32_t	tos1;
	u_int32_t	mem_init;
	u_int32_t	transfer_delay;
	u_int32_t	unused2[19];
	u_int32_t	mask;
	u_int32_t	unused3[31];
	u_int32_t	cr;
	u_int32_t	unused4[31];
	u_int32_t	tos2;
	u_int32_t	unused5[31];
	u_int32_t	boot_location;
};

/*
 * G335 layout
 */

struct g335dac {
	u_int32_t	boot_location;
	u_int32_t	unused1[32];
	u_int32_t	half_sync;
	u_int32_t	back_porch;
	u_int32_t	display;
	u_int32_t	short_display;
	u_int32_t	broad_pulse;
	u_int32_t	vsync;
	u_int32_t	vpre_equalize;
	u_int32_t	vpost_equalize;
	u_int32_t	vblank;
	u_int32_t	vdisplay;
	u_int32_t	line_time;
	u_int32_t	tos1;
	u_int32_t	mem_init;
	u_int32_t	transfer_delay;
	u_int32_t	unused2[17];
	u_int32_t	mask;
	u_int32_t	unused3[31];
	u_int32_t	cra;
	u_int32_t	unused4[15];
	u_int32_t	crb;
	u_int32_t	unused5[15];
	u_int32_t	tos2;
	u_int32_t	unused6[32];
	u_int32_t	cursor_palette[3];
	u_int32_t	unused7[28];
	u_int32_t	checksum[3];
	u_int32_t	unused8[4];
	u_int32_t	cursor_position;
	u_int32_t	unused9[56];
	u_int32_t	cmap[256];
	u_int32_t	cursor_store[512];
};

union dac {
	struct g300dac	g300;
	struct g335dac	g335;
};

/*
 * SBUS register mappings
 */
#define	VIGRA_REG_RAMDAC	1	/* either G300 or G335 */
#define	VIGRA_REG_CSR		2
#define	VIGRA_REG_VRAM		3

#define	VIGRA_NREG		4

union vigracmap {
	u_char		cm_map[256][4];	/* 256 R/G/B entries plus pad */
	u_int32_t	cm_chip[256];	/* the way the chip gets loaded */
};

/* per-display variables */
struct vigra_softc {
	struct	sunfb sc_sunfb;		/* common base part */
	bus_space_tag_t	sc_bustag;
	bus_addr_t	sc_paddr;
	volatile struct	csregs *sc_regs;/* control registers */
	volatile union dac *sc_ramdac;	/* ramdac registers */
	union	vigracmap sc_cmap;	/* current colormap */
	int	sc_g300;
	void	*sc_ih;
	int	sc_nscreens;
};

int vigra_ioctl(void *, u_long, caddr_t, int, struct proc *);
paddr_t vigra_mmap(void *, off_t, int);
void vigra_setcolor(void *, u_int, u_int8_t, u_int8_t, u_int8_t);
int vigra_getcmap(union vigracmap *, struct wsdisplay_cmap *, int);
int vigra_putcmap(union vigracmap *, struct wsdisplay_cmap *, int);
void vigra_loadcmap_immediate(struct vigra_softc *, int, int);
static __inline__ void vigra_loadcmap_deferred(struct vigra_softc *,
    u_int, u_int);
void vigra_burner(void *, u_int, u_int);
int vigra_intr(void *);

struct wsdisplay_accessops vigra_accessops = {
	.ioctl = vigra_ioctl,
	.mmap = vigra_mmap,
	.burn_screen = vigra_burner
};

int	vigramatch(struct device *, void *, void *);
void	vigraattach(struct device *, struct device *, void *);

const struct cfattach vigra_ca = {
	sizeof (struct vigra_softc), vigramatch, vigraattach
};

struct cfdriver vigra_cd = {
	NULL, "vigra", DV_DULL
};

/*
 * Match a supported vigra card.
 */
int
vigramatch(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp("vs10", sa->sa_name) != 0 &&
	    strcmp("vs11", sa->sa_name) != 0 &&
	    strcmp("vs12", sa->sa_name) != 0)
		return (0);

	return (1);
}

/*
 * Attach and initialize a vigra display, as well as a child wsdisplay.
 */
void
vigraattach(struct device *parent, struct device *self, void *args)
{
	struct vigra_softc *sc = (struct vigra_softc *)self;
	struct sbus_attach_args *sa = args;
	bus_space_tag_t bt;
	bus_space_handle_t bh;
	int node, isconsole = 0;
	char *nam;

	bt = sa->sa_bustag;
	node = sa->sa_node;
	nam = getpropstring(node, "model");
	if (*nam == '\0')
		nam = (char *)sa->sa_name;
	printf(": %s", nam);

	isconsole = node == fbnode;

	if (sa->sa_nreg < VIGRA_NREG) {
		printf("\n%s: expected %d registers, got %d",
		    self->dv_xname, VIGRA_NREG, sa->sa_nreg);
		return;
	}

	/*
	 * Check whether we are using an G300 or an G335 chip.
	 * The VS10 and VS12 use the G300, while the VS11 uses a G335.
	 */
	sc->sc_g300 = strncmp(nam, "VIGRA,vs11", strlen("VIGRA,vs11"));

	sc->sc_bustag = bt;
	if (sbus_bus_map(bt, sa->sa_reg[VIGRA_REG_CSR].sbr_slot,
	    sa->sa_reg[VIGRA_REG_CSR].sbr_offset,
	    sa->sa_reg[VIGRA_REG_CSR].sbr_size, BUS_SPACE_MAP_LINEAR, 0,
	    &bh) != 0) {
		printf("\n%s: can't map control registers\n", self->dv_xname);
		return;
	}
	sc->sc_regs = bus_space_vaddr(bt, bh);
	if (sbus_bus_map(bt, sa->sa_reg[VIGRA_REG_RAMDAC].sbr_slot,
	    sa->sa_reg[VIGRA_REG_RAMDAC].sbr_offset,
	    sa->sa_reg[VIGRA_REG_RAMDAC].sbr_size, BUS_SPACE_MAP_LINEAR, 0,
	    &bh) != 0) {
		printf("\n%s: can't map ramdac registers\n", self->dv_xname);
		return;
	}
	sc->sc_ramdac = bus_space_vaddr(bt, bh);

	/* enable video */
	vigra_burner(sc, 1, 0);

	fb_setsize(&sc->sc_sunfb, 8, 1152, 900, node, 0);
	if (sbus_bus_map(bt, sa->sa_reg[VIGRA_REG_VRAM].sbr_slot,
	    sa->sa_reg[VIGRA_REG_VRAM].sbr_offset,
	    round_page(sc->sc_sunfb.sf_fbsize), BUS_SPACE_MAP_LINEAR, 0,
	    &bh) != 0) {
		printf("\n%s: can't map video memory\n", self->dv_xname);
		return;
	}
	sc->sc_sunfb.sf_ro.ri_bits = bus_space_vaddr(bt, bh);
	sc->sc_sunfb.sf_ro.ri_hw = sc;
	sc->sc_paddr = sbus_bus_addr(bt, sa->sa_reg[VIGRA_REG_VRAM].sbr_slot,
	    sa->sa_reg[VIGRA_REG_VRAM].sbr_offset);

	printf(", %dx%d\n", sc->sc_sunfb.sf_width, sc->sc_sunfb.sf_height);

	if ((sc->sc_ih = bus_intr_establish(sa->sa_bustag, sa->sa_pri,
	    IPL_TTY, 0, vigra_intr, sc, self->dv_xname)) == NULL) {
		printf("%s: couldn't establish interrupt, pri %d\n",
		    self->dv_xname, INTLEV(sa->sa_pri));
	}

	fbwscons_init(&sc->sc_sunfb, 0, isconsole);
	fbwscons_setcolormap(&sc->sc_sunfb, vigra_setcolor);

	if (isconsole)
		fbwscons_console_init(&sc->sc_sunfb, -1);

	fbwscons_attach(&sc->sc_sunfb, &vigra_accessops, isconsole);
}

int
vigra_ioctl(void *v, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct vigra_softc *sc = v;
	struct wsdisplay_cmap *cm;
	struct wsdisplay_fbinfo *wdf;
	int error;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(u_int *)data = WSDISPLAY_TYPE_UNKNOWN;
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
		error = vigra_getcmap(&sc->sc_cmap, cm, sc->sc_g300);
		if (error)
			return (error);
		break;
	case WSDISPLAYIO_PUTCMAP:
		cm = (struct wsdisplay_cmap *)data;
		error = vigra_putcmap(&sc->sc_cmap, cm, sc->sc_g300);
		if (error)
			return (error);
		/* if we can handle interrupts, defer the update */
		if (sc->sc_ih != NULL)
			vigra_loadcmap_deferred(sc, cm->index, cm->count);
		else
			vigra_loadcmap_immediate(sc, cm->index, cm->count);
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
		return (-1);	/* not supported yet */
        }

	return (0);
}

/*
 * Return the address that would map the given device at the given
 * offset, allowing for the given protection, or return -1 for error.
 */
paddr_t
vigra_mmap(void *v, off_t offset, int prot)
{
	struct vigra_softc *sc = v;

	if (offset & PGOFSET)
		return (-1);

	if (offset >= 0 && offset < sc->sc_sunfb.sf_fbsize) {
		return (bus_space_mmap(sc->sc_bustag, sc->sc_paddr,
		    offset, prot, BUS_SPACE_MAP_LINEAR));
	}

	return (-1);
}

void
vigra_setcolor(void *v, u_int index, u_int8_t r, u_int8_t g, u_int8_t b)
{
	struct vigra_softc *sc = v;

	if (sc->sc_g300) {
		sc->sc_cmap.cm_map[index][3] = r;
		sc->sc_cmap.cm_map[index][2] = g;
		sc->sc_cmap.cm_map[index][1] = b;
	} else {
		sc->sc_cmap.cm_map[index][3] = b;
		sc->sc_cmap.cm_map[index][2] = g;
		sc->sc_cmap.cm_map[index][1] = r;
	}
	sc->sc_cmap.cm_map[index][0] = 0;	/* no alpha channel */

	vigra_loadcmap_immediate(sc, index, 1);
}

int
vigra_getcmap(union vigracmap *cm, struct wsdisplay_cmap *rcm, int g300)
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	if (g300) {
		for (i = 0; i < count; i++) {
			if ((error = copyout(&cm->cm_map[index + i][3],
			    &rcm->red[i], 1)) != 0)
				return (error);
			if ((error = copyout(&cm->cm_map[index + i][1],
			    &rcm->blue[i], 1)) != 0)
				return (error);
		}
	} else {
		for (i = 0; i < count; i++) {
			if ((error = copyout(&cm->cm_map[index + i][1],
			    &rcm->red[i], 1)) != 0)
				return (error);
			if ((error = copyout(&cm->cm_map[index + i][3],
			    &rcm->blue[i], 1)) != 0)
				return (error);
		}
	}

	for (i = 0; i < count; i++) {
		if ((error = copyout(&cm->cm_map[index + i][2],
		    &rcm->green[i], 1)) != 0)
			return (error);
	}
	return (0);
}

int
vigra_putcmap(union vigracmap *cm, struct wsdisplay_cmap *rcm, int g300)
{
	u_int index = rcm->index, count = rcm->count, i;
	int error;

	if (index >= 256 || count > 256 - index)
		return (EINVAL);

	if (g300) {
		for (i = 0; i < count; i++) {
			if ((error = copyin(&rcm->red[i],
			    &cm->cm_map[index + i][3], 1)) != 0)
				return (error);
			if ((error = copyin(&rcm->blue[i],
			    &cm->cm_map[index + i][1], 1)) != 0)
				return (error);
		}
	} else {
		for (i = 0; i < count; i++) {
			if ((error = copyin(&rcm->red[i],
			    &cm->cm_map[index + i][1], 1)) != 0)
				return (error);
			if ((error = copyin(&rcm->blue[i],
			    &cm->cm_map[index + i][3], 1)) != 0)
				return (error);
		}
	}

	for (i = 0; i < count; i++) {
		if ((error = copyin(&rcm->green[i],
		    &cm->cm_map[index + i][2], 1)) != 0)
			return (error);
		cm->cm_map[index + i][0] = 0;	/* no alpha channel */
	}
	return (0);
}

void
vigra_loadcmap_immediate(struct vigra_softc *sc, int start, int ncolors)
{
	u_int32_t *colp = &sc->sc_cmap.cm_chip[start];
	volatile u_int32_t *lutp;
       
	if (sc->sc_g300)
		lutp = &(sc->sc_ramdac->g300.cmap[start]);
	else
		lutp = &(sc->sc_ramdac->g335.cmap[start]);

	while (--ncolors >= 0)
		*lutp++ = *colp++;
}

static __inline__ void
vigra_loadcmap_deferred(struct vigra_softc *sc, u_int start, u_int ncolors)
{

	sc->sc_regs->imr = 1;
}

void
vigra_burner(void *v, u_int on, u_int flags)
{
	struct vigra_softc *sc = v;

	if (on) {
		sc->sc_regs->bcr = 0;
	} else {
		sc->sc_regs->bcr = 1;
	}
}

int
vigra_intr(void *v)
{
	struct vigra_softc *sc = v;

	if (sc->sc_regs->imr == 0 ||
	    !ISSET(sc->sc_regs->g3sr, STATUS_INTR)) {
		/* Not expecting an interrupt, it's not for us. */
		return (0);
	}

	/* Acknowledge the interrupt and disable it. */
	sc->sc_regs->imr = 0;

	vigra_loadcmap_immediate(sc, 0, 256);

	return (1);
}
