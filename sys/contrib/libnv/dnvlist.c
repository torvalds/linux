/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>

#include <machine/stdarg.h>

#else
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#endif

#include <sys/dnv.h>
#include <sys/nv.h>

#include "nv_impl.h"

#define	DNVLIST_GET(ftype, type)					\
ftype									\
dnvlist_get_##type(const nvlist_t *nvl, const char *name, ftype defval)	\
{									\
									\
	if (nvlist_exists_##type(nvl, name))				\
		return (nvlist_get_##type(nvl, name));			\
	else								\
		return (defval);					\
}

DNVLIST_GET(bool, bool)
DNVLIST_GET(uint64_t, number)
DNVLIST_GET(const char *, string)
DNVLIST_GET(const nvlist_t *, nvlist)
#ifndef _KERNEL
DNVLIST_GET(int, descriptor)
#endif

#undef	DNVLIST_GET

const void *
dnvlist_get_binary(const nvlist_t *nvl, const char *name, size_t *sizep,
    const void *defval, size_t defsize)
{
	const void *value;

	if (nvlist_exists_binary(nvl, name))
		value = nvlist_get_binary(nvl, name, sizep);
	else {
		if (sizep != NULL)
			*sizep = defsize;
		value = defval;
	}
	return (value);
}

#define	DNVLIST_TAKE(ftype, type)					\
ftype									\
dnvlist_take_##type(nvlist_t *nvl, const char *name, ftype defval)	\
{									\
									\
	if (nvlist_exists_##type(nvl, name))				\
		return (nvlist_take_##type(nvl, name));			\
	else								\
		return (defval);					\
}

DNVLIST_TAKE(bool, bool)
DNVLIST_TAKE(uint64_t, number)
DNVLIST_TAKE(char *, string)
DNVLIST_TAKE(nvlist_t *, nvlist)
#ifndef _KERNEL
DNVLIST_TAKE(int, descriptor)
#endif

#undef	DNVLIST_TAKE

void *
dnvlist_take_binary(nvlist_t *nvl, const char *name, size_t *sizep,
    void *defval, size_t defsize)
{
	void *value;

	if (nvlist_exists_binary(nvl, name))
		value = nvlist_take_binary(nvl, name, sizep);
	else {
		if (sizep != NULL)
			*sizep = defsize;
		value = defval;
	}
	return (value);
}

