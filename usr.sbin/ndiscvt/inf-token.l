%{
/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <regex.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "y.tab.h"

int lineno = 1;

int yylex(void);
void yyerror(const char *);

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
\n			{ lineno++; return EOL; }
\r			;
;.*$			;
\/\/.*$			;
=			{ return EQUALS; }
,			{ return COMMA; }
\"(\\\"|[^"]|\"\")*\"	{
			int len = strlen(yytext) - 2;
			int blen = len + 1;
			char *walker;
			int i;
			update_lineno(yytext);
			yylval.str = (char *)malloc(blen);
			if (yylval.str == NULL)
				goto out;
			walker = yylval.str;
			for (i = 1; i <= len; i++) {
				if (yytext[i] == '\"') {
					switch (yytext[i + 1]) {
					case '\"':
						i++;
						break;
					default:
						break;
					}
				}
				if (yytext[i] == '\\') {
					switch (yytext[i + 1]) {
					case '\n':
						i += 2;
						while(isspace(yytext[i]))
							i++;
						break;
					case '\"':
						i++;
						break;
					case '(':
						i++;
						break;
					default:
						break;
					}
				}
				*walker++ = yytext[i];
			}
			*walker++ = '\0';
			out:;
			return STRING;
			}
\[[a-zA-Z0-9%&\{\}\-\.\/_\\\*\ ]+\]	{
				int len = strlen(yytext);
				yytext[len-1] = '\0';
				yylval.str = strdup(yytext+1);
				return SECTION;
			}
[a-zA-Z0-9%&\{\}\-\.\/_\\\*]+		{
				yylval.str = strdup(yytext);
				return WORD;
			}
%%

void
yyerror(const char *s)
{
	errx(1, "line %d: %s%s %s.", lineno, yytext, yytext?":":"", s);
}
