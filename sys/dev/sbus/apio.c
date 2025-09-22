/*	$OpenBSD: apio.c,v 1.9 2022/03/13 13:34:54 mpi Exp $	*/

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
 * Driver for Aurora 210SJ parallel ports.
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
#include <dev/ic/lptvar.h>
#include "apio.h"
#include "lpt.h"

struct apio_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_csr_h;
	bus_space_handle_t	sc_clk_h;
	bus_space_handle_t	sc_lpt_h;
	void			*sc_ih;
	struct device		*sc_port;
};

struct apio_attach_args {
	char *aaa_name;
	bus_space_tag_t aaa_iot;
	bus_space_handle_t aaa_ioh;
	bus_space_handle_t aaa_clkh;
	u_int32_t aaa_pri;
	u_int8_t aaa_inten;
};

int	apio_match(struct device *, void *, void *);
void	apio_attach(struct device *, struct device *, void *);
int	apio_print(void *, const char *);
void	apio_intr_enable(struct device *, u_int8_t);

const struct cfattach apio_ca = {
	sizeof(struct apio_softc), apio_match, apio_attach
};

struct cfdriver apio_cd = {
	NULL, "apio", DV_DULL
};

int
apio_match(struct device *parent, void *match, void *aux)
{
	struct sbus_attach_args *sa = aux;

	if (strcmp(sa->sa_name, "pio1") == 0)
		return (1);
	return (0);
}

void
apio_attach(struct device *parent, struct device *self, void *aux)
{
	struct apio_softc *sc = (void *)self;
	struct sbus_attach_args *sa = aux;
	struct apio_attach_args aaa;
	char *model;

	sc->sc_bt = sa->sa_bustag;

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

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[1].sbr_slot,
	    sa->sa_reg[1].sbr_offset, sa->sa_reg[1].sbr_size,
	    0, 0, &sc->sc_clk_h)) {
		printf(": couldn't map clk\n");
		return;
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[2].sbr_slot,
	    sa->sa_reg[2].sbr_offset, sa->sa_reg[2].sbr_size,
	    0, 0, &sc->sc_lpt_h)) {
		printf(": couldn't map clk\n");
		return;
	}

	printf(": %s\n", model);

	aaa.aaa_name = "lpt";
	aaa.aaa_iot = sc->sc_bt;
	aaa.aaa_ioh = sc->sc_lpt_h;
	aaa.aaa_clkh = sc->sc_clk_h;
	aaa.aaa_inten = ASIO_CSR_SJ_PAR_INTEN;
	aaa.aaa_pri = sa->sa_intr[0].sbi_pri;
	sc->sc_port = config_found(self, &aaa, apio_print);
}

int
apio_print(void *aux, const char *name)
{
	struct apio_attach_args *aaa = aux;

	if (name != NULL)
		printf("%s at %s", aaa->aaa_name, name);
	return (UNCONF);
}

#if NLPT_APIO > 0
int	lpt_apio_match(struct device *, void *, void *);
void	lpt_apio_attach(struct device *, struct device *, void *);
int	lpt_apio_intr(void *);

struct lpt_apio_softc {
	struct lpt_softc sc_lpt;
	bus_space_handle_t sc_clk_h;
	void *sc_ih;
};

const struct cfattach lpt_apio_ca = {
	sizeof(struct lpt_apio_softc), lpt_apio_match, lpt_apio_attach
};

void
apio_intr_enable(struct device *dv, u_int8_t en)
{
	struct apio_softc *sc = (struct apio_softc *)dv;
	u_int8_t csr;

	csr = bus_space_read_1(sc->sc_bt, sc->sc_csr_h, 0);
	csr &= ~(ASIO_CSR_SBUS_INT7 | ASIO_CSR_SBUS_INT6);
	csr |= ASIO_CSR_SBUS_INT5 | en;
	bus_space_write_1(sc->sc_bt, sc->sc_csr_h, 0, csr);
}

int
lpt_apio_match(struct device *parent, void *match, void *aux)
{
	return (1);
}

void
lpt_apio_attach(struct device *parent, struct device *self, void *aux)
{
	struct lpt_apio_softc *sc = (struct lpt_apio_softc *)self;
	struct apio_attach_args *aaa = aux;

	sc->sc_lpt.sc_state = 0;
	sc->sc_lpt.sc_iot = aaa->aaa_iot;
	sc->sc_lpt.sc_ioh = aaa->aaa_ioh;
	sc->sc_clk_h = aaa->aaa_clkh;
	sc->sc_ih = bus_intr_establish(aaa->aaa_iot, aaa->aaa_pri,
	    IPL_TTY, 0, lpt_apio_intr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": cannot allocate intr\n");
		return;
	}
	apio_intr_enable(parent, aaa->aaa_inten);

	lpt_attach_common(&sc->sc_lpt);
}

int
lpt_apio_intr(void *vsc)
{
	struct lpt_apio_softc *sc = vsc;
	int r;

	r = lptintr(&sc->sc_lpt);
	bus_space_read_1(sc->sc_lpt.sc_iot, sc->sc_clk_h, 0);
	return (r);
}
#endif
