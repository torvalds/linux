/*	$OpenBSD: login_chpass.c,v 1.22 2024/09/22 04:19:22 jsg Exp $	*/

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
 *	BSDI $From: login_chpass.c,v 1.3 1996/08/21 21:01:48 prb Exp $
 */

#include <sys/resource.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define	_PATH_LOGIN_LCHPASS	"/usr/libexec/auth/login_lchpass"

void	local_chpass(char **);

int
main(int argc, char *argv[])
{
	struct rlimit rl;
	int c;

	rl.rlim_cur = 0;
	rl.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rl);

	(void)setpriority(PRIO_PROCESS, 0, 0);

	if (pledge("stdio exec", NULL) == -1)
		err(1, "pledge");

	openlog("login", LOG_ODELAY, LOG_AUTH);

	while ((c = getopt(argc, argv, "s:v:")) != -1)
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
		break;
	default:
		syslog(LOG_ERR, "usage error");
		exit(1);
	}

	local_chpass(argv);
	/* NOTREACHED */
	exit(0);
}

void
local_chpass(char *argv[])
{

	/* login_lchpass doesn't check instance so don't bother restoring it */
	argv[0] = strrchr(_PATH_LOGIN_LCHPASS, '/') + 1;
	execv(_PATH_LOGIN_LCHPASS, argv);
	syslog(LOG_ERR, "%s: %m", _PATH_LOGIN_LCHPASS);
	exit(1);
}
