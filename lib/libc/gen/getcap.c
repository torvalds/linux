/*	$OpenBSD: getcap.c,v 1.37 2024/01/22 17:22:58 deraadt Exp $	*/
/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Casey Leedom of Lawrence Livermore National Laboratory.
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

#include <sys/types.h>

#include <ctype.h>
#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	BFRAG		1024
#define	BSIZE		1024
#define	ESC		('[' & 037)	/* ASCII ESC */
#define	MAX_RECURSION	32		/* maximum getent recursion */
#define	SFRAG		100		/* cgetstr mallocs in SFRAG chunks */

#define RECOK	(char)0
#define TCERR	(char)1
#define	SHADOW	(char)2

static size_t	 topreclen;	/* toprec length */
static char	*toprec;	/* Additional record specified by cgetset() */
static int	 gottoprec;	/* Flag indicating retrieval of toprecord */

static int	cdbget(DB *, char **, const char *);
static int 	getent(char **, u_int *, char **, FILE *, const char *, int, char *);
static int	nfcmp(const char *, char *);

static int	usedb = 1;

/*
 * Cgetusedb() allows the user to specify whether or not to use a .db
 * version of the database file (if it exists) in preference to the
 * text version.  By default, the getcap(3) routines will use a .db file.
 */
int
cgetusedb(int new_usedb)
{
	int old_usedb = usedb;

	usedb = new_usedb;
	return(old_usedb);
}
DEF_WEAK(cgetusedb);

/*
 * Cgetset() allows the addition of a user specified buffer to be added
 * to the database array, in effect "pushing" the buffer on top of the
 * virtual database. 0 is returned on success, -1 on failure.
 */
int
cgetset(const char *ent)
{
	if (ent == NULL) {
		free(toprec);
		toprec = NULL;
		topreclen = 0;
		return (0);
	}
	topreclen = strlen(ent);
	if ((toprec = malloc(topreclen + 1)) == NULL)
		return (-1);
	gottoprec = 0;
	memcpy(toprec, ent, topreclen + 1);
	return (0);
}
DEF_WEAK(cgetset);

/*
 * Cgetcap searches the capability record buf for the capability cap with
 * type `type'.  A pointer to the value of cap is returned on success, NULL
 * if the requested capability couldn't be found.
 *
 * Specifying a type of ':' means that nothing should follow cap (:cap:).
 * In this case a pointer to the terminating ':' or NUL will be returned if
 * cap is found.
 *
 * If (cap, '@') or (cap, terminator, '@') is found before (cap, terminator)
 * return NULL.
 */
char *
cgetcap(char *buf, const char *cap, int type)
{
	char *bp;
	const char *cp;

	bp = buf;
	for (;;) {
		/*
		 * Skip past the current capability field - it's either the
		 * name field if this is the first time through the loop, or
		 * the remainder of a field whose name failed to match cap.
		 */
		for (;;)
			if (*bp == '\0')
				return (NULL);
			else
				if (*bp++ == ':')
					break;

		/*
		 * Try to match (cap, type) in buf.
		 */
		for (cp = cap; *cp == *bp && *bp != '\0'; cp++, bp++)
			continue;
		if (*cp != '\0')
			continue;
		if (*bp == '@')
			return (NULL);
		if (type == ':') {
			if (*bp != '\0' && *bp != ':')
				continue;
			return(bp);
		}
		if (*bp != type)
			continue;
		bp++;
		return (*bp == '@' ? NULL : bp);
	}
	/* NOTREACHED */
}
DEF_WEAK(cgetcap);

/*
 * Cgetent extracts the capability record name from the NULL terminated file
 * array db_array and returns a pointer to a malloc'd copy of it in buf.
 * Buf must be retained through all subsequent calls to cgetcap, cgetnum,
 * cgetflag, and cgetstr, but may then be free'd.  0 is returned on success,
 * -1 if the requested record couldn't be found, -2 if a system error was
 * encountered (couldn't open/read a file, etc.), and -3 if a potential
 * reference loop is detected.
 */
