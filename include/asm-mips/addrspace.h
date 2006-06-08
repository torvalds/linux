/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 99 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999 by Silicon Graphics, Inc.
 */
#ifndef _ASM_ADDRSPACE_H
#define _ASM_ADDRSPACE_H

#include <linux/config.h>
#include <spaces.h>

/*
 *  Configure language
 */
#ifdef __ASSEMBLY__
#define _ATYPE_
#define _ATYPE32_
#define _ATYPE64_
#define _LLCONST_(x)	x
#else
#define _ATYPE_		__PTRDIFF_TYPE__
#define _ATYPE32_	int
#define _ATYPE64_	long long
#define _LLCONST_(x)	x ## LL
#endif

/*
 *  32-bit MIPS address spaces
 */
#ifdef __ASSEMBLY__
#define _ACAST32_
#define _ACAST64_
#else
#define _ACAST32_		(_ATYPE_)(_ATYPE32_)	/* widen if necessary */
#define _ACAST64_		(_ATYPE64_)		/* do _not_ narrow */
#endif

/*
 * Returns the kernel segment base of a given address
 */
#define KSEGX(a)		((_ACAST32_ (a)) & 0xe0000000)

/*
 * Returns the physical address of a CKSEGx / XKPHYS address
 */
#define CPHYSADDR(a)		((_ACAST32_(a)) & 0x1fffffff)
#define XPHYSADDR(a)            ((_ACAST64_(a)) &			\
				 _LLCONST_(0x000000ffffffffff))

#ifdef CONFIG_64BIT

/*
 * Memory segments (64bit kernel mode addresses)
 * The compatibility segments use the full 64-bit sign extended value.  Note
 * the R8000 doesn't have them so don't reference these in generic MIPS code.
 */
#define XKUSEG			_LLCONST_(0x0000000000000000)
#define XKSSEG			_LLCONST_(0x4000000000000000)
#define XKPHYS			_LLCONST_(0x8000000000000000)
#define XKSEG			_LLCONST_(0xc000000000000000)
#define CKSEG0			_LLCONST_(0xffffffff80000000)
#define CKSEG1			_LLCONST_(0xffffffffa0000000)
#define CKSSEG			_LLCONST_(0xffffffffc0000000)
#define CKSEG3			_LLCONST_(0xffffffffe0000000)

#define CKSEG0ADDR(a)		(CPHYSADDR(a) | CKSEG0)
#define CKSEG1ADDR(a)		(CPHYSADDR(a) | CKSEG1)
#define CKSEG2ADDR(a)		(CPHYSADDR(a) | CKSEG2)
#define CKSEG3ADDR(a)		(CPHYSADDR(a) | CKSEG3)

#else

#define CKSEG0ADDR(a)		(CPHYSADDR(a) | KSEG0)
#define CKSEG1ADDR(a)		(CPHYSADDR(a) | KSEG1)
#define CKSEG2ADDR(a)		(CPHYSADDR(a) | KSEG2)
#define CKSEG3ADDR(a)		(CPHYSADDR(a) | KSEG3)

/*
 * Map an address to a certain kernel segment
 */
#define KSEG0ADDR(a)		(CPHYSADDR(a) | KSEG0)
#define KSEG1ADDR(a)		(CPHYSADDR(a) | KSEG1)
#define KSEG2ADDR(a)		(CPHYSADDR(a) | KSEG2)
#define KSEG3ADDR(a)		(CPHYSADDR(a) | KSEG3)

/*
 * Memory segments (32bit kernel mode addresses)
 * These are the traditional names used in the 32-bit universe.
 */
#define KUSEG			0x00000000
#define KSEG0			0x80000000
#define KSEG1			0xa0000000
#define KSEG2			0xc0000000
#define KSEG3			0xe0000000

#define CKUSEG			0x00000000
#define CKSEG0			0x80000000
#define CKSEG1			0xa0000000
#define CKSEG2			0xc0000000
#define CKSEG3			0xe0000000

#endif

/*
 * Cache modes for XKPHYS address conversion macros
 */
#define K_CALG_COH_EXCL1_NOL2	0
#define K_CALG_COH_SHRL1_NOL2	1
#define K_CALG_UNCACHED		2
#define K_CALG_NONCOHERENT	3
#define K_CALG_COH_EXCL		4
#define K_CALG_COH_SHAREABLE	5
#define K_CALG_NOTUSED		6
#define K_CALG_UNCACHED_ACCEL	7

/*
 * 64-bit address conversions
 */
