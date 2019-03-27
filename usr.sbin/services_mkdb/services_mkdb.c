/*	$NetBSD: services_mkdb.c,v 1.14 2008/04/28 20:24:17 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn and Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/stat.h>

#include <assert.h>
#include <db.h>
#include <err.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <ctype.h>
#include <errno.h>
#include <stringlist.h>

#include "extern.h"

static char tname[MAXPATHLEN];

#define	PMASK		0xffff
#define PROTOMAX	5

static void	add(DB *, StringList *, size_t, const char *, size_t *, int);
static StringList ***parseservices(const char *, StringList *);
static void	cleanup(void);
static void	store(DB *, DBT *, DBT *, int);
static void	killproto(DBT *);
static char    *getstring(const char *, size_t, char **, const char *);
static size_t	getprotoindex(StringList *, const char *);
static const char *getprotostr(StringList *, size_t);
static const char *mkaliases(StringList *, char *, size_t);
static void	usage(void);

HASHINFO hinfo = {
	.bsize = 256,
	.ffactor = 4,
	.nelem = 32768,
	.cachesize = 1024,
	.hash = NULL,
	.lorder = 0
};


int
main(int argc, char *argv[])
{
	DB	*db;
	int	 ch;
	const char *fname = _PATH_SERVICES;
	const char *dbname = _PATH_SERVICES_DB;
	int	 warndup = 1;
	int	 unique = 0;
	int	 otherflag = 0;
	int	 byteorder = 0;
	size_t	 cnt = 0;
	StringList *sl, ***svc;
	size_t port, proto;
	char *dbname_dir, *dbname_dirbuf;
	int dbname_dir_fd = -1;

	setprogname(argv[0]);

	while ((ch = getopt(argc, argv, "blo:qu")) != -1)
		switch (ch) {
		case 'b':
		case 'l':
			if (byteorder != 0)
				usage();
			byteorder = ch == 'b' ? 4321 : 1234;
			break;
		case 'q':
			otherflag = 1;
			warndup = 0;
			break;
		case 'o':
			otherflag = 1;
			dbname = optarg;
			break;
		case 'u':
			unique++;
			break;
		case '?':
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc > 1 || (unique && otherflag))
		usage();
	if (argc == 1)
		fname = argv[0];

	/* Set byte order. */
	hinfo.lorder = byteorder;

	if (unique)
		uniq(fname);

	svc = parseservices(fname, sl = sl_init());

	if (atexit(cleanup))
		err(1, "Cannot install exit handler");

	(void)snprintf(tname, sizeof(tname), "%s.tmp", dbname);
	db = dbopen(tname, O_RDWR | O_CREAT | O_EXCL,
	    (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH), DB_HASH, &hinfo);
	if (!db)
		err(1, "Error opening temporary database `%s'", tname);


	for (port = 0; port < PMASK + 1; port++) {
		if (svc[port] == NULL)
			continue;

		for (proto = 0; proto < PROTOMAX; proto++) {
			StringList *s;
			if ((s = svc[port][proto]) == NULL)
				continue;
			add(db, s, port, getprotostr(sl, proto), &cnt, warndup);
		}

		free(svc[port]);
	}

	free(svc);
	sl_free(sl, 1);

	if ((db->close)(db))
		err(1, "Error closing temporary database `%s'", tname);

	/*
	 * Make sure file is safe on disk. To improve performance we will call
	 * fsync() to the directory where file lies
	 */
	if (rename(tname, dbname) == -1 ||
	    (dbname_dirbuf = strdup(dbname)) == NULL ||
	    (dbname_dir = dirname(dbname_dirbuf)) == NULL ||
	    (dbname_dir_fd = open(dbname_dir, O_RDONLY|O_DIRECTORY)) == -1 ||
	    fsync(dbname_dir_fd) != 0) {
		if (dbname_dir_fd != -1)
			close(dbname_dir_fd);
		err(1, "Cannot rename `%s' to `%s'", tname, dbname);
	}

	if (dbname_dir_fd != -1)
		close(dbname_dir_fd);

	return 0;
}

