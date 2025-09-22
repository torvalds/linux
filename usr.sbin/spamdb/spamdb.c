/*	$OpenBSD: spamdb.c,v 1.36 2018/07/26 19:33:20 mestre Exp $	*/

/*
 * Copyright (c) 2004 Bob Beck.  All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <db.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>

#include "grey.h"

/* things we may add/delete from the db */
#define WHITE 0
#define TRAPHIT 1
#define SPAMTRAP 2
#define GREY 3

int	dblist(DB *);
int	dbupdate(DB *, char *, int, int);

int
dbupdate(DB *db, char *ip, int add, int type)
{
	DBT		dbk, dbd;
	struct gdata	gd;
	time_t		now;
	int		r;
	struct addrinfo hints, *res;

	now = time(NULL);
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;	/*dummy*/
	hints.ai_flags = AI_NUMERICHOST;
	if (add && (type == TRAPHIT || type == WHITE)) {
		if (getaddrinfo(ip, NULL, &hints, &res) != 0) {
			warnx("invalid ip address %s", ip);
			goto bad;
		}
		freeaddrinfo(res);
	}
	memset(&dbk, 0, sizeof(dbk));
	dbk.size = strlen(ip);
	dbk.data = ip;
	memset(&dbd, 0, sizeof(dbd));
	if (!add) {
		/* remove entry */
		if (type == GREY) {
			for (r = db->seq(db, &dbk, &dbd, R_FIRST); !r;
			    r = db->seq(db, &dbk, &dbd, R_NEXT)) {
				char *cp = memchr(dbk.data, '\n', dbk.size);
				if (cp != NULL) {
					size_t len = cp - (char *)dbk.data;
					if (memcmp(ip, dbk.data, len) == 0 &&
					    ip[len] == '\0')
						break;
				}
			}
		} else {
			r = db->get(db, &dbk, &dbd, 0);
			if (r == -1) {
				warn("db->get failed");
				goto bad;
			}
		}
		if (r) {
			warnx("no entry for %s", ip);
			goto bad;
		} else if (db->del(db, &dbk, 0)) {
			warn("db->del failed");
			goto bad;
		}
	} else {
		/* add or update entry */
		r = db->get(db, &dbk, &dbd, 0);
		if (r == -1) {
			warn("db->get failed");
			goto bad;
		}
		if (r) {
			int i;

			/* new entry */
			memset(&gd, 0, sizeof(gd));
			gd.first = now;
			gd.bcount = 1;
			switch (type) {
			case WHITE:
				gd.pass = now;
				gd.expire = now + WHITEEXP;
				break;
			case TRAPHIT:
				gd.expire = now + TRAPEXP;
				gd.pcount = -1;
				break;
			case SPAMTRAP:
				gd.expire = 0;
				gd.pcount = -2;
				/* ensure address is of the form user@host */
				if (strchr(ip, '@') == NULL)
					errx(-1, "not an email address: %s", ip);
				/* ensure address is lower case*/
				for (i = 0; ip[i] != '\0'; i++)
					if (isupper((unsigned char)ip[i]))
						ip[i] = tolower((unsigned char)ip[i]);
				break;
			default:
				errx(-1, "unknown type %d", type);
			}
			memset(&dbk, 0, sizeof(dbk));
			dbk.size = strlen(ip);
			dbk.data = ip;
			memset(&dbd, 0, sizeof(dbd));
			dbd.size = sizeof(gd);
			dbd.data = &gd;
			r = db->put(db, &dbk, &dbd, 0);
			if (r) {
				warn("db->put failed");
				goto bad;
			}
		} else {
			if (gdcopyin(&dbd, &gd) == -1) {
				/* whatever this is, it doesn't belong */
				db->del(db, &dbk, 0);
				goto bad;
			}
			gd.pcount++;
			switch (type) {
			case WHITE:
				gd.pass = now;
				gd.expire = now + WHITEEXP;
				break;
			case TRAPHIT:
				gd.expire = now + TRAPEXP;
				gd.pcount = -1;
				break;
			case SPAMTRAP:
				gd.expire = 0; /* XXX */
				gd.pcount = -2;
				break;
			default:
				errx(-1, "unknown type %d", type);
			}

			memset(&dbk, 0, sizeof(dbk));
			dbk.size = strlen(ip);
			dbk.data = ip;
			memset(&dbd, 0, sizeof(dbd));
			dbd.size = sizeof(gd);
			dbd.data = &gd;
			r = db->put(db, &dbk, &dbd, 0);
			if (r) {
				warn("db->put failed");
				goto bad;
			}
		}
	}
	return (0);
 bad:
	return (1);
}

