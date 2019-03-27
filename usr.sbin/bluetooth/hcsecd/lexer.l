%{
/*-
 * lexer.l
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: lexer.l,v 1.1 2002/11/24 20:22:39 max Exp $
 * $FreeBSD$
 */

#include <string.h>
#include "parser.h"
%}

%option yylineno noyywrap nounput noinput

delim		[ \t\n]
ws		{delim}+
empty		{delim}*
comment		\#.*

hexdigit	[0-9a-fA-F]
hexbyte		{hexdigit}{hexdigit}

device_word	device
bdaddr_word	bdaddr
name_word	name
key_word	key
nokey_word	nokey
pin_word	pin
nopin_word	nopin

bdaddrstring	{hexbyte}:{hexbyte}:{hexbyte}:{hexbyte}:{hexbyte}:{hexbyte}
hexstring	0x{hexbyte}+
string		\".+\"

%%

\;		return (';');
\:		return (':');
\{		return ('{');
\}		return ('}');

{ws}		;
{empty}		;
{comment}	;

{device_word}	return (T_DEVICE);
{bdaddr_word}	return (T_BDADDR);
{name_word}	return (T_NAME);
{key_word}	return (T_KEY);
{nokey_word}	return (T_NOKEY);
{pin_word}	return (T_PIN);
{nopin_word}	return (T_NOPIN);

{bdaddrstring}	{
		yylval.string = yytext;
		return (T_BDADDRSTRING);
		}

{hexstring}	{
		yylval.string = &yytext[2];
		return (T_HEXSTRING);
		}

{string}	{
		yytext[strlen(yytext) - 1] = 0;
		yylval.string = &yytext[1];
		return (T_STRING);
		}

%%

