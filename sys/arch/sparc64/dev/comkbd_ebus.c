/*	$OpenBSD: comkbd_ebus.c,v 1.24 2021/10/24 17:05:03 mpi Exp $	*/

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
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/syslog.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wskbdvar.h>

#include <dev/sun/sunkbdreg.h>
#include <dev/sun/sunkbdvar.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>
#include <dev/ic/ns16550reg.h>

#include <dev/cons.h>

#define	COMK_RX_RING	64
#define	COMK_TX_RING	64

struct comkbd_softc {
	struct sunkbd_softc	sc_base;

	bus_space_tag_t sc_iot;		/* bus tag */
	bus_space_handle_t sc_ioh;	/* bus handle */
	void *sc_ih, *sc_si;		/* interrupt vectors */

	u_int sc_rxcnt;
	u_int8_t sc_rxbuf[COMK_RX_RING];
	u_int8_t *sc_rxbeg, *sc_rxend, *sc_rxget, *sc_rxput;

	u_int sc_txcnt;
	u_int8_t sc_txbuf[COMK_TX_RING];
	u_int8_t *sc_txbeg, *sc_txend, *sc_txget, *sc_txput;

	u_int8_t sc_ier;
};

#define	COM_WRITE(sc,r,v) \
    bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (r), (v))
#define	COM_READ(sc,r) \
    bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (r))

int comkbd_match(struct device *, void *, void *);
void comkbd_attach(struct device *, struct device *, void *);
int comkbd_iskbd(int);

/* wskbd glue */
void comkbd_cnpollc(void *, int);
void comkbd_cngetc(void *, u_int *, int *);

/* internals */
int comkbd_enqueue(void *, u_int8_t *, u_int);
int comkbd_init(struct comkbd_softc *);
void comkbd_putc(struct comkbd_softc *, u_int8_t);
int comkbd_intr(void *);
void comkbd_soft(void *);

const struct cfattach comkbd_ca = {
	sizeof(struct comkbd_softc), comkbd_match, comkbd_attach
};

struct cfdriver comkbd_cd = {
	NULL, "comkbd", DV_DULL
};

const char *comkbd_names[] = {
	"su",
	"su_pnp",
	NULL
};

struct wskbd_consops comkbd_consops = {
	comkbd_cngetc,
	comkbd_cnpollc
};

int
comkbd_iskbd(int node)
{
	if (OF_getproplen(node, "keyboard") == 0)
		return (10);
	return (0);
}

int
comkbd_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;
	int i;

	for (i = 0; comkbd_names[i]; i++)
		if (strcmp(ea->ea_name, comkbd_names[i]) == 0)
			return (comkbd_iskbd(ea->ea_node));

	if (strcmp(ea->ea_name, "serial") == 0) {
		char compat[80];

		if ((i = OF_getproplen(ea->ea_node, "compatible")) &&
		    OF_getprop(ea->ea_node, "compatible", compat,
			sizeof(compat)) == i) {
			if (strcmp(compat, "su16550") == 0 ||
			    strcmp(compat, "su") == 0)
				return (comkbd_iskbd(ea->ea_node));
		}
	}
	return (0);
}

