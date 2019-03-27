/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_SYSMACROS_H
#define	_SYS_SYSMACROS_H

#include <sys/param.h>
#include <sys/isa_defs.h>
#if defined(__FreeBSD__) && defined(_KERNEL)
#include <sys/libkern.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Some macros for units conversion
 */
/*
 * Disk blocks (sectors) and bytes.
 */
#define	dtob(DD)	((DD) << DEV_BSHIFT)
#define	btod(BB)	(((BB) + DEV_BSIZE - 1) >> DEV_BSHIFT)
#define	btodt(BB)	((BB) >> DEV_BSHIFT)
#define	lbtod(BB)	(((offset_t)(BB) + DEV_BSIZE - 1) >> DEV_BSHIFT)

/* common macros */
#ifndef MIN
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)	((a) < (b) ? (b) : (a))
#endif
#ifndef ABS
#define	ABS(a)		((a) < 0 ? -(a) : (a))
#endif
#ifndef	SIGNOF
#define	SIGNOF(a)	((a) < 0 ? -1 : (a) > 0)
#endif

#ifdef _KERNEL

/*
 * Convert a single byte to/from binary-coded decimal (BCD).
 */
extern unsigned char byte_to_bcd[256];
extern unsigned char bcd_to_byte[256];

#define	BYTE_TO_BCD(x)	byte_to_bcd[(x) & 0xff]
#define	BCD_TO_BYTE(x)	bcd_to_byte[(x) & 0xff]

#endif	/* _KERNEL */

/*
 * WARNING: The device number macros defined here should not be used by device
 * drivers or user software. Device drivers should use the device functions
 * defined in the DDI/DKI interface (see also ddi.h). Application software
 * should make use of the library routines available in makedev(3). A set of
 * new device macros are provided to operate on the expanded device number
 * format supported in SVR4. Macro versions of the DDI device functions are
 * provided for use by kernel proper routines only. Macro routines bmajor(),
 * major(), minor(), emajor(), eminor(), and makedev() will be removed or
 * their definitions changed at the next major release following SVR4.
 */

#define	O_BITSMAJOR	7	/* # of SVR3 major device bits */
#define	O_BITSMINOR	8	/* # of SVR3 minor device bits */
#define	O_MAXMAJ	0x7f	/* SVR3 max major value */
#define	O_MAXMIN	0xff	/* SVR3 max minor value */


#define	L_BITSMAJOR32	14	/* # of SVR4 major device bits */
#define	L_BITSMINOR32	18	/* # of SVR4 minor device bits */
#define	L_MAXMAJ32	0x3fff	/* SVR4 max major value */
#define	L_MAXMIN32	0x3ffff	/* MAX minor for 3b2 software drivers. */
				/* For 3b2 hardware devices the minor is */
				/* restricted to 256 (0-255) */

#ifdef _LP64
#define	L_BITSMAJOR	32	/* # of major device bits in 64-bit Solaris */
#define	L_BITSMINOR	32	/* # of minor device bits in 64-bit Solaris */
#define	L_MAXMAJ	0xfffffffful	/* max major value */
#define	L_MAXMIN	0xfffffffful	/* max minor value */
#else
#define	L_BITSMAJOR	L_BITSMAJOR32
#define	L_BITSMINOR	L_BITSMINOR32
#define	L_MAXMAJ	L_MAXMAJ32
#define	L_MAXMIN	L_MAXMIN32
#endif

#ifdef illumos
#ifdef _KERNEL

/* major part of a device internal to the kernel */

#define	major(x)	(major_t)((((unsigned)(x)) >> O_BITSMINOR) & O_MAXMAJ)
#define	bmajor(x)	(major_t)((((unsigned)(x)) >> O_BITSMINOR) & O_MAXMAJ)

/* get internal major part of expanded device number */

#define	getmajor(x)	(major_t)((((dev_t)(x)) >> L_BITSMINOR) & L_MAXMAJ)

/* minor part of a device internal to the kernel */

#define	minor(x)	(minor_t)((x) & O_MAXMIN)

/* get internal minor part of expanded device number */

