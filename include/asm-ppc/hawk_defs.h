/*
 * include/asm-ppc/hawk_defs.h
 *
 * Definitions for Motorola MCG Falcon/Raven & HAWK North Bridge & Memory ctlr.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * Modified by Randy Vinson (rvinson@mvista.com)
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASMPPC_HAWK_DEFS_H
#define __ASMPPC_HAWK_DEFS_H

#include <asm/pci-bridge.h>

/*
 * The Falcon/Raven and HAWK have 4 sets of registers:
 *   1) PPC Registers which define the mappings from PPC bus to PCI bus,
 *      etc.
 *   2) PCI Registers which define the mappings from PCI bus to PPC bus and the
 *      MPIC base address.
 *   3) MPIC registers
 *   4) System Memory Controller (SMC) registers.
 */

#define HAWK_PCI_CONFIG_ADDR_OFF	0x00000cf8
#define HAWK_PCI_CONFIG_DATA_OFF	0x00000cfc

#define HAWK_MPIC_SIZE			0x00040000U
#define HAWK_SMC_SIZE			0x00001000U

/*
 * Define PPC register offsets.
 */
#define HAWK_PPC_XSADD0_OFF			0x40
#define HAWK_PPC_XSOFF0_OFF			0x44
#define HAWK_PPC_XSADD1_OFF			0x48
#define HAWK_PPC_XSOFF1_OFF			0x4c
#define HAWK_PPC_XSADD2_OFF			0x50
#define HAWK_PPC_XSOFF2_OFF			0x54
#define HAWK_PPC_XSADD3_OFF			0x58
#define HAWK_PPC_XSOFF3_OFF			0x5c

/*
 * Define PCI register offsets.
 */
#define HAWK_PCI_PSADD0_OFF			0x80
#define HAWK_PCI_PSOFF0_OFF			0x84
#define HAWK_PCI_PSADD1_OFF			0x88
#define HAWK_PCI_PSOFF1_OFF			0x8c
#define HAWK_PCI_PSADD2_OFF			0x90
#define HAWK_PCI_PSOFF2_OFF			0x94
#define HAWK_PCI_PSADD3_OFF			0x98
#define HAWK_PCI_PSOFF3_OFF			0x9c

/*
 * Define the System Memory Controller (SMC) register offsets.
 */
#define HAWK_SMC_RAM_A_SIZE_REG_OFF		0x10
#define HAWK_SMC_RAM_B_SIZE_REG_OFF		0x11
#define HAWK_SMC_RAM_C_SIZE_REG_OFF		0x12
#define HAWK_SMC_RAM_D_SIZE_REG_OFF		0x13
#define HAWK_SMC_RAM_E_SIZE_REG_OFF		0xc0	/* HAWK Only */
#define HAWK_SMC_RAM_F_SIZE_REG_OFF		0xc1	/* HAWK Only */
#define HAWK_SMC_RAM_G_SIZE_REG_OFF		0xc2	/* HAWK Only */
#define HAWK_SMC_RAM_H_SIZE_REG_OFF		0xc3	/* HAWK Only */

#define FALCON_SMC_REG_COUNT			4
#define HAWK_SMC_REG_COUNT			8
#endif				/* __ASMPPC_HAWK_DEFS_H */
