/*	$OpenBSD: pwd.h,v 1.26 2018/09/13 12:31:15 millert Exp $	*/
/*	$NetBSD: pwd.h,v 1.9 1996/05/15 21:36:45 jtc Exp $	*/

/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 * Portions Copyright(C) 1995, 1996, Jason Downs.  All rights reserved.
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
 *	@(#)pwd.h	8.2 (Berkeley) 1/21/94
 */

#ifndef _PWD_H_
#define	_PWD_H_

#include <sys/types.h>

#if __BSD_VISIBLE
#define	_PATH_PASSWD		"/etc/passwd"
#define	_PATH_MASTERPASSWD	"/etc/master.passwd"
#define	_PATH_MASTERPASSWD_LOCK	"/etc/ptmp"

#define	_PATH_MP_DB		"/etc/pwd.db"
#define	_PATH_SMP_DB		"/etc/spwd.db"

#define	_PATH_PWD_MKDB		"/usr/sbin/pwd_mkdb"

#define	_PW_KEYBYNAME		'1'	/* stored by name */
#define	_PW_KEYBYNUM		'2'	/* stored by entry in the "file" */
#define	_PW_KEYBYUID		'3'	/* stored by uid */

#define _PW_YPTOKEN		"__YP!"

#define	_PASSWORD_EFMT1		'_'	/* extended encryption format */

#define	_PASSWORD_LEN		128	/* max length, not counting NUL */
#define	_PW_NAME_LEN		31	/* max length, not counting NUL */
					/* Should be MAXLOGNAME - 1 */
#define _PW_BUF_LEN		1024	/* length of getpw*_r buffer */

#define _PASSWORD_NOUID		0x01	/* flag for no specified uid. */
#define _PASSWORD_NOGID		0x02	/* flag for no specified gid. */
#define _PASSWORD_NOCHG		0x04	/* flag for no specified change. */
#define _PASSWORD_NOEXP		0x08	/* flag for no specified expire. */

/* Flags for pw_mkdb(3) */
#define	_PASSWORD_SECUREONLY	0x01	/* only generate spwd.db file */
#define	_PASSWORD_OMITV7	0x02	/* don't generate v7 passwd file */

#endif

struct passwd {
	char	*pw_name;		/* user name */
	char	*pw_passwd;		/* encrypted password */
	uid_t	pw_uid;			/* user uid */
	gid_t	pw_gid;			/* user gid */
	time_t	pw_change;		/* password change time */
	char	*pw_class;		/* user access class */
	char	*pw_gecos;		/* Honeywell login info */
	char	*pw_dir;		/* home directory */
	char	*pw_shell;		/* default shell */
	time_t	pw_expire;		/* account expiration */
};

__BEGIN_DECLS
struct passwd	*getpwuid(uid_t);
struct passwd	*getpwnam(const char *);
struct passwd	*getpwuid_shadow(uid_t);
struct passwd	*getpwnam_shadow(const char *);
int		getpwnam_r(const char *, struct passwd *, char *, size_t,
		    struct passwd **);
int		getpwuid_r(uid_t, struct passwd *, char *, size_t,
		    struct passwd **);
#if __BSD_VISIBLE || __XPG_VISIBLE
struct passwd	*getpwent(void);
void		 setpwent(void);
void		 endpwent(void);
#endif
#if __BSD_VISIBLE
int		 setpassent(int);
int		 uid_from_user(const char *, uid_t *);
const char	*user_from_uid(uid_t, int);
char		*bcrypt_gensalt(u_int8_t);
char		*bcrypt(const char *, const char *);
int		bcrypt_newhash(const char *, int, char *, size_t);
int		bcrypt_checkpass(const char *, const char *);
struct passwd	*pw_dup(const struct passwd *);
#endif
__END_DECLS

#endif /* !_PWD_H_ */
