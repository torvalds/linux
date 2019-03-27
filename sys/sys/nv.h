/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2009-2013 The FreeBSD Foundation
 * Copyright (c) 2013-2015 Mariusz Zaborski <oshogbo@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef	_NV_H_
#define	_NV_H_

#include <sys/cdefs.h>

#ifndef _KERNEL
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#endif

#ifndef	_NVLIST_T_DECLARED
#define	_NVLIST_T_DECLARED
struct nvlist;

typedef struct nvlist nvlist_t;
#endif

#define	NV_NAME_MAX	2048

#define	NV_TYPE_NONE			0

#define	NV_TYPE_NULL			1
#define	NV_TYPE_BOOL			2
#define	NV_TYPE_NUMBER			3
#define	NV_TYPE_STRING			4
#define	NV_TYPE_NVLIST			5
#define	NV_TYPE_DESCRIPTOR		6
#define	NV_TYPE_BINARY			7
#define	NV_TYPE_BOOL_ARRAY		8
#define	NV_TYPE_NUMBER_ARRAY		9
#define	NV_TYPE_STRING_ARRAY		10
#define	NV_TYPE_NVLIST_ARRAY		11
#define	NV_TYPE_DESCRIPTOR_ARRAY	12

/*
 * Perform case-insensitive lookups of provided names.
 */
#define	NV_FLAG_IGNORE_CASE		0x01
/*
 * Names don't have to be unique.
 */
#define	NV_FLAG_NO_UNIQUE		0x02

#if defined(_KERNEL) && defined(MALLOC_DECLARE)
MALLOC_DECLARE(M_NVLIST);
#endif

__BEGIN_DECLS

nvlist_t	*nvlist_create(int flags);
void		 nvlist_destroy(nvlist_t *nvl);
int		 nvlist_error(const nvlist_t *nvl);
bool		 nvlist_empty(const nvlist_t *nvl);
int		 nvlist_flags(const nvlist_t *nvl);
void		 nvlist_set_error(nvlist_t *nvl, int error);

nvlist_t *nvlist_clone(const nvlist_t *nvl);

#ifndef _KERNEL
void nvlist_dump(const nvlist_t *nvl, int fd);
void nvlist_fdump(const nvlist_t *nvl, FILE *fp);
#endif

size_t		 nvlist_size(const nvlist_t *nvl);
void		*nvlist_pack(const nvlist_t *nvl, size_t *sizep);
nvlist_t	*nvlist_unpack(const void *buf, size_t size, int flags);

int nvlist_send(int sock, const nvlist_t *nvl);
nvlist_t *nvlist_recv(int sock, int flags);
nvlist_t *nvlist_xfer(int sock, nvlist_t *nvl, int flags);

const char *nvlist_next(const nvlist_t *nvl, int *typep, void **cookiep);

const nvlist_t *nvlist_get_parent(const nvlist_t *nvl, void **cookiep);

const nvlist_t *nvlist_get_array_next(const nvlist_t *nvl);
bool nvlist_in_array(const nvlist_t *nvl);

const nvlist_t *nvlist_get_pararr(const nvlist_t *nvl, void **cookiep);

/*
 * The nvlist_exists functions check if the given name (optionally of the given
 * type) exists on nvlist.
 */

bool nvlist_exists(const nvlist_t *nvl, const char *name);
bool nvlist_exists_type(const nvlist_t *nvl, const char *name, int type);

