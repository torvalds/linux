/* $OpenBSD: cia.c,v 1.29 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: cia.c,v 1.56 2000/06/29 08:58:45 mrg Exp $ */

/*-
 * Copyright (c) 1998, 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * Author: Chris G. Demetriou
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
#include <machine/rpb.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/ciareg.h>
#include <alpha/pci/ciavar.h>

#ifdef DEC_KN20AA
#include <alpha/pci/pci_kn20aa.h>
#endif
#ifdef DEC_EB164
#include <alpha/pci/pci_eb164.h>
#endif
#ifdef DEC_550
#include <alpha/pci/pci_550.h>
#endif
#ifdef DEC_1000A
#include <alpha/pci/pci_1000a.h>
#endif
#ifdef DEC_1000
#include <alpha/pci/pci_1000.h>
#endif

int	ciamatch(struct device *, void *, void *);
void	ciaattach(struct device *, struct device *, void *);

const struct cfattach cia_ca = {
	sizeof(struct device), ciamatch, ciaattach,
};

struct cfdriver cia_cd = {
	NULL, "cia", DV_DULL,
};

static int	ciaprint(void *, const char *pnp);

/* There can be only one. */
int ciafound;
struct cia_config cia_configuration;

/*
 * This determines if we attempt to use BWX for PCI bus and config space
 * access.  Some systems, notably with Pyxis, don't fare so well unless
 * BWX is used.
 *
 * EXCEPT!  Some devices have a really hard time if BWX is used (WHY?!).
 * So, we decouple the uses for PCI config space and PCI bus space.
 *
 * FURTHERMORE!  The Pyxis, most notably earlier revs, really don't
 * do so well if you don't use BWX for bus access.  So we default to
 * forcing BWX on those chips.
 *
 * Geez.
 */

#ifndef CIA_PCI_USE_BWX
#define	CIA_PCI_USE_BWX	1
#endif

#ifndef	CIA_BUS_USE_BWX
#define	CIA_BUS_USE_BWX	1
#endif

#ifndef CIA_PYXIS_FORCE_BWX
#define	CIA_PYXIS_FORCE_BWX 1
#endif

int	cia_pci_use_bwx = CIA_PCI_USE_BWX;
int	cia_bus_use_bwx = CIA_BUS_USE_BWX;
int	cia_pyxis_force_bwx = CIA_PYXIS_FORCE_BWX;

int
ciamatch(struct device *parent, void *match, void *aux)
{
	struct mainbus_attach_args *ma = aux;

	/* Make sure that we're looking for a CIA. */
	if (strcmp(ma->ma_name, cia_cd.cd_name) != 0)
		return (0);

	if (ciafound)
		return (0);

	return (1);
}

/*
 * Set up the chipset's function pointers.
 */
