/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 The FreeBSD Foundation
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

#ifndef	_NV_IMPL_H_
#define	_NV_IMPL_H_

#ifndef	_NVPAIR_T_DECLARED
#define	_NVPAIR_T_DECLARED
struct nvpair;

typedef struct nvpair nvpair_t;
#endif

#define	NV_TYPE_NVLIST_ARRAY_NEXT	254
#define	NV_TYPE_NVLIST_UP		255

#define	NV_TYPE_FIRST			NV_TYPE_NULL
#define	NV_TYPE_LAST			NV_TYPE_DESCRIPTOR_ARRAY

#define	NV_FLAG_BIG_ENDIAN		0x080
#define	NV_FLAG_IN_ARRAY		0x100

#ifdef _KERNEL
#define	nv_malloc(size)			malloc((size), M_NVLIST, M_WAITOK)
#define	nv_calloc(n, size)		mallocarray((n), (size), M_NVLIST, \
					    M_WAITOK | M_ZERO)
#define	nv_realloc(buf, size)		realloc((buf), (size), M_NVLIST, \
					    M_WAITOK)
#define	nv_free(buf)			free((buf), M_NVLIST)
#define	nv_strdup(buf)			strdup((buf), M_NVLIST)
#define	nv_vasprintf(ptr, ...)		vasprintf(ptr, M_NVLIST, __VA_ARGS__)

#define	ERRNO_SET(var)			do { } while (0)
#define	ERRNO_SAVE()			do { do { } while(0)
#define	ERRNO_RESTORE()			} while (0)

#define	ERRNO_OR_DEFAULT(default)	(default)

#else

#define	nv_malloc(size)			malloc((size))
#define	nv_calloc(n, size)		calloc((n), (size))
#define	nv_realloc(buf, size)		realloc((buf), (size))
#define	nv_free(buf)			free((buf))
#define	nv_strdup(buf)			strdup((buf))
#define	nv_vasprintf(ptr, ...)		vasprintf(ptr, __VA_ARGS__)

#define	ERRNO_SET(var)			do { errno = (var); } while (0)
#define	ERRNO_SAVE()			do {				\
						int _serrno;		\
									\
						_serrno = errno

#define	ERRNO_RESTORE()				errno = _serrno;	\
					} while (0)

#define	ERRNO_OR_DEFAULT(default)	(errno == 0 ? (default) : errno)

#endif

int	*nvlist_descriptors(const nvlist_t *nvl, size_t *nitemsp);
size_t	 nvlist_ndescriptors(const nvlist_t *nvl);
void	 nvlist_set_flags(nvlist_t *nvl, int flags);

nvpair_t *nvlist_first_nvpair(const nvlist_t *nvl);
nvpair_t *nvlist_next_nvpair(const nvlist_t *nvl, const nvpair_t *nvp);
nvpair_t *nvlist_prev_nvpair(const nvlist_t *nvl, const nvpair_t *nvp);

void nvlist_add_nvpair(nvlist_t *nvl, const nvpair_t *nvp);

bool nvlist_move_nvpair(nvlist_t *nvl, nvpair_t *nvp);

void nvlist_set_parent(nvlist_t *nvl, nvpair_t *parent);
void nvlist_set_array_next(nvlist_t *nvl, nvpair_t *ele);
nvpair_t *nvlist_get_array_next_nvpair(nvlist_t *nvl);

const nvpair_t *nvlist_get_nvpair(const nvlist_t *nvl, const char *name);

nvpair_t *nvlist_take_nvpair(nvlist_t *nvl, const char *name);

/* Function removes the given nvpair from the nvlist. */
void nvlist_remove_nvpair(nvlist_t *nvl, nvpair_t *nvp);

void nvlist_free_nvpair(nvlist_t *nvl, nvpair_t *nvp);

int nvpair_type(const nvpair_t *nvp);
const char *nvpair_name(const nvpair_t *nvp);

nvpair_t *nvpair_clone(const nvpair_t *nvp);

nvpair_t *nvpair_create_null(const char *name);
nvpair_t *nvpair_create_bool(const char *name, bool value);
nvpair_t *nvpair_create_number(const char *name, uint64_t value);
nvpair_t *nvpair_create_string(const char *name, const char *value);
nvpair_t *nvpair_create_stringf(const char *name, const char *valuefmt, ...) __printflike(2, 3);
nvpair_t *nvpair_create_stringv(const char *name, const char *valuefmt, va_list valueap) __printflike(2, 0);
nvpair_t *nvpair_create_nvlist(const char *name, const nvlist_t *value);
nvpair_t *nvpair_create_descriptor(const char *name, int value);
nvpair_t *nvpair_create_binary(const char *name, const void *value, size_t size);
nvpair_t *nvpair_create_bool_array(const char *name, const bool *value, size_t nitems);
nvpair_t *nvpair_create_number_array(const char *name, const uint64_t *value, size_t nitems);
nvpair_t *nvpair_create_string_array(const char *name, const char * const *value, size_t nitems);
nvpair_t *nvpair_create_nvlist_array(const char *name, const nvlist_t * const *value, size_t nitems);
nvpair_t *nvpair_create_descriptor_array(const char *name, const int *value, size_t nitems);

nvpair_t *nvpair_move_string(const char *name, char *value);
nvpair_t *nvpair_move_nvlist(const char *name, nvlist_t *value);
nvpair_t *nvpair_move_descriptor(const char *name, int value);
nvpair_t *nvpair_move_binary(const char *name, void *value, size_t size);
nvpair_t *nvpair_move_bool_array(const char *name, bool *value, size_t nitems);
nvpair_t *nvpair_move_nvlist_array(const char *name, nvlist_t **value, size_t nitems);
nvpair_t *nvpair_move_descriptor_array(const char *name, int *value, size_t nitems);
nvpair_t *nvpair_move_number_array(const char *name, uint64_t *value, size_t nitems);
nvpair_t *nvpair_move_string_array(const char *name, char **value, size_t nitems);

int nvpair_append_bool_array(nvpair_t *nvp, const bool value);
int nvpair_append_number_array(nvpair_t *nvp, const uint64_t value);
int nvpair_append_string_array(nvpair_t *nvp, const char *value);
int nvpair_append_nvlist_array(nvpair_t *nvp, const nvlist_t *value);
int nvpair_append_descriptor_array(nvpair_t *nvp, const int value);

bool			 nvpair_get_bool(const nvpair_t *nvp);
uint64_t		 nvpair_get_number(const nvpair_t *nvp);
const char		*nvpair_get_string(const nvpair_t *nvp);
const nvlist_t		*nvpair_get_nvlist(const nvpair_t *nvp);
int			 nvpair_get_descriptor(const nvpair_t *nvp);
const void		*nvpair_get_binary(const nvpair_t *nvp, size_t *sizep);
const bool		*nvpair_get_bool_array(const nvpair_t *nvp, size_t *nitemsp);
const uint64_t		*nvpair_get_number_array(const nvpair_t *nvp, size_t *nitemsp);
const char * const	*nvpair_get_string_array(const nvpair_t *nvp, size_t *nitemsp);
const nvlist_t * const	*nvpair_get_nvlist_array(const nvpair_t *nvp, size_t *nitemsp);
const int		*nvpair_get_descriptor_array(const nvpair_t *nvp, size_t *nitemsp);

void nvpair_free(nvpair_t *nvp);

#endif	/* !_NV_IMPL_H_ */
