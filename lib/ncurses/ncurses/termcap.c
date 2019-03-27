/* A portion of this file is from ncurses: */
/***************************************************************************
*                            COPYRIGHT NOTICE                              *
****************************************************************************
*                ncurses is copyright (C) 1992-1995                        *
*                          Zeyd M. Ben-Halim                               *
*                          zmbenhal@netcom.com                             *
*                          Eric S. Raymond                                 *
*                          esr@snark.thyrsus.com                           *
*                                                                          *
*        Permission is hereby granted to reproduce and distribute ncurses  *
*        by any means and for any fee, whether alone or as part of a       *
*        larger distribution, in source or in binary form, PROVIDED        *
*        this notice is included with any such distribution, and is not    *
*        removed from any of its header files. Mention of ncurses in any   *
*        applications linked with it is highly appreciated.                *
*                                                                          *
*        ncurses comes AS IS with no warranty, implied or expressed.       *
*                                                                          *
***************************************************************************/

#include <curses.priv.h>

#include <string.h>
#include <term.h>
#include <tic.h>
#include <term_entry.h>

/* The rest is from BSD */
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#if 0
#ifndef lint
static const char sccsid[] = "@(#)termcap.c	8.1 (Berkeley) 6/4/93";
#endif /* not lint */
#endif

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>
#include "pathnames.h"

#define	PBUFSIZ	MAXPATHLEN	/* max length of filename path */
#define	PVECSIZ	32		/* max number of names in path */
#define	TBUFSIZ 1024		/* max length of _nc_tgetent buffer */

char _nc_termcap[TBUFSIZ + 1]; /* Last getcap, provided to tgetent() emul */

/*
 * termcap - routines for dealing with the terminal capability data base
 *
 * BUG:		Should use a "last" pointer in tbuf, so that searching
 *		for capabilities alphabetically would not be a n**2/2
 *		process when large numbers of capabilities are given.
 * Note:	If we add a last pointer now we will screw up the
 *		tc capability. We really should compile termcap.
 *
 * Essentially all the work here is scanning and decoding escapes
 * in string capabilities.  We don't use stdio because the editor
 * doesn't, and because living w/o it is not hard.
 */

/*
 * Get an entry for terminal name in buffer _nc_termcap from the termcap
 * file.
 */
int
_nc_read_termcap_entry(const char *const name, TERMTYPE *const tp)
{
	ENTRY	*ep;
	char *p;
	char *cp;
	char  *dummy;
	char **fname;
	char  *home;
	int    i;
	char   pathbuf[PBUFSIZ];	/* holds raw path of filenames */
	char  *pathvec[PVECSIZ];	/* to point to names in pathbuf */
	char **pvec;			/* holds usable tail of path vector */
	char  *termpath;

	_nc_termcap[0] = '\0';		/* in case */
	dummy = NULL;
	fname = pathvec;
	pvec = pathvec;
	p = pathbuf;
	cp = getenv("TERMCAP");
	/*
	 * TERMCAP can have one of two things in it. It can be the
	 * name of a file to use instead of /etc/termcap. In this
	 * case it better start with a "/". Or it can be an entry to
	 * use so we don't have to read the file. In this case it
	 * has to already have the newlines crunched out.  If TERMCAP
	 * does not hold a file name then a path of names is searched
	 * instead.  The path is found in the TERMPATH variable, or
	 * becomes "$HOME/.termcap /etc/termcap" if no TERMPATH exists.
	 */
	if (!cp || *cp != '/') {	/* no TERMCAP or it holds an entry */
		if ( (termpath = getenv("TERMPATH")) )
			strncpy(pathbuf, termpath, PBUFSIZ);
		else {
			if ( (home = getenv("HOME")) ) {/* set up default */
				strncpy(pathbuf, home, PBUFSIZ - 1); /* $HOME first */
				pathbuf[PBUFSIZ - 2] = '\0'; /* -2 because we add a slash */
				p += strlen(pathbuf);	/* path, looking in */
				*p++ = '/';
			}	/* if no $HOME look in current directory */
			strncpy(p, _PATH_DEF, PBUFSIZ - (p - pathbuf));
		}
	}
	else				/* user-defined name in TERMCAP */
		strncpy(pathbuf, cp, PBUFSIZ);	/* still can be tokenized */

	/* For safety */
	if (issetugid())
		strcpy(pathbuf, _PATH_DEF_SEC);

	pathbuf[PBUFSIZ - 1] = '\0';

	*fname++ = pathbuf;	/* tokenize path into vector of names */
	while (*++p)
		if (*p == ' ' || *p == ':') {
			*p = '\0';
			while (*++p)
				if (*p != ' ' && *p != ':')
					break;
			if (*p == '\0')
				break;
			*fname++ = p;
			if (fname >= pathvec + PVECSIZ) {
				fname--;
				break;
			}
		}
	*fname = (char *) 0;			/* mark end of vector */
	if (cp && *cp && *cp != '/')
		if (cgetset(cp) < 0)
			return(-2);

	i = cgetent(&dummy, pathvec, (char *)name);

	if (i == 0) {
		char *pd, *ps, *tok, *s, *tcs;
		size_t len;

		pd = _nc_termcap;
		ps = dummy;
		if ((tok = strchr(ps, ':')) == NULL) {
			len = strlen(ps);
			if (len >= TBUFSIZ)
				i = -1;
			else
				strcpy(pd, ps);
			goto done;
		}
		len = tok - ps + 1;
		if (pd + len + 1 - _nc_termcap >= TBUFSIZ) {
			i = -1;
			goto done;
		}
		memcpy(pd, ps, len);
		ps += len;
		pd += len;
		*pd = '\0';
		tcs = pd - 1;
		for (;;) {
			while ((tok = strsep(&ps, ":")) != NULL &&
			       *(tok - 2) != '\\' &&
			       (*tok == '\0' || *tok == '\\' || !isgraph(UChar(*tok))))
				;
			if (tok == NULL)
				break;
			for (s = tcs; s != NULL && s[1] != '\0';
			     s = strchr(s, ':')) {
				s++;
				if (s[0] == tok[0] && s[1] == tok[1])
					goto skip_it;
			}
			len = strlen(tok);
			if (pd + len + 1 - _nc_termcap >= TBUFSIZ) {
				i = -1;
				break;
			}
			memcpy(pd, tok, len);
			pd += len;
			*pd++ = ':';
			*pd = '\0';
		skip_it: ;
		}
	}
done:
	if (dummy)
		free(dummy);


/*
 * From here on is ncurses-specific glue code
 */

	if (i < 0)
		return(TGETENT_ERR);

	_nc_set_source("TERMCAP");
	_nc_read_entry_source((FILE *)NULL, _nc_termcap, FALSE, TRUE, NULLHOOK);

	if (_nc_head == (ENTRY *)NULL)
		return(TGETENT_ERR);

	/* resolve all use references */
	_nc_resolve_uses2(TRUE, FALSE);

	for_entry_list(ep)
		if (_nc_name_match(ep->tterm.term_names, name, "|:"))
		{
			/*
			 * Make a local copy of the terminal capabilities, delinked
			 * from the list.
			 */
			memcpy(tp, &ep->tterm, sizeof(TERMTYPE));
			_nc_delink_entry(_nc_head, &(ep->tterm));
			free(ep);
			_nc_free_entries(_nc_head);
			_nc_head = _nc_tail = NULL;	/* do not reuse! */

			return TGETENT_YES;	/* OK */
		}

	_nc_free_entries(_nc_head);
	_nc_head = _nc_tail = NULL;	/* do not reuse! */
	return(TGETENT_NO);	/* not found */
}
