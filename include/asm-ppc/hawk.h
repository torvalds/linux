/*
 * include/asm-ppc/hawk.h
 *
 * Support functions for MCG Falcon/Raven & HAWK North Bridge & Memory ctlr.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * Modified by Randy Vinson (rvinson@mvista.com)
 *
 * 2001,2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_HAWK_H
#define __ASMPPC_HAWK_H

#include <asm/pci-bridge.h>
#include <asm/hawk_defs.h>

extern int hawk_init(struct pci_controller *hose,
	      unsigned int ppc_reg_base, unsigned long processor_pci_mem_start,
	      unsigned long processor_pci_mem_end,
	      unsigned long processor_pci_io_start,
	      unsigned long processor_pci_io_end,
	      unsigned long processor_mpic_base);
extern unsigned long hawk_get_mem_size(unsigned int smc_base);
extern int hawk_mpic_init(unsigned int pci_mem_offset);

#endif	/* __ASMPPC_HAWK_H */