int
cgetent(char **buf, char **db_array, const char *name)
{
	u_int dummy;

	return (getent(buf, &dummy, db_array, NULL, name, 0, NULL));
}
DEF_WEAK(cgetent);

/*
 * Getent implements the functions of cgetent.  If fp is non-NULL,
 * *db_array has already been opened and fp is the open file descriptor.  We
 * do this to save time and avoid using up file descriptors for tc=
 * recursions.
 *
 * Getent returns the same success/failure codes as cgetent.  On success, a
 * pointer to a malloc'ed capability record with all tc= capabilities fully
 * expanded and its length (not including trailing ASCII NUL) are left in
 * *cap and *len.
 *
 * Basic algorithm:
 *	+ Allocate memory incrementally as needed in chunks of size BFRAG
 *	  for capability buffer.
 *	+ Recurse for each tc=name and interpolate result.  Stop when all
 *	  names interpolated, a name can't be found, or depth exceeds
 *	  MAX_RECURSION.
 */
static int
getent(char **cap, u_int *len, char **db_array, FILE *fp,
	const char *name, int depth, char *nfield)
{
	DB *capdbp;
	char *r_end, *rp, **db_p;
	int myfd, eof, foundit, opened, retval, clen;
	char *record, *cbuf;
	int tc_not_resolved;
	char pbuf[PATH_MAX];

	/*
	 * Return with ``loop detected'' error if we've recursed more than
	 * MAX_RECURSION times.
	 */
	if (depth > MAX_RECURSION)
		return (-3);

	opened = 0;

	/*
	 * Check if we have a top record from cgetset().
	 */
	if (depth == 0 && toprec != NULL && cgetmatch(toprec, name) == 0) {
		opened++;
		if ((record = malloc(topreclen + 1 + BFRAG)) == NULL)
			return (-2);
		memcpy(record, toprec, topreclen + 1);
		myfd = 0;
		db_p = db_array;
		rp = record + topreclen + 1;
		r_end = rp + BFRAG;
		goto tc_exp;
	}
	/*
	 * Allocate first chunk of memory.
	 */
	if ((record = malloc(BFRAG)) == NULL)
		return (-2);
	r_end = record + BFRAG;
	foundit = 0;
	/*
	 * Loop through database array until finding the record.
	 */

	for (db_p = db_array; *db_p != NULL; db_p++) {
		eof = 0;

		/*
		 * Open database if not already open.
		 */
		if (fp != NULL) {
			fseeko(fp, 0, SEEK_SET);
			myfd = 0;
			opened++;
		} else {
			char *dbrecord;

			clen = snprintf(pbuf, sizeof(pbuf), "%s.db", *db_p);
			if (clen >= 0 && clen < sizeof(pbuf) && usedb &&
			    (capdbp = __hash_open(pbuf, O_RDONLY, 0, NULL, 0))) {
				opened++;
				retval = cdbget(capdbp, &dbrecord, name);
				if (retval < 0) {
					/* no record available */
					(void)capdbp->close(capdbp);
					continue;
				}
				free(record);
				/* save the data; close frees it */
				clen = strlen(dbrecord);
				if ((cbuf = malloc(clen + 1)) == NULL)
					return (-2);
				memcpy(cbuf, dbrecord, clen + 1);
				if (capdbp->close(capdbp) < 0) {
					free(cbuf);
					return (-2);
				}
				/* assume tc='s have been expanded??? */
				*len = clen;
				*cap = cbuf;
				return (retval);
			} else {
				fp = fopen(*db_p, "re");
				if (fp == NULL) {
					/* No error on unfound file. */
					continue;
				}
				myfd = 1;
				opened++;
			}
		}
		/*
		 * Find the requested capability record ...
		 */
		{
		    char buf[BUFSIZ];
		    char *b_end, *bp;
		    int c;

		    /*
		     * Loop invariants:
		     *	There is always room for one more character in record.
		     *	R_end always points just past end of record.
		     *	Rp always points just past last character in record.
		     *	B_end always points just past last character in buf.
		     *	Bp always points at next character in buf.
		     */
		    b_end = buf;
		    bp = buf;
		    for (;;) {

			/*
			 * Read in a line implementing (\, newline)
			 * line continuation.
			 */
			rp = record;
			for (;;) {
				if (bp >= b_end) {
					size_t n;

					n = fread(buf, 1, sizeof(buf), fp);
					if (n == 0) {
						eof = feof(fp);
						if (myfd)
							(void)fclose(fp);
						if (eof) {
							fp = NULL;
							break;
						}
						free(record);
						return (-2);
					}
					b_end = buf+n;
					bp = buf;
				}

				c = *bp++;
				if (c == '\n') {
					if (rp > record && *(rp-1) == '\\') {
						rp--;
						continue;
					} else
						break;
				}
				*rp++ = c;

				/*
				 * Enforce loop invariant: if no room
				 * left in record buffer, try to get
				 * some more.
				 */
				if (rp >= r_end) {
					size_t pos;
					size_t newsize;
					char *nrecord;

					pos = rp - record;
					newsize = r_end - record + BFRAG;
					nrecord = realloc(record, newsize);
					if (nrecord == NULL) {
						free(record);
						if (myfd)
							(void)fclose(fp);
						errno = ENOMEM;
						return (-2);
					}
					record = nrecord;
					r_end = record + newsize;
					rp = record + pos;
				}
			}
				/* loop invariant lets us do this */
			*rp++ = '\0';

			/*
			 * If encountered EOF check next file.
			 */
			if (eof)
				break;

			/*
			 * Toss blank lines and comments.
			 */
			if (*record == '\0' || *record == '#')
				continue;

			/*
			 * See if this is the record we want ...
			 */
			if (cgetmatch(record, name) == 0) {
				if (nfield == NULL || !nfcmp(nfield, record)) {
					foundit = 1;
					break;	/* found it! */
				}
			}
		    }
		}
		if (foundit)
			break;
	}

	if (!foundit) {
		free(record);
		return (opened ? -1 : -2);
	}

	/*
	 * Got the capability record, but now we have to expand all tc=name
	 * references in it ...
	 */
tc_exp:	{
		char *s;
		u_int ilen;
		int diff, iret, tclen;
		char *ibuf, *icap, *scan, *tc, *tcstart, *tcend;

		/*
		 * Loop invariants:
		 *	There is room for one more character in record.
		 *	R_end points just past end of record.
		 *	Rp points just past last character in record.
		 *	Scan points at remainder of record that needs to be
		 *	scanned for tc=name constructs.
		 */
		scan = record;
		tc_not_resolved = 0;
		for (;;) {
			if ((tc = cgetcap(scan, "tc", '=')) == NULL)
				break;

			/*
			 * Find end of tc=name and stomp on the trailing `:'
			 * (if present) so we can use it to call ourselves.
			 */
			s = tc;
			for (;;) {
				if (*s == '\0')
					break;
				else
					if (*s++ == ':') {
						*(s - 1) = '\0';
						break;
					}
			}
			tcstart = tc - 3;
			tclen = s - tcstart;
			tcend = s;

			iret = getent(&icap, &ilen, db_p, fp, tc, depth+1,
				      NULL);
			if (iret != 0) {
				/* an error */
				if (iret < -1) {
					if (myfd)
						(void)fclose(fp);
					free(record);
					return (iret);
				}
				if (iret == 1)
					tc_not_resolved = 1;
				/* couldn't resolve tc */
				if (iret == -1) {
					*(s - 1) = ':';
					scan = s - 1;
					tc_not_resolved = 1;
					continue;

				}
			}
			/* not interested in name field of tc'ed record */
			s = ibuf = icap;
			for (;;)
				if (*s == '\0')
					break;
				else
					if (*s++ == ':')
						break;
			ilen -= s - icap;
			icap = s;

			/* make sure interpolated record is `:'-terminated */
			s += ilen;
			if (*(s-1) != ':') {
				*s = ':';	/* overwrite NUL with : */
				ilen++;
			}

			/*
			 * Make sure there's enough room to insert the
			 * new record.
			 */
			diff = ilen - tclen;
			if (diff >= r_end - rp) {
				u_int pos, tcpos, tcposend;
				size_t newsize;
				char *nrecord;

				pos = rp - record;
				newsize = r_end - record + diff + BFRAG;
				tcpos = tcstart - record;
				tcposend = tcend - record;
				nrecord = realloc(record, newsize);
				if (nrecord == NULL) {
					free(record);
					if (myfd)
						(void)fclose(fp);
					free(ibuf);
					errno = ENOMEM;
					return (-2);
				}
				record = nrecord;
				r_end = record + newsize;
				rp = record + pos;
				tcstart = record + tcpos;
				tcend = record + tcposend;
			}

			/*
			 * Insert tc'ed record into our record.
			 */
			s = tcstart + ilen;
			memmove(s, tcend, rp - tcend);
			memmove(tcstart, icap, ilen);
			rp += diff;
			free(ibuf);

			/*
			 * Start scan on `:' so next cgetcap works properly
			 * (cgetcap always skips first field).
			 */
			scan = s-1;
		}

	}
	/*
	 * Close file (if we opened it), give back any extra memory, and
	 * return capability, length and success.
	 */
	if (myfd)
		(void)fclose(fp);
	*len = rp - record - 1;	/* don't count NUL */
	if (r_end > rp) {
		char *nrecord;

		if ((nrecord = realloc(record, rp - record)) == NULL) {
			free(record);
			errno = ENOMEM;
			return (-2);
		}
		record = nrecord;
	}
	*cap = record;
	if (tc_not_resolved)
		return (1);
	return (0);
}

