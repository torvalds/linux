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
 *	@(#)pwd.h	8.2 (Berkeley) 1/21/94
 * $FreeBSD$
 */

#ifndef _PWD_H_
#define	_PWD_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifndef _GID_T_DECLARED
typedef	__gid_t		gid_t;
#define	_GID_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef	__time_t	time_t;
#define	_TIME_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef	__uid_t		uid_t;
#define	_UID_T_DECLARED
#endif

#ifndef _SIZE_T_DECLARED
typedef __size_t	size_t;
#define _SIZE_T_DECLARED
#endif

#define _PATH_PWD		"/etc"
#define	_PATH_PASSWD		"/etc/passwd"
#define	_PASSWD			"passwd"
#define	_PATH_MASTERPASSWD	"/etc/master.passwd"
#define	_MASTERPASSWD		"master.passwd"

#define	_PATH_MP_DB		"/etc/pwd.db"
#define	_MP_DB			"pwd.db"
#define	_PATH_SMP_DB		"/etc/spwd.db"
#define	_SMP_DB			"spwd.db"

#define	_PATH_PWD_MKDB		"/usr/sbin/pwd_mkdb"

/* Historically, the keys in _PATH_MP_DB/_PATH_SMP_DB had the format
 * `1 octet tag | key', where the tag is one of the _PW_KEY* values
 * listed below.  These values happen to be ASCII digits.  Starting
 * with FreeBSD 5.1, the tag is now still a single octet, but the
 * upper 4 bits are interpreted as a version.  Pre-FreeBSD 5.1 format
 * entries are version `3' -- this conveniently results in the same
 * key values as before.  The new, architecture-independent entries
 * are version `4'.
 * As it happens, some applications read the database directly.
 * (Bad app, no cookie!)  Thus, we leave the _PW_KEY* symbols at their
 * old pre-FreeBSD 5.1 values so these apps still work.  Consequently
 * we have to muck around a bit more to get the correct, versioned
 * tag, and that is what the _PW_VERSIONED macro is about.
 */

#define _PW_VERSION_MASK	'\xF0'
#define _PW_VERSIONED(x, v)	((unsigned char)(((x) & 0xCF) | ((v)<<4)))

#define	_PW_KEYBYNAME		'\x31'	/* stored by name */
#define	_PW_KEYBYNUM		'\x32'	/* stored by entry in the "file" */
#define	_PW_KEYBYUID		'\x33'	/* stored by uid */
#define _PW_KEYYPENABLED	'\x34'	/* YP is enabled */
#define	_PW_KEYYPBYNUM		'\x35'	/* special +@netgroup entries */

/* The database also contains a key to indicate the format version of
 * the entries therein.  There may be other, older versioned entries
 * as well.
 */
#define _PWD_VERSION_KEY	"\xFF" "VERSION"
#define _PWD_CURRENT_VERSION	'\x04'

#define	_PASSWORD_EFMT1		'_'	/* extended encryption format */

#define	_PASSWORD_LEN		128	/* max length, not counting NULL */

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
	int	pw_fields;		/* internal: fields filled in */
};

/* Mapping from fields to bits for pw_fields. */
#define _PWF(x)		(1 << x)
#define _PWF_NAME	_PWF(0)
#define _PWF_PASSWD	_PWF(1)
#define _PWF_UID	_PWF(2)
#define _PWF_GID	_PWF(3)
#define _PWF_CHANGE	_PWF(4)
#define _PWF_CLASS	_PWF(5)
#define _PWF_GECOS	_PWF(6)
#define _PWF_DIR	_PWF(7)
#define _PWF_SHELL	_PWF(8)
#define _PWF_EXPIRE	_PWF(9)

/* XXX These flags are bogus.  With nsswitch, there are many
 * possible sources and they cannot be represented in a small integer.
 */                           
#define _PWF_SOURCE	0x3000
#define _PWF_FILES	0x1000
#define _PWF_NIS	0x2000
#define _PWF_HESIOD	0x3000

__BEGIN_DECLS
struct passwd	*getpwnam(const char *);
struct passwd	*getpwuid(uid_t);

#if __XSI_VISIBLE >= 500
void		 endpwent(void);
struct passwd	*getpwent(void);
void		 setpwent(void);
#endif

#if __POSIX_VISIBLE >= 200112 || __XSI_VISIBLE >= 500
int		 getpwnam_r(const char *, struct passwd *, char *, size_t,
		    struct passwd **);
int		 getpwuid_r(uid_t, struct passwd *, char *, size_t,
		    struct passwd **);
#endif

#if __BSD_VISIBLE
int		 getpwent_r(struct passwd *, char *, size_t, struct passwd **);
int		 setpassent(int);
const char	*user_from_uid(uid_t, int);
int		 uid_from_user(const char *, uid_t *);
int		 pwcache_userdb(int (*)(int), void (*)(void),
		    struct passwd * (*)(const char *),
		    struct passwd * (*)(uid_t));
#endif
__END_DECLS

#endif /* !_PWD_H_ */
