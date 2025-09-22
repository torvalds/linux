/*	$OpenBSD: comms_ebus.c,v 1.2 2009/06/17 06:48:38 matthieu Exp $	*/

/*
 * Copyright (c) 2009 Miodrag Vallat.
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
/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/syslog.h>
#include <sys/tty.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsmousevar.h>

#include <dev/sun/sunmsvar.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>
#include <dev/ic/ns16550reg.h>
#define	com_lcr com_cfcr

#include <dev/cons.h>

/* should match com_ebus.c */
#define	BAUD_BASE	(1843200)

#define	COMMS_RX_RING	64

struct comms_softc {
	struct	sunms_softc sc_base;

	u_int	sc_ier;

	bus_space_tag_t sc_iot;		/* bus tag */
	bus_space_handle_t sc_ioh;	/* bus handle */
	void *sc_ih, *sc_si;		/* interrupt vectors */

	u_int sc_rxcnt;
	u_int8_t sc_rxbuf[COMMS_RX_RING];
	u_int8_t *sc_rxbeg, *sc_rxend, *sc_rxget, *sc_rxput;
};

#define	COM_WRITE(sc,r,v) \
    bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (r), (v))
#define	COM_READ(sc,r) \
    bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (r))

/*
 * autoconf glue.
 */

int	comms_match(struct device *, void *, void *);
void	comms_attach(struct device *, struct device *, void *);

const struct cfattach comms_ca = {
	sizeof(struct comms_softc), comms_match, comms_attach
};

struct cfdriver comms_cd = {
	NULL, "comms", DV_DULL
};

/*
 * wsmouse accessops.
 */

void	comms_disable(void *);
int	comms_enable(void *);

const struct wsmouse_accessops comms_accessops = {
	comms_enable,
	sunms_ioctl,
	comms_disable
};

/*
 *  com glue.
 */

int	comms_hardintr(void *);
int	comms_ismouse(int);
void	comms_softintr(void *);
void	comms_speed_change(void *, uint);

/*
 * autoconf glue.
 */

static const char *comms_names[] = {
	"su",
	"su_pnp",
	NULL
};

int
comms_ismouse(int node)
{
	if (OF_getproplen(node, "mouse") == 0)
		return 10;
	return 0;
}

int
comms_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;
	int i;

	for (i = 0; comms_names[i]; i++)
		if (strcmp(ea->ea_name, comms_names[i]) == 0)
			return comms_ismouse(ea->ea_node);

	if (strcmp(ea->ea_name, "serial") == 0) {
		char compat[80];

		if ((i = OF_getproplen(ea->ea_node, "compatible")) &&
		    OF_getprop(ea->ea_node, "compatible", compat,
			sizeof(compat)) == i) {
			if (strcmp(compat, "su16550") == 0 ||
			    strcmp(compat, "su") == 0)
				return comms_ismouse(ea->ea_node);
		}
	}
	return 0;
}

void
comms_attach(struct device *parent, struct device *self, void *aux)
{
	struct comms_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;

	sc->sc_iot = ea->ea_memtag;

	sc->sc_rxget = sc->sc_rxput = sc->sc_rxbeg = sc->sc_rxbuf;
	sc->sc_rxend = sc->sc_rxbuf + COMMS_RX_RING;
	sc->sc_rxcnt = 0;

	/* we really want IPL_TTY here. */
	sc->sc_si = softintr_establish(IPL_TTY, comms_softintr, sc);
	if (sc->sc_si == NULL) {
		printf(": can't get soft intr\n");
		return;
	}

	/* Use prom address if available, otherwise map it. */
	if (ea->ea_nvaddrs && bus_space_map(ea->ea_memtag, ea->ea_vaddrs[0], 0,
	    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_memtag;
	} else if (ebus_bus_map(ea->ea_memtag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_memtag;
	} else if (ebus_bus_map(ea->ea_iotag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]),
	    ea->ea_regs[0].size, 0, 0, &sc->sc_ioh) == 0) {
		sc->sc_iot = ea->ea_iotag;
	} else {
		printf(": can't map register space\n");
                return;
	}

	sc->sc_ih = bus_intr_establish(sc->sc_iot,
	    ea->ea_intrs[0], IPL_TTY, 0, comms_hardintr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't get hard intr\n");
		return;
	}

	/* Initialize hardware. */
	sc->sc_ier = 0;
	comms_speed_change(sc, INIT_SPEED);

	sc->sc_base.sc_speed_change = comms_speed_change;

	sunms_attach(&sc->sc_base, &comms_accessops);
}

