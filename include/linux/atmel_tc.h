/*
 * Timer/Counter Unit (TC) registers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef ATMEL_TC_H
#define ATMEL_TC_H

#include <linux/compiler.h>
#include <linux/list.h>

/*
 * Many 32-bit Atmel SOCs include one or more TC blocks, each of which holds
 * three general-purpose 16-bit timers.  These timers share one register bank.
 * Depending on the SOC, each timer may have its own clock and IRQ, or those
 * may be shared by the whole TC block.
 *
 * These TC blocks may have up to nine external pins:  TCLK0..2 signals for
 * clocks or clock gates, and per-timer TIOA and TIOB signals used for PWM
 * or triggering.  Those pins need to be set up for use with the TC block,
 * else they will be used as GPIOs or for a different controller.
 *
 * Although we expect each TC block to have a platform_device node, those
 * nodes are not what drivers bind to.  Instead, they ask for a specific
 * TC block, by number ... which is a common approach on systems with many
 * timers.  Then they use clk_get() and platform_get_irq() to get clock and
 * IRQ resources.
 */

struct clk;

/**
 * struct atmel_tcb_config - SoC data for a Timer/Counter Block
 * @counter_width: size in bits of a timer counter register
 */
struct atmel_tcb_config {
	size_t	counter_width;
};

/**
 * struct atmel_tc - information about a Timer/Counter Block
 * @pdev: physical device
 * @iomem: resource associated with the I/O register
 * @regs: mapping through which the I/O registers can be accessed
 * @tcb_config: configuration data from SoC
 * @irq: irq for each of the three channels
 * @clk: internal clock source for each of the three channels
 * @node: list node, for tclib internal use
 *
 * On some platforms, each TC channel has its own clocks and IRQs,
 * while on others, all TC channels share the same clock and IRQ.
 * Drivers should clk_enable() all the clocks they need even though
 * all the entries in @clk may point to the same physical clock.
 * Likewise, drivers should request irqs independently for each
 * channel, but they must use IRQF_SHARED in case some of the entries
 * in @irq are actually the same IRQ.
 */
struct atmel_tc {
	struct platform_device	*pdev;
	struct resource		*iomem;
	void __iomem		*regs;
	const struct atmel_tcb_config *tcb_config;
	int			irq[3];
	struct clk		*clk[3];
	struct list_head	node;
};

extern struct atmel_tc *atmel_tc_alloc(unsigned block, const char *name);
extern void atmel_tc_free(struct atmel_tc *tc);

/* platform-specific ATMEL_TC_TIMER_CLOCKx divisors (0 means 32KiHz) */
extern const u8 atmel_tc_divisors[5];


/*
 * Two registers have block-wide controls.  These are: configuring the three
 * "external" clocks (or event sources) used by the timer channels; and
 * synchronizing the timers by resetting them all at once.
 *
 * "External" can mean "external to chip" using the TCLK0, TCLK1, or TCLK2
 * signals.  Or, it can mean "external to timer", using the TIOA output from
 * one of the other two timers that's being run in waveform mode.
 */

#define ATMEL_TC_BCR	0xc0		/* TC Block Control Register */
#define     ATMEL_TC_SYNC	(1 << 0)	/* synchronize timers */

