/*	$OpenBSD: mongoose_gsc.c,v 1.2 2022/03/13 08:04:38 mpi Exp $	*/

/*
 * Copyright (c) 2004, Miodrag Vallat.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <hppa/dev/cpudevs.h>

#include <dev/eisa/eisavar.h>
#include <dev/isa/isavar.h>

#include <hppa/dev/mongoosereg.h>
#include <hppa/dev/mongoosevar.h>

#include <hppa/gsc/gscbusvar.h>

void	mgattach_gsc(struct device *, struct device *, void *);
int	mgmatch_gsc(struct device *, void *, void *);

const struct cfattach mg_gsc_ca = {
	sizeof(struct mongoose_softc), mgmatch_gsc, mgattach_gsc
};

int
mgmatch_gsc(struct device *parent, void *cfdata, void *aux)
{
	struct gsc_attach_args *ga = aux;

	if (ga->ga_type.iodc_type != HPPA_TYPE_BHA ||
	    ga->ga_type.iodc_sv_model != HPPA_BHA_WEISA)
		return (0);

	return (1);
}

void
mgattach_gsc(struct device *parent, struct device *self, void *aux)
{
	struct mongoose_softc *sc = (struct mongoose_softc *)self;
	struct gsc_attach_args *ga = aux;
	bus_space_handle_t ioh;

	sc->sc_bt = ga->ga_iot;
	sc->sc_iomap = ga->ga_hpa;

	if (bus_space_map(ga->ga_iot, ga->ga_hpa + MONGOOSE_MONGOOSE,
	    sizeof(struct mongoose_regs), 0, &ioh) != 0) {
		printf(": can't map IO space\n");
		return;
	}
	sc->sc_regs = (struct mongoose_regs *)ioh;

	if (bus_space_map(ga->ga_iot, ga->ga_hpa + MONGOOSE_CTRL,
	    sizeof(struct mongoose_ctrl), 0, &ioh) != 0) {
		printf(": can't map control registers\n");
		bus_space_unmap(ga->ga_iot, (bus_space_handle_t)sc->sc_regs,
		    sizeof(struct mongoose_regs));
		return;
	}
	sc->sc_ctrl = (struct mongoose_ctrl *)ioh;

	if (mgattach_common(sc) != 0)
		return;

	sc->sc_ih = gsc_intr_establish((struct gsc_softc *)parent,
	    ga->ga_irq, IPL_HIGH, mg_intr, sc, sc->sc_dev.dv_xname);
}
