/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2000 James Bloom
 * All rights reserved.
 * Based upon code Copyright 1998 Juniper Networks, Inc.
 * Copyright (c) 2001-2003 Networks Associates Technology, Inc.
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
#include <opie.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PAM_SM_AUTH

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

#define PAM_OPT_NO_FAKE_PROMPTS	"no_fake_prompts"

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct opie opie;
	struct passwd *pwd;
	int retval, i;
	const char *(promptstr[]) = { "%s\nPassword: ", "%s\nPassword [echo on]: "};
	char challenge[OPIE_CHALLENGE_MAX + 1];
	char principal[OPIE_PRINCIPAL_MAX];
	const char *user;
	char *response;
	int style;

	user = NULL;
	if (openpam_get_option(pamh, PAM_OPT_AUTH_AS_SELF)) {
		if ((pwd = getpwnam(getlogin())) == NULL)
			return (PAM_AUTH_ERR);
		user = pwd->pw_name;
	}
	else {
		retval = pam_get_user(pamh, &user, NULL);
		if (retval != PAM_SUCCESS)
			return (retval);
	}

	PAM_LOG("Got user: %s", user);

	/*
	 * Watch out: libopie feels entitled to truncate the user name
	 * passed to it if it's longer than OPIE_PRINCIPAL_MAX, which is
	 * not uncommon in Windows environments.
	 */
	if (strlen(user) >= sizeof(principal))
		return (PAM_AUTH_ERR);
	strlcpy(principal, user, sizeof(principal));

	/*
	 * Don't call the OPIE atexit() handler when our program exits,
	 * since the module has been unloaded and we will SEGV.
	 */
	opiedisableaeh();

	/*
	 * If the no_fake_prompts option was given, and the user
	 * doesn't have an OPIE key, just fail rather than present the
	 * user with a bogus OPIE challenge.
	 */
	if (opiechallenge(&opie, principal, challenge) != 0 &&
	    openpam_get_option(pamh, PAM_OPT_NO_FAKE_PROMPTS))
		return (PAM_AUTH_ERR);

	/*
	 * It doesn't make sense to use a password that has already been
	 * typed in, since we haven't presented the challenge to the user
	 * yet, so clear the stored password.
	 */
	pam_set_item(pamh, PAM_AUTHTOK, NULL);

	style = PAM_PROMPT_ECHO_OFF;
	for (i = 0; i < 2; i++) {
		retval = pam_prompt(pamh, style, &response,
		    promptstr[i], challenge);
		if (retval != PAM_SUCCESS) {
			opieunlock();
			return (retval);
		}

		PAM_LOG("Completed challenge %d: %s", i, response);

		if (response[0] != '\0')
			break;

		/* Second time round, echo the password */
		style = PAM_PROMPT_ECHO_ON;
	}

	pam_set_item(pamh, PAM_AUTHTOK, response);

	/*
	 * Opieverify is supposed to return -1 only if an error occurs.
	 * But it returns -1 even if the response string isn't in the form
	 * it expects.  Thus we can't log an error and can only check for
	 * success or lack thereof.
	 */
	retval = opieverify(&opie, response);
	free(response);
	return (retval == 0 ? PAM_SUCCESS : PAM_AUTH_ERR);
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh __unused, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{

	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY("pam_opie");
