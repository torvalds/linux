/*
 * include/asm-v850/rte_cb.h -- Midas labs RTE-CB series of evaluation boards
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_RTE_CB_H__
#define __V850_RTE_CB_H__


/* The SRAM on the Mother-A motherboard.  */
#define MB_A_SRAM_ADDR		GCS0_ADDR
#define MB_A_SRAM_SIZE		0x00200000 /* 2MB */


#ifdef CONFIG_RTE_GBUS_INT
/* GBUS interrupt support.  */

# include <asm/gbus_int.h>

# define GBUS_INT_BASE_IRQ	NUM_RTE_CB_IRQS
# define GBUS_INT_BASE_ADDR	(GCS2_ADDR + 0x00006000)

/* Some specific interrupts.  */
# define IRQ_MB_A_LAN		IRQ_GBUS_INT(10)
# define IRQ_MB_A_PCI1(n)	(IRQ_GBUS_INT(16) + (n))
# define IRQ_MB_A_PCI1_NUM	4
# define IRQ_MB_A_PCI2(n)	(IRQ_GBUS_INT(20) + (n))
# define IRQ_MB_A_PCI2_NUM	4
# define IRQ_MB_A_EXT(n)	(IRQ_GBUS_INT(24) + (n))
# define IRQ_MB_A_EXT_NUM	4
# define IRQ_MB_A_USB_OC(n)	(IRQ_GBUS_INT(28) + (n))
# define IRQ_MB_A_USB_OC_NUM	2
# define IRQ_MB_A_PCMCIA_OC	IRQ_GBUS_INT(30)

/* We define NUM_MACH_IRQS to include extra interrupts from the GBUS.  */
# define NUM_MACH_IRQS		(NUM_RTE_CB_IRQS + IRQ_GBUS_INT_NUM)

#else /* !CONFIG_RTE_GBUS_INT */

# define NUM_MACH_IRQS		NUM_RTE_CB_IRQS

#endif /* CONFIG_RTE_GBUS_INT */


#ifdef CONFIG_RTE_MB_A_PCI
/* Mother-A PCI bus support.  */

# include <asm/rte_mb_a_pci.h>

/* These are the base addresses used for allocating device address
   space.  512K of the motherboard SRAM is in the same space, so we have
   to be careful not to let it be allocated.  */
# define PCIBIOS_MIN_MEM	(MB_A_PCI_MEM_ADDR + 0x80000)
# define PCIBIOS_MIN_IO		MB_A_PCI_IO_ADDR

/* As we don't really support PCI DMA to cpu memory, and use bounce-buffers
   instead, perversely enough, this becomes always true! */
# define pci_dma_supported(dev, mask)		1
# define pcibios_assign_all_busses()		1

#endif /* CONFIG_RTE_MB_A_PCI */


/* For <asm/param.h> */
#ifndef HZ
#define HZ	100
#endif


#ifndef __ASSEMBLY__
extern void rte_cb_early_init (void);
extern void rte_cb_init_irqs (void);
#endif /* !__ASSEMBLY__ */


#endif /* __V850_RTE_CB_H__ */