void
comkbd_attach(struct device *parent, struct device *self, void *aux)
{
	struct comkbd_softc *sc = (void *)self;
	struct sunkbd_softc *ss = (void *)sc;
	struct ebus_attach_args *ea = aux;
	struct wskbddev_attach_args a;
	int console;

	ss->sc_sendcmd = comkbd_enqueue;
	timeout_set(&ss->sc_bellto, sunkbd_bellstop, sc);

	sc->sc_iot = ea->ea_memtag;

	sc->sc_rxget = sc->sc_rxput = sc->sc_rxbeg = sc->sc_rxbuf;
	sc->sc_rxend = sc->sc_rxbuf + COMK_RX_RING;
	sc->sc_rxcnt = 0;

	sc->sc_txget = sc->sc_txput = sc->sc_txbeg = sc->sc_txbuf;
	sc->sc_txend = sc->sc_txbuf + COMK_TX_RING;
	sc->sc_txcnt = 0;

	console = (ea->ea_node == OF_instance_to_package(OF_stdin()));

	sc->sc_si = softintr_establish(IPL_TTY, comkbd_soft, sc);
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
	    ea->ea_intrs[0], IPL_TTY, 0, comkbd_intr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't get hard intr\n");
		return;
	}

	if (comkbd_init(sc) == 0) {
		return;
	}

	ss->sc_click =
	    strcmp(getpropstring(optionsnode, "keyboard-click?"), "true") == 0;
	sunkbd_setclick(ss, ss->sc_click);

	a.console = console;
	if (ISTYPE5(ss->sc_layout)) {
		a.keymap = &sunkbd5_keymapdata;
#ifndef SUNKBD5_LAYOUT
		if (ss->sc_layout < MAXSUNLAYOUT &&
		    sunkbd_layouts[ss->sc_layout] != -1)
			sunkbd5_keymapdata.layout =
			    sunkbd_layouts[ss->sc_layout];
#endif
	} else {
		a.keymap = &sunkbd_keymapdata;
#ifndef SUNKBD_LAYOUT
		if (ss->sc_layout < MAXSUNLAYOUT &&
		    sunkbd_layouts[ss->sc_layout] != -1)
			sunkbd_keymapdata.layout =
			    sunkbd_layouts[ss->sc_layout];
#endif
	}
	a.accessops = &sunkbd_accessops;
	a.accesscookie = sc;

	if (console) {
		cn_tab->cn_dev = makedev(77, ss->sc_dev.dv_unit); /* XXX */
		cn_tab->cn_pollc = wskbd_cnpollc;
		cn_tab->cn_getc = wskbd_cngetc;
		wskbd_cnattach(&comkbd_consops, sc, a.keymap);
		sc->sc_ier = IER_ETXRDY | IER_ERXRDY;
		COM_WRITE(sc, com_ier, sc->sc_ier);
		COM_READ(sc, com_iir);
		COM_WRITE(sc, com_mcr, MCR_IENABLE | MCR_DTR | MCR_RTS);
	}

	sunkbd_attach(ss, &a);
}

void
comkbd_cnpollc(void *vsc, int on)
{
}

void
comkbd_cngetc(void *v, u_int *type, int *data)
{
	struct comkbd_softc *sc = v;
	int s;
	u_int8_t c;

	s = splhigh();
	while (1) {
		if (COM_READ(sc, com_lsr) & LSR_RXRDY)
			break;
	}
	c = COM_READ(sc, com_data);
	COM_READ(sc, com_iir);
	splx(s);

	sunkbd_decode(c, type, data);
}