static int
cdbget(DB *capdbp, char **bp, const char *name)
{
	DBT key, data;

	key.data = (void *)name;
	key.size = strlen(name);

	for (;;) {
		/* Get the reference. */
		switch(capdbp->get(capdbp, &key, &data, 0)) {
		case -1:
			return (-2);
		case 1:
			return (-1);
		}

		/* If not an index to another record, leave. */
		if (((char *)data.data)[0] != SHADOW)
			break;

		key.data = (char *)data.data + 1;
		key.size = data.size - 1;
	}

	*bp = (char *)data.data + 1;
	return (((char *)(data.data))[0] == TCERR ? 1 : 0);
}

/*
 * Cgetmatch will return 0 if name is one of the names of the capability
 * record buf, -1 if not.
 */
int
cgetmatch(char *buf, const char *name)
{
	char *bp;
	const char *np;

	if (*name == '\0')
		return (-1);
	/*
	 * Start search at beginning of record.
	 */
	bp = buf;
	for (;;) {
		/*
		 * Try to match a record name.
		 */
		np = name;
		for (;;)
			if (*np == '\0') {
				if (*bp == '|' || *bp == ':' || *bp == '\0')
					return (0);
				else
					break;
			} else
				if (*bp++ != *np++)
					break;

		/*
		 * Match failed, skip to next name in record.
		 */
		bp--;	/* a '|' or ':' may have stopped the match */
		for (;;)
			if (*bp == '\0' || *bp == ':')
				return (-1);	/* match failed totally */
			else
				if (*bp++ == '|')
					break;	/* found next name */
	}
}
DEF_WEAK(cgetmatch);

