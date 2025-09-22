/*	$OpenBSD: dbtest.c,v 1.21 2022/12/04 23:50:46 cheloha Exp $	*/
/*	$NetBSD: dbtest.c,v 1.8 1996/05/03 21:57:48 cgd Exp $	*/

/*-
 * Copyright (c) 1992, 1993, 1994
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

#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include <db.h>

#define MINIMUM(a, b)    (((a) < (b)) ? (a) : (b))

enum S { COMMAND, COMPARE, GET, PUT, REMOVE, SEQ, SEQFLAG, KEY, DATA };

void	 compare(DBT *, DBT *);
DBTYPE	 dbtype(char *);
void	 dump(DB *, int);
void	 __dead dberr(const char *, ...);
void	 get(DB *, DBT *);
void	 getdata(DB *, DBT *, DBT *);
void	 put(DB *, DBT *, DBT *);
void	 rem(DB *, DBT *);
char	*sflags(int);
void	 synk(DB *);
void	*rfile(char *, size_t *);
void	 seq(DB *, DBT *);
u_int	 setflags(char *);
void	*setinfo(DBTYPE, char *);
void	 __dead usage(void);
void	*xmalloc(char *, size_t);

DBTYPE type;				/* Database type. */
void *infop;				/* Iflags. */
u_long lineno;				/* Current line in test script. */
u_int flags;				/* Current DB flags. */
int ofd = STDOUT_FILENO;		/* Standard output fd. */

DB *XXdbp;				/* Global for gdb. */
int XXlineno;				/* Fast breakpoint for gdb. */

