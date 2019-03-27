/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)head.c	8.2 (Berkeley) 4/20/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Routines for processing and detecting headlines.
 */

/*
 * See if the passed line buffer is a mail header.
 * Return true if yes.  Note the extreme pains to
 * accommodate all funny formats.
 */
int
ishead(char linebuf[])
{
	struct headline hl;
	char parbuf[BUFSIZ];

	if (strncmp(linebuf, "From ", 5) != 0)
		return (0);
	parse(linebuf, &hl, parbuf);
	if (hl.l_date == NULL) {
		fail(linebuf, "No date field");
		return (0);
	}
	if (!isdate(hl.l_date)) {
		fail(linebuf, "Date field not legal date");
		return (0);
	}
	/*
	 * I guess we got it!
	 */
	return (1);
}

void
fail(const char *linebuf __unused, const char *reason __unused)
{

	/*
	if (value("debug") == NULL)
		return;
	fprintf(stderr, "\"%s\"\nnot a header because %s\n", linebuf, reason);
	*/
}

/*
 * Split a headline into its useful components.
 * Copy the line into dynamic string space, then set
 * pointers into the copied line in the passed headline
 * structure.  Actually, it scans.
 */
void
parse(char line[], struct headline *hl, char pbuf[])
{
	char *cp, *sp;
	char word[LINESIZE];

	hl->l_from = NULL;
	hl->l_tty = NULL;
	hl->l_date = NULL;
	cp = line;
	sp = pbuf;
	/*
	 * Skip over "From" first.
	 */
	cp = nextword(cp, word);
	/*
	 * Check for missing return-path.
	 */
	if (isdate(cp)) {
		hl->l_date = copyin(cp, &sp);
		return;
	}
	cp = nextword(cp, word);
	if (strlen(word) > 0)
		hl->l_from = copyin(word, &sp);
	if (cp != NULL && strncmp(cp, "tty", 3) == 0) {
		cp = nextword(cp, word);
		hl->l_tty = copyin(word, &sp);
	}
	if (cp != NULL)
		hl->l_date = copyin(cp, &sp);
}

/*
 * Copy the string on the left into the string on the right
 * and bump the right (reference) string pointer by the length.
 * Thus, dynamically allocate space in the right string, copying
 * the left string into it.
 */
char *
copyin(char *src, char **space)
{
	char *cp, *top;

	top = cp = *space;
	while ((*cp++ = *src++) != '\0')
		;
	*space = cp;
	return (top);
}

/*
 * Test to see if the passed string is a ctime(3) generated
 * date string as documented in the manual.  The template
 * below is used as the criterion of correctness.
 * Also, we check for a possible trailing time zone using
 * the tmztype template.
 *
 * If the mail file is created by Sys V (Solaris), there are
 * no seconds in the time. If the mail is created by another
 * program such as imapd, it might have timezone as
 * <-|+>nnnn (-0800 for instance) at the end.
 */

/*
 * 'A'	An upper case char
 * 'a'	A lower case char
 * ' '	A space
 * '0'	A digit
 * 'O'	A digit or space
 * 'p'	A punctuation char
 * 'P'	A punctuation char or space
 * ':'	A colon
 * 'N'	A new line
 */

static char *date_formats[] = {
	"Aaa Aaa O0 00:00:00 0000",	   /* Mon Jan 01 23:59:59 2001 */
	"Aaa Aaa O0 00:00:00 AAA 0000",	   /* Mon Jan 01 23:59:59 PST 2001 */
	"Aaa Aaa O0 00:00:00 0000 p0000",  /* Mon Jan 01 23:59:59 2001 -0800 */
	"Aaa Aaa O0 00:00 0000",	   /* Mon Jan 01 23:59 2001 */
	"Aaa Aaa O0 00:00 AAA 0000",	   /* Mon Jan 01 23:59 PST 2001 */
	"Aaa Aaa O0 00:00 0000 p0000",	   /* Mon Jan 01 23:59 2001 -0800 */
	NULL
};

int
isdate(char date[])
{
	int i;

	for(i = 0; date_formats[i] != NULL; i++) {
		if (cmatch(date, date_formats[i]))
			return (1);
	}
	return (0);
}

/*
 * Match the given string (cp) against the given template (tp).
 * Return 1 if they match, 0 if they don't
 */
int
cmatch(char *cp, char *tp)
{

	while (*cp != '\0' && *tp != '\0')
		switch (*tp++) {
		case 'a':
			if (!islower((unsigned char)*cp++))
				return (0);
			break;
		case 'A':
			if (!isupper((unsigned char)*cp++))
				return (0);
			break;
		case ' ':
			if (*cp++ != ' ')
				return (0);
			break;
		case '0':
			if (!isdigit((unsigned char)*cp++))
				return (0);
			break;
		case 'O':
			if (*cp != ' ' && !isdigit((unsigned char)*cp))
				return (0);
			cp++;
			break;
		case 'p':
			if (!ispunct((unsigned char)*cp++))
				return (0);
			break;
		case 'P':
			if (*cp != ' ' && !ispunct((unsigned char)*cp))
				return (0);
			cp++;
			break;
		case ':':
			if (*cp++ != ':')
				return (0);
			break;
		case 'N':
			if (*cp++ != '\n')
				return (0);
			break;
		}
	if (*cp != '\0' || *tp != '\0')
		return (0);
	return (1);
}

/*
 * Collect a liberal (space, tab delimited) word into the word buffer
 * passed.  Also, return a pointer to the next word following that,
 * or NULL if none follow.
 */
char *
nextword(char *wp, char *wbuf)
{
	int c;

	if (wp == NULL) {
		*wbuf = '\0';
		return (NULL);
	}
	while ((c = *wp++) != '\0' && c != ' ' && c != '\t') {
		*wbuf++ = c;
		if (c == '"') {
 			while ((c = *wp++) != '\0' && c != '"')
 				*wbuf++ = c;
 			if (c == '"')
 				*wbuf++ = c;
			else
				wp--;
 		}
	}
	*wbuf = '\0';
	for (; c == ' ' || c == '\t'; c = *wp++)
		;
	if (c == '\0')
		return (NULL);
	return (wp - 1);
}
