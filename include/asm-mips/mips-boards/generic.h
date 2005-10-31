/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Defines of the MIPS boards specific address-MAP, registers, etc.
 */
#ifndef __ASM_MIPS_BOARDS_GENERIC_H
#define __ASM_MIPS_BOARDS_GENERIC_H

#include <linux/config.h>
#include <asm/addrspace.h>
#include <asm/byteorder.h>
#include <asm/mips-boards/bonito64.h>

/*
 * Display register base.
 */
#ifdef CONFIG_MIPS_SEAD
#define ASCII_DISPLAY_POS_BASE     0x1f0005c0
#else
#define ASCII_DISPLAY_WORD_BASE    0x1f000410
#define ASCII_DISPLAY_POS_BASE     0x1f000418
#endif


/*
 * Yamon Prom print address.
 */
#define YAMON_PROM_PRINT_ADDR      0x1fc00504


/*
 * Reset register.
 */
#ifdef CONFIG_MIPS_SEAD
#define SOFTRES_REG       0x1e800050
#define GORESET           0x4d
#else
#define SOFTRES_REG       0x1f000500
#define GORESET           0x42
#endif

/*
 * Revision register.
 */
#define MIPS_REVISION_REG                  0x1fc00010
#define MIPS_REVISION_CORID_QED_RM5261     0
#define MIPS_REVISION_CORID_CORE_LV        1
#define MIPS_REVISION_CORID_BONITO64       2
#define MIPS_REVISION_CORID_CORE_20K       3
#define MIPS_REVISION_CORID_CORE_FPGA      4
#define MIPS_REVISION_CORID_CORE_MSC       5
#define MIPS_REVISION_CORID_CORE_EMUL      6
#define MIPS_REVISION_CORID_CORE_FPGA2     7
#define MIPS_REVISION_CORID_CORE_FPGAR2    8
#define MIPS_REVISION_CORID_CORE_FPGA3     9

/**** Artificial corid defines ****/
/*
 *  CoreEMUL with   Bonito   System Controller is treated like a Core20K
 *  CoreEMUL with SOC-it 101 System Controller is treated like a CoreMSC
 */
#define MIPS_REVISION_CORID_CORE_EMUL_BON  0x63
#define MIPS_REVISION_CORID_CORE_EMUL_MSC  0x65

#define MIPS_REVISION_CORID (((*(volatile u32 *)ioremap(MIPS_REVISION_REG, 4)) >> 10) & 0x3f)

extern unsigned int mips_revision_corid;

#ifdef CONFIG_PCI
extern void mips_pcibios_init(void);
#else
#define mips_pcibios_init() do { } while (0)
#endif

#endif  /* __ASM_MIPS_BOARDS_GENERIC_H */
