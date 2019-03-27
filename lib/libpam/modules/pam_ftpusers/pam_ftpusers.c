/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Networks Associates Technology, Inc.
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

#include <ctype.h>
#include <grp.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAM_SM_ACCOUNT

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>
#include <security/openpam.h>

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct passwd *pwd;
	struct group *grp;
	const char *user;
	int pam_err, found, allow;
	char *line, *name, **mem;
	size_t len, ulen;
	FILE *f;

	pam_err = pam_get_user(pamh, &user, NULL);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);
	if (user == NULL || (pwd = getpwnam(user)) == NULL)
		return (PAM_SERVICE_ERR);

	found = 0;
	ulen = strlen(user);
	if ((f = fopen(_PATH_FTPUSERS, "r")) == NULL) {
		PAM_LOG("%s: %m", _PATH_FTPUSERS);
		goto done;
	}
	while (!found && (line = fgetln(f, &len)) != NULL) {
		if (*line == '#')
			continue;
		while (len > 0 && isspace(line[len - 1]))
			--len;
		if (len == 0)
			continue;
		/* simple case first */
		if (*line != '@') {
			if (len == ulen && strncmp(user, line, len) == 0)
				found = 1;
			continue;
		}
		/* member of specified group? */
		asprintf(&name, "%.*s", (int)len - 1, line + 1);
		if (name == NULL) {
			fclose(f);
			return (PAM_BUF_ERR);
		}
		grp = getgrnam(name);
		free(name);
		if (grp == NULL)
			continue;
		for (mem = grp->gr_mem; mem && *mem && !found; ++mem)
			if (strcmp(user, *mem) == 0)
				found = 1;
	}
 done:
	allow = (openpam_get_option(pamh, "disallow") == NULL);
	if (found)
		pam_err = allow ? PAM_SUCCESS : PAM_AUTH_ERR;
	else
		pam_err = allow ? PAM_AUTH_ERR : PAM_SUCCESS;
	if (f != NULL)
		fclose(f);
	return (pam_err);
}

PAM_MODULE_ENTRY("pam_ftpusers");
