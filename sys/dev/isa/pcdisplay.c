/* $OpenBSD: pcdisplay.c,v 1.15 2022/04/06 18:59:28 naddy Exp $ */
/* $NetBSD: pcdisplay.c,v 1.9.4.1 2000/06/30 16:27:48 simonb Exp $ */

/*
 * Copyright (c) 1998
 *	Matthias Drochner.  All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <machine/bus.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isareg.h>

#include <dev/ic/mc6845reg.h>
#include <dev/ic/pcdisplayvar.h>
#include <dev/isa/pcdisplayvar.h>

#include <dev/ic/pcdisplay.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsdisplayvar.h>

struct pcdisplay_config {
	struct pcdisplayscreen pcs;
	struct pcdisplay_handle dc_ph;
	int mono;
};

struct pcdisplay_softc {
	struct device sc_dev;
	struct pcdisplay_config *sc_dc;
	int nscreens;
};

static int pcdisplayconsole, pcdisplay_console_attached;
static struct pcdisplay_config pcdisplay_console_dc;

int	pcdisplay_match(struct device *, void *, void *);
void	pcdisplay_attach(struct device *, struct device *, void *);

static int pcdisplay_is_console(bus_space_tag_t);
static int pcdisplay_probe_col(bus_space_tag_t, bus_space_tag_t);
static int pcdisplay_probe_mono(bus_space_tag_t, bus_space_tag_t);
static void pcdisplay_init(struct pcdisplay_config *,
			     bus_space_tag_t, bus_space_tag_t,
			     int);
static int pcdisplay_pack_attr(void *, int, int, int, uint32_t *);
static void pcdisplay_unpack_attr(void *, uint32_t, int *, int *, int *);

const struct cfattach pcdisplay_ca = {
	sizeof(struct pcdisplay_softc), pcdisplay_match, pcdisplay_attach,
};

const struct wsdisplay_emulops pcdisplay_emulops = {
	pcdisplay_cursor,
	pcdisplay_mapchar,
	pcdisplay_putchar,
	pcdisplay_copycols,
	pcdisplay_erasecols,
	pcdisplay_copyrows,
	pcdisplay_eraserows,
	pcdisplay_pack_attr,
	pcdisplay_unpack_attr
};

const struct wsscreen_descr pcdisplay_scr = {
	"80x25", 80, 25,
	&pcdisplay_emulops,
	0, 0, /* no font support */
	WSSCREEN_REVERSE /* that's minimal... */
};

const struct wsscreen_descr *_pcdisplay_scrlist[] = {
	&pcdisplay_scr,
};

const struct wsscreen_list pcdisplay_screenlist = {
	sizeof(_pcdisplay_scrlist) / sizeof(struct wsscreen_descr *),
	_pcdisplay_scrlist
};

static int pcdisplay_ioctl(void *, u_long, caddr_t, int, struct proc *);
static paddr_t pcdisplay_mmap(void *, off_t, int);
static int pcdisplay_alloc_screen(void *, const struct wsscreen_descr *,
				       void **, int *, int *, uint32_t *);
static void pcdisplay_free_screen(void *, void *);
static int pcdisplay_show_screen(void *, void *, int,
				 void (*) (void *, int, int), void *);

const struct wsdisplay_accessops pcdisplay_accessops = {
	.ioctl = pcdisplay_ioctl,
	.mmap = pcdisplay_mmap,
	.alloc_screen = pcdisplay_alloc_screen,
	.free_screen = pcdisplay_free_screen,
	.show_screen = pcdisplay_show_screen
};

static int
pcdisplay_probe_col(bus_space_tag_t iot, bus_space_tag_t memt)
{
	bus_space_handle_t memh, ioh_6845;
	u_int16_t oldval, val;

	if (bus_space_map(memt, 0xb8000, 0x8000, 0, &memh))
		return (0);
	oldval = bus_space_read_2(memt, memh, 0);
	bus_space_write_2(memt, memh, 0, 0xa55a);
	val = bus_space_read_2(memt, memh, 0);
	bus_space_write_2(memt, memh, 0, oldval);
	bus_space_unmap(memt, memh, 0x8000);
	if (val != 0xa55a)
		return (0);

	if (bus_space_map(iot, 0x3d0, 0x10, 0, &ioh_6845))
		return (0);
	bus_space_unmap(iot, ioh_6845, 0x10);

	return (1);
}

