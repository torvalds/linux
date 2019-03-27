/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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

#if 0
#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1989, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#ifndef lint
static char sccsid[] = "@(#)nfsiod.c	8.4 (Berkeley) 5/3/95";
#endif
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/syslog.h>
#include <sys/wait.h>
#include <sys/linker.h>
#include <sys/mount.h>
#include <sys/sysctl.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define	MAXNFSDCNT      20

static void
usage(void)
{
	(void)fprintf(stderr, "usage: nfsiod [-n num_servers]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int ch;
	struct xvfsconf vfc;
	int error;
	unsigned int iodmin, iodmax, num_servers;
	size_t len;

	error = getvfsbyname("nfs", &vfc);
	if (error) {
		if (kldload("nfs") == -1)
			err(1, "kldload(nfs)");
		error = getvfsbyname("nfs", &vfc);
	}
	if (error)
		errx(1, "NFS support is not available in the running kernel");

	num_servers = 0;
	while ((ch = getopt(argc, argv, "n:")) != -1)
		switch (ch) {
		case 'n':
			num_servers = atoi(optarg);
			if (num_servers < 1) {
				warnx("nfsiod count %u; reset to %d",
				    num_servers, 1);
				num_servers = 1;
			}
			if (num_servers > MAXNFSDCNT) {
				warnx("nfsiod count %u; reset to %d",
				    num_servers, MAXNFSDCNT);
				num_servers = MAXNFSDCNT;
			}
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc > 0)
		usage();

	len = sizeof iodmin;
	error = sysctlbyname("vfs.nfs.iodmin", &iodmin, &len, NULL, 0);
	if (error < 0)
		err(1, "sysctlbyname(\"vfs.nfs.iodmin\")");
	len = sizeof iodmax;
	error = sysctlbyname("vfs.nfs.iodmax", &iodmax, &len, NULL, 0);
	if (error < 0)
		err(1, "sysctlbyname(\"vfs.nfs.iodmax\")");
	if (num_servers == 0) {		/* no change */
		printf("vfs.nfs.iodmin=%u\nvfs.nfs.iodmax=%u\n",
		    iodmin, iodmax);
		exit(0);
	}
	/* Catch the case where we're lowering num_servers below iodmin */
	if (iodmin > num_servers) {
		iodmin = num_servers;
		error = sysctlbyname("vfs.nfs.iodmin", NULL, 0, &iodmin,
		    sizeof iodmin);
		if (error < 0)
			err(1, "sysctlbyname(\"vfs.nfs.iodmin\")");
	}
	iodmax = num_servers;
	error = sysctlbyname("vfs.nfs.iodmax", NULL, 0, &iodmax, sizeof iodmax);
	if (error < 0)
		err(1, "sysctlbyname(\"vfs.nfs.iodmax\")");
	exit (0);
}

