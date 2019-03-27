/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technology, Inc.
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

#define _BSD_SOURCE

#include <sys/types.h>
#include <opie.h>
#include <pwd.h>
#include <unistd.h>
#include <syslog.h>

#define PAM_SM_AUTH

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct opie opie;
	struct passwd *pwent;
	const void *luser, *rhost;
	int r;

	r = pam_get_item(pamh, PAM_USER, &luser);
	if (r != PAM_SUCCESS)
		return (r);
	if (luser == NULL)
		return (PAM_SERVICE_ERR);

	pwent = getpwnam(luser);
	if (pwent == NULL || opielookup(&opie, __DECONST(char *, luser)) != 0)
		return (PAM_SUCCESS);

	r = pam_get_item(pamh, PAM_RHOST, &rhost);
	if (r != PAM_SUCCESS)
		return (r);
	if (rhost == NULL || *(const char *)rhost == '\0')
		rhost = openpam_get_option(pamh, "allow_local") ?
		    "" : "localhost";

	if (opieaccessfile(__DECONST(char *, rhost)) != 0 &&
	    opiealways(pwent->pw_dir) != 0)
		return (PAM_SUCCESS);

	PAM_VERBOSE_ERROR("Refused; remote host is not in opieaccess");

	return (PAM_AUTH_ERR);
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh __unused, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{

	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY("pam_opieaccess");