static int
pcdisplay_probe_mono(bus_space_tag_t iot, bus_space_tag_t memt)
{
	bus_space_handle_t memh, ioh_6845;
	u_int16_t oldval, val;

	if (bus_space_map(memt, 0xb0000, 0x8000, 0, &memh))
		return (0);
	oldval = bus_space_read_2(memt, memh, 0);
	bus_space_write_2(memt, memh, 0, 0xa55a);
	val = bus_space_read_2(memt, memh, 0);
	bus_space_write_2(memt, memh, 0, oldval);
	bus_space_unmap(memt, memh, 0x8000);
	if (val != 0xa55a)
		return (0);

	if (bus_space_map(iot, 0x3b0, 0x10, 0, &ioh_6845))
		return (0);
	bus_space_unmap(iot, ioh_6845, 0x10);

	return (1);
}

static void
pcdisplay_init(struct pcdisplay_config *dc, bus_space_tag_t iot,
    bus_space_tag_t memt, int mono)
{
	struct pcdisplay_handle *ph = &dc->dc_ph;
	int cpos;

        ph->ph_iot = iot;
        ph->ph_memt = memt;
	dc->mono = mono;

	if (bus_space_map(memt, mono ? 0xb0000 : 0xb8000, 0x8000,
			  0, &ph->ph_memh))
		panic("pcdisplay_init: can't map mem space");
	if (bus_space_map(iot, mono ? 0x3b0 : 0x3d0, 0x10,
			  0, &ph->ph_ioh_6845))
		panic("pcdisplay_init: can't map i/o space");

	/*
	 * initialize the only screen
	 */
	dc->pcs.hdl = ph;
	dc->pcs.type = &pcdisplay_scr;
	dc->pcs.active = 1;
	dc->pcs.mem = NULL;

	cpos = pcdisplay_6845_read(ph, cursorh) << 8;
	cpos |= pcdisplay_6845_read(ph, cursorl);

	/* make sure we have a valid cursor position */
	if (cpos < 0 || cpos >= pcdisplay_scr.nrows * pcdisplay_scr.ncols)
		cpos = 0;

	dc->pcs.dispoffset = 0;
	dc->pcs.visibleoffset = 0;

	dc->pcs.vc_crow = cpos / pcdisplay_scr.ncols;
	dc->pcs.vc_ccol = cpos % pcdisplay_scr.ncols;
	pcdisplay_cursor_init(&dc->pcs, 1);
}

int
pcdisplay_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	int mono;

	/* If values are hardwired to something that they can't be, punt. */
	if ((ia->ia_iobase != IOBASEUNK &&
	     ia->ia_iobase != 0x3d0 &&
	     ia->ia_iobase != 0x3b0) ||
	    /* ia->ia_iosize != 0 || XXX isa.c */
	    (ia->ia_maddr != MADDRUNK &&
	     ia->ia_maddr != 0xb8000 &&
	     ia->ia_maddr != 0xb0000) ||
	    (ia->ia_msize != 0 && ia->ia_msize != 0x8000) ||
	    ia->ia_irq != IRQUNK || ia->ia_drq != DRQUNK)
		return (0);

	if (pcdisplay_is_console(ia->ia_iot))
		mono = pcdisplay_console_dc.mono;
	else if (ia->ia_iobase != 0x3b0 && ia->ia_maddr != 0xb0000 &&
		 pcdisplay_probe_col(ia->ia_iot, ia->ia_memt))
		mono = 0;
	else if (ia->ia_iobase != 0x3d0 && ia->ia_maddr != 0xb8000 &&
		 pcdisplay_probe_mono(ia->ia_iot, ia->ia_memt))
		mono = 1;
	else
		return (0);

	ia->ia_iobase = mono ? 0x3b0 : 0x3d0;
	ia->ia_iosize = 0x10;
	ia->ia_maddr = mono ? 0xb0000 : 0xb8000;
	ia->ia_msize = 0x8000;
	return (1);
}

