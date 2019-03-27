/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
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

#ifndef _GIC_V3_REG_H_
#define	_GIC_V3_REG_H_

/*
 * Maximum number of interrupts
 * supported by GIC (including SGIs, PPIs and SPIs)
 */
#define	GIC_I_NUM_MAX		(1020)
/*
 * Priority MAX/MIN values
 */
#define	GIC_PRIORITY_MAX	(0x00UL)
/* Upper value is determined by LPI max priority */
#define	GIC_PRIORITY_MIN	(0xFCUL)

/* Numbers for shared peripheral interrupts */
#define	GIC_LAST_SPI		(1019)
/* Numbers for local peripheral interrupts */
#define	GIC_FIRST_LPI		(8192)

/*
 * Registers (v2/v3)
 */
/* GICD_CTLR */
#define	 GICD_CTLR_G1		(1 << 0)
#define	 GICD_CTLR_G1A		(1 << 1)
#define	 GICD_CTLR_ARE_NS	(1 << 4)
#define	 GICD_CTLR_RWP		(1 << 31)
/* GICD_TYPER */
#define	 GICD_TYPER_IDBITS(n)	((((n) >> 19) & 0x1F) + 1)

/*
 * Registers (v3)
 */
#define	GICD_IROUTER(n)		(0x6000 + ((n) * 8))

#define	GICD_PIDR4		0xFFD0
#define	GICD_PIDR5		0xFFD4
#define	GICD_PIDR6		0xFFD8
#define	GICD_PIDR7		0xFFDC
#define	GICD_PIDR0		0xFFE0
#define	GICD_PIDR1		0xFFE4
#define	GICD_PIDR2		0xFFE8

#define	GICR_PIDR2_ARCH_SHIFT	4
#define	GICR_PIDR2_ARCH_MASK	0xF0
#define	GICR_PIDR2_ARCH(x)				\
    (((x) & GICR_PIDR2_ARCH_MASK) >> GICR_PIDR2_ARCH_SHIFT)
#define	GICR_PIDR2_ARCH_GICv3	0x3
#define	GICR_PIDR2_ARCH_GICv4	0x4

#define	GICD_PIDR3		0xFFEC

/* Redistributor registers */
#define	GICR_CTLR		GICD_CTLR
#define		GICR_CTLR_LPI_ENABLE	(1 << 0)

#define	GICR_PIDR2		GICD_PIDR2

#define	GICR_TYPER		(0x0008)
#define	GICR_TYPER_PLPIS	(1 << 0)
#define	GICR_TYPER_VLPIS	(1 << 1)
#define	GICR_TYPER_LAST		(1 << 4)
#define	GICR_TYPER_CPUNUM_SHIFT	(8)
#define	GICR_TYPER_CPUNUM_MASK	(0xFFFUL << GICR_TYPER_CPUNUM_SHIFT)
#define	GICR_TYPER_CPUNUM(x)	\
	    (((x) & GICR_TYPER_CPUNUM_MASK) >> GICR_TYPER_CPUNUM_SHIFT)
#define	GICR_TYPER_AFF_SHIFT	(32)

#define	GICR_WAKER		(0x0014)
#define	GICR_WAKER_PS		(1 << 1) /* Processor sleep */
#define	GICR_WAKER_CA		(1 << 2) /* Children asleep */

#define	GICR_PROPBASER		(0x0070)
#define		GICR_PROPBASER_IDBITS_MASK	0x1FUL
/*
 * Cacheability
 * 0x0 - Device-nGnRnE
 * 0x1 - Normal Inner Non-cacheable
 * 0x2 - Normal Inner Read-allocate, Write-through
 * 0x3 - Normal Inner Read-allocate, Write-back
 * 0x4 - Normal Inner Write-allocate, Write-through
 * 0x5 - Normal Inner Write-allocate, Write-back
 * 0x6 - Normal Inner Read-allocate, Write-allocate, Write-through
 * 0x7 - Normal Inner Read-allocate, Write-allocate, Write-back
 */
