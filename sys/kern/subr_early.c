/*-
 * Copyright (c) 2018 The FreeBSD Foundation
 *
 * This software was developed by Mateusz Guzik <mjg@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <machine/cpu.h>

#ifndef	MEMSET_EARLY_FUNC
#define	MEMSET_EARLY_FUNC	memset
#else
void *MEMSET_EARLY_FUNC(void *, int, size_t);
#endif

void *
memset_early(void *buf, int c, size_t len)
{

	return (MEMSET_EARLY_FUNC(buf, c, len));
}

#ifndef	MEMCPY_EARLY_FUNC
#define	MEMCPY_EARLY_FUNC	memcpy
#else
void *MEMCPY_EARLY_FUNC(void *, const void *, size_t);
#endif

void *
memcpy_early(void *to, const void *from, size_t len)
{

	return (MEMCPY_EARLY_FUNC(to, from, len));
}

#ifndef	MEMMOVE_EARLY_FUNC
#define	MEMMOVE_EARLY_FUNC	memmove
#else
void *MEMMOVE_EARLY_FUNC(void *, const void *, size_t);
#endif

void *
memmove_early(void *to, const void *from, size_t len)
{

	return (MEMMOVE_EARLY_FUNC(to, from, len));
}
