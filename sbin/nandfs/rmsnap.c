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
#include <limits.h>
#include <sysexits.h>

#include <fs/nandfs/nandfs_fs.h>
#include <libnandfs.h>

#include "nandfs.h"

static void
rmsnap_usage(void)
{

	fprintf(stderr, "usage:\n");
	fprintf(stderr, "\trmsnap snap node\n");
}

int
nandfs_rmsnap(int argc, char **argv)
{
	struct nandfs fs;
	uint64_t cpno;
	int error;

	if (argc != 2) {
		rmsnap_usage();
		return (EX_USAGE);
	}

	cpno = strtoll(argv[0], (char **)NULL, 10);
	if (cpno == 0) {
		fprintf(stderr, "%s must be a number greater than 0\n",
		    argv[0]);
		return (EX_USAGE);
	}

	nandfs_init(&fs, argv[1]);
	error = nandfs_open(&fs);
	if (error == -1) {
		fprintf(stderr, "nandfs_open: %s\n", nandfs_errmsg(&fs));
		goto out;
	}

	error = nandfs_delete_snap(&fs, cpno);
	if (error == -1)
		fprintf(stderr, "nandfs_delete_snap: %s\n", nandfs_errmsg(&fs));

out:
	nandfs_close(&fs);
	nandfs_destroy(&fs);
	return (error);
}