static void
add(DB *db, StringList *sl, size_t port, const char *proto, size_t *cnt,
    int warndup)
{
	size_t i;
	char	 keyb[BUFSIZ], datab[BUFSIZ], abuf[BUFSIZ];
	DBT	 data, key;
	key.data = keyb;
	data.data = datab;

#ifdef DEBUG
	(void)printf("add %s %zu %s [ ", sl->sl_str[0], port, proto);
	for (i = 1; i < sl->sl_cur; i++)
	    (void)printf("%s ", sl->sl_str[i]);
	(void)printf("]\n");
#endif

	/* key `indirect key', data `full line' */
	data.size = snprintf(datab, sizeof(datab), "%zu", (*cnt)++) + 1;
	key.size = snprintf(keyb, sizeof(keyb), "%s %zu/%s %s",
	    sl->sl_str[0], port, proto, mkaliases(sl, abuf, sizeof(abuf))) + 1;
	store(db, &data, &key, warndup);

	/* key `\377port/proto', data = `indirect key' */
	key.size = snprintf(keyb, sizeof(keyb), "\377%zu/%s",
	    port, proto) + 1;
	store(db, &key, &data, warndup);

	/* key `\377port', data = `indirect key' */
	killproto(&key);
	store(db, &key, &data, warndup);

	/* add references for service and all aliases */
	for (i = 0; i < sl->sl_cur; i++) {
		/* key `\376service/proto', data = `indirect key' */
		key.size = snprintf(keyb, sizeof(keyb), "\376%s/%s",
		    sl->sl_str[i], proto) + 1;
		store(db, &key, &data, warndup);

		/* key `\376service', data = `indirect key' */
		killproto(&key);
		store(db, &key, &data, warndup);
	}
	sl_free(sl, 1);
}

static StringList ***
parseservices(const char *fname, StringList *sl)
{
	ssize_t len;
	size_t linecap, line, pindex;
	FILE *fp;
	StringList ***svc, *s;
	char *p, *ep;

	if ((fp = fopen(fname, "r")) == NULL)
		err(1, "Cannot open `%s'", fname);

	line = linecap = 0;
	if ((svc = calloc(PMASK + 1, sizeof(StringList **))) == NULL)
		err(1, "Cannot allocate %zu bytes", (size_t)(PMASK + 1));

	p = NULL;
	while ((len = getline(&p, &linecap, fp)) != -1) {
		char	*name, *port, *proto, *aliases, *cp, *alias;
		unsigned long pnum;

		line++;

		if (len == 0)
			continue;

		if (p[len - 1] == '\n')
			p[len - 1] = '\0';

		for (cp = p; *cp && isspace((unsigned char)*cp); cp++)
			continue;

		if (*cp == '\0' || *cp == '#')
			continue;

		if ((name = getstring(fname, line, &cp, "name")) == NULL)
			continue;

		if ((port = getstring(fname, line, &cp, "port")) == NULL)
			continue;

		if (cp) {
			for (aliases = cp; *cp && *cp != '#'; cp++)
				continue;

			if (*cp)
				*cp = '\0';
		} else
			aliases = NULL;

		proto = strchr(port, '/');
		if (proto == NULL || proto[1] == '\0') {
			warnx("%s, %zu: no protocol found", fname, line);
			continue;
		}
		*proto++ = '\0';

		errno = 0;
		pnum = strtoul(port, &ep, 0);
		if (*port == '\0' || *ep != '\0') {
			warnx("%s, %zu: invalid port `%s'", fname, line, port);
			continue;
		}
		if ((errno == ERANGE && pnum == ULONG_MAX) || pnum > PMASK) {
			warnx("%s, %zu: port too big `%s'", fname, line, port);
			continue;
		}

		if (svc[pnum] == NULL) {
			svc[pnum] = calloc(PROTOMAX, sizeof(StringList *));
			if (svc[pnum] == NULL)
				err(1, "Cannot allocate %zu bytes",
				    (size_t)PROTOMAX);
		}

		pindex = getprotoindex(sl, proto);
		if (svc[pnum][pindex] == NULL)
			s = svc[pnum][pindex] = sl_init();
		else
			s = svc[pnum][pindex];

		/* build list of aliases */
		if (sl_find(s, name) == NULL) {
			char *p2;

			if ((p2 = strdup(name)) == NULL)
				err(1, "Cannot copy string");
			(void)sl_add(s, p2);
		}

		if (aliases) {
			while ((alias = strsep(&aliases, " \t")) != NULL) {
				if (alias[0] == '\0')
					continue;
				if (sl_find(s, alias) == NULL) {
					char *p2;

					if ((p2 = strdup(alias)) == NULL)
						err(1, "Cannot copy string");
					(void)sl_add(s, p2);
				}
			}
		}
	}
	(void)fclose(fp);
	return svc;
}

