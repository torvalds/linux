/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 */
#ifndef __ASM_MIPS_MARVELL_H
#define __ASM_MIPS_MARVELL_H

#include <linux/pci.h>

#include <asm/byteorder.h>

extern unsigned long marvell_base;

/*
 * Because of an error/peculiarity in the Galileo chip, we need to swap the
 * bytes when running bigendian.
 */
#define __MV_READ(ofs)							\
	(*(volatile u32 *)(marvell_base+(ofs)))
#define __MV_WRITE(ofs, data)						\
	do { *(volatile u32 *)(marvell_base+(ofs)) = (data); } while (0)

#define MV_READ(ofs)		le32_to_cpu(__MV_READ(ofs))
#define MV_WRITE(ofs, data)	__MV_WRITE(ofs, cpu_to_le32(data))

#define MV_READ_16(ofs)							\
        le16_to_cpu(*(volatile u16 *)(marvell_base+(ofs)))
#define MV_WRITE_16(ofs, data)  \
        *(volatile u16 *)(marvell_base+(ofs)) = cpu_to_le16(data)

#define MV_READ_8(ofs)							\
	*(volatile u8 *)(marvell_base+(ofs))
#define MV_WRITE_8(ofs, data)						\
	*(volatile u8 *)(marvell_base+(ofs)) = data

#define MV_SET_REG_BITS(ofs, bits)					\
	(*((volatile u32 *)(marvell_base + (ofs)))) |= ((u32)cpu_to_le32(bits))
#define MV_RESET_REG_BITS(ofs, bits)					\
	(*((volatile u32 *)(marvell_base + (ofs)))) &= ~((u32)cpu_to_le32(bits))

extern struct pci_ops mv_pci_ops;

struct mv_pci_controller {
	struct pci_controller   pcic;

	/*
	 * GT-64240/MV-64340 specific, per host bus information
	 */
	unsigned long   config_addr;
	unsigned long   config_vreg;
};

extern void ll_mv64340_irq(struct pt_regs *regs);

#endif	/* __ASM_MIPS_MARVELL_H */
