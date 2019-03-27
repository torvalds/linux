/* $FreeBSD$ */
/* $NetBSD: lex.l,v 1.3 2006/02/09 22:03:15 dogcow Exp $	*/

%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
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
#include <sys/endian.h>
#include <sys/queue.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ldef.h"
#include "yacc.h"

#define YY_DECL int yylex(void)

int linenumber = 1;
%}
%option	noinput
%option	nounput

%x	COMMENT

%%

[ \t]+	{ }
#.*[\n]|"//".*[\n]|[\n]	{ linenumber++; return (R_LN); }

"/*"		{ BEGIN COMMENT; }
<COMMENT>"*/"	{ BEGIN 0; }
<COMMENT>[\n]	{ linenumber++; }
<COMMENT>.	{ }
<COMMENT><<EOF>>	{
		yyerror("unexpected file end (unterminated comment)\n");
		exit(1);
	}

([1-9][0-9]*)|(0[0-9]*)|(0[xX][0-9A-Fa-f]+)	{
		yylval.i_value = strtoul(yytext, NULL, 0);
		return (L_IMM);
	}

"NAME"		{ return (R_NAME); }
"ENCODING"	{ return (R_ENCODING); }
"VARIABLE"	{ return (R_VARIABLE); }
"DEFCSID"	{ return (R_DEFCSID); }
"INVALID"	{ return (R_INVALID); }

\"([^\"\n]*(\\\")?)*\"|\'([^\'\n]*(\\\')?)*\'	{
		size_t len;

		len = strlen(yytext);
		yylval.s_value = malloc(len - 1);
		strlcpy(yylval.s_value, yytext + 1, len - 1);
		return (L_STRING);
	}
[^ =/\-0-9\t\n][^ \t\n]*	{
		yylval.s_value = strdup(yytext);
		return (L_STRING);
	}

%%

#ifndef yywrap
int
yywrap(void)
{

	return (1);
}
#endif
