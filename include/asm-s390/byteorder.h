#ifndef _S390_BYTEORDER_H
#define _S390_BYTEORDER_H

/*
 *  include/asm-s390/byteorder.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <asm/types.h>

#ifdef __GNUC__

#ifdef __s390x__
static __inline__ __u64 ___arch__swab64p(const __u64 *x)
{
	__u64 result;

	__asm__ __volatile__ (
		"   lrvg %0,%1"
		: "=d" (result) : "m" (*x) );
	return result;
}

static __inline__ __u64 ___arch__swab64(__u64 x)
{
	__u64 result;

	__asm__ __volatile__ (
		"   lrvgr %0,%1"
		: "=d" (result) : "d" (x) );
	return result;
}

static __inline__ void ___arch__swab64s(__u64 *x)
{
	*x = ___arch__swab64p(x);
}
#endif /* __s390x__ */

static __inline__ __u32 ___arch__swab32p(const __u32 *x)
{
	__u32 result;
	
	__asm__ __volatile__ (
#ifndef __s390x__
		"        icm   %0,8,3(%1)\n"
		"        icm   %0,4,2(%1)\n"
		"        icm   %0,2,1(%1)\n"
		"        ic    %0,0(%1)"
		: "=&d" (result) : "a" (x), "m" (*x) : "cc" );
#else /* __s390x__ */
		"   lrv  %0,%1"
		: "=d" (result) : "m" (*x) );
#endif /* __s390x__ */
	return result;
}

static __inline__ __u32 ___arch__swab32(__u32 x)
{
#ifndef __s390x__
	return ___arch__swab32p(&x);
#else /* __s390x__ */
	__u32 result;
	
	__asm__ __volatile__ (
		"   lrvr  %0,%1"
		: "=d" (result) : "d" (x) );
	return result;
#endif /* __s390x__ */
}

static __inline__ void ___arch__swab32s(__u32 *x)
{
	*x = ___arch__swab32p(x);
}

static __inline__ __u16 ___arch__swab16p(const __u16 *x)
{
	__u16 result;
	
	__asm__ __volatile__ (
#ifndef __s390x__
		"        icm   %0,2,1(%1)\n"
		"        ic    %0,0(%1)\n"
		: "=&d" (result) : "a" (x), "m" (*x) : "cc" );
#else /* __s390x__ */
		"   lrvh %0,%1"
		: "=d" (result) : "m" (*x) );
#endif /* __s390x__ */
	return result;
}

static __inline__ __u16 ___arch__swab16(__u16 x)
{
	return ___arch__swab16p(&x);
}

static __inline__ void ___arch__swab16s(__u16 *x)
{
	*x = ___arch__swab16p(x);
}

#ifdef __s390x__
#define __arch__swab64(x) ___arch__swab64(x)
#define __arch__swab64p(x) ___arch__swab64p(x)
#define __arch__swab64s(x) ___arch__swab64s(x)
#endif /* __s390x__ */
#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)
#define __arch__swab32p(x) ___arch__swab32p(x)
#define __arch__swab16p(x) ___arch__swab16p(x)
#define __arch__swab32s(x) ___arch__swab32s(x)
#define __arch__swab16s(x) ___arch__swab16s(x)

#ifndef __s390x__
#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif
#else /* __s390x__ */
#define __BYTEORDER_HAS_U64__
#endif /* __s390x__ */

#endif /* __GNUC__ */

#include <linux/byteorder/big_endian.h>

#endif /* _S390_BYTEORDER_H */
