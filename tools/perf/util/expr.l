%option prefix="expr_"
%option reentrant
%option bison-bridge

%{
#include <linux/compiler.h>
#include "expr.h"
#include "expr-bison.h"

char *expr_get_text(yyscan_t yyscanner);
YYSTYPE *expr_get_lval(yyscan_t yyscanner);

static double __value(YYSTYPE *yylval, char *str, int token)
{
	double num;

	errno = 0;
	num = strtod(str, NULL);
	if (errno)
		return EXPR_ERROR;

	yylval->num = num;
	return token;
}

static int value(yyscan_t scanner)
{
	YYSTYPE *yylval = expr_get_lval(scanner);
	char *text = expr_get_text(scanner);

	return __value(yylval, text, NUMBER);
}

/*
 * Allow @ instead of / to be able to specify pmu/event/ without
 * conflicts with normal division.
 */
static char *normalize(char *str, int runtime)
{
	char *ret = str;
	char *dst = str;

	while (*str) {
		if (*str == '@')
			*dst++ = '/';
		else if (*str == '\\')
			*dst++ = *++str;
		 else if (*str == '?') {
			char *paramval;
			int i = 0;
			int size = asprintf(&paramval, "%d", runtime);

			if (size < 0)
				*dst++ = '0';
			else {
				while (i < size)
					*dst++ = paramval[i++];
				free(paramval);
			}
		}
		else
			*dst++ = *str;
		str++;
	}

	*dst = 0x0;
	return ret;
}

static int str(yyscan_t scanner, int token, int runtime)
{
	YYSTYPE *yylval = expr_get_lval(scanner);
	char *text = expr_get_text(scanner);

	yylval->str = normalize(strdup(text), runtime);
	if (!yylval->str)
		return EXPR_ERROR;

	yylval->str = normalize(yylval->str, runtime);
	return token;
}
%}

number		([0-9]+\.?[0-9]*|[0-9]*\.?[0-9]+)

sch		[-,=]
spec		\\{sch}
sym		[0-9a-zA-Z_\.:@?]+
symbol		({spec}|{sym})+

%%
	struct expr_scanner_ctx *sctx = expr_get_extra(yyscanner);

	{
		int start_token = sctx->start_token;

		if (sctx->start_token) {
			sctx->start_token = 0;
			return start_token;
		}
	}

max		{ return MAX; }
min		{ return MIN; }
if		{ return IF; }
else		{ return ELSE; }
#smt_on		{ return SMT_ON; }
{number}	{ return value(yyscanner); }
{symbol}	{ return str(yyscanner, ID, sctx->runtime); }
"|"		{ return '|'; }
"^"		{ return '^'; }
"&"		{ return '&'; }
"-"		{ return '-'; }
"+"		{ return '+'; }
"*"		{ return '*'; }
"/"		{ return '/'; }
"%"		{ return '%'; }
"("		{ return '('; }
")"		{ return ')'; }
","		{ return ','; }
.		{ }
%%

int expr_wrap(void *scanner __maybe_unused)
{
	return 1;
}