bool nvlist_exists_null(const nvlist_t *nvl, const char *name);
bool nvlist_exists_bool(const nvlist_t *nvl, const char *name);
bool nvlist_exists_number(const nvlist_t *nvl, const char *name);
bool nvlist_exists_string(const nvlist_t *nvl, const char *name);
bool nvlist_exists_nvlist(const nvlist_t *nvl, const char *name);
bool nvlist_exists_binary(const nvlist_t *nvl, const char *name);
bool nvlist_exists_bool_array(const nvlist_t *nvl, const char *name);
bool nvlist_exists_number_array(const nvlist_t *nvl, const char *name);
bool nvlist_exists_string_array(const nvlist_t *nvl, const char *name);
bool nvlist_exists_nvlist_array(const nvlist_t *nvl, const char *name);
#ifndef _KERNEL
bool nvlist_exists_descriptor(const nvlist_t *nvl, const char *name);
bool nvlist_exists_descriptor_array(const nvlist_t *nvl, const char *name);
#endif

/*
 * The nvlist_add functions add the given name/value pair.
 * If a pointer is provided, nvlist_add will internally allocate memory for the
 * given data (in other words it won't consume provided buffer).
 */

void nvlist_add_null(nvlist_t *nvl, const char *name);
void nvlist_add_bool(nvlist_t *nvl, const char *name, bool value);
void nvlist_add_number(nvlist_t *nvl, const char *name, uint64_t value);
void nvlist_add_string(nvlist_t *nvl, const char *name, const char *value);
void nvlist_add_stringf(nvlist_t *nvl, const char *name, const char *valuefmt, ...) __printflike(3, 4);
#if !defined(_KERNEL) || defined(_VA_LIST_DECLARED)
void nvlist_add_stringv(nvlist_t *nvl, const char *name, const char *valuefmt, va_list valueap) __printflike(3, 0);
#endif
void nvlist_add_nvlist(nvlist_t *nvl, const char *name, const nvlist_t *value);
void nvlist_add_binary(nvlist_t *nvl, const char *name, const void *value, size_t size);
void nvlist_add_bool_array(nvlist_t *nvl, const char *name, const bool *value, size_t nitems);
void nvlist_add_number_array(nvlist_t *nvl, const char *name, const uint64_t *value, size_t nitems);
void nvlist_add_string_array(nvlist_t *nvl, const char *name, const char * const *value, size_t nitems);
void nvlist_add_nvlist_array(nvlist_t *nvl, const char *name, const nvlist_t * const *value, size_t nitems);
#ifndef _KERNEL
void nvlist_add_descriptor(nvlist_t *nvl, const char *name, int value);
void nvlist_add_descriptor_array(nvlist_t *nvl, const char *name, const int *value, size_t nitems);
#endif

void nvlist_append_bool_array(nvlist_t *nvl, const char *name, const bool value);
void nvlist_append_number_array(nvlist_t *nvl, const char *name, const uint64_t value);
void nvlist_append_string_array(nvlist_t *nvl, const char *name, const char * const value);
void nvlist_append_nvlist_array(nvlist_t *nvl, const char *name, const nvlist_t * const value);
#ifndef _KERNEL
void nvlist_append_descriptor_array(nvlist_t *nvl, const char *name, int value);
#endif

/*
 * The nvlist_move functions add the given name/value pair.
 * The functions consumes provided buffer.
 */

void nvlist_move_string(nvlist_t *nvl, const char *name, char *value);
void nvlist_move_nvlist(nvlist_t *nvl, const char *name, nvlist_t *value);
void nvlist_move_binary(nvlist_t *nvl, const char *name, void *value, size_t size);
void nvlist_move_bool_array(nvlist_t *nvl, const char *name, bool *value, size_t nitems);
void nvlist_move_string_array(nvlist_t *nvl, const char *name, char **value, size_t nitems);
void nvlist_move_nvlist_array(nvlist_t *nvl, const char *name, nvlist_t **value, size_t nitems);
void nvlist_move_number_array(nvlist_t *nvl, const char *name, uint64_t *value, size_t nitems);
#ifndef _KERNEL
void nvlist_move_descriptor(nvlist_t *nvl, const char *name, int value);
void nvlist_move_descriptor_array(nvlist_t *nvl, const char *name, int *value, size_t nitems);
#endif