int
cgetfirst(char **buf, char **db_array)
{

	(void)cgetclose();
	return (cgetnext(buf, db_array));
}
DEF_WEAK(cgetfirst);

static FILE *pfp;
static int slash;
static char **dbp;

int
cgetclose(void)
{

	if (pfp != NULL) {
		(void)fclose(pfp);
		pfp = NULL;
	}
	dbp = NULL;
	gottoprec = 0;
	slash = 0;
	return(0);
}
DEF_WEAK(cgetclose);

/*
 * Cgetnext() gets either the first or next entry in the logical database
 * specified by db_array.  It returns 0 upon completion of the database, 1
 * upon returning an entry with more remaining, and -1 if an error occurs.
 */
int
cgetnext(char **cap, char **db_array)
{
	size_t len, otopreclen = topreclen;
	int c, serrno, status = -1;
	char buf[BUFSIZ], nbuf[BSIZE];
	char *b_end, *bp, *r_end, *rp;
	char *record = NULL;
	char *otoprec = toprec;
	u_int dummy;
	off_t pos;

	if (dbp == NULL)
		dbp = db_array;

	if (pfp == NULL && (pfp = fopen(*dbp, "re")) == NULL)
		goto done;

	/*
	 * Check if we have an unused top record from cgetset().
	 */
	if (toprec && !gottoprec) {
		gottoprec = 1;
		record = toprec;
		goto lookup;
	}

	/*
	 * Allocate first chunk of memory.
	 */
	if ((record = malloc(BFRAG)) == NULL)
		goto done;
	r_end = record + BFRAG;

	/*
	 * Find the next capability record
	 */
	/*
	 * Loop invariants:
	 *	There is always room for one more character in record.
	 *	R_end always points just past end of record.
	 *	Rp always points just past last character in record.
	 *	B_end always points just past last character in buf.
	 *	Bp always points at next character in buf.
	 */
	b_end = buf;
	bp = buf;
	for (;;) {
		/*
		 * Read in a line implementing (\, newline)
		 * line continuation.
		 */
		rp = record;
		for (;;) {
			if (bp >= b_end) {
				size_t n;

				n = fread(buf, 1, sizeof(buf), pfp);
				if (n == 0) {
					if (ferror(pfp))
						goto done;
					(void)fclose(pfp);
					pfp = NULL;
					if (*++dbp == NULL) {
						status = 0;
						goto done;
					} else if ((pfp =
					    fopen(*dbp, "re")) == NULL) {
						goto done;
					} else
						continue;
				}
				b_end = buf + n;
				bp = buf;
			}

			c = *bp++;
			if (c == '\n') {
				if (rp > record && *(rp-1) == '\\') {
					rp--;
					continue;
				} else
					break;
			}
			*rp++ = c;

			/*
			 * Enforce loop invariant: if no room
			 * left in record buffer, try to get
			 * some more.
			 */
			if (rp >= r_end) {
				size_t newsize, off;
				char *nrecord;

				off = rp - record;
				newsize = r_end - record + BFRAG;
				nrecord = realloc(record, newsize);
				if (nrecord == NULL)
					goto done;
				record = nrecord;
				r_end = record + newsize;
				rp = record + off;
			}
		}
		/* loop invariant lets us do this */
		*rp++ = '\0';

		/*
		 * If not blank or comment, set toprec and topreclen so
		 * getent() doesn't have to re-parse the file to find it.
		 */
		if (*record != '\0' && *record != '#') {
			/* Rewind to end of record */
			fseeko(pfp, bp - b_end, SEEK_CUR);
			toprec = record;
			topreclen = rp - record;
			break;
		}
	}
lookup:
	/* extract name from record */
	len = strcspn(record, "|:");
	memcpy(nbuf, record, len);
	nbuf[len] = '\0';

	/* return value of getent() is one less than cgetnext() */
	pos = ftello(pfp);
	status = getent(cap, &dummy, dbp, pfp, nbuf, 0, NULL) + 1;
	if (status > 0)
		fseeko(pfp, pos, SEEK_SET);
done:
	serrno = errno;
	if (toprec != otoprec) {
		toprec = otoprec;
		topreclen = otopreclen;
		free(record);
	}
	if (status <= 0)
		(void)cgetclose();
	errno = serrno;

	return (status);
}
DEF_WEAK(cgetnext);

