/*	$OpenBSD: check_expire.c,v 1.14 2021/10/24 21:24:20 deraadt Exp $	*/

/*
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
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
 *	This product includes software developed by Berkeley Software Design,
 *	Inc.
 * 4. The name of Berkeley Software Design, Inc.  may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN, INC. BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	BSDI $From: check_expire.c,v 2.1 1997/08/08 18:38:25 prb Exp $
 */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <login_cap.h>
#include <bsd_auth.h>

#include "util.h"

static char *pwd_update(const struct passwd *, const struct passwd *);

#define SECSPERDAY	(24 * 60 * 60)
#define TWOWEEKS	(2 * 7 * SECSPERDAY)

int
login_check_expire(FILE *back, struct passwd *pwd, char *class, int lastchance)
{
	auth_session_t *as;
	login_cap_t *lc;
	quad_t dead, expire, warn;
	char *p;

	if ((as = auth_open()) == NULL) {
		fprintf(back, BI_VALUE
		    " errormsg Unable to create auth session\n");
		fprintf(back, BI_REJECT "\n");
		return (1);
	}
	if (auth_setpwd(as, pwd) < 0) {
		fprintf(back, BI_VALUE
		    " errormsg Unable to set pwd entry in auth session\n");
		fprintf(back, BI_REJECT "\n");
		return (1);
	}

	expire = auth_check_change(as);
	auth_close(as);

	if (expire != 0) {
		fprintf(back, BI_VALUE " expire %qd\n", expire);

		if (class == NULL)
			class = pwd->pw_class;

		if ((lc = login_getclass(class)) == NULL) {
			dead = 0;
			warn = 0;
		} else {
			dead = login_getcaptime(lc, "password-dead", 0, 0);
			warn = login_getcaptime(lc, "password-warn",
			    TWOWEEKS, TWOWEEKS);
			if (dead < 0)
				dead = 0;
			if (warn < 0)
				warn = 0;
		}
		login_close(lc);

		/*
		 * If their password is dead (expired longer than
		 * password-dead) then just reject them.  If it is
		 * expired but not dead yet, reject them with a
		 * PWEXPIRED so login knows they can still sort of
		 * get in.
		 */
		if (expire < -dead) {
			fprintf(back, BI_VALUE
			    " errormsg Your password has expired\n");
			fprintf(back, BI_REJECT "\n");
			return (1);
		}
		if (expire < 0) {
			if (lastchance) {
				struct passwd *npwd;

				endpwent();

				/*
				 * Only let them play this game once.
				 * Set their password change time to 1.
				 * This will most certainly cause any
				 * expired password to be dead, as well.
				 */
				npwd = pw_dup(pwd);
				npwd->pw_change = 1;
				p = pwd_update(npwd, pwd);
				explicit_bzero(npwd->pw_passwd,
				    strlen(npwd->pw_passwd));
				free(npwd);
				if (p != NULL) {
					char *errval = auth_mkvalue(p);
					if (errval != NULL) {
						fprintf(back, BI_VALUE
						    " errormsg %s", errval);
						free(errval);
					}
					fprintf(back, BI_REJECT "\n");
					return (1);
				}
			}
			fprintf(back, BI_VALUE
			    " errormsg Your password has expired\n");
			fprintf(back, BI_PWEXPIRED "\n");
			return (1);
		}

		/*
		 * If their password is not expired but is about to expire
		 * then warn them.
		 */
		if (expire <= warn) {
			fprintf(back, BI_VALUE
			    " warnmsg Your password expires on %s\n",
			    ctime(&pwd->pw_change));
		}
	}
	return (0);
}

static char *
pwd_update(const struct passwd *pwd, const struct passwd *opwd)
{
	int tfd, pfd;

	pw_init();
	tfd = pw_lock(0);
	if (tfd == -1) {
		if (errno == EEXIST)
			return("the passwd file is busy.");
		else
			return("can't open passwd temp file");
	}

	pfd = open(_PATH_MASTERPASSWD, O_RDONLY|O_CLOEXEC);
	if (pfd == -1) {
		pw_abort();
		return(strerror(errno));
	}

	pw_copy(pfd, tfd, pwd, opwd);
	if (pw_mkdb(pwd->pw_name, 0) == -1) {
		pw_abort();
		return("unable to update password database");
	}

	return(NULL);
}
