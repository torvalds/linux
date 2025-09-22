/*	$OpenBSD: grp.h,v 1.13 2018/09/13 12:31:15 millert Exp $	*/
/*	$NetBSD: grp.h,v 1.7 1995/04/29 05:30:40 cgd Exp $	*/

/*-
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
 */

#ifndef _GRP_H_
#define	_GRP_H_

#include <sys/types.h>

#if __BSD_VISIBLE
#define	_PATH_GROUP		"/etc/group"
#define	_GR_BUF_LEN		(1024+200*sizeof(char*))
#endif

struct group {
	char	*gr_name;		/* group name */
	char	*gr_passwd;		/* group password */
	gid_t	gr_gid;			/* group id */
	char	**gr_mem;		/* group members */
};

__BEGIN_DECLS
struct group	*getgrgid(gid_t);
struct group	*getgrnam(const char *);
#if __BSD_VISIBLE || __XPG_VISIBLE
struct group	*getgrent(void);
void		 setgrent(void);
void		 endgrent(void);
#endif
#if __BSD_VISIBLE || __POSIX_VISIBLE >= 199506 || __XPG_VISIBLE
int		 getgrgid_r(gid_t, struct group *, char *,
		    size_t, struct group **);
int		 getgrnam_r(const char *, struct group *, char *,
		    size_t, struct group **);
#endif
#if __BSD_VISIBLE
int		 setgroupent(int);
int		 gid_from_group(const char *, gid_t *);
const char	*group_from_gid(gid_t, int);
#endif
__END_DECLS

#endif /* !_GRP_H_ */
