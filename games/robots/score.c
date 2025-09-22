/*	$OpenBSD: score.c,v 1.17 2023/10/10 09:48:06 tb Exp $	*/
/*	$NetBSD: score.c,v 1.3 1995/04/22 10:09:12 cgd Exp $	*/

/*
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

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "robots.h"

char	Scorefile[PATH_MAX];

#ifndef MAX_PER_UID
#define MAX_PER_UID	5
#endif

int	Max_per_uid = MAX_PER_UID;

static SCORE	Top[MAXSCORES];

/*
 * score:
 *	Post the player's score, if reasonable, and then print out the
 *	top list.
 */
void
score(int score_wfd)
{
	int	inf = score_wfd;
	SCORE	*scp;
	uid_t	uid;
	bool	done_show = FALSE;
	static int	numscores, max_uid;

	Newscore = FALSE;
	if (inf < 0)
		return;

	if (read(inf, &max_uid, sizeof max_uid) == sizeof max_uid)
		read(inf, Top, sizeof Top);
	else {
		for (scp = Top; scp < &Top[MAXSCORES]; scp++)
			scp->s_score = -1;
		max_uid = Max_per_uid;
	}

	uid = getuid();
	if (Top[MAXSCORES-1].s_score <= Score) {
		numscores = 0;
		for (scp = Top; scp < &Top[MAXSCORES]; scp++)
			if (scp->s_score < 0 ||
			    (scp->s_uid == uid && ++numscores == max_uid)) {
				if (scp->s_score > Score)
					break;
				scp->s_score = Score;
				scp->s_uid = uid;
				set_name(scp);
				Newscore = TRUE;
				break;
			}
		if (scp == &Top[MAXSCORES]) {
			Top[MAXSCORES-1].s_score = Score;
			Top[MAXSCORES-1].s_uid = uid;
			set_name(&Top[MAXSCORES-1]);
			Newscore = TRUE;
		}
		if (Newscore)
			qsort(Top, MAXSCORES, sizeof Top[0], cmp_sc);
	}

	if (!Newscore) {
		Full_clear = FALSE;
		fsync(inf);
		lseek(inf, 0, SEEK_SET);
		return;
	}
	else
		Full_clear = TRUE;

	for (scp = Top; scp < &Top[MAXSCORES]; scp++) {
		if (scp->s_score < 0)
			break;
		move((scp - Top) + 1, 6);
		if (!done_show && scp->s_uid == uid && scp->s_score == Score)
			standout();
		printw(" %td\t%d\t%-*s ", (scp - Top) + 1, scp->s_score,
			(int)(sizeof scp->s_name), scp->s_name);
		if (!done_show && scp->s_uid == uid && scp->s_score == Score) {
			standend();
			done_show = TRUE;
		}
	}
	Num_scores = scp - Top;
	refresh();

	if (Newscore) {
		lseek(inf, 0L, SEEK_SET);
		write(inf, &max_uid, sizeof max_uid);
		write(inf, Top, sizeof Top);
	}
	fsync(inf);
	lseek(inf, 0, SEEK_SET);
}

void
set_name(SCORE *scp)
{
	const char	*name;

	name = getenv("LOGNAME");
	if (name == NULL || *name == '\0')
		name = getenv("USER");
	if (name == NULL || *name == '\0')
		name = getlogin();
	if (name == NULL || *name == '\0')
		name = "  ???";

	strlcpy(scp->s_name, name, LOGIN_NAME_MAX);
}

/*
 * cmp_sc:
 *	Compare two scores.
 */
int
cmp_sc(const void *s1, const void *s2)
{
	return ((SCORE *)s2)->s_score - ((SCORE *)s1)->s_score;
}

/*
 * show_score:
 *	Show the score list for the '-s' option.
 */
void
show_score(void)
{
	SCORE	*scp;
	int	inf;
	static int	max_score;

	if ((inf = open(Scorefile, O_RDONLY)) == -1) {
		perror(Scorefile);
		return;
	}

	for (scp = Top; scp < &Top[MAXSCORES]; scp++)
		scp->s_score = -1;

	read(inf, &max_score, sizeof max_score);
	read(inf, Top, sizeof Top);
	close(inf);
	inf = 1;
	for (scp = Top; scp < &Top[MAXSCORES]; scp++)
		if (scp->s_score >= 0)
			printf("%d\t%d\t%.*s\n", inf++, scp->s_score,
				(int)(sizeof scp->s_name), scp->s_name);
}
