/*	$OpenBSD: cu.c,v 1.19 2006/05/25 08:41:52 jmc Exp $	*/
/*	$NetBSD: cu.c,v 1.5 1997/02/11 09:24:05 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)cu.c	8.1 (Berkeley) 6/6/93";
static const char rcsid[] = "$OpenBSD: cu.c,v 1.19 2006/05/25 08:41:52 jmc Exp $";
#endif
#endif /* not lint */

#include "tip.h"

static void	cuusage(void);

/*
 * Botch the interface to look like cu's
 */
void
cumain(int argc, char *argv[])
{
	int ch, i, parity;
	long l;
	char *cp;
	static char sbuf[12];

	if (argc < 2)
		cuusage();
	CU = DV = NOSTR;
	BR = DEFBR;
	parity = 0;	/* none */

	/*
	 * We want to accept -# as a speed.  It's easiest to look through
	 * the arguments, replace -# with -s#, and let getopt() handle it.
	 */
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' &&
		    argv[i][1] >= '0' && argv[i][1] <= '9') {
			asprintf(&cp, "-s%s", argv[i] + 1);
			if (cp == NULL) {
				fprintf(stderr,
				    "%s: cannot convert -# to -s#\n",
				    __progname);
				exit(3);
			}
			argv[i] = cp;
		}
	}

	while ((ch = getopt(argc, argv, "a:l:s:htoe")) != -1) {
		switch (ch) {
		case 'a':
			CU = optarg;
			break;
		case 'l':
			if (DV != NULL) {
				fprintf(stderr,
				    "%s: cannot specificy multiple -l options\n",
				    __progname);
				exit(3);
			}
			if (strchr(optarg, '/'))
				DV = optarg;
			else
				asprintf(&DV, "/dev/%s", optarg);
			break;
		case 's':
			l = strtol(optarg, &cp, 10);
			if (*cp != '\0' || l < 0 || l >= INT_MAX) {
				fprintf(stderr, "%s: unsupported speed %s\n",
				    __progname, optarg);
				exit(3);
			}
			BR = (int)l;
			break;
		case 'h':
			setboolean(value(LECHO), TRUE);
			HD = TRUE;
			break;
		case 't':
			HW = 1, DU = -1;
			break;
		case 'o':
			if (parity != 0)
				parity = 0;	/* -e -o */
			else
				parity = 1;	/* odd */
			break;
		case 'e':
			if (parity != 0)
				parity = 0;	/* -o -e */
			else
				parity = -1;	/* even */
			break;
		default:
			cuusage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	switch (argc) {
	case 1:
		PN = argv[0];
		break;
	case 0:
		break;
	default:
		cuusage();
		break;
	}

	signal(SIGINT, cleanup);
	signal(SIGQUIT, cleanup);
	signal(SIGHUP, cleanup);
	signal(SIGTERM, cleanup);
	signal(SIGCHLD, SIG_DFL);

	/*
	 * The "cu" host name is used to define the
	 * attributes of the generic dialer.
	 */
	(void)snprintf(sbuf, sizeof(sbuf), "cu%ld", BR);
	if ((i = hunt(sbuf)) == 0) {
		printf("all ports busy\n");
		exit(3);
	}
	if (i == -1) {
		printf("link down\n");
		(void)uu_unlock(uucplock);
		exit(3);
	}
	setbuf(stdout, NULL);
	loginit();
	user_uid();
	vinit();
	switch (parity) {
	case -1:
		setparity("even");
		break;
	case 1:
		setparity("odd");
		break;
	default:
		setparity("none");
		break;
	}
	setboolean(value(VERBOSE), FALSE);
	if (HW && ttysetup(BR)) {
		fprintf(stderr, "%s: unsupported speed %ld\n",
		    __progname, BR);
		daemon_uid();
		(void)uu_unlock(uucplock);
		exit(3);
	}
	if (con()) {
		printf("Connect failed\n");
		daemon_uid();
		(void)uu_unlock(uucplock);
		exit(1);
	}
	if (!HW && ttysetup(BR)) {
		fprintf(stderr, "%s: unsupported speed %ld\n",
		    __progname, BR);
		daemon_uid();
		(void)uu_unlock(uucplock);
		exit(3);
	}
}

static void
cuusage(void)
{
	fprintf(stderr, "usage: cu [-ehot] [-a acu] [-l line] "
	    "[-s speed | -speed] [phone-number]\n");
	exit(8);
}