/*
 * wsmouse accessops.
 */

void
comms_disable(void *v)
{
	struct comms_softc *sc = v;
	int s;

	s = spltty();
	sc->sc_ier = 0;
	COM_WRITE(sc, com_ier, sc->sc_ier);
	splx(s);
}

int
comms_enable(void *v)
{
	struct comms_softc *sc = v;
	int s;

	s = spltty();
	sc->sc_ier = IER_ERXRDY;
	COM_WRITE(sc, com_ier, sc->sc_ier);
	splx(s);

	return 0;
}

/*
 * com glue.
 */

void
comms_softintr(void *v)
{
	struct comms_softc *sc = v;
	uint8_t c;

	/*
	 * If we have a baud rate change pending, do it now.
	 * This will reset the rx ring, so we can proceed safely.
	 */
	if (sc->sc_base.sc_state == STATE_RATE_CHANGE) {
		sunms_speed_change(&sc->sc_base);
	}

	/*
	 * Copy data from the receive ring, if any, to the event layer.
	 */
	while (sc->sc_rxcnt) {
		c = *sc->sc_rxget;
		if (++sc->sc_rxget == sc->sc_rxend)
			sc->sc_rxget = sc->sc_rxbeg;
		sc->sc_rxcnt--;
		sunms_input(&sc->sc_base, c);
	}
}

int
comms_hardintr(void *v)
{
	struct comms_softc *sc = v;
	u_int8_t iir, lsr, data;
	int needsoft = 0;

	/* Nothing to do */
	iir = COM_READ(sc, com_iir);
	if (ISSET(iir, IIR_NOPEND))
		return 0;

	for (;;) {
		lsr = COM_READ(sc, com_lsr);

		if (ISSET(lsr, LSR_BI)) {
			if (sc->sc_base.sc_state != STATE_RATE_CHANGE &&
			    ++sc->sc_base.sc_brk > 1) {
				sc->sc_base.sc_state = STATE_RATE_CHANGE;
				needsoft = 1;
#ifdef DEBUG
				printf("%s: break detected, changing speed\n",
				    sc->sc_base.sc_dev.dv_xname);
#endif
			}
		}

		if (ISSET(lsr, LSR_RXRDY)) {
			needsoft = 1;

			do {
				data = COM_READ(sc, com_data);
				if (sc->sc_base.sc_state != STATE_RATE_CHANGE &&
				    sc->sc_rxcnt != COMMS_RX_RING) {
					*sc->sc_rxput = data;
					if (++sc->sc_rxput == sc->sc_rxend)
						sc->sc_rxput = sc->sc_rxbeg;
					sc->sc_rxcnt++;
				}
				lsr = COM_READ(sc, com_lsr);
			} while (ISSET(lsr, LSR_RXRDY));
		}

		iir = COM_READ(sc, com_iir);
		if (ISSET(iir, IIR_NOPEND))
			break;
	}

	if (needsoft)
		softintr_schedule(sc->sc_si);

	return 1;
}

/*
 * Reinitialize the line to a different speed.  Invoked at spltty().
 */
void
comms_speed_change(void *v, uint bps)
{
	struct comms_softc *sc = v;
	int ospeed;

	/*
	 * Eat everything on the line.
	 */
	while (ISSET(COM_READ(sc, com_lsr), LSR_RXRDY))
		COM_READ(sc, com_data);

	ospeed = comspeed(BAUD_BASE, bps);

	/* disable interrupts while the chip is reprogrammed */
	COM_WRITE(sc, com_ier, 0);

	COM_WRITE(sc, com_lcr, LCR_DLAB);
	COM_WRITE(sc, com_dlbl, ospeed);
	COM_WRITE(sc, com_dlbh, ospeed >> 8);
	/* 8 data bits, no parity, 2 stop bits */
	COM_WRITE(sc, com_lcr, LCR_8BITS | LCR_PNONE | LCR_STOPB);
	COM_READ(sc, com_iir);

	COM_WRITE(sc, com_mcr, MCR_IENABLE | MCR_DTR | MCR_RTS);
	/* XXX do something about the FIFO? */

	COM_WRITE(sc, com_ier, sc->sc_ier);
}
