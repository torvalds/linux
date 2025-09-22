/*	$OpenBSD: lca.c,v 1.25 2025/06/28 16:04:09 miod Exp $	*/
/*	$NetBSD: lca.c,v 1.14 1996/12/05 01:39:35 cgd Exp $	*/

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

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Jeffrey Hsu and Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/cpuconf.h>
#include <machine/rpb.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/lcareg.h>
#include <alpha/pci/lcavar.h>
#ifdef DEC_AXPPCI_33
#include <alpha/pci/pci_axppci_33.h>
#endif
#ifdef DEC_ALPHABOOK1
#include <alpha/pci/pci_alphabook1.h>
#endif
#ifdef DEC_EB66
#include <alpha/pci/pci_eb66.h>
#endif

int	lcamatch(struct device *, void *, void *);
void	lcaattach(struct device *, struct device *, void *);

const struct cfattach lca_ca = {
	sizeof(struct device), lcamatch, lcaattach,
};

struct cfdriver lca_cd = {
	NULL, "lca", DV_DULL,
};

int	lcaprint(void *, const char *pnp);

void	lca_machine_check(unsigned long, struct trapframe *, unsigned long,
	    unsigned long);

/* There can be only one. */
int lcafound;
struct lca_config lca_configuration;

int
lcamatch(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	/* Make sure that we're looking for a LCA. */
	if (strcmp(ma->ma_name, lca_cd.cd_name) != 0)
		return (0);

	if (lcafound)
		return (0);

	return (1);
}

/*
 * Set up the chipset's function pointers.
 */
void
lca_init(struct lca_config *lcp, int mallocsafe)
{

	/*
	 * The LCA HAE register is WRITE-ONLY, so we can't tell where
	 * the second sparse window is actually mapped.  Therefore,
	 * we have to guess where it is.  This seems to be the normal
	 * address.
	 */
	lcp->lc_s_mem_w2_masked_base = 0x80000000;

	if (!lcp->lc_initted) {
		/* don't do these twice since they set up extents */
		lca_bus_io_init(&lcp->lc_iot, lcp);
		lca_bus_mem_init(&lcp->lc_memt, lcp);
	}
	lcp->lc_mallocsafe = mallocsafe;

	lca_pci_init(&lcp->lc_pc, lcp);
	alpha_pci_chipset = &lcp->lc_pc;
	alpha_pci_chipset->pc_name = "lca";
	alpha_pci_chipset->pc_mem = LCA_PCI_SPARSE;
	alpha_pci_chipset->pc_ports = LCA_PCI_SIO;
	alpha_pci_chipset->pc_hae_mask = IOC_HAE_ADDREXT;
	alpha_pci_chipset->pc_dense = LCA_PCI_DENSE;
	alpha_pci_chipset->pc_bwx = 0;

	/*
	 * Refer to ``DECchip 21066 and DECchip 21068 Alpha AXP Microprocessors
	 * Hardware Reference Manual''.
	 * ...
	 */

	/*
	 * According to section 6.4.1, all bits of the IOC_HAE register are
	 * undefined after reset.  Bits <31:27> are write-only.  However, we
	 * cannot blindly set it to zero.  The serial ROM code that initializes
	 * the PCI devices' address spaces, allocates sparse memory blocks in
	 * the range that must use the IOC_HAE register for address translation,
	 * and sets this register accordingly (see section 6.4.14).
	 *
	 *	IOC_HAE left AS IS.
	 */

	/*
	 * According to section 6.4.2, all bits of the IOC_CONF register are
	 * undefined after reset.  Bits <1:0> are write-only.  Set them to
	 * 0x00 for PCI Type 0 configuration access.
	 *
	 *	IOC_CONF set to ZERO.
	 */
	REGVAL64(LCA_IOC_CONF) = 0;

	/*
	 * Disable parity on the bus. According to Linux, on some systems,
	 * siop(4) behaves badly unless parity is disabled.
	 */
	REGVAL64(LCA_IOC_PAR_DIS) = IOC_PAR_DISABLE;

	lcp->lc_initted = 1;
}

void
lcaattach(struct device *parent, struct device *self, void *aux)
{
	struct lca_config *lcp;
	struct pcibus_attach_args pba;

	/* note that we've attached the chipset; can't have 2 LCAs. */
	/* Um, not sure about this.  XXX JH */
	lcafound = 1;

	/*
	 * set up the chipset's info; done once at console init time
	 * (maybe), but we must do it twice to take care of things
	 * that need to use memory allocation.
	 */
	lcp = &lca_configuration;
	lca_init(lcp, 1);

	/* XXX print chipset information */
	printf("\n");

	lca_dma_init(lcp);

	/* safe to override since LCA machines do not set mcheck_handler */
	platform.mcheck_handler = lca_machine_check;

	switch (cputype) {
#ifdef DEC_AXPPCI_33
	case ST_DEC_AXPPCI_33:
		pci_axppci_33_pickintr(lcp);
		break;
#endif
#ifdef DEC_ALPHABOOK1
	case ST_ALPHABOOK1:
		pci_alphabook1_pickintr(lcp);
		break;
#endif
#ifdef DEC_EB66
	case ST_EB66:
		pci_eb66_pickintr(lcp);
		break;
#endif

	default:
		panic("lcaattach: shouldn't be here, really...");
	}

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &lcp->lc_iot;
	pba.pba_memt = &lcp->lc_memt;
	pba.pba_dmat =
	    alphabus_dma_get_tag(&lcp->lc_dmat_direct, ALPHA_BUS_PCI);
	pba.pba_pc = &lcp->lc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
#ifdef notyet
	pba.pba_flags = PCI_FLAGS_IO_ENABLED | PCI_FLAGS_MEM_ENABLED |
	    PCI_FLAGS_MRL_OKAY | PCI_FLAGS_MRM_OKAY | PCI_FLAGS_MWI_OKAY;
#endif
	config_found(self, &pba, lcaprint);
}

int
lcaprint(void *aux, const char *pnp)
{
	register struct pcibus_attach_args *pba = aux;

	/* only PCIs can attach to LCAes; easy. */
	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}

void
lca_machine_check(unsigned long mces, struct trapframe *framep,
    unsigned long vector, unsigned long param)
{
	int64_t stat0;

	machine_check(mces, framep, vector, param);
	/* clear error flags in IOC_STATUS0 register */
	stat0 = REGVAL64(LCA_IOC_STAT0);
	REGVAL64(LCA_IOC_STAT0) = stat0;
}
