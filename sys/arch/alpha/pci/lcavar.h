/*	$OpenBSD: lcavar.h,v 1.11 2006/03/16 22:32:44 miod Exp $	*/
/* $NetBSD: lcavar.h,v 1.7 1997/06/06 23:54:32 thorpej Exp $ */

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Jeffrey Hsu
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
 * LCA chipset's configuration.
 *
 * All of the information that the chipset-specific functions need to
 * do their dirty work (and more!).
 */
struct lca_config {
	int	lc_initted;

	struct alpha_bus_space lc_iot, lc_memt;
	struct alpha_pci_chipset lc_pc;

	struct alpha_bus_dma_tag lc_dmat_direct;
	struct alpha_bus_dma_tag lc_dmat_sgmap;

	struct alpha_sgmap lc_sgmap;

	bus_addr_t lc_s_mem_w2_masked_base;

	struct extent *lc_io_ex, *lc_d_mem_ex, *lc_s_mem_ex;
	int	lc_mallocsafe;
};

void	lca_init(struct lca_config *, int);
void	lca_pci_init(pci_chipset_tag_t, void *);
void	lca_dma_init(struct lca_config *);

void	lca_bus_io_init(bus_space_tag_t, void *);
void	lca_bus_mem_init(bus_space_tag_t, void *);
