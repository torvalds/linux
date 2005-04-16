/*
 * include/asm-v850/rte_me2_cb.h -- Midas labs RTE-V850E/ME2-CB board
 *
 *  Copyright (C) 2001,02,03  NEC Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_RTE_ME2_CB_H__
#define __V850_RTE_ME2_CB_H__

#include <asm/rte_cb.h>		/* Common defs for Midas RTE-CB boards.  */


#define PLATFORM		"rte-v850e/me2-cb"
#define PLATFORM_LONG		"Midas lab RTE-V850E/ME2-CB"

#define CPU_CLOCK_FREQ		150000000 /* 150MHz */
#define FIXED_BOGOMIPS		50

/* 32MB of onbard SDRAM.  */
#define SDRAM_ADDR		0x00800000
#define SDRAM_SIZE		0x02000000 /* 32MB */


/* CPU addresses of GBUS memory spaces.  */
#define GCS0_ADDR		0x04000000 /* GCS0 - Common SRAM (2MB) */
#define GCS0_SIZE		0x00800000 /*   8MB */
#define GCS1_ADDR		0x04800000 /* GCS1 - Flash ROM (8MB) */
#define GCS1_SIZE		0x00800000 /*   8MB */
#define GCS2_ADDR		0x07000000 /* GCS2 - I/O registers */
#define GCS2_SIZE		0x00800000 /*   8MB */
#define GCS5_ADDR		0x08000000 /* GCS5 - PCI bus space */
#define GCS5_SIZE		0x02000000 /*   32MB */
#define GCS6_ADDR		0x07800000 /* GCS6 - PCI control registers */
#define GCS6_SIZE		0x00800000 /*   8MB */


/* For <asm/page.h> */
#define PAGE_OFFSET 		SDRAM_ADDR


#ifdef CONFIG_ROM_KERNEL
/* Kernel is in ROM, starting at address 0.  */

#define INTV_BASE		0
#define ROOT_FS_IMAGE_RW	0

#else /* !CONFIG_ROM_KERNEL */
/* Using RAM-kernel.  Assume some sort of boot-loader got us loaded at
   address 0.  */

#define INTV_BASE		0
#define ROOT_FS_IMAGE_RW	1

#endif /* CONFIG_ROM_KERNEL */


/* Some misc. on-board devices.  */

/* Seven-segment LED display (four digits).  */
#define LED_ADDR(n)		(0x0FE02000 + (n))
#define LED(n)			(*(volatile unsigned char *)LED_ADDR(n))
#define LED_NUM_DIGITS		4


/* On-board PIC.  */

#define CB_PIC_BASE_ADDR 	0x0FE04000

#define CB_PIC_INT0M_ADDR 	(CB_PIC_BASE_ADDR + 0x00)
#define CB_PIC_INT0M      	(*(volatile u16 *)CB_PIC_INT0M_ADDR)
#define CB_PIC_INT1M_ADDR 	(CB_PIC_BASE_ADDR + 0x10)
#define CB_PIC_INT1M      	(*(volatile u16 *)CB_PIC_INT1M_ADDR)
#define CB_PIC_INTR_ADDR  	(CB_PIC_BASE_ADDR + 0x20)
#define CB_PIC_INTR       	(*(volatile u16 *)CB_PIC_INTR_ADDR)
#define CB_PIC_INTEN_ADDR 	(CB_PIC_BASE_ADDR + 0x30)
#define CB_PIC_INTEN      	(*(volatile u16 *)CB_PIC_INTEN_ADDR)

#define CB_PIC_INT0EN        	0x0001
#define CB_PIC_INT1EN        	0x0002
#define CB_PIC_INT0SEL       	0x0080

/* The PIC interrupts themselves.  */
#define CB_PIC_BASE_IRQ		NUM_CPU_IRQS
#define IRQ_CB_PIC_NUM		10

/* Some specific CB_PIC interrupts. */
#define IRQ_CB_EXTTM0		(CB_PIC_BASE_IRQ + 0)
#define IRQ_CB_EXTSIO		(CB_PIC_BASE_IRQ + 1)
#define IRQ_CB_TOVER		(CB_PIC_BASE_IRQ + 2)
#define IRQ_CB_GINT0		(CB_PIC_BASE_IRQ + 3)
#define IRQ_CB_USB		(CB_PIC_BASE_IRQ + 4)
#define IRQ_CB_LANC		(CB_PIC_BASE_IRQ + 5)
#define IRQ_CB_USB_VBUS_ON	(CB_PIC_BASE_IRQ + 6)
#define IRQ_CB_USB_VBUS_OFF	(CB_PIC_BASE_IRQ + 7)
#define IRQ_CB_EXTTM1		(CB_PIC_BASE_IRQ + 8)
#define IRQ_CB_EXTTM2		(CB_PIC_BASE_IRQ + 9)

