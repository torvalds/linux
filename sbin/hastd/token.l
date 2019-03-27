%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2010 The FreeBSD Foundation
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>
 * All rights reserved.
 *
 * This software was developed by Pawel Jakub Dawidek under sponsorship from
 * the FreeBSD Foundation.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <string.h>

#include "hast.h"

#include "y.tab.h"

int depth;
int lineno;

#define	DP	do { } while (0)
#define	YY_DECL	int yylex(void)

extern int yylex(void);
%}

%option noinput
%option nounput
%option noyywrap

%%
control			{ DP; return CONTROL; }
pidfile			{ DP; return PIDFILE; }
listen			{ DP; return LISTEN; }
replication		{ DP; return REPLICATION; }
checksum		{ DP; return CHECKSUM; }
compression		{ DP; return COMPRESSION; }
timeout			{ DP; return TIMEOUT; }
exec			{ DP; return EXEC; }
metaflush		{ DP; return METAFLUSH; }
resource		{ DP; return RESOURCE; }
name			{ DP; return NAME; }
local			{ DP; return LOCAL; }
remote			{ DP; return REMOTE; }
source			{ DP; return SOURCE; }
on			{ DP; return ON; }
off			{ DP; return OFF; }
fullsync		{ DP; return FULLSYNC; }
memsync			{ DP; return MEMSYNC; }
async			{ DP; return ASYNC; }
none			{ DP; return NONE; }
crc32			{ DP; return CRC32; }
sha256			{ DP; return SHA256; }
hole			{ DP; return HOLE; }
lzf			{ DP; return LZF; }
[0-9]+			{ DP; yylval.num = atoi(yytext); return NUM; }
[a-zA-Z0-9\.\-_/\:\[\]]+ { DP; yylval.str = strdup(yytext); return STR; }
\{			{ DP; depth++; return OB; }
\}			{ DP; depth--; return CB; }
#.*$			/* ignore comments */;
\n			{ lineno++; }
[ \t]+			/* ignore whitespace */;
%%
