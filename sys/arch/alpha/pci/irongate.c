/*	$OpenBSD: irongate.c,v 1.13 2025/06/28 16:04:09 miod Exp $	*/
/* $NetBSD: irongate.c,v 1.3 2000/11/29 06:29:10 thorpej Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/rpb.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <alpha/pci/irongatereg.h>
#include <alpha/pci/irongatevar.h>

#ifdef API_UP1000
#include <alpha/pci/pci_up1000.h>
#endif

int	irongate_match(struct device *, void *, void *);
void	irongate_attach(struct device *, struct device *, void *);

const struct cfattach irongate_ca = {
	sizeof(struct device), irongate_match, irongate_attach,
};

int	irongate_print(void *, const char *pnp);

struct cfdriver irongate_cd = {
	NULL, "irongate", DV_DULL,
};

/* There can be only one. */
struct irongate_config irongate_configuration;
int	irongate_found;

/*
 * Set up the chipset's function pointers.
 */
void
irongate_init(struct irongate_config *icp, int mallocsafe)
{
	pcitag_t tag;
	pcireg_t reg;

	icp->ic_mallocsafe = mallocsafe;

	/*
	 * Set up PCI configuration space; we can only read the
	 * revision info through configuration space.
	 */
	irongate_pci_init(&icp->ic_pc, icp);
	alpha_pci_chipset = &icp->ic_pc;
	alpha_pci_chipset->pc_name = "irongate";
	alpha_pci_chipset->pc_mem = IRONGATE_MEM_BASE;
	alpha_pci_chipset->pc_mem = IRONGATE_MEM_BASE;
	alpha_pci_chipset->pc_bwx = 1;

	tag = pci_make_tag(&icp->ic_pc, 0, IRONGATE_PCIHOST_DEV, 0);

	/* Read the revision. */
	reg = irongate_conf_read0(icp, tag, PCI_CLASS_REG);
	icp->ic_rev = PCI_REVISION(reg);

	if (icp->ic_initted == 0) {
		/* Don't do these twice, since they set up extents. */
		irongate_bus_io_init(&icp->ic_iot, icp);
		irongate_bus_mem_init(&icp->ic_memt, icp);
	}

	icp->ic_initted = 1;
}

int
irongate_match(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	/* Make sure we're looking for an Irongate. */
	if (strcmp(ma->ma_name, irongate_cd.cd_name) != 0)
		return (0);

	if (irongate_found)
		return (0);

	return (1);
}

void
irongate_attach(struct device *parent, struct device *self, void *aux)
{
	struct irongate_config *icp;
	struct pcibus_attach_args pba;

	/* Note that we've attached the chipset; can't have 2 Irongates. */
	irongate_found = 1;

	/*
	 * Set up the chipset's info; done once at console init time
	 * (maybe), but we must do it here as well to take care of things
	 * that need to use memory allocation.
	 */
	icp = &irongate_configuration;
	irongate_init(icp, 1);

	printf(": rev. %d\n", icp->ic_rev);

	irongate_dma_init(icp);

	/*
	 * Do PCI memory initialization that needs to be deferred until
	 * malloc is safe.
	 */
	irongate_bus_mem_init2(&icp->ic_memt, icp);

	switch (cputype) {
#ifdef API_UP1000
	case ST_API_NAUTILUS:
		pci_up1000_pickintr(icp);
		break;
#endif

	default:
		panic("irongate_attach: shouldn't be here, really...");
	}

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &icp->ic_iot;
	pba.pba_memt = &icp->ic_memt;
	pba.pba_dmat =
	    alphabus_dma_get_tag(&icp->ic_dmat_pci, ALPHA_BUS_PCI);
	pba.pba_pc = &icp->ic_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
#ifdef notyet
	pba.pba_flags = PCI_FLAGS_IO_ENABLED | PCI_FLAGS_MEM_ENABLED |
	    PCI_FLAGS_MRL_OKAY | PCI_FLAGS_MRM_OKAY | PCI_FLAGS_MWI_OKAY;
#endif
	config_found(self, &pba, irongate_print);
}

int
irongate_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	/* Only PCIs can attach to Irongates; easy. */
	if (pnp != NULL)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}
