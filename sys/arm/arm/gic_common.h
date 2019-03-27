/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * the sponsorship of the FreeBSD Foundation.
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

#ifndef _GIC_COMMON_H_
#define _GIC_COMMON_H_

#define	GIC_IVAR_HW_REV		500
#define	GIC_IVAR_BUS		501

/* GIC_IVAR_BUS values */
#define	GIC_BUS_UNKNOWN		0
#define	GIC_BUS_FDT		1
#define	GIC_BUS_ACPI		2
#define	GIC_BUS_MAX		2

__BUS_ACCESSOR(gic, hw_rev, GIC, HW_REV, u_int);
__BUS_ACCESSOR(gic, bus, GIC, BUS, u_int);

/* Software Generated Interrupts */
#define	GIC_FIRST_SGI		 0	/* Irqs 0-15 are SGIs/IPIs. */
#define	GIC_LAST_SGI		15
/* Private Peripheral Interrupts */
#define	GIC_FIRST_PPI		16	/* Irqs 16-31 are private (per */
#define	GIC_LAST_PPI		31	/* core) peripheral interrupts. */
/* Shared Peripheral Interrupts */
#define	GIC_FIRST_SPI		32	/* Irqs 32+ are shared peripherals. */

/* Common register values */
#define	GICD_CTLR		0x0000				/* v1 ICDDCR */
#define	GICD_TYPER		0x0004				/* v1 ICDICTR */
#define	 GICD_TYPER_I_NUM(n)	((((n) & 0x1F) + 1) * 32)
#define	GICD_IIDR		0x0008				/* v1 ICDIIDR */
#define	 GICD_IIDR_PROD_SHIFT	24
#define	 GICD_IIDR_PROD_MASK	0xff000000
#define	 GICD_IIDR_PROD(x)					\
    (((x) & GICD_IIDR_PROD_MASK) >> GICD_IIDR_PROD_SHIFT)
#define	 GICD_IIDR_VAR_SHIFT	16
#define	 GICD_IIDR_VAR_MASK	0x000f0000
#define	 GICD_IIDR_VAR(x)					\
    (((x) & GICD_IIDR_VAR_MASK) >> GICD_IIDR_VAR_SHIFT)
#define	 GICD_IIDR_REV_SHIFT	12
#define	 GICD_IIDR_REV_MASK	0x0000f000
#define	 GICD_IIDR_REV(x)					\
    (((x) & GICD_IIDR_REV_MASK) >> GICD_IIDR_REV_SHIFT)
#define	 GICD_IIDR_IMPL_SHIFT	0
#define	 GICD_IIDR_IMPL_MASK	0x00000fff
#define	 GICD_IIDR_IMPL(x)					\
    (((x) & GICD_IIDR_IMPL_MASK) >> GICD_IIDR_IMPL_SHIFT)
#define	GICD_IGROUPR(n)		(0x0080 + (((n) >> 5) * 4))	/* v1 ICDISER */
#define	 GICD_I_PER_IGROUPRn	32
#define	GICD_ISENABLER(n)	(0x0100 + (((n) >> 5) * 4))	/* v1 ICDISER */
#define	 GICD_I_MASK(n)		(1ul << ((n) & 0x1f))
#define	 GICD_I_PER_ISENABLERn	32
#define	GICD_ICENABLER(n)	(0x0180 + (((n) >> 5) * 4))	/* v1 ICDICER */
#define	GICD_ISPENDR(n)		(0x0200 + (((n) >> 5) * 4))	/* v1 ICDISPR */
#define	GICD_ICPENDR(n)		(0x0280 + (((n) >> 5) * 4))	/* v1 ICDICPR */
#define	GICD_ICACTIVER(n)	(0x0380 + (((n) >> 5) * 4))	/* v1 ICDABR */
#define	GICD_IPRIORITYR(n)	(0x0400 + (((n) >> 2) * 4))	/* v1 ICDIPR */
#define	 GICD_I_PER_IPRIORITYn	4
#define	GICD_ITARGETSR(n)	(0x0800 + (((n) >> 2) * 4))	/* v1 ICDIPTR */
#define	GICD_ICFGR(n)		(0x0C00 + (((n) >> 4) * 4))	/* v1 ICDICFR */
#define	 GICD_I_PER_ICFGRn	16
/* First bit is a polarity bit (0 - low, 1 - high) */
#define	 GICD_ICFGR_POL_LOW	(0 << 0)
#define	 GICD_ICFGR_POL_HIGH	(1 << 0)
#define	 GICD_ICFGR_POL_MASK	0x1
/* Second bit is a trigger bit (0 - level, 1 - edge) */
#define	 GICD_ICFGR_TRIG_LVL	(0 << 1)
#define	 GICD_ICFGR_TRIG_EDGE	(1 << 1)
#define	 GICD_ICFGR_TRIG_MASK	0x2
#define GICD_SGIR		0x0F00				/* v1 ICDSGIR */
#define	 GICD_SGI_TARGET_SHIFT	16

#endif /* _GIC_COMMON_H_ */