void
cia_init(struct cia_config *ccp, int mallocsafe)
{
	int pci_use_bwx = cia_pci_use_bwx;
	int bus_use_bwx = cia_bus_use_bwx;

	ccp->cc_hae_mem = REGVAL(CIA_CSR_HAE_MEM);
	ccp->cc_hae_io = REGVAL(CIA_CSR_HAE_IO);
	ccp->cc_rev = REGVAL(CIA_CSR_REV) & REV_MASK;

	/*
	 * Determine if we have a Pyxis.  Only two systypes can
	 * have this: the EB164 systype (AlphaPC164LX and AlphaPC164SX)
	 * and the DEC_550 systype (Miata).
	 */
	if ((cputype == ST_EB164 &&
	     (hwrpb->rpb_variation & SV_ST_MASK) >= SV_ST_ALPHAPC164LX_400) ||
	    cputype == ST_DEC_550) {
		ccp->cc_flags |= CCF_ISPYXIS;
		if (cia_pyxis_force_bwx)
			pci_use_bwx = bus_use_bwx = 1;
	}

	/*
	 * ALCOR/ALCOR2 Revisions >= 2 and Pyxis have the CNFG register.
	 */
	if (ccp->cc_rev >= 2 || (ccp->cc_flags & CCF_ISPYXIS) != 0)
		ccp->cc_cnfg = REGVAL(CIA_CSR_CNFG);
	else
		ccp->cc_cnfg = 0;

	if (!ccp->cc_initted) {
		/*
		 * cpu_amask is not initialized if we are invoked during
		 * console initialization.
		 */
		u_long amask = 0;
		if (alpha_implver() >= ALPHA_IMPLVER_EV5)
			amask =
			    (~alpha_amask(ALPHA_AMASK_ALL)) & ALPHA_AMASK_ALL;

		/*
		 * Use BWX iff:
		 *
		 *	- It hasn't been disabled by the user,
		 *	- it's enabled in CNFG,
		 *	- we're implementation version ev5,
		 *	- BWX is enabled in the CPU's capabilities mask (yes,
		 *	  the bit is really cleared if the capability exists...)
		 */
		if ((pci_use_bwx || bus_use_bwx) &&
		    (ccp->cc_cnfg & CNFG_BWEN) != 0 &&
		    (amask & ALPHA_AMASK_BWX) != 0) {
			u_int32_t ctrl;

			if (pci_use_bwx)
				ccp->cc_flags |= CCF_PCI_USE_BWX;
			if (bus_use_bwx)
				ccp->cc_flags |= CCF_BUS_USE_BWX;

			/*
			 * For whatever reason, the firmware seems to enable PCI
			 * loopback mode if it also enables BWX.  Make sure it's
			 * enabled if we have an old, buggy firmware rev.
			 */
			alpha_mb();
			ctrl = REGVAL(CIA_CSR_CTRL);
			if ((ctrl & CTRL_PCI_LOOP_EN) == 0) {
				REGVAL(CIA_CSR_CTRL) = ctrl | CTRL_PCI_LOOP_EN;
				alpha_mb();
			}
		}

		/* don't do these twice since they set up extents */
		if (ccp->cc_flags & CCF_BUS_USE_BWX) {
			cia_bwx_bus_io_init(&ccp->cc_iot, ccp);
			cia_bwx_bus_mem_init(&ccp->cc_memt, ccp);
		} else {
			cia_bus_io_init(&ccp->cc_iot, ccp);
			cia_bus_mem_init(&ccp->cc_memt, ccp);
		}
	}
	ccp->cc_mallocsafe = mallocsafe;

	cia_pci_init(&ccp->cc_pc, ccp);
	alpha_pci_chipset = &ccp->cc_pc;
	alpha_pci_chipset->pc_name = "cia";
	alpha_pci_chipset->pc_dense = CIA_PCI_DENSE;
	alpha_pci_chipset->pc_hae_mask = 7L << 29;
	if (ccp->cc_flags & CCF_BUS_USE_BWX) {
		alpha_pci_chipset->pc_mem = CIA_EV56_BWMEM;
		alpha_pci_chipset->pc_ports = CIA_EV56_BWIO;
		alpha_pci_chipset->pc_bwx = 1;
	} else {
		alpha_pci_chipset->pc_mem = CIA_PCI_SMEM1;
		alpha_pci_chipset->pc_ports = CIA_PCI_SIO1;
		alpha_pci_chipset->pc_bwx = 0;
	}

	ccp->cc_initted = 1;
}

