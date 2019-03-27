/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <ulog.h>

/*
 * This setuid helper utility writes user login records to disk.
 * Unprivileged processes are not capable of writing records to utmpx,
 * but we do want to allow this for pseudo-terminals.  Because a file
 * descriptor to a pseudo-terminal master device can only be obtained by
 * processes using the pseudo-terminal, we expect such a descriptor on
 * stdin.
 *
 * It uses the real user ID of the calling process to determine the
 * username.  It does allow users to log arbitrary hostnames.
 */

static const char *
get_username(void)
{
	const struct passwd *pw;
	const char *login;
	uid_t uid;

	/*
	 * Attempt to determine the username corresponding to this login
	 * session.  First, validate the results of getlogin() against
	 * the password database.  If getlogin() returns invalid data,
	 * return an arbitrary username corresponding to this uid.
	 */
	uid = getuid();
	if ((login = getlogin()) != NULL && (pw = getpwnam(login)) != NULL &&
	    pw->pw_uid == uid)
		return (login);
	if ((pw = getpwuid(uid)) != NULL)
		return (pw->pw_name);
	return (NULL);
}

int
main(int argc, char *argv[])
{
	const char *line, *user, *host;

	/* Device line name. */
	if ((line = ptsname(STDIN_FILENO)) == NULL)
		return (EX_USAGE);

	if ((argc == 2 || argc == 3) && strcmp(argv[1], "login") == 0) {
		/* Username. */
		user = get_username();
		if (user == NULL)
			return (EX_OSERR);

		/* Hostname. */
		host = argc == 3 ? argv[2] : NULL;

		ulog_login(line, user, host);
		return (EX_OK);
	} else if (argc == 2 && strcmp(argv[1], "logout") == 0) {
		ulog_logout(line);
		return (EX_OK);
	}

	return (EX_USAGE);
}
