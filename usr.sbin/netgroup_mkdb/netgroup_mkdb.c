/*	$OpenBSD: netgroup_mkdb.c,v 1.24 2023/01/04 13:00:11 jsg Exp $	*/

/*
 * Copyright (c) 1994 Christos Zoulas
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

#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <db.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netgroup.h>
#include <assert.h>

#include "str.h"
#include "stringlist.h"
#include "util.h"

#define DEBUG_NG

struct nentry {
	int             n_type;
	size_t          n_size;	/* Buffer size required for printing */
	union {
		char            *_name;
		struct netgroup *_group;
	} _n;
#define n_name	_n._name
#define n_group _n._group
	struct nentry  *n_next;
};


static DB       *ng_insert(DB *, const char *);
static void	 ng_reventry(DB *, DB *, struct nentry *, char *,
		    size_t, struct stringlist *);

static void	 ng_print(struct nentry *, struct string *);
static void	 ng_rprint(DB *, struct string *);
static DB	*ng_reverse(DB *, size_t);
static DB	*ng_load(const char *);
static void	 ng_write(DB *, DB *, int);
static void	 ng_rwrite(DB *, DB *, int);
static void	 usage(void);
static void	 cleanup(void);

#ifdef DEBUG_NG
static int 	 debug = 0;
static void	 ng_dump(DB *);
static void	 ng_rdump(DB *);
#endif /* DEBUG_NG */


static const char ng_empty[] = "";
#define NG_EMPTY(a)	((a) ? (a) : ng_empty)

static char    *dbname = _PATH_NETGROUP_DB;

int
main(int argc, char *argv[])
{
	char		  buf[PATH_MAX], *fname = _PATH_NETGROUP;
	DB		 *db, *ndb, *hdb, *udb;
	int               ch;

	if (pledge("stdio rpath wpath cpath", NULL) == -1)
		err(1, "pledge");

	while ((ch = getopt(argc, argv, "do:")) != -1)
		switch (ch) {
#ifdef DEBUG_NG
		case 'd':
			debug++;
			break;
#endif
		case 'o':
			dbname = optarg;
			break;

		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc == 1)
		fname = *argv;
	else if (argc > 1)
		usage();

	if (atexit(cleanup) != 0)
		err(1, "Cannot install exit handler");

	/* Read and parse the netgroup file */
	ndb = ng_load(fname);
#ifdef DEBUG_NG
	if (debug) {
		(void) fprintf(stderr, "#### Database\n");
		ng_dump(ndb);
	}
#endif

	/* Reverse the database by host */
	hdb = ng_reverse(ndb, offsetof(struct netgroup, ng_host));
#ifdef DEBUG_NG
	if (debug) {
		(void) fprintf(stderr, "#### Reverse by host\n");
		ng_rdump(hdb);
	}
#endif

	/* Reverse the database by user */
	udb = ng_reverse(ndb, offsetof(struct netgroup, ng_user));
#ifdef DEBUG_NG
	if (debug) {
		(void) fprintf(stderr, "#### Reverse by user\n");
		ng_rdump(udb);
	}
#endif

	(void) snprintf(buf, sizeof(buf), "%s.tmp", dbname);

	db = dbopen(buf, O_RDWR | O_CREAT | O_EXCL,
	    (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH), DB_HASH, NULL);
	if (!db)
		err(1, "%s", buf);

	ng_write(db, ndb, _NG_KEYBYNAME);
	ng_rwrite(db, udb, _NG_KEYBYUSER);
	ng_rwrite(db, hdb, _NG_KEYBYHOST);

	if ((db->close)(db))
		err(1, "Error closing database");

	if (rename(buf, dbname) == -1)
		err(1, "Cannot rename `%s' to `%s'", buf, dbname);

	return 0;
}


/*
 * cleanup(): Remove temporary files upon exit
 */
static void
cleanup(void)
{
	char buf[PATH_MAX];

	(void) snprintf(buf, sizeof(buf), "%s.tmp", dbname);
	(void) unlink(buf);
}



/*
 * ng_load(): Load the netgroup database from a file
 */