#define	getminor(x)	(minor_t)((x) & L_MAXMIN)

#else

/* major part of a device external from the kernel (same as emajor below) */

#define	major(x)	(major_t)((((unsigned)(x)) >> O_BITSMINOR) & O_MAXMAJ)

/* minor part of a device external from the kernel  (same as eminor below) */

#define	minor(x)	(minor_t)((x) & O_MAXMIN)

#endif	/* _KERNEL */

/* create old device number */

#define	makedev(x, y) (unsigned short)(((x) << O_BITSMINOR) | ((y) & O_MAXMIN))

/* make an new device number */

#define	makedevice(x, y) (dev_t)(((dev_t)(x) << L_BITSMINOR) | ((y) & L_MAXMIN))


/*
 * emajor() allows kernel/driver code to print external major numbers
 * eminor() allows kernel/driver code to print external minor numbers
 */

#define	emajor(x) \
	(major_t)(((unsigned int)(x) >> O_BITSMINOR) > O_MAXMAJ) ? \
	    NODEV : (((unsigned int)(x) >> O_BITSMINOR) & O_MAXMAJ)

#define	eminor(x) \
	(minor_t)((x) & O_MAXMIN)

/*
 * get external major and minor device
 * components from expanded device number
 */
#define	getemajor(x)	(major_t)((((dev_t)(x) >> L_BITSMINOR) > L_MAXMAJ) ? \
			    NODEV : (((dev_t)(x) >> L_BITSMINOR) & L_MAXMAJ))
#define	geteminor(x)	(minor_t)((x) & L_MAXMIN)
#endif /* illumos */

/*
 * These are versions of the kernel routines for compressing and
 * expanding long device numbers that don't return errors.
 */
#if (L_BITSMAJOR32 == L_BITSMAJOR) && (L_BITSMINOR32 == L_BITSMINOR)

#define	DEVCMPL(x)	(x)
#define	DEVEXPL(x)	(x)

#else

#define	DEVCMPL(x)	\
	(dev32_t)((((x) >> L_BITSMINOR) > L_MAXMAJ32 || \
	    ((x) & L_MAXMIN) > L_MAXMIN32) ? NODEV32 : \
	    ((((x) >> L_BITSMINOR) << L_BITSMINOR32) | ((x) & L_MAXMIN32)))

#define	DEVEXPL(x)	\
	(((x) == NODEV32) ? NODEV : \
	makedevice(((x) >> L_BITSMINOR32) & L_MAXMAJ32, (x) & L_MAXMIN32))

#endif /* L_BITSMAJOR32 ... */

/* convert to old (SVR3.2) dev format */

#define	cmpdev(x) \
	(o_dev_t)((((x) >> L_BITSMINOR) > O_MAXMAJ || \
	    ((x) & L_MAXMIN) > O_MAXMIN) ? NODEV : \
	    ((((x) >> L_BITSMINOR) << O_BITSMINOR) | ((x) & O_MAXMIN)))

/* convert to new (SVR4) dev format */

#define	expdev(x) \
	(dev_t)(((dev_t)(((x) >> O_BITSMINOR) & O_MAXMAJ) << L_BITSMINOR) | \
	    ((x) & O_MAXMIN))

/*
 * Macro for checking power of 2 address alignment.
 */
#define	IS_P2ALIGNED(v, a) ((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)

/*
 * Macros for counting and rounding.
 */
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#define	roundup(x, y)	((((x)+((y)-1))/(y))*(y))

/*
 * Macro to determine if value is a power of 2
 */
#define	ISP2(x)		(((x) & ((x) - 1)) == 0)

/*
 * Macros for various sorts of alignment and rounding.  The "align" must
 * be a power of 2.  Often times it is a block, sector, or page.
 */

/*
 * return x rounded down to an align boundary
 * eg, P2ALIGN(1200, 1024) == 1024 (1*align)
 * eg, P2ALIGN(1024, 1024) == 1024 (1*align)
 * eg, P2ALIGN(0x1234, 0x100) == 0x1200 (0x12*align)
 * eg, P2ALIGN(0x5600, 0x100) == 0x5600 (0x56*align)
 */