void
pcdisplay_attach(struct device *parent, struct device *self, void *aux)
{
	struct isa_attach_args *ia = aux;
	struct pcdisplay_softc *sc = (struct pcdisplay_softc *)self;
	int console;
	struct pcdisplay_config *dc;
	struct wsemuldisplaydev_attach_args aa;

	printf("\n");

	console = pcdisplay_is_console(ia->ia_iot);

	if (console) {
		dc = &pcdisplay_console_dc;
		sc->nscreens = 1;
		pcdisplay_console_attached = 1;
	} else {
		dc = malloc(sizeof(struct pcdisplay_config),
			    M_DEVBUF, M_WAITOK);
		if (ia->ia_iobase != 0x3b0 && ia->ia_maddr != 0xb0000 &&
		    pcdisplay_probe_col(ia->ia_iot, ia->ia_memt))
			pcdisplay_init(dc, ia->ia_iot, ia->ia_memt, 0);
		else if (ia->ia_iobase != 0x3d0 && ia->ia_maddr != 0xb8000 &&
			 pcdisplay_probe_mono(ia->ia_iot, ia->ia_memt))
			pcdisplay_init(dc, ia->ia_iot, ia->ia_memt, 1);
		else
			panic("pcdisplay_attach: display disappeared");
	}
	sc->sc_dc = dc;

	aa.console = console;
	aa.scrdata = &pcdisplay_screenlist;
	aa.accessops = &pcdisplay_accessops;
	aa.accesscookie = sc;
	aa.defaultscreens = 0;

        config_found(self, &aa, wsemuldisplaydevprint);
}


int
pcdisplay_cnattach(bus_space_tag_t iot, bus_space_tag_t memt)
{
	int mono;

	if (pcdisplay_probe_col(iot, memt))
		mono = 0;
	else if (pcdisplay_probe_mono(iot, memt))
		mono = 1;
	else
		return (ENXIO);

	pcdisplay_init(&pcdisplay_console_dc, iot, memt, mono);

	wsdisplay_cnattach(&pcdisplay_scr, &pcdisplay_console_dc,
			   pcdisplay_console_dc.pcs.vc_ccol,
			   pcdisplay_console_dc.pcs.vc_crow,
			   FG_LIGHTGREY | BG_BLACK);

	pcdisplayconsole = 1;
	return (0);
}

static int
pcdisplay_is_console(bus_space_tag_t iot)
{
	if (pcdisplayconsole &&
	    !pcdisplay_console_attached &&
	    iot == pcdisplay_console_dc.dc_ph.ph_iot)
		return (1);
	return (0);
}

static int
pcdisplay_ioctl(void *v, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	/*
	 * XXX "do something!"
	 */
	return (-1);
}

static paddr_t
pcdisplay_mmap(void *v, off_t offset, int prot)
{
	return (-1);
}

static int
pcdisplay_alloc_screen(void *v, const struct wsscreen_descr *type,
    void **cookiep, int *curxp, int *curyp, uint32_t *defattrp)
{
	struct pcdisplay_softc *sc = v;

	if (sc->nscreens > 0)
		return (ENOMEM);

	*cookiep = sc->sc_dc;
	*curxp = 0;
	*curyp = 0;
	*defattrp = FG_LIGHTGREY | BG_BLACK;
	sc->nscreens++;
	return (0);
}

static void
pcdisplay_free_screen(void *v, void *cookie)
{
	struct pcdisplay_softc *sc = v;

	if (sc->sc_dc == &pcdisplay_console_dc)
		panic("pcdisplay_free_screen: console");

	sc->nscreens--;
}

static int
pcdisplay_show_screen(void *v, void *cookie, int waitok,
    void (*cb)(void *, int, int), void *cbarg)
{
#ifdef DIAGNOSTIC
	struct pcdisplay_softc *sc = v;

	if (cookie != sc->sc_dc)
		panic("pcdisplay_show_screen: bad screen");
#endif
	return (0);
}

static int
pcdisplay_pack_attr(void *id, int fg, int bg, int flags, uint32_t *attrp)
{
	if (flags & WSATTR_REVERSE)
		*attrp = FG_BLACK | BG_LIGHTGREY;
	else
		*attrp = FG_LIGHTGREY | BG_BLACK;
	return (0);
}

static void
pcdisplay_unpack_attr(void *id, uint32_t attr, int *fg, int *bg, int *ul)
{
	if (attr == (FG_BLACK | BG_LIGHTGREY)) {
		*fg = WSCOL_BLACK;
		*bg = WSCOL_WHITE;
	} else {
		*fg = WSCOL_WHITE;
		*bg = WSCOL_BLACK;
	}
	if (ul != NULL)
		*ul = 0;
}

struct cfdriver pcdisplay_cd = {
	NULL, "pcdisplay", DV_DULL
};
