/*
 * include/asm-v850/me2.h -- V850E/ME2 cpu chip
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

#ifndef __V850_ME2_H__
#define __V850_ME2_H__

#include <asm/v850e.h>
#include <asm/v850e_cache.h>


#define CPU_MODEL	"v850e/me2"
#define CPU_MODEL_LONG	"NEC V850E/ME2"


/* Hardware-specific interrupt numbers (in the kernel IRQ namespace).  */
#define IRQ_INTP(n)       (n) /* Pnnn (pin) interrupts */
#define IRQ_INTP_NUM      31
#define IRQ_INTCMD(n)     (0x31 + (n)) /* interval timer interrupts 0-3 */
#define IRQ_INTCMD_NUM    4
#define IRQ_INTDMA(n)     (0x41 + (n)) /* DMA interrupts 0-3 */
#define IRQ_INTDMA_NUM    4
#define IRQ_INTUBTIRE(n)  (0x49 + (n)*5)/* UARTB 0-1 reception error */
#define IRQ_INTUBTIRE_NUM 2
#define IRQ_INTUBTIR(n)   (0x4a + (n)*5) /* UARTB 0-1 reception complete */
#define IRQ_INTUBTIR_NUM  2
#define IRQ_INTUBTIT(n)   (0x4b + (n)*5) /* UARTB 0-1 transmission complete */
#define IRQ_INTUBTIT_NUM  2
#define IRQ_INTUBTIF(n)   (0x4c + (n)*5) /* UARTB 0-1 FIFO trans. complete */
#define IRQ_INTUBTIF_NUM  2
#define IRQ_INTUBTITO(n)  (0x4d + (n)*5) /* UARTB 0-1 reception timeout */
#define IRQ_INTUBTITO_NUM 2

/* For <asm/irq.h> */
#define NUM_CPU_IRQS		0x59 /* V850E/ME2 */


/* For <asm/entry.h> */
/* We use on-chip RAM, for a few miscellaneous variables that must be
   accessible using a load instruction relative to R0.  */
#define R0_RAM_ADDR			0xFFFFB000 /* V850E/ME2 */


/* V850E/ME2 UARTB details.*/
#define V850E_UART_NUM_CHANNELS		2
#define V850E_UARTB_BASE_FREQ		(CPU_CLOCK_FREQ / 4)

/* This is a function that gets called before configuring the UART.  */
#define V850E_UART_PRE_CONFIGURE	me2_uart_pre_configure
#ifndef __ASSEMBLY__
extern void me2_uart_pre_configure (unsigned chan,
				    unsigned cflags, unsigned baud);
#endif /* __ASSEMBLY__ */


/* V850E/ME2 timer C details.  */
#define V850E_TIMER_C_BASE_ADDR		0xFFFFF600


/* V850E/ME2 timer D details.  */
#define V850E_TIMER_D_BASE_ADDR		0xFFFFF540
#define V850E_TIMER_D_TMD_BASE_ADDR	(V850E_TIMER_D_BASE_ADDR + 0x0)
#define V850E_TIMER_D_CMD_BASE_ADDR 	(V850E_TIMER_D_BASE_ADDR + 0x2)
#define V850E_TIMER_D_TMCD_BASE_ADDR	(V850E_TIMER_D_BASE_ADDR + 0x4)

#define V850E_TIMER_D_BASE_FREQ		(CPU_CLOCK_FREQ / 2)


/* Select iRAM mode.  */
#define ME2_IRAMM_ADDR			0xFFFFF80A
#define ME2_IRAMM			(*(volatile u8*)ME2_IRAMM_ADDR)


/* Interrupt edge-detection configuration.  INTF(n) and INTR(n) are only
   valid for n == 1, 2, or 5.  */
#define ME2_INTF_ADDR(n)		(0xFFFFFC00 + (n) * 0x2)
#define ME2_INTF(n)			(*(volatile u8*)ME2_INTF_ADDR(n))
#define ME2_INTR_ADDR(n)		(0xFFFFFC20 + (n) * 0x2)
#define ME2_INTR(n)			(*(volatile u8*)ME2_INTR_ADDR(n))
#define ME2_INTFAL_ADDR			0xFFFFFC10
#define ME2_INTFAL			(*(volatile u8*)ME2_INTFAL_ADDR)
#define ME2_INTRAL_ADDR			0xFFFFFC30
#define ME2_INTRAL			(*(volatile u8*)ME2_INTRAL_ADDR)
#define ME2_INTFDH_ADDR			0xFFFFFC16
#define ME2_INTFDH			(*(volatile u16*)ME2_INTFDH_ADDR)
#define ME2_INTRDH_ADDR			0xFFFFFC36
#define ME2_INTRDH			(*(volatile u16*)ME2_INTRDH_ADDR)
#define ME2_SESC_ADDR(n)		(0xFFFFF609 + (n) * 0x10)
#define ME2_SESC(n)			(*(volatile u8*)ME2_SESC_ADDR(n))
#define ME2_SESA10_ADDR			0xFFFFF5AD
#define ME2_SESA10			(*(volatile u8*)ME2_SESA10_ADDR)
#define ME2_SESA11_ADDR			0xFFFFF5DD
#define ME2_SESA11			(*(volatile u8*)ME2_SESA11_ADDR)


