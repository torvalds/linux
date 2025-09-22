/*	$OpenBSD: scores.c,v 1.25 2019/06/28 13:32:52 deraadt Exp $	*/
/*	$NetBSD: scores.c,v 1.2 1995/04/22 07:42:38 cgd Exp $	*/

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek and Darren F. Provine.
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
 *
 *	@(#)scores.c	8.1 (Berkeley) 5/31/93
 */

/*
 * Score code for Tetris, by Darren Provine (kilroy@gboro.glassboro.edu)
 * modified 22 January 1992, to limit the number of entries any one
 * person has.
 *
 * Major whacks since then.
 */
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <term.h>
#include <time.h>
#include <unistd.h>

#include "scores.h"
#include "screen.h"
#include "tetris.h"

/*
 * Within this code, we can hang onto one extra "high score", leaving
 * room for our current score (whether or not it is high).
 *
 * We also sometimes keep tabs on the "highest" score on each level.
 * As long as the scores are kept sorted, this is simply the first one at
 * that level.
 */
#define NUMSPOTS (MAXHISCORES + 1)
#define	NLEVELS (MAXLEVEL + 1)

static time_t now;
static int nscores;
static int gotscores;
static struct highscore scores[NUMSPOTS];

static int checkscores(struct highscore *, int);
static int cmpscores(const void *, const void *);
static void getscores(FILE **);
static void printem(int, int, struct highscore *, int, const char *);
static char *thisuser(void);

/*
 * Read the score file.  Can be called from savescore (before showscores)
 * or showscores (if savescore will not be called).  If the given pointer
 * is not NULL, sets *fpp to an open file pointer that corresponds to a
 * read/write score file that is locked with LOCK_EX.  Otherwise, the
 * file is locked with LOCK_SH for the read and closed before return.
 *
 * Note, we assume closing the stdio file releases the lock.
 */
static void
getscores(FILE **fpp)
{
	int sd, mint, i;
	char *mstr, *human;
	FILE *sf;

	if (fpp != NULL) {
		mint = O_RDWR | O_CREAT;
		mstr = "r+";
		human = "read/write";
		*fpp = NULL;
	} else {
		mint = O_RDONLY;
		mstr = "r";
		human = "reading";
	}

	sd = open(scorepath, mint, 0666);
	if (sd == -1) {
		if (fpp == NULL) {
			nscores = 0;
			return;
		}
		err(1, "cannot open %s for %s", scorepath, human);
	}
	if ((sf = fdopen(sd, mstr)) == NULL)
		err(1, "cannot fdopen %s for %s", scorepath, human);

	nscores = fread(scores, sizeof(scores[0]), MAXHISCORES, sf);
	if (ferror(sf))
		err(1, "error reading %s", scorepath);
	for (i = 0; i < nscores; i++)
		if (scores[i].hs_level < MINLEVEL ||
		    scores[i].hs_level > MAXLEVEL)
			errx(1, "scorefile %s corrupt", scorepath);

	if (fpp)
		*fpp = sf;
	else
		(void)fclose(sf);
}

void
savescore(int level)
{
	struct highscore *sp;
	int i;
	int change;
	FILE *sf;
	const char *me;

	getscores(&sf);
	gotscores = 1;
	(void)time(&now);

	/*
	 * Allow at most one score per person per level -- see if we
	 * can replace an existing score, or (easiest) do nothing.
	 * Otherwise add new score at end (there is always room).
	 */
	change = 0;
	me = thisuser();
	for (i = 0, sp = &scores[0]; i < nscores; i++, sp++) {
		if (sp->hs_level != level || strcmp(sp->hs_name, me) != 0)
			continue;
		if (score > sp->hs_score) {
			(void)printf("%s bettered %s %d score of %d!\n",
			    "\nYou", "your old level", level,
			    sp->hs_score * sp->hs_level);
			sp->hs_score = score;	/* new score */
			sp->hs_time = now;	/* and time */
			change = 1;
		} else if (score == sp->hs_score) {
			(void)printf("%s tied %s %d high score.\n",
			    "\nYou", "your old level", level);
			sp->hs_time = now;	/* renew it */
			change = 1;		/* gotta rewrite, sigh */
		} /* else new score < old score: do nothing */
		break;
	}
	if (i >= nscores) {
		strlcpy(sp->hs_name, me, sizeof sp->hs_name);
		sp->hs_level = level;
		sp->hs_score = score;
		sp->hs_time = now;
		nscores++;
		change = 1;
	}

	if (change) {
		/*
		 * Sort & clean the scores, then rewrite.
		 */
		nscores = checkscores(scores, nscores);
		if (fseek(sf, 0L, SEEK_SET) == -1)
			err(1, "fseek");
		if (fwrite(scores, sizeof(*sp), nscores, sf) != nscores ||
		    fflush(sf) == EOF)
			warnx("error writing scorefile: %s\n\t-- %s",
			    strerror(errno),
			    "high scores may be damaged");
	}
	(void)fclose(sf);	/* releases lock */
}

/*
 * Get login name, or if that fails, get something suitable.
 * The result is always trimmed to fit in a score.
 */
static char *
thisuser(void)
{
	const char *p;
	static char u[sizeof(scores[0].hs_name)];

	if (u[0])
		return (u);
	p = getenv("LOGNAME");
	if (p == NULL || *p == '\0')
		p = getenv("USER");
	if (p == NULL || *p == '\0')
		p = getlogin();
	if (p == NULL || *p == '\0')
		p = "  ???";
	strlcpy(u, p, sizeof(u));
	return (u);
}

/*
 * Score comparison function for qsort.
 *
 * If two scores are equal, the person who had the score first is
 * listed first in the highscore file.
 */
static int
cmpscores(const void *x, const void *y)
{
	const struct highscore *a, *b;
	long l;

	a = x;
	b = y;
	l = (long)b->hs_level * b->hs_score - (long)a->hs_level * a->hs_score;
	if (l < 0)
		return (-1);
	if (l > 0)
		return (1);
	if (a->hs_time < b->hs_time)
		return (-1);
	if (a->hs_time > b->hs_time)
		return (1);
	return (0);
}

/*
 * If we've added a score to the file, we need to check the file and ensure
 * that this player has only a few entries.  The number of entries is
 * controlled by MAXSCORES, and is to ensure that the highscore file is not
 * monopolised by just a few people.  People who no longer have accounts are
 * only allowed the highest score.  Scores older than EXPIRATION seconds are
 * removed, unless they are someone's personal best.
 * Caveat:  the highest score on each level is always kept.
 */
static int
checkscores(struct highscore *hs, int num)
{
	struct highscore *sp;
	int i, j, k, nrnames;
	int levelfound[NLEVELS];
	struct peruser {
		char *name;
		int times;
	} count[NUMSPOTS];
	struct peruser *pu;

	/*
	 * Sort so that highest totals come first.
	 *
	 * levelfound[i] becomes set when the first high score for that
	 * level is encountered.  By definition this is the highest score.
	 */
	qsort((void *)hs, nscores, sizeof(*hs), cmpscores);
	for (i = MINLEVEL; i < NLEVELS; i++)
		levelfound[i] = 0;
	nrnames = 0;
	for (i = 0, sp = hs; i < num;) {
		/*
		 * This is O(n^2), but do you think we care?
		 */
		for (j = 0, pu = count; j < nrnames; j++, pu++)
			if (strcmp(sp->hs_name, pu->name) == 0)
				break;
		if (j == nrnames) {
			/*
			 * Add new user, set per-user count to 1.
			 */
			pu->name = sp->hs_name;
			pu->times = 1;
			nrnames++;
		} else {
			/*
			 * Two ways to keep this score:
			 * - Not too many (per user), still has acct, &
			 *	score not dated; or
			 * - High score on this level.
			 */
			if ((pu->times < MAXSCORES &&
			     sp->hs_time + EXPIRATION >= now) ||
			    levelfound[sp->hs_level] == 0)
				pu->times++;
			else {
				/*
				 * Delete this score, do not count it,
				 * do not pass go, do not collect $200.
				 */
				num--;
				for (k = i; k < num; k++)
					hs[k] = hs[k + 1];
				continue;
			}
		}
		levelfound[sp->hs_level] = 1;
		i++, sp++;
	}
	return (num > MAXHISCORES ? MAXHISCORES : num);
}

/*
 * Show current scores.  This must be called after savescore, if
 * savescore is called at all, for two reasons:
 * - Showscores munches the time field.
 * - Even if that were not the case, a new score must be recorded
 *   before it can be shown anyway.
 */
void
showscores(int level)
{
	struct highscore *sp;
	int i, n, c;
	const char *me;
	int levelfound[NLEVELS];

	if (!gotscores)
		getscores((FILE **)NULL);
	(void)printf("\n\t\t    Tetris High Scores\n");

	/*
	 * If level == 0, the person has not played a game but just asked for
	 * the high scores; we do not need to check for printing in highlight
	 * mode.  If SOstr is null, we can't do highlighting anyway.
	 */
	me = level && SOstr ? thisuser() : NULL;

	/*
	 * Set times to 0 except for high score on each level.
	 */
	for (i = MINLEVEL; i < NLEVELS; i++)
		levelfound[i] = 0;
	for (i = 0, sp = scores; i < nscores; i++, sp++) {
		if (levelfound[sp->hs_level])
			sp->hs_time = 0;
		else {
			sp->hs_time = 1;
			levelfound[sp->hs_level] = 1;
		}
	}

	/*
	 * Page each screenful of scores.
	 */
	for (i = 0, sp = scores; i < nscores; sp += n) {
		n = 20;
		if (i + n > nscores)
			n = nscores - i;
		printem(level, i + 1, sp, n, me);
		if ((i += n) < nscores) {
			(void)printf("\nHit RETURN to continue.");
			(void)fflush(stdout);
			while ((c = getchar()) != '\n')
				if (c == EOF)
					break;
			(void)printf("\n");
		}
	}

	if (nscores == 0)
		printf("\t\t\t      - none to date.\n");
}

static void
printem(int level, int offset, struct highscore *hs, int n, const char *me)
{
	struct highscore *sp;
	int row, highlight, i;
	char buf[100];
#define	TITLE "Rank  Score   Name                          (points/level)"
#define	TITL2 "=========================================================="

	printf("%s\n%s\n", TITLE, TITL2);

	highlight = 0;

	for (row = 0; row < n; row++) {
		sp = &hs[row];
		(void)snprintf(buf, sizeof(buf),
		    "%3d%c %6d  %-31s (%6d on %d)\n",
		    row + offset, sp->hs_time ? '*' : ' ',
		    sp->hs_score * sp->hs_level,
		    sp->hs_name, sp->hs_score, sp->hs_level);
		/* Print leaders every three lines */
		if ((row + 1) % 3 == 0) {
			for (i = 0; i < sizeof(buf); i++)
				if (buf[i] == ' ')
					buf[i] = '_';
		}
		/*
		 * Highlight if appropriate.  This works because
		 * we only get one score per level.
		 */
		if (me != NULL &&
		    sp->hs_level == level &&
		    sp->hs_score == score &&
		    strcmp(sp->hs_name, me) == 0) {
			putpad(SOstr);
			highlight = 1;
		}
		(void)printf("%s", buf);
		if (highlight) {
			putpad(SEstr);
			highlight = 0;
		}
	}
}
