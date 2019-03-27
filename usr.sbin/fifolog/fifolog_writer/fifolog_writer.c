/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008 Poul-Henning Kamp
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
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <err.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <poll.h>
#include <string.h>
#include <zlib.h>

#include "libfifolog.h"

static void
usage(void)
{
	fprintf(stderr,
	    "Usage: fifolog_writer [-w write-rate] [-s sync-rate] "
	    "[-z compression] file\n");
	exit(EX_USAGE);
}

int
main(int argc, char * const *argv)
{
	struct fifolog_writer *f;
	const char *es;
	struct pollfd pfd[1];
	char buf[BUFSIZ], *p;
	int i, c;
	unsigned w_opt = 10;
	unsigned s_opt = 60;
	unsigned z_opt = Z_BEST_COMPRESSION;

	while ((c = getopt(argc, argv, "w:s:z:")) != -1) {
		switch(c) {
		case 'w':
			w_opt = strtoul(optarg, NULL, 0);
			break;
		case 's':
			s_opt = strtoul(optarg, NULL, 0);
			break;
		case 'z':
			z_opt = strtoul(optarg, NULL, 0);
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();

	if (z_opt > 9)
		usage();

	if (w_opt > s_opt)
		usage();

	f = fifolog_write_new();
	assert(f != NULL);

	es = fifolog_write_open(f, argv[0], w_opt, s_opt, z_opt);
	if (es)
		err(1, "Error: %s", es);

	while (1) {
		pfd[0].fd = 0;
		pfd[0].events = POLLIN;
		i = poll(pfd, 1, 1000);
		if (i == 1) {
			if (fgets(buf, sizeof buf, stdin) == NULL)
				break;
			p = strchr(buf, '\0');
			assert(p != NULL);
			while (p > buf && isspace(p[-1]))
				p--;
			*p = '\0';
			if (*buf != '\0')
				fifolog_write_record_poll(f, 0, 0, buf, 0);
		} else if (i == 0)
			fifolog_write_poll(f, 0);
	}
	fifolog_write_close(f);
	return (0);
}
