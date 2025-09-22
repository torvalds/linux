/*	$OpenBSD: login_reject.c,v 1.18 2021/10/23 19:08:48 mestre Exp $	*/

/*-
 * Copyright (c) 1995 Berkeley Software Design, Inc. All rights reserved.
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
 *	BSDI $From: login_reject.c,v 1.5 1996/08/22 20:43:11 prb Exp $
 */

#include <sys/resource.h>

#include <login_cap.h>
#include <readpassphrase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	struct rlimit rl;
	FILE *back;
	char passbuf[1];
	int mode = 0, c;

	rl.rlim_cur = 0;
	rl.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rl);

	(void)setpriority(PRIO_PROCESS, 0, 0);

	if (pledge("stdio rpath tty", NULL) == -1) {
		syslog(LOG_AUTH|LOG_ERR, "pledge: %m");
		exit(1);
	}

	openlog("login", LOG_ODELAY, LOG_AUTH);

	while ((c = getopt(argc, argv, "v:s:")) != -1)
		switch (c) {
		case 'v':
			break;
		case 's':	/* service */
			if (strcmp(optarg, "login") == 0)
				mode = 0;
			else if (strcmp(optarg, "challenge") == 0)
				mode = 1;
			else if (strcmp(optarg, "response") == 0)
				mode = 2;
			else {
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
	case 1:
		break;
	default:
		syslog(LOG_ERR, "usage error");
		exit(1);
	}

	if (!(back = fdopen(3, "r+")))  {
		syslog(LOG_ERR, "reopening back channel: %m");
		exit(1);
	}
	if (mode == 1) {
		fprintf(back, BI_SILENT "\n");
		exit(0);
	}

	if (mode == 2) {
		mode = 0;
		c = -1;
		while (read(3, passbuf, 1) == 1) {
			if (passbuf[0] == '\0' && ++mode == 2)
				break;
		}
		if (mode < 2) {
			syslog(LOG_ERR, "protocol error on back channel");
			exit(1);
		}
	} else
		readpassphrase("Password:", passbuf, sizeof(passbuf), 0);

	crypt_checkpass("password", NULL);
	explicit_bzero(passbuf, sizeof(passbuf));

	fprintf(back, BI_REJECT "\n");
	exit(1);
}
