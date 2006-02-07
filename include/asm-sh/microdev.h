/*
 * linux/include/asm-sh/microdev.h
 *
 * Copyright (C) 2003 Sean McGoogan (Sean.McGoogan@superh.com)
 *
 * Definitions for the SuperH SH4-202 MicroDev board.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */
#ifndef __ASM_SH_MICRODEV_H
#define __ASM_SH_MICRODEV_H

extern void init_microdev_irq(void);
extern void microdev_print_fpga_intc_status(void);

/*
 * The following are useful macros for manipulating the interrupt
 * controller (INTC) on the CPU-board FPGA.  should be noted that there
 * is an INTC on the FPGA, and a seperate INTC on the SH4-202 core -
 * these are two different things, both of which need to be prorammed to
 * correctly route - unfortunately, they have the same name and
 * abbreviations!
 */
#define	MICRODEV_FPGA_INTC_BASE		0xa6110000ul				/* INTC base address on CPU-board FPGA */
#define	MICRODEV_FPGA_INTENB_REG	(MICRODEV_FPGA_INTC_BASE+0ul)		/* Interrupt Enable Register on INTC on CPU-board FPGA */
#define	MICRODEV_FPGA_INTDSB_REG	(MICRODEV_FPGA_INTC_BASE+8ul)		/* Interrupt Disable Register on INTC on CPU-board FPGA */
#define	MICRODEV_FPGA_INTC_MASK(n)	(1ul<<(n))				/* Interupt mask to enable/disable INTC in CPU-board FPGA */
#define	MICRODEV_FPGA_INTPRI_REG(n)	(MICRODEV_FPGA_INTC_BASE+0x10+((n)/8)*8)/* Interrupt Priority Register on INTC on CPU-board FPGA */
#define	MICRODEV_FPGA_INTPRI_LEVEL(n,x)	((x)<<(((n)%8)*4))			/* MICRODEV_FPGA_INTPRI_LEVEL(int_number, int_level) */
#define	MICRODEV_FPGA_INTPRI_MASK(n)	(MICRODEV_FPGA_INTPRI_LEVEL((n),0xful))	/* Interrupt Priority Mask on INTC on CPU-board FPGA */
#define	MICRODEV_FPGA_INTSRC_REG	(MICRODEV_FPGA_INTC_BASE+0x30ul)	/* Interrupt Source Register on INTC on CPU-board FPGA */
#define	MICRODEV_FPGA_INTREQ_REG	(MICRODEV_FPGA_INTC_BASE+0x38ul)	/* Interrupt Request Register on INTC on CPU-board FPGA */


/*
 * The following are the IRQ numbers for the Linux Kernel for external
 * interrupts.  i.e. the numbers seen by 'cat /proc/interrupt'.
 */
#define MICRODEV_LINUX_IRQ_KEYBOARD	 1	/* SuperIO Keyboard */
#define MICRODEV_LINUX_IRQ_SERIAL1	 2	/* SuperIO Serial #1 */
#define MICRODEV_LINUX_IRQ_ETHERNET	 3	/* on-board Ethnernet */
#define MICRODEV_LINUX_IRQ_SERIAL2	 4	/* SuperIO Serial #2 */
#define MICRODEV_LINUX_IRQ_USB_HC	 7	/* on-board USB HC */
#define MICRODEV_LINUX_IRQ_MOUSE	12	/* SuperIO PS/2 Mouse */
#define MICRODEV_LINUX_IRQ_IDE2		13	/* SuperIO IDE #2 */
#define MICRODEV_LINUX_IRQ_IDE1		14	/* SuperIO IDE #1 */

/*
 * The following are the IRQ numbers for the INTC on the FPGA for
 * external interrupts.  i.e. the bits in the INTC registers in the
 * FPGA.
 */
#define MICRODEV_FPGA_IRQ_KEYBOARD	 1	/* SuperIO Keyboard */
#define MICRODEV_FPGA_IRQ_SERIAL1	 3	/* SuperIO Serial #1 */
#define MICRODEV_FPGA_IRQ_SERIAL2	 4	/* SuperIO Serial #2 */
#define MICRODEV_FPGA_IRQ_MOUSE		12	/* SuperIO PS/2 Mouse */
#define MICRODEV_FPGA_IRQ_IDE1		14	/* SuperIO IDE #1 */
#define MICRODEV_FPGA_IRQ_IDE2		15	/* SuperIO IDE #2 */
#define MICRODEV_FPGA_IRQ_USB_HC	16	/* on-board USB HC */
#define MICRODEV_FPGA_IRQ_ETHERNET	18	/* on-board Ethnernet */

#define MICRODEV_IRQ_PCI_INTA		 8
#define MICRODEV_IRQ_PCI_INTB		 9
#define MICRODEV_IRQ_PCI_INTC		10
#define MICRODEV_IRQ_PCI_INTD		11

#define __IO_PREFIX microdev
#include <asm/io_generic.h>

#if defined(CONFIG_PCI)
unsigned char  microdev_pci_inb(unsigned long port);
unsigned short microdev_pci_inw(unsigned long port);
unsigned long  microdev_pci_inl(unsigned long port);
void           microdev_pci_outb(unsigned char  data, unsigned long port);
void           microdev_pci_outw(unsigned short data, unsigned long port);
void           microdev_pci_outl(unsigned long  data, unsigned long port);
#endif

#endif /* __ASM_SH_MICRODEV_H */