/* Port 1 */
/* Direct I/O.  Bits 0-3 are pins P10-P13.  */
#define ME2_PORT1_IO_ADDR		0xFFFFF402
#define ME2_PORT1_IO			(*(volatile u8 *)ME2_PORT1_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define ME2_PORT1_PM_ADDR		0xFFFFF422
#define ME2_PORT1_PM			(*(volatile u8 *)ME2_PORT1_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define ME2_PORT1_PMC_ADDR		0xFFFFF442
#define ME2_PORT1_PMC			(*(volatile u8 *)ME2_PORT1_PMC_ADDR)
/* Port function control (for serial interfaces, 0 = CSI30, 1 = UARTB0 ).  */
#define ME2_PORT1_PFC_ADDR		0xFFFFF462
#define ME2_PORT1_PFC			(*(volatile u8 *)ME2_PORT1_PFC_ADDR)

/* Port 2 */
/* Direct I/O.  Bits 0-3 are pins P20-P25.  */
#define ME2_PORT2_IO_ADDR		0xFFFFF404
#define ME2_PORT2_IO			(*(volatile u8 *)ME2_PORT2_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define ME2_PORT2_PM_ADDR		0xFFFFF424
#define ME2_PORT2_PM			(*(volatile u8 *)ME2_PORT2_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define ME2_PORT2_PMC_ADDR		0xFFFFF444
#define ME2_PORT2_PMC			(*(volatile u8 *)ME2_PORT2_PMC_ADDR)
/* Port function control (for serial interfaces, 0 = INTP2x, 1 = UARTB1 ).  */
#define ME2_PORT2_PFC_ADDR		0xFFFFF464
#define ME2_PORT2_PFC			(*(volatile u8 *)ME2_PORT2_PFC_ADDR)

/* Port 5 */
/* Direct I/O.  Bits 0-5 are pins P50-P55.  */
#define ME2_PORT5_IO_ADDR		0xFFFFF40A
#define ME2_PORT5_IO			(*(volatile u8 *)ME2_PORT5_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define ME2_PORT5_PM_ADDR		0xFFFFF42A
#define ME2_PORT5_PM			(*(volatile u8 *)ME2_PORT5_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define ME2_PORT5_PMC_ADDR		0xFFFFF44A
#define ME2_PORT5_PMC			(*(volatile u8 *)ME2_PORT5_PMC_ADDR)
/* Port function control ().  */
#define ME2_PORT5_PFC_ADDR		0xFFFFF46A
#define ME2_PORT5_PFC			(*(volatile u8 *)ME2_PORT5_PFC_ADDR)

/* Port 6 */
/* Direct I/O.  Bits 5-7 are pins P65-P67.  */
#define ME2_PORT6_IO_ADDR		0xFFFFF40C
#define ME2_PORT6_IO			(*(volatile u8 *)ME2_PORT6_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define ME2_PORT6_PM_ADDR		0xFFFFF42C
#define ME2_PORT6_PM			(*(volatile u8 *)ME2_PORT6_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define ME2_PORT6_PMC_ADDR		0xFFFFF44C
#define ME2_PORT6_PMC			(*(volatile u8 *)ME2_PORT6_PMC_ADDR)
/* Port function control ().  */
#define ME2_PORT6_PFC_ADDR		0xFFFFF46C
#define ME2_PORT6_PFC			(*(volatile u8 *)ME2_PORT6_PFC_ADDR)

/* Port 7 */
/* Direct I/O.  Bits 2-7 are pins P72-P77.  */
#define ME2_PORT7_IO_ADDR		0xFFFFF40E
#define ME2_PORT7_IO			(*(volatile u8 *)ME2_PORT7_IO_ADDR)
/* Port mode (for direct I/O, 0 = output, 1 = input).  */
#define ME2_PORT7_PM_ADDR		0xFFFFF42E
#define ME2_PORT7_PM			(*(volatile u8 *)ME2_PORT7_PM_ADDR)
/* Port mode control (0 = direct I/O mode, 1 = alternative I/O mode).  */
#define ME2_PORT7_PMC_ADDR		0xFFFFF44E
#define ME2_PORT7_PMC			(*(volatile u8 *)ME2_PORT7_PMC_ADDR)
/* Port function control ().  */
#define ME2_PORT7_PFC_ADDR		0xFFFFF46E
#define ME2_PORT7_PFC			(*(volatile u8 *)ME2_PORT7_PFC_ADDR)


#ifndef __ASSEMBLY__
/* Initialize V850E/ME2 chip interrupts.  */
extern void me2_init_irqs (void);
#endif /* !__ASSEMBLY__ */


#endif /* __V850_ME2_H__ */
