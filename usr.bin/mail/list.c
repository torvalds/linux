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
static char sccsid[] = "@(#)list.c	8.4 (Berkeley) 5/1/95";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "rcv.h"
#include <ctype.h>
#include "extern.h"

/*
 * Mail -- a mail program
 *
 * Message list handling.
 */

/*
 * Convert the user string of message numbers and
 * store the numbers into vector.
 *
 * Returns the count of messages picked up or -1 on error.
 */
int
getmsglist(char *buf, int *vector, int flags)
{
	int *ip;
	struct message *mp;

	if (msgCount == 0) {
		*vector = 0;
		return (0);
	}
	if (markall(buf, flags) < 0)
		return (-1);
	ip = vector;
	for (mp = &message[0]; mp < &message[msgCount]; mp++)
		if (mp->m_flag & MMARK)
			*ip++ = mp - &message[0] + 1;
	*ip = 0;
	return (ip - vector);
}

/*
 * Mark all messages that the user wanted from the command
 * line in the message structure.  Return 0 on success, -1
 * on error.
 */

/*
 * Bit values for colon modifiers.
 */

#define	CMNEW		01		/* New messages */
#define	CMOLD		02		/* Old messages */
#define	CMUNREAD	04		/* Unread messages */
#define	CMDELETED	010		/* Deleted messages */
#define	CMREAD		020		/* Read messages */

/*
 * The following table describes the letters which can follow
 * the colon and gives the corresponding modifier bit.
 */

static struct coltab {
	char	co_char;		/* What to find past : */
	int	co_bit;			/* Associated modifier bit */
	int	co_mask;		/* m_status bits to mask */
	int	co_equal;		/* ... must equal this */
} coltab[] = {
	{ 'n',		CMNEW,		MNEW,		MNEW	},
	{ 'o',		CMOLD,		MNEW,		0	},
	{ 'u',		CMUNREAD,	MREAD,		0	},
	{ 'd',		CMDELETED,	MDELETED,	MDELETED},
	{ 'r',		CMREAD,		MREAD,		MREAD	},
	{ 0,		0,		0,		0	}
};

static	int	lastcolmod;

int
markall(char buf[], int f)
{
	char **np;
	int i;
	struct message *mp;
	char *namelist[NMLSIZE], *bufp;
	int tok, beg, mc, star, other, valdot, colmod, colresult;

	valdot = dot - &message[0] + 1;
	colmod = 0;
	for (i = 1; i <= msgCount; i++)
		unmark(i);
	bufp = buf;
	mc = 0;
	np = &namelist[0];
	scaninit();
	tok = scan(&bufp);
	star = 0;
	other = 0;
	beg = 0;
	while (tok != TEOL) {
		switch (tok) {
		case TNUMBER:
number:
			if (star) {
				printf("No numbers mixed with *\n");
				return (-1);
			}
			mc++;
			other++;
			if (beg != 0) {
				if (check(lexnumber, f))
					return (-1);
				for (i = beg; i <= lexnumber; i++)
					if (f == MDELETED || (message[i - 1].m_flag & MDELETED) == 0)
						mark(i);
				beg = 0;
				break;
			}
			beg = lexnumber;
			if (check(beg, f))
				return (-1);
			tok = scan(&bufp);
			regret(tok);
			if (tok != TDASH) {
				mark(beg);
				beg = 0;
			}
			break;

		case TPLUS:
			if (beg != 0) {
				printf("Non-numeric second argument\n");
				return (-1);
			}
			i = valdot;
			do {
				i++;
				if (i > msgCount) {
					printf("Referencing beyond EOF\n");
					return (-1);
				}
			} while ((message[i - 1].m_flag & MDELETED) != f);
			mark(i);
			break;

		case TDASH:
			if (beg == 0) {
				i = valdot;
				do {
					i--;
					if (i <= 0) {
						printf("Referencing before 1\n");
						return (-1);
					}
				} while ((message[i - 1].m_flag & MDELETED) != f);
				mark(i);
			}
			break;

		case TSTRING:
			if (beg != 0) {
				printf("Non-numeric second argument\n");
				return (-1);
			}
			other++;
			if (lexstring[0] == ':') {
				colresult = evalcol(lexstring[1]);
				if (colresult == 0) {
					printf("Unknown colon modifier \"%s\"\n",
					    lexstring);
					return (-1);
				}
				colmod |= colresult;
			}
			else
				*np++ = savestr(lexstring);
			break;

		case TDOLLAR:
		case TUP:
		case TDOT:
			lexnumber = metamess(lexstring[0], f);
			if (lexnumber == -1)
				return (-1);
			goto number;

		case TSTAR:
			if (other) {
				printf("Can't mix \"*\" with anything\n");
				return (-1);
			}
			star++;
			break;

		case TERROR:
			return (-1);
		}
		tok = scan(&bufp);
	}
	lastcolmod = colmod;
	*np = NULL;
	mc = 0;
	if (star) {
		for (i = 0; i < msgCount; i++)
			if ((message[i].m_flag & MDELETED) == f) {
				mark(i+1);
				mc++;
			}
		if (mc == 0) {
			printf("No applicable messages.\n");
			return (-1);
		}
		return (0);
	}

	/*
	 * If no numbers were given, mark all of the messages,
	 * so that we can unmark any whose sender was not selected
	 * if any user names were given.
	 */

	if ((np > namelist || colmod != 0) && mc == 0)
		for (i = 1; i <= msgCount; i++)
			if ((message[i-1].m_flag & MDELETED) == f)
				mark(i);

	/*
	 * If any names were given, go through and eliminate any
	 * messages whose senders were not requested.
	 */

	if (np > namelist) {
		for (i = 1; i <= msgCount; i++) {
			for (mc = 0, np = &namelist[0]; *np != NULL; np++)
				if (**np == '/') {
					if (matchfield(*np, i)) {
						mc++;
						break;
					}
				}
				else {
					if (matchsender(*np, i)) {
						mc++;
						break;
					}
				}
			if (mc == 0)
				unmark(i);
		}

		/*
		 * Make sure we got some decent messages.
		 */

		mc = 0;
		for (i = 1; i <= msgCount; i++)
			if (message[i-1].m_flag & MMARK) {
				mc++;
				break;
			}
		if (mc == 0) {
			printf("No applicable messages from {%s",
				namelist[0]);
			for (np = &namelist[1]; *np != NULL; np++)
				printf(", %s", *np);
			printf("}\n");
			return (-1);
		}
	}

	/*
	 * If any colon modifiers were given, go through and
	 * unmark any messages which do not satisfy the modifiers.
	 */

	if (colmod != 0) {
		for (i = 1; i <= msgCount; i++) {
			struct coltab *colp;

			mp = &message[i - 1];
			for (colp = &coltab[0]; colp->co_char != '\0'; colp++)
				if (colp->co_bit & colmod)
					if ((mp->m_flag & colp->co_mask)
					    != colp->co_equal)
						unmark(i);

		}
		for (mp = &message[0]; mp < &message[msgCount]; mp++)
			if (mp->m_flag & MMARK)
				break;
		if (mp >= &message[msgCount]) {
			struct coltab *colp;

			printf("No messages satisfy");
			for (colp = &coltab[0]; colp->co_char != '\0'; colp++)
				if (colp->co_bit & colmod)
					printf(" :%c", colp->co_char);
			printf("\n");
			return (-1);
		}
	}
	return (0);
}

