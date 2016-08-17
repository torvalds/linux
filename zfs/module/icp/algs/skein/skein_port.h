/*
 * Platform-specific definitions for Skein hash function.
 *
 * Source code author: Doug Whiting, 2008.
 *
 * This algorithm and source code is released to the public domain.
 *
 * Many thanks to Brian Gladman for his portable header files.
 *
 * To port Skein to an "unsupported" platform, change the definitions
 * in this file appropriately.
 */
/* Copyright 2013 Doug Whiting. This code is released to the public domain. */

#ifndef	_SKEIN_PORT_H_
#define	_SKEIN_PORT_H_

#include <sys/types.h>	/* get integer type definitions */
#include <sys/systm.h>	/* for bcopy() */

#ifndef	RotL_64
#define	RotL_64(x, N)	(((x) << (N)) | ((x) >> (64 - (N))))
#endif

/*
 * Skein is "natively" little-endian (unlike SHA-xxx), for optimal
 * performance on x86 CPUs. The Skein code requires the following
 * definitions for dealing with endianness:
 *
 *    SKEIN_NEED_SWAP:  0 for little-endian, 1 for big-endian
 *    Skein_Put64_LSB_First
 *    Skein_Get64_LSB_First
 *    Skein_Swap64
 *
 * If SKEIN_NEED_SWAP is defined at compile time, it is used here
 * along with the portable versions of Put64/Get64/Swap64, which
 * are slow in general.
 *
 * Otherwise, an "auto-detect" of endianness is attempted below.
 * If the default handling doesn't work well, the user may insert
 * platform-specific code instead (e.g., for big-endian CPUs).
 *
 */
#ifndef	SKEIN_NEED_SWAP		/* compile-time "override" for endianness? */

#include <sys/isa_defs.h>	/* get endianness selection */

#define	PLATFORM_MUST_ALIGN	_ALIGNMENT_REQUIRED
#if	defined(_BIG_ENDIAN)
/* here for big-endian CPUs */
#define	SKEIN_NEED_SWAP   (1)
#else
/* here for x86 and x86-64 CPUs (and other detected little-endian CPUs) */
#define	SKEIN_NEED_SWAP   (0)
#if	PLATFORM_MUST_ALIGN == 0	/* ok to use "fast" versions? */
#define	Skein_Put64_LSB_First(dst08, src64, bCnt) bcopy(src64, dst08, bCnt)
#define	Skein_Get64_LSB_First(dst64, src08, wCnt) \
	bcopy(src08, dst64, 8 * (wCnt))
#endif
#endif

#endif				/* ifndef SKEIN_NEED_SWAP */

/*
 * Provide any definitions still needed.
 */
#ifndef	Skein_Swap64	/* swap for big-endian, nop for little-endian */
#if	SKEIN_NEED_SWAP
#define	Skein_Swap64(w64)				\
	(((((uint64_t)(w64)) & 0xFF) << 56) |		\
	(((((uint64_t)(w64)) >> 8) & 0xFF) << 48) |	\
	(((((uint64_t)(w64)) >> 16) & 0xFF) << 40) |	\
	(((((uint64_t)(w64)) >> 24) & 0xFF) << 32) |	\
	(((((uint64_t)(w64)) >> 32) & 0xFF) << 24) |	\
	(((((uint64_t)(w64)) >> 40) & 0xFF) << 16) |	\
	(((((uint64_t)(w64)) >> 48) & 0xFF) << 8) |	\
	(((((uint64_t)(w64)) >> 56) & 0xFF)))
#else
#define	Skein_Swap64(w64)  (w64)
#endif
#endif				/* ifndef Skein_Swap64 */

#ifndef	Skein_Put64_LSB_First
void
Skein_Put64_LSB_First(uint8_t *dst, const uint64_t *src, size_t bCnt)
#ifdef	SKEIN_PORT_CODE		/* instantiate the function code here? */
{
	/*
	 * this version is fully portable (big-endian or little-endian),
	 * but slow
	 */
	size_t n;

	for (n = 0; n < bCnt; n++)
		dst[n] = (uint8_t)(src[n >> 3] >> (8 * (n & 7)));
}
#else
;				/* output only the function prototype */
#endif
#endif				/* ifndef Skein_Put64_LSB_First */

#ifndef	Skein_Get64_LSB_First
void
Skein_Get64_LSB_First(uint64_t *dst, const uint8_t *src, size_t wCnt)
#ifdef	SKEIN_PORT_CODE		/* instantiate the function code here? */
{
	/*
	 * this version is fully portable (big-endian or little-endian),
	 * but slow
	 */
	size_t n;

	for (n = 0; n < 8 * wCnt; n += 8)
		dst[n / 8] = (((uint64_t)src[n])) +
		    (((uint64_t)src[n + 1]) << 8) +
		    (((uint64_t)src[n + 2]) << 16) +
		    (((uint64_t)src[n + 3]) << 24) +
		    (((uint64_t)src[n + 4]) << 32) +
		    (((uint64_t)src[n + 5]) << 40) +
		    (((uint64_t)src[n + 6]) << 48) +
		    (((uint64_t)src[n + 7]) << 56);
}
#else
;				/* output only the function prototype */
#endif
#endif				/* ifndef Skein_Get64_LSB_First */

#endif	/* _SKEIN_PORT_H_ */
