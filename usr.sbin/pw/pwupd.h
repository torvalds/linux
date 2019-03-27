/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
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

#ifndef _PWUPD_H_
#define _PWUPD_H_

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/types.h>

#include <pwd.h>
#include <grp.h>
#include <stdbool.h>
#include <stringlist.h>

struct pwf {
	int		    _altdir;
	void		  (*_setpwent)(void);
	void		  (*_endpwent)(void);
	struct passwd * (*_getpwent)(void);
	struct passwd	* (*_getpwuid)(uid_t uid);
	struct passwd	* (*_getpwnam)(const char * nam);
	void		  (*_setgrent)(void);
	void		  (*_endgrent)(void);
	struct group  * (*_getgrent)(void);
	struct group  * (*_getgrgid)(gid_t gid);
	struct group  * (*_getgrnam)(const char * nam);
};

struct userconf {
	int		default_password;	/* Default password for new users? */
	int		reuse_uids;		/* Reuse uids? */
	int		reuse_gids;		/* Reuse gids? */
	char		*nispasswd;		/* Path to NIS version of the passwd file */
	char		*dotdir;		/* Where to obtain skeleton files */
	char		*newmail;		/* Mail to send to new accounts */
	char		*logfile;		/* Where to log changes */
	char		*home;			/* Where to create home directory */
	mode_t		homemode;		/* Home directory permissions */
	char		*shelldir;		/* Where shells are located */
	char		**shells;		/* List of shells */
	char		*shell_default;		/* Default shell */
	char		*default_group;		/* Default group number */
	StringList	*groups;		/* Default (additional) groups */
	char		*default_class;		/* Default user class */
	uid_t		min_uid, max_uid;	/* Allowed range of uids */
	gid_t		min_gid, max_gid;	/* Allowed range of gids */
	time_t		expire_days;		/* Days to expiry */
	time_t		password_days;		/* Days to password expiry */
};

struct pwconf {
	char		 rootdir[MAXPATHLEN];
	char		 etcpath[MAXPATHLEN];
	int		 fd;
	int		 rootfd;
	bool		 checkduplicate;
};

extern struct pwf PWF;
extern struct pwf VPWF;
extern struct pwconf conf;

#define SETPWENT()	PWF._setpwent()
#define ENDPWENT()	PWF._endpwent()
#define GETPWENT()	PWF._getpwent()
#define GETPWUID(uid)	PWF._getpwuid(uid)
#define GETPWNAM(nam)	PWF._getpwnam(nam)

#define SETGRENT()	PWF._setgrent()
#define ENDGRENT()	PWF._endgrent()
#define GETGRENT()	PWF._getgrent()
#define GETGRGID(gid)	PWF._getgrgid(gid)
#define GETGRNAM(nam)	PWF._getgrnam(nam)

#define PWF_REGULAR 0
#define PWF_ALT 1
#define PWF_ROOTDIR 2

#define PWALTDIR()	PWF._altdir
#ifndef _PATH_PWD
#define _PATH_PWD	"/etc"
#endif
#ifndef _GROUP
#define _GROUP		"group"
#endif
#ifndef _MASTERPASSWD
#define _MASTERPASSWD	"master.passwd"
#endif

__BEGIN_DECLS
int addpwent(struct passwd * pwd);
int delpwent(struct passwd * pwd);
int chgpwent(char const * login, struct passwd * pwd);

char * getpwpath(char const * file);

int addgrent(struct group * grp);
int delgrent(struct group * grp);
int chggrent(char const * name, struct group * grp);

char * getgrpath(const char *file);

void vsetpwent(void);
void vendpwent(void);
struct passwd * vgetpwent(void);
struct passwd * vgetpwuid(uid_t uid);
struct passwd * vgetpwnam(const char * nam);

struct group * vgetgrent(void);
struct group * vgetgrgid(gid_t gid);
struct group * vgetgrnam(const char * nam);
void           vsetgrent(void);
void           vendgrent(void);

void copymkdir(int rootfd, char const * dir, int skelfd, mode_t mode, uid_t uid,
    gid_t gid, int flags);
void rm_r(int rootfd, char const * dir, uid_t uid);
__END_DECLS

#endif				/* !_PWUPD_H */