#define PHYS_TO_XKSEG_UNCACHED(p)	PHYS_TO_XKPHYS(K_CALG_UNCACHED,(p))
#define PHYS_TO_XKSEG_CACHED(p)		PHYS_TO_XKPHYS(K_CALG_COH_SHAREABLE,(p))
#define XKPHYS_TO_PHYS(p)		((p) & TO_PHYS_MASK)
#define PHYS_TO_XKPHYS(cm,a)		(_LLCONST_(0x8000000000000000) | \
					 ((cm)<<59) | (a))

#if defined (CONFIG_CPU_R4300)						\
    || defined (CONFIG_CPU_R4X00)					\
    || defined (CONFIG_CPU_R5000)					\
    || defined (CONFIG_CPU_RM7000)					\
    || defined (CONFIG_CPU_NEVADA)					\
    || defined (CONFIG_CPU_TX49XX)					\
    || defined (CONFIG_CPU_MIPS64)
#define KUSIZE		_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define KUSIZE_64	_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define K0SIZE		_LLCONST_(0x0000001000000000)	/* 2^^36 */
#define K1SIZE		_LLCONST_(0x0000001000000000)	/* 2^^36 */
#define K2SIZE		_LLCONST_(0x000000ff80000000)
#define KSEGSIZE	_LLCONST_(0x000000ff80000000)	/* max syssegsz */
#define TO_PHYS_MASK	_LLCONST_(0x0000000fffffffff)	/* 2^^36 - 1 */
#endif

#if defined (CONFIG_CPU_R8000)
/* We keep KUSIZE consistent with R4000 for now (2^^40) instead of (2^^48) */
#define KUSIZE		_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define KUSIZE_64	_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define K0SIZE		_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define K1SIZE		_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define K2SIZE		_LLCONST_(0x0001000000000000)
#define KSEGSIZE	_LLCONST_(0x0000010000000000)	/* max syssegsz */
#define TO_PHYS_MASK	_LLCONST_(0x000000ffffffffff)	/* 2^^40 - 1 */
#endif

#if defined (CONFIG_CPU_R10000)
#define KUSIZE		_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define KUSIZE_64	_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define K0SIZE		_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define K1SIZE		_LLCONST_(0x0000010000000000)	/* 2^^40 */
#define K2SIZE		_LLCONST_(0x00000fff80000000)
#define KSEGSIZE	_LLCONST_(0x00000fff80000000)	/* max syssegsz */
#define TO_PHYS_MASK	_LLCONST_(0x000000ffffffffff)	/* 2^^40 - 1 */
#endif

#if defined(CONFIG_CPU_SB1) || defined(CONFIG_CPU_SB1A)
#define KUSIZE		_LLCONST_(0x0000100000000000)	/* 2^^44 */
#define KUSIZE_64	_LLCONST_(0x0000100000000000)	/* 2^^44 */
#define K0SIZE		_LLCONST_(0x0000100000000000)	/* 2^^44 */
#define K1SIZE		_LLCONST_(0x0000100000000000)	/* 2^^44 */
#define K2SIZE		_LLCONST_(0x0000ffff80000000)
#define KSEGSIZE	_LLCONST_(0x0000ffff80000000)	/* max syssegsz */
#define TO_PHYS_MASK	_LLCONST_(0x00000fffffffffff)	/* 2^^44 - 1 */
#endif

/*
 * Further names for SGI source compatibility.  These are stolen from
 * IRIX's <sys/mips_addrspace.h>.
 */
#define KUBASE		_LLCONST_(0)
#define KUSIZE_32	_LLCONST_(0x0000000080000000)	/* KUSIZE
							   for a 32 bit proc */
#define K0BASE_EXL_WR	_LLCONST_(0xa800000000000000)	/* exclusive on write */
#define K0BASE_NONCOH	_LLCONST_(0x9800000000000000)	/* noncoherent */
#define K0BASE_EXL	_LLCONST_(0xa000000000000000)	/* exclusive */

#ifndef CONFIG_CPU_R8000

/*
 * The R8000 doesn't have the 32-bit compat spaces so we don't define them
 * in order to catch bugs in the source code.
 */

#define COMPAT_K1BASE32		_LLCONST_(0xffffffffa0000000)
#define PHYS_TO_COMPATK1(x)	((x) | COMPAT_K1BASE32) /* 32-bit compat k1 */

#endif

#define KDM_TO_PHYS(x)		(_ACAST64_ (x) & TO_PHYS_MASK)
#define PHYS_TO_K0(x)		(_ACAST64_ (x) | CAC_BASE)

#endif /* _ASM_ADDRSPACE_H */
