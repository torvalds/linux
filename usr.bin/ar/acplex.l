%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Kai Wang
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>

#include "y.tab.h"

int lineno = 1;

int	yylex(void);

#define	YY_DECL	int yylex(void)

%}

%option nounput
%option noinput
%option noyywrap

%%

ADDLIB|addlib		{ return (ADDLIB); }
ADDMOD|addmod		{ return (ADDMOD); }
CLEAR|clear		{ return (CLEAR); }
CREATE|create		{ return (CREATE); }
DELETE|delete		{ return (DELETE); }
DIRECTORY|directory	{ return (DIRECTORY); }
END|end			{ return (END); }
EXTRACT|extract		{ return (EXTRACT); }
LIST|list		{ return (LIST); }
OPEN|open		{ return (OPEN); }
REPLACE|replace		{ return (REPLACE); }
VERBOSE|verbose		{ return (VERBOSE); }
SAVE|save		{ return (SAVE); }
"("			{ return (LP); }
")"			{ return (RP); }
","			{ return (COMMA); }

[-_A-Za-z0-9/:$.\\]+	{
	yylval.str = strdup(yytext);
	if (yylval.str == NULL)
		errc(EX_SOFTWARE, errno, "strdup failed");
	return (FNAME);
}

[ \t]			/* whitespace */
"*".*			/* comment */
";".*			/* comment */
"+\n"			{ lineno++; /* '+' is line continuation char */ }
"\n"			{ lineno++; return (EOL); }