int
print_entry(DBT *dbk, DBT *dbd)
{
	struct gdata gd;
	char *a, *cp;

	if ((dbk->size < 1) || gdcopyin(dbd, &gd) == -1) {
		warnx("bogus size db entry - bad db file?");
		return (1);
	}
	a = malloc(dbk->size + 1);
	if (a == NULL)
		err(1, "malloc");
	memcpy(a, dbk->data, dbk->size);
	a[dbk->size]='\0';
	cp = strchr(a, '\n');
	if (cp == NULL) {
		/* this is a non-greylist entry */
		switch (gd.pcount) {
		case -1: /* spamtrap hit, with expiry time */
			printf("TRAPPED|%s|%lld\n", a,
			    (long long)gd.expire);
			break;
		case -2: /* spamtrap address */
			printf("SPAMTRAP|%s\n", a);
			break;
		default: /* whitelist */
			printf("WHITE|%s|||%lld|%lld|%lld|%d|%d\n", a,
			    (long long)gd.first, (long long)gd.pass,
			    (long long)gd.expire, gd.bcount,
			    gd.pcount);
			break;
		}
	} else {
		char *helo, *from, *to;

		/* greylist entry */
		*cp = '\0';
		helo = cp + 1;
		from = strchr(helo, '\n');
		if (from == NULL) {
			warnx("No from part in grey key %s", a);
			free(a);
			return (1);
		}
		*from = '\0';
		from++;
		to = strchr(from, '\n');
		if (to == NULL) {
			/* probably old format - print it the
			 * with an empty HELO field instead 
			 * of erroring out.
			 */			  
			printf("GREY|%s|%s|%s|%s|%lld|%lld|%lld|%d|%d\n",
			    a, "", helo, from, (long long)gd.first,
			    (long long)gd.pass, (long long)gd.expire,
			    gd.bcount, gd.pcount);
		
		} else {
			*to = '\0';
			to++;
			printf("GREY|%s|%s|%s|%s|%lld|%lld|%lld|%d|%d\n",
			    a, helo, from, to, (long long)gd.first,
			    (long long)gd.pass, (long long)gd.expire,
			    gd.bcount, gd.pcount);
		}
	}
	free(a);

	return (0);
}

int
dblist(DB *db)
{
	DBT		dbk, dbd;
	int		r;

	/* walk db, list in text format */
	memset(&dbk, 0, sizeof(dbk));
	memset(&dbd, 0, sizeof(dbd));
	for (r = db->seq(db, &dbk, &dbd, R_FIRST); !r;
	    r = db->seq(db, &dbk, &dbd, R_NEXT)) {
		if (print_entry(&dbk, &dbd) != 0) {
			r = -1;
			break;
		}
	}
	db->close(db);
	db = NULL;
	return (r == -1);
}

int
dbshow(DB *db, char **addrs)
{
	DBT dbk, dbd;
	int errors = 0;
	char *a;

	/* look up each addr */
	while ((a = *addrs) != NULL) {
		memset(&dbk, 0, sizeof(dbk));
		dbk.size = strlen(a);
		dbk.data = a;
		memset(&dbd, 0, sizeof(dbd));
		switch (db->get(db, &dbk, &dbd, 0)) {
		case -1:
			warn("db->get failed");
			errors++;
			goto done;
		case 0:
			if (print_entry(&dbk, &dbd) != 0) {
				errors++;
				goto done;
			}
			break;
		case 1:
		default:
			/* not found */
			errors++;
			break;
		}
		addrs++;
	}
 done:
	db->close(db);
	db = NULL;
	return (errors);
}

extern char *__progname;

static int
usage(void)
{
	fprintf(stderr, "usage: %s [-adGTt] [keys ...]\n", __progname);
	exit(1);
	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	int i, ch, action = 0, type = WHITE, r = 0, c = 0;
	HASHINFO	hashinfo;
	DB		*db;

	while ((ch = getopt(argc, argv, "adGtT")) != -1) {
		switch (ch) {
		case 'a':
			action = 1;
			break;
		case 'd':
			action = 2;
			break;
		case 'G':
			type = GREY;
			break;
		case 't':
			type = TRAPHIT;
			break;
		case 'T':
			type = SPAMTRAP;
			break;
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;
	if (action == 0 && type != WHITE)
		usage();

	memset(&hashinfo, 0, sizeof(hashinfo));
	db = dbopen(PATH_SPAMD_DB, O_EXLOCK | (action ? O_RDWR : O_RDONLY),
	    0600, DB_HASH, &hashinfo);
	if (db == NULL) {
		err(1, "cannot open %s for %s", PATH_SPAMD_DB,
		    action ? "writing" : "reading");
	}

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	switch (action) {
	case 0:
		if (argc)
			return dbshow(db, argv);
		else
			return dblist(db);
	case 1:
		if (type == GREY)
			errx(2, "cannot add GREY entries");
		for (i=0; i<argc; i++)
			if (argv[i][0] != '\0') {
				c++;
				r += dbupdate(db, argv[i], 1, type);
			}
		if (c == 0)
			errx(2, "no addresses specified");
		break;
	case 2:
		for (i=0; i<argc; i++)
			if (argv[i][0] != '\0') {
				c++;
				r += dbupdate(db, argv[i], 0, type);
			}
		if (c == 0)
			errx(2, "no addresses specified");
		break;
	default:
		errx(-1, "bad action");
	}
	db->close(db);
	return (r);
}
