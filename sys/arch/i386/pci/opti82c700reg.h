/*	$OpenBSD: opti82c700reg.h,v 1.4 2000/08/08 19:12:47 mickey Exp $	*/
/*	$NetBSD: opti82c700reg.h,v 1.1 1999/11/17 01:21:20 thorpej Exp $	*/

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
 * Register definitions for the Opti 82c700 PCI-ISA bridge interrupt
 * controller.
 */

#define	FIRESTAR_CFG_INTR_IRQ	0xb0	/* PCI configuration space */
#define	FIRESTAR_CFG_INTR_PIRQ	0xb8	/* PCI configuration space */

#define	FIRESTAR_PIRQ_NONE	0
#define	FIRESTAR_PIRQ_MASK	0xdffa
#define	FIRESTAR_LEGAL_IRQ(irq)	((irq) >= 0 && (irq) <= 15 &&		\
				 ((1 << (irq)) & FIRESTAR_PIRQ_MASK) != 0)

#define	FIRESTAR_CFG_PIRQ_MASK	0x0f

#define	FIRESTAR_TRIGGER_MASK	0x01
#define	FIRESTAR_TRIGGER_SHIFT	4

/*
 * Opti's suggested Link values.
 */
#define	FIRESTAR_PIR_REGOFS_MASK	0x07
#define	FIRESTAR_PIR_REGOFS_SHIFT	4
#define	FIRESTAR_PIR_REGOFS(link)					\
	(((link) >> FIRESTAR_PIR_REGOFS_SHIFT) & FIRESTAR_PIR_REGOFS_MASK)

#define	FIRESTAR_PIR_SELECTSRC_MASK	0x07
#define	FIRESTAR_PIR_SELECTSRC_SHIFT	0
#define	FIRESTAR_PIR_SELECTSRC(link)					\
	(((link) >> FIRESTAR_PIR_SELECTSRC_SHIFT) & FIRESTAR_PIR_SELECTSRC_MASK)

#define	FIRESTAR_PIR_SELECT_NONE	0
#define	FIRESTAR_PIR_SELECT_IRQ		1
#define	FIRESTAR_PIR_SELECT_PIRQ	2
#define	FIRESTAR_PIR_SELECT_BRIDGE	3

#define	FIRESTAR_PIR_MAKELINK(src, ofs)					\
	(((src) << FIRESTAR_PIR_SELECTSRC_SHIFT) |			\
	 ((ofs) << FIRESTAR_PIR_REGOFS_SHIFT))
