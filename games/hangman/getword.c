/*	$OpenBSD: getword.c,v 1.11 2016/09/11 09:32:47 tb Exp $	*/
/*	$NetBSD: getword.c,v 1.4 1995/03/23 08:32:45 cgd Exp $	*/

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

#include <ctype.h>
#include <curses.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "hangman.h"
#include "pathnames.h"

/*
 * getword:
 *	Get a valid word out of the dictionary file
 */
void
getword(void)
{
	FILE		*inf;
	char		*wp, *gp;
	long		 pos;
	int		badwords, countwords;
	size_t		wordlen;

	inf = Dict;
	badwords = 0;
	/* Make sure the dictionary file is valid if it's not the default */
	countwords = strcmp(Dict_name, _PATH_DICT);
	while (badwords < MAXBADWORDS) {
		if (countwords)
			badwords++;
		pos = arc4random_uniform(Dict_size);
		fseek(inf, pos, SEEK_SET);
		if (fgets(Word, BUFSIZ, inf) == NULL)
			continue;
		if (fgets(Word, BUFSIZ, inf) == NULL)
			continue;
		wordlen = strlen(Word);
		if (wordlen > 0 && Word[wordlen - 1] == '\n')
			Word[wordlen - 1] = '\0';
		if (wordlen < MINLEN || wordlen > MAXLEN)
			continue;
		for (wp = Word; *wp; wp++)
			if (!islower((unsigned char)*wp))
				goto cont;
		break;
cont:		;
	}
	if (badwords >= MAXBADWORDS) {
		mvcur(0, COLS - 1, LINES - 1, 0);
		endwin();
		errx(1, "file %s appears to be incorrectly formatted\n"
		    "(Need one lower-case word per line)", Dict_name);
	}
	gp = Known;
	wp = Word;
	while (*wp) {
		*gp++ = '-';
		wp++;
	}
	*gp = '\0';
}
