/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
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

#include <db.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <rpc/rpc.h>
#include <rpcsvc/yp.h>

#ifndef _PATH_YP
#define _PATH_YP "/var/yp/"
#endif

#ifndef _PATH_LIBEXEC
#define _PATH_LIBEXEC "/usr/libexec/"
#endif

#ifndef MAX_CHILDREN
#define MAX_CHILDREN 20
#endif

#define YP_SECURE 0x1
#define YP_INTERDOMAIN 0x2

/*
 * External functions and variables.
 */

extern int	debug;
extern int	ypdb_debug;
extern int	do_dns;
extern int	children;
extern int	resfd;
extern char 	*progname;
extern char	*yp_dir;
extern pid_t	yp_pid;

extern enum ypstat	yp_errno;
extern void	yp_error(const char *, ...) __printflike(1, 2);
#ifdef DB_CACHE
extern int	yp_get_record(DB *, const DBT *, DBT *, int);
#else
extern int	yp_get_record(const char *, const char *, const DBT *, DBT *, int);
#endif
extern int	yp_first_record(const DB *, DBT *, DBT *, int);
extern int	yp_next_record(const DB *, DBT *, DBT *, int, int);
extern char	*yp_dnsname(char *);
extern char	*yp_dnsaddr(const char *);
#ifdef DB_CACHE
extern int	yp_access(const char *, const char *, const struct svc_req *);
#else
extern int	yp_access(const char *, const struct svc_req *);
#endif
extern int	yp_validdomain(const char *);
extern DB	*yp_open_db(const char *, const char *);
extern DB	*yp_open_db_cache(const char *, const char *, const char *, int);
extern void	yp_flush_all(void);
extern void	yp_init_dbs(void);
extern int	yp_testflag(char *, char *, int);
extern void	load_securenets(void);

#ifdef DB_CACHE
extern ypstat	yp_select_map(char *, char *, keydat *, int);
extern ypstat	yp_getbykey(keydat *, valdat *);
extern ypstat	yp_firstbykey(keydat *, valdat *);
extern ypstat	yp_nextbykey(keydat *, valdat *);
#endif

extern unsigned long	svcudp_set_xid(SVCXPRT *, unsigned long);
extern unsigned long	svcudp_get_xid(SVCXPRT *);

#ifndef RESOLVER_TIMEOUT
#define RESOLVER_TIMEOUT 3600
#endif

extern int	yp_init_resolver(void);
extern void	yp_run_dnsq(void);
extern void	yp_prune_dnsq(void);
extern ypstat	yp_async_lookup_name(struct svc_req *, char *, int);
extern ypstat	yp_async_lookup_addr(struct svc_req *, char *, int);
