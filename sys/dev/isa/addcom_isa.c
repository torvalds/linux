/*	$OpenBSD: addcom_isa.c,v 1.8 2024/05/28 05:46:32 jsg Exp $	*/
/*	$NetBSD: addcom_isa.c,v 1.2 2000/04/21 20:13:41 explorer Exp $	*/

/*
 * Copyright (c) 2000 Michael Graff.  All rights reserved.
 * Copyright (c) 1996 Christopher G. Demetriou.  All rights reserved.
 * Copyright (c) 1995 Charles M. Hannum.  All rights reserved.
 *
 * This code is derived from public-domain software written by
 * Roland McGrath, and information provided by David Muir Sharnoff.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Charles M. Hannum.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * This code was written and tested with the Addonics FlexPort 8S.
 * It has 8 ports, using 16650-compatible chips, sharing a single
 * interrupt.
 *
 * An interrupt status register exists at 0x240, according to the
 * skimpy documentation supplied.  It doesn't change depending on
 * io base address, so only one of these cards can ever be used at
 * a time.
 *
 * NOTE: the status register does not appear to work as advertised,
 * so instead we depend on the slave devices being intelligent enough
 * to determine whether they interrupted or not.
 *
 * This card is different from the boca or other cards in that ports
 * 0..5 are from addresses 0x108..0x137, and 6..7 are from 0x200..0x20f,
 * making a gap that the other cards do not have.
 *
 * The addresses which are documented are 0x108, 0x1108, 0x1d08, and
 * 0x8508, for the base (port 0) address.
 *
 * --Michael <explorer@netbsd.org> -- April 21, 2000
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/termios.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <dev/isa/isavar.h>

#define	NSLAVES	8

/*
 * Grr.  This card always uses 0x420 for the status register, regardless
 * of io base address.
 */
#define STATUS_IOADDR	0x420
#define	STATUS_SIZE	8		/* May be bogus... */

struct addcom_softc {
	struct device sc_dev;
	void *sc_ih;

	bus_space_tag_t sc_iot;
	bus_addr_t sc_iobase;

	int sc_alive[NSLAVES];
	void *sc_slaves[NSLAVES];	/* com device unit numbers */
	bus_space_handle_t sc_slaveioh[NSLAVES];
	bus_space_handle_t sc_statusioh;
};

#define SLAVE_IOBASE_OFFSET 0x108
static int slave_iobases[8] = {
	0x108,	/* port 0, base port */
	0x110,
	0x118,
	0x120,
	0x128,
	0x130,
	0x200,	/* port 7, note address skip... */
	0x208
};

int addcomprobe(struct device *, void *, void *);
void addcomattach(struct device *, struct device *, void *);
int addcomintr(void *);
int addcomprint(void *, const char *);

const struct cfattach addcom_isa_ca = {
	sizeof(struct addcom_softc), addcomprobe, addcomattach,
};

struct cfdriver addcom_cd = {
	NULL, "addcom", DV_TTY
};

int
addcomprobe(struct device *parent, void *self, void *aux)
{
	struct isa_attach_args *ia = aux;
	int iobase = ia->ia_iobase;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int i, rv = 1;

	/*
	 * Do the normal com probe for the first UART and assume
	 * its presence, and the ability to map the other UARTS,
	 * means there is a multiport board there.
	 * XXX Needs more robustness.
	 */

	/* Disallow wildcarded i/o address. */
	if (ia->ia_iobase == -1 /* ISACF_PORT_DEFAULT */)
		return (0);

	/* if the first port is in use as console, then it. */
	if (iobase == comconsaddr && !comconsattached)
		goto checkmappings;

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
		iobase += slave_iobases[i] - slave_iobases[i - 1];

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
addcomprint(void *aux, const char *pnp)
{
	struct commulti_attach_args *ca = aux;

	if (pnp)
		printf("com at %s", pnp);
	printf(" slave %d", ca->ca_slave);
	return (UNCONF);
}

void
addcomattach(struct device *parent, struct device *self, void *aux)
{
	struct addcom_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct commulti_attach_args ca;
	bus_space_tag_t iot = ia->ia_iot;
	bus_addr_t iobase;
	int i;

	sc->sc_iot = ia->ia_iot;
	sc->sc_iobase = ia->ia_iobase;

	/* Disallow wildcard interrupt. */
	if (ia->ia_irq == IRQUNK) {
		printf(": wildcard interrupt not supported\n");
		return;
	}

	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_TTY, addcomintr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	if (bus_space_map(iot, STATUS_IOADDR, STATUS_SIZE,
	    0, &sc->sc_statusioh)) {
		printf(": can't map status space\n");
		return;
	}

	for (i = 0; i < NSLAVES; i++) {
		iobase = sc->sc_iobase
		    + slave_iobases[i]
		    - SLAVE_IOBASE_OFFSET;

		if ((!(iobase == comconsaddr && !comconsattached)) &&
		    bus_space_map(iot, iobase, COM_NPORTS, 0,
			&sc->sc_slaveioh[i])) {
			printf(": can't map i/o space for slave %d\n", i);
			return;
		}
	}

	printf("\n");

	for (i = 0; i < NSLAVES; i++) {
		ca.ca_slave = i;
		ca.ca_iot = sc->sc_iot;
		ca.ca_ioh = sc->sc_slaveioh[i];
		ca.ca_iobase = sc->sc_iobase
		    + slave_iobases[i]
		    - SLAVE_IOBASE_OFFSET;
		ca.ca_noien = 0;

		sc->sc_slaves[i] = config_found(self, &ca, addcomprint);
		if (sc->sc_slaves[i] != NULL)
			sc->sc_alive[i] = 1;
	}

}

int
addcomintr(void *arg)
{
	struct addcom_softc *sc = arg;
	int intrd, r = 0, i;

	do {
		intrd = 0;
		for (i = 0; i < NSLAVES; i++)
			if (sc->sc_alive[i] && comintr(sc->sc_slaves[i])) {
				r = 1;
				intrd = 1;
			}
	} while (intrd);

	return (r);
}
