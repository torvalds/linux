/*	$OpenBSD: hil_gsc.c,v 1.6 2022/03/13 08:04:38 mpi Exp $	*/
/*
 * Copyright (c) 2003, Miodrag Vallat.
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
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>
#include <machine/bus.h>

#include <hppa/dev/cpudevs.h>
#include <hppa/gsc/gscbusvar.h>

#include <machine/hil_machdep.h>

#include <dev/hil/hilvar.h>

int	hil_gsc_match(struct device *, void *, void *);
void	hil_gsc_attach(struct device *, struct device *, void *);

struct hil_gsc_softc {
	struct hil_softc sc_hs;
	int		 sc_hil_console;
};

const struct cfattach hil_gsc_ca = {
	sizeof(struct hil_gsc_softc), hil_gsc_match, hil_gsc_attach
};

int
hil_gsc_match(struct device *parent, void *match, void *aux)
{
	struct gsc_attach_args *ga = aux;

	if (ga->ga_type.iodc_type != HPPA_TYPE_FIO ||
	    ga->ga_type.iodc_sv_model != HPPA_FIO_HIL)
		return (0);

	return (1);
}

void
hil_gsc_attach(struct device *parent, struct device *self, void *aux)
{
	struct hil_gsc_softc *gsc = (void *)self;
	struct hil_softc *sc = &gsc->sc_hs;
	struct gsc_attach_args *ga = aux;

	sc->sc_bst = ga->ga_iot;
	if (bus_space_map(ga->ga_iot, ga->ga_hpa,
	    HILMAPSIZE, 0, &sc->sc_bsh)) {
		printf(": couldn't map hil controller\n");
		return;
	}

	gsc->sc_hil_console = ga->ga_dp.dp_mod == PAGE0->mem_kbd.pz_dp.dp_mod &&
	    bcmp(ga->ga_dp.dp_bc, PAGE0->mem_kbd.pz_dp.dp_bc, 6) == 0;

	hil_attach(sc, &gsc->sc_hil_console);

	gsc_intr_establish((struct gsc_softc *)parent, ga->ga_irq, IPL_TTY,
	    hil_intr, sc, sc->sc_dev.dv_xname);

	startuphook_establish(hil_attach_deferred, sc);
}
