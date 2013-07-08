/*
 * lz4defs.h -- architecture specific defines
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Detects 64 bits mode
 */
#if (defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) \
	|| defined(__ppc64__) || defined(__LP64__))
#define LZ4_ARCH64 1
#else
#define LZ4_ARCH64 0
#endif

/*
 * Architecture-specific macros
 */
#define BYTE	u8
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)		\
	|| defined(CONFIG_ARM) && __LINUX_ARM_ARCH__ >= 6	\
	&& defined(ARM_EFFICIENT_UNALIGNED_ACCESS)
typedef struct _U32_S { u32 v; } U32_S;
typedef struct _U64_S { u64 v; } U64_S;

#define A32(x) (((U32_S *)(x))->v)
#define A64(x) (((U64_S *)(x))->v)

#define PUT4(s, d) (A32(d) = A32(s))
#define PUT8(s, d) (A64(d) = A64(s))
#else /* CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS */

#define PUT4(s, d) \
	put_unaligned(get_unaligned((const u32 *) s), (u32 *) d)
#define PUT8(s, d) \
	put_unaligned(get_unaligned((const u64 *) s), (u64 *) d)
#endif

#define COPYLENGTH 8
#define ML_BITS  4
#define ML_MASK  ((1U << ML_BITS) - 1)
#define RUN_BITS (8 - ML_BITS)
#define RUN_MASK ((1U << RUN_BITS) - 1)

#if LZ4_ARCH64/* 64-bit */
#define STEPSIZE 8

#define LZ4_COPYSTEP(s, d)	\
	do {			\
		PUT8(s, d);	\
		d += 8;		\
		s += 8;		\
	} while (0)

#define LZ4_COPYPACKET(s, d)	LZ4_COPYSTEP(s, d)

#define LZ4_SECURECOPY(s, d, e)			\
	do {					\
		if (d < e) {			\
			LZ4_WILDCOPY(s, d, e);	\
		}				\
	} while (0)

#else	/* 32-bit */
#define STEPSIZE 4

#define LZ4_COPYSTEP(s, d)	\
	do {			\
		PUT4(s, d);	\
		d += 4;		\
		s += 4;		\
	} while (0)

#define LZ4_COPYPACKET(s, d)		\
	do {				\
		LZ4_COPYSTEP(s, d);	\
		LZ4_COPYSTEP(s, d);	\
	} while (0)

#define LZ4_SECURECOPY	LZ4_WILDCOPY
#endif

#define LZ4_READ_LITTLEENDIAN_16(d, s, p) \
	(d = s - get_unaligned_le16(p))

#define LZ4_WILDCOPY(s, d, e)		\
	do {				\
		LZ4_COPYPACKET(s, d);	\
	} while (d < e)
