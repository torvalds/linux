/*-
 * Copyright (c) 2010-2012 Aleksandr Rybalko
 * All rights reserved.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __FREEBSD_XZ_CONFIG_H__
#define __FREEBSD_XZ_CONFIG_H__

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/types.h>
#include <sys/systm.h>

#include <contrib/xz-embedded/linux/include/linux/xz.h>
#include "xz_malloc.h"

#define	XZ_PREBOOT	1

#undef	XZ_EXTERN
#define	XZ_EXTERN	extern

#undef	STATIC
#define	STATIC

#undef	INIT
#define	INIT

#undef	bool
#undef	true
#undef	false
#define	bool	int
#define	true	1
#define	false	0

#define	kmalloc(size, flags)	xz_malloc(size)
#define	kfree(ptr)		xz_free(ptr)
#define	vmalloc(size)		xz_malloc(size)
#define	vfree(ptr)		xz_free(ptr)

#define	memeq(a, b, size)	(memcmp((a), (b), (size)) == 0)
#define	memzero(buf, size)	bzero((buf), (size))

#ifndef min
#	define min(x, y)	MIN((x), (y))
#endif

#define	min_t(type, x, y)	min((x), (y))

#define	get_le32(ptr)	le32toh(*(const uint32_t *)(ptr))

#endif /* __FREEBSD_XZ_CONFIG_H__ */
