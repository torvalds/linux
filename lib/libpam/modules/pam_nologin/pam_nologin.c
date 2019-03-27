/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2001 Mark R V Murray
 * All rights reserved.
 * Copyright (c) 2001 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <login_cap.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PAM_SM_ACCOUNT

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

#define	_PATH_NOLOGIN	"/var/run/nologin"

static char nologin_def[] = _PATH_NOLOGIN;

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags,
    int argc __unused, const char *argv[] __unused)
{
	login_cap_t *lc;
	struct passwd *pwd;
	struct stat st;
	int retval, fd;
	ssize_t ss;
	const char *user, *nologin;
	char *mtmp;

	retval = pam_get_user(pamh, &user, NULL);
	if (retval != PAM_SUCCESS)
		return (retval);

	PAM_LOG("Got user: %s", user);

	pwd = getpwnam(user);
	if (pwd == NULL)
		return (PAM_USER_UNKNOWN);

	/*
	 * login_getpwclass(3) will select the "root" class by default
	 * if pwd->pw_uid is 0.  That class should have "ignorenologin"
	 * capability so that super-user can bypass nologin.
	 */
	lc = login_getpwclass(pwd);
	if (lc == NULL) {
		PAM_LOG("Unable to get login class for user %s", user);
		return (PAM_SERVICE_ERR);
	}

	if (login_getcapbool(lc, "ignorenologin", 0)) {
		login_close(lc);
		return (PAM_SUCCESS);
	}

	nologin = login_getcapstr(lc, "nologin", nologin_def, nologin_def);

	fd = open(nologin, O_RDONLY, 0);
	if (fd < 0) {
		login_close(lc);
		return (PAM_SUCCESS);
	}

	PAM_LOG("Opened %s file", nologin);

	if (fstat(fd, &st) == 0) {
		mtmp = malloc(st.st_size + 1);
		if (mtmp != NULL) {
			ss = read(fd, mtmp, st.st_size);
			if (ss > 0) {
				mtmp[ss] = '\0';
				pam_error(pamh, "%s", mtmp);
			}
			free(mtmp);
		}
	}

	PAM_VERBOSE_ERROR("Administrator refusing you: %s", nologin);

	close(fd);
	login_close(lc);

	return (PAM_AUTH_ERR);
}

PAM_MODULE_ENTRY("pam_nologin");
