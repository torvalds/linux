/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
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

#ifndef _NFSCL_NFS_KDTRACE_H_
#define	_NFSCL_NFS_KDTRACE_H_

/*
 * Definitions for NFS access cache probes.
 */
extern uint32_t	nfscl_accesscache_flush_done_id;
extern uint32_t	nfscl_accesscache_get_hit_id;
extern uint32_t	nfscl_accesscache_get_miss_id;
extern uint32_t	nfscl_accesscache_load_done_id;

/*
 * Definitions for NFS attribute cache probes.
 */
extern uint32_t	nfscl_attrcache_flush_done_id;
extern uint32_t	nfscl_attrcache_get_hit_id;
extern uint32_t	nfscl_attrcache_get_miss_id;
extern uint32_t	nfscl_attrcache_load_done_id;

#ifdef KDTRACE_HOOKS
#include <sys/dtrace_bsd.h>

#define	KDTRACE_NFS_ACCESSCACHE_FLUSH_DONE(vp)	do {			\
	if (dtrace_nfscl_accesscache_flush_done_probe != NULL)		\
		(dtrace_nfscl_accesscache_flush_done_probe)(		\
		    nfscl_accesscache_flush_done_id, (vp));		\
} while (0)

#define	KDTRACE_NFS_ACCESSCACHE_GET_HIT(vp, uid, mode)	do {		\
	if (dtrace_nfscl_accesscache_get_hit_probe != NULL)		\
		(dtrace_nfscl_accesscache_get_hit_probe)(		\
		    nfscl_accesscache_get_hit_id, (vp), (uid),		\
		    (mode));						\
} while (0)
	
#define	KDTRACE_NFS_ACCESSCACHE_GET_MISS(vp, uid, mode)	do {		\
	if (dtrace_nfscl_accesscache_get_miss_probe != NULL)		\
		(dtrace_nfscl_accesscache_get_miss_probe)(		\
		    nfscl_accesscache_get_miss_id, (vp), (uid),		\
		    (mode));						\
} while (0)

#define	KDTRACE_NFS_ACCESSCACHE_LOAD_DONE(vp, uid, rmode, error) do {	\
	if (dtrace_nfscl_accesscache_load_done_probe != NULL)		\
		(dtrace_nfscl_accesscache_load_done_probe)(		\
		    nfscl_accesscache_load_done_id, (vp), (uid),	\
		    (rmode), (error));					\
} while (0)

#define	KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp)	do {			\
	if (dtrace_nfscl_attrcache_flush_done_probe != NULL)		\
		(dtrace_nfscl_attrcache_flush_done_probe)(		\
		    nfscl_attrcache_flush_done_id, (vp));		\
} while (0)

#define	KDTRACE_NFS_ATTRCACHE_GET_HIT(vp, vap)	do {			\
	if (dtrace_nfscl_attrcache_get_hit_probe != NULL)		\
		(dtrace_nfscl_attrcache_get_hit_probe)(			\
		    nfscl_attrcache_get_hit_id, (vp), (vap));		\
} while (0)

#define	KDTRACE_NFS_ATTRCACHE_GET_MISS(vp)	do {			\
	if (dtrace_nfscl_attrcache_get_miss_probe != NULL)		\
		(dtrace_nfscl_attrcache_get_miss_probe)(		\
			    nfscl_attrcache_get_miss_id, (vp));		\
} while (0)

#define	KDTRACE_NFS_ATTRCACHE_LOAD_DONE(vp, vap, error)	do {		\
	if (dtrace_nfscl_attrcache_load_done_probe != NULL)		\
		(dtrace_nfscl_attrcache_load_done_probe)(		\
		    nfscl_attrcache_load_done_id, (vp), (vap),		\
		    (error));						\
} while (0)

#else /* !KDTRACE_HOOKS */

#define	KDTRACE_NFS_ACCESSCACHE_FLUSH_DONE(vp)
#define	KDTRACE_NFS_ACCESSCACHE_GET_HIT(vp, uid, mode)
#define	KDTRACE_NFS_ACCESSCACHE_GET_MISS(vp, uid, mode)
#define	KDTRACE_NFS_ACCESSCACHE_LOAD_DONE(vp, uid, rmode, error)

#define	KDTRACE_NFS_ATTRCACHE_FLUSH_DONE(vp)
#define	KDTRACE_NFS_ATTRCACHE_GET_HIT(vp, vap)
#define	KDTRACE_NFS_ATTRCACHE_GET_MISS(vp)
#define	KDTRACE_NFS_ATTRCACHE_LOAD_DONE(vp, vap, error)

#endif /* KDTRACE_HOOKS */

#endif /* !_NFSCL_NFS_KDTRACE_H_ */
