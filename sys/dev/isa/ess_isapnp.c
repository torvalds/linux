/*	$OpenBSD: ess_isapnp.c,v 1.9 2024/06/26 01:40:49 jsg Exp $	*/
/*	$NetBSD: ess_isa.c,v 1.4 1999/03/18 20:57:11 mycroft Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nathan J. Williams.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/cpu.h>
#include <machine/bus.h>

#include <dev/isa/isavar.h>

#include <dev/isa/essreg.h>
#include <dev/isa/essvar.h>

#ifdef ESS_ISAPNP_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

int ess_isapnp_probe(struct device *, void *, void *);
void ess_isapnp_attach(struct device *, struct device *, void *);

const struct cfattach ess_isapnp_ca = {
	sizeof(struct ess_softc), ess_isapnp_probe, ess_isapnp_attach
};

int
ess_isapnp_probe(struct device *parent, void *match, void *aux)
{
	return 1;
}

void
ess_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct ess_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;

	sc->sc_ic = ia->ia_ic;
	sc->sc_iot = ia->ia_iot;
	sc->sc_ioh = ia->ipa_io[0].h;
	sc->sc_iobase = ia->ipa_io[0].base;

	sc->sc_audio1.irq = ia->ipa_irq[0].num;
	sc->sc_audio1.ist = ia->ipa_irq[0].type;
	sc->sc_audio1.drq = ia->ipa_drq[0].num;
	sc->sc_audio2.irq = ia->ipa_irq[0].num;
	sc->sc_audio2.ist = ia->ipa_irq[0].type;
	sc->sc_audio2.drq = ia->ipa_drq[1].num;

	sc->sc_isa = parent->dv_parent;

	if (!essmatch(sc)) {
		printf(": essmatch failed\n");
		return;
	}

	essattach(sc);
}
