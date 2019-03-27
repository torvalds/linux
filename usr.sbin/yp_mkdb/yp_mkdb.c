/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1995, 1996
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR CONTRIBUTORS BE LIABLE
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

#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <rpcsvc/yp.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "yp_extern.h"
#include "ypxfr_extern.h"

char *yp_dir = "";	/* No particular default needed. */
int debug = 1;

static void
usage(void)
{
	fprintf(stderr, "%s\n%s\n%s\n%s\n",
	"usage: yp_mkdb -c",
	"       yp_mkdb -u dbname",
	"       yp_mkdb [-c] [-b] [-s] [-f] [-i inputfile] [-o outputfile]",
	"               [-d domainname ] [-m mastername] inputfile dbname");
	exit(1);
}

#define PERM_SECURE (S_IRUSR|S_IWUSR)
static DB *
open_db(char *path, int flags)
{
	extern HASHINFO openinfo;

	return(dbopen(path, flags, PERM_SECURE, DB_HASH, &openinfo));
}

static void
unwind(char *map)
{
	DB *dbp;
	DBT key, data;

	dbp = open_db(map, O_RDONLY);

	if (dbp == NULL)
		err(1, "open_db(%s) failed", map);

	key.data = NULL;
	while (yp_next_record(dbp, &key, &data, 1, 1) == YP_TRUE)
		printf("%.*s %.*s\n", (int)key.size, key.data, (int)data.size,
		    data.data);

	(void)(dbp->close)(dbp);
	return;
}

