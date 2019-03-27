/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

/* Registers definition for Freescale i.MX515 Generic Periodic Timer */

#define	IMX_GPT_CR	0x0000 /* Control Register          R/W */
#define		GPT_CR_FO3		(1U << 31)
#define		GPT_CR_FO2		(1 << 30)
#define		GPT_CR_FO1		(1 << 29)
#define		GPT_CR_OM3_SHIFT	26
#define		GPT_CR_OM3_MASK		0x1c000000
#define		GPT_CR_OM2_SHIFT	23
#define		GPT_CR_OM2_MASK		0x03800000
#define		GPT_CR_OM1_SHIFT	20
#define		GPT_CR_OM1_MASK		0x00700000
#define		GPT_CR_OMX_NONE		0
#define		GPT_CR_OMX_TOGGLE	1
#define		GPT_CR_OMX_CLEAR	2
#define		GPT_CR_OMX_SET		3
#define		GPT_CR_OMX_PULSE	4 /* Run CLKSRC on output pin */
#define		GPT_CR_IM2_SHIFT	18
#define		GPT_CR_IM2_MASK		0x000c0000
#define		GPT_CR_IM1_SHIFT	16
#define		GPT_CR_IM1_MASK		0x00030000
#define		GPT_CR_IMX_NONE		0
#define		GPT_CR_IMX_REDGE	1
#define		GPT_CR_IMX_FEDGE	2
#define		GPT_CR_IMX_BOTH		3
#define		GPT_CR_SWR		(1 << 15)
#define		GPT_CR_24MEN		(1 << 10)
#define		GPT_CR_FRR		(1 << 9)
#define		GPT_CR_CLKSRC_NONE	(0 << 6)
#define		GPT_CR_CLKSRC_IPG	(1 << 6)
#define		GPT_CR_CLKSRC_IPG_HIGH	(2 << 6)
#define		GPT_CR_CLKSRC_EXT	(3 << 6)
#define		GPT_CR_CLKSRC_32K	(4 << 6)
#define		GPT_CR_CLKSRC_24M	(5 << 6)
#define		GPT_CR_STOPEN		(1 << 5)
#define		GPT_CR_DOZEEN		(1 << 4)
#define		GPT_CR_WAITEN		(1 << 3)
#define		GPT_CR_DBGEN		(1 << 2)
#define		GPT_CR_ENMOD		(1 << 1)
#define		GPT_CR_EN		(1 << 0)

#define	IMX_GPT_PR	0x0004 /* Prescaler Register        R/W */
#define		GPT_PR_VALUE_SHIFT	0
#define		GPT_PR_VALUE_MASK	0x00000fff
#define		GPT_PR_VALUE_SHIFT_24M	12
#define		GPT_PR_VALUE_MASK_24M	0x0000f000

/* Same map for SR and IR */
#define	IMX_GPT_SR	0x0008 /* Status Register           R/W */
#define	IMX_GPT_IR	0x000c /* Interrupt Register        R/W */
#define		GPT_IR_ROV		(1 << 5)
#define		GPT_IR_IF2		(1 << 4)
#define		GPT_IR_IF1		(1 << 3)
#define		GPT_IR_OF3		(1 << 2)
#define		GPT_IR_OF2		(1 << 1)
#define		GPT_IR_OF1		(1 << 0)
#define		GPT_IR_ALL		\
			(GPT_IR_ROV |	\
			GPT_IR_IF2 |	\
			GPT_IR_IF1 |	\
			GPT_IR_OF3 |	\
			GPT_IR_OF2 |	\
			GPT_IR_OF1)

#define	IMX_GPT_OCR1	0x0010 /* Output Compare Register 1 R/W */
#define	IMX_GPT_OCR2	0x0014 /* Output Compare Register 2 R/W */
#define	IMX_GPT_OCR3	0x0018 /* Output Compare Register 3 R/W */
#define	IMX_GPT_ICR1	0x001c /* Input capture Register 1  RO */
#define	IMX_GPT_ICR2	0x0020 /* Input capture Register 2  RO */
#define	IMX_GPT_CNT	0x0024 /* Counter Register          RO */
