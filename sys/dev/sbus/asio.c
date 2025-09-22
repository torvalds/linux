/*	$OpenBSD: asio.c,v 1.12 2022/03/13 13:34:54 mpi Exp $	*/

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

/*
 * Driver for Aurora 210SJ serial ports.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/tty.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <dev/sbus/sbusvar.h>
#include <dev/sbus/asioreg.h>
#include <dev/ic/comvar.h>

#include "asio.h"

#define BAUD_BASE       (1843200)

struct asio_port {
	u_int8_t		ap_inten;
	bus_space_handle_t	ap_bh;
	struct device		*ap_dev;
};

struct asio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_csr_h;
	void			*sc_ih;
	struct asio_port	sc_ports[2];
	int			sc_nports;
};

struct asio_attach_args {
	char *aaa_name;
	int aaa_port;
	bus_space_tag_t aaa_iot;
	bus_space_handle_t aaa_ioh;
	u_int32_t aaa_pri;
	u_int8_t aaa_inten;
};

int	asio_match(struct device *, void *, void *);
void	asio_attach(struct device *, struct device *, void *);
int	asio_print(void *, const char *);
void	asio_intr_enable(struct device *, u_int8_t);

const struct cfattach asio_ca = {
	sizeof(struct asio_softc), asio_match, asio_attach
};

struct cfdriver asio_cd = {
	NULL, "asio", DV_DULL
};

int
asio_match(struct device *parent, void *match, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp(sa->sa_name, "sio2") == 0)
		return (1);
	return (0);
}

void
asio_attach(struct device *parent, struct device *self, void *aux)
{
	struct asio_softc *sc = (void *)self;
	struct sbus_attach_args *sa = aux;
	struct asio_attach_args aaa;
	int i;
	char *model;

	sc->sc_bt = sa->sa_bustag;
	sc->sc_nports = 2;

	model = getpropstring(sa->sa_node, "model");
	if (model == NULL) {
		printf(": empty model, unsupported\n");
		return;
	}
	if (strcmp(model, "210sj") != 0) {
		printf(": unsupported model %s\n", model);
		return;
	}

	if (sa->sa_nreg < 3) {
		printf(": %d registers expected, got %d\n",
		    3, sa->sa_nreg);
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset, sa->sa_reg[0].sbr_size,
	    0, 0, &sc->sc_csr_h)) {
		printf(": couldn't map csr\n");
		return;
	}

	for (i = 0; i < sc->sc_nports; i++) {
		if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[i + 1].sbr_slot,
		    sa->sa_reg[i + 1].sbr_offset, sa->sa_reg[i + 1].sbr_size,
		    0, 0, &sc->sc_ports[i].ap_bh)) {
			printf(": couldn't map uart%d\n", i);
			return;
		}
	}

	sc->sc_ports[0].ap_inten = ASIO_CSR_SJ_UART0_INTEN;
	sc->sc_ports[1].ap_inten = ASIO_CSR_UART1_INTEN;

	printf(": %s\n", model);

	for (i = 0; i < sc->sc_nports; i++) {
		aaa.aaa_name = "com";
		aaa.aaa_port = i;
		aaa.aaa_iot = sc->sc_bt;
		aaa.aaa_ioh = sc->sc_ports[i].ap_bh;
		aaa.aaa_inten = sc->sc_ports[i].ap_inten;
		aaa.aaa_pri = sa->sa_intr[0].sbi_pri;
		sc->sc_ports[i].ap_dev = config_found(self, &aaa, asio_print);
	}
}

int
asio_print(void *aux, const char *name)
{
	struct asio_attach_args *aaa = aux;

	if (name != NULL)
		printf("%s at %s", aaa->aaa_name, name);
	printf(" port %d", aaa->aaa_port);
	return (UNCONF);
}

#if NCOM_ASIO > 0
int	com_asio_match(struct device *, void *, void *);
void	com_asio_attach(struct device *, struct device *, void *);

const struct cfattach com_asio_ca = {
	sizeof(struct com_softc), com_asio_match, com_asio_attach
};

void
asio_intr_enable(struct device *dv, u_int8_t en)
{
	struct asio_softc *sc = (struct asio_softc *)dv;
	u_int8_t csr;

	csr = bus_space_read_1(sc->sc_bt, sc->sc_csr_h, 0);
	csr &= ~(ASIO_CSR_SBUS_INT7 | ASIO_CSR_SBUS_INT6);
	csr |= ASIO_CSR_SBUS_INT5 | en;
	bus_space_write_1(sc->sc_bt, sc->sc_csr_h, 0, csr);
}

int
com_asio_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
com_asio_attach(struct device *parent, struct device *self, void *aux)
{
	struct com_softc *sc = (struct com_softc *)self;
	struct asio_attach_args *aaa = aux;

	sc->sc_iot = aaa->aaa_iot;
	sc->sc_ioh = aaa->aaa_ioh;
	sc->sc_iobase = 0;   /* XXX WTF is iobase for? It used to be the lower 32 bits of ioh's vaddr... */
	sc->sc_hwflags = 0;
	sc->sc_swflags = 0;
	sc->sc_frequency = BAUD_BASE;

	sc->sc_ih = bus_intr_establish(aaa->aaa_iot, aaa->aaa_pri,
	    IPL_TTY, 0, comintr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": cannot allocate intr\n");
		return;
	}
	asio_intr_enable(parent, aaa->aaa_inten);

	com_attach_subr(sc);
}
#endif
