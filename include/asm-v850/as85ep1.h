/*
 * include/asm-v850/as85ep1.h -- AS85EP1 evaluation CPU chip/board
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

#ifndef __V850_AS85EP1_H__
#define __V850_AS85EP1_H__

#include <asm/v850e.h>


#define CPU_MODEL	"as85ep1"
#define CPU_MODEL_LONG	"NEC V850E/AS85EP1"
#define PLATFORM	"AS85EP1"
#define PLATFORM_LONG	"NEC V850E/AS85EP1 evaluation board"

#define CPU_CLOCK_FREQ	96000000 /*  96MHz */
#define SYS_CLOCK_FREQ	CPU_CLOCK_FREQ


/* 1MB of static RAM.  */
#define SRAM_ADDR	0x00400000
#define SRAM_SIZE	0x00100000 /* 1MB */
/* About 58MB of DRAM.  This can actually be at one of two positions,
   determined by jump JP3; we have to use the first position because the
   second is partially out of processor instruction addressing range
   (though in the second position there's actually 64MB available).  */
#define SDRAM_ADDR	0x00600000
#define SDRAM_SIZE	0x039F8000 /* approx 58MB */

/* For <asm/page.h> */
#define PAGE_OFFSET 	SRAM_ADDR

/* We use on-chip RAM, for a few miscellaneous variables that must be
   accessible using a load instruction relative to R0.  The AS85EP1 chip
   16K of internal RAM located slightly before I/O space.  */
#define R0_RAM_ADDR	0xFFFF8000


/* AS85EP1 specific control registers.  */
#define AS85EP1_CSC_ADDR(n)	(0xFFFFF060 + (n) * 2)
#define AS85EP1_CSC(n)		(*(volatile u16 *)AS85EP1_CSC_ADDR(n))
#define AS85EP1_BSC_ADDR	0xFFFFF066
#define AS85EP1_BSC		(*(volatile u16 *)AS85EP1_BSC_ADDR)
#define AS85EP1_BCT_ADDR(n)	(0xFFFFF480 + (n) * 2)
#define AS85EP1_BCT(n)		(*(volatile u16 *)AS85EP1_BCT_ADDR(n))
#define AS85EP1_DWC_ADDR(n)	(0xFFFFF484 + (n) * 2)
#define AS85EP1_DWC(n)		(*(volatile u16 *)AS85EP1_DWC_ADDR(n))
#define AS85EP1_BCC_ADDR	0xFFFFF488
#define AS85EP1_BCC		(*(volatile u16 *)AS85EP1_BCC_ADDR)
#define AS85EP1_ASC_ADDR	0xFFFFF48A
#define AS85EP1_ASC		(*(volatile u16 *)AS85EP1_ASC_ADDR)
#define AS85EP1_BCP_ADDR	0xFFFFF48C
#define AS85EP1_BCP		(*(volatile u16 *)AS85EP1_BCP_ADDR)
#define AS85EP1_LBS_ADDR	0xFFFFF48E
#define AS85EP1_LBS		(*(volatile u16 *)AS85EP1_LBS_ADDR)
#define AS85EP1_BMC_ADDR	0xFFFFF498
#define AS85EP1_BMC		(*(volatile u16 *)AS85EP1_BMC_ADDR)
#define AS85EP1_PRC_ADDR	0xFFFFF49A
#define AS85EP1_PRC		(*(volatile u16 *)AS85EP1_PRC_ADDR)
#define AS85EP1_SCR_ADDR(n)	(0xFFFFF4A0 + (n) * 4)
#define AS85EP1_SCR(n)		(*(volatile u16 *)AS85EP1_SCR_ADDR(n))
#define AS85EP1_RFS_ADDR(n)	(0xFFFFF4A2 + (n) * 4)
#define AS85EP1_RFS(n)		(*(volatile u16 *)AS85EP1_RFS_ADDR(n))
#define AS85EP1_IRAMM_ADDR	0xFFFFF80A
#define AS85EP1_IRAMM		(*(volatile u8 *)AS85EP1_IRAMM_ADDR)



