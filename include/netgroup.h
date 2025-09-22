/*	$OpenBSD: netgroup.h,v 1.9 2023/01/04 13:00:11 jsg Exp $ */
/*
 * Copyright (c) 1994 Christos Zoulas
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#ifndef _NETGROUP_H_
#define	_NETGROUP_H_

#define	_PATH_NETGROUP		"/etc/netgroup"

#define	_PATH_NETGROUP_DB	"/etc/netgroup.db"

#define	_PATH_NETGROUP_MKDB	"/usr/sbin/netgroup_mkdb"

#define	_NG_KEYBYNAME		'1'	/* stored by name */
#define	_NG_KEYBYUSER		'2'	/* stored by user */
#define	_NG_KEYBYHOST		'3'	/* stored by host */

#define _NG_ERROR	-1
#define _NG_NONE	 0
#define _NG_NAME	 1
#define _NG_GROUP	 2

struct netgroup {
	char		*ng_host;	/* host name */
	char		*ng_user;	/* user name */
	char		*ng_domain;	/* domain name */
	struct netgroup	*ng_next;	/* thread */
};

#include <sys/cdefs.h>

__BEGIN_DECLS
void	setnetgrent(const char *);
int	getnetgrent(const char **, const char **, const char **);
void	endnetgrent(void);
int	innetgr(const char *, const char *, const char *, const char *);
__END_DECLS

#endif /* !_NETGROUP_H_ */