#define		GICR_PROPBASER_CACHE_SHIFT	7
#define		GICR_PROPBASER_CACHE_DnGnRnE	0x0UL
#define		GICR_PROPBASER_CACHE_NIN	0x1UL
#define		GICR_PROPBASER_CACHE_NIRAWT	0x2UL
#define		GICR_PROPBASER_CACHE_NIRAWB	0x3UL
#define		GICR_PROPBASER_CACHE_NIWAWT	0x4UL
#define		GICR_PROPBASER_CACHE_NIWAWB	0x5UL
#define		GICR_PROPBASER_CACHE_NIRAWAWT	0x6UL
#define		GICR_PROPBASER_CACHE_NIRAWAWB	0x7UL
#define		GICR_PROPBASER_CACHE_MASK	\
		    (0x7UL << GICR_PROPBASER_CACHE_SHIFT)

/*
 * Shareability
 * 0x0 - Non-shareable
 * 0x1 - Inner-shareable
 * 0x2 - Outer-shareable
 * 0x3 - Reserved. Threated as 0x0
 */
#define		GICR_PROPBASER_SHARE_SHIFT	10
#define		GICR_PROPBASER_SHARE_NS		0x0UL
#define		GICR_PROPBASER_SHARE_IS		0x1UL
#define		GICR_PROPBASER_SHARE_OS		0x2UL
#define		GICR_PROPBASER_SHARE_RES	0x3UL
#define		GICR_PROPBASER_SHARE_MASK	\
		    (0x3UL << GICR_PROPBASER_SHARE_SHIFT)

#define	GICR_PENDBASER		(0x0078)
/*
 * Cacheability
 * 0x0 - Device-nGnRnE
 * 0x1 - Normal Inner Non-cacheable
 * 0x2 - Normal Inner Read-allocate, Write-through
 * 0x3 - Normal Inner Read-allocate, Write-back
 * 0x4 - Normal Inner Write-allocate, Write-through
 * 0x5 - Normal Inner Write-allocate, Write-back
 * 0x6 - Normal Inner Read-allocate, Write-allocate, Write-through
 * 0x7 - Normal Inner Read-allocate, Write-allocate, Write-back
 */
#define		GICR_PENDBASER_CACHE_SHIFT	7
#define		GICR_PENDBASER_CACHE_DnGnRnE	0x0UL
#define		GICR_PENDBASER_CACHE_NIN	0x1UL
#define		GICR_PENDBASER_CACHE_NIRAWT	0x2UL
#define		GICR_PENDBASER_CACHE_NIRAWB	0x3UL
#define		GICR_PENDBASER_CACHE_NIWAWT	0x4UL
#define		GICR_PENDBASER_CACHE_NIWAWB	0x5UL
#define		GICR_PENDBASER_CACHE_NIRAWAWT	0x6UL
#define		GICR_PENDBASER_CACHE_NIRAWAWB	0x7UL
#define		GICR_PENDBASER_CACHE_MASK	\
		    (0x7UL << GICR_PENDBASER_CACHE_SHIFT)

/*
 * Shareability
 * 0x0 - Non-shareable
 * 0x1 - Inner-shareable
 * 0x2 - Outer-shareable
 * 0x3 - Reserved. Threated as 0x0
 */
#define		GICR_PENDBASER_SHARE_SHIFT	10
#define		GICR_PENDBASER_SHARE_NS		0x0UL
#define		GICR_PENDBASER_SHARE_IS		0x1UL
#define		GICR_PENDBASER_SHARE_OS		0x2UL
#define		GICR_PENDBASER_SHARE_RES	0x3UL
#define		GICR_PENDBASER_SHARE_MASK	\
		    (0x3UL << GICR_PENDBASER_SHARE_SHIFT)

/* Re-distributor registers for SGIs and PPIs */
#define	GICR_RD_BASE_SIZE	PAGE_SIZE_64K
#define	GICR_SGI_BASE_SIZE	PAGE_SIZE_64K
#define	GICR_VLPI_BASE_SIZE	PAGE_SIZE_64K
#define	GICR_RESERVED_SIZE	PAGE_SIZE_64K

