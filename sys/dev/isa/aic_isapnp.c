/*	$OpenBSD: aic_isapnp.c,v 1.3 2022/04/06 18:59:28 naddy Exp $	*/

/*
 * Copyright (c) 2002 Anders Arnholm
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * Acknowledgements: Many of the algorithms used in this driver are
 * inspired by the work of Julian Elischer (julian@tfs.com),
 * Charles Hannum (mycroft@duality.gnu.ai.mit.edu) and Jarle Greipsland.
 * Thanks a million!
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/isa/isavar.h>

#include <dev/ic/aic6360var.h>

int	aic_isapnp_match(struct device *, void *, void *);
void	aic_isapnp_attach(struct device *, struct device *, void *);

const struct cfattach aic_isapnp_ca = {
	sizeof(struct aic_softc), aic_isapnp_match, aic_isapnp_attach
};

/*
 * INITIALIZATION ROUTINES (match, attach ++)
 */
/*
 * aic_isapnp_match: isapnp code does the probing for us, and other configured
 *		     and other configured card are found by aic_isa_match
 */
int
aic_isapnp_match(struct device *parent, void *match, void *aux)
{
	AIC_TRACE(("aic: aic_isapnp_match\n"));
	return(1);
}

/*
 * Attach the AIC6360, takes the data from isapnp and feads into aicattach.
 */
void
aic_isapnp_attach(struct device *parent, struct device *self, void *aux)
{
	struct aic_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;

	AIC_TRACE(("aic: aic_isapnp_attach\n"));

	sc->sc_iot = ia->ia_iot;
	sc->sc_ioh = ia->ia_ioh;
	sc->sc_irq = ia->ia_irq;
	sc->sc_drq = ia->ia_drq;

	AIC_TRACE(("aic: aic_isapnp_attach isa_intr_establish(...)\n"));
	sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq, IST_EDGE,
	    IPL_BIO, aicintr, sc, sc->sc_dev.dv_xname);
	AIC_TRACE(("aic: aic_isapnp_attach aicattach(0x%08x, 0x%08x, %d, %d)\n",
		sc->sc_iot, sc->sc_ioh, sc->sc_irq, sc->sc_drq));
	aicattach(sc);
}
