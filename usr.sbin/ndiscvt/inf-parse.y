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

#include <stdio.h>
#include <sys/types.h>
#include <sys/queue.h>

#include "inf.h"

extern int yylex (void);
extern void yyerror(const char *);
%}

%token	EQUALS COMMA EOL
%token	<str> SECTION
%token	<str> STRING
%token	<str> WORD

%union {
	char *str;
}

%%

inf_file
	: inf_list
	|
	;

inf_list
	: inf
	| inf_list inf
	;

inf
	: SECTION EOL
		{ section_add($1); }
	| WORD EQUALS assign EOL
		{ assign_add($1); }
	| WORD COMMA regkey EOL
		{ regkey_add($1); }
	| WORD EOL
		{ define_add($1); }
	| EOL
	;

assign
	: WORD
		{ push_word($1); }
	| STRING
		{ push_word($1); }
	| WORD COMMA assign
		{ push_word($1); }
	| STRING COMMA assign
		{ push_word($1); }
	| COMMA assign
		{ push_word(NULL); }
	| COMMA
		{ push_word(NULL); }
	|
	;

regkey
	: WORD
		{ push_word($1); }
	| STRING
		{ push_word($1); }
	| WORD COMMA regkey
		{ push_word($1); }
	| STRING COMMA regkey
		{ push_word($1); }
	| COMMA regkey
		{ push_word(NULL); }
	| COMMA
		{ push_word(NULL); }
	;
%%