#define ATMEL_TC_BMR	0xc4		/* TC Block Mode Register */
#define     ATMEL_TC_TC0XC0S	(3 << 0)	/* external clock 0 source */
#define        ATMEL_TC_TC0XC0S_TCLK0	(0 << 0)
#define        ATMEL_TC_TC0XC0S_NONE	(1 << 0)
#define        ATMEL_TC_TC0XC0S_TIOA1	(2 << 0)
#define        ATMEL_TC_TC0XC0S_TIOA2	(3 << 0)
#define     ATMEL_TC_TC1XC1S	(3 << 2)	/* external clock 1 source */
#define        ATMEL_TC_TC1XC1S_TCLK1	(0 << 2)
#define        ATMEL_TC_TC1XC1S_NONE	(1 << 2)
#define        ATMEL_TC_TC1XC1S_TIOA0	(2 << 2)
#define        ATMEL_TC_TC1XC1S_TIOA2	(3 << 2)
#define     ATMEL_TC_TC2XC2S	(3 << 4)	/* external clock 2 source */
#define        ATMEL_TC_TC2XC2S_TCLK2	(0 << 4)
#define        ATMEL_TC_TC2XC2S_NONE	(1 << 4)
#define        ATMEL_TC_TC2XC2S_TIOA0	(2 << 4)
#define        ATMEL_TC_TC2XC2S_TIOA1	(3 << 4)


/*
 * Each TC block has three "channels", each with one counter and controls.
 *
 * Note that the semantics of ATMEL_TC_TIMER_CLOCKx (input clock selection
 * when it's not "external") is silicon-specific.  AT91 platforms use one
 * set of definitions; AVR32 platforms use a different set.  Don't hard-wire
 * such knowledge into your code, use the global "atmel_tc_divisors" ...
 * where index N is the divisor for clock N+1, else zero to indicate it uses
 * the 32 KiHz clock.
 *
 * The timers can be chained in various ways, and operated in "waveform"
 * generation mode (including PWM) or "capture" mode (to time events).  In
 * both modes, behavior can be configured in many ways.
 *
 * Each timer has two I/O pins, TIOA and TIOB.  Waveform mode uses TIOA as a
 * PWM output, and TIOB as either another PWM or as a trigger.  Capture mode
 * uses them only as inputs.
 */
#define ATMEL_TC_CHAN(idx)	((idx)*0x40)
#define ATMEL_TC_REG(idx, reg)	(ATMEL_TC_CHAN(idx) + ATMEL_TC_ ## reg)

#define ATMEL_TC_CCR	0x00		/* Channel Control Register */
#define     ATMEL_TC_CLKEN	(1 << 0)	/* clock enable */
#define     ATMEL_TC_CLKDIS	(1 << 1)	/* clock disable */
#define     ATMEL_TC_SWTRG	(1 << 2)	/* software trigger */

#define ATMEL_TC_CMR	0x04		/* Channel Mode Register */

/* Both modes share some CMR bits */
#define     ATMEL_TC_TCCLKS	(7 << 0)	/* clock source */
#define        ATMEL_TC_TIMER_CLOCK1	(0 << 0)
#define        ATMEL_TC_TIMER_CLOCK2	(1 << 0)
#define        ATMEL_TC_TIMER_CLOCK3	(2 << 0)
#define        ATMEL_TC_TIMER_CLOCK4	(3 << 0)
#define        ATMEL_TC_TIMER_CLOCK5	(4 << 0)
#define        ATMEL_TC_XC0		(5 << 0)
#define        ATMEL_TC_XC1		(6 << 0)
#define        ATMEL_TC_XC2		(7 << 0)
#define     ATMEL_TC_CLKI	(1 << 3)	/* clock invert */
#define     ATMEL_TC_BURST	(3 << 4)	/* clock gating */
#define        ATMEL_TC_GATE_NONE	(0 << 4)
#define        ATMEL_TC_GATE_XC0	(1 << 4)
#define        ATMEL_TC_GATE_XC1	(2 << 4)
#define        ATMEL_TC_GATE_XC2	(3 << 4)
#define     ATMEL_TC_WAVE	(1 << 15)	/* true = Waveform mode */

