/*	$OpenBSD: getpar.c,v 1.15 2016/01/07 14:37:51 mestre Exp $	*/
/*	$NetBSD: getpar.c,v 1.4 1995/04/24 12:25:57 cgd Exp $	*/

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "getpar.h"
#include "trek.h"

static int testterm(void);

/**
 **	get integer parameter
 **/

int
getintpar(const char *s)
{
	int	i, n;

	while (1)
	{
		if (testnl() && s)
			printf("%s: ", s);
		i = scanf("%d", &n);
		if (i < 0)
			exit(1);
		if (i > 0 && testterm())
			return (n);
		printf("invalid input; please enter an integer\n");
		skiptonl(0);
	}
}

/**
 **	get floating parameter
 **/

double
getfltpar(const char *s)
{
	int		i;
	double		d;

	while (1)
	{
		if (testnl() && s)
			printf("%s: ", s);
		i = scanf("%lf", &d);
		if (i < 0)
			exit(1);
		if (i > 0 && testterm())
			return (d);
		printf("invalid input; please enter a double\n");
		skiptonl(0);
	}
}

/**
 **	get yes/no parameter
 **/

const struct cvntab	Yntab[] =
{
	{ "y",	"es",	(cmdfun)1,	1 },
	{ "n",	"o",	(cmdfun)0,	0 },
	{ NULL,	NULL,	NULL,		0 }
};

int
getynpar(const char *s)
{
	const struct cvntab	*r;

	r = getcodpar(s, Yntab);
	return (r->value2);
}


/**
 **	get coded parameter
 **/

const struct cvntab *
getcodpar(const char *s, const struct cvntab tab[])
{
	char				input[100];
	const struct cvntab		*r;
	int				flag;
	const char			*p, *q;
	int				c;
	int				f;

	flag = 0;
	while (1)
	{
		flag |= (f = testnl());
		if (flag)
			printf("%s: ", s);
		if (f)
			getchar();		/* throw out the newline */
		scanf("%*[ \t;]");
		if ((c = scanf("%99[^ \t;\n]", input)) < 0)
			exit(1);
		if (c == 0)
			continue;
		flag = 1;

		/* if command list, print four per line */
		if (input[0] == '?' && input[1] == 0)
		{
			c = 4;
			for (r = tab; r->abrev; r++)
			{
				strlcpy(input, r->abrev, sizeof input);
				strlcat(input, r->full, sizeof input);
				printf("%14.14s", input);
				if (--c > 0)
					continue;
				c = 4;
				printf("\n");
			}
			if (c != 4)
				printf("\n");
			continue;
		}

		/* search for in table */
		for (r = tab; r->abrev; r++)
		{
			p = input;
			for (q = r->abrev; *q; q++)
				if (*p++ != *q)
					break;
			if (!*q)
			{
				for (q = r->full; *p && *q; q++, p++)
					if (*p != *q)
						break;
				if (!*p || !*q)
					break;
			}
		}

		/* check for not found */
		if (!r->abrev)
		{
			printf("invalid input; ? for valid inputs\n");
			skiptonl(0);
		}
		else
			return (r);
	}
}


/**
 **	get string parameter
 **/

void
getstrpar(const char *s, char *r, int l, const char *t)
{
	int	i, f;
	char	format[20];

	if (t == 0)
		t = " \t\n;";
	(void)snprintf(format, sizeof format, "%%%d[^%s]", l, t);
	while (1)
	{
		if ((f = testnl()) && s)
			printf("%s: ", s);
		if (f)
			getchar();
		scanf("%*[\t ;]");
		i = scanf(format, r);
		if (i < 0)
			exit(1);
		if (i != 0)
			return;
	}
}


/**
 **	test if newline is next valid character
 **/

int
testnl(void)
{
	int	c;

	while ((c = getchar()) != '\n')
		if ((c >= '0' && c <= '9') || c == '.' || c == '!' ||
				(c >= 'A' && c <= 'Z') ||
				(c >= 'a' && c <= 'z') || c == '-' || c == EOF)
		{
			ungetc(c, stdin);
			return(0);
		}
	ungetc(c, stdin);
	return (1);
}


/**
 **	scan for newline
 **/

void
skiptonl(int c)
{
	while (c != '\n')
		if (!(c = getchar()))
			return;
	ungetc('\n', stdin);
	return;
}


/**
 **	test for valid terminator
 **/

static int
testterm(void)
{
	int	c;

	if (!(c = getchar()))
		return (1);
	if (c == '.')
		return (0);
	if (c == '\n' || c == ';')
		ungetc(c, stdin);
	return (1);
}


/*
**  TEST FOR SPECIFIED DELIMETER
**
**	The standard input is scanned for the parameter.  If found,
**	it is thrown away and non-zero is returned.  If not found,
**	zero is returned.
*/

int
readdelim(int d)
{
	int	c;

	while ((c = getchar()))
	{
		if (c == d)
			return (1);
		if (c == ' ')
			continue;
		ungetc(c, stdin);
		break;
	}
	return (0);
}
