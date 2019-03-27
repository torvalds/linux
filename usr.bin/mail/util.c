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
static char sccsid[] = "@(#)aux.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/time.h>

#include <fcntl.h>

#include "rcv.h"
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Auxiliary functions.
 */

static char *save2str(char *, char *);

/*
 * Return a pointer to a dynamic copy of the argument.
 */
char *
savestr(char *str)
{
	char *new;
	int size = strlen(str) + 1;

	if ((new = salloc(size)) != NULL)
		bcopy(str, new, size);
	return (new);
}

/*
 * Make a copy of new argument incorporating old one.
 */
static char *
save2str(char *str, char *old)
{
	char *new;
	int newsize = strlen(str) + 1;
	int oldsize = old ? strlen(old) + 1 : 0;

	if ((new = salloc(newsize + oldsize)) != NULL) {
		if (oldsize) {
			bcopy(old, new, oldsize);
			new[oldsize - 1] = ' ';
		}
		bcopy(str, new + oldsize, newsize);
	}
	return (new);
}

/*
 * Touch the named message by setting its MTOUCH flag.
 * Touched messages have the effect of not being sent
 * back to the system mailbox on exit.
 */
void
touch(struct message *mp)
{

	mp->m_flag |= MTOUCH;
	if ((mp->m_flag & MREAD) == 0)
		mp->m_flag |= MREAD|MSTATUS;
}

/*
 * Test to see if the passed file name is a directory.
 * Return true if it is.
 */
int
isdir(char name[])
{
	struct stat sbuf;

	if (stat(name, &sbuf) < 0)
		return (0);
	return (S_ISDIR(sbuf.st_mode));
}

/*
 * Count the number of arguments in the given string raw list.
 */
int
argcount(char **argv)
{
	char **ap;

	for (ap = argv; *ap++ != NULL;)
		;
	return (ap - argv - 1);
}

/*
 * Return the desired header line from the passed message
 * pointer (or NULL if the desired header field is not available).
 */
char *
hfield(const char *field, struct message *mp)
{
	FILE *ibuf;
	char linebuf[LINESIZE];
	int lc;
	char *hfield;
	char *colon, *oldhfield = NULL;

	ibuf = setinput(mp);
	if ((lc = mp->m_lines - 1) < 0)
		return (NULL);
	if (readline(ibuf, linebuf, LINESIZE) < 0)
		return (NULL);
	while (lc > 0) {
		if ((lc = gethfield(ibuf, linebuf, lc, &colon)) < 0)
			return (oldhfield);
		if ((hfield = ishfield(linebuf, colon, field)) != NULL)
			oldhfield = save2str(hfield, oldhfield);
	}
	return (oldhfield);
}

/*
 * Return the next header field found in the given message.
 * Return >= 0 if something found, < 0 elsewise.
 * "colon" is set to point to the colon in the header.
 * Must deal with \ continuations & other such fraud.
 */
int
gethfield(FILE *f, char linebuf[], int rem, char **colon)
{
	char line2[LINESIZE];
	char *cp, *cp2;
	int c;

	for (;;) {
		if (--rem < 0)
			return (-1);
		if ((c = readline(f, linebuf, LINESIZE)) <= 0)
			return (-1);
		for (cp = linebuf; isprint((unsigned char)*cp) && *cp != ' ' && *cp != ':';
		    cp++)
			;
		if (*cp != ':' || cp == linebuf)
			continue;
		/*
		 * I guess we got a headline.
		 * Handle wraparounding
		 */
		*colon = cp;
		cp = linebuf + c;
		for (;;) {
			while (--cp >= linebuf && (*cp == ' ' || *cp == '\t'))
				;
			cp++;
			if (rem <= 0)
				break;
			ungetc(c = getc(f), f);
			if (c != ' ' && c != '\t')
				break;
			if ((c = readline(f, line2, LINESIZE)) < 0)
				break;
			rem--;
			for (cp2 = line2; *cp2 == ' ' || *cp2 == '\t'; cp2++)
				;
			c -= cp2 - line2;
			if (cp + c >= linebuf + LINESIZE - 2)
				break;
			*cp++ = ' ';
			bcopy(cp2, cp, c);
			cp += c;
		}
		*cp = 0;
		return (rem);
	}
	/* NOTREACHED */
}

/*
 * Check whether the passed line is a header line of
 * the desired breed.  Return the field body, or 0.
 */

char*
ishfield(char *linebuf, char *colon, const char *field)
{
	char *cp = colon;

	*cp = 0;
	if (strcasecmp(linebuf, field) != 0) {
		*cp = ':';
		return (0);
	}
	*cp = ':';
	for (cp++; *cp == ' ' || *cp == '\t'; cp++)
		;
	return (cp);
}

