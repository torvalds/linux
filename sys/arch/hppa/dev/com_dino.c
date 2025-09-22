/*	$OpenBSD: com_dino.c,v 1.6 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 2004 Michael Shalayeff
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/tty.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <hppa/dev/cpudevs.h>

void *dino_intr_establish(void *sc, int irq, int pri,
    int (*handler)(void *v), void *arg, const char *name);

#define	COM_DINO_FREQ	7272700

struct com_dino_regs {
	u_int8_t	reset;
	u_int8_t	pad0[3];
	u_int8_t	test;
#define	COM_DINO_PAR_LOOP	0x01
#define	COM_DINO_CLK_SEL	0x02
	u_int8_t	pad1[3];
	u_int32_t	iodc;
	u_int8_t	pad2[0x54];
	u_int8_t	dither;
};

int	com_dino_match(struct device *, void *, void *);
void	com_dino_attach(struct device *, struct device *, void *);

const struct cfattach com_dino_ca = {
	sizeof(struct com_softc), com_dino_match, com_dino_attach
};

int
com_dino_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;

	if (ca->ca_type.iodc_type != HPPA_TYPE_FIO ||
	    ca->ca_type.iodc_sv_model != HPPA_FIO_GRS232)
		return (0);

	return (1);
	/* HOZER comprobe1(ca->ca_iot, ca->ca_hpa + IOMOD_DEVOFFSET); */
}

void
com_dino_attach(struct device *parent, struct device *self, void *aux)
{
	struct com_softc *sc = (void *)self;
	struct confargs *ca = aux;
	struct com_dino_regs *regs = (struct com_dino_regs *)ca->ca_hpa;

	sc->sc_iot = ca->ca_iot;
	sc->sc_iobase = (bus_addr_t)ca->ca_hpa + IOMOD_DEVOFFSET;

	if (bus_space_map(sc->sc_iot, sc->sc_iobase, COM_NPORTS,
	    0, &sc->sc_ioh)) {
		printf(": cannot map io space\n");
		return;
	}

	if (PAGE0->mem_cons.pz_class == PCL_DUPLEX &&
	    PAGE0->mem_cons.pz_hpa == ca->ca_hpa) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, COM_NPORTS);
		comcnattach(sc->sc_iot, sc->sc_iobase, comdefaultrate,
		    COM_DINO_FREQ, comconscflag);
	}

	/* select clock freq */
	regs->test = COM_DINO_CLK_SEL;
	sc->sc_frequency = COM_DINO_FREQ;

	com_attach_subr(sc);

	sc->sc_ih = dino_intr_establish(parent, ca->ca_irq, IPL_TTY,
	    comintr, sc, sc->sc_dev.dv_xname);
}
