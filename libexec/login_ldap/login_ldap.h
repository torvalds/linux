/*
 * $OpenBSD: login_ldap.h,v 1.1 2020/09/12 15:06:12 martijn Exp $
 * Copyright (c) 2002 Institute for Open Systems Technology Australia (IFOST)
 * Copyright (c) 2007 Michael Erdely <merdely@openbsd.org>
 * Copyright (c) 2019 Martijn van Duren <martijn@openbsd.org>
 *
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef __login_ldap_h
#define __login_ldap_h

#include <sys/queue.h>

#define DEFTIMEOUT	60 /* number of seconds to wait before a timeout */

struct aldap_urlq {
	struct aldap_url	 s;
	TAILQ_ENTRY(aldap_urlq)	 entries;
};

struct auth_ctx {
	char		*user; 	/* the user to authenticate */
	struct aldap	*ld;	/* ldap server connection */
	TAILQ_HEAD(, aldap_urlq) s;	/* info about the servers */
	char		*basedn;/* base dn for search, may be null */
	char		*binddn;/* bind dn for search, may be null */
	char		*bindpw;/* bind password for search, may be null */
	char		*cacert; /* path to CA ssl certificate */
	char		*cacertdir;
	char		*userdn; /* dn as returned from search */
	char		*filter;
	int		 scope;
	int		 timeout;
	char		*gbasedn;
	char		*gfilter;
	int		 gscope;
};

/* util.c */
extern int debug;

void	dlog(int, char *, ...);
int	parse_conf(struct auth_ctx *, const char *);
int	conn(struct auth_ctx *);
int	do_conn(struct auth_ctx *, struct aldap_url *);
char *	parse_filter(struct auth_ctx *, const char *);
const char *ldap_resultcode(enum result_code code);

/* bind.c */
int 	bind_password(struct auth_ctx *, char *, char *);
int	unbind(struct auth_ctx *);

/* search.c */
char * search(struct auth_ctx *, char *, char *, enum scope);
#endif /* __login_ldap_h */
