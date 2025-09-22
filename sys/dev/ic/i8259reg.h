/*	$NetBSD: i8259reg.h,v 1.4 2008/04/28 20:23:50 martin Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _DEV_IC_I8259REG_H_
#define	_DEV_IC_I8259REG_H_

/*
 * Register definitions for the Intel i8259 Programmable Interrupt
 * Controller.
 *
 * XXX More bits should be filled in, here, as this was taken from
 * XXX the Intel PIIX4 manual.  Someone with a real 8259 data sheet
 * XXX should fill them in.
 */

/*
 * Note a write to ICW1 starts an initialization cycle, and must be
 * followed by writes to ICW2, ICW3, and ICW4.
 */
#define	PIC_ICW1	0x00	/* Initialization Command Word 1 (w) */
#define	ICW1_IC4	(1U << 0)	/* ICW4 Write Required */
#define	ICW1_SNGL	(1U << 1)	/* 1 == single, 0 == cascade */
#define	ICW1_ADI	(1U << 2)	/* CALL address interval */
#define	ICW1_LTIM	(1U << 3)	/* 1 == intrs are level trigger */
#define	ICW1_SELECT	(1U << 4)	/* select ICW1 */
#define	ICW1_IVA(x)	((x) << 5)	/* interrupt vector address (MCS-80) */

#define	PIC_ICW2	0x01	/* Initialization Command Word 2 (w) */
#define	ICW2_VECTOR(x)	((x) & 0xf8)	/* vector base address */
#define	ICW2_IRL(x)	((x) << 0)	/* interrupt request level */

#define	PIC_ICW3	0x01	/* Initialization Command Word 3 (w) */
#define	ICW3_CASCADE(x)	(1U << (x))	/* cascaded mode enable */
#define	ICW3_SIC(x)	((x) << 0)	/* slave identification code */

#define	PIC_ICW4	0x01	/* Initialization Command Word 4 (w) */
#define	ICW4_8086	(1U << 0)	/* 8086 mode */
#define	ICW4_AEOI	(1U << 1)	/* automatic end-of-interrupt */
#define	ICW4_BUFM	(1U << 2)	/* buffered mode master */
#define	ICW4_BUF	(1U << 3)	/* buffered mode */
#define	ICW4_SFNM	(1U << 4)	/* special fully nested mode */

/*
 * After an initialization sequence, you get to access the OCWs.
 */
#define	PIC_OCW1	0x01	/* Operational Control Word 1 (r/w) */
#define	OCW1_IRM(x)	(1U << (x))	/* interrupt request mask */

#define	PIC_OCW2	0x00	/* Operational Control Word 2 (w) */
#define	OCW2_SELECT	(0)		/* select OCW2 */
#define	OCW2_EOI	(1U << 5)	/* EOI */
#define	OCW2_SL		(1U << 6)	/* specific */
#define	OCW2_R		(1U << 7)	/* rotate */
#define	OCW2_ILS(x)	((x) << 0)	/* interrupt level select */

#define	PIC_OCW3	0x00	/* Operational Control Word 3 (r/w) */
#define	OCW3_SSMM	(1U << 6)	/* set special mask mode */
#define	OCW3_SMM	(1U << 5)	/* 1 = enable smm, 0 = disable */
#define	OCW3_SELECT	(1U << 3)	/* select OCW3 */
#define	OCW3_POLL	(1U << 2)	/* poll mode command */
#define	OCW3_RR		(1U << 1)	/* register read */
#define	OCW3_RIS	(1U << 0)	/* 1 = read IS, 0 = read IR */

#define	OCW3_POLL_IRQ(x) ((x) & 0x7f)
#define	OCW3_POLL_PENDING (1U << 7)

#endif /* _DEV_IC_I8259REG_H_ */
