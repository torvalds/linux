/*
 * $OpenBSD: login_ldap.c,v 1.3 2021/09/02 20:57:58 deraadt Exp $
 * Copyright (c) 2002 Institute for Open Systems Technology Australia (IFOST)
 * Copyright (c) 2007 Michael Erdely <merdely@openbsd.org>
 * Copyright (c) 2019 Martijn van Duren <martijn@openbsd.org>
 *
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <limits.h>
#include <unistd.h>
#include <login_cap.h>
#include <stdarg.h>
#include <bsd_auth.h>

#include "aldap.h"
#include "login_ldap.h"

int	auth_ldap(char *, char *, char *);
__dead void usage(void);

static void
handler(int signo)
{
	_exit(1);
}

/*
 * Authenticate a user using LDAP
 */

int
main(int argc, char **argv)
{
	int c;
	char *class, *service, *username, *password;
	char backbuf[BUFSIZ];
	FILE *back = NULL;

	password = class = NULL;
	service = LOGIN_DEFSERVICE;

	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGALRM, handler);
	(void)setpriority(PRIO_PROCESS, 0, 0);

	openlog("login_ldap", LOG_ODELAY, LOG_AUTH);

	/*
	 * Usage: login_xxx [-s service] [-v var] user [class]
	 */
	while ((c = getopt(argc, argv, "ds:v:")) != -1) {
		switch(c) {
		case 'd':
			back = stdout;
			debug = 1;
			break;
		case 'v':
			/* For compatibility */
			break;
		case 's':       /* service */
			service = optarg;
			if (strcmp(service, "login") != 0 &&
				strcmp(service, "challenge") != 0 &&
				strcmp(service, "response") != 0) {
					dlog(0, "%s not supported", service);
					return 1;
				}
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	switch (argc) {
	case 2:
		class = argv[1];
		/* FALLTHROUGH */
	case 1:
		username = argv[0];
		break;
	default:
		usage();
	}

	if (strlen(username) >= LOGIN_NAME_MAX) {
		dlog(0, "username too long");
		return 1;
	}

	/*
	 * Filedes 3 is the back channel, where we send back results.
	 */
	if (back == NULL && (back = fdopen(3, "a")) == NULL)  {
		dlog(0, "error reopening back channel");
		return 1;
	}

	if (strcmp(service, "login") == 0) {
		password = getpass("Password: ");
	} else if (strcmp(service, "response") == 0) {
		int n;

		/*
		 * format of back channel message
		 *
		 * c h a l l e n g e \0 p a s s w o r d \0
		 *
		 * challenge can be NULL (so can password too
		 * i suppose).
		 */

		n = read(3, backbuf, sizeof(backbuf));
		if (n == -1) {
			dlog(0, "read error from back channel");
			return 1;
		}

		/* null challenge */
		if (backbuf[0] == '\0')
			password = backbuf + 1;
		/* skip the first string to get the password */
		else if ((password = strchr(backbuf, '\0')) != NULL)
			password++;
		else
			dlog(0, "protocol error on back channel");

	} else if (strcmp(service, "challenge") == 0) {
		(void)fprintf(back, BI_SILENT "\n");
		return 0;
	}

	if (password == NULL || password[0] == '\0') {
		dlog(1, "No password specified");
		(void)fprintf(back, BI_REJECT "\n");
		return 1;
	}

	if (auth_ldap(username, class, password))
		(void)fprintf(back, BI_AUTH "\n");
	else
		(void)fprintf(back, BI_REJECT "\n");

	explicit_bzero(password, strlen(password));

	closelog();

	return 0;
}

int
auth_ldap(char *user, char *class, char *pass)
{
	struct auth_ctx ctx;
	const char *cfile = NULL;
	char *tmp;
	login_cap_t	*lc;

	memset(&ctx, 0, sizeof(struct auth_ctx));
	TAILQ_INIT(&(ctx.s));

	if ((lc = login_getclass(class)) != NULL) {
		cfile = login_getcapstr(lc, "ldap-conffile", NULL, NULL);
	}

	ctx.user = user;

	if (!parse_conf(&ctx, cfile != NULL ? cfile : "/etc/login_ldap.conf"))
		return 0;


	if (!conn(&ctx)) {
		dlog(0, "ldap_open failed");
		return 0;
	}

	if (ctx.basedn == NULL) {
		if (!bind_password(&ctx, ctx.binddn, pass))
			return 0;
		ctx.userdn = ctx.binddn;
	} else {
		if (!bind_password(&ctx, ctx.binddn, ctx.bindpw))
			return 0;

		dlog(1, "bind success!");

		ctx.userdn = search(&ctx, ctx.basedn, ctx.filter, ctx.scope);
		if (ctx.userdn == NULL) {
			dlog(1, "no user!");
			return 0;
		}

	}
	dlog(1, "userdn %s", ctx.userdn);

	if (ctx.gbasedn != NULL && ctx.gfilter != NULL) {
		tmp = ctx.gfilter;
		if ((ctx.gfilter = parse_filter(&ctx, ctx.gfilter)) == NULL)
			return 0;
		free(tmp);
		tmp = ctx.gbasedn;
		if ((ctx.gbasedn = parse_filter(&ctx, ctx.gbasedn)) == NULL)
			return 0;
		free(tmp);

		if (search(&ctx, ctx.gbasedn, ctx.gfilter, ctx.gscope) == NULL) {
			dlog(0, "user authenticated but failed group check: "
			    "%s", ctx.gfilter);
			return 0;
		}
		dlog(1, "group filter matched!");
	}
	

	if (ctx.basedn != NULL) {
		if (!bind_password(&ctx, ctx.userdn, pass)) {
			dlog(0, "user bind failed, dn: %s", ctx.userdn);
			return 0;
		}
	}

	dlog(1, "user bind success!");

	return 1;
}

__dead void
usage(void)
{
	dlog(0, "usage: %s [-d] [-s service] [-v name=value] user [class]", getprogname());
	exit(1);
}
