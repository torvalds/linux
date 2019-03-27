/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Adam Starak <starak.adam@gmail.com>
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
 *
 * $FreeBSD$
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

#include <sys/cnv.h>
#include <sys/nv.h>

#include "nv_impl.h"
#include "nvlist_impl.h"
#include "nvpair_impl.h"

const char *
cnvlist_name(const void *cookie)
{

	return (nvpair_name(cookie));
}

int
cnvlist_type(const void *cookie)
{

	return (nvpair_type(cookie));
}

#define	CNVLIST_GET(ftype, type, NVTYPE)				\
ftype									\
cnvlist_get_##type(const void *cookie)					\
{									\
									\
	if (nvpair_type(cookie) != NV_TYPE_##NVTYPE) {			\
		nvlist_report_missing(NV_TYPE_##NVTYPE,			\
		    nvpair_name(cookie));				\
	}								\
        return (nvpair_get_##type(cookie));				\
}

CNVLIST_GET(bool, bool, BOOL)
CNVLIST_GET(uint64_t, number, NUMBER)
CNVLIST_GET(const char *, string, STRING)
CNVLIST_GET(const nvlist_t *, nvlist, NVLIST)
#ifndef _KERNEL
CNVLIST_GET(int, descriptor, DESCRIPTOR)
#endif

#undef	CNVLIST_GET

#define	CNVLIST_GET_ARRAY(ftype, type, NVTYPE)				\
ftype									\
cnvlist_get_##type(const void *cookie, size_t *nitemsp)			\
{									\
									\
	if (nvpair_type(cookie) != NV_TYPE_##NVTYPE) {			\
		nvlist_report_missing(NV_TYPE_##NVTYPE,			\
		    nvpair_name(cookie));				\
	}								\
	return (nvpair_get_##type(cookie, nitemsp));			\
}

CNVLIST_GET_ARRAY(const bool *, bool_array, BOOL_ARRAY)
CNVLIST_GET_ARRAY(const uint64_t *, number_array, NUMBER_ARRAY)
CNVLIST_GET_ARRAY(const char * const *, string_array, STRING_ARRAY)
CNVLIST_GET_ARRAY(const nvlist_t * const *, nvlist_array, NVLIST_ARRAY)
#ifndef _KERNEL
CNVLIST_GET_ARRAY(const int *, descriptor_array, DESCRIPTOR_ARRAY)
#endif

#undef	CNVLIST_GET_ARRAY

const void *
cnvlist_get_binary(const void *cookie, size_t *sizep)
{

	if (nvpair_type(cookie) != NV_TYPE_BINARY)
		nvlist_report_missing(NV_TYPE_BINARY, nvpair_name(cookie));
	return (nvpair_get_binary(cookie, sizep));
}

#define CNVLIST_TAKE(ftype, type, NVTYPE)				\
ftype									\
cnvlist_take_##type(void *cookie)					\
{									\
	ftype value;							\
	nvlist_t *nvl;							\
									\
	if (nvpair_type(cookie) != NV_TYPE_##NVTYPE) {			\
		nvlist_report_missing(NV_TYPE_##NVTYPE,			\
		    nvpair_name(cookie));				\
	}								\
	nvl = nvpair_nvlist(cookie);					\
	value = (ftype)(intptr_t)nvpair_get_##type(cookie);		\
	nvlist_remove_nvpair(nvl, cookie);				\
	nvpair_free_structure(cookie);					\
	return (value);							\
}

CNVLIST_TAKE(bool, bool, BOOL)
CNVLIST_TAKE(uint64_t, number, NUMBER)
CNVLIST_TAKE(char *, string, STRING)
CNVLIST_TAKE(nvlist_t *, nvlist, NVLIST)
#ifndef _KERNEL
CNVLIST_TAKE(int, descriptor, DESCRIPTOR)
#endif

#undef	CNVLIST_TAKE

#define	CNVLIST_TAKE_ARRAY(ftype, type, NVTYPE)				\
ftype									\
cnvlist_take_##type(void *cookie, size_t *nitemsp)			\
{									\
	ftype value;							\
	nvlist_t *nvl;							\
									\
	if (nvpair_type(cookie) != NV_TYPE_##NVTYPE) {			\
		nvlist_report_missing(NV_TYPE_##NVTYPE,			\
		    nvpair_name(cookie));				\
	}								\
	nvl = nvpair_nvlist(cookie);					\
	value = (ftype)(intptr_t)nvpair_get_##type(cookie, nitemsp);	\
	nvlist_remove_nvpair(nvl, cookie);				\
	nvpair_free_structure(cookie);					\
	return (value);							\
}

CNVLIST_TAKE_ARRAY(bool *, bool_array, BOOL_ARRAY)
CNVLIST_TAKE_ARRAY(uint64_t *, number_array, NUMBER_ARRAY)
CNVLIST_TAKE_ARRAY(char **, string_array, STRING_ARRAY)
CNVLIST_TAKE_ARRAY(nvlist_t **, nvlist_array, NVLIST_ARRAY)
#ifndef _KERNEL
CNVLIST_TAKE_ARRAY(int *, descriptor_array, DESCRIPTOR_ARRAY);
#endif

#undef	CNVLIST_TAKE_ARRAY

void *
cnvlist_take_binary(void *cookie, size_t *sizep)
{
	void *value;
	nvlist_t *nvl;

	if (nvpair_type(cookie) != NV_TYPE_BINARY)
		nvlist_report_missing(NV_TYPE_BINARY, nvpair_name(cookie));
	nvl = nvpair_nvlist(cookie);
	value = (void *)(intptr_t)nvpair_get_binary(cookie, sizep);
	nvlist_remove_nvpair(nvl, cookie);
	nvpair_free_structure(cookie);
	return (value);
}


#define	CNVLIST_FREE(type)						\
void									\
cnvlist_free_##type(void *cookie)					\
{									\
									\
	nvlist_free_nvpair(nvpair_nvlist(cookie), cookie);		\
}

CNVLIST_FREE(bool)
CNVLIST_FREE(number)
CNVLIST_FREE(string)
CNVLIST_FREE(nvlist)
CNVLIST_FREE(binary);
CNVLIST_FREE(bool_array)
CNVLIST_FREE(number_array)
CNVLIST_FREE(string_array)
CNVLIST_FREE(nvlist_array)
#ifndef _KERNEL
CNVLIST_FREE(descriptor)
CNVLIST_FREE(descriptor_array)
#endif

#undef	CNVLIST_FREE
