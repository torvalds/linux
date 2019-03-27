/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Networks Associates Technology, Inc.
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

#include <string.h>

#define PAM_SM_AUTH

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/openpam.h>

#define DEFAULT_GUESTS	"guest"

static int
lookup(const char *str, const char *list)
{
	const char *next;
	size_t len;

	len = strlen(str);
	while (*list != '\0') {
		while (*list == ',')
			++list;
		if ((next = strchr(list, ',')) == NULL)
			next = strchr(list, '\0');
		if (next - list == (ptrdiff_t)len &&
		    strncmp(list, str, len) == 0)
			return (1);
		list = next;
	}
	return (0);
}

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	const char *authtok, *guests, *user;
	int err, is_guest;

	/* get target account */
	if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS || user == NULL)
		return (PAM_AUTH_ERR);

	/* get list of guest logins */
	if ((guests = openpam_get_option(pamh, "guests")) == NULL)
		guests = DEFAULT_GUESTS;

	/* check if the target account is on the list */
	is_guest = lookup(user, guests);

	/* check password */
	if (!openpam_get_option(pamh, "nopass")) {
		err = pam_get_authtok(pamh, PAM_AUTHTOK, &authtok, NULL);
		if (err != PAM_SUCCESS)
			return (err);
		if (openpam_get_option(pamh, "pass_is_user") &&
		    strcmp(user, authtok) != 0)
			return (PAM_AUTH_ERR);
		if (openpam_get_option(pamh, "pass_as_ruser"))
			pam_set_item(pamh, PAM_RUSER, authtok);
	}

	/* done */
	if (is_guest) {
		pam_setenv(pamh, "GUEST", user, 1);
		return (PAM_SUCCESS);
	}
	return (PAM_AUTH_ERR);
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t * pamh __unused, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{

	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY("pam_guest");
