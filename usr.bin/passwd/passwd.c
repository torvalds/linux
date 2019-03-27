/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2002 Networks Associates Technologies, Inc.
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

#include <sys/param.h>

#include <err.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include <security/pam_appl.h>
#include <security/openpam.h>

static pam_handle_t *pamh;
static struct pam_conv pamc = {
	openpam_ttyconv,
	NULL
};

static char	*yp_domain;
static char	*yp_host;

static void
usage(void)
{
	fprintf(stderr, "usage: passwd [-ly] [-d domain] [-h host] [user]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char hostname[MAXHOSTNAMELEN];
	struct passwd *pwd = NULL; /* Keep compiler happy. */
	int o, pam_err;
	uid_t uid;

	while ((o = getopt(argc, argv, "d:h:loy")) != -1)
		switch (o) {
		case 'd':
			yp_domain = optarg;
			break;
		case 'h':
			yp_host = optarg;
			break;
		case 'l':
		case 'o':
		case 'y':
			/* compatibility */
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	uid = getuid();

	switch (argc) {
	case 0:
		if ((pwd = getpwuid(uid)) == NULL)
			errx(1, "who are you?");
		break;
	case 1:
		if ((pwd = getpwnam(*argv)) == NULL)
			errx(1, "%s: no such user", *argv);
		break;
	default:
		usage();
	}

	if (uid != 0 && uid != pwd->pw_uid)
		errx(1, "permission denied");

	/* check where the user's from */
	switch (pwd->pw_fields & _PWF_SOURCE) {
	case _PWF_FILES:
		fprintf(stderr, "Changing local password for %s\n",
		    pwd->pw_name);
		break;
	case _PWF_NIS:
		fprintf(stderr, "Changing NIS password for %s\n",
		    pwd->pw_name);
		break;
	default:
		/* XXX: Green men ought to be supported via PAM. */
		errx(1, 
	  "Sorry, `passwd' can only change passwords for local or NIS users.");
	}

#define pam_check(func) do { \
	if (pam_err != PAM_SUCCESS) { \
		if (pam_err == PAM_AUTH_ERR || pam_err == PAM_PERM_DENIED || \
		    pam_err == PAM_AUTHTOK_RECOVERY_ERR) \
			warnx("sorry"); \
		else \
			warnx("%s(): %s", func, pam_strerror(pamh, pam_err)); \
		goto end; \
	} \
} while (0)

	/* initialize PAM */
	pam_err = pam_start("passwd", pwd->pw_name, &pamc, &pamh);
	pam_check("pam_start");

	pam_err = pam_set_item(pamh, PAM_TTY, ttyname(STDERR_FILENO));
	pam_check("pam_set_item");
	gethostname(hostname, sizeof hostname);
	pam_err = pam_set_item(pamh, PAM_RHOST, hostname);
	pam_check("pam_set_item");
	pam_err = pam_set_item(pamh, PAM_RUSER, getlogin());
	pam_check("pam_set_item");

	/* set YP domain and host */
	pam_err = pam_set_data(pamh, "yp_domain", yp_domain, NULL);
	pam_check("pam_set_data");
	pam_err = pam_set_data(pamh, "yp_server", yp_host, NULL);
	pam_check("pam_set_data");

	/* set new password */
	pam_err = pam_chauthtok(pamh, 0);
	pam_check("pam_chauthtok");

 end:
	pam_end(pamh, pam_err);
	exit(pam_err == PAM_SUCCESS ? 0 : 1);
}
