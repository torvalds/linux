/*	$OpenBSD: login_lchpass.c,v 1.21 2018/04/26 12:42:51 guenther Exp $	*/

/*-
 * Copyright (c) 1995,1996 Berkeley Software Design, Inc. All rights reserved.
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
 *      This product includes software developed by Berkeley Software Design,
 *      Inc.
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
 *	BSDI $From: login_lchpass.c,v 1.4 1997/08/08 18:58:23 prb Exp $
 */

#include <sys/resource.h>
#include <sys/uio.h>

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <login_cap.h>
#include <readpassphrase.h>

#define BACK_CHANNEL	3

int local_passwd(char *, int);

int
main(int argc, char *argv[])
{
	struct iovec iov[2];
	struct passwd *pwd;
	char *username = NULL, *hash = NULL;
	char oldpass[1024];
	struct rlimit rl;
	int c;

	iov[0].iov_base = BI_SILENT;
	iov[0].iov_len = sizeof(BI_SILENT) - 1;
	iov[1].iov_base = "\n";
	iov[1].iov_len = 1;

	rl.rlim_cur = 0;
	rl.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rl);

	(void)setpriority(PRIO_PROCESS, 0, 0);

	openlog("login", LOG_ODELAY, LOG_AUTH);

	while ((c = getopt(argc, argv, "v:s:")) != -1)
		switch (c) {
		case 'v':
			break;
		case 's':	/* service */
			if (strcmp(optarg, "login") != 0) {
				syslog(LOG_ERR, "%s: invalid service", optarg);
				exit(1);
			}
			break;
		default:
			syslog(LOG_ERR, "usage error");
			exit(1);
		}

	switch (argc - optind) {
	case 2:
		/* class is not used */
	case 1:
		username = argv[optind];
		break;
	default:
		syslog(LOG_ERR, "usage error");
		exit(1);
	}

	pwd = getpwnam_shadow(username);
	if (pwd) {
		if (pwd->pw_uid == 0) {
			syslog(LOG_ERR, "attempted root password change");
			pwd = NULL;
		} else if (*pwd->pw_passwd == '\0') {
			syslog(LOG_ERR, "%s attempting to add password",
			    username);
			pwd = NULL;
		}
	}

	if (pwd)
		hash = pwd->pw_passwd;

	(void)setpriority(PRIO_PROCESS, 0, -4);

	(void)printf("Changing local password for %s.\n", username);
	if ((readpassphrase("Old Password:", oldpass, sizeof(oldpass),
		    RPP_ECHO_OFF)) == NULL)
		exit(1);

	if (crypt_checkpass(oldpass, hash) != 0) {
		explicit_bzero(oldpass, strlen(oldpass));
		exit(1);
	}
	explicit_bzero(oldpass, strlen(oldpass));

	/*
	 * We rely on local_passwd() to block signals during the
	 * critical section.
	 */
	local_passwd(pwd->pw_name, 1);
	(void)writev(BACK_CHANNEL, iov, 2);
	exit(0);
}