#define	GICR_IGROUPR0				(0x0080)
#define	GICR_ISENABLER0				(0x0100)
#define	GICR_ICENABLER0				(0x0180)
#define		GICR_I_ENABLER_SGI_MASK		(0x0000FFFF)
#define		GICR_I_ENABLER_PPI_MASK		(0xFFFF0000)

#define		GICR_I_PER_IPRIORITYn		(GICD_I_PER_IPRIORITYn)

/* ITS registers */
#define	GITS_PIDR2		GICR_PIDR2
#define	GITS_PIDR2_ARCH_MASK	GICR_PIDR2_ARCH_MASK
#define	GITS_PIDR2_ARCH_GICv3	GICR_PIDR2_ARCH_GICv3
#define	GITS_PIDR2_ARCH_GICv4	GICR_PIDR2_ARCH_GICv4

#define	GITS_CTLR		(0x0000)
#define		GITS_CTLR_EN	(1 << 0)

#define	GITS_IIDR		(0x0004)
#define	 GITS_IIDR_PRODUCT_SHIFT	24
#define	 GITS_IIDR_PRODUCT_MASK		(0xff << GITS_IIDR_PRODUCT_SHIFT)
#define	 GITS_IIDR_VARIANT_SHIFT	16
#define	 GITS_IIDR_VARIANT_MASK		(0xf << GITS_IIDR_VARIANT_SHIFT)
#define	 GITS_IIDR_REVISION_SHIFT	12
#define	 GITS_IIDR_REVISION_MASK	(0xf << GITS_IIDR_REVISION_SHIFT)
#define	 GITS_IIDR_IMPLEMENTOR_SHIFT	0
#define	 GITS_IIDR_IMPLEMENTOR_MASK	(0xfff << GITS_IIDR_IMPLEMENTOR_SHIFT)

#define	 GITS_IIDR_RAW(impl, prod, var, rev)		\
    ((prod) << GITS_IIDR_PRODUCT_SHIFT |		\
     (var) << GITS_IIDR_VARIANT_SHIFT | 		\
     (rev) << GITS_IIDR_REVISION_SHIFT |		\
     (impl) << GITS_IIDR_IMPLEMENTOR_SHIFT)

#define	 GITS_IIDR_IMPL_CAVIUM		(0x34c)
#define	 GITS_IIDR_PROD_THUNDER		(0xa1)
#define	 GITS_IIDR_VAR_THUNDER_1	(0x0)

#define	GITS_CBASER		(0x0080)
#define		GITS_CBASER_VALID	(1UL << 63)
/*
 * Cacheability
 * 0x0 - Device-nGnRnE
 * 0x1 - Normal Inner Non-cacheable
 * 0x2 - Normal Inner Read-allocate, Write-through
 * 0x3 - Normal Inner Read-allocate, Write-back
 * 0x4 - Normal Inner Write-allocate, Write-through
 * 0x5 - Normal Inner Write-allocate, Write-back
 * 0x6 - Normal Inner Read-allocate, Write-allocate, Write-through
 * 0x7 - Normal Inner Read-allocate, Write-allocate, Write-back
 */
#define		GITS_CBASER_CACHE_SHIFT		59
#define		GITS_CBASER_CACHE_DnGnRnE	0x0UL
#define		GITS_CBASER_CACHE_NIN		0x1UL
#define		GITS_CBASER_CACHE_NIRAWT	0x2UL
#define		GITS_CBASER_CACHE_NIRAWB	0x3UL
#define		GITS_CBASER_CACHE_NIWAWT	0x4UL
#define		GITS_CBASER_CACHE_NIWAWB	0x5UL
#define		GITS_CBASER_CACHE_NIRAWAWT	0x6UL
#define		GITS_CBASER_CACHE_NIRAWAWB	0x7UL
#define		GITS_CBASER_CACHE_MASK	(0x7UL << GITS_CBASER_CACHE_SHIFT)
/*
 * Shareability
 * 0x0 - Non-shareable
 * 0x1 - Inner-shareable
 * 0x2 - Outer-shareable
 * 0x3 - Reserved. Threated as 0x0
 */
