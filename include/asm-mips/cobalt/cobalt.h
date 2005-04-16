/*
 * Lowlevel hardware stuff for the MIPS based Cobalt microservers.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1997 Cobalt Microserver
 * Copyright (C) 1997, 2003 Ralf Baechle
 * Copyright (C) 2001, 2002, 2003 Liam Davies (ldavies@agile.tv)
 */
#ifndef __ASM_COBALT_H
#define __ASM_COBALT_H

/*
 * i8259 legacy interrupts used on Cobalt:
 *
 *     8  - RTC
 *     9  - PCI
 *    14  - IDE0
 *    15  - IDE1
 *
 * CPU IRQs  are 16 ... 23
 */
#define COBALT_TIMER_IRQ	18
#define COBALT_SCC_IRQ          19		/* pre-production has 85C30 */
#define COBALT_RAQ_SCSI_IRQ	19
#define COBALT_ETH0_IRQ		19
#define COBALT_ETH1_IRQ		20
#define COBALT_SERIAL_IRQ	21
#define COBALT_SCSI_IRQ         21
#define COBALT_VIA_IRQ		22		/* Chained to VIA ISA bridge */
#define COBALT_QUBE_SLOT_IRQ	23

/*
 * PCI configuration space manifest constants.  These are wired into
 * the board layout according to the PCI spec to enable the software
 * to probe the hardware configuration space in a well defined manner.
 *
 * The PCI_DEVSHFT() macro transforms these values into numbers
 * suitable for passing as the dev parameter to the various
 * pcibios_read/write_config routines.
 */
#define COBALT_PCICONF_CPU      0x06
#define COBALT_PCICONF_ETH0     0x07
#define COBALT_PCICONF_RAQSCSI  0x08
#define COBALT_PCICONF_VIA      0x09
#define COBALT_PCICONF_PCISLOT  0x0A
#define COBALT_PCICONF_ETH1     0x0C


/*
 * The Cobalt board id information.  The boards have an ID number wired
 * into the VIA that is available in the high nibble of register 94.
 * This register is available in the VIA configuration space through the
 * interface routines qube_pcibios_read/write_config. See cobalt/pci.c
 */
#define VIA_COBALT_BRD_ID_REG  0x94
#define VIA_COBALT_BRD_REG_to_ID(reg)  ((unsigned char) (reg) >> 4)
#define COBALT_BRD_ID_QUBE1    0x3
#define COBALT_BRD_ID_RAQ1     0x4
#define COBALT_BRD_ID_QUBE2    0x5
#define COBALT_BRD_ID_RAQ2     0x6

/*
 * Galileo chipset access macros for the Cobalt. The base address for
 * the GT64111 chip is 0x14000000
 *
 * Most of this really should go into a separate GT64111 header file.
 */
#define GT64111_IO_BASE		0x10000000UL
#define GT64111_BASE		0x14000000UL
#define GALILEO_REG(ofs)	(KSEG0 + GT64111_BASE + (unsigned long)(ofs))

#define GALILEO_INL(port)	(*(volatile unsigned int *) GALILEO_REG(port))
#define GALILEO_OUTL(val, port)						\
do {									\
	*(volatile unsigned int *) GALILEO_REG(port) = (port);		\
} while (0)

#define GALILEO_T0EXP		0x0100
#define GALILEO_ENTC0		0x01
#define GALILEO_SELTC0		0x02

#define PCI_CFG_SET(devfn,where)					\
	GALILEO_OUTL((0x80000000 | (PCI_SLOT (devfn) << 11) |		\
		(PCI_FUNC (devfn) << 8) | (where)), GT_PCI0_CFGADDR_OFS)


#endif /* __ASM_COBALT_H */