/*
 * Turn the character after a colon modifier into a bit
 * value.
 */
int
evalcol(int col)
{
	struct coltab *colp;

	if (col == 0)
		return (lastcolmod);
	for (colp = &coltab[0]; colp->co_char != '\0'; colp++)
		if (colp->co_char == col)
			return (colp->co_bit);
	return (0);
}

/*
 * Check the passed message number for legality and proper flags.
 * If f is MDELETED, then either kind will do.  Otherwise, the message
 * has to be undeleted.
 */
int
check(int mesg, int f)
{
	struct message *mp;

	if (mesg < 1 || mesg > msgCount) {
		printf("%d: Invalid message number\n", mesg);
		return (-1);
	}
	mp = &message[mesg-1];
	if (f != MDELETED && (mp->m_flag & MDELETED) != 0) {
		printf("%d: Inappropriate message\n", mesg);
		return (-1);
	}
	return (0);
}

/*
 * Scan out the list of string arguments, shell style
 * for a RAWLIST.
 */
int
getrawlist(char line[], char **argv, int argc)
{
	char c, *cp, *cp2, quotec;
	int argn;
	char *linebuf;
	size_t linebufsize = BUFSIZ;

	if ((linebuf = malloc(linebufsize)) == NULL)
		err(1, "Out of memory");

	argn = 0;
	cp = line;
	for (;;) {
		for (; *cp == ' ' || *cp == '\t'; cp++)
			;
		if (*cp == '\0')
			break;
		if (argn >= argc - 1) {
			printf(
			"Too many elements in the list; excess discarded.\n");
			break;
		}
		cp2 = linebuf;
		quotec = '\0';
		while ((c = *cp) != '\0') {
			/* Allocate more space if necessary */
			if (cp2 - linebuf == linebufsize - 1) {
				linebufsize += BUFSIZ;
				if ((linebuf = realloc(linebuf, linebufsize)) == NULL)
					err(1, "Out of memory");
				cp2 = linebuf + linebufsize - BUFSIZ - 1;
			}
			cp++;
			if (quotec != '\0') {
				if (c == quotec)
					quotec = '\0';
				else if (c == '\\')
					switch (c = *cp++) {
					case '\0':
						*cp2++ = '\\';
						cp--;
						break;
					case '0': case '1': case '2': case '3':
					case '4': case '5': case '6': case '7':
						c -= '0';
						if (*cp >= '0' && *cp <= '7')
							c = c * 8 + *cp++ - '0';
						if (*cp >= '0' && *cp <= '7')
							c = c * 8 + *cp++ - '0';
						*cp2++ = c;
						break;
					case 'b':
						*cp2++ = '\b';
						break;
					case 'f':
						*cp2++ = '\f';
						break;
					case 'n':
						*cp2++ = '\n';
						break;
					case 'r':
						*cp2++ = '\r';
						break;
					case 't':
						*cp2++ = '\t';
						break;
					case 'v':
						*cp2++ = '\v';
						break;
					default:
						*cp2++ = c;
					}
				else if (c == '^') {
					c = *cp++;
					if (c == '?')
						*cp2++ = '\177';
					/* null doesn't show up anyway */
					else if ((c >= 'A' && c <= '_') ||
					    (c >= 'a' && c <= 'z'))
						*cp2++ = c & 037;
					else {
						*cp2++ = '^';
						cp--;
					}
				} else
					*cp2++ = c;
			} else if (c == '"' || c == '\'')
				quotec = c;
			else if (c == ' ' || c == '\t')
				break;
			else
				*cp2++ = c;
		}
		*cp2 = '\0';
		argv[argn++] = savestr(linebuf);
	}
	argv[argn] = NULL;
	(void)free(linebuf);
	return (argn);
}