int
main(int argc, char *argv[])
{
	int ch;
	int un = 0;
	int clear = 0;
	int filter_plusminus = 0;
	char *infile = NULL;
	char *map = NULL;
	char *domain = NULL;
	char *infilename = NULL;
	char *outfilename = NULL;
	char *mastername = NULL;
	int interdom = 0;
	int secure = 0;
	DB *dbp;
	DBT key, data;
	char buf[10240];
	char *keybuf, *datbuf;
	FILE *ifp;
	char hname[MAXHOSTNAMELEN + 2];

	while ((ch = getopt(argc, argv, "uhcbsfd:i:o:m:")) != -1) {
		switch (ch) {
		case 'f':
			filter_plusminus++;
			break;
		case 'u':
			un++;
			break;
		case 'c':
			clear++;
			break;
		case 'b':
			interdom++;
			break;
		case 's':
			secure++;
			break;
		case 'd':
			domain = optarg;
			break;
		case 'i':
			infilename = optarg;
			break;
		case 'o':
			outfilename = optarg;
			break;
		case 'm':
			mastername = optarg;
			break;
		case 'h':
		default:
			usage();
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (un) {
		map = argv[0];
		if (map == NULL)
			usage();
		unwind(map);
		exit(0);

	}

	infile = argv[0];
	map = argv[1];

	if (infile == NULL || map == NULL) {
		if (clear)
			goto doclear;
		usage();
	}

	if (mastername == NULL) {
		if (gethostname((char *)&hname, sizeof(hname)) == -1)
			err(1, "gethostname() failed");
		mastername = (char *)&hname;
	}

	/*
	 * Note that while we can read from stdin, we can't
	 * write to stdout; the db library doesn't let you
	 * write to a file stream like that.
	 */
	if (!strcmp(infile, "-")) {
		ifp = stdin;
	} else {
		if ((ifp = fopen(infile, "r")) == NULL)
			err(1, "failed to open %s", infile);
	}

	if ((dbp = open_db(map, O_RDWR|O_EXLOCK|O_EXCL|O_CREAT)) == NULL)
		err(1, "open_db(%s) failed", map);

	if (interdom) {
		key.data = "YP_INTERDOMAIN";
		key.size = sizeof("YP_INTERDOMAIN") - 1;
		data.data = "";
		data.size = 0;
		yp_put_record(dbp, &key, &data, 0);
	}

	if (secure) {
		key.data = "YP_SECURE";
		key.size = sizeof("YP_SECURE") - 1;
		data.data = "";
		data.size = 0;
		yp_put_record(dbp, &key, &data, 0);
	}

	key.data = "YP_MASTER_NAME";
	key.size = sizeof("YP_MASTER_NAME") - 1;
	data.data = mastername;
	data.size = strlen(mastername);
	yp_put_record(dbp, &key, &data, 0);

	key.data = "YP_LAST_MODIFIED";
	key.size = sizeof("YP_LAST_MODIFIED") - 1;
	snprintf(buf, sizeof(buf), "%jd", (intmax_t)time(NULL));
	data.data = (char *)&buf;
	data.size = strlen(buf);
	yp_put_record(dbp, &key, &data, 0);

	if (infilename) {
		key.data = "YP_INPUT_FILE";
		key.size = sizeof("YP_INPUT_FILE") - 1;
		data.data = infilename;
		data.size = strlen(infilename);
		yp_put_record(dbp, &key, &data, 0);
	}

	if (outfilename) {
		key.data = "YP_OUTPUT_FILE";
		key.size = sizeof("YP_OUTPUT_FILE") - 1;
		data.data = outfilename;
		data.size = strlen(outfilename);
		yp_put_record(dbp, &key, &data, 0);
	}

	if (domain) {
		key.data = "YP_DOMAIN_NAME";
		key.size = sizeof("YP_DOMAIN_NAME") - 1;
		data.data = domain;
		data.size = strlen(domain);
		yp_put_record(dbp, &key, &data, 0);
	}

	while (fgets((char *)&buf, sizeof(buf), ifp)) {
		char *sep = NULL;
		int rval;

		/* NUL terminate */
		if ((sep = strchr(buf, '\n')))
			*sep = '\0';

		/* handle backslash line continuations */
		while (buf[strlen(buf) - 1] == '\\') {
			fgets((char *)&buf[strlen(buf) - 1],
					sizeof(buf) - strlen(buf), ifp);
			if ((sep = strchr(buf, '\n')))
				*sep = '\0';
		}

		/* find the separation between the key and data */
		if ((sep = strpbrk(buf, " \t")) == NULL) {
			warnx("bad input -- no white space: %s", buf);
			continue;
		}

		/* separate the strings */
		keybuf = (char *)&buf;
		datbuf = sep + 1;
		*sep = '\0';

		/* set datbuf to start at first non-whitespace character */
		while (*datbuf == ' ' || *datbuf == '\t')
			datbuf++;

		/* Check for silliness. */
		if (filter_plusminus) {
			if  (*keybuf == '+' || *keybuf == '-' ||
			     *datbuf == '+' || *datbuf == '-') {
				warnx("bad character at "
				    "start of line: %s", buf);
				continue;
			}
		}

		if (strlen(keybuf) > YPMAXRECORD) {
			warnx("key too long: %s", keybuf);
			continue;
		}

		if (!strlen(keybuf)) {
			warnx("no key -- check source file for blank lines");
			continue;
		}

		if (strlen(datbuf) > YPMAXRECORD) {
			warnx("data too long: %s", datbuf);
			continue;
		}

		key.data = keybuf;
		key.size = strlen(keybuf);
		data.data = datbuf;
		data.size = strlen(datbuf);

		if ((rval = yp_put_record(dbp, &key, &data, 0)) != YP_TRUE) {
			switch (rval) {
			case YP_FALSE:
				warnx("duplicate key '%s' - skipping", keybuf);
				break;
			case YP_BADDB:
			default:
				err(1,"failed to write new record - exiting");
				break;
			}
		}

	}

	(void)(dbp->close)(dbp);

doclear:
	if (clear) {
		char in = 0;
		char *out = NULL;
		int stat;
		if ((stat = callrpc("localhost", YPPROG,YPVERS, YPPROC_CLEAR,
			(xdrproc_t)xdr_void, &in,
			(xdrproc_t)xdr_void, out)) != RPC_SUCCESS) {
			warnx("failed to send 'clear' to local ypserv: %s",
				clnt_sperrno((enum clnt_stat) stat));
		}
	}

	exit(0);
}
