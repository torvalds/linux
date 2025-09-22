/*	$OpenBSD: mkalias.c,v 1.29 2016/09/22 00:09:47 jsg Exp $ */

/*
 * Copyright (c) 1997 Mats O Jansson <moj@stacken.kth.se>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <err.h>
#include <time.h>
#include "ypdb.h"
#include "ypdef.h"

static void
split_address(char *address, size_t len, char *user, char *host)
{
	char *c, *s, *r;
	size_t  i = 0;

	if (memchr(address, '@', len)) {
		s = user;
		for (c = address; i < len; i++) {
			if (*c == '@') {
				*s = '\0';
				s = host;
			} else {
				*s++ = *c;
			}
			c++;
		}
		*s = '\0';
	}

	if ((r = memrchr(address, '!',  len))) {
		s = host;
		for (c = address; i < len; i++) {
			if (c == r) {
				*s = '\0';
				s = user;
			} else {
				*s++ = *c;
			}
			c++;
		}
		*s = '\0';
	}
}

static int
check_host(char *address, size_t len, char *host, int dflag, int uflag, int Eflag)
{
	union {
		HEADER hdr;
		u_char buf[PACKETSZ];
	} answer;
	int  status;

	if ((dflag && memchr(address, '@', len)) ||
	    (uflag && memchr(address, '!', len)))
		return(0);

	if ((_res.options & RES_INIT) == 0)
		res_init();

	status = res_search(host, C_IN, T_AAAA, answer.buf, sizeof(answer.buf));

	if (status == -1)
		status = res_search(host, C_IN, T_A, answer.buf, sizeof(answer.buf));

	if (status == -1 && Eflag)
		status = res_search(host, C_IN, T_MX, answer.buf, sizeof(answer.buf));

	return(status == -1);
}

static void
capitalize(char *name, int len)
{
	char last = ' ';
	char *c;
	int  i = 0;

	for (c = name; i < len; i++) {
		if (*c == '.')
			last = '.';
		c++;
	}

	i = 0;
	if (last == '.') {
		for (c = name; i < len; i++) {
			if (last == '.')
				*c = (char)toupper(*c);
			last = *c++;
		}
	}
}

static void
usage(void)
{
	fprintf(stderr,
	    "usage: mkalias [-nv] [-E | -e [-du]] input [output]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int	eflag = 0, dflag = 0, nflag = 0;
	int	uflag = 0, vflag = 0, Eflag = 0;
	int	status, ch, fd;
	char	*input = NULL, *output = NULL;
	DBM	*db;
	datum	key, val;
	DBM	*new_db = NULL;
	static	char mapname[] = "ypdbXXXXXXXXXX";
	char	db_mapname[PATH_MAX], db_outfile[PATH_MAX];
	char	db_tempname[PATH_MAX];
	char	user[4096], host[4096]; /* XXX: DB bsize = 4096 in ypdb.c */
	char	myname[HOST_NAME_MAX+1], datestr[11], *slash;

	while ((ch = getopt(argc, argv, "Edensuv")) != -1)
		switch (ch) {
		case 'E':
			eflag = 1;	/* Check hostname */
			Eflag = 1;	/* .. even check MX records */
			break;
		case 'd':
			dflag = 1;	/* Don't check DNS hostname */
			break;
		case 'e':
			eflag = 1;	/* Check hostname */
			break;
		case 'n':
			nflag = 1;	/* Capitalize name parts */
			break;
		case 's':		/* Ignore */ 
			break;
		case 'u':
			uflag = 1;	/* Don't check UUCP hostname */
			break;
		case 'v':
			vflag = 1;	/* Verbose */
			break;
		default:
			usage();
			break;
		}

	if (optind == argc)
		usage();

	input = argv[optind++];
	if (optind < argc)
		output = argv[optind++];
	if (optind < argc)
		usage();

	db = ypdb_open(input, O_RDONLY, 0444);
	if (db == NULL) {
		err(1, "Unable to open input database %s", input);
		/* NOTREACHED */
	}

	if (output != NULL) {
		if (strlen(output) + strlen(YPDB_SUFFIX) > PATH_MAX) {
			errx(1, "%s: file name too long", output);
			/* NOTREACHED */
		}

		snprintf(db_outfile, sizeof(db_outfile),
		    "%s%s", output, YPDB_SUFFIX);

		slash = strrchr(output, '/');
		if (slash != NULL)
			slash[1] = 0;			/* truncate to dir */
		else
			*output = 0;			/* eliminate */

		/* note: output is now directory where map goes ! */

		if (strlen(output) + strlen(mapname) +
		    strlen(YPDB_SUFFIX) > PATH_MAX) {
			errx(1, "%s: directory name too long", output);
			/* NOTREACHED */
		}

		snprintf(db_tempname, sizeof(db_tempname), "%s%s%s", output,
		    mapname, YPDB_SUFFIX);
		fd = mkstemps(db_tempname, 3);
		if (fd == -1)
			goto fail;
		close(fd);

		strncpy(db_mapname, db_tempname, strlen(db_tempname) - 3);
		db_mapname[sizeof(db_mapname) - 1] = '\0';

		new_db = ypdb_open(db_mapname, O_RDWR|O_TRUNC, 0444);
		if (new_db == NULL) {
fail:
			if (fd != -1)
				unlink(db_tempname);
			err(1, "Unable to open output database %s",
			    db_outfile);
			/* NOTREACHED */
		}
	}

	for (key = ypdb_firstkey(db); key.dptr != NULL;
	    key = ypdb_nextkey(db)) {
		val = ypdb_fetch(db, key);

		if (val.dptr == NULL)
			continue;			/* No value */
		if (*key.dptr == '@' && key.dsize == 1)
			continue;			/* Sendmail token */
		if (strncmp(key.dptr, "YP_", 3)==0)	/* YP token */
			continue;
		if (memchr(val.dptr, ',', val.dsize))
			continue;			/* List... */
		if (memchr(val.dptr, '|', val.dsize))
			continue;			/* Pipe... */

		if (!(memchr(val.dptr, '@', val.dsize) ||
		    memchr(val.dptr, '!', val.dsize)))
			continue;		/* Skip local users */

		split_address(val.dptr, val.dsize, user, host);

		if (eflag && check_host(val.dptr, val.dsize, host, dflag, uflag, Eflag)) {
			warnx("Invalid host %s in %*.*s:%*.*s",
			    host, key.dsize, key.dsize, key.dptr,
			    val.dsize, val.dsize, val.dptr);
			continue;
		}

		if (nflag)
			capitalize(key.dptr, key.dsize);

		if (new_db != NULL) {
			status = ypdb_store(new_db, val, key, YPDB_INSERT);
			if (status != 0) {
				warnx("problem storing %*.*s %*.*s",
				    val.dsize, val.dsize, val.dptr,
				    key.dsize, key.dsize, key.dptr);
			}
		}

		if (vflag) {
			printf("%*.*s --> %*.*s\n",
			    val.dsize, val.dsize, val.dptr,
			    key.dsize, key.dsize, key.dptr);
		}

	}

	if (new_db != NULL) {
		snprintf(datestr, sizeof datestr, "%010lld",
		    (long long)time(NULL));
		key.dptr = YP_LAST_KEY;
		key.dsize = strlen(YP_LAST_KEY);
		val.dptr = datestr;
		val.dsize = strlen(datestr);
		status = ypdb_store(new_db, key, val, YPDB_INSERT);
		if (status != 0) {
			warnx("problem storing %*.*s %*.*s",
			    key.dsize, key.dsize, key.dptr,
			    val.dsize, val.dsize, val.dptr);
		}
	}

	if (new_db != NULL) {
		gethostname(myname, sizeof(myname));
		key.dptr = YP_MASTER_KEY;
		key.dsize = strlen(YP_MASTER_KEY);
		val.dptr = myname;
		val.dsize = strlen(myname);
		status = ypdb_store(new_db, key, val, YPDB_INSERT);
		if (status != 0) {
			warnx("problem storing %*.*s %*.*s",
			    key.dsize, key.dsize, key.dptr,
			    val.dsize, val.dsize, val.dptr);
		}
	}

	ypdb_close(db);

	if (new_db != NULL) {
		ypdb_close(new_db);
		if (rename(db_tempname, db_outfile) < 0) {
			err(1, "rename %s -> %s failed", db_tempname,
			    db_outfile);
			/* NOTREACHED */
		}
	}
	return(0);
}
