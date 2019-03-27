/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#ifndef lint
static const char copyright[] =
"@(#) Copyright (c) 1992, 1993\n\
	The Regents of the University of California.  All rights reserved.\n";
#endif

#if 0
#ifndef lint
static char sccsid[] = "@(#)cap_mkdb.c	8.2 (Berkeley) 4/27/95";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>

#include <db.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void	 db_build(char **);
static void	 dounlink(void);
static void	 usage(void);

static DB	*capdbp;
static int	 verbose;
static char	*capname, buf[8 * 1024];

static HASHINFO openinfo = {
	4096,		/* bsize */
	0,		/* ffactor */
	0,		/* nelem */
	0,		/* cachesize */
	NULL,		/* hash() */
	0		/* lorder */
};

/*
 * Mkcapdb creates a capability hash database for quick retrieval of capability
 * records.  The database contains 2 types of entries: records and references
 * marked by the first byte in the data.  A record entry contains the actual
 * capability record whereas a reference contains the name (key) under which
 * the correct record is stored.
 */
int
main(int argc, char *argv[])
{
	int byteorder, c;

	capname = NULL;
	byteorder = 0;
	while ((c = getopt(argc, argv, "bf:lv")) != -1) {
		switch(c) {
		case 'b':
		case 'l':
			if (byteorder != 0)
				usage();
			byteorder = c == 'b' ? 4321 : 1234;
			break;
		case 'f':
			capname = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (*argv == NULL)
		usage();

	/* Set byte order. */
	openinfo.lorder = byteorder;

	/*
	 * The database file is the first argument if no name is specified.
	 * Make arrangements to unlink it if exit badly.
	 */
	(void)snprintf(buf, sizeof(buf), "%s.db", capname ? capname : *argv);
	if ((capname = strdup(buf)) == NULL)
		errx(1, "strdup failed");
	if ((capdbp = dbopen(capname, O_CREAT | O_TRUNC | O_RDWR,
	    DEFFILEMODE, DB_HASH, &openinfo)) == NULL)
		err(1, "%s", buf);

	if (atexit(dounlink))
		err(1, "atexit");

	db_build(argv);

	if (capdbp->close(capdbp) < 0)
		err(1, "%s", capname);
	capname = NULL;
	exit(0);
}

static void
dounlink(void)
{
	if (capname != NULL)
		(void)unlink(capname);
}

/*
 * Any changes to these definitions should be made also in the getcap(3)
 * library routines.
 */
#define RECOK	(char)0
#define TCERR	(char)1
#define SHADOW	(char)2

/*
 * Db_build() builds the name and capability databases according to the
 * details above.
 */
static void
db_build(char **ifiles)
{
	DBT key, data;
	recno_t reccnt;
	size_t len, bplen;
	int st;
	char *bp, *p, *t;

	data.data = NULL;
	key.data = NULL;
	for (reccnt = 0, bplen = 0; (st = cgetnext(&bp, ifiles)) > 0;) {

		/*
		 * Allocate enough memory to store record, terminating
		 * NULL and one extra byte.
		 */
		len = strlen(bp);
		if (bplen <= len + 2) {
			bplen += MAX(256, len + 2);
			if ((data.data = realloc(data.data, bplen)) == NULL)
				errx(1, "malloc failed");
		}

		/* Find the end of the name field. */
		if ((p = strchr(bp, ':')) == NULL) {
			warnx("no name field: %.*s", (int)MIN(len, 20), bp);
			continue;
		}

		/* First byte of stored record indicates status. */
		switch(st) {
		case 1:
			((char *)(data.data))[0] = RECOK;
			break;
		case 2:
			((char *)(data.data))[0] = TCERR;
			warnx("record not tc expanded: %.*s", (int)(p - bp),
			    bp);
			break;
		}

		/* Create the stored record. */
		memmove(&((u_char *)(data.data))[1], bp, len + 1);
		data.size = len + 2;

		/* Store the record under the name field. */
		key.data = bp;
		key.size = p - bp;

		switch(capdbp->put(capdbp, &key, &data, R_NOOVERWRITE)) {
		case -1:
			err(1, "put");
			/* NOTREACHED */
		case 1:
			warnx("ignored duplicate: %.*s",
			    (int)key.size, (char *)key.data);
			continue;
		}
		++reccnt;

		/* If only one name, ignore the rest. */
		*p = '\0';
		if (strchr(bp, '|') == NULL)
			continue;
		*p = ':';

		/* The rest of the names reference the entire name. */
		((char *)(data.data))[0] = SHADOW;
		memmove(&((u_char *)(data.data))[1], key.data, key.size);
		data.size = key.size + 1;

		/* Store references for other names. */
		for (p = t = bp;; ++p) {
			if (p > t && (*p == ':' || *p == '|')) {
				key.size = p - t;
				key.data = t;
				switch(capdbp->put(capdbp,
				    &key, &data, R_NOOVERWRITE)) {
				case -1:
					err(1, "put");
					/* NOTREACHED */
				case 1:
					warnx("ignored duplicate: %.*s",
					    (int)key.size, (char *)key.data);
				}
				t = p + 1;
			}
			if (*p == ':')
				break;
		}
	}

	switch(st) {
	case -1:
		err(1, "file argument");
		/* NOTREACHED */
	case -2:
		errx(1, "potential reference loop detected");
		/* NOTREACHED */
	}

	if (verbose)
		(void)printf("cap_mkdb: %d capability records\n", reccnt);
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "usage: cap_mkdb [-b | -l] [-v] [-f outfile] file ...\n");
	exit(1);
}