/*
 * The nvlist_get functions returns value associated with the given name.
 * If it returns a pointer, the pointer represents internal buffer and should
 * not be freed by the caller.
 */

bool			 nvlist_get_bool(const nvlist_t *nvl, const char *name);
uint64_t		 nvlist_get_number(const nvlist_t *nvl, const char *name);
const char		*nvlist_get_string(const nvlist_t *nvl, const char *name);
const nvlist_t		*nvlist_get_nvlist(const nvlist_t *nvl, const char *name);
const void		*nvlist_get_binary(const nvlist_t *nvl, const char *name, size_t *sizep);
const bool		*nvlist_get_bool_array(const nvlist_t *nvl, const char *name, size_t *nitemsp);
const uint64_t		*nvlist_get_number_array(const nvlist_t *nvl, const char *name, size_t *nitemsp);
const char * const	*nvlist_get_string_array(const nvlist_t *nvl, const char *name, size_t *nitemsp);
const nvlist_t * const	*nvlist_get_nvlist_array(const nvlist_t *nvl, const char *name, size_t *nitemsp);
#ifndef _KERNEL
int			 nvlist_get_descriptor(const nvlist_t *nvl, const char *name);
const int		*nvlist_get_descriptor_array(const nvlist_t *nvl, const char *name, size_t *nitemsp);
#endif

/*
 * The nvlist_take functions returns value associated with the given name and
 * remove the given entry from the nvlist.
 * The caller is responsible for freeing received data.
 */

bool		  nvlist_take_bool(nvlist_t *nvl, const char *name);
uint64_t	  nvlist_take_number(nvlist_t *nvl, const char *name);
char		 *nvlist_take_string(nvlist_t *nvl, const char *name);
nvlist_t	 *nvlist_take_nvlist(nvlist_t *nvl, const char *name);
void		 *nvlist_take_binary(nvlist_t *nvl, const char *name, size_t *sizep);
bool		 *nvlist_take_bool_array(nvlist_t *nvl, const char *name, size_t *nitemsp);
uint64_t	 *nvlist_take_number_array(nvlist_t *nvl, const char *name, size_t *nitemsp);
char		**nvlist_take_string_array(nvlist_t *nvl, const char *name, size_t *nitemsp);
nvlist_t	**nvlist_take_nvlist_array(nvlist_t *nvl, const char *name, size_t *nitemsp);
#ifndef _KERNEL
int		 nvlist_take_descriptor(nvlist_t *nvl, const char *name);
int		 *nvlist_take_descriptor_array(nvlist_t *nvl, const char *name, size_t *nitemsp);
#endif

/*
 * The nvlist_free functions removes the given name/value pair from the nvlist
 * and frees memory associated with it.
 */

void nvlist_free(nvlist_t *nvl, const char *name);
void nvlist_free_type(nvlist_t *nvl, const char *name, int type);

void nvlist_free_null(nvlist_t *nvl, const char *name);
void nvlist_free_bool(nvlist_t *nvl, const char *name);
void nvlist_free_number(nvlist_t *nvl, const char *name);
void nvlist_free_string(nvlist_t *nvl, const char *name);
void nvlist_free_nvlist(nvlist_t *nvl, const char *name);
void nvlist_free_binary(nvlist_t *nvl, const char *name);
void nvlist_free_bool_array(nvlist_t *nvl, const char *name);
void nvlist_free_number_array(nvlist_t *nvl, const char *name);
void nvlist_free_string_array(nvlist_t *nvl, const char *name);
void nvlist_free_nvlist_array(nvlist_t *nvl, const char *name);
void nvlist_free_binary_array(nvlist_t *nvl, const char *name);
#ifndef _KERNEL
void nvlist_free_descriptor(nvlist_t *nvl, const char *name);
void nvlist_free_descriptor_array(nvlist_t *nvl, const char *name);
#endif

__END_DECLS

#endif	/* !_NV_H_ */
