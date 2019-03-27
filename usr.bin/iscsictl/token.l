%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "iscsictl.h"
#include "y.tab.h"

int lineno;

#define	YY_DECL int yylex(void)
extern int	yylex(void);

%}

%option noinput
%option nounput
%option noyywrap

%%
HeaderDigest		{ return HEADER_DIGEST; }
DataDigest		{ return DATA_DIGEST; }
TargetName		{ return TARGET_NAME; }
TargetAddress		{ return TARGET_ADDRESS; }
InitiatorName		{ return INITIATOR_NAME; }
InitiatorAddress	{ return INITIATOR_ADDRESS; }
InitiatorAlias		{ return INITIATOR_ALIAS; }
chapIName		{ return USER; }
chapSecret		{ return SECRET; }
tgtChapName		{ return MUTUAL_USER; }
tgtChapSecret		{ return MUTUAL_SECRET; }
AuthMethod		{ return AUTH_METHOD; }
SessionType		{ return SESSION_TYPE; }
enable			{ return ENABLE; }
protocol		{ return PROTOCOL; }
offload			{ return OFFLOAD; }
port			{ return IGNORED; }
MaxConnections		{ return IGNORED; }
TargetAlias		{ return IGNORED; }
TargetPortalGroupTag	{ return IGNORED; }
InitialR2T		{ return IGNORED; }
ImmediateData		{ return IGNORED; }
MaxRecvDataSegmentLength	{ return IGNORED; }
MaxBurstLength		{ return IGNORED; }
FirstBurstLength	{ return IGNORED; }
DefaultTime2Wait	{ return IGNORED; }
DefaultTime2Retain	{ return IGNORED; }
MaxOutstandingR2T	{ return IGNORED; }
DataPDUInOrder		{ return IGNORED; }
DataSequenceInOrder	{ return IGNORED; }
ErrorRecoveryLevel	{ return IGNORED; }
tags			{ return IGNORED; }
maxluns			{ return IGNORED; }
sockbufsize		{ return IGNORED; }
chapDigest		{ return IGNORED; }
\"[^"]+\"		{ yylval.str = strndup(yytext + 1,
			    strlen(yytext) - 2); return STR; }
[a-zA-Z0-9\.\-_/\:\[\]]+ { yylval.str = strdup(yytext); return STR; }
\{			{ return OPENING_BRACKET; }
\}			{ return CLOSING_BRACKET; }
=			{ return EQUALS; }
;			{ return SEMICOLON; }
#.*$			/* ignore comments */;
\r\n			{ lineno++; }
\n			{ lineno++; }
[ \t]+			/* ignore whitespace */;
.			{ yylval.str = strdup(yytext); return STR; }
%%