/* I/O port P0-P13. */
/* Direct I/O.  Bits 0-7 are pins Pn0-Pn7.  */
#define AS85EP1_PORT_IO_ADDR(n)	(0xFFFFF400 + (n) * 2)
#define AS85EP1_PORT_IO(n)	(*(volatile u8 *)AS85EP1_PORT_IO_ADDR(n))
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define AS85EP1_PORT_PM_ADDR(n)	(0xFFFFF420 + (n) * 2)
#define AS85EP1_PORT_PM(n)	(*(volatile u8 *)AS85EP1_PORT_PM_ADDR(n))
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define AS85EP1_PORT_PMC_ADDR(n) (0xFFFFF440 + (n) * 2)
#define AS85EP1_PORT_PMC(n)	(*(volatile u8 *)AS85EP1_PORT_PMC_ADDR(n))


/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
#define IRQ_INTCCC(n)	(0x0C + (n))
#define IRQ_INTCCC_NUM	8
#define IRQ_INTCMD(n)	(0x14 + (n)) /* interval timer interrupts 0-5 */
#define IRQ_INTCMD_NUM	6
#define IRQ_INTSRE(n)	(0x1E + (n)*3) /* UART 0-1 reception error */
#define IRQ_INTSRE_NUM	2
#define IRQ_INTSR(n)	(0x1F + (n)*3) /* UART 0-1 reception completion */
#define IRQ_INTSR_NUM	2
#define IRQ_INTST(n)	(0x20 + (n)*3) /* UART 0-1 transmission completion */
#define IRQ_INTST_NUM	2

#define NUM_CPU_IRQS	64

#ifndef __ASSEMBLY__
/* Initialize chip interrupts.  */
extern void as85ep1_init_irqs (void);
#endif


/* AS85EP1 UART details (basically the same as the V850E/MA1, but 2 channels).  */
#define V850E_UART_NUM_CHANNELS		2
#define V850E_UART_BASE_FREQ		(SYS_CLOCK_FREQ / 4)
#define V850E_UART_CHIP_NAME 		"V850E/NA85E"

/* This is a function that gets called before configuring the UART.  */
#define V850E_UART_PRE_CONFIGURE	as85ep1_uart_pre_configure
#ifndef __ASSEMBLY__
extern void as85ep1_uart_pre_configure (unsigned chan,
					unsigned cflags, unsigned baud);
#endif

/* This board supports RTS/CTS for the on-chip UART, but only for channel 1. */

/* CTS for UART channel 1 is pin P54 (bit 4 of port 5).  */
#define V850E_UART_CTS(chan)   ((chan) == 1 ? !(AS85EP1_PORT_IO(5) & 0x10) : 1)
/* RTS for UART channel 1 is pin P53 (bit 3 of port 5).  */
#define V850E_UART_SET_RTS(chan, val)					      \
   do {									      \
	   if (chan == 1) {						      \
		   unsigned old = AS85EP1_PORT_IO(5); 			      \
		   if (val)						      \
			   AS85EP1_PORT_IO(5) = old & ~0x8;		      \
		   else							      \
			   AS85EP1_PORT_IO(5) = old | 0x8;		      \
	   }								      \
   } while (0)


/* Timer C details.  */
#define V850E_TIMER_C_BASE_ADDR		0xFFFFF600

/* Timer D details (the AS85EP1 actually has 5 of these; should change later). */
#define V850E_TIMER_D_BASE_ADDR		0xFFFFF540
#define V850E_TIMER_D_TMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x0)
#define V850E_TIMER_D_CMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x2)
#define V850E_TIMER_D_TMCD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x4)

#define V850E_TIMER_D_BASE_FREQ		SYS_CLOCK_FREQ
#define V850E_TIMER_D_TMCD_CS_MIN	2 /* min 2^2 divider */


/* For <asm/param.h> */
#ifndef HZ
#define HZ	100
#endif


#endif /* __V850_AS85EP1_H__ */