/* CAPTURE mode CMR bits */
#define     ATMEL_TC_LDBSTOP	(1 << 6)	/* counter stops on RB load */
#define     ATMEL_TC_LDBDIS	(1 << 7)	/* counter disable on RB load */
#define     ATMEL_TC_ETRGEDG	(3 << 8)	/* external trigger edge */
#define        ATMEL_TC_ETRGEDG_NONE	(0 << 8)
#define        ATMEL_TC_ETRGEDG_RISING	(1 << 8)
#define        ATMEL_TC_ETRGEDG_FALLING	(2 << 8)
#define        ATMEL_TC_ETRGEDG_BOTH	(3 << 8)
#define     ATMEL_TC_ABETRG	(1 << 10)	/* external trigger is TIOA? */
#define     ATMEL_TC_CPCTRG	(1 << 14)	/* RC compare trigger enable */
#define     ATMEL_TC_LDRA	(3 << 16)	/* RA loading edge (of TIOA) */
#define        ATMEL_TC_LDRA_NONE	(0 << 16)
#define        ATMEL_TC_LDRA_RISING	(1 << 16)
#define        ATMEL_TC_LDRA_FALLING	(2 << 16)
#define        ATMEL_TC_LDRA_BOTH	(3 << 16)
#define     ATMEL_TC_LDRB	(3 << 18)	/* RB loading edge (of TIOA) */
#define        ATMEL_TC_LDRB_NONE	(0 << 18)
#define        ATMEL_TC_LDRB_RISING	(1 << 18)
#define        ATMEL_TC_LDRB_FALLING	(2 << 18)
#define        ATMEL_TC_LDRB_BOTH	(3 << 18)

/* WAVEFORM mode CMR bits */
#define     ATMEL_TC_CPCSTOP	(1 <<  6)	/* RC compare stops counter */
#define     ATMEL_TC_CPCDIS	(1 <<  7)	/* RC compare disables counter */
#define     ATMEL_TC_EEVTEDG	(3 <<  8)	/* external event edge */
#define        ATMEL_TC_EEVTEDG_NONE	(0 << 8)
#define        ATMEL_TC_EEVTEDG_RISING	(1 << 8)
#define        ATMEL_TC_EEVTEDG_FALLING	(2 << 8)
#define        ATMEL_TC_EEVTEDG_BOTH	(3 << 8)
#define     ATMEL_TC_EEVT	(3 << 10)	/* external event source */
#define        ATMEL_TC_EEVT_TIOB	(0 << 10)
#define        ATMEL_TC_EEVT_XC0	(1 << 10)
#define        ATMEL_TC_EEVT_XC1	(2 << 10)
#define        ATMEL_TC_EEVT_XC2	(3 << 10)
#define     ATMEL_TC_ENETRG	(1 << 12)	/* external event is trigger */
#define     ATMEL_TC_WAVESEL	(3 << 13)	/* waveform type */
#define        ATMEL_TC_WAVESEL_UP	(0 << 13)
#define        ATMEL_TC_WAVESEL_UPDOWN	(1 << 13)
#define        ATMEL_TC_WAVESEL_UP_AUTO	(2 << 13)
#define        ATMEL_TC_WAVESEL_UPDOWN_AUTO (3 << 13)
#define     ATMEL_TC_ACPA	(3 << 16)	/* RA compare changes TIOA */
#define        ATMEL_TC_ACPA_NONE	(0 << 16)
#define        ATMEL_TC_ACPA_SET	(1 << 16)
#define        ATMEL_TC_ACPA_CLEAR	(2 << 16)
#define        ATMEL_TC_ACPA_TOGGLE	(3 << 16)
#define     ATMEL_TC_ACPC	(3 << 18)	/* RC compare changes TIOA */
#define        ATMEL_TC_ACPC_NONE	(0 << 18)
#define        ATMEL_TC_ACPC_SET	(1 << 18)
#define        ATMEL_TC_ACPC_CLEAR	(2 << 18)
#define        ATMEL_TC_ACPC_TOGGLE	(3 << 18)
#define     ATMEL_TC_AEEVT	(3 << 20)	/* external event changes TIOA */
#define        ATMEL_TC_AEEVT_NONE	(0 << 20)
#define        ATMEL_TC_AEEVT_SET	(1 << 20)
#define        ATMEL_TC_AEEVT_CLEAR	(2 << 20)
#define        ATMEL_TC_AEEVT_TOGGLE	(3 << 20)
#define     ATMEL_TC_ASWTRG	(3 << 22)	/* software trigger changes TIOA */
#define        ATMEL_TC_ASWTRG_NONE	(0 << 22)
#define        ATMEL_TC_ASWTRG_SET	(1 << 22)
#define        ATMEL_TC_ASWTRG_CLEAR	(2 << 22)
#define        ATMEL_TC_ASWTRG_TOGGLE	(3 << 22)
#define     ATMEL_TC_BCPB	(3 << 24)	/* RB compare changes TIOB */
#define        ATMEL_TC_BCPB_NONE	(0 << 24)
#define        ATMEL_TC_BCPB_SET	(1 << 24)
#define        ATMEL_TC_BCPB_CLEAR	(2 << 24)
#define        ATMEL_TC_BCPB_TOGGLE	(3 << 24)
#define     ATMEL_TC_BCPC	(3 << 26)	/* RC compare changes TIOB */
#define        ATMEL_TC_BCPC_NONE	(0 << 26)
#define        ATMEL_TC_BCPC_SET	(1 << 26)
#define        ATMEL_TC_BCPC_CLEAR	(2 << 26)
#define        ATMEL_TC_BCPC_TOGGLE	(3 << 26)
#define     ATMEL_TC_BEEVT	(3 << 28)	/* external event changes TIOB */
#define        ATMEL_TC_BEEVT_NONE	(0 << 28)
#define        ATMEL_TC_BEEVT_SET	(1 << 28)
#define        ATMEL_TC_BEEVT_CLEAR	(2 << 28)
#define        ATMEL_TC_BEEVT_TOGGLE	(3 << 28)
#define     ATMEL_TC_BSWTRG	(3 << 30)	/* software trigger changes TIOB */
#define        ATMEL_TC_BSWTRG_NONE	(0 << 30)
#define        ATMEL_TC_BSWTRG_SET	(1 << 30)
#define        ATMEL_TC_BSWTRG_CLEAR	(2 << 30)
#define        ATMEL_TC_BSWTRG_TOGGLE	(3 << 30)

