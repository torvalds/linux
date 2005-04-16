/*
 * include/asm-v850/teg.h -- NB85E-TEG cpu chip
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

#ifndef __V850_TEG_H__
#define __V850_TEG_H__


/* The TEG uses the V850E cpu core.  */
#include <asm/v850e.h>
#include <asm/v850e_cache.h>


#define CPU_MODEL	"v850e/nb85e-teg"
#define CPU_MODEL_LONG	"NEC V850E/NB85E TEG"


/* For <asm/entry.h> */
/* We use on-chip RAM, for a few miscellaneous variables that must be
   accessible using a load instruction relative to R0.  On the NB85E/TEG,
   There's 60KB of iRAM starting at 0xFFFF0000, however we need the base
   address to be addressable by a 16-bit signed offset, so we only use the
   second half of it starting from 0xFFFF8000.  */
#define R0_RAM_ADDR			0xFFFF8000


/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).
   Some of these are parameterized even though there's only a single
   interrupt, for compatibility with some generic code that works on other
   processor models.  */
#define IRQ_INTCMD(n)	6	/* interval timer interrupt */
#define IRQ_INTCMD_NUM	1
#define IRQ_INTSER(n)	16	/* UART reception error */
#define IRQ_INTSER_NUM	1
#define IRQ_INTSR(n)	17	/* UART reception completion */
#define IRQ_INTSR_NUM	1
#define IRQ_INTST(n)	18	/* UART transmission completion */
#define IRQ_INTST_NUM	1

/* For <asm/irq.h> */
#define NUM_CPU_IRQS	64


/* TEG UART details.  */
#define V850E_UART_BASE_ADDR(n)		(0xFFFFF600 + 0x10 * (n))
#define V850E_UART_ASIM_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x0)
#define V850E_UART_ASIS_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x2)
#define V850E_UART_ASIF_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x4)
#define V850E_UART_CKSR_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x6)
#define V850E_UART_BRGC_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0x8)
#define V850E_UART_TXB_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0xA)
#define V850E_UART_RXB_ADDR(n)		(V850E_UART_BASE_ADDR(n) + 0xC)
#define V850E_UART_NUM_CHANNELS		1
#define V850E_UART_BASE_FREQ		CPU_CLOCK_FREQ
/* This is a function that gets called before configuring the UART.  */
#define V850E_UART_PRE_CONFIGURE	teg_uart_pre_configure
#ifndef __ASSEMBLY__
extern void teg_uart_pre_configure (unsigned chan,
				    unsigned cflags, unsigned baud);
#endif


/* The TEG RTPU.  */
#define V850E_RTPU_BASE_ADDR		0xFFFFF210


/* TEG series timer D details.  */
#define V850E_TIMER_D_BASE_ADDR		0xFFFFF210
#define V850E_TIMER_D_TMCD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x0)
#define V850E_TIMER_D_TMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x4)
#define V850E_TIMER_D_CMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x8)
#define V850E_TIMER_D_BASE_FREQ		CPU_CLOCK_FREQ


/* `Interrupt Source Select' control register.  */
#define TEG_ISS_ADDR			0xFFFFF7FA
#define TEG_ISS				(*(volatile u8 *)TEG_ISS_ADDR)

/* Port 0 I/O register (bits 0-3 used).  */
#define TEG_PORT0_IO_ADDR		0xFFFFF7F2
#define TEG_PORT0_IO			(*(volatile u8 *)TEG_PORT0_IO_ADDR)
/* Port 0 control register (bits 0-3 control mode, 0 = output, 1 = input).  */
#define TEG_PORT0_PM_ADDR		0xFFFFF7F4
#define TEG_PORT0_PM			(*(volatile u8 *)TEG_PORT0_PM_ADDR)


#ifndef __ASSEMBLY__
extern void teg_init_irqs (void);
#endif


#endif /* __V850_TEG_H__ */