/*
 * Cgetstr retrieves the value of the string capability cap from the
 * capability record pointed to by buf.  A pointer to a decoded, NUL
 * terminated, malloc'd copy of the string is returned in the char *
 * pointed to by str.  The length of the string not including the trailing
 * NUL is returned on success, -1 if the requested string capability
 * couldn't be found, -2 if a system error was encountered (storage
 * allocation failure).
 */
int
cgetstr(char *buf, const char *cap, char **str)
{
	u_int m_room;
	char *bp, *mp;
	int len;
	char *mem;

	/*
	 * Find string capability cap
	 */
	bp = cgetcap(buf, cap, '=');
	if (bp == NULL)
		return (-1);

	/*
	 * Conversion / storage allocation loop ...  Allocate memory in
	 * chunks SFRAG in size.
	 */
	if ((mem = malloc(SFRAG)) == NULL)
		return (-2);	/* couldn't even allocate the first fragment */
	m_room = SFRAG;
	mp = mem;

	while (*bp != ':' && *bp != '\0') {
		/*
		 * Loop invariants:
		 *	There is always room for one more character in mem.
		 *	Mp always points just past last character in mem.
		 *	Bp always points at next character in buf.
		 */
		if (*bp == '^') {
			bp++;
			if (*bp == ':' || *bp == '\0')
				break;	/* drop unfinished escape */
			*mp++ = *bp++ & 037;
		} else if (*bp == '\\') {
			bp++;
			if (*bp == ':' || *bp == '\0')
				break;	/* drop unfinished escape */
			if ('0' <= *bp && *bp <= '7') {
				int n, i;

				n = 0;
				i = 3;	/* maximum of three octal digits */
				do {
					n = n * 8 + (*bp++ - '0');
				} while (--i && '0' <= *bp && *bp <= '7');
				*mp++ = n;
			}
			else switch (*bp++) {
				case 'b': case 'B':
					*mp++ = '\b';
					break;
				case 't': case 'T':
					*mp++ = '\t';
					break;
				case 'n': case 'N':
					*mp++ = '\n';
					break;
				case 'f': case 'F':
					*mp++ = '\f';
					break;
				case 'r': case 'R':
					*mp++ = '\r';
					break;
				case 'e': case 'E':
					*mp++ = ESC;
					break;
				case 'c': case 'C':
					*mp++ = ':';
					break;
				default:
					/*
					 * Catches '\', '^', and
					 *  everything else.
					 */
					*mp++ = *(bp-1);
					break;
			}
		} else
			*mp++ = *bp++;
		m_room--;

		/*
		 * Enforce loop invariant: if no room left in current
		 * buffer, try to get some more.
		 */
		if (m_room == 0) {
			size_t size = mp - mem;
			char *nmem;

			if ((nmem = realloc(mem, size + SFRAG)) == NULL) {
				free(mem);
				return (-2);
			}
			mem = nmem;
			m_room = SFRAG;
			mp = mem + size;
		}
	}
	*mp++ = '\0';	/* loop invariant let's us do this */
	m_room--;
	len = mp - mem - 1;

	/*
	 * Give back any extra memory and return value and success.
	 */
	if (m_room != 0) {
		char *nmem;

		if ((nmem = realloc(mem, mp - mem)) == NULL) {
			free(mem);
			return (-2);
		}
		mem = nmem;
	}
	*str = mem;
	return (len);
}
DEF_WEAK(cgetstr);

