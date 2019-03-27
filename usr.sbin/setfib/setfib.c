/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2008 Cisco Systems, All rights reserved
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
 * 
 * setfib file skelaton taken from nice.c
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/sysctl.h>

static void usage(void);

int
main(int argc, char *argv[])
{
	long fib = 0;
	int ch;
	char *ep;
	int	numfibs;
	size_t intsize = sizeof(int);

        if (sysctlbyname("net.fibs", &numfibs, &intsize, NULL, 0) == -1)
		errx(1, "Multiple FIBS not supported");
	if (argc < 2)
		usage();
	ep = argv[1];
	/*
	 * convert -N or N to -FN. (N is a number)
	 */
	if (ep[0]== '-' && isdigit((unsigned char)ep[1]))
		ep++;
	if (isdigit((unsigned char)*ep))
               if (asprintf(&argv[1], "-F%s", ep) < 0)
                        err(1, "asprintf");

	while ((ch = getopt(argc, argv, "F:")) != -1) {
		switch (ch) {
		case 'F':
			errno = 0;
			fib = strtol(optarg, &ep, 10);
			if (ep == optarg || *ep != '\0' || errno ||
			    fib < 0 || fib >= numfibs)
				errx(1, "%s: invalid FIB (max %d)",
				    optarg, numfibs - 1);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	errno = 0;
	if (setfib((int)fib))
		warn("setfib");
	execvp(*argv, argv);
	err(errno == ENOENT ? 127 : 126, "%s", *argv);
}

static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: setfib [-[F]]value command\n");
	exit(1);
}
