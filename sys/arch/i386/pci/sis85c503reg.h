/*	$OpenBSD: sis85c503reg.h,v 1.3 2000/03/28 03:38:00 mickey Exp $	*/
/*	$NetBSD: sis85c503reg.h,v 1.1 1999/11/17 01:21:21 thorpej Exp $	*/

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
 * Register definitions for the SiS 85c503 PCI-ISA bridge interrupt controller.
 */

#define	SIS85C503_CFG_PIRQ_REGSTART	0x41	/* PCI configuration space */
#define	SIS85C503_CFG_PIRQ_REGEND	0x44

#define	SIS85C503_LEGAL_LINK(link) ((link) >= SIS85C503_CFG_PIRQ_REGSTART && \
				    (link) <= SIS85C503_CFG_PIRQ_REGEND)

#define	SIS85C503_CFG_PIRQ_REGOFS(regofs) (((regofs) >> 2) << 2)
#define	SIS85C503_CFG_PIRQ_SHIFT(regofs)				\
	(((regofs) - SIS85C503_CFG_PIRQ_REGOFS(regofs)) << 3)

#define	SIS85C503_CFG_PIRQ_MASK		0xff
#define	SIS85C503_CFG_PIRQ_INTR_MASK	0x0f

#define	SIS85C503_CFG_PIRQ_REG(reg, regofs)				\
	(((reg) >> SIS85C503_CFG_PIRQ_SHIFT(regofs)) & SIS85C503_CFG_PIRQ_MASK)

#define	SIS85C503_CFG_PIRQ_ROUTE_DISABLE 0x80

#define	SIS85C503_PIRQ_MASK		0xdef8
#define	SIS85C503_LEGAL_IRQ(irq)	((irq) >= 0 && (irq) <= 15 &&	\
				 ((1 << (irq)) & SIS85C503_PIRQ_MASK) != 0)