#define		GITS_CBASER_SHARE_SHIFT		10
#define		GITS_CBASER_SHARE_NS		0x0UL
#define		GITS_CBASER_SHARE_IS		0x1UL
#define		GITS_CBASER_SHARE_OS		0x2UL
#define		GITS_CBASER_SHARE_RES		0x3UL
#define		GITS_CBASER_SHARE_MASK		\
		    (0x3UL << GITS_CBASER_SHARE_SHIFT)

#define		GITS_CBASER_PA_SHIFT	12
#define		GITS_CBASER_PA_MASK	(0xFFFFFFFFFUL << GITS_CBASER_PA_SHIFT)

#define	GITS_CWRITER		(0x0088)
#define	GITS_CREADR		(0x0090)

#define	GITS_BASER_BASE		(0x0100)
#define	GITS_BASER(x)		(GITS_BASER_BASE + (x) * 8)

#define		GITS_BASER_VALID	(1UL << 63)

#define		GITS_BASER_TYPE_SHIFT	56
#define		GITS_BASER_TYPE(x)	\
		    (((x) & GITS_BASER_TYPE_MASK) >> GITS_BASER_TYPE_SHIFT)
#define		GITS_BASER_TYPE_UNIMPL	0x0UL	/* Unimplemented */
#define		GITS_BASER_TYPE_DEV	0x1UL	/* Devices */
#define		GITS_BASER_TYPE_VP	0x2UL	/* Virtual Processors */
#define		GITS_BASER_TYPE_PP	0x3UL	/* Physical Processors */
#define		GITS_BASER_TYPE_IC	0x4UL	/* Interrupt Collections */
#define		GITS_BASER_TYPE_RES5	0x5UL	/* Reserved */
#define		GITS_BASER_TYPE_RES6	0x6UL	/* Reserved */
#define		GITS_BASER_TYPE_RES7	0x7UL	/* Reserved */
#define		GITS_BASER_TYPE_MASK	(0x7UL << GITS_BASER_TYPE_SHIFT)
/*
 * Cacheability
 * 0x0 - Non-cacheable, non-bufferable
 * 0x1 - Non-cacheable
 * 0x2 - Read-allocate, Write-through
 * 0x3 - Read-allocate, Write-back
 * 0x4 - Write-allocate, Write-through
 * 0x5 - Write-allocate, Write-back
 * 0x6 - Read-allocate, Write-allocate, Write-through
 * 0x7 - Read-allocate, Write-allocate, Write-back
 */
#define		GITS_BASER_CACHE_SHIFT	59
#define		GITS_BASER_CACHE_NCNB	0x0UL
#define		GITS_BASER_CACHE_NC	0x1UL
#define		GITS_BASER_CACHE_RAWT	0x2UL
#define		GITS_BASER_CACHE_RAWB	0x3UL
#define		GITS_BASER_CACHE_WAWT	0x4UL
#define		GITS_BASER_CACHE_WAWB	0x5UL
#define		GITS_BASER_CACHE_RAWAWT	0x6UL
#define		GITS_BASER_CACHE_RAWAWB	0x7UL
#define		GITS_BASER_CACHE_MASK	(0x7UL << GITS_BASER_CACHE_SHIFT)

#define		GITS_BASER_ESIZE_SHIFT	48
#define		GITS_BASER_ESIZE_MASK	(0x1FUL << GITS_BASER_ESIZE_SHIFT)
#define		GITS_BASER_ESIZE(x)	\
		    ((((x) & GITS_BASER_ESIZE_MASK) >> GITS_BASER_ESIZE_SHIFT) + 1)

#define		GITS_BASER_PA_SHIFT	12
#define		GITS_BASER_PA_MASK	(0xFFFFFFFFFUL << GITS_BASER_PA_SHIFT)