void
comkbd_putc(struct comkbd_softc *sc, u_int8_t c)
{
	int s, timo;

	s = splhigh();

	timo = 150000;
	while (--timo) {
		if (COM_READ(sc, com_lsr) & LSR_TXRDY)
			break;
	}

	COM_WRITE(sc, com_data, c);
	bus_space_barrier(sc->sc_iot, sc->sc_ioh, 0, COM_NPORTS,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	timo = 150000;
	while (--timo) {
		if (COM_READ(sc, com_lsr) & LSR_TXRDY)
			break;
	}

	splx(s);
}

int
comkbd_enqueue(void *v, u_int8_t *buf, u_int buflen)
{
	struct comkbd_softc *sc = v;
	int s;
	u_int i;

	s = spltty();

	/* See if there is room... */
	if ((sc->sc_txcnt + buflen) > COMK_TX_RING) {
		splx(s);
		return (-1);
	}

	for (i = 0; i < buflen; i++) {
		*sc->sc_txget = *buf;
		buf++;
		sc->sc_txcnt++;
		sc->sc_txget++;
		if (sc->sc_txget == sc->sc_txend)
			sc->sc_txget = sc->sc_txbeg;
	}

	comkbd_soft(sc);

	splx(s);
	return (0);
}

void
comkbd_soft(void *vsc)
{
	struct comkbd_softc *sc = vsc;
	struct sunkbd_softc *ss = (void *)sc;
	u_int8_t cbuf[SUNKBD_MAX_INPUT_SIZE], *cptr;
	u_int8_t c;

	cptr = cbuf;
	while (sc->sc_rxcnt) {
		*cptr++ = *sc->sc_rxget;
		if (++sc->sc_rxget == sc->sc_rxend)
			sc->sc_rxget = sc->sc_rxbeg;
		sc->sc_rxcnt--;
		if (cptr - cbuf == sizeof cbuf) {
			sunkbd_input(ss, cbuf, cptr - cbuf);
			cptr = cbuf;
		}
	}
	if (cptr != cbuf)
		sunkbd_input(ss, cbuf, cptr - cbuf);

	if (sc->sc_txcnt) {
		c = sc->sc_ier | IER_ETXRDY;
		if (c != sc->sc_ier) {
			COM_WRITE(sc, com_ier, c);
			sc->sc_ier = c;
		}
		if (COM_READ(sc, com_lsr) & LSR_TXRDY) {
			sc->sc_txcnt--;
			COM_WRITE(sc, com_data, *sc->sc_txput);
			if (++sc->sc_txput == sc->sc_txend)
				sc->sc_txput = sc->sc_txbeg;
		}
	}
}

int
comkbd_intr(void *vsc)
{
	struct comkbd_softc *sc = vsc;
	u_int8_t iir, lsr, data;
	int needsoft = 0;

	/* Nothing to do */
	iir = COM_READ(sc, com_iir);
	if (iir & IIR_NOPEND)
		return (0);

	for (;;) {
		lsr = COM_READ(sc, com_lsr);
		if (lsr & LSR_RXRDY) {
			needsoft = 1;

			do {
				data = COM_READ(sc, com_data);
				if (sc->sc_rxcnt != COMK_RX_RING) {
					*sc->sc_rxput = data;
					if (++sc->sc_rxput == sc->sc_rxend)
						sc->sc_rxput = sc->sc_rxbeg;
					sc->sc_rxcnt++;
				}
				lsr = COM_READ(sc, com_lsr);
			} while (lsr & LSR_RXRDY);
		}

		if (lsr & LSR_TXRDY) {
			if (sc->sc_txcnt == 0) {
				/* Nothing further to send */
				sc->sc_ier &= ~IER_ETXRDY;
				COM_WRITE(sc, com_ier, sc->sc_ier);
			} else
				needsoft = 1;
		}

		iir = COM_READ(sc, com_iir);
		if (iir & IIR_NOPEND)
			break;
	}

	if (needsoft)
		softintr_schedule(sc->sc_si);

	return (1);
}

int
comkbd_init(struct comkbd_softc *sc)
{
	struct sunkbd_softc *ss = (void *)sc;
	u_int8_t stat, c;
	int tries;

	for (tries = 5; tries != 0; tries--) {
		int ltries;

		ss->sc_leds = 0;
		ss->sc_layout = -1;

		/* Send reset request */
		comkbd_putc(sc, SKBD_CMD_RESET);

		ltries = 1000;
		while (--ltries > 0) {
			stat = COM_READ(sc,com_lsr);
			if (stat & LSR_RXRDY) {
				c = COM_READ(sc, com_data);
				
				sunkbd_raw(ss, c);
				if (ss->sc_kbdstate == SKBD_STATE_RESET)
					break;
			}
			DELAY(1000);
		}
		if (ltries == 0)
			continue;

		/* Wait for reset to finish. */
		ltries = 1000;
		while (--ltries > 0) {
			stat = COM_READ(sc, com_lsr);
			if (stat & LSR_RXRDY) {
				c = COM_READ(sc, com_data);
				sunkbd_raw(ss, c);
				if (ss->sc_kbdstate == SKBD_STATE_GETKEY)
					break;
			}
			DELAY(1000);
		}
		if (ltries == 0)
			continue;

		/* Some Sun<=>PS/2 converters need some delay here */
		DELAY(5000);

		/* Send layout request */
		comkbd_putc(sc, SKBD_CMD_LAYOUT);

		ltries = 1000;
		while (--ltries > 0) {
			stat = COM_READ(sc, com_lsr);
			if (stat & LSR_RXRDY) {
				c = COM_READ(sc, com_data);
				sunkbd_raw(ss, c);
				if (ss->sc_layout != -1)
					break;
			}
			DELAY(1000);
		}
		if (ltries != 0)
			break;
	}
	if (tries == 0)
		printf(": no keyboard\n");
	else
		printf(": layout %d\n", ss->sc_layout);

	return tries;
}