/*
 * Cgetustr retrieves the value of the string capability cap from the
 * capability record pointed to by buf.  The difference between cgetustr()
 * and cgetstr() is that cgetustr does not decode escapes but rather treats
 * all characters literally.  A pointer to a  NUL terminated malloc'd
 * copy of the string is returned in the char pointed to by str.  The
 * length of the string not including the trailing NUL is returned on success,
 * -1 if the requested string capability couldn't be found, -2 if a system
 * error was encountered (storage allocation failure).
 */
int
cgetustr(char *buf, const char *cap, char **str)
{
	u_int m_room;
	char *bp, *mp;
	int len;
	char *mem;

	/*
	 * Find string capability cap
	 */
	if ((bp = cgetcap(buf, cap, '=')) == NULL)
		return (-1);

	/*
	 * Conversion / storage allocation loop ...  Allocate memory in
	 * chunks SFRAG in size.
	 */
	if ((mem = malloc(SFRAG)) == NULL)
		return (-2);	/* couldn't even allocate the first fragment */
	m_room = SFRAG;
	mp = mem;

	while (*bp != ':' && *bp != '\0') {
		/*
		 * Loop invariants:
		 *	There is always room for one more character in mem.
		 *	Mp always points just past last character in mem.
		 *	Bp always points at next character in buf.
		 */
		*mp++ = *bp++;
		m_room--;

		/*
		 * Enforce loop invariant: if no room left in current
		 * buffer, try to get some more.
		 */
		if (m_room == 0) {
			size_t size = mp - mem;
			char *nmem;

			if ((nmem = realloc(mem, size + SFRAG)) == NULL) {
				free(mem);
				return (-2);
			}
			mem = nmem;
			m_room = SFRAG;
			mp = mem + size;
		}
	}
	*mp++ = '\0';	/* loop invariant let's us do this */
	m_room--;
	len = mp - mem - 1;

	/*
	 * Give back any extra memory and return value and success.
	 */
	if (m_room != 0) {
		char *nmem;

		if ((nmem = realloc(mem, mp - mem)) == NULL) {
			free(mem);
			return (-2);
		}
		mem = nmem;
	}
	*str = mem;
	return (len);
}
DEF_WEAK(cgetustr);

