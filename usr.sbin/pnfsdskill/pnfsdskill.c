/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Rick Macklem
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <nfs/nfssvc.h>

#include <fs/nfs/nfsproto.h>
#include <fs/nfs/nfskpiport.h>
#include <fs/nfs/nfs.h>

static void usage(void);

static struct option longopts[] = {
	{ "force",	no_argument,	NULL,	'f'	},
	{ NULL,		0,		NULL,	0	}
};

/*
 * This program disables use of a DS mirror.  The "dspath" command line
 * argument must be an exact match for the mounted-on path of the DS.
 * It should be done before any forced dismount is performed on the path
 * and should work even when the mount point is hung.
 */
int
main(int argc, char *argv[])
{
	struct nfsd_pnfsd_args pnfsdarg;
	int ch, force;

	if (geteuid() != 0)
		errx(1, "Must be run as root/su");
	force = 0;
	while ((ch = getopt_long(argc, argv, "f", longopts, NULL)) != -1) {
		switch (ch) {
		case 'f':
			force = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();

	if (force != 0)
		pnfsdarg.op = PNFSDOP_FORCEDELDS;
	else
		pnfsdarg.op = PNFSDOP_DELDSSERVER;
	pnfsdarg.dspath = *argv;
	if (nfssvc(NFSSVC_PNFSDS, &pnfsdarg) < 0)
		err(1, "Can't kill %s", *argv);
}

static void
usage(void)
{

	fprintf(stderr, "pnfsdsfile [-f] mounted-on-DS-dir\n");
	exit(1);
}

