/*	$OpenBSD: opti82c558reg.h,v 1.3 2000/03/28 03:37:59 mickey Exp $	*/
/*	$NetBSD: opti82c558reg.h,v 1.1 1999/11/17 01:21:20 thorpej Exp $  */

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
 * Register definitions for the Opti 82c558 PCI-ISA bridge interrupt
 * controller.
 */

/*
 * PCI IRQ Select Register
 */

#define	VIPER_CFG_PIRQ		0x40	/* PCI configuration space */

/*
 * Trigger setting:
 *
 *	[1:7]=>5,9,10,11,12,14,15 Edge = 0 Level = 1
 */
#define	VIPER_CFG_TRIGGER_SHIFT	16

#define	VIPER_LEGAL_LINK(link)	((link) >= 0 && (link) <= 3)

#define	VIPER_PIRQ_MASK		0xde20
#define	VIPER_LEGAL_IRQ(irq)	((irq) >= 0 && (irq) <= 15 &&		\
				 ((1 << (irq)) & VIPER_PIRQ_MASK) != 0)

#define	VIPER_PIRQ_NONE		0
#define	VIPER_PIRQ_5		1
#define	VIPER_PIRQ_9		2
#define	VIPER_PIRQ_10		3
#define	VIPER_PIRQ_11		4
#define	VIPER_PIRQ_12		5
#define	VIPER_PIRQ_14		6
#define	VIPER_PIRQ_15		7

#define	VIPER_PIRQ_SELECT_MASK	0x07
#define	VIPER_PIRQ_SELECT_SHIFT	3

#define	VIPER_PIRQ(reg, x)	(((reg) >> ((x) * VIPER_PIRQ_SELECT_SHIFT)) \
				 & VIPER_PIRQ_SELECT_MASK)
