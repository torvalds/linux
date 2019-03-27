/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Michael Bushkov <bushman@rsu.ru>
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
 *
 * $FreeBSD$
 */

#ifndef __NS_CACHE_H__
#define __NS_CACHE_H__

#include "nscachedcli.h"

typedef int (*nss_cache_id_func_t)(char *, size_t *, va_list, void *);
typedef int (*nss_cache_marshal_func_t)(char *, size_t *, void *, va_list,
	void *);
typedef int (*nss_cache_unmarshal_func_t)(char *, size_t, void *, va_list,
	void *);

typedef	void (*nss_set_mp_ws_func_t)(cached_mp_write_session);
typedef	cached_mp_write_session	(*nss_get_mp_ws_func_t)(void);

typedef void (*nss_set_mp_rs_func_t)(cached_mp_read_session);
typedef cached_mp_read_session	(*nss_get_mp_rs_func_t)(void);

typedef struct _nss_cache_info {
	char	*entry_name;
	void	*mdata;

	/*
	 * These 3 functions should be implemented specifically for each
	 * nsswitch database.
	 */
	nss_cache_id_func_t id_func;	/* marshals the request parameters */
	nss_cache_marshal_func_t marshal_func;	   /* marshals response */
	nss_cache_unmarshal_func_t unmarshal_func; /* unmarshals response */

	/*
	 * These 4 functions should be generated with NSS_MP_CACHE_HANDLING
	 * macro.
	 */
	nss_set_mp_ws_func_t set_mp_ws_func; /* sets current write session */
	nss_get_mp_ws_func_t get_mp_ws_func; /* gets current write session */

	nss_set_mp_rs_func_t set_mp_rs_func; /* sets current read session */
	nss_get_mp_rs_func_t get_mp_rs_func; /* gets current read session */
} nss_cache_info;

/*
 * NSS_MP_CACHE_HANDLING implements the set_mp_ws, get_mp_ws, set_mp_rs,
 * get_mp_rs functions, that are used in _nss_cache_info. It uses
 * NSS_TLS_HANDLING macro to organize thread local storage.
 */
#define NSS_MP_CACHE_HANDLING(name)					\
struct name##_mp_state {						\
	cached_mp_write_session	mp_write_session;			\
	cached_mp_read_session	mp_read_session;			\
};									\
									\
static void								\
name##_mp_endstate(void *s) {						\
	struct name##_mp_state	*mp_state;				\
									\
	mp_state = (struct name##_mp_state *)s;				\
	if (mp_state->mp_write_session != INVALID_CACHED_MP_WRITE_SESSION)\
		__abandon_cached_mp_write_session(mp_state->mp_write_session);\
									\
	if (mp_state->mp_read_session != INVALID_CACHED_MP_READ_SESSION)\
		__close_cached_mp_read_session(mp_state->mp_read_session);\
}									\
NSS_TLS_HANDLING(name##_mp);						\
									\
static void								\
name##_set_mp_ws(cached_mp_write_session ws)				\
{									\
	struct name##_mp_state	*mp_state;				\
	int	res;							\
									\
	res = name##_mp_getstate(&mp_state);				\
	if (res != 0)							\
		return;							\
									\
	mp_state->mp_write_session = ws;				\
}									\
									\
static cached_mp_write_session						\
name##_get_mp_ws(void)							\
{									\
	struct name##_mp_state	*mp_state;				\
	int	res;							\
									\
	res = name##_mp_getstate(&mp_state);				\
	if (res != 0)							\
		return (INVALID_CACHED_MP_WRITE_SESSION);		\
									\
	return (mp_state->mp_write_session);				\
}									\
									\
static void								\
name##_set_mp_rs(cached_mp_read_session rs)				\
{									\
	struct name##_mp_state	*mp_state;				\
	int	res;							\
									\
	res = name##_mp_getstate(&mp_state);				\
	if (res != 0)							\
		return;							\
									\
	mp_state->mp_read_session = rs;					\
}									\
									\
static cached_mp_read_session						\
name##_get_mp_rs(void)							\
{									\
	struct name##_mp_state	*mp_state;				\
	int	res;							\
									\
	res = name##_mp_getstate(&mp_state);				\
	if (res != 0)							\
		return (INVALID_CACHED_MP_READ_SESSION);		\
									\
	return (mp_state->mp_read_session);				\
}

/*
 * These macros should be used to initialize _nss_cache_info structure. For
 * multipart queries in setXXXent and getXXXent functions mf and uf
 * (marshal function and unmarshal function) should be both NULL.
 */
#define NS_COMMON_CACHE_INFO_INITIALIZER(name, mdata, if, mf, uf)	\
	{#name, mdata, if, mf, uf, NULL, NULL, NULL, NULL}
#define NS_MP_CACHE_INFO_INITIALIZER(name, mdata, mf, uf)		\
	{#name, mdata, NULL, mf, uf, name##_set_mp_ws, name##_get_mp_ws,\
		name##_set_mp_rs, name##_get_mp_rs }

/*
 * Analog of other XXX_CB macros. Has the pointer to _nss_cache_info
 * structure as the only argument.
 */
#define NS_CACHE_CB(cinfo) {NSSRC_CACHE, __nss_cache_handler, (void *)(cinfo) },

/* args are: current pointer, current buffer, initial buffer, pointer type */
#define NS_APPLY_OFFSET(cp, cb, ib, p_type)				\
	if ((cp) != NULL)						\
		(cp) = (p_type)((char *)(cb) + (size_t)(cp) - (size_t)(ib))
/*
 * Gets new pointer from the marshalled buffer by uisng initial address
 * and initial buffer address
 */
#define NS_GET_NEWP(cp, cb, ib)						\
	((char *)(cb) + (size_t)(cp) - (size_t)(ib))

typedef struct _nss_cache_data {
	char	*key;
	size_t	key_size;

	nss_cache_info const	*info;
} nss_cache_data;

__BEGIN_DECLS
/* dummy function, which is needed to make nss_method_lookup happy */
extern	int	__nss_cache_handler(void *, void *, va_list);

#ifdef _NS_PRIVATE
extern	int	__nss_common_cache_read(void *, void *, va_list);
extern	int	__nss_common_cache_write(void *, void *, va_list);
extern	int	__nss_common_cache_write_negative(void *);

extern	int	__nss_mp_cache_read(void *, void *, va_list);
extern	int	__nss_mp_cache_write(void *, void *, va_list);
extern	int	__nss_mp_cache_write_submit(void *, void *, va_list);
extern	int	__nss_mp_cache_end(void *, void *, va_list);
#endif /* _NS_PRIVATE */

__END_DECLS

#endif
