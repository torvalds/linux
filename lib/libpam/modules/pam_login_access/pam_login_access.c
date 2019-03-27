/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Mark R V Murray
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

#define _BSD_SOURCE

#include <sys/param.h>

#include <syslog.h>
#include <unistd.h>

#define PAM_SM_ACCOUNT

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

#include "pam_login_access.h"

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	const void *rhost, *tty, *user;
	char hostname[MAXHOSTNAMELEN];
	int pam_err;

	pam_err = pam_get_item(pamh, PAM_USER, &user);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);

	if (user == NULL)
		return (PAM_SERVICE_ERR);

	PAM_LOG("Got user: %s", (const char *)user);

	pam_err = pam_get_item(pamh, PAM_RHOST, &rhost);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);

	pam_err = pam_get_item(pamh, PAM_TTY, &tty);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);

	gethostname(hostname, sizeof hostname);

	if (rhost != NULL && *(const char *)rhost != '\0') {
		PAM_LOG("Checking login.access for user %s from host %s",
		    (const char *)user, (const char *)rhost);
		if (login_access(user, rhost) != 0)
			return (PAM_SUCCESS);
		PAM_VERBOSE_ERROR("%s is not allowed to log in from %s",
		    (const char *)user, (const char *)rhost);
	} else if (tty != NULL && *(const char *)tty != '\0') {
		PAM_LOG("Checking login.access for user %s on tty %s",
		    (const char *)user, (const char *)tty);
		if (login_access(user, tty) != 0)
			return (PAM_SUCCESS);
		PAM_VERBOSE_ERROR("%s is not allowed to log in on %s",
		    (const char *)user, (const char *)tty);
	} else {
		PAM_LOG("Checking login.access for user %s",
		    (const char *)user);
		if (login_access(user, "***unknown***") != 0)
			return (PAM_SUCCESS);
		PAM_VERBOSE_ERROR("%s is not allowed to log in",
		    (const char *)user);
	}

	return (PAM_AUTH_ERR);
}

PAM_MODULE_ENTRY("pam_login_access");
