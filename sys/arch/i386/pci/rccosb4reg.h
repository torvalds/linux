/*	$OpenBSD: rccosb4reg.h,v 1.3 2005/11/23 09:24:57 mickey Exp $	*/

/*
 * Copyright (c) 2004,2005 Michael Shalayeff
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
 * Register definitions for the RCC South Bridge interrupt controller.
 */

#define	OSB4_LEGAL_LINK(link)	((link) >= 0 && (link) <= 0x1f)

#define	OSB4_PIRQ_MASK		0xdefa
#define	OSB4_LEGAL_IRQ(irq)	((irq) > 0 && (irq) <= 15 &&		\
				 ((1 << (irq)) & OSB4_PIRQ_MASK) != 0)

/*
 * PCI Interrupts Address Index Register
 */
#define	OSB4_PIAIR	0xc00
#define	OSB4_PIRR	0xc01
#define	OSB4_PISP	3	/* special lines assumed to route thru ISA */

/*
 * ELCR - EDGE/LEVEL CONTROL REGISTER
 *
 * PCI I/O registers 0x4d0, 0x4d1
 */
#define	OSB4_REG_ELCR		0x4d0
#define	OSB4_REG_ELCR_SIZE	2
