/*
 * arch/ppc/kernel/harrier.h
 *
 * Definitions for Motorola MCG Harrier North Bridge & Memory controller
 *
 * Author: Dale Farnsworth
 *         dale.farnsworth@mvista.com
 *
 * Modified by: Randy Vinson
 * 	   rvinson@mvista.com
 *
 * Copyright 2001-2002 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __ASMPPC_HARRIER_H
#define __ASMPPC_HARRIER_H

#include <linux/types.h>
#include <asm/pci-bridge.h>

struct pci_controller;
int harrier_init(struct pci_controller *hose,
		 uint ppc_reg_base,
		 ulong processor_pci_mem_start,
		 ulong processor_pci_mem_end,
		 ulong processor_pci_io_start,
		 ulong processor_pci_io_end,
		 ulong processor_mpic_base);

unsigned long harrier_get_mem_size(uint smc_base);

int harrier_mpic_init(unsigned int pci_mem_offset);

void harrier_setup_nonmonarch(uint ppc_reg_base,
			      uint in0_size);
void harrier_release_eready(uint ppc_reg_base);

void harrier_wait_eready(uint ppc_reg_base);

#endif /* __ASMPPC_HARRIER_H */