/*
 * scan out a single lexical item and return its token number,
 * updating the string pointer passed **p.  Also, store the value
 * of the number or string scanned in lexnumber or lexstring as
 * appropriate.  In any event, store the scanned `thing' in lexstring.
 */

static struct lex {
	char	l_char;
	char	l_token;
} singles[] = {
	{ '$',	TDOLLAR	},
	{ '.',	TDOT	},
	{ '^',	TUP 	},
	{ '*',	TSTAR 	},
	{ '-',	TDASH 	},
	{ '+',	TPLUS 	},
	{ '(',	TOPEN 	},
	{ ')',	TCLOSE 	},
	{ 0,	0 	}
};

int
scan(char **sp)
{
	char *cp, *cp2;
	int c;
	struct lex *lp;
	int quotec;

	if (regretp >= 0) {
		strcpy(lexstring, string_stack[regretp]);
		lexnumber = numberstack[regretp];
		return (regretstack[regretp--]);
	}
	cp = *sp;
	cp2 = lexstring;
	c = *cp++;

	/*
	 * strip away leading white space.
	 */

	while (c == ' ' || c == '\t')
		c = *cp++;

	/*
	 * If no characters remain, we are at end of line,
	 * so report that.
	 */

	if (c == '\0') {
		*sp = --cp;
		return (TEOL);
	}

	/*
	 * If the leading character is a digit, scan
	 * the number and convert it on the fly.
	 * Return TNUMBER when done.
	 */

	if (isdigit((unsigned char)c)) {
		lexnumber = 0;
		while (isdigit((unsigned char)c)) {
			lexnumber = lexnumber*10 + c - '0';
			*cp2++ = c;
			c = *cp++;
		}
		*cp2 = '\0';
		*sp = --cp;
		return (TNUMBER);
	}

	/*
	 * Check for single character tokens; return such
	 * if found.
	 */

	for (lp = &singles[0]; lp->l_char != '\0'; lp++)
		if (c == lp->l_char) {
			lexstring[0] = c;
			lexstring[1] = '\0';
			*sp = cp;
			return (lp->l_token);
		}

	/*
	 * We've got a string!  Copy all the characters
	 * of the string into lexstring, until we see
	 * a null, space, or tab.
	 * If the lead character is a " or ', save it
	 * and scan until you get another.
	 */

	quotec = 0;
	if (c == '\'' || c == '"') {
		quotec = c;
		c = *cp++;
	}
	while (c != '\0') {
		if (c == quotec) {
			cp++;
			break;
		}
		if (quotec == 0 && (c == ' ' || c == '\t'))
			break;
		if (cp2 - lexstring < STRINGLEN-1)
			*cp2++ = c;
		c = *cp++;
	}
	if (quotec && c == '\0') {
		fprintf(stderr, "Missing %c\n", quotec);
		return (TERROR);
	}
	*sp = --cp;
	*cp2 = '\0';
	return (TSTRING);
}

/*
 * Unscan the named token by pushing it onto the regret stack.
 */
void
regret(int token)
{
	if (++regretp >= REGDEP)
		errx(1, "Too many regrets");
	regretstack[regretp] = token;
	lexstring[STRINGLEN-1] = '\0';
	string_stack[regretp] = savestr(lexstring);
	numberstack[regretp] = lexnumber;
}

