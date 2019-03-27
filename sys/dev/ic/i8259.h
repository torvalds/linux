/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Peter Wemm
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

/*
 * Register defintions for the i8259A programmable interrupt controller.
 */

#ifndef _DEV_IC_I8259_H_
#define	_DEV_IC_I8259_H_

/* Initialization control word 1. Written to even address. */
#define	ICW1_IC4	0x01		/* ICW4 present */
#define	ICW1_SNGL	0x02		/* 1 = single, 0 = cascaded */
#define	ICW1_ADI	0x04		/* 1 = 4, 0 = 8 byte vectors */
#define	ICW1_LTIM	0x08		/* 1 = level trigger, 0 = edge */
#define	ICW1_RESET	0x10		/* must be 1 */
/* 0x20 - 0x80 - in 8080/8085 mode only */

/* Initialization control word 2. Written to the odd address. */
/* No definitions, it is the base vector of the IDT for 8086 mode */

/* Initialization control word 3. Written to the odd address. */
/* For a master PIC, bitfield indicating a slave 8259 on given input */
/* For slave, lower 3 bits are the slave's ID binary id on master */

/* Initialization control word 4. Written to the odd address. */
#define	ICW4_8086	0x01		/* 1 = 8086, 0 = 8080 */
#define	ICW4_AEOI	0x02		/* 1 = Auto EOI */
#define	ICW4_MS		0x04		/* 1 = buffered master, 0 = slave */
#define	ICW4_BUF	0x08		/* 1 = enable buffer mode */
#define	ICW4_SFNM	0x10		/* 1 = special fully nested mode */

/* Operation control words.  Written after initialization. */

/* Operation control word type 1 */
/*
 * No definitions.  Written to the odd address.  Bitmask for interrupts.
 * 1 = disabled.
 */

/* Operation control word type 2.  Bit 3 (0x08) must be zero. Even address. */
#define	OCW2_L0		0x01		/* Level */
#define	OCW2_L1		0x02
#define	OCW2_L2		0x04
/* 0x08 must be 0 to select OCW2 vs OCW3 */
/* 0x10 must be 0 to select OCW2 vs ICW1 */
#define	OCW2_EOI	0x20		/* 1 = EOI */
#define	OCW2_SL		0x40		/* EOI mode */
#define	OCW2_R		0x80		/* EOI mode */

/* Operation control word type 3.  Bit 3 (0x08) must be set. Even address. */
#define	OCW3_RIS	0x01		/* 1 = read IS, 0 = read IR */
#define	OCW3_RR		0x02		/* register read */
#define	OCW3_P		0x04		/* poll mode command */
/* 0x08 must be 1 to select OCW3 vs OCW2 */
#define	OCW3_SEL	0x08		/* must be 1 */
/* 0x10 must be 0 to select OCW3 vs ICW1 */
#define	OCW3_SMM	0x20		/* special mode mask */
#define	OCW3_ESMM	0x40		/* enable SMM */

#endif /* !_DEV_IC_I8259_H_ */
