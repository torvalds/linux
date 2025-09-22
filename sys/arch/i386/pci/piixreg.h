/*	$OpenBSD: piixreg.h,v 1.4 2003/03/28 23:12:33 mickey Exp $	*/
/*	$NetBSD: piixreg.h,v 1.1 1999/11/17 01:21:21 thorpej Exp $	*/

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
 * Register definitions for the Intel PIIX PCI-ISA bridge interrupt controller.
 */

/*
 * PIRQ[3:0]# - PIRQ ROUTE CONTROL REGISTERS
 *
 * PCI Configuration registers 0x60-0x63, 0x68-0x6b
 */

#define	PIIX_LEGAL_LINK(link)	((link) >= 0 && (link) <= 7)

#define	PIIX_PIRQ_MASK		0xdef8
#define	PIIX_LEGAL_IRQ(irq)	((irq) >= 0 && (irq) <= 15 &&		\
				 ((1 << (irq)) & PIIX_PIRQ_MASK) != 0)

#define	PIIX_CFG_PIRQ		0x60	/* PCI configuration space */
#define	PIIX_CFG_PIRQH		0x68
#define	PIIX_CFG_PIRQ_NONE	0x80
#define	PIIX_CFG_PIRQ_MASK	0x0f
#define	PIIX_PIRQ(reg, x)	(((reg) >> ((x) << 3)) & 0xff)

/*
 * ELCR - EDGE/LEVEL CONTROL REGISTER
 *
 * PCI I/O registers 0x4d0, 0x4d1
 */
#define	PIIX_REG_ELCR		0x4d0
#define	PIIX_REG_ELCR_SIZE	2
