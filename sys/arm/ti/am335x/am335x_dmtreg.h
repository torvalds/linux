/*-
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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

#ifndef AM335X_DMTREG_H
#define AM335X_DMTREG_H

#define	AM335X_NUM_TIMERS	8

#define	DMT_TIDR		0x00		/* Identification Register */
#define	DMT_TIOCP_CFG		0x10		/* OCP Configuration Reg */
#define	  DMT_TIOCP_RESET	  (1 << 0)	/* TIOCP perform soft reset */
#define	DMT_IQR_EOI		0x20		/* IRQ End-Of-Interrupt Reg */
#define	DMT_IRQSTATUS_RAW	0x24		/* IRQSTATUS Raw Reg */
#define	DMT_IRQSTATUS		0x28		/* IRQSTATUS Reg */
#define	DMT_IRQENABLE_SET	0x2c		/* IRQSTATUS Set Reg */
#define	DMT_IRQENABLE_CLR	0x30		/* IRQSTATUS Clear Reg */
#define	DMT_IRQWAKEEN		0x34		/* IRQ Wakeup Enable Reg */
#define	  DMT_IRQ_MAT		  (1 << 0)	/* IRQ: Match */
#define	  DMT_IRQ_OVF		  (1 << 1)	/* IRQ: Overflow */
#define	  DMT_IRQ_TCAR		  (1 << 2)	/* IRQ: Capture */
#define	  DMT_IRQ_MASK		  (DMT_IRQ_TCAR | DMT_IRQ_OVF | DMT_IRQ_MAT)
#define	DMT_TCLR		0x38		/* Control Register */
#define	  DMT_TCLR_START	  (1 << 0)	/* Start timer */
#define	  DMT_TCLR_AUTOLOAD	  (1 << 1)	/* Auto-reload on overflow */
#define	  DMT_TCLR_PRES_MASK	  (7 << 2)	/* Prescaler mask */
#define	  DMT_TCLR_PRES_ENABLE	  (1 << 5)	/* Prescaler enable */
#define	  DMT_TCLR_COMP_ENABLE	  (1 << 6)	/* Compare enable */
#define	  DMT_TCLR_PWM_HIGH	  (1 << 7)	/* PWM default output high */
#define	  DMT_TCLR_CAPTRAN_MASK	  (3 << 8)	/* Capture transition mask */
#define	  DMT_TCLR_CAPTRAN_NONE	  (0 << 8)	/* Capture: none */
#define	  DMT_TCLR_CAPTRAN_LOHI	  (1 << 8)	/* Capture lo->hi transition */
#define	  DMT_TCLR_CAPTRAN_HILO	  (2 << 8)	/* Capture hi->lo transition */
#define	  DMT_TCLR_CAPTRAN_BOTH	  (3 << 8)	/* Capture both transitions */
#define	  DMT_TCLR_TRGMODE_MASK	  (3 << 10)	/* Trigger output mode mask */
#define	  DMT_TCLR_TRGMODE_NONE	  (0 << 10)	/* Trigger off */
#define	  DMT_TCLR_TRGMODE_OVFL	  (1 << 10)	/* Trigger on overflow */
#define	  DMT_TCLR_TRGMODE_BOTH	  (2 << 10)	/* Trigger on match + ovflow */
#define	  DMT_TCLR_PWM_PTOGGLE	  (1 << 12)	/* PWM toggles */
#define	  DMT_TCLR_CAP_MODE_2ND	  (1 << 13)	/* Capture second event mode */
#define	  DMT_TCLR_GPO_CFG	  (1 << 14)	/* Tmr pin conf, 0=out, 1=in */
#define	DMT_TCRR		0x3C		/* Counter Register */
#define	DMT_TLDR		0x40		/* Load Reg */
#define	DMT_TTGR		0x44		/* Trigger Reg */
#define	DMT_TWPS		0x48		/* Write Posted Status Reg */
#define	DMT_TMAR		0x4C		/* Match Reg */
#define	DMT_TCAR1		0x50		/* Capture Reg */
#define	DMT_TSICR		0x54		/* Synchr. Interface Ctrl Reg */
#define	  DMT_TSICR_RESET	  (1 << 1)	/* TSICR perform soft reset */
#define	DMT_TCAR2		0x48		/* Capture Reg */

#endif /* AM335X_DMTREG_H */
