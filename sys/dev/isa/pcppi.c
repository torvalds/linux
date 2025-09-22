/* $OpenBSD: pcppi.c,v 1.19 2022/04/06 18:59:28 naddy Exp $ */
/* $NetBSD: pcppi.c,v 1.1 1998/04/15 20:26:18 drochner Exp $ */

/*
 * Copyright (c) 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/isa/pcppireg.h>
#include <dev/isa/pcppivar.h>

#include <dev/ic/i8253reg.h>

#include "pckbd.h"
#include "hidkbd.h"
#if NPCKBD > 0 || NHIDKBD > 0
#include <dev/ic/pckbcvar.h>
#include <dev/pckbc/pckbdvar.h>
#include <dev/hid/hidkbdvar.h>
void	pcppi_kbd_bell(void *, u_int, u_int, u_int, int);
#endif

struct pcppi_softc {
	struct device sc_dv;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ppi_ioh, sc_pit1_ioh;

	struct timeout sc_bell_timeout;

	int sc_bellactive, sc_bellpitch;
	int sc_slp;
	int sc_timeout;
};

int	pcppi_match(struct device *, void *, void *);
void	pcppi_attach(struct device *, struct device *, void *);

const struct cfattach pcppi_ca = {
	sizeof(struct pcppi_softc), pcppi_match, pcppi_attach,
};

struct cfdriver pcppi_cd = {
	NULL, "pcppi", DV_DULL
};

static void pcppi_bell_stop(void *);

#define PCPPIPRI (PZERO - 1)

int
pcppi_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_handle_t ppi_ioh, pit1_ioh;
	int have_pit1, have_ppi, rv;
	u_int8_t v, nv;

	/* If values are hardwired to something that they can't be, punt. */
	if ((ia->ia_iobase != IOBASEUNK && ia->ia_iobase != IO_PPI) ||
	    ia->ia_maddr != MADDRUNK || ia->ia_msize != 0 ||
	    ia->ia_irq != IRQUNK || ia->ia_drq != DRQUNK)
		return (0);

	rv = 0;
	have_pit1 = have_ppi = 0;

	if (bus_space_map(ia->ia_iot, IO_TIMER1, 4, 0, &pit1_ioh))
		goto lose;
	have_pit1 = 1;
	if (bus_space_map(ia->ia_iot, IO_PPI, 1, 0, &ppi_ioh))
		goto lose;
	have_ppi = 1;

	/*
	 * Check for existence of PPI.  Realistically, this is either going to
	 * be here or nothing is going to be here.
	 *
	 * We don't want to have any chance of changing speaker output (which
	 * this test might, if it crashes in the middle, or something;
	 * normally it's too quick to produce anything audible), but
	 * many "combo chip" mock-PPI's don't seem to support the top bit
	 * of Port B as a settable bit.  The bottom bit has to be settable,
	 * since the speaker driver hardware still uses it.
	 */
	v = bus_space_read_1(ia->ia_iot, ppi_ioh, 0);		/* XXX */
	bus_space_write_1(ia->ia_iot, ppi_ioh, 0, v ^ 0x01);	/* XXX */
	nv = bus_space_read_1(ia->ia_iot, ppi_ioh, 0);		/* XXX */
	if (((nv ^ v) & 0x01) == 0x01)
		rv = 1;
	bus_space_write_1(ia->ia_iot, ppi_ioh, 0, v);		/* XXX */
	nv = bus_space_read_1(ia->ia_iot, ppi_ioh, 0);		/* XXX */
	if (((nv ^ v) & 0x01) != 0x00) {
		rv = 0;
		goto lose;
	}

	/*
	 * We assume that the programmable interval timer is there.
	 */

lose:
	if (have_pit1)
		bus_space_unmap(ia->ia_iot, pit1_ioh, 4);
	if (have_ppi)
		bus_space_unmap(ia->ia_iot, ppi_ioh, 1);
	if (rv) {
		ia->ia_iobase = IO_PPI;
		ia->ia_iosize = 0x1;
		ia->ia_msize = 0x0;
	}
	return (rv);
}

