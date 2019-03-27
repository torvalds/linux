/*-
 * Copyright (c) 2011 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2015 Hiroki Mori
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_ARM_RALINK_RT1310REG_H
#define	_ARM_RALINK_RT1310REG_H

/*
 * Interrupt controller
 */

#define	RT_INTC_SCR0			0x00
#define	RT_INTC_SVR0			0x80
#define	RT_INTC_ISR			0x104
#define	RT_INTC_IPR			0x108
#define	RT_INTC_IMR			0x10c
#define	RT_INTC_IECR			0x114
#define	RT_INTC_ICCR			0x118

#define	RT_INTC_TRIG_LOW_LVL		(0)
#define	RT_INTC_TRIG_HIGH_LVL		(1)
#define	RT_INTC_TRIG_NEG_EDGE		(2)
#define	RT_INTC_TRIG_POS_EDGE		(3)

#define	RT_INTC_TRIG_SHIF		6

/*
 * Timer 0|1|2|3.
 */

#define	RT_TIMER_LOAD			0x00
#define	RT_TIMER_VALUE			0x04
#define	RT_TIMER_CONTROL		0x08

#define	RT_TIMER_CTRL_INTCTL		(1 << 1)
#define	RT_TIMER_CTRL_INTCLR		(1 << 2)
#define	RT_TIMER_CTRL_INTMASK		(1 << 3)
#define	RT_TIMER_CTRL_DIV16		(3 << 4)
#define	RT_TIMER_CTRL_DIV256		(7 << 4)
#define	RT_TIMER_CTRL_PERIODCAL		(1 << 7)
#define	RT_TIMER_CTRL_ENABLE		(1 << 8)

#define	RT_TIMER_INTERVAL		(5000*150)

/*
 * GPIO
 */

#define	RT_GPIO_PORTA	(0)
#define	RT_GPIO_PORTB	(1)

#define	RT_GPIO_OFF_PADR	(0x0)
#define	RT_GPIO_OFF_PADIR	(0x4)
#define	RT_GPIO_OFF_PBDR	(0x8)
#define	RT_GPIO_OFF_PBDIR	(0xC)

#endif	/* _ARM_RALINK_RT1310REG_H */
