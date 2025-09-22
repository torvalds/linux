/*	$OpenBSD: cfscores.c,v 1.24 2018/08/24 11:31:17 mestre Exp $	*/
/*	$NetBSD: cfscores.c,v 1.3 1995/03/21 15:08:37 cgd Exp $	*/

/*
 * Copyright (c) 1983, 1993
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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct betinfo {
	long	hand;		/* cost of dealing hand */
	long	inspection;	/* cost of inspecting hand */
	long	game;		/* cost of buying game */
	long	runs;		/* cost of running through hands */
	long	information;	/* cost of information */
	long	thinktime;	/* cost of thinking time */
	long	wins;		/* total winnings */
	long	worth;		/* net worth after costs */
};

int dbfd;
char scorepath[PATH_MAX];

void	printuser(void);

int
main(int argc, char *argv[])
{
	const char *home;
	int ret;

	if (pledge("stdio rpath", NULL) == -1)
		err(1, "pledge");
	
	home = getenv("HOME");
	if (home == NULL || *home == '\0')
		err(1, "getenv");

	ret = snprintf(scorepath, sizeof(scorepath), "%s/%s", home,
	    ".cfscores");
	if (ret < 0 || ret >= PATH_MAX)
		errc(1, ENAMETOOLONG, "%s/%s", home, ".cfscores");

	dbfd = open(scorepath, O_RDONLY);
	if (dbfd < 0)
		err(2, "%s", scorepath);

	printuser();
	return 0;
}

void
printuser(void)
{
	struct betinfo total;
	const char *name;
	int i;

	name = getlogin();
	if (name == NULL || *name == '\0')
		name = " ??? ";

	i = read(dbfd, (char *)&total, sizeof(total));
	if (i < 0) {
		warn("lseek %s", scorepath);
		return;
	}
	if (i == 0 || total.hand == 0) {
		printf("%s has never played canfield.\n", name);
		return;
	}
	i = strlen(name);
	printf("*----------------------*\n");
	if (total.worth >= 0) {
		if (i <= 8)
			printf("* Winnings for %-8s*\n", name);
		else {
			printf("*     Winnings for     *\n");
			if (i <= 20)
				printf("* %20s *\n", name);
			else
				printf("%s\n", name);
		}
	} else {
		if (i <= 10)
			printf("* Losses for %-10s*\n", name);
		else {
			printf("*      Losses for      *\n");
			if (i <= 20)
				printf("* %20s *\n", name);
			else
				printf("%s\n", name);
		}
	}
	printf("*======================*\n");
	printf("|Costs           Total |\n");
	printf("| Hands       %8ld |\n", total.hand);
	printf("| Inspections %8ld |\n", total.inspection);
	printf("| Games       %8ld |\n", total.game);
	printf("| Runs        %8ld |\n", total.runs);
	printf("| Information %8ld |\n", total.information);
	printf("| Think time  %8ld |\n", total.thinktime);
	printf("|Total Costs  %8ld |\n", total.wins - total.worth);
	printf("|Winnings     %8ld |\n", total.wins);
	printf("|Net Worth    %8ld |\n", total.worth);
	printf("*----------------------*\n\n");
}
