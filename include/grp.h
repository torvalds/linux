/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)grp.h	8.2 (Berkeley) 1/21/94
 * $FreeBSD$
 */

#ifndef _GRP_H_
#define	_GRP_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#define	_PATH_GROUP		"/etc/group"

#ifndef _GID_T_DECLARED
typedef	__gid_t		gid_t;
#define	_GID_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef __size_t	size_t;
#define _SIZE_T_DECLARED
#endif

struct group {
	char	*gr_name;		/* group name */
	char	*gr_passwd;		/* group password */
	gid_t	gr_gid;			/* group id */
	char	**gr_mem;		/* group members */
};

__BEGIN_DECLS
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
void		 endgrent(void);
struct group	*getgrent(void);
#endif
struct group	*getgrgid(gid_t);
struct group	*getgrnam(const char *);
#if __BSD_VISIBLE
const char	*group_from_gid(gid_t, int);
int		 gid_from_group(const char *, gid_t *);
int		 pwcache_groupdb(int (*)(int), void (*)(void),
		    struct group * (*)(const char *),
		    struct group * (*)(gid_t));
#endif
#if __XSI_VISIBLE
void		 setgrent(void);
#endif
#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE
int		 getgrgid_r(gid_t, struct group *, char *, size_t,
		    struct group **);
int		 getgrnam_r(const char *, struct group *, char *, size_t,
		    struct group **);
#endif
#if __BSD_VISIBLE
int		 getgrent_r(struct group *, char *, size_t, struct group **);
int		 setgroupent(int);
#endif
__END_DECLS

#endif /* !_GRP_H_ */
