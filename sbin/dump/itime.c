/*	$OpenBSD: itime.c,v 1.27 2024/05/09 08:35:40 florian Exp $	*/
/*	$NetBSD: itime.c,v 1.4 1997/04/15 01:09:50 lukem Exp $	*/

/*-
 * Copyright (c) 1980, 1993
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

#include <sys/param.h>	/* MAXBSIZE */
#include <sys/time.h>
#include <ufs/ufs/dinode.h>

#include <protocols/dumprestore.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "dump.h"

int	nddates = 0;
struct	dumpdates **ddatev = NULL;
char	*dumpdates = NULL;
char	lastlevel = 0;
int	ddates_in = 0;
struct	dumptime *dthead = NULL;

static	void dumprecout(FILE *, struct dumpdates *);
static	int getrecord(FILE *, struct dumpdates *);
static	int makedumpdate(struct dumpdates *, char *);
static	void readdumptimes(FILE *);

void
initdumptimes(void)
{
	FILE *df;

	if ((df = fopen(dumpdates, "r")) == NULL) {
		if (errno != ENOENT) {
			quit("cannot read %s: %s\n", dumpdates,
			    strerror(errno));
			/* NOTREACHED */
		}
		/*
		 * Dumpdates does not exist, make an empty one.
		 */
		msg("WARNING: no file `%s', making an empty one\n", dumpdates);
		if ((df = fopen(dumpdates, "w")) == NULL) {
			quit("cannot create %s: %s\n", dumpdates,
			    strerror(errno));
			/* NOTREACHED */
		}
		(void) fclose(df);
		if ((df = fopen(dumpdates, "r")) == NULL) {
			quit("cannot read %s even after creating it: %s\n",
			    dumpdates, strerror(errno));
			/* NOTREACHED */
		}
	}
	(void) flock(fileno(df), LOCK_SH);
	readdumptimes(df);
	(void) fclose(df);
}

static void
readdumptimes(FILE *df)
{
	int i;
	struct	dumptime *dtwalk;

	for (;;) {
		dtwalk = calloc(1, sizeof(struct dumptime));
		if (dtwalk == NULL)
			quit("allocation failed");
		if (getrecord(df, &(dtwalk->dt_value)) < 0) {
			free(dtwalk);
			break;
		}
		nddates++;
		dtwalk->dt_next = dthead;
		dthead = dtwalk;
	}

	ddates_in = 1;
	/*
	 *	arrayify the list, leaving enough room for the additional
	 *	record that we may have to add to the ddate structure
	 */
	ddatev = calloc((unsigned) (nddates + 1), sizeof(struct dumpdates *));
	if (ddatev == NULL)
		quit("allocation failed");
	dtwalk = dthead;
	for (i = nddates - 1; i >= 0; i--, dtwalk = dtwalk->dt_next)
		ddatev[i] = &dtwalk->dt_value;
}

void
getdumptime(void)
{
	struct dumpdates *ddp;
	int i;
	char *fname;

	fname = duid ? duid : disk;
#ifdef FDEBUG
	msg("Looking for name %s in dumpdates = %s for level = %c\n",
		fname, dumpdates, level);
#endif
	spcl.c_ddate = 0;
	lastlevel = '0';

	initdumptimes();
	/*
	 *	Go find the entry with the same name for a lower increment
	 *	and older date
	 */
	ITITERATE(i, ddp) {
		if ((strncmp(fname, ddp->dd_name, sizeof(ddp->dd_name)) != 0) &&
		    (strncmp(disk, ddp->dd_name, sizeof(ddp->dd_name)) != 0))
			continue;
		if (ddp->dd_level >= level)
			continue;
		if (ddp->dd_ddate <= (time_t)spcl.c_ddate)
			continue;
		spcl.c_ddate = (int64_t)ddp->dd_ddate;
		lastlevel = ddp->dd_level;
	}
}

