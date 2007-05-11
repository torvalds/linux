#ifndef _LINUX_BYTEORDER_SWAB_H
#define _LINUX_BYTEORDER_SWAB_H

/*
 * linux/byteorder/swab.h
 * Byte-swapping, independently from CPU endianness
 *	swabXX[ps]?(foo)
 *
 * Francois-Rene Rideau <fare@tunes.org> 19971205
 *    separated swab functions from cpu_to_XX,
 *    to clean up support for bizarre-endian architectures.
 *
 * Trent Piepho <xyzzy@speakeasy.org> 2007114
 *    make constant-folding work, provide C versions that
 *    gcc can optimize better, explain different versions
 *
 * See asm-i386/byteorder.h and suches for examples of how to provide
 * architecture-dependent optimized versions
 *
 */

#include <linux/compiler.h>

/* Functions/macros defined, there are a lot:
 *
 * ___swabXX
 *    Generic C versions of the swab functions.
 *
 * ___constant_swabXX
 *    C versions that gcc can fold into a compile-time constant when
 *    the argument is a compile-time constant.
 *
 * __arch__swabXX[sp]?
 *    Architecture optimized versions of all the swab functions
 *    (including the s and p versions).  These can be defined in
 *    asm-arch/byteorder.h.  Any which are not, are defined here.
 *    __arch__swabXXs() is defined in terms of __arch__swabXXp(), which
 *    is defined in terms of __arch__swabXX(), which is in turn defined
 *    in terms of ___swabXX(x).
 *    These must be macros.  They may be unsafe for arguments with
 *    side-effects.
 *
 * __fswabXX
 *    Inline function versions of the __arch__ macros.  These _are_ safe
 *    if the arguments have side-effects.  Note there are no s and p
 *    versions of these.
 *
 * __swabXX[sb]
 *    There are the ones you should actually use.  The __swabXX versions
 *    will be a constant given a constant argument and use the arch
 *    specific code (if any) for non-constant arguments.  The s and p
 *    versions always use the arch specific code (constant folding
 *    doesn't apply).  They are safe to use with arguments with
 *    side-effects.
 *
 * swabXX[sb]
 *    Nicknames for __swabXX[sb] to use in the kernel.
 */

/* casts are necessary for constants, because we never know how for sure
 * how U/UL/ULL map to __u16, __u32, __u64. At least not in a portable way.
 */

static __inline__ __attribute_const__ __u16 ___swab16(__u16 x)
{
	return x<<8 | x>>8;
}
static __inline__ __attribute_const__ __u32 ___swab32(__u32 x)
{
	return x<<24 | x>>24 |
		(x & (__u32)0x0000ff00UL)<<8 |
		(x & (__u32)0x00ff0000UL)>>8;
}
static __inline__ __attribute_const__ __u64 ___swab64(__u64 x)
{
	return x<<56 | x>>56 |
		(x & (__u64)0x000000000000ff00ULL)<<40 |
		(x & (__u64)0x0000000000ff0000ULL)<<24 |
		(x & (__u64)0x00000000ff000000ULL)<< 8 |
	        (x & (__u64)0x000000ff00000000ULL)>> 8 |
		(x & (__u64)0x0000ff0000000000ULL)>>24 |
		(x & (__u64)0x00ff000000000000ULL)>>40;
}

#define ___constant_swab16(x) \
	((__u16)( \
		(((__u16)(x) & (__u16)0x00ffU) << 8) | \
		(((__u16)(x) & (__u16)0xff00U) >> 8) ))
#define ___constant_swab32(x) \
	((__u32)( \
		(((__u32)(x) & (__u32)0x000000ffUL) << 24) | \
		(((__u32)(x) & (__u32)0x0000ff00UL) <<  8) | \
		(((__u32)(x) & (__u32)0x00ff0000UL) >>  8) | \
		(((__u32)(x) & (__u32)0xff000000UL) >> 24) ))
