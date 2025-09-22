/*	$OpenBSD: siop_pci.c,v 1.9 2024/05/24 06:02:58 jsg Exp $ */
/*	$NetBSD: siop_pci.c,v 1.18 2005/06/28 00:28:42 thorpej Exp $	*/

/*
 * Copyright (c) 2000 Manuel Bouyer.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* SYM53c8xx PCI-SCSI I/O Processors driver: PCI front-end */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/siopvar_common.h>
#include <dev/pci/siop_pci_common.h>
#include <dev/ic/siopvar.h>

int     siop_pci_match(struct device *, void *, void *);
void    siop_pci_attach(struct device *, struct device *, void *);

struct siop_pci_softc {
	struct siop_softc siop;
	struct siop_pci_common_softc siop_pci;
};

const struct cfattach siop_pci_ca = {
	sizeof(struct siop_pci_softc), siop_pci_match, siop_pci_attach
};

int
siop_pci_match( struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;
	const struct siop_product_desc *pp;

	/* look if it's a known product */
	pp = siop_lookup_product(pa->pa_id, PCI_REVISION(pa->pa_class));
	if (pp)
		return 1;
	return 0;
}

void
siop_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct siop_pci_softc *sc = (struct siop_pci_softc *)self;

	if (siop_pci_attach_common(&sc->siop_pci, &sc->siop.sc_c,
	    pa, siop_intr) == 0)
		return;

	siop_attach(&sc->siop);
}