#define ATMEL_TC_CV	0x10		/* counter Value */
#define ATMEL_TC_RA	0x14		/* register A */
#define ATMEL_TC_RB	0x18		/* register B */
#define ATMEL_TC_RC	0x1c		/* register C */

#define ATMEL_TC_SR	0x20		/* status (read-only) */
/* Status-only flags */
#define     ATMEL_TC_CLKSTA	(1 << 16)	/* clock enabled */
#define     ATMEL_TC_MTIOA	(1 << 17)	/* TIOA mirror */
#define     ATMEL_TC_MTIOB	(1 << 18)	/* TIOB mirror */

#define ATMEL_TC_IER	0x24		/* interrupt enable (write-only) */
#define ATMEL_TC_IDR	0x28		/* interrupt disable (write-only) */
#define ATMEL_TC_IMR	0x2c		/* interrupt mask (read-only) */

/* Status and IRQ flags */
#define     ATMEL_TC_COVFS	(1 <<  0)	/* counter overflow */
#define     ATMEL_TC_LOVRS	(1 <<  1)	/* load overrun */
#define     ATMEL_TC_CPAS	(1 <<  2)	/* RA compare */
#define     ATMEL_TC_CPBS	(1 <<  3)	/* RB compare */
#define     ATMEL_TC_CPCS	(1 <<  4)	/* RC compare */
#define     ATMEL_TC_LDRAS	(1 <<  5)	/* RA loading */
#define     ATMEL_TC_LDRBS	(1 <<  6)	/* RB loading */
#define     ATMEL_TC_ETRGS	(1 <<  7)	/* external trigger */

#endif
