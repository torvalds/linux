/*
 * include/asm-v850/ma.h -- V850E/MA series of cpu chips
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

#ifndef __V850_MA_H__
#define __V850_MA_H__

/* The MA series uses the V850E cpu core.  */
#include <asm/v850e.h>


/* For <asm/entry.h> */
/* We use on-chip RAM, for a few miscellaneous variables that must be
   accessible using a load instruction relative to R0.  The amount
   varies between chip models, but there's always at least 4K, and it
   should always start at FFFFC000.  */
#define R0_RAM_ADDR			0xFFFFC000


/* MA series UART details.  */
#define V850E_UART_BASE_FREQ		CPU_CLOCK_FREQ

/* This is a function that gets called before configuring the UART.  */
#define V850E_UART_PRE_CONFIGURE	ma_uart_pre_configure
#ifndef __ASSEMBLY__
extern void ma_uart_pre_configure (unsigned chan,
				   unsigned cflags, unsigned baud);
#endif


/* MA series timer C details.  */
#define V850E_TIMER_C_BASE_ADDR		0xFFFFF600


/* MA series timer D details.  */
#define V850E_TIMER_D_BASE_ADDR		0xFFFFF540
#define V850E_TIMER_D_TMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x0)
#define V850E_TIMER_D_CMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x2)
#define V850E_TIMER_D_TMCD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x4)

#define V850E_TIMER_D_BASE_FREQ		CPU_CLOCK_FREQ


/* Port 0 */
/* Direct I/O.  Bits 0-7 are pins P00-P07.  */
#define MA_PORT0_IO_ADDR		0xFFFFF400
#define MA_PORT0_IO			(*(volatile u8 *)MA_PORT0_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define MA_PORT0_PM_ADDR		0xFFFFF420
#define MA_PORT0_PM			(*(volatile u8 *)MA_PORT0_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define MA_PORT0_PMC_ADDR		0xFFFFF440
#define MA_PORT0_PMC			(*(volatile u8 *)MA_PORT0_PMC_ADDR)
/* Port function control (for P04-P07, 0 = IRQ, 1 = DMARQ).  */
#define MA_PORT0_PFC_ADDR		0xFFFFF460
#define MA_PORT0_PFC			(*(volatile u8 *)MA_PORT0_PFC_ADDR)

/* Port 1 */
/* Direct I/O.  Bits 0-3 are pins P10-P13.  */
#define MA_PORT1_IO_ADDR		0xFFFFF402
#define MA_PORT1_IO			(*(volatile u8 *)MA_PORT1_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define MA_PORT1_PM_ADDR		0xFFFFF420
#define MA_PORT1_PM			(*(volatile u8 *)MA_PORT1_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define MA_PORT1_PMC_ADDR		0xFFFFF442
#define MA_PORT1_PMC			(*(volatile u8 *)MA_PORT1_PMC_ADDR)

/* Port 4 */
/* Direct I/O.  Bits 0-5 are pins P40-P45.  */
#define MA_PORT4_IO_ADDR		0xFFFFF408
#define MA_PORT4_IO			(*(volatile u8 *)MA_PORT4_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define MA_PORT4_PM_ADDR		0xFFFFF428
#define MA_PORT4_PM			(*(volatile u8 *)MA_PORT4_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define MA_PORT4_PMC_ADDR		0xFFFFF448
#define MA_PORT4_PMC			(*(volatile u8 *)MA_PORT4_PMC_ADDR)
/* Port function control (for serial interfaces, 0 = CSI, 1 = UART).  */
#define MA_PORT4_PFC_ADDR		0xFFFFF468
#define MA_PORT4_PFC			(*(volatile u8 *)MA_PORT4_PFC_ADDR)


#ifndef __ASSEMBLY__

/* Initialize MA chip interrupts.  */
extern void ma_init_irqs (void);

#endif /* !__ASSEMBLY__ */


#endif /* __V850_MA_H__ */
