/*
 * include/asm-v850/mb_a_pci.h -- PCI support for Midas lab RTE-MOTHER-A board
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_MB_A_PCI_H__
#define __V850_MB_A_PCI_H__


#define MB_A_PCI_MEM_ADDR	GCS5_ADDR
#define MB_A_PCI_MEM_SIZE	(GCS5_SIZE / 2)
#define MB_A_PCI_IO_ADDR	(GCS5_ADDR + MB_A_PCI_MEM_SIZE)
#define MB_A_PCI_IO_SIZE	(GCS5_SIZE / 2)
#define MB_A_PCI_REG_BASE_ADDR	GCS6_ADDR

#define MB_A_PCI_PCICR_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x4)
#define MB_A_PCI_PCICR		(*(volatile u16 *)MB_A_PCI_PCICR_ADDR)
#define MB_A_PCI_PCISR_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x6)
#define MB_A_PCI_PCISR		(*(volatile u16 *)MB_A_PCI_PCISR_ADDR)
#define MB_A_PCI_PCILTR_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0xD)
#define MB_A_PCI_PCILTR		(*(volatile u8 *)MB_A_PCI_PCILTR_ADDR)
#define MB_A_PCI_PCIBAR0_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x10)
#define MB_A_PCI_PCIBAR0	(*(volatile u32 *)MB_A_PCI_PCIBAR0_ADDR)
#define MB_A_PCI_PCIBAR1_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x14)
#define MB_A_PCI_PCIBAR1	(*(volatile u32 *)MB_A_PCI_PCIBAR1_ADDR)
#define MB_A_PCI_PCIBAR2_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x18)
#define MB_A_PCI_PCIBAR2	(*(volatile u32 *)MB_A_PCI_PCIBAR2_ADDR)
#define MB_A_PCI_VENDOR_ID_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x2C)
#define MB_A_PCI_VENDOR_ID	(*(volatile u16 *)MB_A_PCI_VENDOR_ID_ADDR)
#define MB_A_PCI_DEVICE_ID_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x2E)
#define MB_A_PCI_DEVICE_ID	(*(volatile u16 *)MB_A_PCI_DEVICE_ID_ADDR)
#define MB_A_PCI_DMRR_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0x9C)
#define MB_A_PCI_DMRR		(*(volatile u32 *)MB_A_PCI_DMRR_ADDR)
#define MB_A_PCI_DMLBAM_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0xA0)
#define MB_A_PCI_DMLBAM		(*(volatile u32 *)MB_A_PCI_DMLBAM_ADDR)
#define MB_A_PCI_DMLBAI_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0xA4)
#define MB_A_PCI_DMLBAI		(*(volatile u32 *)MB_A_PCI_DMLBAI_ADDR)
#define MB_A_PCI_PCIPBAM_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0xA8)
#define MB_A_PCI_PCIPBAM	(*(volatile u32 *)MB_A_PCI_PCIPBAM_ADDR)
/* `PCI Configuration Address Register for Direct Master to PCI IO/CFG'  */
#define MB_A_PCI_DMCFGA_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0xAC)
#define MB_A_PCI_DMCFGA		(*(volatile u32 *)MB_A_PCI_DMCFGA_ADDR)
/* `PCI Permanent Configuration ID Register'  */
#define MB_A_PCI_PCIHIDR_ADDR	(MB_A_PCI_REG_BASE_ADDR + 0xF0)
#define MB_A_PCI_PCIHIDR	(*(volatile u32 *)MB_A_PCI_PCIHIDR_ADDR)


#endif /* __V850_MB_A_PCI_H__ */
