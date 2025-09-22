/* $OpenBSD: ciavar.h,v 1.13 2006/03/16 22:32:44 miod Exp $ */
/* $NetBSD: ciavar.h,v 1.17 2000/03/19 01:43:25 thorpej Exp $ */

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

#include <dev/isa/isavar.h>
#include <dev/pci/pcivar.h>
#include <alpha/pci/pci_sgmap_pte64.h>

/*
 * A 21171 chipset's configuration.
 *
 * All of the information that the chipset-specific functions need to
 * do their dirty work (and more!).
 */
struct cia_config {
	int	cc_initted;

	struct alpha_bus_space cc_iot, cc_memt;
	struct alpha_pci_chipset cc_pc;

	struct alpha_bus_dma_tag cc_dmat_direct;
	struct alpha_bus_dma_tag cc_dmat_sgmap;

	struct alpha_sgmap cc_sgmap;

	u_int32_t cc_hae_mem;
	u_int32_t cc_hae_io;

	u_int32_t cc_rev;
	u_int32_t cc_cnfg;

	int	cc_flags;

#define	CCF_ISPYXIS	0x01		/* chip is a 21174 Pyxis */
#define	CCF_PYXISBUG	0x02
#define	CCF_PCI_USE_BWX	0x04		/* use BWX for PCI config space */
#define	CCF_BUS_USE_BWX	0x08		/* use BWX for bus space */

	struct extent *cc_io_ex, *cc_d_mem_ex, *cc_s_mem_ex;
	int	cc_mallocsafe;
};

void	cia_init(struct cia_config *, int);
void	cia_pci_init(pci_chipset_tag_t, void *);
void	cia_dma_init(struct cia_config *);

void	cia_bwx_bus_io_init(bus_space_tag_t, void *);
void	cia_bwx_bus_mem_init(bus_space_tag_t, void *);

void	cia_bus_io_init(bus_space_tag_t, void *);
void	cia_bus_mem_init(bus_space_tag_t, void *);

void	cia_pyxis_intr_enable(int, int);