/*
 * Cgetnum retrieves the value of the numeric capability cap from the
 * capability record pointed to by buf.  The numeric value is returned in
 * the long pointed to by num.  0 is returned on success, -1 if the requested
 * numeric capability couldn't be found.
 */
int
cgetnum(char *buf, const char *cap, long *num)
{
	long n;
	int base, digit;
	char *bp;

	/*
	 * Find numeric capability cap
	 */
	bp = cgetcap(buf, cap, '#');
	if (bp == NULL)
		return (-1);

	/*
	 * Look at value and determine numeric base:
	 *	0x... or 0X...	hexadecimal,
	 * else	0...		octal,
	 * else			decimal.
	 */
	if (*bp == '0') {
		bp++;
		if (*bp == 'x' || *bp == 'X') {
			bp++;
			base = 16;
		} else
			base = 8;
	} else
		base = 10;

	/*
	 * Conversion loop ...
	 */
	n = 0;
	for (;;) {
		if ('0' <= *bp && *bp <= '9')
			digit = *bp - '0';
		else if ('a' <= *bp && *bp <= 'f')
			digit = 10 + *bp - 'a';
		else if ('A' <= *bp && *bp <= 'F')
			digit = 10 + *bp - 'A';
		else
			break;

		if (digit >= base)
			break;

		n = n * base + digit;
		bp++;
	}

	/*
	 * Return value and success.
	 */
	*num = n;
	return (0);
}
DEF_WEAK(cgetnum);

/*
 * Compare name field of record.
 */
static int
nfcmp(const char *nf, char *rec)
{
	char *cp, tmp;
	int ret;

	for (cp = rec; *cp != ':'; cp++)
		;

	tmp = *(cp + 1);
	*(cp + 1) = '\0';
	ret = strcmp(nf, rec);
	*(cp + 1) = tmp;

	return (ret);
}
