/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 */

#include <sys/cdefs.h>
__SCCSID("@(#)pw_scan.c	8.3 (Berkeley) 4/2/94");
__FBSDID("$FreeBSD$");

/*
 * This module is used to "verify" password entries by chpass(1) and
 * pwd_mkdb(8).
 */

#include <sys/param.h>

#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "pw_scan.h"

/*
 * Some software assumes that IDs are short.  We should emit warnings
 * for id's which cannot be stored in a short, but we are more liberal
 * by default, warning for IDs greater than USHRT_MAX.
 *
 * If pw_big_ids_warning is -1 on entry to pw_scan(), it will be set based
 * on the existence of PW_SCAN_BIG_IDS in the environment.
 *
 * It is believed all baseline system software that can not handle the
 * normal ID sizes is now gone so pw_big_ids_warning is disabled for now.
 * But the code has been left in place in case end-users want to re-enable
 * it and/or for the next time the ID sizes get bigger but pieces of the
 * system lag behind.
 */
static int	pw_big_ids_warning = 0;

void
__pw_initpwd(struct passwd *pwd)
{
	static char nul[] = "";

	memset(pwd, 0, sizeof(*pwd));
	pwd->pw_uid = (uid_t)-1;  /* Considered least likely to lead to */
	pwd->pw_gid = (gid_t)-1;  /* a security issue.                  */
	pwd->pw_name = nul;
	pwd->pw_passwd = nul;
	pwd->pw_class = nul;
	pwd->pw_gecos = nul;
	pwd->pw_dir = nul;
	pwd->pw_shell = nul;
}

int
__pw_scan(char *bp, struct passwd *pw, int flags)
{
	uid_t id;
	int root;
	char *ep, *p, *sh;
	unsigned long temp;

	if (pw_big_ids_warning == -1)
		pw_big_ids_warning = getenv("PW_SCAN_BIG_IDS") == NULL ? 1 : 0;

	pw->pw_fields = 0;
	if (!(pw->pw_name = strsep(&bp, ":")))		/* login */
		goto fmt;
	root = !strcmp(pw->pw_name, "root");
	if (pw->pw_name[0] && (pw->pw_name[0] != '+' || pw->pw_name[1] == '\0'))
		pw->pw_fields |= _PWF_NAME;

	if (!(pw->pw_passwd = strsep(&bp, ":")))	/* passwd */
		goto fmt;
	if (pw->pw_passwd[0])
		pw->pw_fields |= _PWF_PASSWD;

	if (!(p = strsep(&bp, ":")))			/* uid */
		goto fmt;
	if (p[0])
		pw->pw_fields |= _PWF_UID;
	else {
		if (pw->pw_name[0] != '+' && pw->pw_name[0] != '-') {
			if (flags & _PWSCAN_WARN)
				warnx("no uid for user %s", pw->pw_name);
			return (0);
		}
	}
	errno = 0;
	temp = strtoul(p, &ep, 10);
	if ((temp == ULONG_MAX && errno == ERANGE) || temp > UID_MAX) {
		if (flags & _PWSCAN_WARN)
			warnx("%s > max uid value (%u)", p, UID_MAX);
		return (0);
	}
	id = temp;
	if (*ep != '\0') {
		if (flags & _PWSCAN_WARN)
			warnx("%s uid is incorrect", p);
		return (0);
	}
	if (root && id) {
		if (flags & _PWSCAN_WARN)
			warnx("root uid should be 0");
		return (0);
	}
	if (flags & _PWSCAN_WARN && pw_big_ids_warning && id > USHRT_MAX) {
		warnx("%s > recommended max uid value (%u)", p, USHRT_MAX);
		/*return (0);*/ /* THIS SHOULD NOT BE FATAL! */
	}
	pw->pw_uid = id;

	if (!(p = strsep(&bp, ":")))			/* gid */
		goto fmt;
	if (p[0])
		pw->pw_fields |= _PWF_GID;
	else {
		if (pw->pw_name[0] != '+' && pw->pw_name[0] != '-') {
			if (flags & _PWSCAN_WARN)
				warnx("no gid for user %s", pw->pw_name);
			return (0);
		}
	}
	errno = 0;
	temp = strtoul(p, &ep, 10);
	if ((temp == ULONG_MAX && errno == ERANGE) || temp > GID_MAX) {
		if (flags & _PWSCAN_WARN)
			warnx("%s > max gid value (%u)", p, GID_MAX);
		return (0);
	}
	id = temp;
	if (*ep != '\0') {
		if (flags & _PWSCAN_WARN)
			warnx("%s gid is incorrect", p);
		return (0);
	}
	if (flags & _PWSCAN_WARN && pw_big_ids_warning && id > USHRT_MAX) {
		warnx("%s > recommended max gid value (%u)", p, USHRT_MAX);
		/* return (0); This should not be fatal! */
	}
	pw->pw_gid = id;

	if (flags & _PWSCAN_MASTER ) {
		if (!(pw->pw_class = strsep(&bp, ":")))	/* class */
			goto fmt;
		if (pw->pw_class[0])
			pw->pw_fields |= _PWF_CLASS;
		
		if (!(p = strsep(&bp, ":")))		/* change */
			goto fmt;
		if (p[0])
			pw->pw_fields |= _PWF_CHANGE;
		pw->pw_change = atol(p);
		
		if (!(p = strsep(&bp, ":")))		/* expire */
			goto fmt;
		if (p[0])
			pw->pw_fields |= _PWF_EXPIRE;
		pw->pw_expire = atol(p);
	}
	if (!(pw->pw_gecos = strsep(&bp, ":")))		/* gecos */
		goto fmt;
	if (pw->pw_gecos[0])
		pw->pw_fields |= _PWF_GECOS;

	if (!(pw->pw_dir = strsep(&bp, ":")))		/* directory */
		goto fmt;
	if (pw->pw_dir[0])
		pw->pw_fields |= _PWF_DIR;

	if (!(pw->pw_shell = strsep(&bp, ":")))		/* shell */
		goto fmt;

	p = pw->pw_shell;
	if (root && *p) {				/* empty == /bin/sh */
		for (setusershell();;) {
			if (!(sh = getusershell())) {
				if (flags & _PWSCAN_WARN)
					warnx("warning, unknown root shell");
				break;
			}
			if (!strcmp(p, sh))
				break;
		}
		endusershell();
	}
	if (p[0])
		pw->pw_fields |= _PWF_SHELL;

	if ((p = strsep(&bp, ":"))) {			/* too many */
fmt:		
		if (flags & _PWSCAN_WARN)
			warnx("corrupted entry");
		return (0);
	}
	return (1);
}
