/*
 * linux/include/asm-arm/arch-l7200/time.h
 *
 * Copyright (C) 2000 Rob Scott (rscott@mtrob.fdns.net)
 *                    Steve Hill (sjhill@cotw.com)
 *
 * Changelog:
 *   01-02-2000	RS	Created l7200 version, derived from rpc code
 *   05-03-2000	SJH	Complete rewrite
 */
#ifndef _ASM_ARCH_TIME_H
#define _ASM_ARCH_TIME_H

#include <asm/arch/irqs.h>

/*
 * RTC base register address
 */
#define RTC_BASE	(IO_BASE_2 + 0x2000)

/*
 * RTC registers
 */
#define RTC_RTCDR	(*(volatile unsigned char *) (RTC_BASE + 0x000))
#define RTC_RTCMR	(*(volatile unsigned char *) (RTC_BASE + 0x004))
#define RTC_RTCS	(*(volatile unsigned char *) (RTC_BASE + 0x008))
#define RTC_RTCC	(*(volatile unsigned char *) (RTC_BASE + 0x008))
#define RTC_RTCDV	(*(volatile unsigned char *) (RTC_BASE + 0x00c))
#define RTC_RTCCR	(*(volatile unsigned char *) (RTC_BASE + 0x010))

/*
 * RTCCR register values
 */
#define RTC_RATE_32	0x00      /* 32 Hz tick */
#define RTC_RATE_64	0x10      /* 64 Hz tick */
#define RTC_RATE_128	0x20      /* 128 Hz tick */
#define RTC_RATE_256	0x30      /* 256 Hz tick */
#define RTC_EN_ALARM	0x01      /* Enable alarm */
#define RTC_EN_TIC	0x04      /* Enable counter */
#define RTC_EN_STWDOG	0x08      /* Enable watchdog */

/*
 * Handler for RTC timer interrupt
 */
static irqreturn_t
timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	do_timer(regs);
#ifndef CONFIG_SMP
	update_process_times(user_mode(regs));
#endif
	do_profile(regs);
	RTC_RTCC = 0;				/* Clear interrupt */

	return IRQ_HANDLED;
}

/*
 * Set up RTC timer interrupt, and return the current time in seconds.
 */
void __init time_init(void)
{
	RTC_RTCC = 0;				/* Clear interrupt */

	timer_irq.handler = timer_interrupt;

	setup_irq(IRQ_RTC_TICK, &timer_irq);

	RTC_RTCCR = RTC_RATE_128 | RTC_EN_TIC;	/* Set rate and enable timer */
}

#endif
