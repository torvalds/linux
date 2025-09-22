/*	$OpenBSD: via82c586reg.h,v 1.5 2001/06/08 03:18:04 mickey Exp $	*/
/*	$NetBSD: via82c586reg.h,v 1.2 2000/04/22 15:00:41 uch Exp $	*/

/*
 * Copyright (c) 1999, by UCHIYAMA Yasushi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * Register definitions for the VIA 82c586 PCI-ISA bridge interrupt controller.
 */

#define	VP3_CFG_PIRQ_REG		0x54	/* PCI configuration space */
#define	VP3_CFG_KBDMISCCTRL12_REG	0x44
#define	VP3_CFG_IDEMISCCTRL3_REG	0x48

#define	VP3_CFG_MISCCTRL2_SHIFT		24
#define	VP3_CFG_MISCCTRL2_MASK		0x0f
#define	VP3_CFG_MISCCTRL2_EISA4D04D1PORT_ENABLE 0x20
#define	VP3_CFG_MISCCTRL2_REG(reg)					\
	(((reg) >> VP3_CFG_MISCCTRL2_SHIFT) & VP3_CFG_MISCCTRL2_MASK)

#define	VP3_CFG_TRIGGER_LEVEL		0
#define	VP3_CFG_TRIGGER_EDGE		1

#define	VP3_CFG_TRIGGER_MASK		0x01
#define	VP3_CFG_TRIGGER_SHIFT_PIRQA	3
#define	VP3_CFG_TRIGGER_SHIFT_PIRQB	2
#define	VP3_CFG_TRIGGER_SHIFT_PIRQC	1
#define	VP3_CFG_TRIGGER_SHIFT_PIRQD	0

#define	VP3_CFG_INTR_MASK		0x0f
#define	VP3_PIRQ_MASK			0xdefa

#define	VP3_CFG_INTR_SHIFT_PIRQA	0x14
#define	VP3_CFG_INTR_SHIFT_PIRQB	0x10
#define	VP3_CFG_INTR_SHIFT_PIRQC	0x1c
#define	VP3_CFG_INTR_SHIFT_PIRQD	0x0c
#define	VP3_CFG_INTR_SHIFT_PIRQ0	0x10
#define	VP3_CFG_INTR_SHIFT_PIRQ1	0x08
#define	VP3_CFG_INTR_SHIFT_PIRQ2	0x00

#define	VP3_PIRQ_NONE			0
#define	VP3_LEGAL_LINK(link)		((link) >= 0 && (link) <= 6)
#define	VP3_LEGAL_IRQ(irq)		((irq) >= 0 && (irq) <= 15 &&	\
					 ((1 << (irq)) & VP3_PIRQ_MASK) != 0)