#define	P2ALIGN(x, align)		((x) & -(align))

/*
 * return x % (mod) align
 * eg, P2PHASE(0x1234, 0x100) == 0x34 (x-0x12*align)
 * eg, P2PHASE(0x5600, 0x100) == 0x00 (x-0x56*align)
 */
#define	P2PHASE(x, align)		((x) & ((align) - 1))

/*
 * return how much space is left in this block (but if it's perfectly
 * aligned, return 0).
 * eg, P2NPHASE(0x1234, 0x100) == 0xcc (0x13*align-x)
 * eg, P2NPHASE(0x5600, 0x100) == 0x00 (0x56*align-x)
 */
#define	P2NPHASE(x, align)		(-(x) & ((align) - 1))

/*
 * return x rounded up to an align boundary
 * eg, P2ROUNDUP(0x1234, 0x100) == 0x1300 (0x13*align)
 * eg, P2ROUNDUP(0x5600, 0x100) == 0x5600 (0x56*align)
 */
#define	P2ROUNDUP(x, align)		(-(-(x) & -(align)))

/*
 * return the ending address of the block that x is in
 * eg, P2END(0x1234, 0x100) == 0x12ff (0x13*align - 1)
 * eg, P2END(0x5600, 0x100) == 0x56ff (0x57*align - 1)
 */
#define	P2END(x, align)			(-(~(x) & -(align)))

/*
 * return x rounded up to the next phase (offset) within align.
 * phase should be < align.
 * eg, P2PHASEUP(0x1234, 0x100, 0x10) == 0x1310 (0x13*align + phase)
 * eg, P2PHASEUP(0x5600, 0x100, 0x10) == 0x5610 (0x56*align + phase)
 */
#define	P2PHASEUP(x, align, phase)	((phase) - (((phase) - (x)) & -(align)))

/*
 * return TRUE if adding len to off would cause it to cross an align
 * boundary.
 * eg, P2BOUNDARY(0x1234, 0xe0, 0x100) == TRUE (0x1234 + 0xe0 == 0x1314)
 * eg, P2BOUNDARY(0x1234, 0x50, 0x100) == FALSE (0x1234 + 0x50 == 0x1284)
 */
#define	P2BOUNDARY(off, len, align) \
	(((off) ^ ((off) + (len) - 1)) > (align) - 1)

/*
 * Return TRUE if they have the same highest bit set.
 * eg, P2SAMEHIGHBIT(0x1234, 0x1001) == TRUE (the high bit is 0x1000)
 * eg, P2SAMEHIGHBIT(0x1234, 0x3010) == FALSE (high bit of 0x3010 is 0x2000)
 */
#define	P2SAMEHIGHBIT(x, y)		(((x) ^ (y)) < ((x) & (y)))

/*
 * Typed version of the P2* macros.  These macros should be used to ensure
 * that the result is correctly calculated based on the data type of (x),
 * which is passed in as the last argument, regardless of the data
 * type of the alignment.  For example, if (x) is of type uint64_t,
 * and we want to round it up to a page boundary using "PAGESIZE" as
 * the alignment, we can do either
 *	P2ROUNDUP(x, (uint64_t)PAGESIZE)
 * or
 *	P2ROUNDUP_TYPED(x, PAGESIZE, uint64_t)
 */
#define	P2ALIGN_TYPED(x, align, type)	\
	((type)(x) & -(type)(align))
#define	P2PHASE_TYPED(x, align, type)	\
	((type)(x) & ((type)(align) - 1))
#define	P2NPHASE_TYPED(x, align, type)	\
	(-(type)(x) & ((type)(align) - 1))
#define	P2ROUNDUP_TYPED(x, align, type)	\
	(-(-(type)(x) & -(type)(align)))
#define	P2END_TYPED(x, align, type)	\
	(-(~(type)(x) & -(type)(align)))