static DB *
ng_load(const char *fname)
{
	FILE           *fp;
	DB             *db;
	char           *buf, *p, *name;
	size_t          size;
	struct nentry  *tail, *head, *e;
	struct netgroup *ng;
	DBT             data, key;

	/* Open the netgroup file */
	if ((fp = fopen(fname, "r")) == NULL)
		err(1, "%s", fname);

	db = dbopen(NULL, O_RDWR | O_CREAT | O_EXCL, 0, DB_HASH, NULL);

	if (db == NULL)
		err(1, "dbopen");

	while ((buf = get_line(fp, &size)) != NULL) {
		tail = head = NULL;
		p = buf;

		while (p != NULL) {
			switch (_ng_parse(&p, &name, &ng)) {
			case _NG_NONE:
				/* done with this one */
				p = NULL;
				free(buf);
				if (head == NULL)
					break;

				key.data = (u_char *) head->n_name;
				key.size = strlen(head->n_name) + 1;
				data.data = (u_char *) & head;
				data.size = sizeof(head);
				switch ((db->put)(db, &key, &data, 
						  R_NOOVERWRITE)) {
				case 0:
					break;

				case 1:
					warnx("Duplicate entry netgroup `%s'",
					    head->n_name);
					break;

				case -1:
					err(1, "put");
					break;

				default:
					abort();
					break;
				}
				break;

			case _NG_NAME:
				e = emalloc(sizeof(struct nentry));
				e->n_type = _NG_NAME;
				e->n_name = name;
				e->n_next = NULL;
				e->n_size = size;
				if (tail == NULL)
					head = tail = e;
				else {
					tail->n_next = e;
					tail = e;
				}
				break;

			case _NG_GROUP:
				if (tail == NULL) {
					char fmt[BUFSIZ];
					_ng_print(fmt, sizeof(fmt), ng);
					errx(1, "no netgroup key for %s", fmt);
				} else {
					e = emalloc(sizeof(struct nentry));
					e->n_type = _NG_GROUP;
					e->n_group = ng;
					e->n_next = NULL;
					e->n_size = size;
					tail->n_next = e;
					tail = e;
				}
				break;

			default:
				abort();
				break;
			}
		}
	}
	(void) fclose(fp);
	return db;
}


/*
 * ng_insert(): Insert named key into the database, and return its associated
 * string database
 */
static DB *
ng_insert(DB *db, const char *name)
{
	DB             *xdb = NULL;
	DBT             key, data;

	key.data = (u_char *) name;
	key.size = strlen(name) + 1;

	switch ((db->get)(db, &key, &data, 0)) {
	case 0:
		memcpy(&xdb, data.data, sizeof(xdb));
		break;

	case 1:
		xdb = dbopen(NULL, O_RDWR | O_CREAT | O_EXCL, 0, DB_HASH, NULL);
		if (xdb == NULL)
			err(1, "dbopen");

		data.data = (u_char *) & xdb;
		data.size = sizeof(xdb);
		switch ((db->put)(db, &key, &data, R_NOOVERWRITE)) {
		case 0:
			break;

		case -1:
			err(1, "db put `%s'", name);
			break;

		case 1:
		default:
			abort();
		}
		break;

	case -1:
		err(1, "db get `%s'", name);
		break;

	default:
		abort();
		break;
	}

	return xdb;
}


/*
 * ng_reventry(): Recursively add all the netgroups to the group entry.
 */
static void
ng_reventry(DB *db, DB *udb, struct nentry *fe, char *name, size_t s,
    struct stringlist *ss)
{
	DBT             key, data;
	struct nentry  *e;
	struct netgroup *ng;
	struct nentry *rfe;
	char           *p;
	DB             *xdb;

	if (_ng_sl_find(ss, fe->n_name) != NULL) {
		warnx("Cycle in netgroup `%s'", name);
		return;
	}
	if (_ng_sl_add(ss, fe->n_name) == -1) {
		warn(NULL);
		return;
	}

	for (e = fe->n_next; e != NULL; e = e->n_next)
		switch (e->n_type) {
		case _NG_GROUP:
			ng = e->n_group;
			p = _ng_makekey(*((char **)(((char *) ng) + s)),
			    ng->ng_domain, e->n_size);
			xdb = ng_insert(udb, p);
			key.data = (u_char *) name;
			key.size = strlen(name) + 1;
			data.data = NULL;
			data.size = 0;
			switch ((xdb->put)(xdb, &key, &data, R_NOOVERWRITE)) {
			case 0:
			case 1:
				break;

			case -1:
				err(1, "db put `%s'", name);
				return;

			default:
				abort();
				break;
			}
			free(p);
			break;

		case _NG_NAME:
			key.data = (u_char *) e->n_name;
			key.size = strlen(e->n_name) + 1;
			switch ((db->get)(db, &key, &data, 0)) {
			case 0:
				(void) memcpy(&rfe, data.data, sizeof(rfe));
				ng_reventry(db, udb, rfe, name, s, ss);
				break;

			case 1:
				break;

			case -1:
				err(1, "db get `%s'", e->n_name);
				return;

			default:
				abort();
				return;
			}
			break;

		default:
			abort();
			break;
		}
}


/*
 * ng_reverse(): Reverse the database
 */
static DB *
ng_reverse(DB *db, size_t s)
{
	int             pos;
	struct stringlist *sl;
	DBT             key, data;
	struct nentry  *fe;
	DB             *udb;

	udb = dbopen(NULL, O_RDWR | O_CREAT | O_EXCL, 0, DB_HASH, NULL);

	if (udb == NULL)
		err(1, "dbopen");

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			sl = _ng_sl_init();
			memcpy(&fe, data.data, sizeof(fe));
			ng_reventry(db, udb, fe, (char *) key.data, s, sl);
			_ng_sl_free(sl, 0);
			break;

		case 1:
			return udb;

		case -1:
			err(1, "seq");
			return udb;
		}

	return udb;
}