/*
 * Reset all the scanner global variables.
 */
void
scaninit(void)
{
	regretp = -1;
}

/*
 * Find the first message whose flags & m == f  and return
 * its message number.
 */
int
first(int f, int m)
{
	struct message *mp;

	if (msgCount == 0)
		return (0);
	f &= MDELETED;
	m &= MDELETED;
	for (mp = dot; mp < &message[msgCount]; mp++)
		if ((mp->m_flag & m) == f)
			return (mp - message + 1);
	for (mp = dot-1; mp >= &message[0]; mp--)
		if ((mp->m_flag & m) == f)
			return (mp - message + 1);
	return (0);
}

/*
 * See if the passed name sent the passed message number.  Return true
 * if so.
 */
int
matchsender(char *str, int mesg)
{
	char *cp;

	/* null string matches nothing instead of everything */
	if (*str == '\0')
		return (0);

	cp = nameof(&message[mesg - 1], 0);
	return (strcasestr(cp, str) != NULL);
}

/*
 * See if the passed name received the passed message number.  Return true
 * if so.
 */

static char *to_fields[] = { "to", "cc", "bcc", NULL };

static int
matchto(char *str, int mesg)
{
	struct message *mp;
	char *cp, **to;

	str++;

	/* null string matches nothing instead of everything */
	if (*str == '\0')
		return (0);

	mp = &message[mesg - 1];

	for (to = to_fields; *to != NULL; to++) {
		cp = hfield(*to, mp);
		if (cp != NULL && strcasestr(cp, str) != NULL)
			return (1);
	}
	return (0);
}

/*
 * See if the given substring is contained within the specified field. If
 * 'searchheaders' is set, then the form '/x:y' will be accepted and matches
 * any message with the substring 'y' in field 'x'. If 'x' is omitted or
 * 'searchheaders' is not set, then the search matches any messages
 * with the substring 'y' in the 'Subject'. The search is case insensitive.
 *
 * The form '/to:y' is a special case, and will match all messages
 * containing the substring 'y' in the 'To', 'Cc', or 'Bcc' header
 * fields. The search for 'to' is case sensitive, so that '/To:y' can
 * be used to limit the search to just the 'To' field.
 */

static char lastscan[STRINGLEN];
int
matchfield(char *str, int mesg)
{
	struct message *mp;
	char *cp, *cp2;

	str++;
	if (*str == '\0')
		str = lastscan;
	else
		strlcpy(lastscan, str, sizeof(lastscan));
	mp = &message[mesg-1];

	/*
	 * Now look, ignoring case, for the word in the string.
	 */

	if (value("searchheaders") && (cp = strchr(str, ':')) != NULL) {
		/* Check for special case "/to:" */
		if (strncmp(str, "to:", 3) == 0)
			return (matchto(cp, mesg));
		*cp++ = '\0';
		cp2 = hfield(*str != '\0' ? str : "subject", mp);
		cp[-1] = ':';
		str = cp;
		cp = cp2;
	} else
		cp = hfield("subject", mp);

	if (cp == NULL)
		return (0);

	return (strcasestr(cp, str) != NULL);
}

/*
 * Mark the named message by setting its mark bit.
 */
void
mark(int mesg)
{
	int i;

	i = mesg;
	if (i < 1 || i > msgCount)
		errx(1, "Bad message number to mark");
	message[i-1].m_flag |= MMARK;
}

/*
 * Unmark the named message.
 */
void
unmark(int mesg)
{
	int i;

	i = mesg;
	if (i < 1 || i > msgCount)
		errx(1, "Bad message number to unmark");
	message[i-1].m_flag &= ~MMARK;
}

/*
 * Return the message number corresponding to the passed meta character.
 */
int
metamess(int meta, int f)
{
	int c, m;
	struct message *mp;

	c = meta;
	switch (c) {
	case '^':
		/*
		 * First 'good' message left.
		 */
		for (mp = &message[0]; mp < &message[msgCount]; mp++)
			if ((mp->m_flag & MDELETED) == f)
				return (mp - &message[0] + 1);
		printf("No applicable messages\n");
		return (-1);

	case '$':
		/*
		 * Last 'good message left.
		 */
		for (mp = &message[msgCount-1]; mp >= &message[0]; mp--)
			if ((mp->m_flag & MDELETED) == f)
				return (mp - &message[0] + 1);
		printf("No applicable messages\n");
		return (-1);

	case '.':
		/*
		 * Current message.
		 */
		m = dot - &message[0] + 1;
		if ((dot->m_flag & MDELETED) != f) {
			printf("%d: Inappropriate message\n", m);
			return (-1);
		}
		return (m);

	default:
		printf("Unknown metachar (%c)\n", c);
		return (-1);
	}
}