/*
 * Copy a string and lowercase the result.
 * dsize: space left in buffer (including space for NULL)
 */
void
istrncpy(char *dest, const char *src, size_t dsize)
{

	strlcpy(dest, src, dsize);
	for (; *dest; dest++)
		*dest = tolower((unsigned char)*dest);
}

/*
 * The following code deals with input stacking to do source
 * commands.  All but the current file pointer are saved on
 * the stack.
 */

static	int	ssp;			/* Top of file stack */
struct sstack {
	FILE	*s_file;		/* File we were in. */
	int	s_cond;			/* Saved state of conditionals */
	int	s_loading;		/* Loading .mailrc, etc. */
};
#define	SSTACK_SIZE	64		/* XXX was NOFILE. */
static struct sstack sstack[SSTACK_SIZE];

/*
 * Pushdown current input file and switch to a new one.
 * Set the global flag "sourcing" so that others will realize
 * that they are no longer reading from a tty (in all probability).
 */
int
source(char **arglist)
{
	FILE *fi;
	char *cp;

	if ((cp = expand(*arglist)) == NULL)
		return (1);
	if ((fi = Fopen(cp, "r")) == NULL) {
		warn("%s", cp);
		return (1);
	}
	if (ssp >= SSTACK_SIZE - 1) {
		printf("Too much \"sourcing\" going on.\n");
		(void)Fclose(fi);
		return (1);
	}
	sstack[ssp].s_file = input;
	sstack[ssp].s_cond = cond;
	sstack[ssp].s_loading = loading;
	ssp++;
	loading = 0;
	cond = CANY;
	input = fi;
	sourcing++;
	return (0);
}

/*
 * Pop the current input back to the previous level.
 * Update the "sourcing" flag as appropriate.
 */
int
unstack(void)
{
	if (ssp <= 0) {
		printf("\"Source\" stack over-pop.\n");
		sourcing = 0;
		return (1);
	}
	(void)Fclose(input);
	if (cond != CANY)
		printf("Unmatched \"if\"\n");
	ssp--;
	cond = sstack[ssp].s_cond;
	loading = sstack[ssp].s_loading;
	input = sstack[ssp].s_file;
	if (ssp == 0)
		sourcing = loading;
	return (0);
}

/*
 * Touch the indicated file.
 * This is nifty for the shell.
 */
void
alter(char *name)
{
	struct timespec ts[2];

	(void)clock_gettime(CLOCK_REALTIME, &ts[0]);
	ts[0].tv_sec++;
	ts[1].tv_sec = 0;
	ts[1].tv_nsec = UTIME_OMIT;
	(void)utimensat(AT_FDCWD, name, ts, 0);
}

/*
 * Get sender's name from this message.  If the message has
 * a bunch of arpanet stuff in it, we may have to skin the name
 * before returning it.
 */
char *
nameof(struct message *mp, int reptype)
{
	char *cp, *cp2;

	cp = skin(name1(mp, reptype));
	if (reptype != 0 || charcount(cp, '!') < 2)
		return (cp);
	cp2 = strrchr(cp, '!');
	cp2--;
	while (cp2 > cp && *cp2 != '!')
		cp2--;
	if (*cp2 == '!')
		return (cp2 + 1);
	return (cp);
}

/*
 * Start of a "comment".
 * Ignore it.
 */
char *
skip_comment(char *cp)
{
	int nesting = 1;

	for (; nesting > 0 && *cp; cp++) {
		switch (*cp) {
		case '\\':
			if (cp[1])
				cp++;
			break;
		case '(':
			nesting++;
			break;
		case ')':
			nesting--;
			break;
		}
	}
	return (cp);
}

/*
 * Skin an arpa net address according to the RFC 822 interpretation
 * of "host-phrase."
 */