void
pcppi_attach(struct device *parent, struct device *self, void *aux)
{
	struct pcppi_softc *sc = (struct pcppi_softc *)self;
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot;
	struct pcppi_attach_args pa;

	timeout_set(&sc->sc_bell_timeout, pcppi_bell_stop, sc);

	sc->sc_iot = iot = ia->ia_iot;

	if (bus_space_map(iot, IO_TIMER1, 4, 0, &sc->sc_pit1_ioh) ||
	    bus_space_map(iot, IO_PPI, 1, 0, &sc->sc_ppi_ioh))
		panic("pcppi_attach: couldn't map");

	printf("\n");

	sc->sc_bellactive = sc->sc_bellpitch = sc->sc_slp = 0;

	/* Provide a beeper for the keyboard, if there isn't one already. */
#if NPCKBD > 0
	pckbd_hookup_bell(pcppi_kbd_bell, sc);
#endif
#if NHIDKBD > 0
	hidkbd_hookup_bell(pcppi_kbd_bell, sc);
#endif

	pa.pa_cookie = sc;
	while (config_found(self, &pa, 0))
		;
}

void
pcppi_bell(pcppi_tag_t self, int pitch, int period_ms, int slp)
{
	struct pcppi_softc *sc = self;
	int s1, s2;

	if (pitch < 0)
		pitch = 0;
	else if (pitch > INT_MAX - TIMER_FREQ)
		pitch = INT_MAX - TIMER_FREQ;

	if (period_ms < 0)
		period_ms = 0;
	else if (period_ms > INT_MAX / 1000)
		period_ms = INT_MAX / 1000;

	s1 = spltty(); /* ??? */
	if (sc->sc_bellactive) {
		if (sc->sc_timeout) {
			sc->sc_timeout = 0;
			timeout_del(&sc->sc_bell_timeout);
		}
		if (sc->sc_slp)
			wakeup(pcppi_bell_stop);
	}
	if (pitch == 0 || period_ms == 0) {
		pcppi_bell_stop(sc);
		sc->sc_bellpitch = 0;
		splx(s1);
		return;
	}
	if (!sc->sc_bellactive || sc->sc_bellpitch != pitch) {
		s2 = splhigh();
		bus_space_write_1(sc->sc_iot, sc->sc_pit1_ioh, TIMER_MODE,
		    TIMER_SEL2 | TIMER_16BIT | TIMER_SQWAVE);
		bus_space_write_1(sc->sc_iot, sc->sc_pit1_ioh, TIMER_CNTR2,
		    TIMER_DIV(pitch) % 256);
		bus_space_write_1(sc->sc_iot, sc->sc_pit1_ioh, TIMER_CNTR2,
		    TIMER_DIV(pitch) / 256);
		splx(s2);
		/* enable speaker */
		bus_space_write_1(sc->sc_iot, sc->sc_ppi_ioh, 0,
			bus_space_read_1(sc->sc_iot, sc->sc_ppi_ioh, 0)
			| PIT_SPKR);
	}
	sc->sc_bellpitch = pitch;

	sc->sc_bellactive = 1;

	if (slp & PCPPI_BELL_POLL) {
		delay(period_ms * 1000);
		pcppi_bell_stop(sc);
	} else {
		sc->sc_timeout = 1;
		timeout_add_msec(&sc->sc_bell_timeout, period_ms);
		if (slp & PCPPI_BELL_SLEEP) {
			sc->sc_slp = 1;
			tsleep_nsec(pcppi_bell_stop, PCPPIPRI | PCATCH, "bell",
			    INFSLP);
			sc->sc_slp = 0;
		}
	}
	splx(s1);
}

static void
pcppi_bell_stop(void *arg)
{
	struct pcppi_softc *sc = arg;
	int s;

	s = spltty(); /* ??? */
	sc->sc_timeout = 0;

	/* disable bell */
	bus_space_write_1(sc->sc_iot, sc->sc_ppi_ioh, 0,
			  bus_space_read_1(sc->sc_iot, sc->sc_ppi_ioh, 0)
			  & ~PIT_SPKR);
	sc->sc_bellactive = 0;
	if (sc->sc_slp)
		wakeup(pcppi_bell_stop);
	splx(s);
}

#if NPCKBD > 0 || NHIDKBD > 0
void
pcppi_kbd_bell(void *arg, u_int pitch, u_int period, u_int volume, int poll)
{
	/*
	 * NB: volume ignored.
	 */
	pcppi_bell(arg, volume ? pitch : 0, period,
	    poll ? PCPPI_BELL_POLL : 0);
}
#endif /* NPCKBD > 0 || NHIDKBD > 0 */
