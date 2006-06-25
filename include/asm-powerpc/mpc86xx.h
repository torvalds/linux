/*
 * MPC86xx definitions
 *
 * Author: Jeff Brown
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_POWERPC_MPC86xx_H__
#define __ASM_POWERPC_MPC86xx_H__

#include <linux/config.h>
#include <asm/mmu.h>

#ifdef CONFIG_PPC_86xx

#ifdef CONFIG_MPC8641_HPCN
#include <platforms/86xx/mpc8641_hpcn.h>
#endif

#define _IO_BASE        isa_io_base
#define _ISA_MEM_BASE   isa_mem_base
#ifdef CONFIG_PCI
#define PCI_DRAM_OFFSET pci_dram_offset
#else
#define PCI_DRAM_OFFSET 0
#endif

#define CPU0_BOOT_RELEASE 0x01000000
#define CPU1_BOOT_RELEASE 0x02000000
#define CPU_ALL_RELEASED (CPU0_BOOT_RELEASE | CPU1_BOOT_RELEASE)
#define MCM_PORT_CONFIG_OFFSET 0x1010

/* Offset from CCSRBAR */
#define MPC86xx_OPENPIC_OFFSET	(0x40000)
#define MPC86xx_MCM_OFFSET      (0x00000)
#define MPC86xx_MCM_SIZE        (0x02000)

#endif /* CONFIG_PPC_86xx */
#endif /* __ASM_POWERPC_MPC86xx_H__ */
#endif /* __KERNEL__ */