void
ciaattach(struct device *parent, struct device *self, void *aux)
{
	struct cia_config *ccp;
	struct pcibus_attach_args pba;
	const char *name;
	int pass;

	/* note that we've attached the chipset; can't have 2 CIAs. */
	ciafound = 1;

	/*
	 * set up the chipset's info; done once at console init time
	 * (maybe), but we must do it here as well to take care of things
	 * that need to use memory allocation.
	 */
	ccp = &cia_configuration;
	cia_init(ccp, 1);

	if (ccp->cc_flags & CCF_ISPYXIS) {
		name = "Pyxis";
		pass = ccp->cc_rev;
	} else {
		name = "ALCOR/ALCOR2";
		pass = ccp->cc_rev + 1;
	}

	printf(": DECchip 2117x Core Logic Chipset (%s), pass %d\n",
	    name, pass);

	if (ccp->cc_cnfg)
		printf("%s: extended capabilities: %b\n", self->dv_xname,
		    ccp->cc_cnfg, CIA_CSR_CNFG_BITS);

	switch (ccp->cc_flags & (CCF_PCI_USE_BWX|CCF_BUS_USE_BWX)) {
	case CCF_PCI_USE_BWX|CCF_BUS_USE_BWX:
		name = "PCI config and bus";
		break;
	case CCF_PCI_USE_BWX:
		name = "PCI config";
		break;
	case CCF_BUS_USE_BWX:
		name = "bus";
		break;
	default:
		name = NULL;
		break;
	}
	if (name != NULL)
		printf("%s: using BWX for %s access\n", self->dv_xname, name);

#ifdef DEC_550
	if (cputype == ST_DEC_550 &&
	    (hwrpb->rpb_variation & SV_ST_MASK) < SV_ST_MIATA_1_5) {
		/*
		 * Miata 1 systems have a bug: DMA cannot cross
		 * an 8k boundary!  Make sure PCI read prefetching
		 * is disabled on these chips.  Note that secondary
		 * PCI busses don't have this problem, because of
		 * the way PPBs handle PCI read requests.
		 *
		 * In the 21174 Technical Reference Manual, this is
		 * actually documented as "Pyxis Pass 1", but apparently
		 * there are chips that report themselves as "Pass 1"
		 * which do not have the bug!  Miatas with the Cypress
		 * PCI-ISA bridge (i.e. Miata 1.5 and Miata 2) do not
		 * have the bug, so we use this check.
		 *
		 * NOTE: This bug is actually worked around in cia_dma.c,
		 * when direct-mapped DMA maps are created.
		 *
		 * XXX WE NEED TO THINK ABOUT HOW TO HANDLE THIS FOR
		 * XXX SGMAP DMA MAPPINGS!
		 */
		u_int32_t ctrl;

		/* XXX no bets... */
		printf("%s: WARNING: Pyxis pass 1 DMA bug; no bets...\n",
		    self->dv_xname);

		ccp->cc_flags |= CCF_PYXISBUG;

		alpha_mb();
		ctrl = REGVAL(CIA_CSR_CTRL);
		ctrl &= ~(CTRL_RD_TYPE|CTRL_RL_TYPE|CTRL_RM_TYPE);
		REGVAL(CIA_CSR_CTRL) = ctrl;
		alpha_mb();
	}
#endif /* DEC_550 */

	cia_dma_init(ccp);

	switch (cputype) {
#ifdef DEC_KN20AA
	case ST_DEC_KN20AA:
		pci_kn20aa_pickintr(ccp);
		break;
#endif

#ifdef DEC_EB164
	case ST_EB164:
		pci_eb164_pickintr(ccp);
		break;
#endif

#ifdef DEC_550
	case ST_DEC_550:
		pci_550_pickintr(ccp);
		break;
#endif

#ifdef DEC_1000A
	case ST_DEC_1000A:
		pci_1000a_pickintr(ccp, &ccp->cc_iot, &ccp->cc_memt,
			&ccp->cc_pc);
		break;
#endif

#ifdef DEC_1000
	case ST_DEC_1000:
		pci_1000_pickintr(ccp, &ccp->cc_iot, &ccp->cc_memt,
			&ccp->cc_pc);
		break;
#endif

	default:
		panic("ciaattach: shouldn't be here, really...");
	}

	bzero(&pba, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &ccp->cc_iot;
	pba.pba_memt = &ccp->cc_memt;
	pba.pba_dmat =
	    alphabus_dma_get_tag(&ccp->cc_dmat_direct, ALPHA_BUS_PCI);
	pba.pba_pc = &ccp->cc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
#ifdef notyet
	pba.pba_flags = PCI_FLAGS_IO_ENABLED | PCI_FLAGS_MEM_ENABLED;
	if ((ccp->cc_flags & CCF_PYXISBUG) == 0)
		pba.pba_flags |= PCI_FLAGS_MRL_OKAY | PCI_FLAGS_MRM_OKAY |
		    PCI_FLAGS_MWI_OKAY;
#endif
	config_found(self, &pba, ciaprint);
}

static int
ciaprint(void *aux, const char *pnp)
{
	register struct pcibus_attach_args *pba = aux;

	/* only PCIs can attach to CIAs; easy. */
	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return (UNCONF);
}

void
cia_pyxis_intr_enable(int irq, int onoff)
{
	u_int64_t imask;
	int s;

#if 0
	printf("cia_pyxis_intr_enable: %s %d\n",
	    onoff ? "enabling" : "disabling", irq);
#endif

	s = splhigh();
	alpha_mb();
	imask = REGVAL64(PYXIS_INT_MASK);
	if (onoff)
		imask |= (1UL << irq);
	else
		imask &= ~(1UL << irq);
	REGVAL64(PYXIS_INT_MASK) = imask;
	alpha_mb();
	splx(s);
}
