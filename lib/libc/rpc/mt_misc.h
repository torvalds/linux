/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2006 The FreeBSD Project. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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
#ifndef _MT_MISC_H
#define	_MT_MISC_H

/* Take these locks out of the application namespace. */
#define	svc_lock		__svc_lock
#define	svc_fd_lock		__svc_fd_lock
#define	rpcbaddr_cache_lock	__rpcbaddr_cache_lock
#define	authdes_ops_lock	__authdes_ops_lock
#define	authnone_lock		__authnone_lock
#define	authsvc_lock		__authsvc_lock
#define	clnt_fd_lock		__clnt_fd_lock
#define	clntraw_lock		__clntraw_lock
#define	dupreq_lock		__dupreq_lock
#define	loopnconf_lock		__loopnconf_lock
#define	ops_lock		__ops_lock
#define	proglst_lock		__proglst_lock
#define	rpcsoc_lock		__rpcsoc_lock
#define	svcraw_lock		__svcraw_lock
#define	xprtlist_lock		__xprtlist_lock

extern pthread_rwlock_t	svc_lock;
extern pthread_rwlock_t	svc_fd_lock;
extern pthread_rwlock_t	rpcbaddr_cache_lock;
extern pthread_mutex_t	authdes_ops_lock;
extern pthread_mutex_t	svcauthdesstats_lock;
extern pthread_mutex_t	authnone_lock;
extern pthread_mutex_t	authsvc_lock;
extern pthread_mutex_t	clnt_fd_lock;
extern pthread_mutex_t	clntraw_lock;
extern pthread_mutex_t	dupreq_lock;
extern pthread_mutex_t	loopnconf_lock;
extern pthread_mutex_t	ops_lock;
extern pthread_mutex_t	proglst_lock;
extern pthread_mutex_t	rpcsoc_lock;
extern pthread_mutex_t	svcraw_lock;
extern pthread_mutex_t	tsd_lock;
extern pthread_mutex_t	xprtlist_lock;

#endif
