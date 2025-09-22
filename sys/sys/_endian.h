/*	$OpenBSD: _endian.h,v 1.8 2018/01/11 23:13:37 dlg Exp $	*/

/*-
 * Copyright (c) 1997 Niklas Hallqvist.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Internal endianness macros.  This pulls in <machine/endian.h> to
 * get the correct setting direction for the platform and sets internal
 * ('__' prefix) macros appropriately.
 */

#ifndef _SYS__ENDIAN_H_
#define _SYS__ENDIAN_H_

#include <sys/_types.h>

#define __FROM_SYS__ENDIAN
#include <machine/endian.h>
#undef __FROM_SYS__ENDIAN

#define _LITTLE_ENDIAN	1234
#define _BIG_ENDIAN	4321
#define _PDP_ENDIAN	3412

/* Note that these macros evaluate their arguments several times.  */

#define __swap16gen(x)							\
    (__uint16_t)(((__uint16_t)(x) & 0xffU) << 8 | ((__uint16_t)(x) & 0xff00U) >> 8)

#define __swap32gen(x)							\
    (__uint32_t)(((__uint32_t)(x) & 0xff) << 24 |			\
    ((__uint32_t)(x) & 0xff00) << 8 | ((__uint32_t)(x) & 0xff0000) >> 8 |\
    ((__uint32_t)(x) & 0xff000000) >> 24)

#define __swap64gen(x)							\
	(__uint64_t)((((__uint64_t)(x) & 0xff) << 56) |			\
	    ((__uint64_t)(x) & 0xff00ULL) << 40 |			\
	    ((__uint64_t)(x) & 0xff0000ULL) << 24 |			\
	    ((__uint64_t)(x) & 0xff000000ULL) << 8 |			\
	    ((__uint64_t)(x) & 0xff00000000ULL) >> 8 |			\
	    ((__uint64_t)(x) & 0xff0000000000ULL) >> 24 |		\
	    ((__uint64_t)(x) & 0xff000000000000ULL) >> 40 |		\
	    ((__uint64_t)(x) & 0xff00000000000000ULL) >> 56)

#ifndef __HAVE_MD_SWAP
static __inline __uint16_t
__swap16md(__uint16_t x)
{
	return (__swap16gen(x));
}

static __inline __uint32_t
__swap32md(__uint32_t x)
{
	return (__swap32gen(x));
}

static __inline __uint64_t
__swap64md(__uint64_t x)
{
	return (__swap64gen(x));
}
#endif

#define __swap16(x)							\
	(__uint16_t)(__builtin_constant_p(x) ? __swap16gen(x) : __swap16md(x))
#define __swap32(x)							\
	(__uint32_t)(__builtin_constant_p(x) ? __swap32gen(x) : __swap32md(x))
#define __swap64(x)							\
	(__uint64_t)(__builtin_constant_p(x) ? __swap64gen(x) : __swap64md(x))

#if _BYTE_ORDER == _LITTLE_ENDIAN

#define _QUAD_HIGHWORD 1
#define _QUAD_LOWWORD 0

#define __htobe16	__swap16
#define __htobe32	__swap32
#define __htobe64	__swap64
#define __htole16(x)	((__uint16_t)(x))
#define __htole32(x)	((__uint32_t)(x))
#define __htole64(x)	((__uint64_t)(x))

#ifdef _KERNEL
#ifdef __HAVE_MD_SWAPIO

#define __bemtoh16(_x) __mswap16(_x)
#define __bemtoh32(_x) __mswap32(_x)
#define __bemtoh64(_x) __mswap64(_x)

#define __htobem16(_x, _v) __swapm16((_x), (_v))
#define __htobem32(_x, _v) __swapm32((_x), (_v))
#define __htobem64(_x, _v) __swapm64((_x), (_v))

#endif /* __HAVE_MD_SWAPIO */
#endif /* _KERNEL */
#endif /* _BYTE_ORDER == _LITTLE_ENDIAN */

#if _BYTE_ORDER == _BIG_ENDIAN

#define _QUAD_HIGHWORD 0
#define _QUAD_LOWWORD 1

#define __htobe16(x)	((__uint16_t)(x))
#define __htobe32(x)	((__uint32_t)(x))
#define __htobe64(x)	((__uint64_t)(x))
#define __htole16	__swap16
#define __htole32	__swap32
#define __htole64	__swap64

#ifdef _KERNEL
#ifdef __HAVE_MD_SWAPIO

#define __lemtoh16(_x) __mswap16(_x)
#define __lemtoh32(_x) __mswap32(_x)
#define __lemtoh64(_x) __mswap64(_x)

#define __htolem16(_x, _v) __swapm16((_x), (_v))
#define __htolem32(_x, _v) __swapm32((_x), (_v))
#define __htolem64(_x, _v) __swapm64((_x), (_v))

#endif /* __HAVE_MD_SWAPIO */
#endif /* _KERNEL */
#endif /* _BYTE_ORDER == _BIG_ENDIAN */


#ifdef _KERNEL
/*
 * Fill in the __hto[bl]em{16,32,64} and __[bl]emtoh{16,32,64} macros
 * that haven't been defined yet
 */

#ifndef __bemtoh16
#define __bemtoh16(_x)		__htobe16(*(__uint16_t *)(_x))
#define __bemtoh32(_x)		__htobe32(*(__uint32_t *)(_x))
#define __bemtoh64(_x)		__htobe64(*(__uint64_t *)(_x))
#endif

#ifndef __htobem16
#define __htobem16(_x, _v)	(*(__uint16_t *)(_x) = __htobe16(_v))
#define __htobem32(_x, _v)	(*(__uint32_t *)(_x) = __htobe32(_v))
#define __htobem64(_x, _v)	(*(__uint64_t *)(_x) = __htobe64(_v))
#endif

#ifndef __lemtoh16
#define __lemtoh16(_x)		__htole16(*(__uint16_t *)(_x))
#define __lemtoh32(_x)		__htole32(*(__uint32_t *)(_x))
#define __lemtoh64(_x)		__htole64(*(__uint64_t *)(_x))
#endif

#ifndef __htolem16
#define __htolem16(_x, _v)	(*(__uint16_t *)(_x) = __htole16(_v))
#define __htolem32(_x, _v)	(*(__uint32_t *)(_x) = __htole32(_v))
#define __htolem64(_x, _v)	(*(__uint64_t *)(_x) = __htole64(_v))
#endif
#endif /* _KERNEL */

#endif /* _SYS__ENDIAN_H_ */