#define	P2PHASEUP_TYPED(x, align, phase, type)	\
	((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define	P2CROSS_TYPED(x, y, align, type)	\
	(((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define	P2SAMEHIGHBIT_TYPED(x, y, type) \
	(((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

/*
 * Macros to atomically increment/decrement a variable.  mutex and var
 * must be pointers.
 */
#define	INCR_COUNT(var, mutex) mutex_enter(mutex), (*(var))++, mutex_exit(mutex)
#define	DECR_COUNT(var, mutex) mutex_enter(mutex), (*(var))--, mutex_exit(mutex)

/*
 * Macros to declare bitfields - the order in the parameter list is
 * Low to High - that is, declare bit 0 first.  We only support 8-bit bitfields
 * because if a field crosses a byte boundary it's not likely to be meaningful
 * without reassembly in its nonnative endianness.
 */
#if defined(_BIT_FIELDS_LTOH)
#define	DECL_BITFIELD2(_a, _b)				\
	uint8_t _a, _b
#define	DECL_BITFIELD3(_a, _b, _c)			\
	uint8_t _a, _b, _c
#define	DECL_BITFIELD4(_a, _b, _c, _d)			\
	uint8_t _a, _b, _c, _d
#define	DECL_BITFIELD5(_a, _b, _c, _d, _e)		\
	uint8_t _a, _b, _c, _d, _e
#define	DECL_BITFIELD6(_a, _b, _c, _d, _e, _f)		\
	uint8_t _a, _b, _c, _d, _e, _f
#define	DECL_BITFIELD7(_a, _b, _c, _d, _e, _f, _g)	\
	uint8_t _a, _b, _c, _d, _e, _f, _g
#define	DECL_BITFIELD8(_a, _b, _c, _d, _e, _f, _g, _h)	\
	uint8_t _a, _b, _c, _d, _e, _f, _g, _h
#elif defined(_BIT_FIELDS_HTOL)
#define	DECL_BITFIELD2(_a, _b)				\
	uint8_t _b, _a
#define	DECL_BITFIELD3(_a, _b, _c)			\
	uint8_t _c, _b, _a
#define	DECL_BITFIELD4(_a, _b, _c, _d)			\
	uint8_t _d, _c, _b, _a
#define	DECL_BITFIELD5(_a, _b, _c, _d, _e)		\
	uint8_t _e, _d, _c, _b, _a
#define	DECL_BITFIELD6(_a, _b, _c, _d, _e, _f)		\
	uint8_t _f, _e, _d, _c, _b, _a
#define	DECL_BITFIELD7(_a, _b, _c, _d, _e, _f, _g)	\
	uint8_t _g, _f, _e, _d, _c, _b, _a
#define	DECL_BITFIELD8(_a, _b, _c, _d, _e, _f, _g, _h)	\
	uint8_t _h, _g, _f, _e, _d, _c, _b, _a
#else
#error	One of _BIT_FIELDS_LTOH or _BIT_FIELDS_HTOL must be defined
#endif  /* _BIT_FIELDS_LTOH */

#if defined(_KERNEL) && !defined(_KMEMUSER) && !defined(offsetof)

/* avoid any possibility of clashing with <stddef.h> version */

#define	offsetof(s, m)	((size_t)(&(((s *)0)->m)))
#endif

/*
 * Find highest one bit set.
 *      Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * High order bit is 31 (or 63 in _LP64 kernel).
 */
static __inline int
highbit(ulong_t i)
{
#if defined(__FreeBSD__) && defined(_KERNEL) && defined(HAVE_INLINE_FLSL)
	return (flsl(i));
#else
	int h = 1;

	if (i == 0)
		return (0);
#ifdef _LP64
	if (i & 0xffffffff00000000ul) {
		h += 32; i >>= 32;
	}
#endif
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
#endif
}

/*
 * Find highest one bit set.
 *	Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 */
static __inline int
highbit64(uint64_t i)
{
#if defined(__FreeBSD__) && defined(_KERNEL) && defined(HAVE_INLINE_FLSLL)
	return (flsll(i));
#else
	int h = 1;

	if (i == 0)
		return (0);
	if (i & 0xffffffff00000000ULL) {
		h += 32; i >>= 32;
	}
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
#endif
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SYSMACROS_H */
