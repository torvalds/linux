/*-
 * Copyright (c) 2014 Rui Paulo <rpaulo@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _TI_WDT_H_
#define _TI_WDT_H_

/* TI WDT registers */
#define	TI_WDT_WIDR		0x00	/* Watchdog Identification Register */
#define	TI_WDT_WDSC		0x10	/* Watchdog System Control Register */
#define	TI_WDT_WDST		0x14	/* Watchdog Status Register */
#define	TI_WDT_WISR		0x18	/* Watchdog Interrupt Status Register */
#define	TI_WDT_WIER		0x1c	/* Watchdog Interrupt Enable Register */
#define	TI_WDT_WCLR		0x24	/* Watchdog Control Register */
#define	TI_WDT_WCRR		0x28	/* Watchdog Counter Register */
#define	TI_WDT_WLDR		0x2c	/* Watchdog Load Register */
#define	TI_WDT_WTGR		0x30	/* Watchdog Trigger Register */
#define	TI_WDT_WWPS		0x34	/* Watchdog Write Posting Register */
#define	TI_WDT_WDLY		0x44	/* Watchdog Delay Configuration Reg */
#define	TI_WDT_WSPR		0x48	/* Watchdog Start/Stop Register */
#define	TI_WDT_WIRQSTATRAW	0x54	/* Watchdog Raw Interrupt Status Reg. */
#define	TI_WDT_WIRQSTAT		0x58	/* Watchdog Int. Status Register */
#define	TI_WDT_WIRQENSET	0x5c	/* Watchdog Int. Enable Set Register */
#define	TI_WDT_WIRQENCLR	0x60	/* Watchdog Int. Enable Clear Reg. */

/* WDT_WDSC Register */
#define	TI_WDSC_SR		(1 << 1) /* Soft reset */

/*
 * WDT_WWPS Register
 *
 * Writes to some registers require synchronisation with a different clock
 * domain.  The WDT_WWPS register is the place where this synchronisation
 * happens.
 */
#define	TI_W_PEND_WCLR		(1 << 0)
#define	TI_W_PEND_WCRR		(1 << 1)
#define	TI_W_PEND_WLDR		(1 << 2)
#define	TI_W_PEND_WTGR		(1 << 3)
#define	TI_W_PEND_WSPR		(1 << 4)
#define	TI_W_PEND_WDLY		(1 << 5)

/* WDT_WIRQENSET Register */
#define	TI_IRQ_EN_OVF		(1 << 0)	/* Overflow interrupt */
#define	TI_IRQ_EN_DLY		(1 << 1)	/* Delay interrupt */

/* WDT_WIRQSTAT Register */
#define	TI_IRQ_EV_OVF		(1 << 0)	/* Overflow event */
#define	TI_IRQ_EV_DLY		(1 << 1)	/* Delay event */

#endif /* _TI_WDT_H_ */