/*
 * Shareability
 * 0x0 - Non-shareable
 * 0x1 - Inner-shareable
 * 0x2 - Outer-shareable
 * 0x3 - Reserved. Threated as 0x0
 */
#define		GITS_BASER_SHARE_SHIFT	10
#define		GITS_BASER_SHARE_NS	0x0UL
#define		GITS_BASER_SHARE_IS	0x1UL
#define		GITS_BASER_SHARE_OS	0x2UL
#define		GITS_BASER_SHARE_RES	0x3UL
#define		GITS_BASER_SHARE_MASK	(0x3UL << GITS_BASER_SHARE_SHIFT)

#define		GITS_BASER_PSZ_SHIFT	8
#define		GITS_BASER_PSZ_4K	0x0UL
#define		GITS_BASER_PSZ_16K	0x1UL
#define		GITS_BASER_PSZ_64K	0x2UL
#define		GITS_BASER_PSZ_MASK	(0x3UL << GITS_BASER_PSZ_SHIFT)

#define		GITS_BASER_SIZE_MASK	0xFFUL

#define		GITS_BASER_NUM		8

#define	GITS_TYPER		(0x0008)
#define		GITS_TYPER_PTA		(1UL << 19)
#define		GITS_TYPER_DEVB_SHIFT	13
#define		GITS_TYPER_DEVB_MASK	(0x1FUL << GITS_TYPER_DEVB_SHIFT)
/* Number of device identifiers implemented */
#define		GITS_TYPER_DEVB(x)	\
		    ((((x) & GITS_TYPER_DEVB_MASK) >> GITS_TYPER_DEVB_SHIFT) + 1)
#define		GITS_TYPER_ITTES_SHIFT	4
#define		GITS_TYPER_ITTES_MASK	(0xFUL << GITS_TYPER_ITTES_SHIFT)
/* Number of bytes per ITT Entry */
#define		GITS_TYPER_ITTES(x)	\
		    ((((x) & GITS_TYPER_ITTES_MASK) >> GITS_TYPER_ITTES_SHIFT) + 1)

#define	GITS_TRANSLATER		(0x10040)
/*
 * LPI related
 */
#define		LPI_CONF_PRIO_MASK	(0xFC)
#define		LPI_CONF_GROUP1		(1 << 1)
#define		LPI_CONF_ENABLE		(1 << 0)

/*
 * CPU interface
 */

/*
 * Registers list (ICC_xyz_EL1):
 *
 * PMR     - Priority Mask Register
 *		* interrupts of priority higher than specified
 *		  in this mask will be signalled to the CPU.
 *		  (0xff - lowest possible prio., 0x00 - highest prio.)
 *
 * CTLR    - Control Register
 *		* controls behavior of the CPU interface and displays
 *		  implemented features.
 *
 * IGRPEN1 - Interrupt Group 1 Enable Register
 *
 * IAR1    - Interrupt Acknowledge Register Group 1
 *		* contains number of the highest priority pending
 *		  interrupt from the Group 1.
 *
 * EOIR1   - End of Interrupt Register Group 1
 *		* Writes inform CPU interface about completed Group 1
 *		  interrupts processing.
 */

#define	gic_icc_write(reg, val)					\
do {								\
	WRITE_SPECIALREG(ICC_ ##reg ##_EL1, val);		\
	isb();							\
} while (0)

#define	gic_icc_read(reg)					\
({								\
	uint64_t val;						\
								\
	val = READ_SPECIALREG(ICC_ ##reg ##_EL1);		\
	(val);							\
})

#define	gic_icc_set(reg, mask)					\
do {								\
	uint64_t val;						\
	val = gic_icc_read(reg);				\
	val |= (mask);						\
	gic_icc_write(reg, val);				\
} while (0)

#define	gic_icc_clear(reg, mask)				\
do {								\
	uint64_t val;						\
	val = gic_icc_read(reg);				\
	val &= ~(mask);						\
	gic_icc_write(reg, val);				\
} while (0)

#endif /* _GIC_V3_REG_H_ */