/*
 * cleanup(): Remove temporary files upon exit
 */
static void
cleanup(void)
{
	if (tname[0])
		(void)unlink(tname);
}

static char *
getstring(const char *fname, size_t line, char **cp, const char *tag)
{
	char *str;

	while ((str = strsep(cp, " \t")) != NULL && *str == '\0')
		continue;

	if (str == NULL)
		warnx("%s, %zu: no %s found", fname, line, tag);

	return str;
}

static void
killproto(DBT *key)
{
	char *p, *d = key->data;

	if ((p = strchr(d, '/')) == NULL)
		abort();
	*p++ = '\0';
	key->size = p - d;
}

static void
store(DB *db, DBT *key, DBT *data, int warndup)
{
#ifdef DEBUG
	int k = key->size - 1;
	int d = data->size - 1;
	(void)printf("store [%*.*s] [%*.*s]\n",
		k, k, (char *)key->data + 1,
		d, d, (char *)data->data + 1);
#endif
	switch ((db->put)(db, key, data, R_NOOVERWRITE)) {
	case 0:
		break;
	case 1:
		if (warndup)
			warnx("duplicate service `%s'",
			    &((char *)key->data)[1]);
		break;
	case -1:
		err(1, "put");
		break;
	default:
		abort();
		break;
	}
}

static size_t
getprotoindex(StringList *sl, const char *str)
{
	size_t i;
	char *p;

	for (i= 0; i < sl->sl_cur; i++)
		if (strcmp(sl->sl_str[i], str) == 0)
			return i;

	if (i == PROTOMAX)
		errx(1, "Ran out of protocols adding `%s';"
		    " recompile with larger PROTOMAX", str);
	if ((p = strdup(str)) == NULL)
		err(1, "Cannot copy string");
	(void)sl_add(sl, p);
	return i;
}

static const char *
getprotostr(StringList *sl, size_t i)
{
	assert(i < sl->sl_cur);
	return sl->sl_str[i];
}

static const char *
mkaliases(StringList *sl, char *buf, size_t len)
{
	size_t nc, i, pos;

	buf[0] = 0;
	for (i = 1, pos = 0; i < sl->sl_cur; i++) {
		nc = strlcpy(buf + pos, sl->sl_str[i], len);
		if (nc >= len)
			goto out;
		pos += nc;
		len -= nc;
		nc = strlcpy(buf + pos, " ", len);
		if (nc >= len)
			goto out;
		pos += nc;
		len -= nc;
	}
	return buf;
out:
	warn("aliases for `%s' truncated", sl->sl_str[0]);
	return buf;
}

static void
usage(void)
{
	(void)fprintf(stderr,
	    "Usage:\t%s [-b | -l] [-q] [-o <db>] [<servicefile>]\n"
	    "\t%s -u [<servicefile>]\n", getprogname(), getprogname());
	exit(1);
}
