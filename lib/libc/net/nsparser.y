%{
/*	$NetBSD: nsparser.y,v 1.3 1999/01/25 00:16:18 lukem Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997, 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#define _NS_PRIVATE
#include <nsswitch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "un-namespace.h"

static	void	_nsaddsrctomap(const char *);

static	ns_dbt		curdbt;
static	ns_src		cursrc;
%}

%union {
	char *str;
	int   mapval;
}

%token	NL
%token	SUCCESS UNAVAIL NOTFOUND TRYAGAIN
%token	RETURN CONTINUE
%token  ERRORTOKEN
%token	<str> STRING

%type	<mapval> Status Action

%%

File
	:	/* empty */
	| Lines
	;

Lines
	: Entry
	| Lines Entry
	;

Entry
	: NL
	| Database ':' NL
		{
			free((char*)curdbt.name);
		}
	| Database ':' Srclist NL
		{
			_nsdbtput(&curdbt);
		}
	| error NL
		{
			yyerrok;
		}
	;

Database
	: STRING
		{
			curdbt.name = yylval.str;
			curdbt.srclist = NULL;
			curdbt.srclistsize = 0;
		}
	;

Srclist
	: Item
	| Srclist Item
	;

Item
	: STRING
		{
			cursrc.flags = NS_TERMINATE;
			_nsaddsrctomap($1);
		}
	| STRING '[' { cursrc.flags = NS_SUCCESS; } Criteria ']'
		{
			_nsaddsrctomap($1);
		}
	;

Criteria
	: Criterion
	| Criteria Criterion
	;

Criterion
	: Status '=' Action
		{
			if ($3)	     /* if action == RETURN set RETURN bit */
				cursrc.flags |= $1;  
			else	     /* else unset it */
				cursrc.flags &= ~$1;
		}
	;

Status
	: SUCCESS	{ $$ = NS_SUCCESS; }
	| UNAVAIL	{ $$ = NS_UNAVAIL; }
	| NOTFOUND	{ $$ = NS_NOTFOUND; }
	| TRYAGAIN	{ $$ = NS_TRYAGAIN; }
	;

Action
	: RETURN	{ $$ = NS_ACTION_RETURN; }
	| CONTINUE	{ $$ = NS_ACTION_CONTINUE; }
	;

%%

static void
_nsaddsrctomap(const char *elem)
{
	int		i, lineno;
	extern int	_nsyylineno;
	extern char *	_nsyytext;

	lineno = _nsyylineno - (*_nsyytext == '\n' ? 1 : 0);
	if (curdbt.srclistsize > 0) {
		if (((strcasecmp(elem, NSSRC_COMPAT) == 0) &&
		    (strcasecmp(curdbt.srclist[0].name, NSSRC_CACHE) != 0)) ||
		    (strcasecmp(curdbt.srclist[0].name, NSSRC_COMPAT) == 0)) {
			syslog(LOG_ERR,
	    "NSSWITCH(nsparser): %s line %d: 'compat' used with sources, other than 'cache'",
			    _PATH_NS_CONF, lineno);
			free((void*)elem);
			return;
		}
	}
	for (i = 0; i < curdbt.srclistsize; i++) {
		if (strcasecmp(curdbt.srclist[i].name, elem) == 0) {
			syslog(LOG_ERR,
		       "NSSWITCH(nsparser): %s line %d: duplicate source '%s'",
			    _PATH_NS_CONF, lineno, elem);
			free((void*)elem);
			return;
		}
	}
	cursrc.name = elem;
	_nsdbtaddsrc(&curdbt, &cursrc);
}
