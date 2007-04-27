/*
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_AVR32_ARCH_AT32AP_TIME_H
#define _ASM_AVR32_ARCH_AT32AP_TIME_H

#include <linux/platform_device.h>

extern struct irqaction timer_irqaction;
extern struct platform_device at32_systc0_device;
extern void local_timer_interrupt(int irq, void *dev_id);

#define TIMER_BCR					0x000000c0
#define TIMER_BCR_SYNC						 0
#define TIMER_BMR					0x000000c4
#define TIMER_BMR_TC0XC0S					 0
#define TIMER_BMR_TC1XC1S					 2
#define TIMER_BMR_TC2XC2S					 4
#define TIMER_CCR					0x00000000
#define TIMER_CCR_CLKDIS					 1
#define TIMER_CCR_CLKEN						 0
#define TIMER_CCR_SWTRG						 2
#define TIMER_CMR					0x00000004
#define TIMER_CMR_ABETRG					10
#define TIMER_CMR_ACPA						16
#define TIMER_CMR_ACPC						18
#define TIMER_CMR_AEEVT						20
#define TIMER_CMR_ASWTRG					22
#define TIMER_CMR_BCPB						24
#define TIMER_CMR_BCPC						26
#define TIMER_CMR_BEEVT						28
#define TIMER_CMR_BSWTRG					30
#define TIMER_CMR_BURST						 4
#define TIMER_CMR_CLKI						 3
#define TIMER_CMR_CPCDIS					 7
#define TIMER_CMR_CPCSTOP					 6
#define TIMER_CMR_CPCTRG					14
#define TIMER_CMR_EEVT						10
#define TIMER_CMR_EEVTEDG					 8
#define TIMER_CMR_ENETRG					12
#define TIMER_CMR_ETRGEDG					 8
#define TIMER_CMR_LDBDIS					 7
#define TIMER_CMR_LDBSTOP					 6
#define TIMER_CMR_LDRA						16
#define TIMER_CMR_LDRB						18
#define TIMER_CMR_TCCLKS					 0
#define TIMER_CMR_WAVE						15
#define TIMER_CMR_WAVSEL					13
#define TIMER_CV					0x00000010
#define TIMER_CV_CV						 0
#define TIMER_IDR					0x00000028
#define TIMER_IDR_COVFS						 0
#define TIMER_IDR_CPAS						 2
#define TIMER_IDR_CPBS						 3
#define TIMER_IDR_CPCS						 4
#define TIMER_IDR_ETRGS						 7
#define TIMER_IDR_LDRAS						 5
#define TIMER_IDR_LDRBS						 6
#define TIMER_IDR_LOVRS						 1
#define TIMER_IER					0x00000024
#define TIMER_IER_COVFS						 0
#define TIMER_IER_CPAS						 2
#define TIMER_IER_CPBS						 3
#define TIMER_IER_CPCS						 4
#define TIMER_IER_ETRGS						 7
#define TIMER_IER_LDRAS						 5
#define TIMER_IER_LDRBS						 6
#define TIMER_IER_LOVRS						 1
#define TIMER_IMR					0x0000002c
#define TIMER_IMR_COVFS						 0
#define TIMER_IMR_CPAS						 2
#define TIMER_IMR_CPBS						 3
#define TIMER_IMR_CPCS						 4
#define TIMER_IMR_ETRGS						 7
#define TIMER_IMR_LDRAS						 5
#define TIMER_IMR_LDRBS						 6
#define TIMER_IMR_LOVRS						 1
#define TIMER_RA					0x00000014
#define TIMER_RA_RA						 0
#define TIMER_RB					0x00000018
#define TIMER_RB_RB						 0
#define TIMER_RC					0x0000001c
#define TIMER_RC_RC						 0
#define TIMER_SR					0x00000020
#define TIMER_SR_CLKSTA						16
#define TIMER_SR_COVFS						 0
#define TIMER_SR_CPAS						 2
#define TIMER_SR_CPBS						 3
#define TIMER_SR_CPCS						 4
#define TIMER_SR_ETRGS						 7
#define TIMER_SR_LDRAS						 5
#define TIMER_SR_LDRBS						 6
#define TIMER_SR_LOVRS						 1
#define TIMER_SR_MTIOA						17
#define TIMER_SR_MTIOB						18

/* Bit manipulation macros */
#define TIMER_BIT(name)		(1 << TIMER_##name)
#define TIMER_BF(name,value)	((value) << TIMER_##name)

/* Register access macros */
#define timer_read(port,instance,reg) \
	__raw_readl(port + (0x40 * instance) + TIMER_##reg)
#define timer_write(port,instance,reg,value) \
	__raw_writel((value), port + (0x40 * instance) + TIMER_##reg)

#endif /* _ASM_AVR32_ARCH_AT32AP_TIME_H */