/* The GBUS GINT1 - GINT3 (note, not GINT0!) interrupts are connected to
   the INTP65 - INTP67 pins on the CPU.  These are shared among the GBUS
   interrupts.  */
#define IRQ_GINT(n)		IRQ_INTP((n) + 9)  /* 0 is unused! */
#define IRQ_GINT_NUM		4		   /* 0 is unused! */

/* The shared interrupt line from the PIC is connected to CPU pin INTP23.  */
#define IRQ_CB_PIC		IRQ_INTP(4) /* P23 */

/* Used by <asm/rte_cb.h> to derive NUM_MACH_IRQS.  */
#define NUM_RTE_CB_IRQS		(NUM_CPU_IRQS + IRQ_CB_PIC_NUM)


#ifndef __ASSEMBLY__
struct cb_pic_irq_init {
	const char *name;	/* name of interrupt type */

	/* Range of kernel irq numbers for this type:
	   BASE, BASE+INTERVAL, ..., BASE+INTERVAL*NUM  */
	unsigned base, num, interval;

	unsigned priority;	/* interrupt priority to assign */
};
struct hw_interrupt_type;	/* fwd decl */

/* Enable interrupt handling for interrupt IRQ.  */
extern void cb_pic_enable_irq (unsigned irq);
/* Disable interrupt handling for interrupt IRQ.  Note that any interrupts
   received while disabled will be delivered once the interrupt is enabled
   again, unless they are explicitly cleared using `cb_pic_clear_pending_irq'.  */
extern void cb_pic_disable_irq (unsigned irq);
/* Initialize HW_IRQ_TYPES for PIC irqs described in array INITS (which is
   terminated by an entry with the name field == 0).  */
extern void cb_pic_init_irq_types (struct cb_pic_irq_init *inits,
				   struct hw_interrupt_type *hw_irq_types);
/* Initialize PIC interrupts.  */
extern void cb_pic_init_irqs (void);
#endif /* __ASSEMBLY__ */


/* TL16C550C on board UART see also asm/serial.h */
#define CB_UART_BASE    	0x0FE08000
#define CB_UART_REG_GAP 	0x10
#define CB_UART_CLOCK   	0x16000000

/* CompactFlash setting */
#define CB_CF_BASE     		0x0FE0C000
#define CB_CF_CCR_ADDR 		(CB_CF_BASE+0x200)
#define CB_CF_CCR      		(*(volatile u8 *)CB_CF_CCR_ADDR)
#define CB_CF_REG0_ADDR		(CB_CF_BASE+0x1000)
#define CB_CF_REG0     		(*(volatile u16 *)CB_CF_REG0_ADDR)
#define CB_CF_STS0_ADDR		(CB_CF_BASE+0x1004)
#define CB_CF_STS0     		(*(volatile u16 *)CB_CF_STS0_ADDR)
#define CB_PCATA_BASE  		(CB_CF_BASE+0x800)
#define CB_IDE_BASE    		(CB_CF_BASE+0x9F0)
#define CB_IDE_CTRL    		(CB_CF_BASE+0xBF6)
#define CB_IDE_REG_OFFS		0x1


/* SMSC LAN91C111 setting */
#if defined(CONFIG_SMC91111)
#define CB_LANC_BASE 		0x0FE10300
#define CONFIG_SMC16BITONLY
#define ETH0_ADDR 		CB_LANC_BASE
#define ETH0_IRQ 		IRQ_CB_LANC
#endif /* CONFIG_SMC16BITONLY */


#undef V850E_UART_PRE_CONFIGURE
#define V850E_UART_PRE_CONFIGURE	rte_me2_cb_uart_pre_configure
#ifndef __ASSEMBLY__
extern void rte_me2_cb_uart_pre_configure (unsigned chan,
					   unsigned cflags, unsigned baud);
#endif /* __ASSEMBLY__ */

/* This board supports RTS/CTS for the on-chip UART, but only for channel 0. */

/* CTS for UART channel 0 is pin P22 (bit 2 of port 2).  */
#define V850E_UART_CTS(chan)	((chan) == 0 ? !(ME2_PORT2_IO & 0x4) : 1)
/* RTS for UART channel 0 is pin P21 (bit 1 of port 2).  */
#define V850E_UART_SET_RTS(chan, val)					      \
   do {									      \
	   if (chan == 0) {						      \
		   unsigned old = ME2_PORT2_IO; 			      \
		   if (val)						      \
			   ME2_PORT2_IO = old & ~0x2;			      \
		   else							      \
			   ME2_PORT2_IO = old | 0x2;			      \
	   }								      \
   } while (0)


#ifndef __ASSEMBLY__
extern void rte_me2_cb_init_irqs (void);
#endif /* !__ASSEMBLY__ */


#endif /* __V850_RTE_ME2_CB_H__ */
