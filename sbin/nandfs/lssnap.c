/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <time.h>

#include <fs/nandfs/nandfs_fs.h>
#include <libnandfs.h>

#include "nandfs.h"

#define NCPINFO	512

static void
lssnap_usage(void)
{

	fprintf(stderr, "usage:\n");
	fprintf(stderr, "\tlssnap node\n");
}

static void
print_cpinfo(struct nandfs_cpinfo *cpinfo)
{
	struct tm tm;
	time_t t;
	char timebuf[128];

	t = (time_t)cpinfo->nci_create;
	localtime_r(&t, &tm);
	strftime(timebuf, sizeof(timebuf), "%F %T", &tm);

	printf("%20llu  %s\n", (unsigned long long)cpinfo->nci_cno, timebuf);
}

int
nandfs_lssnap(int argc, char **argv)
{
	struct nandfs_cpinfo *cpinfos;
	struct nandfs fs;
	uint64_t next;
	int error, nsnap, i;

	if (argc != 1) {
		lssnap_usage();
		return (EX_USAGE);
	}

	cpinfos = malloc(sizeof(*cpinfos) * NCPINFO);
	if (cpinfos == NULL) {
		fprintf(stderr, "cannot allocate memory\n");
		return (-1);
	}

	nandfs_init(&fs, argv[0]);
	error = nandfs_open(&fs);
	if (error == -1) {
		fprintf(stderr, "nandfs_open: %s\n", nandfs_errmsg(&fs));
		goto out;
	}

	for (next = 1; next != 0; next = cpinfos[nsnap - 1].nci_next) {
		nsnap = nandfs_get_snap(&fs, next, cpinfos, NCPINFO);
		if (nsnap < 1)
			break;

		for (i = 0; i < nsnap; i++)
			print_cpinfo(&cpinfos[i]);
	}

	if (nsnap == -1)
		fprintf(stderr, "nandfs_get_snap: %s\n", nandfs_errmsg(&fs));

out:
	nandfs_close(&fs);
	nandfs_destroy(&fs);
	free(cpinfos);
	return (error);
}
