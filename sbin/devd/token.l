%{
/*-
 * DEVD (Device action daemon)
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 M. Warner Losh <imp@freebsd.org>.
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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "devd.h"
#include "y.tab.h"

int lineno = 1;

static void
update_lineno(const char *cp)
{
	while (*cp)	
		if (*cp++ == '\n')
			lineno++;
}

%}

%option nounput
%option noinput

%%

[ \t]+			;
\n			lineno++;
;			{ return SEMICOLON; }
#.*$			;
\/\/.*$			;
\/\*([^*]|(\*+([^*\/])))*\*+\/ { update_lineno(yytext); }
\{			{ return BEGINBLOCK; }
\}			{ return ENDBLOCK; }
[0-9]+			{ yylval.i = atoi(yytext); return NUMBER; }
\"[^"]+\"		{
				int len = strlen(yytext) - 2;
				char *walker;
				int i;
				update_lineno(yytext);
				if ((yylval.str = (char *) malloc(len + 1)) == NULL)
					goto out;
				walker = yylval.str;
				for (i = 1; i <= len; i++) {
					if (yytext[i] == '\\' && 
					    yytext[i + 1] == '\n') {
						i += 2;
						while(isspace(yytext[i]))
							i++;
					}
					*walker++ = yytext[i];
				}
				*walker++ = '\0';
			out:;
				return STRING;
			}


options			{ return OPTIONS; }
set			{ return SET; }
directory		{ return DIRECTORY; }
pid-file		{ return PID_FILE; }
attach			{ return ATTACH; }
detach			{ return DETACH; }
device-name		{ return DEVICE_NAME; }
media-type		{ return MEDIA_TYPE; }
class			{ return CLASS; }
subdevice		{ return SUBDEVICE; }
action			{ return ACTION; }
match			{ return MATCH; }
nomatch			{ return NOMATCH; }
notify			{ return NOTIFY; }
[A-Za-z][A-Za-z0-9_-]*	{
				yylval.str = strdup(yytext);
				return ID;
			}
%%

void
yyerror(const char *s)
{
	syslog(LOG_ERR, "line %d: %s%s %s.\n", lineno, yytext, yytext?":":"", s);
}