void
putdumptime(void)
{
	FILE *df;
	struct dumpdates *dtwalk;
	int fd, i;
	char *fname, *ct;
	time_t t;

	if(uflag == 0)
		return;
	if ((df = fopen(dumpdates, "r+")) == NULL)
		quit("cannot rewrite %s: %s\n", dumpdates, strerror(errno));
	fd = fileno(df);
	(void) flock(fd, LOCK_EX);
	fname = duid ? duid : disk;
	free(ddatev);
	ddatev = NULL;
	nddates = 0;
	dthead = NULL;
	ddates_in = 0;
	readdumptimes(df);
	if (fseek(df, 0L, SEEK_SET) == -1)
		quit("fseek: %s\n", strerror(errno));
	spcl.c_ddate = 0;
	ITITERATE(i, dtwalk) {
		if ((strncmp(fname, dtwalk->dd_name,
			     sizeof(dtwalk->dd_name)) != 0) &&
		    (strncmp(disk, dtwalk->dd_name,
			     sizeof(dtwalk->dd_name)) != 0))
			continue;
		if (dtwalk->dd_level != level)
			continue;
		goto found;
	}
	/*
	 *	construct the new upper bound;
	 *	Enough room has been allocated.
	 */
	dtwalk = ddatev[nddates] = calloc(1, sizeof(struct dumpdates));
	if (dtwalk == NULL)
		quit("allocation failed");
	nddates += 1;
  found:
	(void) strlcpy(dtwalk->dd_name, fname, sizeof(dtwalk->dd_name));
	dtwalk->dd_level = level;
	dtwalk->dd_ddate = (time_t)spcl.c_date;

	ITITERATE(i, dtwalk) {
		dumprecout(df, dtwalk);
	}
	if (fflush(df))
		quit("%s: %s\n", dumpdates, strerror(errno));
	if (ftruncate(fd, ftello(df)))
		quit("ftruncate (%s): %s\n", dumpdates, strerror(errno));
	(void) fclose(df);
	t = (time_t)spcl.c_date;
	if (t == 0)
		ct = "the epoch\n";
	else if ((ct = ctime(&t)) == NULL)
		ct = "?\n";
	msg("level %c dump on %s", level, ct);
}

static void
dumprecout(FILE *file, struct dumpdates *what)
{
	char *ct;

	ct = ctime(&what->dd_ddate);
	if (ct == NULL)
		quit("Cannot convert date\n");

	if (fprintf(file, DUMPOUTFMT,
		    what->dd_name,
		    what->dd_level,
		    ctime(&what->dd_ddate)) < 0)
		quit("%s: %s\n", dumpdates, strerror(errno));
}

int	recno;

static int
getrecord(FILE *df, struct dumpdates *ddatep)
{
	char tbuf[BUFSIZ];

	recno = 0;
	if (fgets(tbuf, sizeof(tbuf), df) == NULL)
		return(-1);
	recno++;
	if (makedumpdate(ddatep, tbuf) < 0)
		msg("Unknown intermediate format in %s, line %d\n",
			dumpdates, recno);

#ifdef FDEBUG
	{
		char *ct;

		if (ddatep->dd_ddate == 0)
			ct = "the epoch\n";
		else
			ct = ctime(&ddatep->dd_ddate);

		if (ct)
			msg("getrecord: %s %c %s", ddatep->dd_name,
			    ddatep->dd_level, ct);
		else
			msg("getrecord: %s %c %lld seconds after the epoch\n",
			    ddatep->dd_name, ddatep->dd_level,
			    ddatep->dd_ddate);
	}
#endif
	return(0);
}

static int
makedumpdate(struct dumpdates *ddp, char *tbuf)
{
	char un_buf[BUFSIZ], *str;
	struct tm then;

	if (sscanf(tbuf, DUMPINFMT, ddp->dd_name, &ddp->dd_level, un_buf) != 3)
		return(-1);
	str = getduid(ddp->dd_name);
	if (str != NULL) {
		strlcpy(ddp->dd_name, str, sizeof(ddp->dd_name));
		free(str);
	}
	str = strptime(un_buf, "%a %b %e %H:%M:%S %Y", &then);
	then.tm_isdst = -1;
	if (str == NULL || (*str != '\n' && *str != '\0'))
		ddp->dd_ddate = (time_t) -1;
	else
		ddp->dd_ddate = mktime(&then);
	if (ddp->dd_ddate < 0)
		return(-1);
	return(0);
}