char *
skin(char *name)
{
	char *nbuf, *bufend, *cp, *cp2;
	int c, gotlt, lastsp;

	if (name == NULL)
		return (NULL);
	if (strchr(name, '(') == NULL && strchr(name, '<') == NULL
	    && strchr(name, ' ') == NULL)
		return (name);

	/* We assume that length(input) <= length(output) */
	if ((nbuf = malloc(strlen(name) + 1)) == NULL)
		err(1, "Out of memory");
	gotlt = 0;
	lastsp = 0;
	bufend = nbuf;
	for (cp = name, cp2 = bufend; (c = *cp++) != '\0'; ) {
		switch (c) {
		case '(':
			cp = skip_comment(cp);
			lastsp = 0;
			break;

		case '"':
			/*
			 * Start of a "quoted-string".
			 * Copy it in its entirety.
			 */
			while ((c = *cp) != '\0') {
				cp++;
				if (c == '"')
					break;
				if (c != '\\')
					*cp2++ = c;
				else if ((c = *cp) != '\0') {
					*cp2++ = c;
					cp++;
				}
			}
			lastsp = 0;
			break;

		case ' ':
			if (cp[0] == 'a' && cp[1] == 't' && cp[2] == ' ')
				cp += 3, *cp2++ = '@';
			else
			if (cp[0] == '@' && cp[1] == ' ')
				cp += 2, *cp2++ = '@';
			else
				lastsp = 1;
			break;

		case '<':
			cp2 = bufend;
			gotlt++;
			lastsp = 0;
			break;

		case '>':
			if (gotlt) {
				gotlt = 0;
				while ((c = *cp) != '\0' && c != ',') {
					cp++;
					if (c == '(')
						cp = skip_comment(cp);
					else if (c == '"')
						while ((c = *cp) != '\0') {
							cp++;
							if (c == '"')
								break;
							if (c == '\\' && *cp != '\0')
								cp++;
						}
				}
				lastsp = 0;
				break;
			}
			/* FALLTHROUGH */

		default:
			if (lastsp) {
				lastsp = 0;
				*cp2++ = ' ';
			}
			*cp2++ = c;
			if (c == ',' && !gotlt &&
			    (*cp == ' ' || *cp == '"' || *cp == '<')) {
				*cp2++ = ' ';
				while (*cp == ' ')
					cp++;
				lastsp = 0;
				bufend = cp2;
			}
		}
	}
	*cp2 = '\0';

	if ((cp = realloc(nbuf, strlen(nbuf) + 1)) != NULL)
		nbuf = cp;
	return (nbuf);
}

/*
 * Fetch the sender's name from the passed message.
 * Reptype can be
 *	0 -- get sender's name for display purposes
 *	1 -- get sender's name for reply
 *	2 -- get sender's name for Reply
 */
char *
name1(struct message *mp, int reptype)
{
	char namebuf[LINESIZE];
	char linebuf[LINESIZE];
	char *cp, *cp2;
	FILE *ibuf;
	int first = 1;

	if ((cp = hfield("from", mp)) != NULL)
		return (cp);
	if (reptype == 0 && (cp = hfield("sender", mp)) != NULL)
		return (cp);
	ibuf = setinput(mp);
	namebuf[0] = '\0';
	if (readline(ibuf, linebuf, LINESIZE) < 0)
		return (savestr(namebuf));
newname:
	for (cp = linebuf; *cp != '\0' && *cp != ' '; cp++)
		;
	for (; *cp == ' ' || *cp == '\t'; cp++)
		;
	for (cp2 = &namebuf[strlen(namebuf)];
	    *cp != '\0' && *cp != ' ' && *cp != '\t' &&
	    cp2 < namebuf + LINESIZE - 1;)
		*cp2++ = *cp++;
	*cp2 = '\0';
	if (readline(ibuf, linebuf, LINESIZE) < 0)
		return (savestr(namebuf));
	if ((cp = strchr(linebuf, 'F')) == NULL)
		return (savestr(namebuf));
	if (strncmp(cp, "From", 4) != 0)
		return (savestr(namebuf));
	while ((cp = strchr(cp, 'r')) != NULL) {
		if (strncmp(cp, "remote", 6) == 0) {
			if ((cp = strchr(cp, 'f')) == NULL)
				break;
			if (strncmp(cp, "from", 4) != 0)
				break;
			if ((cp = strchr(cp, ' ')) == NULL)
				break;
			cp++;
			if (first) {
				cp2 = namebuf;
				first = 0;
			} else
				cp2 = strrchr(namebuf, '!') + 1;
			strlcpy(cp2, cp, sizeof(namebuf) - (cp2 - namebuf) - 1);
			strcat(namebuf, "!");
			goto newname;
		}
		cp++;
	}
	return (savestr(namebuf));
}

/*
 * Count the occurrences of c in str
 */
int
charcount(char *str, int c)
{
	char *cp;
	int i;

	for (i = 0, cp = str; *cp != '\0'; cp++)
		if (*cp == c)
			i++;
	return (i);
}

/*
 * See if the given header field is supposed to be ignored.
 */
int
isign(const char *field, struct ignoretab ignore[2])
{
	char realfld[LINESIZE];

	if (ignore == ignoreall)
		return (1);
	/*
	 * Lower-case the string, so that "Status" and "status"
	 * will hash to the same place.
	 */
	istrncpy(realfld, field, sizeof(realfld));
	if (ignore[1].i_count > 0)
		return (!member(realfld, ignore + 1));
	else
		return (member(realfld, ignore));
}

int
member(char *realfield, struct ignoretab *table)
{
	struct ignore *igp;

	for (igp = table->i_head[hash(realfield)]; igp != NULL; igp = igp->i_link)
		if (*igp->i_field == *realfield &&
		    equal(igp->i_field, realfield))
			return (1);
	return (0);
}
