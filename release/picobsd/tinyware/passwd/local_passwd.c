/*-
 * SPDX-License-Identifier: BSD-4-Clause
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifndef lint
static const char sccsid[] = "@(#)local_passwd.c	8.3 (Berkeley) 4/2/94";
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pw_util.h>
#ifdef YP
#include <pw_yp.h>
#endif

#ifdef LOGGING
#include <syslog.h>
#endif

#ifdef LOGIN_CAP
#ifdef AUTH_NONE /* multiple defs :-( */
#undef AUTH_NONE
#endif
#include <login_cap.h>
#endif

#include "extern.h"

static uid_t uid;
int randinit;

extern void
pw_copy(int ffd, int tfd, struct passwd *pw, struct passwd *old_pw);

char   *tempname;

static unsigned char itoa64[] =		/* 0 ... 63 => ascii - 64 */
	"./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

void
to64(s, v, n)
	char *s;
	long v;
	int n;
{
	while (--n >= 0) {
		*s++ = itoa64[v&0x3f];
		v >>= 6;
	}
}

char *
getnewpasswd(pw, nis)
	struct passwd *pw;
	int nis;
{
	int tries, min_length = 6;
	int force_mix_case = 1;
	char *p, *t;
#ifdef LOGIN_CAP
	login_cap_t * lc;
#endif
	char buf[_PASSWORD_LEN+1], salt[32];
	struct timeval tv;

	if (!nis)
		(void)printf("Changing local password for %s.\n", pw->pw_name);

	if (uid && pw->pw_passwd[0] &&
	    strcmp(crypt(getpass("Old password:"), pw->pw_passwd),
	    pw->pw_passwd)) {
		errno = EACCES;
		pw_error(NULL, 1, 1);
	}

#ifdef LOGIN_CAP
	/*
	 * Determine minimum password length, next password change date,
	 * and whether or not to force mixed case passwords.
	 * Note that even for NIS passwords, login_cap is still used.
	 */
	if ((lc = login_getpwclass(pw)) != NULL) {
		time_t	period;

		/* minpasswordlen capablity */
		min_length = (int)login_getcapnum(lc, "minpasswordlen",
				min_length, min_length);
		/* passwordtime capability */
		period = login_getcaptime(lc, "passwordtime", 0, 0);
		if (period > (time_t)0) {
			pw->pw_change = time(NULL) + period;
		}
		/* mixpasswordcase capability */
		force_mix_case = login_getcapbool(lc, "mixpasswordcase", 1);
	}
#endif

	for (buf[0] = '\0', tries = 0;;) {
		p = getpass("New password:");
		if (!*p) {
			(void)printf("Password unchanged.\n");
			pw_error(NULL, 0, 0);
		}
		if (strlen(p) < min_length && (uid != 0 || ++tries < 2)) {
			(void)printf("Please enter a password at least %d characters in length.\n", min_length);
			continue;
		}
		
		if (force_mix_case) {
		    for (t = p; *t && islower(*t); ++t);
		    if (!*t && (uid != 0 || ++tries < 2)) {
			    (void)printf("Please don't use an all-lower case password.\nUnusual capitalization, control characters or digits are suggested.\n");
			    continue;
		    }
		}
		(void)strcpy(buf, p);
		if (!strcmp(buf, getpass("Retype new password:")))
			break;
		(void)printf("Mismatch; try again, EOF to quit.\n");
	}
	/* grab a random printable character that isn't a colon */
	if (!randinit) {
		randinit = 1;
		srandomdev();
	}
#ifdef NEWSALT
	salt[0] = _PASSWORD_EFMT1;
	to64(&salt[1], (long)(29 * 25), 4);
	to64(&salt[5], random(), 4);
	salt[9] = '\0';
#else
	/* Make a good size salt for algorithms that can use it. */
	gettimeofday(&tv,0);
#ifdef LOGIN_CAP
	if (login_setcryptfmt(lc, "md5", NULL) == NULL)
		pw_error("cannot set password cipher", 1, 1);
	login_close(lc);
#else
	(void)crypt_set_format("md5");
#endif
	/* Salt suitable for anything */
	to64(&salt[0], random(), 3);
	to64(&salt[3], tv.tv_usec, 3);
	to64(&salt[6], tv.tv_sec, 2);
	to64(&salt[8], random(), 5);
	to64(&salt[13], random(), 5);
	to64(&salt[17], random(), 5);
	to64(&salt[22], random(), 5);
	salt[27] = '\0';
#endif
	return (crypt(buf, salt));
}

int
local_passwd(uname)
	char *uname;
{
	struct passwd *pw;
	int pfd, tfd;

	if (!(pw = getpwnam(uname)))
		errx(1, "unknown user %s", uname);

#ifdef YP
	/* Use the right password information. */
	pw = (struct passwd *)&local_password;
#endif
	uid = getuid();
	if (uid && uid != pw->pw_uid)
		errx(1, "%s", strerror(EACCES));

	pw_init();

	/*
	 * Get the new password.  Reset passwd change time to zero by
	 * default. If the user has a valid login class (or the default
	 * fallback exists), then the next password change date is set
	 * by getnewpasswd() according to the "passwordtime" capability
	 * if one has been specified.
	 */
	pw->pw_change = 0;
	pw->pw_passwd = getnewpasswd(pw, 0);

	pfd = pw_lock();
	tfd = pw_tmp();
	pw_copy(pfd, tfd, pw, NULL);

	if (!pw_mkdb(uname))
		pw_error((char *)NULL, 0, 1);
#ifdef LOGGING
	syslog(LOG_DEBUG, "user %s changed their local password\n", uname);
#endif
	return (0);
}
