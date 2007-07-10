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

#include <irq.h>

/*
 * i8259 legacy interrupts used on Cobalt:
 *
 *     8  - RTC
 *     9  - PCI
 *    14  - IDE0
 *    15  - IDE1
 */
#define COBALT_QUBE_SLOT_IRQ	9

/*
 * CPU IRQs  are 16 ... 23
 */
#define COBALT_CPU_IRQ		MIPS_CPU_IRQ_BASE

#define COBALT_GALILEO_IRQ	(COBALT_CPU_IRQ + 2)
#define COBALT_RAQ_SCSI_IRQ	(COBALT_CPU_IRQ + 3)
#define COBALT_ETH0_IRQ		(COBALT_CPU_IRQ + 3)
#define COBALT_QUBE1_ETH0_IRQ	(COBALT_CPU_IRQ + 4)
#define COBALT_ETH1_IRQ		(COBALT_CPU_IRQ + 4)
#define COBALT_SERIAL_IRQ	(COBALT_CPU_IRQ + 5)
#define COBALT_SCSI_IRQ         (COBALT_CPU_IRQ + 5)
#define COBALT_VIA_IRQ		(COBALT_CPU_IRQ + 6)	/* Chained to VIA ISA bridge */

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

extern int cobalt_board_id;

#define COBALT_LED_PORT		(*(volatile unsigned char *) CKSEG1ADDR(0x1c000000))
# define COBALT_LED_BAR_LEFT	(1 << 0)	/* Qube */
# define COBALT_LED_BAR_RIGHT	(1 << 1)	/* Qube */
# define COBALT_LED_WEB		(1 << 2)	/* RaQ */
# define COBALT_LED_POWER_OFF	(1 << 3)	/* RaQ */
# define COBALT_LED_RESET	0x0f

#define COBALT_KEY_PORT		((~*(volatile unsigned int *) CKSEG1ADDR(0x1d000000) >> 24) & COBALT_KEY_MASK)
# define COBALT_KEY_CLEAR	(1 << 1)
# define COBALT_KEY_LEFT	(1 << 2)
# define COBALT_KEY_UP		(1 << 3)
# define COBALT_KEY_DOWN	(1 << 4)
# define COBALT_KEY_RIGHT	(1 << 5)
# define COBALT_KEY_ENTER	(1 << 6)
# define COBALT_KEY_SELECT	(1 << 7)
# define COBALT_KEY_MASK	0xfe

#define COBALT_UART		((volatile unsigned char *) CKSEG1ADDR(0x1c800000))

#endif /* __ASM_COBALT_H */
