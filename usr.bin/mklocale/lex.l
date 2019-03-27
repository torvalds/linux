%{
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)lex.l	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "ldef.h"
#include "y.tab.h"
#include "extern.h"

#define	YY_DECL	int yylex(void)
%}

ODIGIT	[0-7]
DIGIT	[0-9]
XDIGIT	[0-9a-fA-F]
W	[\t\n\r ]

%%
\'.\'				{ yylval.rune = (unsigned char)yytext[1];
				  return(RUNE); }

'\\a'				{ yylval.rune = '\a';
				  return(RUNE); }
'\\b'				{ yylval.rune = '\b';
				  return(RUNE); }
'\\f'				{ yylval.rune = '\f';
				  return(RUNE); }
'\\n'				{ yylval.rune = '\n';
				  return(RUNE); }
'\\r'				{ yylval.rune = '\r';
				  return(RUNE); }
'\\t'				{ yylval.rune = '\t';
				  return(RUNE); }
'\\v'				{ yylval.rune = '\v';
				  return(RUNE); }

0x{XDIGIT}+			{ yylval.rune = strtol(yytext, 0, 16);
				  return(RUNE); }
0{ODIGIT}+			{ yylval.rune = strtol(yytext, 0, 8);
				  return(RUNE); }
{DIGIT}+			{ yylval.rune = strtol(yytext, 0, 10);
				  return(RUNE); }


MAPLOWER			{ return(MAPLOWER); }
MAPUPPER			{ return(MAPUPPER); }
TODIGIT				{ return(DIGITMAP); }
INVALID				{ return(INVALID); }

ALPHA				{ yylval.i = _CTYPE_A|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
CONTROL				{ yylval.i = _CTYPE_C;
				  return(LIST); }
DIGIT				{ yylval.i = _CTYPE_D|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
GRAPH				{ yylval.i = _CTYPE_G|_CTYPE_R;
				  return(LIST); }
LOWER				{ yylval.i = _CTYPE_L|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
PUNCT				{ yylval.i = _CTYPE_P|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
SPACE				{ yylval.i = _CTYPE_S;
				  return(LIST); }
UPPER				{ yylval.i = _CTYPE_U|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
XDIGIT				{ yylval.i = _CTYPE_X|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
BLANK				{ yylval.i = _CTYPE_B;
				  return(LIST); }
PRINT				{ yylval.i = _CTYPE_R;
				  return(LIST); }
IDEOGRAM			{ yylval.i = _CTYPE_I|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
SPECIAL				{ yylval.i = _CTYPE_T|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
PHONOGRAM			{ yylval.i = _CTYPE_Q|_CTYPE_R|_CTYPE_G;
				  return(LIST); }
SWIDTH0				{ yylval.i = _CTYPE_SW0; return(LIST); }
SWIDTH1				{ yylval.i = _CTYPE_SW1; return(LIST); }
SWIDTH2				{ yylval.i = _CTYPE_SW2; return(LIST); }
SWIDTH3				{ yylval.i = _CTYPE_SW3; return(LIST); }

VARIABLE[\t ]			{ static char vbuf[1024];
				  char *v = vbuf;
				  while ((*v = input()) && *v != '\n')
					++v;
                                  if (*v) {
					unput(*v);
					*v = 0;
				  }
				  yylval.str = vbuf;
				  return(VARIABLE);
				}

ENCODING			{ return(ENCODING); }

\".*\"				{ char *e = yytext + 1;
				  yylval.str = e;
				  while (*e && *e != '"')
					++e;
				  *e = 0;
				  return(STRING); }

\<|\(|\[			{ return(LBRK); }

\>|\)|\]			{ return(RBRK); }

\-				{ return(THRU); }
\.\.\.				{ return(THRU); }

\:				{ return(':'); }

{W}+				;

^\#.*\n				;
\/\*				{ char lc = 0;
				  do {
				    while ((lc) != '*')
					if ((lc = input()) == 0)
					    break;
				  } while((lc = input()) != '/');
				}

\\$				;
.				{ printf("Lex is skipping '%s'\n", yytext); }
%%

#if	!defined(yywrap)
int
yywrap(void)
{
	return(1);
}
#endif
