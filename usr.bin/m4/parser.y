%{
/* $OpenBSD: parser.y,v 1.7 2012/04/12 17:00:11 espie Exp $ */
/*
 * Copyright (c) 2004 Marc Espie <espie@cvs.openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * $FreeBSD$
 */

#include <math.h>
#include <stdint.h>
#define YYSTYPE	int32_t
extern int32_t end_result;
extern int yylex(void);
extern int yyerror(const char *);
%}
%token NUMBER
%token ERROR
%left LOR
%left LAND
%left '|'
%left '^'
%left '&'
%left EQ NE
%left '<' LE '>' GE
%left LSHIFT RSHIFT
%left '+' '-'
%left '*' '/' '%'
%right EXPONENT
%right UMINUS UPLUS '!' '~'

%%

top	: expr { end_result = $1; }
	;
expr 	: expr '+' expr { $$ = $1 + $3; }
     	| expr '-' expr { $$ = $1 - $3; }
	| expr EXPONENT expr { $$ = pow($1, $3); }
     	| expr '*' expr { $$ = $1 * $3; }
	| expr '/' expr {
		if ($3 == 0) {
			yyerror("division by zero");
			exit(1);
		}
		$$ = $1 / $3;
	}
	| expr '%' expr { 
		if ($3 == 0) {
			yyerror("modulo zero");
			exit(1);
		}
		$$ = $1 % $3;
	}
	| expr LSHIFT expr { $$ = $1 << $3; }
	| expr RSHIFT expr { $$ = $1 >> $3; }
	| expr '<' expr { $$ = $1 < $3; }
	| expr '>' expr { $$ = $1 > $3; }
	| expr LE expr { $$ = $1 <= $3; }
	| expr GE expr { $$ = $1 >= $3; }
	| expr EQ expr { $$ = $1 == $3; }
	| expr NE expr { $$ = $1 != $3; }
	| expr '&' expr { $$ = $1 & $3; }
	| expr '^' expr { $$ = $1 ^ $3; }
	| expr '|' expr { $$ = $1 | $3; }
	| expr LAND expr { $$ = $1 && $3; }
	| expr LOR expr { $$ = $1 || $3; }
	| '(' expr ')' { $$ = $2; }
	| '-' expr %prec UMINUS { $$ = -$2; }
	| '+' expr %prec UPLUS  { $$ = $2; }
	| '!' expr { $$ = !$2; }
	| '~' expr { $$ = ~$2; }
	| NUMBER
	;
%%