#define ___constant_swab64(x) \
	((__u64)( \
		(__u64)(((__u64)(x) & (__u64)0x00000000000000ffULL) << 56) | \
		(__u64)(((__u64)(x) & (__u64)0x000000000000ff00ULL) << 40) | \
		(__u64)(((__u64)(x) & (__u64)0x0000000000ff0000ULL) << 24) | \
		(__u64)(((__u64)(x) & (__u64)0x00000000ff000000ULL) <<  8) | \
	        (__u64)(((__u64)(x) & (__u64)0x000000ff00000000ULL) >>  8) | \
		(__u64)(((__u64)(x) & (__u64)0x0000ff0000000000ULL) >> 24) | \
		(__u64)(((__u64)(x) & (__u64)0x00ff000000000000ULL) >> 40) | \
		(__u64)(((__u64)(x) & (__u64)0xff00000000000000ULL) >> 56) ))

/*
 * provide defaults when no architecture-specific optimization is detected
 */
#ifndef __arch__swab16
#  define __arch__swab16(x) ___swab16(x)
#endif
#ifndef __arch__swab32
#  define __arch__swab32(x) ___swab32(x)
#endif
#ifndef __arch__swab64
#  define __arch__swab64(x) ___swab64(x)
#endif

#ifndef __arch__swab16p
#  define __arch__swab16p(x) __arch__swab16(*(x))
#endif
#ifndef __arch__swab32p
#  define __arch__swab32p(x) __arch__swab32(*(x))
#endif
#ifndef __arch__swab64p
#  define __arch__swab64p(x) __arch__swab64(*(x))
#endif

#ifndef __arch__swab16s
#  define __arch__swab16s(x) ((void)(*(x) = __arch__swab16p(x)))
#endif
#ifndef __arch__swab32s
#  define __arch__swab32s(x) ((void)(*(x) = __arch__swab32p(x)))
#endif
#ifndef __arch__swab64s
#  define __arch__swab64s(x) ((void)(*(x) = __arch__swab64p(x)))
#endif


/*
 * Allow constant folding
 */
#if defined(__GNUC__) && defined(__OPTIMIZE__)
#  define __swab16(x) \
(__builtin_constant_p((__u16)(x)) ? \
 ___constant_swab16((x)) : \
 __fswab16((x)))
#  define __swab32(x) \
(__builtin_constant_p((__u32)(x)) ? \
 ___constant_swab32((x)) : \
 __fswab32((x)))
#  define __swab64(x) \
(__builtin_constant_p((__u64)(x)) ? \
 ___constant_swab64((x)) : \
 __fswab64((x)))
#else
#  define __swab16(x) __fswab16(x)
#  define __swab32(x) __fswab32(x)
#  define __swab64(x) __fswab64(x)
#endif /* OPTIMIZE */


static __inline__ __attribute_const__ __u16 __fswab16(__u16 x)
{
	return __arch__swab16(x);
}
static __inline__ __u16 __swab16p(const __u16 *x)
{
	return __arch__swab16p(x);
}
static __inline__ void __swab16s(__u16 *addr)
{
	__arch__swab16s(addr);
}

static __inline__ __attribute_const__ __u32 __fswab32(__u32 x)
{
	return __arch__swab32(x);
}
static __inline__ __u32 __swab32p(const __u32 *x)
{
	return __arch__swab32p(x);
}
static __inline__ void __swab32s(__u32 *addr)
{
	__arch__swab32s(addr);
}

#ifdef __BYTEORDER_HAS_U64__
static __inline__ __attribute_const__ __u64 __fswab64(__u64 x)
{
#  ifdef __SWAB_64_THRU_32__
	__u32 h = x >> 32;
        __u32 l = x & ((1ULL<<32)-1);
        return (((__u64)__swab32(l)) << 32) | ((__u64)(__swab32(h)));
#  else
	return __arch__swab64(x);
#  endif
}
static __inline__ __u64 __swab64p(const __u64 *x)
{
	return __arch__swab64p(x);
}
static __inline__ void __swab64s(__u64 *addr)
{
	__arch__swab64s(addr);
}
#endif /* __BYTEORDER_HAS_U64__ */

#if defined(__KERNEL__)
#define swab16 __swab16
#define swab32 __swab32
#define swab64 __swab64
#define swab16p __swab16p
#define swab32p __swab32p
#define swab64p __swab64p
#define swab16s __swab16s
#define swab32s __swab32s
#define swab64s __swab64s
#endif

#endif /* _LINUX_BYTEORDER_SWAB_H */
