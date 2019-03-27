%{
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * APM (Advanced Power Management) Event Dispatcher
 *
 * Copyright (c) 1999 Mitsuru IWASAKI <iwasaki@FreeBSD.org>
 * Copyright (c) 1999 KOIE Hidetaka <koie@suri.co.jp>
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
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <string.h>
#include <syslog.h>
#include <bitstring.h>
#include "apmd.h"
#include "y.tab.h"

int lineno;
int first_time;
%}

/* We don't need it, avoid the warning. */
%option nounput
%option noinput

%s TOP

%%

%{
	if (first_time) {
		BEGIN TOP;
		lineno = 1;
		first_time = 0;
	}
%}

<TOP>[ \t]+		;
<TOP>\n			lineno++;
<TOP>,			{ return COMMA; }
<TOP>;			{ return SEMICOLON; }
<TOP>#.*$		;

<TOP>apm_event		{ return APMEVENT; }

<TOP>NOEVENT		{ yylval.ev = EVENT_NOEVENT; return EVENT; }
<TOP>STANDBYREQ		{ yylval.ev = EVENT_STANDBYREQ; return EVENT; }
<TOP>SUSPENDREQ		{ yylval.ev = EVENT_SUSPENDREQ; return EVENT; }
<TOP>NORMRESUME		{ yylval.ev = EVENT_NORMRESUME; return EVENT; }
<TOP>CRITRESUME		{ yylval.ev = EVENT_CRITRESUME; return EVENT; }
<TOP>BATTERYLOW		{ yylval.ev = EVENT_BATTERYLOW; return EVENT; }
<TOP>POWERSTATECHANGE	{ yylval.ev = EVENT_POWERSTATECHANGE; return EVENT; }
<TOP>UPDATETIME		{ yylval.ev = EVENT_UPDATETIME; return EVENT; }
<TOP>CRITSUSPEND	{ yylval.ev = EVENT_CRITSUSPEND; return EVENT; }
<TOP>USERSTANDBYREQ	{ yylval.ev = EVENT_USERSTANDBYREQ; return EVENT; }
<TOP>USERSUSPENDREQ	{ yylval.ev = EVENT_USERSUSPENDREQ; return EVENT; }
<TOP>STANDBYRESUME	{ yylval.ev = EVENT_STANDBYRESUME; return EVENT; }
<TOP>CAPABILITIESCHANGE	{ yylval.ev = EVENT_CAPABILITIESCHANGE; return EVENT; }

<TOP>apm_battery	{ return APMBATT; }

<TOP>charging		{ return BATTCHARGE; }
<TOP>discharging	{ return BATTDISCHARGE; }
<TOP>[0-9]+%		{
				yylval.i = atoi(yytext);
				return BATTPERCENT;
			}
<TOP>[0-9]+[Mm]		{
				yylval.i = -atoi(yytext);
				return BATTTIME;
			}

<TOP>exec		{ return EXECCMD; }
<TOP>reject		{ return REJECTCMD; }

<TOP>\{			{ return BEGINBLOCK; }
<TOP>\}			{ return ENDBLOCK; }
<TOP>\"[^"]+\"	{
				int len = strlen(yytext) - 2;
				if ((yylval.str = (char *) malloc(len + 1)) == NULL)
					goto out;
				memcpy(yylval.str, yytext + 1, len);
				yylval.str[len] = '\0';
			out:
				return STRING;
			}

<TOP>[^"{},;#\n\t ]+	{ yylval.str = strdup(yytext); return UNKNOWN; }
%%

void
yyerror(const char *s)
{
	syslog(LOG_ERR, "line %d: %s%s %s.\n", lineno, yytext, yytext?":":"", s);
}