/*
 * ng_print(): Pretty print a netgroup entry
 */
static void
ng_print(struct nentry *e, struct string *str)
{
	char           *ptr = emalloc(e->n_size);

	if (e->n_next == NULL) {
		str_append(str, "", ' ');
		return;
	}

	for (e = e->n_next; e != NULL; e = e->n_next) {
		switch (e->n_type) {
		case _NG_NAME:
			(void) snprintf(ptr, e->n_size, "%s", e->n_name);
			break;

		case _NG_GROUP:
			(void) snprintf(ptr, e->n_size, "(%s,%s,%s)",
			    NG_EMPTY(e->n_group->ng_host),
			    NG_EMPTY(e->n_group->ng_user),
			    NG_EMPTY(e->n_group->ng_domain));
			break;

		default:
			errx(1, "Internal error: Bad netgroup type");
			break;
		}
		str_append(str, ptr, ' ');
	}
	free(ptr);
}


/*
 * ng_rprint(): Pretty print all reverse netgroup mappings in the given entry
 */
static void
ng_rprint(DB *db, struct string *str)
{
	int             pos;
	DBT             key, data;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			str_append(str, (char *) key.data, ',');
			break;

		case 1:
			return;

		default:
			err(1, "seq");
			break;
		}
}


#ifdef DEBUG_NG
/*
 * ng_dump(): Pretty print all netgroups in the given database
 */
static void
ng_dump(DB *db)
{
	int             pos;
	DBT             key, data;
	struct nentry  *e;
	struct string   buf;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			memcpy(&e, data.data, sizeof(e));
			str_init(&buf);
			assert(e->n_type == _NG_NAME);

			ng_print(e, &buf);
			(void) fprintf(stderr, "%s\t%s\n", e->n_name,
			    buf.s_str ? buf.s_str : "");
			str_free(&buf);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
			return;
		}
}


/*
 * ng_rdump(): Pretty print all reverse mappings in the given database
 */
static void
ng_rdump(DB *db)
{
	int             pos;
	DBT             key, data;
	DB             *xdb;
	struct string   buf;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((db->seq)(db, &key, &data, pos)) {
		case 0:
			memcpy(&xdb, data.data, sizeof(xdb));
			str_init(&buf);
			ng_rprint(xdb, &buf);
			(void) fprintf(stderr, "%s\t%s\n",
			    (char *) key.data, buf.s_str ? buf.s_str : "");
			str_free(&buf);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
			return;
		}
}
#endif /* DEBUG_NG */


/*
 * ng_write(): Dump the database into a file.
 */
static void
ng_write(DB *odb, DB *idb, int k)
{
	int             pos;
	DBT             key, data;
	struct nentry  *e;
	struct string   skey, sdata;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((idb->seq)(idb, &key, &data, pos)) {
		case 0:
			memcpy(&e, data.data, sizeof(e));
			str_init(&skey);
			str_init(&sdata);
			assert(e->n_type == _NG_NAME);

			str_prepend(&skey, e->n_name, k);
			ng_print(e, &sdata);
			key.data = (u_char *) skey.s_str;
			key.size = skey.s_len + 1;
			data.data = (u_char *) sdata.s_str;
			data.size = sdata.s_len + 1;

			switch ((odb->put)(odb, &key, &data, R_NOOVERWRITE)) {
			case 0:
				break;

			case -1:
				err(1, "put");
				break;

			case 1:
			default:
				abort();
				break;
			}

			str_free(&skey);
			str_free(&sdata);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
			return;
		}
}


/*
 * ng_rwrite(): Write the database
 */
static void
ng_rwrite(DB *odb, DB *idb, int k)
{
	int             pos;
	DBT             key, data;
	DB             *xdb;
	struct string   skey, sdata;

	for (pos = R_FIRST;; pos = R_NEXT)
		switch ((idb->seq)(idb, &key, &data, pos)) {
		case 0:
			memcpy(&xdb, data.data, sizeof(xdb));
			str_init(&skey);
			str_init(&sdata);

			str_prepend(&skey, (char *) key.data, k);
			ng_rprint(xdb, &sdata);
			key.data = (u_char *) skey.s_str;
			key.size = skey.s_len + 1;
			data.data = (u_char *) sdata.s_str;
			data.size = sdata.s_len + 1;

			switch ((odb->put)(odb, &key, &data, R_NOOVERWRITE)) {
			case 0:
				break;

			case -1:
				err(1, "put");
				break;

			case 1:
			default:
				abort();
				break;
			}

			str_free(&skey);
			str_free(&sdata);
			break;

		case 1:
			return;

		default:
			err(1, "seq");
			return;
		}
}


/*
 * usage(): Print usage message and exit
 */
static void
usage(void)
{
	extern const char *__progname;

	fprintf(stderr, "usage: %s [-o database] file\n", __progname);
	exit(1);
}
