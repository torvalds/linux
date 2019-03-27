/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Doug Rabson
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
#include <sys/linker.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

static void
usage(void)
{
	fprintf(stderr, "usage: kldunload [-fv] -i id ...\n");
	fprintf(stderr, "       kldunload [-fv] [-n] name ...\n");
	exit(EX_USAGE);
}

#define OPT_NULL	0x00
#define OPT_ID		0x01
#define OPT_VERBOSE	0x02
#define OPT_FORCE	0x04

int
main(int argc, char** argv)
{
	struct kld_file_stat stat;
	int c, fileid, force, opt;
	char *filename;

	filename = NULL;
	opt = OPT_NULL;

	while ((c = getopt(argc, argv, "finv")) != -1) {
		switch (c) {
		case 'f':
			opt |= OPT_FORCE;
			break;
		case 'i':
			opt |= OPT_ID;
			break;
		case 'n':
			/* 
			 * XXX: For backward compatibility. Currently does
			 * nothing
			 */
			break;
		case 'v':
			opt |= OPT_VERBOSE;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		usage();

	while ((filename = *argv++) != NULL) {
		if (opt & OPT_ID) {
			fileid = atoi(filename);
			if (fileid < 0)
				errx(EXIT_FAILURE, "Invalid ID %s", optarg);
		} else {
			if ((fileid = kldfind(filename)) < 0)
				errx(EXIT_FAILURE, "can't find file %s",
				    filename);
		}
		if (opt & OPT_VERBOSE) {
			stat.version = sizeof(stat);
			if (kldstat(fileid, &stat) < 0)
				err(EXIT_FAILURE, "can't stat file");
			(void) printf("Unloading %s, id=%d\n", stat.name,
			    fileid);
		}
		if (opt & OPT_FORCE)
			force = LINKER_UNLOAD_FORCE;
		else
			force = LINKER_UNLOAD_NORMAL;

		if (kldunloadf(fileid, force) < 0)
			err(EXIT_FAILURE, "can't unload file");
	}

	return (EXIT_SUCCESS);
}
