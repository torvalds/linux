/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Hudson River Trading LLC
 * Written by: John H. Baldwin <jhb@FreeBSD.org>
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

#include <sys/procctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
usage(void)
{

	fprintf(stderr, "usage: protect [-i] command\n");
	fprintf(stderr, "       protect [-cdi] -g pgrp | -p pid\n");
	exit(1);
}

static id_t
parse_id(char *id)
{
	static bool first = true;
	long value;
	char *ch;

	if (!first) {
		warnx("only one -g or -p flag is permitted");
		usage();
	}
	value = strtol(id, &ch, 0);
	if (*ch != '\0') {
		warnx("invalid process id");
		usage();
	}
	return (value);
}

int
main(int argc, char *argv[])
{
	idtype_t idtype;
	id_t id;
	int ch, flags;
	bool descend, inherit, idset;

	idtype = P_PID;
	id = getpid();
	flags = PPROT_SET;
	descend = inherit = idset = false;
	while ((ch = getopt(argc, argv, "cdig:p:")) != -1)
		switch (ch) {
		case 'c':
			flags = PPROT_CLEAR;
			break;
		case 'd':
			descend = true;
			break;
		case 'i':
			inherit = true;
			break;
		case 'g':
			idtype = P_PGID;
			id = parse_id(optarg);
			idset = true;
			break;
		case 'p':
			idtype = P_PID;
			id = parse_id(optarg);
			idset = true;
			break;
		}
	argc -= optind;
	argv += optind;

	if ((idset && argc != 0) || (!idset && (argc == 0 || descend)))
		usage();

	if (descend)
		flags |= PPROT_DESCEND;
	if (inherit)
		flags |= PPROT_INHERIT;
	if (procctl(idtype, id, PROC_SPROTECT, &flags) == -1)
		err(1, "procctl");

	if (argc != 0) {
		errno = 0;
		execvp(*argv, argv);
		err(errno == ENOENT ? 127 : 126, "%s", *argv);
	}
	return (0);
}
