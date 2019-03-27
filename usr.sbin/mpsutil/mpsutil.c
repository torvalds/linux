/*-
 * Copyright (c) 2015 Netflix, Inc.
 * Written by: Scott Long <scottl@freebsd.org>
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
__RCSID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mpsutil.h"

SET_DECLARE(MPS_DATASET(top), struct mpsutil_command);
SET_DECLARE(MPS_DATASET(usage), struct mpsutil_usage);

int mps_unit;
int is_mps;

static void
usage(void)
{
	struct mpsutil_usage **cmd;
	const char *args, *desc;

	fprintf(stderr, "usage: %s [-u unit] <command> ...\n\n", getprogname());
	fprintf(stderr, "Commands include:\n");
	SET_FOREACH(cmd, MPS_DATASET(usage)) {
		if (*cmd == NULL) {
			fprintf(stderr, "\n");
		} else {
			(*cmd)->handler(&args, &desc);
			if (strncmp((*cmd)->set, "top", 3) == 0)
				fprintf(stderr, "%s %-30s\t%s\n",
				    (*cmd)->name, args, desc);
			else
				fprintf(stderr, "%s %s %-30s\t%s\n",
				    (*cmd)->set, (*cmd)->name, args, desc);
		}
	}
	exit(1);
}

static int
version(int ac, char **av)
{

	printf("%s: version %s", MPSUTIL_VERSION, getprogname());
#ifdef DEBUG
	printf(" (DEBUG)");
#endif
	printf("\n");
	return (0);
}

MPS_COMMAND(top, version, version, "", "version")

int
main(int ac, char **av)
{
	struct mpsutil_command **cmd;
	int ch;

	is_mps = !strcmp(getprogname(), "mpsutil");

	while ((ch = getopt(ac, av, "u:h?")) != -1) {
		switch (ch) {
		case 'u':
			mps_unit = atoi(optarg);
			break;
		case 'h':
		case '?':
			usage();
			return (1);
		}
	}

	av += optind;
	ac -= optind;

	/* getopt() eats av[0], so we can't use mpt_table_handler() directly. */
	if (ac == 0) {
		usage();
		return (1);
	}

	SET_FOREACH(cmd, MPS_DATASET(top)) {
		if (strcmp((*cmd)->name, av[0]) == 0) {
			if ((*cmd)->handler(ac, av))
				return (1);
			else
				return (0);
		}
	}
	warnx("Unknown command %s.", av[0]);
	return (1);
}

int
mps_table_handler(struct mpsutil_command **start, struct mpsutil_command **end,
    int ac, char **av)
{
	struct mpsutil_command **cmd;

	if (ac < 2) {
		warnx("The %s command requires a sub-command.", av[0]);
		return (EINVAL);
	}
	for (cmd = start; cmd < end; cmd++) {
		if (strcmp((*cmd)->name, av[1]) == 0)
			return ((*cmd)->handler(ac - 1, av + 1));
	}

	warnx("%s is not a valid sub-command of %s.", av[1], av[0]);
	return (ENOENT);
}

void
hexdump(const void *ptr, int length, const char *hdr, int flags)
{
	int i, j, k;
	int cols;
	const unsigned char *cp;
	char delim;

	if ((flags & HD_DELIM_MASK) != 0)
		delim = (flags & HD_DELIM_MASK) >> 8;
	else
		delim = ' ';

	if ((flags & HD_COLUMN_MASK) != 0)
		cols = flags & HD_COLUMN_MASK;
	else
		cols = 16;

	cp = ptr;
	for (i = 0; i < length; i+= cols) {
		if (hdr != NULL)
			printf("%s", hdr);

		if ((flags & HD_OMIT_COUNT) == 0)
			printf("%04x  ", i);

		if ((flags & HD_OMIT_HEX) == 0) {
			for (j = 0; j < cols; j++) {
				if (flags & HD_REVERSED)
					k = i + (cols - 1 - j);
				else
					k = i + j;
				if (k < length)
					printf("%c%02x", delim, cp[k]);
				else
					printf("   ");
			}
		}

		if ((flags & HD_OMIT_CHARS) == 0) {
			printf("  |");
			for (j = 0; j < cols; j++) {
				if (flags & HD_REVERSED)
					k = i + (cols - 1 - j);
				else
					k = i + j;
				if (k >= length)
					printf(" ");
				else if (cp[k] >= ' ' && cp[k] <= '~')
					printf("%c", cp[k]);
				else
					printf(".");
			}
			printf("|");
		}
		printf("\n");
	}
}

#define PCHAR(c) { if (retval < tmpsz) { *outbuf++ = (c); retval++; } }

int
mps_parse_flags(uintmax_t num, const char *q, char *outbuf, int tmpsz)
{
	int n, tmp, retval = 0;

	if (num == 0)
		return (retval);

	/* %b conversion flag format. */
	tmp = retval;
	while (*q) {
		n = *q++;
		if (num & (1 << (n - 1))) {
			PCHAR(retval != tmp ?  ',' : '<');
			for (; (n = *q) > ' '; ++q)
				PCHAR(n);
		} else
			for (; *q > ' '; ++q)
				continue;
	}
	if (retval != tmp)
		PCHAR('>');

	return (retval);
}

