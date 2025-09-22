/*	$OpenBSD: login_passwd.c,v 1.20 2021/11/16 21:55:21 deraadt Exp $	*/

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
 *	BSDI $From: login_passwd.c,v 1.11 1997/08/08 18:58:24 prb Exp $
 */

#include <sys/types.h>
#include <sys/resource.h>

#include <errno.h>
#include <pwd.h>
#include <readpassphrase.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <util.h>

#include <login_cap.h>

int
main(int argc, char *argv[])
{
	FILE *back = NULL;
	char *class = NULL, *username = NULL, *wheel = NULL;
	char response[1024], pbuf[1024], *pass = "";
	int ch, rc, mode = 0, lastchance = 0;
	struct passwd *pwd;

	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)setpriority(PRIO_PROCESS, 0, 0);

	openlog(NULL, LOG_ODELAY, LOG_AUTH);

	while ((ch = getopt(argc, argv, "ds:v:")) != -1) {
		switch (ch) {
		case 'd':
			back = stdout;
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
		case 'v':
			if (strncmp(optarg, "wheel=", 6) == 0)
				wheel = optarg + 6;
			else if (strncmp(optarg, "lastchance=", 11) == 0)
				lastchance = (strcmp(optarg + 11, "yes") == 0);
			break;
		default:
			syslog(LOG_ERR, "usage error");
			exit(1);
		}
	}

	switch (argc - optind) {
	case 2:
		class = argv[optind + 1];
		/* FALLTHROUGH */
	case 1:
		username = argv[optind];
		break;
	default:
		syslog(LOG_ERR, "usage error");
		exit(1);
	}

	/* get the password hash before pledge(2) or it will return '*' */
	pwd = getpwnam_shadow(username);

	if (pledge("stdio rpath tty id", NULL) == -1) {
		syslog(LOG_ERR, "pledge: %m");
		exit(1);
	}

	if (back == NULL && (back = fdopen(3, "r+")) == NULL) {
		syslog(LOG_ERR, "reopening back channel: %m");
		exit(1);
	}
	if (wheel != NULL && strcmp(wheel, "yes") != 0) {
		fprintf(back, BI_VALUE " errormsg %s\n",
		    "you are not in group wheel");
		goto reject;
	}

	if (mode == 1) {
		fprintf(back, BI_SILENT "\n");
		exit(0);
	}

	(void)setpriority(PRIO_PROCESS, 0, -4);

	if (mode == 2) {
		mode = 0;
		ch = -1;
		while (++ch < sizeof(response) &&
		    read(3, &response[ch], 1) == 1) {
			if (response[ch] == '\0' && ++mode == 2)
				break;
			if (response[ch] == '\0' && mode == 1)
				pass = response + ch + 1;
		}
		if (mode < 2) {
			syslog(LOG_ERR, "protocol error on back channel");
			exit(1);
		}
	} else {
		if (pwd == NULL || *pwd->pw_passwd != '\0') {
			pass = readpassphrase("Password:", pbuf, sizeof(pbuf),
			    RPP_ECHO_OFF);
			if (pass == NULL)
				goto reject;
		}
	}

	if (pledge("stdio rpath", NULL) == -1) {
		syslog(LOG_ERR, "pledge: %m");
		exit(1);
	}

	rc = crypt_checkpass(pass, pwd ? pwd->pw_passwd : NULL);
	explicit_bzero(pass, strlen(pass));
	if (rc == 0) {
		if (login_check_expire(back, pwd, class, lastchance) == 0) {
		    fprintf(back, BI_AUTH "\n");
		    exit(0);
		}
	}
reject:
	fprintf(back, BI_REJECT "\n");
	exit(1);
}
