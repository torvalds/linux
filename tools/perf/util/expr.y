/* Simple expression parser */
%{
#include "util.h"
#include "util/debug.h"
#define IN_EXPR_Y 1
#include "expr.h"
#include <string.h>

#define MAXIDLEN 256
%}

%pure-parser
%parse-param { double *final_val }
%parse-param { struct parse_ctx *ctx }
%parse-param { const char **pp }
%lex-param { const char **pp }

%union {
	double num;
	char id[MAXIDLEN+1];
}

%token <num> NUMBER
%token <id> ID
%left '|'
%left '^'
%left '&'
%left '-' '+'
%left '*' '/' '%'
%left NEG NOT
%type <num> expr

%{
static int expr__lex(YYSTYPE *res, const char **pp);

static void expr__error(double *final_val __maybe_unused,
		       struct parse_ctx *ctx __maybe_unused,
		       const char **pp __maybe_unused,
		       const char *s)
{
	pr_debug("%s\n", s);
}

static int lookup_id(struct parse_ctx *ctx, char *id, double *val)
{
	int i;

	for (i = 0; i < ctx->num_ids; i++) {
		if (!strcasecmp(ctx->ids[i].name, id)) {
			*val = ctx->ids[i].val;
			return 0;
		}
	}
	return -1;
}

%}
%%

all_expr: expr			{ *final_val = $1; }
	;

expr:	  NUMBER
	| ID			{ if (lookup_id(ctx, $1, &$$) < 0) {
					pr_debug("%s not found", $1);
					YYABORT;
				  }
				}
	| expr '+' expr		{ $$ = $1 + $3; }
	| expr '-' expr		{ $$ = $1 - $3; }
	| expr '*' expr		{ $$ = $1 * $3; }
	| expr '/' expr		{ if ($3 == 0) YYABORT; $$ = $1 / $3; }
	| expr '%' expr		{ if ((long)$3 == 0) YYABORT; $$ = (long)$1 % (long)$3; }
	| '-' expr %prec NEG	{ $$ = -$2; }
	| '(' expr ')'		{ $$ = $2; }
	;

%%

static int expr__symbol(YYSTYPE *res, const char *p, const char **pp)
{
	char *dst = res->id;
	const char *s = p;

	while (isalnum(*p) || *p == '_' || *p == '.') {
		if (p - s >= MAXIDLEN)
			return -1;
		*dst++ = *p++;
	}
	*dst = 0;
	*pp = p;
	return ID;
}

static int expr__lex(YYSTYPE *res, const char **pp)
{
	int tok;
	const char *s;
	const char *p = *pp;

	while (isspace(*p))
		p++;
	s = p;
	switch (*p++) {
	case 'a' ... 'z':
	case 'A' ... 'Z':
		return expr__symbol(res, p - 1, pp);
	case '0' ... '9': case '.':
		res->num = strtod(s, (char **)&p);
		tok = NUMBER;
		break;
	default:
		tok = *s;
		break;
	}
	*pp = p;
	return tok;
}

/* Caller must make sure id is allocated */
void expr__add_id(struct parse_ctx *ctx, const char *name, double val)
{
	int idx;
	assert(ctx->num_ids < MAX_PARSE_ID);
	idx = ctx->num_ids++;
	ctx->ids[idx].name = name;
	ctx->ids[idx].val = val;
}

void expr__ctx_init(struct parse_ctx *ctx)
{
	ctx->num_ids = 0;
}

int expr__find_other(const char *p, const char *one, const char ***other,
		     int *num_otherp)
{
	const char *orig = p;
	int err = -1;
	int num_other;

	*other = malloc((EXPR_MAX_OTHER + 1) * sizeof(char *));
	if (!*other)
		return -1;

	num_other = 0;
	for (;;) {
		YYSTYPE val;
		int tok = expr__lex(&val, &p);
		if (tok == 0) {
			err = 0;
			break;
		}
		if (tok == ID && strcasecmp(one, val.id)) {
			if (num_other >= EXPR_MAX_OTHER - 1) {
				pr_debug("Too many extra events in %s\n", orig);
				break;
			}
			(*other)[num_other] = strdup(val.id);
			if (!(*other)[num_other])
				return -1;
			num_other++;
		}
	}
	(*other)[num_other] = NULL;
	*num_otherp = num_other;
	if (err) {
		*num_otherp = 0;
		free(*other);
		*other = NULL;
	}
	return err;
}
