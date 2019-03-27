/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1998 Dag-Erling Coïdan Smørgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>

static void __dead2
usage(void)
{

	fprintf(stderr, "usage: chkgrp [-q] [groupfile]\n");
	exit(EX_USAGE);
}

int
main(int argc, char *argv[])
{
	FILE *gf;
	unsigned long gid;
	unsigned int i;
	size_t len;
	int opt, quiet;
	int n = 0, k, e = 0;
	const char *cp, *f[4], *gfn, *p;
	char *line;

	quiet = 0;
	while ((opt = getopt(argc, argv, "q")) != -1) {
		switch (opt) {
		case 'q':
			quiet = 1;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc == 0)
		gfn = "/etc/group";
	else if (argc == 1)
		gfn = argv[0];
	else
		usage();

	/* open group file */
	if ((gf = fopen(gfn, "r")) == NULL)
		err(EX_NOINPUT, "%s", gfn);

	/* check line by line */
	while (++n) {
		if ((line = fgetln(gf, &len)) == NULL)
			break;
		if (len > 0 && line[len - 1] != '\n') {
			warnx("%s: line %d: no newline character", gfn, n);
			e = 1;
		}
		while (len && isspace(line[len-1]))
			len--;

		/* ignore blank lines and comments */
		for (p = line; p < line + len; p++)
			if (!isspace(*p)) break;
		if (!len || *p == '#')
			continue;

		/*
		 * Hack: special case for + line
		 */
		if (strncmp(line, "+:::", len) == 0 ||
		    strncmp(line, "+:*::", len) == 0)
			continue;

		/*
		 * A correct group entry has four colon-separated fields,
		 * the third of which must be entirely numeric and the
		 * fourth of which may be empty.
		 */
		for (i = k = 0; k < 4; k++) {
			for (f[k] = line + i; i < len && line[i] != ':'; i++)
				/* nothing */ ;
			if (k < 3 && line[i] != ':')
				break;
			line[i++] = 0;
		}

		if (k < 4) {
			warnx("%s: line %d: missing field(s)", gfn, n);
			while (k < 4)
				f[k++] = "";
			e = 1;
		}

		for (cp = f[0] ; *cp ; cp++) {
			if (!isalnum(*cp) && *cp != '.' && *cp != '_' &&
			    *cp != '-' && (cp > f[0] || *cp != '+')) {
				warnx("%s: line %d: '%c' invalid character",
				    gfn, n, *cp);
				e = 1;
			}
		}

		for (cp = f[3] ; *cp ; cp++) {
			if (!isalnum(*cp) && *cp != '.' && *cp != '_' &&
			    *cp != '-' && *cp != ',') {
				warnx("%s: line %d: '%c' invalid character",
				    gfn, n, *cp);
				e = 1;
			}
		}

		/* check if fourth field ended with a colon */
		if (i < len) {
			warnx("%s: line %d: too many fields", gfn, n);
			e = 1;
		}
	
		/* check that none of the fields contain whitespace */
		for (k = 0; k < 4; k++) {
			if (strcspn(f[k], " \t") != strlen(f[k])) {
				warnx("%s: line %d: field %d contains whitespace",
				    gfn, n, k+1);
				e = 1;
			}
		}

		/* check that the GID is numeric */
		if (strspn(f[2], "0123456789") != strlen(f[2])) {
			warnx("%s: line %d: group id is not numeric", gfn, n);
			e = 1;
		}

		/* check the range of the group id */
		errno = 0;
		gid = strtoul(f[2], NULL, 10);
		if (errno != 0) {
			warnx("%s: line %d: strtoul failed", gfn, n);
		} else if (gid > GID_MAX) {
			warnx("%s: line %d: group id is too large (%ju > %ju)",
			    gfn, n, (uintmax_t)gid, (uintmax_t)GID_MAX);
			e = 1;
		}
	}

	/* check what broke the loop */
	if (ferror(gf))
		err(EX_IOERR, "%s: line %d", gfn, n);

	/* done */
	fclose(gf);
	if (e == 0 && quiet == 0)
		printf("%s is fine\n", gfn);
	exit(e ? EX_DATAERR : EX_OK);
}
