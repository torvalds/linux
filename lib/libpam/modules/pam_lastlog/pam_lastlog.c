/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1987, 1988, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2001 Mark R V Murray
 * All rights reserved.
 * Copyright (c) 2001 Networks Associates Technology, Inc.
 * All rights reserved.
 * Copyright (c) 2004 Joe R. Doupnik
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
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <sys/time.h>

#include <paths.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utmpx.h>

#define PAM_SM_SESSION

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

#define	PAM_UTMPX_ID	"utmpx_id"

PAM_EXTERN int
pam_sm_open_session(pam_handle_t *pamh, int flags,
    int argc __unused, const char *argv[] __unused)
{
	struct utmpx *utx, utl;
	time_t t;
	const char *user;
	const void *rhost, *tty;
	char *id;
	int pam_err;

	pam_err = pam_get_user(pamh, &user, NULL);
	if (pam_err != PAM_SUCCESS)
		return (pam_err);
	if (user == NULL)
		return (PAM_SERVICE_ERR);
	PAM_LOG("Got user: %s", user);

	pam_err = pam_get_item(pamh, PAM_RHOST, &rhost);
	if (pam_err != PAM_SUCCESS) {
		PAM_LOG("No PAM_RHOST");
		goto err;
	}
	pam_err = pam_get_item(pamh, PAM_TTY, &tty);
	if (pam_err != PAM_SUCCESS) {
		PAM_LOG("No PAM_TTY");
		goto err;
	}
	if (tty == NULL) {
		PAM_LOG("No PAM_TTY");
		pam_err = PAM_SERVICE_ERR;
		goto err;
	}
	/* Strip /dev/ component. */
	if (strncmp(tty, _PATH_DEV, sizeof(_PATH_DEV) - 1) == 0)
		tty = (const char *)tty + sizeof(_PATH_DEV) - 1;

	if ((flags & PAM_SILENT) == 0) {
		if (setutxdb(UTXDB_LASTLOGIN, NULL) != 0) {
			PAM_LOG("Failed to open lastlogin database");
		} else {
			utx = getutxuser(user);
			if (utx != NULL && utx->ut_type == USER_PROCESS) {
				t = utx->ut_tv.tv_sec;
				if (*utx->ut_host != '\0')
					pam_info(pamh, "Last login: %.*s from %s",
					    24 - 5, ctime(&t), utx->ut_host);
				else
					pam_info(pamh, "Last login: %.*s on %s",
					    24 - 5, ctime(&t), utx->ut_line);
			}
			endutxent();
		}
	}

	id = malloc(sizeof utl.ut_id);
	if (id == NULL) {
		pam_err = PAM_SERVICE_ERR;
		goto err;
	}
	arc4random_buf(id, sizeof utl.ut_id);

	pam_err = pam_set_data(pamh, PAM_UTMPX_ID, id, openpam_free_data);
	if (pam_err != PAM_SUCCESS) {
		free(id);
		goto err;
	}

	memset(&utl, 0, sizeof utl);
	utl.ut_type = USER_PROCESS;
	memcpy(utl.ut_id, id, sizeof utl.ut_id);
	strncpy(utl.ut_user, user, sizeof utl.ut_user);
	strncpy(utl.ut_line, tty, sizeof utl.ut_line);
	if (rhost != NULL)
		strncpy(utl.ut_host, rhost, sizeof utl.ut_host);
	utl.ut_pid = getpid();
	gettimeofday(&utl.ut_tv, NULL);
	pututxline(&utl);

	return (PAM_SUCCESS);

err:
	if (openpam_get_option(pamh, "no_fail"))
		return (PAM_SUCCESS);
	return (pam_err);
}

PAM_EXTERN int
pam_sm_close_session(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct utmpx utl;
	const void *id;
	int pam_err;

	pam_err = pam_get_data(pamh, PAM_UTMPX_ID, (const void **)&id);
	if (pam_err != PAM_SUCCESS)
		goto err;

	memset(&utl, 0, sizeof utl);
	utl.ut_type = DEAD_PROCESS;
	memcpy(utl.ut_id, id, sizeof utl.ut_id);
	utl.ut_pid = getpid();
	gettimeofday(&utl.ut_tv, NULL);
	pututxline(&utl);

	return (PAM_SUCCESS);

 err:
	if (openpam_get_option(pamh, "no_fail"))
		return (PAM_SUCCESS);
	return (pam_err);
}

PAM_MODULE_ENTRY("pam_lastlog");
