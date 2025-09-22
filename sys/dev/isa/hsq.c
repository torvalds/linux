/*	$OpenBSD: hsq.c,v 1.8 2024/05/28 05:46:32 jsg Exp $	*/

/*-
 * Copyright (c) 1999 Denis A. Doroshenko. All rights reserved.
 *
 * This code is derived from BSD-licensed software written by
 * Christopher G. Demetriou and Charles Hannum, which was
 * derived from public-domain software written by Roland McGrath.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code  must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions  in  binary  form   must  reproduce  the  above
 *    copyright notice,  this list  of  conditions  and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY  THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL  THE AUTHOR  BE LIABLE  FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT  LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES;  LOSS OF  USE,  DATA, OR  PROFITS;  OR BUSINESS
 * INTERRUPTION) HOWEVER  CAUSED  AND  ON  ANY  THEORY  OF LIABILITY,
 * WHETHER  IN  CONTRACT,  STRICT   LIABILITY,   OR  TORT  (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Note: this copyright notice is a version of Berkeley-style license
 * derived from the original by Regents of the University of California.
 */

/*
 * This is OpenBSD driver for Hostess 4-channel serial mux -- hsq
 * ("hsq" stands for "HoStess Quad channel mux"; funny enough).
 *
 * Hostess may be the registered trademark of Comtrol Corp.
 * WARNING! The information that is below about Hostess family
 * serial cards is not based on any information from Comtrol, 
 * and I DON'T guarantee this information is authentic or right.
 * For authentic information on Hostess serial cards visit
 * http://www.comtrol.com
 *
 * Hostess mux is a very simple thing -- it's 4/8/16(?) UARTs one
 * after another, that share one IRQ. Under linux they may be used
 * with setserial, BSD systems are not so flexible with serial ports,
 * so i took ast driver and changed it, in the way i think Hostess
 * muxes should work. It works ifine with my mux (it has ti16c750
 * UARTs on it, and current com driver detects them as 550A, so i
 * changed it a bit, to use the power of 750).
 * 
 * Hostess cards use scratch register of lead UART to control the mux.
 * When a byte is written to the register it is meant as mask, which
 * enables/disables interrupts from 1-8 UARTs by setting 0-7 bits to
 * 1/0. Let us call this register UER -- UARTs Enable Register.
 * When a byte is read it is mask of bits for UARTs, that have some
 * event(s), which are analyzed using that UART's IIR. Let us call
 * this register UIR -- UARTs Interrupt Register.
 *
 * Note: four higher bits of the UIR are always 1 for 4-channel mux,
 * I have no idea what could that mean, it would be better to have
 * them zeroed.
 *
 * Shitty feature: UER's value upon power up is absolutely random,
 * so that UARTs can work and can not and you don't understand what's up...
 * Thus, we have to set its value to 0x0f to get all four UARTs 
 * interrupting, just after we've attached the mux...
 *
 * Use it and share my fun!
 * --
 * Denis A. Doroshenko <cyxob@isl.vtu.lt>
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/termios.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/isa/isavar.h>
#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

/*
 * For 8-channel Hostess serial card change: NSLAVES -> 8, UART_MASK -> 0xff
 */
#define	NSLAVES	(4)
#define UART_MASK (0x0f)

#define com_uer (com_scratch)
#define com_uir (com_scratch)

struct hsq_softc {
	struct device sc_dev;
	void *sc_ih;

	bus_space_tag_t sc_iot;
	int sc_iobase;

	void *sc_slaves[NSLAVES];	/* com device unit numbers */
	bus_space_handle_t sc_slaveioh[NSLAVES];
};

int hsqprobe(struct device *, void *, void *);
void hsqattach(struct device *, struct device *, void *);
int hsqintr(void *);
int hsqprint(void *, const char *);

const struct cfattach hsq_ca = {
	sizeof(struct hsq_softc), hsqprobe, hsqattach
};

struct cfdriver hsq_cd = {
	NULL, "hsq", DV_TTY
};

int
hsqprobe(struct device *parent, void *self, void *aux)
{
	struct isa_attach_args *ia = aux;
	int iobase = ia->ia_iobase;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int i, rv = 1;

	/*
	 * Do the normal com probe for the first UART and assume
	 * its presence, and the ability to map the other UARTs,
	 * means there is a multiport board there.
	 * XXX Needs more robustness.
	 */

	/* if the first port is in use as console, then it. */
	if (iobase == comconsaddr && !comconsattached)
		goto checkmappings;

	/* map UART ports (COM_NPORTS == 8) */
	if (bus_space_map(iot, iobase, COM_NPORTS, 0, &ioh)) {
		rv = 0;
		goto out;
	}

	rv = comprobe1(iot, ioh);
	bus_space_unmap(iot, ioh, COM_NPORTS);
	if (rv == 0)
		goto out;

checkmappings:
	for (i = 1; i < NSLAVES; i++) {
		iobase += COM_NPORTS;

		if (iobase == comconsaddr && !comconsattached)
			continue;

		if (bus_space_map(iot, iobase, COM_NPORTS, 0, &ioh)) {
			rv = 0;
			goto out;
		}
		bus_space_unmap(iot, ioh, COM_NPORTS);
	}

out:
	if (rv)
		ia->ia_iosize = NSLAVES * COM_NPORTS;
	return (rv);
}

int
hsqprint(void *aux, const char *pnp)
{
	struct commulti_attach_args *ca = aux;

	if (pnp)
		printf("com at %s", pnp);
	printf(" slave %d", ca->ca_slave);
	return (UNCONF);
}

void
hsqattach(struct device *parent, struct device *self, void *aux)
{
	struct hsq_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct commulti_attach_args ca;
	int i;

	sc->sc_iot = ia->ia_iot;
	sc->sc_iobase = ia->ia_iobase;

	for (i = 0; i < NSLAVES; i++)
		if (bus_space_map(sc->sc_iot, sc->sc_iobase + i * COM_NPORTS,
		    COM_NPORTS, 0, &sc->sc_slaveioh[i]))
			panic("hsqattach: couldn't map slave %d", i);


	printf("\n");

	for (i = 0; i < NSLAVES; i++) {
		ca.ca_slave = i;
		ca.ca_iot = sc->sc_iot;
		ca.ca_ioh = sc->sc_slaveioh[i];
		ca.ca_iobase = sc->sc_iobase + i * COM_NPORTS;
		ca.ca_noien = 1;

		sc->sc_slaves[i] = config_found(self, &ca, hsqprint);
	}

	/* allow interrupts from all four UARTs */
	bus_space_write_1(sc->sc_iot, sc->sc_slaveioh[0], com_uer, UART_MASK);

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_TTY, hsqintr, sc, sc->sc_dev.dv_xname);
}

int
hsqintr(void *arg)
{
	struct hsq_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	int bits;

	bits = bus_space_read_1(iot, sc->sc_slaveioh[0], com_uir) & UART_MASK;
	if ( !bits )
		return (0);		/* no interrupts pending */

	for (;;) {
#define TRY(n) \
	if ( sc->sc_slaves[n] && bits & (1 << (n)) ) \
		comintr(sc->sc_slaves[n]);

		TRY(0);
		TRY(1);
		TRY(2);
		TRY(3);
#undef TRY

		bits = bus_space_read_1(iot, sc->sc_slaveioh[0],
					com_uir) & UART_MASK;
		if ( !bits )
			return (1);		/* no interrupts pending */
 	}
}
