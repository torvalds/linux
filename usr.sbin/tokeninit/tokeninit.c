/*	$OpenBSD: tokeninit.c,v 1.13 2017/05/03 09:51:39 mestre Exp $	*/

/*-
 * Copyright (c) 1995 Migration Associates Corp. All Rights Reserved
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
 *	BSDI $From: tokeninit.c,v 1.1 1996/08/26 20:27:28 prb Exp
 */

#include <sys/signal.h>
#include <sys/resource.h>
#include <sys/time.h>

#include <err.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <readpassphrase.h>

#include "token.h"
#include "tokendb.h"

static	char	*prompt_for_secret(int, char*);
static	int	parse_secret(int, char *, unsigned char *);

int
main(int argc, char **argv)
{
	unsigned int cmd = TOKEN_INITUSER;
	int	c;
	int	errors = 0;
	int	verbose = 0;
	int	hexformat = 0;
	int	modes = 0;
	char	seed[80];
	unsigned char	secret[9];
	char	*optstr;
	char	*p = NULL;

	struct rlimit cds;

	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)setpriority(PRIO_PROCESS, 0, 0);

	openlog(NULL, LOG_ODELAY, LOG_AUTH);

	/*
	 * Make sure we never dump core as we might have a
	 * valid user shared-secret in memory.
	 */

	cds.rlim_cur = 0;
	cds.rlim_max = 0;
	if (setrlimit(RLIMIT_CORE, &cds) < 0)
		syslog(LOG_ERR, "couldn't set core dump size to 0: %m");

	if (pledge("stdio rpath wpath cpath fattr flock getpw tty", NULL) == -1)
		err(1, "pledge");

	if (token_init(argv[0]) < 0) {
		syslog(LOG_ERR, "unknown token type");
		errx(1, "unknown token type");
	}

	if (tt->options & TOKEN_HEXINIT)
		optstr = "fhm:sv";
	else
		optstr = "fm:sv";

	while ((c = getopt(argc, argv, optstr)) != -1)
		switch (c) {
		case 'f':	/* force initialize existing user account */
			cmd |= TOKEN_FORCEINIT;
			break;

		case 'h':
			hexformat = 1;
			break;

		case 'm':
			if ((c = token_mode(optarg)))
				modes |= c;
			else
				errx(1, "unknown mode");
			break;

		case 's':	/* generate seed during initialization */
			cmd |= TOKEN_GENSECRET;
			break;

		case 'v':	/* verbose */
			verbose = 1;
			break;
		default:
			fprintf(stderr,
			   "usage: %sinit [-f%ssv] [-m mode] user ...\n",
			    tt->name, (tt->options & TOKEN_HEXINIT) ? "h" : "");
			exit(1);
		}

	if ((modes & ~TOKEN_RIM) == 0)
		modes |= tt->defmode;

	argc -= optind;
	argv = &argv[optind];

	while (argc--) {
		if (verbose) {
			printf("Adding %s to %s database\n", *argv, tt->proper);
			fflush(stdout);
		}
		if (!(cmd & TOKEN_GENSECRET)) {
			p = prompt_for_secret(hexformat, *argv);
			if (!readpassphrase(p, seed, sizeof(seed), RPP_ECHO_ON) ||
			    seed[0] == '\0') {
				fprintf(stderr,
				    "%sinit: No seed supplied for token.\n",
				    tt->name);
				exit(1);
			}
			explicit_bzero(secret, sizeof(secret));
			if (parse_secret(hexformat, seed, secret)) {
				fprintf(stderr,
				    "%sinit: Invalid secret entered.\n",
				    tt->name);
				exit(1);
			}
		}
		switch (tokenuserinit(cmd, *argv, secret, modes)) {
		case 0:
			syslog(LOG_INFO, "User %s initialized in %s database",
			    *argv, tt->proper);
			break;
		case 1:
			warnx("%s already exists in %s database!",
			    *argv, tt->proper);
			syslog(LOG_INFO, "%s already exists in %s database",
			    *argv, tt->proper);
			errors++;
			break;
		case -1:
			warnx("Error initializing user %s in %s database.",
			    *argv, tt->proper);
			syslog(LOG_INFO,
			    "Error initializing user %s in %s database: %m",
			    *argv, tt->proper);
			errors++;
		}
		argv++;
	}
	exit(errors);
}

/*
 * Parse the 8 octal numbers or a 16 digit hex string into a token secret
 */

static	int
parse_secret(int hexformat, char *seed, unsigned char *secret)
{
	int i;
	unsigned int tmp[8];

	if (hexformat) {
		if ((i = sscanf(seed, "%02x %02x %02x %02x %02x %02x %02x %02x",
		    &tmp[0], &tmp[1], &tmp[2], &tmp[3],
		    &tmp[4], &tmp[5], &tmp[6], &tmp[7])) != 8)
			return (-1);
	} else {
		if ((i = sscanf(seed, "%o %o %o %o %o %o %o %o",
		    &tmp[0], &tmp[1], &tmp[2], &tmp[3],
		    &tmp[4], &tmp[5], &tmp[6], &tmp[7])) != 8)
			return (-1);
	}
	for (i=0; i < 8; i++)
		secret[i] = tmp[i] & 0xff;

	return (0);
}

/*
 * Prompt user for seed for token
 */

static	char *
prompt_for_secret(int hexformat, char* username)
{
	static char prompt[1024];
	if (hexformat)
		snprintf(prompt, sizeof prompt,
		    "Enter a 16 digit hexadecimal number "
		    "as a seed for %s\'s token:\n", username);
	else
		snprintf(prompt, sizeof prompt,
		    "Enter a series of 8 3-digit octal numbers "
		    "as a seed for %s\'s token:\n", username);
	return prompt;
}
