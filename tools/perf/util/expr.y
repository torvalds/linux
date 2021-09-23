/* Simple expression parser */
%{
#define YYDEBUG 1
#include <math.h>
#include "util/debug.h"
#include "smt.h"
#define IN_EXPR_Y 1
#include "expr.h"
%}

%define api.pure full

%parse-param { double *final_val }
%parse-param { struct expr_parse_ctx *ctx }
%parse-param {void *scanner}
%lex-param {void* scanner}

%union {
	double	 num;
	char	*str;
}

%token ID NUMBER MIN MAX IF ELSE SMT_ON D_RATIO EXPR_ERROR EXPR_PARSE EXPR_OTHER
%left MIN MAX IF
%left '|'
%left '^'
%left '&'
%left '<' '>'
%left '-' '+'
%left '*' '/' '%'
%left NEG NOT
%type <num> NUMBER
%type <str> ID
%destructor { free ($$); } <str>
%type <num> expr if_expr

%{
static void expr_error(double *final_val __maybe_unused,
		       struct expr_parse_ctx *ctx __maybe_unused,
		       void *scanner,
		       const char *s)
{
	pr_debug("%s\n", s);
}

#define BINARY_LONG_OP(RESULT, OP, LHS, RHS)				\
	RESULT = (long)LHS OP (long)RHS;

#define BINARY_OP(RESULT, OP, LHS, RHS)					\
	RESULT = LHS OP RHS;

%}
%%

start:
EXPR_PARSE all_expr
|
EXPR_OTHER all_other

all_other: all_other other
|

other: ID
{
	expr__add_id(ctx, $1);
}
|
MIN | MAX | IF | ELSE | SMT_ON | NUMBER | '|' | '^' | '&' | '-' | '+' | '*' | '/' | '%' | '(' | ')' | ','
|
'<' | '>' | D_RATIO

all_expr: if_expr			{ *final_val = $1; }

if_expr: expr IF expr ELSE expr
{
	$$ = $3 ? $1 : $5;
}
| expr
;

expr: NUMBER
{
	$$ = $1;
}
| ID
{
	struct expr_id_data *data;

	$$ = NAN;
	if (expr__resolve_id(ctx, $1, &data) == 0)
		$$ = expr_id_data__value(data);

	free($1);
}
| expr '|' expr { BINARY_LONG_OP($$, |, $1, $3); }
| expr '&' expr { BINARY_LONG_OP($$, &, $1, $3); }
| expr '^' expr { BINARY_LONG_OP($$, ^, $1, $3); }
| expr '<' expr { BINARY_OP($$, <, $1, $3); }
| expr '>' expr { BINARY_OP($$, >, $1, $3); }
| expr '+' expr { BINARY_OP($$, +, $1, $3); }
| expr '-' expr { BINARY_OP($$, -, $1, $3); }
| expr '*' expr { BINARY_OP($$, *, $1, $3); }
| expr '/' expr
{
	if ($3 == 0) {
		pr_debug("division by zero\n");
		YYABORT;
	}
	$$ = $1 / $3;
}
| expr '%' expr
{
	if ((long)$3 == 0) {
		pr_debug("division by zero\n");
		YYABORT;
	}
	$$ = (long)$1 % (long)$3;
}
| D_RATIO '(' expr ',' expr ')'
{
	if ($5 == 0) {
		$$ = 0;
	} else {
		$$ = $3 / $5;
	}
}
| '-' expr %prec NEG
{
	$$ = -$2;
}
| '(' if_expr ')'
{
	$$ = $2;
}
| MIN '(' expr ',' expr ')'
{
	$$ = $3 < $5 ? $3 : $5;
}
| MAX '(' expr ',' expr ')'
{
	$$ = $3 > $5 ? $3 : $5;
}
| SMT_ON
{
	$$ = smt_on() > 0 ? 1.0 : 0.0;
}
;

%%
