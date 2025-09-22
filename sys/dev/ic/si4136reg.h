/*	$OpenBSD: si4136reg.h,v 1.3 2009/08/16 18:03:48 jsg Exp $	*/
/* $NetBSD$ */

/*
 * Copyright (c) 2004 David Young.  All rights reserved.
 *
 * This code was written by David Young.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY David Young ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL David
 * Young BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef _DEV_IC_SI4136REG_H_
#define	_DEV_IC_SI4136REG_H_

/*
 * Serial bus format for Silicon Laboratories Si4126/Si4136 RF synthesizer.
 */
#define SI4126_TWI_DATA_MASK	0x3ffff0
#define SI4126_TWI_ADDR_MASK	0xf

/*
 * Registers for Silicon Laboratories Si4126/Si4136 RF synthesizer.
 */
#define SI4126_MAIN	0	/* main configuration */
#define	SI4126_MAIN_AUXSEL_MASK	0x3000	/* aux. output pin function */
/* reserved */
#define	SI4126_MAIN_AUXSEL_RSVD		LSHIFT(0x0, SI4126_MAIN_AUXSEL_MASK)
/* force low */
#define	SI4126_MAIN_AUXSEL_FRCLOW	LSHIFT(0x1, SI4126_MAIN_AUXSEL_MASK)
/* Lock Detect (LDETB) */
#define	SI4126_MAIN_AUXSEL_LDETB	LSHIFT(0x3, SI4126_MAIN_AUXSEL_MASK)

#define	SI4126_MAIN_IFDIV_MASK	0xc00	/* IFOUT = IFVCO
						 * frequency / 2**IFDIV.
						 */

#define	SI4126_MAIN_XINDIV2	(1<<6)	/* 1: divide crystal input (XIN) by 2 */
#define	SI4126_MAIN_LPWR	(1<<5)	/* 1: low-power mode */
#define	SI4126_MAIN_AUTOPDB	(1<<3)	/* 1: equivalent to
					 *    reg[SI4126_POWER] <-
					 *    SI4126_POWER_PDIB |
					 *    SI4126_POWER_PDRB.
					 *
					 * 0: power-down under control of
					 *    reg[SI4126_POWER].
					 */

#define	SI4126_GAIN	1		/* phase detector gain */
#define	SI4126_GAIN_KPI_MASK	0x30	/* IF phase detector gain */
#define	SI4126_GAIN_KP2_MASK	0xc	/* RF2 phase detector gain */
#define	SI4126_GAIN_KP1_MASK	0x3	/* RF1 phase detector gain */

#define	SI4126_POWER	2		/* powerdown */
#define	SI4126_POWER_PDIB	(1<<1)	/* 1: IF synthesizer on */
#define	SI4126_POWER_PDRB	(1<<0)	/* 1: RF synthesizer on */

#define	SI4126_RF1N	3		/* RF1 N divider */
#define	SI4126_RF2N	4		/* RF2 N divider */
#define	SI4126_IFN	5		/* IF N divider */
#define	SI4126_RF1R	6		/* RF1 R divider */
#define	SI4126_RF2R	7		/* RF2 R divider */
#define	SI4126_IFR	8		/* IF R divider */

#endif /* _DEV_IC_SI4136REG_H_ */