int
main(int argc, char *argv[])
{
	enum S command, state;
	DB *dbp;
	DBT data, key, keydata;
	size_t len;
	int ch, oflags, sflag;
	char *fname, *infoarg, *p, *t, buf[8 * 1024];

	infoarg = NULL;
	fname = NULL;
	oflags = O_CREAT | O_RDWR;
	sflag = 0;
	while ((ch = getopt(argc, argv, "f:i:lo:s")) != -1)
		switch (ch) {
		case 'f':
			fname = optarg;
			break;
		case 'i':
			infoarg = optarg;
			break;
		case 'l':
			oflags |= DB_LOCK;
			break;
		case 'o':
			if ((ofd = open(optarg,
			    O_WRONLY|O_CREAT|O_TRUNC, 0666)) < 0)
				dberr("%s: %s", optarg, strerror(errno));
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	/* Set the type. */
	type = dbtype(*argv++);

	/* Open the descriptor file. */
        if (strcmp(*argv, "-") && freopen(*argv, "r", stdin) == NULL)
	    dberr("%s: %s", *argv, strerror(errno));

	/* Set up the db structure as necessary. */
	if (infoarg == NULL)
		infop = NULL;
	else
		for (p = strtok(infoarg, ",\t "); p != NULL;
		    p = strtok(0, ",\t "))
			if (*p != '\0')
				infop = setinfo(type, p);

	/*
	 * Open the DB.  Delete any preexisting copy, you almost never
	 * want it around, and it often screws up tests.
	 */
	if (fname == NULL) {
		p = getenv("TMPDIR");
		if (p == NULL)
			p = "/tmp";
		(void)snprintf(buf, sizeof buf, "%s/__dbtest", p);
		fname = buf;
		(void)unlink(buf);
	} else  if (!sflag)
		(void)unlink(fname);

	if ((dbp = dbopen(fname,
	    oflags, S_IRUSR | S_IWUSR, type, infop)) == NULL)
		dberr("dbopen: %s", strerror(errno));
	XXdbp = dbp;

	state = COMMAND;
	for (lineno = 1;
	    (p = fgets(buf, sizeof(buf), stdin)) != NULL; ++lineno) {
		/* Delete the newline, displaying the key/data is easier. */
		if (ofd == STDOUT_FILENO && (t = strchr(p, '\n')) != NULL)
			*t = '\0';
		if ((len = strlen(buf)) == 0 || isspace((unsigned char)*p) || *p == '#')
			continue;

		/* Convenient gdb break point. */
		if (XXlineno == lineno)
			XXlineno = 1;
		switch (*p) {
		case 'c':			/* compare */
			if (state != COMMAND)
				dberr("line %lu: not expecting command",
				    lineno);
			state = KEY;
			command = COMPARE;
			break;
		case 'e':			/* echo */
			if (state != COMMAND)
				dberr("line %lu: not expecting command",
				    lineno);
			/* Don't display the newline, if CR at EOL. */
			if (p[len - 2] == '\r')
				--len;
			if (write(ofd, p + 1, len - 1) != len - 1 ||
			    write(ofd, "\n", 1) != 1)
				dberr("write: %s", strerror(errno));
			break;
		case 'g':			/* get */
			if (state != COMMAND)
				dberr("line %lu: not expecting command",
				    lineno);
			state = KEY;
			command = GET;
			break;
		case 'p':			/* put */
			if (state != COMMAND)
				dberr("line %lu: not expecting command",
				    lineno);
			state = KEY;
			command = PUT;
			break;
		case 'r':			/* remove */
			if (state != COMMAND)
				dberr("line %lu: not expecting command",
				    lineno);
                        if (flags == R_CURSOR) {
				rem(dbp, &key);
				state = COMMAND;
                        } else {
				state = KEY;
				command = REMOVE;
			}
			break;
		case 'S':			/* sync */
			if (state != COMMAND)
				dberr("line %lu: not expecting command",
				    lineno);
			synk(dbp);
			state = COMMAND;
			break;
		case 's':			/* seq */
			if (state != COMMAND)
				dberr("line %lu: not expecting command",
				    lineno);
			if (flags == R_CURSOR) {
				state = KEY;
				command = SEQ;
			} else
				seq(dbp, &key);
			break;
		case 'f':
			flags = setflags(p + 1);
			break;
		case 'D':			/* data file */
			if (state != DATA)
				dberr("line %lu: not expecting data", lineno);
			data.data = rfile(p + 1, &data.size);
			goto ldata;
		case 'd':			/* data */
			if (state != DATA)
				dberr("line %lu: not expecting data", lineno);
			data.data = xmalloc(p + 1, len - 1);
			data.size = len - 1;
ldata:			switch (command) {
			case COMPARE:
				compare(&keydata, &data);
				break;
			case PUT:
				put(dbp, &key, &data);
				break;
			default:
				dberr("line %lu: command doesn't take data",
				    lineno);
			}
			if (type != DB_RECNO)
				free(key.data);
			free(data.data);
			state = COMMAND;
			break;
		case 'K':			/* key file */
			if (state != KEY)
				dberr("line %lu: not expecting a key", lineno);
			if (type == DB_RECNO)
				dberr("line %lu: 'K' not available for recno",
				    lineno);
			key.data = rfile(p + 1, &key.size);
			goto lkey;
		case 'k':			/* key */
			if (state != KEY)
				dberr("line %lu: not expecting a key", lineno);
			if (type == DB_RECNO) {
				static recno_t recno;
				recno = atoi(p + 1);
				key.data = &recno;
				key.size = sizeof(recno);
			} else {
				key.data = xmalloc(p + 1, len - 1);
				key.size = len - 1;
			}
lkey:			switch (command) {
			case COMPARE:
				getdata(dbp, &key, &keydata);
				state = DATA;
				break;
			case GET:
				get(dbp, &key);
				if (type != DB_RECNO)
					free(key.data);
				state = COMMAND;
				break;
			case PUT:
				state = DATA;
				break;
			case REMOVE:
				rem(dbp, &key);
				if ((type != DB_RECNO) && (flags != R_CURSOR))
					free(key.data);
				state = COMMAND;
				break;
			case SEQ:
				seq(dbp, &key);
				if ((type != DB_RECNO) && (flags != R_CURSOR))
					free(key.data);
				state = COMMAND;
				break;
			default:
				dberr("line %lu: command doesn't take a key",
				    lineno);
			}
			break;
		case 'o':
			dump(dbp, p[1] == 'r');
			break;
		default:
			dberr("line %lu: %s: unknown command character",
			    lineno, p);
		}
	}
#ifdef STATISTICS
	/*
	 * -l must be used (DB_LOCK must be set) for this to be
	 * used, otherwise a page will be locked and it will fail.
	 */
	if (type == DB_BTREE && oflags & DB_LOCK)
		__bt_stat(dbp);
#endif
	if (dbp->close(dbp))
		dberr("db->close: %s", strerror(errno));
	(void)close(ofd);
	exit(0);
}

#define	NOOVERWRITE	"put failed, would overwrite key\n"

void
compare(db1, db2)
	DBT *db1, *db2;
{
	register size_t len;
	register u_char *p1, *p2;

	if (db1->size != db2->size)
		printf("compare failed: key->data len %lu != data len %lu\n",
		    db1->size, db2->size);

	len = MINIMUM(db1->size, db2->size);
	for (p1 = db1->data, p2 = db2->data; len--;)
		if (*p1++ != *p2++) {
			printf("compare failed at offset %ld\n",
			    p1 - (u_char *)db1->data);
			break;
		}
}

void
get(dbp, kp)
	DB *dbp;
	DBT *kp;
{
	DBT data;

	switch (dbp->get(dbp, kp, &data, flags)) {
	case 0:
		(void)write(ofd, data.data, data.size);
		if (ofd == STDOUT_FILENO)
			(void)write(ofd, "\n", 1);
		break;
	case -1:
		dberr("line %lu: get: %s", lineno, strerror(errno));
		/* NOTREACHED */
	case 1:
#define	NOSUCHKEY	"get failed, no such key\n"
		if (ofd != STDOUT_FILENO)
			(void)write(ofd, NOSUCHKEY, sizeof(NOSUCHKEY) - 1);
		else
			(void)fprintf(stderr, "%lu: %.*s: %s", lineno,
			    MINIMUM((int)kp->size, 20), kp->data, NOSUCHKEY);
#undef	NOSUCHKEY
		break;
	}
}

void
getdata(dbp, kp, dp)
	DB *dbp;
	DBT *kp, *dp;
{
	switch (dbp->get(dbp, kp, dp, flags)) {
	case 0:
		return;
	case -1:
		dberr("line %lu: getdata: %s", lineno, strerror(errno));
		/* NOTREACHED */
	case 1:
		dberr("line %lu: getdata failed, no such key", lineno);
		/* NOTREACHED */
	}
}

void
put(dbp, kp, dp)
	DB *dbp;
	DBT *kp, *dp;
{
	switch (dbp->put(dbp, kp, dp, flags)) {
	case 0:
		break;
	case -1:
		dberr("line %lu: put: %s", lineno, strerror(errno));
		/* NOTREACHED */
	case 1:
		(void)write(ofd, NOOVERWRITE, sizeof(NOOVERWRITE) - 1);
		break;
	}
}

void
rem(dbp, kp)
	DB *dbp;
	DBT *kp;
{
	switch (dbp->del(dbp, kp, flags)) {
	case 0:
		break;
	case -1:
		dberr("line %lu: rem: %s", lineno, strerror(errno));
		/* NOTREACHED */
	case 1:
#define	NOSUCHKEY	"rem failed, no such key\n"
		if (ofd != STDOUT_FILENO)
			(void)write(ofd, NOSUCHKEY, sizeof(NOSUCHKEY) - 1);
		else if (flags != R_CURSOR)
			(void)fprintf(stderr, "%lu: %.*s: %s", lineno,
			    MINIMUM((int)kp->size, 20), kp->data, NOSUCHKEY);
		else
			(void)fprintf(stderr,
			    "%lu: rem of cursor failed\n", lineno);
#undef	NOSUCHKEY
		break;
	}
}

void
synk(dbp)
	DB *dbp;
{
	switch (dbp->sync(dbp, flags)) {
	case 0:
		break;
	case -1:
		dberr("line %lu: synk: %s", lineno, strerror(errno));
		/* NOTREACHED */
	}
}

void
seq(dbp, kp)
	DB *dbp;
	DBT *kp;
{
	DBT data;

	switch (dbp->seq(dbp, kp, &data, flags)) {
	case 0:
		(void)write(ofd, data.data, data.size);
		if (ofd == STDOUT_FILENO)
			(void)write(ofd, "\n", 1);
		break;
	case -1:
		dberr("line %lu: seq: %s", lineno, strerror(errno));
		/* NOTREACHED */
	case 1:
#define	NOSUCHKEY	"seq failed, no such key\n"
		if (ofd != STDOUT_FILENO)
			(void)write(ofd, NOSUCHKEY, sizeof(NOSUCHKEY) - 1);
		else if (flags == R_CURSOR)
			(void)fprintf(stderr, "%lu: %.*s: %s", lineno,
			    MINIMUM((int)kp->size, 20), kp->data, NOSUCHKEY);
		else
			(void)fprintf(stderr,
			    "%lu: seq (%s) failed\n", lineno, sflags(flags));
#undef	NOSUCHKEY
		break;
	}
}

void
dump(dbp, rev)
	DB *dbp;
	int rev;
{
	DBT key, data;
	int flags, nflags;

	if (rev) {
		flags = R_LAST;
		nflags = R_PREV;
	} else {
		flags = R_FIRST;
		nflags = R_NEXT;
	}
	for (;; flags = nflags)
		switch (dbp->seq(dbp, &key, &data, flags)) {
		case 0:
			(void)write(ofd, data.data, data.size);
			if (ofd == STDOUT_FILENO)
				(void)write(ofd, "\n", 1);
			break;
		case 1:
			goto done;
		case -1:
			dberr("line %lu: (dump) seq: %s",
			    lineno, strerror(errno));
			/* NOTREACHED */
		}
done:	return;
}
	
u_int
setflags(s)
	char *s;
{
	char *p;

	for (; isspace((unsigned char)*s); ++s);
	if (*s == '\n' || *s == '\0')
		return (0);
	if ((p = strchr(s, '\n')) != NULL)
		*p = '\0';
	if (!strcmp(s, "R_CURSOR"))		return (R_CURSOR);
	if (!strcmp(s, "R_FIRST"))		return (R_FIRST);
	if (!strcmp(s, "R_IAFTER")) 		return (R_IAFTER);
	if (!strcmp(s, "R_IBEFORE")) 		return (R_IBEFORE);
	if (!strcmp(s, "R_LAST")) 		return (R_LAST);
	if (!strcmp(s, "R_NEXT")) 		return (R_NEXT);
	if (!strcmp(s, "R_NOOVERWRITE"))	return (R_NOOVERWRITE);
	if (!strcmp(s, "R_PREV"))		return (R_PREV);
	if (!strcmp(s, "R_SETCURSOR"))		return (R_SETCURSOR);

	dberr("line %lu: %s: unknown flag", lineno, s);
	/* NOTREACHED */
}

char *
sflags(flags)
	int flags;
{
	switch (flags) {
	case R_CURSOR:		return ("R_CURSOR");
	case R_FIRST:		return ("R_FIRST");
	case R_IAFTER:		return ("R_IAFTER");
	case R_IBEFORE:		return ("R_IBEFORE");
	case R_LAST:		return ("R_LAST");
	case R_NEXT:		return ("R_NEXT");
	case R_NOOVERWRITE:	return ("R_NOOVERWRITE");
	case R_PREV:		return ("R_PREV");
	case R_SETCURSOR:	return ("R_SETCURSOR");
	}

	return ("UNKNOWN!");
}
	
DBTYPE
dbtype(s)
	char *s;
{
	if (!strcmp(s, "btree"))
		return (DB_BTREE);
	if (!strcmp(s, "hash"))
		return (DB_HASH);
	if (!strcmp(s, "recno"))
		return (DB_RECNO);
	dberr("%s: unknown type (use btree, hash or recno)", s);
	/* NOTREACHED */
}

void *
setinfo(type, s)
	DBTYPE type;
	char *s;
{
	static BTREEINFO ib;
	static HASHINFO ih;
	static RECNOINFO rh;
	char *eq;

	if ((eq = strchr(s, '=')) == NULL)
		dberr("%s: illegal structure set statement", s);
	*eq++ = '\0';
	if (!isdigit((unsigned char)*eq))
		dberr("%s: structure set statement must be a number", s);
		
	switch (type) {
	case DB_BTREE:
		if (!strcmp("flags", s)) {
			ib.flags = atoi(eq);
			return (&ib);
		}
		if (!strcmp("cachesize", s)) {
			ib.cachesize = atoi(eq);
			return (&ib);
		}
		if (!strcmp("maxkeypage", s)) {
			ib.maxkeypage = atoi(eq);
			return (&ib);
		}
		if (!strcmp("minkeypage", s)) {
			ib.minkeypage = atoi(eq);
			return (&ib);
		}
		if (!strcmp("lorder", s)) {
			ib.lorder = atoi(eq);
			return (&ib);
		}
		if (!strcmp("psize", s)) {
			ib.psize = atoi(eq);
			return (&ib);
		}
		break;
	case DB_HASH:
		if (!strcmp("bsize", s)) {
			ih.bsize = atoi(eq);
			return (&ih);
		}
		if (!strcmp("ffactor", s)) {
			ih.ffactor = atoi(eq);
			return (&ih);
		}
		if (!strcmp("nelem", s)) {
			ih.nelem = atoi(eq);
			return (&ih);
		}
		if (!strcmp("cachesize", s)) {
			ih.cachesize = atoi(eq);
			return (&ih);
		}
		if (!strcmp("lorder", s)) {
			ih.lorder = atoi(eq);
			return (&ih);
		}
		break;
	case DB_RECNO:
		if (!strcmp("flags", s)) {
			rh.flags = atoi(eq);
			return (&rh);
		}
		if (!strcmp("cachesize", s)) {
			rh.cachesize = atoi(eq);
			return (&rh);
		}
		if (!strcmp("lorder", s)) {
			rh.lorder = atoi(eq);
			return (&rh);
		}
		if (!strcmp("reclen", s)) {
			rh.reclen = atoi(eq);
			return (&rh);
		}
		if (!strcmp("bval", s)) {
			rh.bval = atoi(eq);
			return (&rh);
		}
		if (!strcmp("psize", s)) {
			rh.psize = atoi(eq);
			return (&rh);
		}
		break;
	}
	dberr("%s: unknown structure value", s);
	/* NOTREACHED */
}

void *
rfile(name, lenp)
	char *name;
	size_t *lenp;
{
	struct stat sb;
	void *p;
	int fd;
	char *np;

	for (; isspace((unsigned char)*name); ++name);
	if ((np = strchr(name, '\n')) != NULL)
		*np = '\0';
	if ((fd = open(name, O_RDONLY)) < 0 ||
	    fstat(fd, &sb))
		dberr("%s: %s\n", name, strerror(errno));
	if (sb.st_size > (off_t)INT_MAX)
		dberr("%s: %s\n", name, strerror(E2BIG));
	if ((p = (void *)malloc((u_int)sb.st_size)) == NULL)
		dberr("%s", strerror(errno));
	(void)read(fd, p, (int)sb.st_size);
	*lenp = sb.st_size;
	(void)close(fd);
	return (p);
}

void *
xmalloc(text, len)
	char *text;
	size_t len;
{
	void *p;

	if ((p = (void *)malloc(len)) == NULL)
		dberr("%s", strerror(errno));
	memmove(p, text, len);
	return (p);
}

void __dead
usage()
{
	(void)fprintf(stderr,
	    "usage: dbtest [-l] [-f file] [-i info] [-o file] type script\n");
	exit(1);
}

void __dead
dberr(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)fprintf(stderr, "dbtest: ");
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fprintf(stderr, "\n");
	exit(1);
	/* NOTREACHED */
}
