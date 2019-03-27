/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2004 by Peter Grehan. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _VIA6522REG_H_
#define _VIA6522REG_H_

/* Registers */
#define REG_OIRB	0	/* Input/output register B */
#define REG_OIRA	1	/* Input/output register A */
#define REG_DDRB	2	/* Data direction register B */
#define REG_DDRA	3	/* Data direction register A */
#define REG_T1CL	4	/* T1 low-order latch/low-order counter */
#define REG_T1CH	5	/* T1 high-order counter */
#define REG_T1LL	6	/* T1 low-order latches */
#define REG_T1LH	7	/* T1 high-order latches */
#define REG_T2CL	8	/* T2 low-order latch/low-order counter */
#define REG_T2CH	9	/* T2 high-order counter */
#define REG_SR		10	/* Shift register */
#define REG_ACR		11	/* Auxiliary control register */
#define REG_PCR		12	/* Peripheral control register */
#define REG_IFR		13	/* Interrupt flag register */
#define REG_IER		14	/* Interrupt-enable register */
#define REG_OIRA_NH	15	/* Input/output register A: no handshake */


/* Auxiliary control register (11) */
#define ACR_SR_NONE	0x0	/* Disabled */
#define ACR_SR_DIR	0x4	/* Bit for shift-register direction 1=out */
#define ACR_SRI_T2	0x1	/* Shift in under control of T2 */
#define ACR_SRI_PHI2	0x2	/*   "    "   "     "      " PHI2 */
#define ACR_SRI_EXTCLK	0x3	/*   "    "   "     "      " external clk */
#define ACR_SRO		0x4	/* Shift out free running at T2 rate */
#define ACR_SRO_T2	0x5	/* Shift out under control of T2 */
#define ACR_SRO_PHI2	0x6	/*   "    "   "     "      "  PHI2 */
#define ACR_SRO_EXTCLK	0x7	/*   "    "   "     "      "  external clk */

#define ACR_T1_SHIFT	5	/* bits 7-5 */
#define ACR_SR_SHIFT	2	/* bits 4-2 */


/* Peripheral control register (12) */
#define PCR_INTCNTL	0x01	/* interrupt active edge: +ve=1, -ve=0 */

#define PCR_CNTL_MASK	0x3	/* 3 bits */
#define PCR_CNTL_NEDGE	0x0	/* Input - negative active edge */
#define PCR_CNTL_INEDGE 0x1	/* Interrupt - negative active edge */
#define PCR_CNTL_PEDGE	0x2	/* Input - positive active edge */
#define PCR_CNTL_IPEDGE 0x3	/* Interrupt - positive active edge */
#define PCR_CNTL_HSHAKE 0x4	/* Handshake output */
#define PCR_CNTL_PULSE	0x5	/* Pulse output */
#define PCR_CNTL_LOW	0x6	/* Low output */
#define PCR_CNTL_HIGH	0x7	/* High output */

#define PCR_CB2_SHIFT	5	/* bits 7-5 */
#define PCR_CB1_SHIFT	4	/* bit 4 */
#define PCR_CA2_SHIFT	1	/* bits 3-1 */
#define PCR_CA1_SHIFT	0	/* bit 0 */

/* Interrupt flag register (13) */
#define IFR_CA2		0x01
#define IFR_CA1		0x02
#define IFR_SR		0x04
#define IFR_CB2		0x08
#define IFR_CB1		0x10
#define IFR_T2		0x20
#define IFR_T1		0x40
#define IFR_IRQB       	0x80	/* status of IRQB output pin */

/* Interrupt enable register (14) */
#define IER_CA2		IFR_CA2
#define IER_CA1		IFR_CA1
#define IER_SR		IFR_SR
#define IER_CB2		IFR_CB2
#define IER_CB1		IFR_CB1
#define IER_T2		IFR_T2
#define IER_T1		IFR_T1
#define IER_IRQB	IFR_IRQB

#endif /* _VIA6522REG_H_ */
