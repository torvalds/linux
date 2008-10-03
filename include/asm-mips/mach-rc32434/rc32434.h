/*
 * Definitions for IDT RC323434 CPU.
 */

#ifndef _ASM_RC32434_RC32434_H_
#define _ASM_RC32434_RC32434_H_

#include <linux/delay.h>
#include <linux/io.h>

#define RC32434_REG_BASE	0x18000000
#define RC32434_RST		(1 << 15)

#define IDT_CLOCK_MULT		2
#define MIPS_CPU_TIMER_IRQ	7

/* Interrupt Controller */
#define IC_GROUP0_PEND		(RC32434_REG_BASE + 0x38000)
#define IC_GROUP0_MASK		(RC32434_REG_BASE + 0x38008)
#define IC_GROUP_OFFSET		0x0C

#define NUM_INTR_GROUPS		5

/* 16550 UARTs */
#define GROUP0_IRQ_BASE		8	/* GRP2 IRQ numbers start here */
					/* GRP3 IRQ numbers start here */
#define GROUP1_IRQ_BASE		(GROUP0_IRQ_BASE + 32)
					/* GRP4 IRQ numbers start here */
#define GROUP2_IRQ_BASE		(GROUP1_IRQ_BASE + 32)
					/* GRP5 IRQ numbers start here */
#define GROUP3_IRQ_BASE		(GROUP2_IRQ_BASE + 32)
#define GROUP4_IRQ_BASE		(GROUP3_IRQ_BASE + 32)


#ifdef __MIPSEB__
#define RC32434_UART0_BASE	(RC32434_REG_BASE + 0x58003)
#else
#define RC32434_UART0_BASE	(RC32434_REG_BASE + 0x58000)
#endif

#define RC32434_UART0_IRQ	(GROUP3_IRQ_BASE + 0)

/* cpu pipeline flush */
static inline void rc32434_sync(void)
{
	__asm__ volatile ("sync");
}

static inline void rc32434_sync_udelay(int us)
{
	__asm__ volatile ("sync");
	udelay(us);
}

static inline void rc32434_sync_delay(int ms)
{
	__asm__ volatile ("sync");
	mdelay(ms);
}

#endif  /* _ASM_RC32434_RC32434_H_ */
