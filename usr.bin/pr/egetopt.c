/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1991 Keith Muller.
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Keith Muller of the University of California, San Diego.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)egetopt.c	8.1 (Berkeley) 6/6/93";
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "extern.h"

/*
 * egetopt:	get option letter from argument vector (an extended
 *		version of getopt).
 *
 * Non standard additions to the ostr specs are:
 * 1) '?': immediate value following arg is optional (no white space
 *    between the arg and the value)
 * 2) '#': +/- followed by a number (with an optional sign but
 *    no white space between the arg and the number). The - may be
 *    combined with other options, but the + cannot.
 */

int	eopterr = 1;		/* if error message should be printed */
int	eoptind = 1;		/* index into parent argv vector */
int	eoptopt;		/* character checked for validity */
char	*eoptarg;		/* argument associated with option */

#define	BADCH	(int)'?'

static char	emsg[] = "";

int
egetopt(int nargc, char * const *nargv, const char *ostr)
{
	static char *place = emsg;	/* option letter processing */
	char *oli;			/* option letter list index */
	static int delim;		/* which option delimiter */
	char *p;
	static char savec = '\0';

	if (savec != '\0') {
		*place = savec;
		savec = '\0';
	}

	if (!*place) {
		/*
		 * update scanning pointer
		 */
		if ((eoptind >= nargc) ||
		    ((*(place = nargv[eoptind]) != '-') && (*place != '+'))) {
			place = emsg;
			return (-1);
		}

		delim = (int)*place;
		if (place[1] && *++place == '-' && !place[1]) {
			/*
			 * found "--"
			 */
			++eoptind;
			place = emsg;
			return (-1);
		}
	}

	/*
	 * check option letter
	 */
	if ((eoptopt = (int)*place++) == (int)':' || (eoptopt == (int)'?') ||
	    !(oli = strchr(ostr, eoptopt))) {
		/*
		 * if the user didn't specify '-' as an option,
		 * assume it means -1 when by itself.
		 */
		if ((eoptopt == (int)'-') && !*place)
			return (-1);
		if (strchr(ostr, '#') && (isdigit(eoptopt) ||
		    (((eoptopt == (int)'-') || (eoptopt == (int)'+')) &&
		      isdigit(*place)))) {
			/*
			 * # option: +/- with a number is ok
			 */
			for (p = place; *p != '\0'; ++p) {
				if (!isdigit(*p))
					break;
			}
			eoptarg = place-1;

			if (*p == '\0') {
				place = emsg;
				++eoptind;
			} else {
				place = p;
				savec = *p;
				*place = '\0';
			}
			return (delim);
		}

		if (!*place)
			++eoptind;
		if (eopterr) {
			if (!(p = strrchr(*nargv, '/')))
				p = *nargv;
			else
				++p;
			(void)fprintf(stderr, "%s: illegal option -- %c\n",
			    p, eoptopt);
		}
		return (BADCH);
	}
	if (delim == (int)'+') {
		/*
		 * '+' is only allowed with numbers
		 */
		if (!*place)
			++eoptind;
		if (eopterr) {
			if (!(p = strrchr(*nargv, '/')))
				p = *nargv;
			else
				++p;
			(void)fprintf(stderr,
				"%s: illegal '+' delimiter with option -- %c\n",
				p, eoptopt);
		}
		return (BADCH);
	}
	++oli;
	if ((*oli != ':') && (*oli != '?')) {
		/*
		 * don't need argument
		 */
		eoptarg = NULL;
		if (!*place)
			++eoptind;
		return (eoptopt);
	}

	if (*place) {
		/*
		 * no white space
		 */
		eoptarg = place;
	} else if (*oli == '?') {
		/*
		 * no arg, but NOT required
		 */
		eoptarg = NULL;
	} else if (nargc <= ++eoptind) {
		/*
		 * no arg, but IS required
		 */
		place = emsg;
		if (eopterr) {
			if (!(p = strrchr(*nargv, '/')))
				p = *nargv;
			else
				++p;
			(void)fprintf(stderr,
			    "%s: option requires an argument -- %c\n", p,
			    eoptopt);
		}
		return (BADCH);
	} else {
		/*
		 * arg has white space
		 */
		eoptarg = nargv[eoptind];
	}
	place = emsg;
	++eoptind;
	return (eoptopt);
}
