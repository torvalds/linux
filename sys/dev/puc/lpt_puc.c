/*	$OpenBSD: lpt_puc.c,v 1.11 2022/04/06 18:59:30 naddy Exp $	*/
/*	$NetBSD: lpt_puc.c,v 1.1 1998/06/26 18:52:41 cgd Exp $	*/

/*
 * Copyright (c) 1998 Christopher G. Demetriou.  All rights reserved.
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
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 * Machine-independent parallel port ('lpt') driver attachment to "PCI 
 * Universal Communications" controller driver.
 *
 * Author: Christopher G. Demetriou, May 17, 1998.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/pci/pucvar.h>
#include <dev/ic/lptvar.h>

int	lpt_puc_probe(struct device *, void *, void *);
void	lpt_puc_attach(struct device *, struct device *, void *);
int	lpt_puc_detach(struct device *, int);

const struct cfattach lpt_puc_ca = {
	sizeof(struct lpt_softc), lpt_puc_probe, lpt_puc_attach, lpt_puc_detach,
};

int
lpt_puc_probe(struct device *parent, void *match, void *aux)
{
	struct puc_attach_args *aa = aux;

	/*
	 * Locators already matched, just check the type.
	 */
	if (PUC_IS_LPT(aa->type))
		return (1);

	return (0);
}

void
lpt_puc_attach(struct device *parent, struct device *self, void *aux)
{
	struct lpt_softc *sc = (void *)self;
	struct puc_attach_args *aa = aux;
	const char *intrstr;

	sc->sc_iot = aa->t;
	sc->sc_ioh = aa->h;

	intrstr = aa->intr_string(aa);
	sc->sc_ih = aa->intr_establish(aa, IPL_TTY, lptintr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(" %s", intrstr);

	lpt_attach_common(sc);
}

int
lpt_puc_detach(struct device *self, int flags)
{
	return (0);
}
