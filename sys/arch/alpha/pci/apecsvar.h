/*	$OpenBSD: apecsvar.h,v 1.10 2006/03/16 22:32:44 miod Exp $	*/
/*	$NetBSD: apecsvar.h,v 1.5 1996/11/25 03:49:36 cgd Exp $	*/

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
 * An APECS chipset's configuration.
 *
 * All of the information that the chipset-specific functions need to
 * do their dirty work (and more!).
 */
struct apecs_config {
	int	ac_initted;

	int	ac_comanche_pass2;
	int	ac_epic_pass2;
	int	ac_memwidth;

	struct alpha_bus_space ac_iot, ac_memt;
	struct alpha_pci_chipset ac_pc;

	struct alpha_bus_dma_tag ac_dmat_direct;
	struct alpha_bus_dma_tag ac_dmat_sgmap;

	struct alpha_sgmap ac_sgmap;

	u_int32_t ac_haxr1, ac_haxr2;

	struct extent *ac_io_ex, *ac_d_mem_ex, *ac_s_mem_ex;
	int	ac_mallocsafe;
};

void	apecs_init(struct apecs_config *, int);
void	apecs_pci_init(pci_chipset_tag_t, void *);
void	apecs_dma_init(struct apecs_config *);

void apecs_bus_io_init(bus_space_tag_t, void *);
void apecs_bus_mem_init(bus_space_tag_t, void *);
