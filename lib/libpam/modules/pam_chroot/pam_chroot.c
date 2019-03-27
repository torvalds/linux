/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Networks Associates Technology, Inc.
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project by ThinkSec AS and
 * NAI Labs, the Security Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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

#include <sys/param.h>

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PAM_SM_SESSION

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/openpam.h>

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	const char *dir, *end, *cwd, *user;
	struct passwd *pwd;
	char buf[PATH_MAX];

	if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS ||
	    user == NULL || (pwd = getpwnam(user)) == NULL)
		return (PAM_SESSION_ERR);
	if (pwd->pw_uid == 0 && !openpam_get_option(pamh, "also_root"))
		return (PAM_SUCCESS);
	if (pwd->pw_dir == NULL)
		return (PAM_SESSION_ERR);
	if ((end = strstr(pwd->pw_dir, "/./")) != NULL) {
		if (snprintf(buf, sizeof(buf), "%.*s",
		    (int)(end - pwd->pw_dir), pwd->pw_dir) > (int)sizeof(buf)) {
			openpam_log(PAM_LOG_ERROR,
			    "%s's home directory is too long", user);
			return (PAM_SESSION_ERR);
		}
		dir = buf;
		cwd = end + 2;
	} else if ((dir = openpam_get_option(pamh, "dir")) != NULL) {
		if ((cwd = openpam_get_option(pamh, "cwd")) == NULL)
			cwd = "/";
	} else {
		if (openpam_get_option(pamh, "always")) {
			openpam_log(PAM_LOG_ERROR,
			    "%s has no chroot directory", user);
			return (PAM_SESSION_ERR);
		}
		return (PAM_SUCCESS);
	}

	openpam_log(PAM_LOG_DEBUG, "chrooting %s to %s", dir, user);

	if (chroot(dir) == -1) {
		openpam_log(PAM_LOG_ERROR, "chroot(): %m");
		return (PAM_SESSION_ERR);
	}
	if (chdir(cwd) == -1) {
		openpam_log(PAM_LOG_ERROR, "chdir(): %m");
		return (PAM_SESSION_ERR);
	}
	pam_setenv(pamh, "HOME", cwd, 1);
	return (PAM_SUCCESS);
}

PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh __unused, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{

	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY("pam_chroot");
