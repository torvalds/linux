%option prefix="expr_"
%option reentrant
%option bison-bridge

%{
#include <linux/compiler.h>
#include "expr.h"
#include "expr-bison.h"

char *expr_get_text(yyscan_t yyscanner);
YYSTYPE *expr_get_lval(yyscan_t yyscanner);

static int __value(YYSTYPE *yylval, char *str, int base, int token)
{
	u64 num;

	errno = 0;
	num = strtoull(str, NULL, base);
	if (errno)
		return EXPR_ERROR;

	yylval->num = num;
	return token;
}

static int value(yyscan_t scanner, int base)
{
	YYSTYPE *yylval = expr_get_lval(scanner);
	char *text = expr_get_text(scanner);

	return __value(yylval, text, base, NUMBER);
}

/*
 * Allow @ instead of / to be able to specify pmu/event/ without
 * conflicts with normal division.
 */
static char *normalize(char *str)
{
	char *ret = str;
	char *dst = str;

	while (*str) {
		if (*str == '@')
			*dst++ = '/';
		else if (*str == '\\')
			*dst++ = *++str;
		else
			*dst++ = *str;
		str++;
	}

	*dst = 0x0;
	return ret;
}

static int str(yyscan_t scanner, int token)
{
	YYSTYPE *yylval = expr_get_lval(scanner);
	char *text = expr_get_text(scanner);

	yylval->str = normalize(strdup(text));
	if (!yylval->str)
		return EXPR_ERROR;

	yylval->str = normalize(yylval->str);
	return token;
}
%}

number		[0-9]+

sch		[-,=]
spec		\\{sch}
sym		[0-9a-zA-Z_\.:@]+
symbol		{spec}*{sym}*{spec}*{sym}*

%%
	{
		int start_token;

		start_token = expr_get_extra(yyscanner);

		if (start_token) {
			expr_set_extra(NULL, yyscanner);
			return start_token;
		}
	}

max		{ return MAX; }
min		{ return MIN; }
if		{ return IF; }
else		{ return ELSE; }
#smt_on		{ return SMT_ON; }
{number}	{ return value(yyscanner, 10); }
{symbol}	{ return str(yyscanner, ID); }
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
