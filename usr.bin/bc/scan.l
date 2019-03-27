%{
/*      $OpenBSD: scan.l,v 1.28 2013/09/19 16:12:01 otto Exp $	*/

/*
 * Copyright (c) 2003, Otto Moerbeek <otto@drijf.net>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <errno.h>
#include <histedit.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"
#include "bc.h"
#include "pathnames.h"

int		lineno;
bool		interactive;

HistEvent	 he;
EditLine	*el;
History		*hist;

static char	*strbuf = NULL;
static size_t	strbuf_sz = 1;
static bool	dot_seen;
static int	use_el;
static volatile sig_atomic_t skipchars;

static void	init_strbuf(void);
static void	add_str(const char *);

static int	 bc_yyinput(char *, int);

#define YY_DECL	int yylex(void)
#define YY_NO_INPUT
#undef YY_INPUT
#define YY_INPUT(buf,retval,max) \
	(retval = bc_yyinput(buf, max))

%}

%option always-interactive

DIGIT		[0-9A-F]
ALPHA		[a-z_]
ALPHANUM	[a-z_0-9]

%x		comment string number

%%

"/*"		BEGIN(comment);
<comment>{
	"*/"	BEGIN(INITIAL);
	\n	lineno++;
	\*	;
	[^*\n]+	;
	<<EOF>>	fatal("end of file in comment");
}

\"		BEGIN(string); init_strbuf();
<string>{
	[^"\n\\\[\]]+	add_str(yytext);
	\[	add_str("\\[");
	\]	add_str("\\]");
	\\	add_str("\\\\");
	\n	add_str("\n"); lineno++;
	\"	BEGIN(INITIAL); yylval.str = strbuf; return STRING;
	<<EOF>>	fatal("end of file in string");
}

{DIGIT}+	{
			BEGIN(number);
			dot_seen = false;
			init_strbuf();
			add_str(yytext);
		}
\.		{
			BEGIN(number);
			dot_seen = true;
			init_strbuf();
			add_str(".");
		}
<number>{
	{DIGIT}+	add_str(yytext);
	\.	{
			if (dot_seen) {
				BEGIN(INITIAL);
				yylval.str = strbuf;
				unput('.');
				return NUMBER;
			} else {
				dot_seen = true;
				add_str(".");
			}
		}
	\\\n[ \t]*	lineno++;
	[^0-9A-F\.]	{
			BEGIN(INITIAL);
			unput(yytext[0]);
			if (strcmp(strbuf, ".") == 0)
				return DOT;
			else {
				yylval.str = strbuf;
				return NUMBER;
			}
		}
}

"auto"		return AUTO;
"break"		return BREAK;
"continue"	return CONTINUE;
"define"	return DEFINE;
"else"		return ELSE;
"ibase"		return IBASE;
"if"		return IF;
"last"		return DOT;
"for"		return FOR;
"length"	return LENGTH;
"obase"		return OBASE;
"print"		return PRINT;
"quit"		return QUIT;
"return"	return RETURN;
"scale"		return SCALE;
"sqrt"		return SQRT;
"while"		return WHILE;

"^"		return EXPONENT;
"*"		return MULTIPLY;
"/"		return DIVIDE;
"%"		return REMAINDER;

"!"		return BOOL_NOT;
"&&"		return BOOL_AND;
"||"		return BOOL_OR;

"+"		return PLUS;
"-"		return MINUS;

"++"		return INCR;
"--"		return DECR;

"="		yylval.str = ""; return ASSIGN_OP;
"+="		yylval.str = "+"; return ASSIGN_OP;
"-="		yylval.str = "-"; return ASSIGN_OP;
"*="		yylval.str = "*"; return ASSIGN_OP;
"/="		yylval.str = "/"; return ASSIGN_OP;
"%="		yylval.str = "%"; return ASSIGN_OP;
"^="		yylval.str = "^"; return ASSIGN_OP;

"=="		return EQUALS;
"<="		return LESS_EQ;
">="		return GREATER_EQ;
"!="		return UNEQUALS;
"<"		return LESS;
">"		return GREATER;

","		return COMMA;
";"		return SEMICOLON;

"("		return LPAR;
")"		return RPAR;

"["		return LBRACKET;
"]"		return RBRACKET;

"{"		return LBRACE;
"}"		return RBRACE;

{ALPHA}{ALPHANUM}* {
			/* alloc an extra byte for the type marker */
			char *p = malloc(yyleng + 2);
			if (p == NULL)
				err(1, NULL);
			strlcpy(p, yytext, yyleng + 1);
			yylval.astr = p;
			return LETTER;
		}

\\\n		lineno++;
\n		lineno++; return NEWLINE;

#[^\n]*		;
[ \t]		;
<<EOF>>		return QUIT;
.		yyerror("illegal character");

%%

static void
init_strbuf(void)
{
	if (strbuf == NULL) {
		strbuf = malloc(strbuf_sz);
		if (strbuf == NULL)
			err(1, NULL);
	}
	strbuf[0] = '\0';
}

static void
add_str(const char *str)
{
	size_t arglen;

	arglen = strlen(str);

	if (strlen(strbuf) + arglen + 1 > strbuf_sz) {
		size_t newsize;
		char *p;

		newsize = strbuf_sz + arglen + 1;
		p = realloc(strbuf, newsize);
		if (p == NULL) {
			free(strbuf);
			err(1, NULL);
		}
		strbuf_sz = newsize;
		strbuf = p;
	}
	strlcat(strbuf, str, strbuf_sz);
}

/* ARGSUSED */
void
abort_line(int sig __unused)
{
	static const char str1[] = "[\n]P\n";
	static const char str2[] = "[^C\n]P\n";
	int save_errno;
	const LineInfo *info;

	save_errno = errno;
	if (use_el) {
		write(STDOUT_FILENO, str2, sizeof(str2) - 1);
		info = el_line(el);
		skipchars = info->lastchar - info->buffer;
	} else
		write(STDOUT_FILENO, str1, sizeof(str1) - 1);
	errno = save_errno;
}

/*
 * Avoid the echo of ^D by the default code of editline and take
 * into account skipchars to make ^D work when the cursor is at start of
 * line after a ^C.
 */
unsigned char
bc_eof(EditLine *e, int ch __unused)
{
	const struct lineinfo *info = el_line(e);

	if (info->buffer + skipchars == info->cursor &&
	    info->cursor == info->lastchar)
		return (CC_EOF);
	else
		return (CC_ERROR);
}

int
yywrap(void)
{
	static int state;
	static YY_BUFFER_STATE buf;

	if (fileindex == 0 && sargc > 0 && strcmp(sargv[0], _PATH_LIBB) == 0) {
		filename = sargv[fileindex++];
		yyin = fopen(filename, "r");
		lineno = 1;
		if (yyin == NULL)
			err(1, "cannot open %s", filename);
		return (0);
	}
	if (state == 0 && cmdexpr[0] != '\0') {
		buf = yy_scan_string(cmdexpr);
		state++;
		lineno = 1;
		filename = "command line";
		return (0);
	} else if (state == 1) {
		yy_delete_buffer(buf);
		free(cmdexpr);
		state++;
	}
	if (yyin != NULL && yyin != stdin)
		fclose(yyin);
	if (fileindex < sargc) {
		filename = sargv[fileindex++];
		yyin = fopen(filename, "r");
		lineno = 1;
		if (yyin == NULL)
			err(1, "cannot open %s", filename);
		return (0);
	} else if (fileindex == sargc) {
		fileindex++;
		yyin = stdin;
		if (interactive) {
			signal(SIGINT, abort_line);
			signal(SIGTSTP, tstpcont);
		}
		lineno = 1;
		filename = "stdin";
		return (0);
	}
	return (1);
}

static int
bc_yyinput(char *buf, int maxlen)
{
	int num;

	if (el != NULL)
		el_get(el, EL_EDITMODE, &use_el);
		
	if (yyin == stdin && interactive && use_el) {
		const char *bp;
		sigset_t oset, nset;

		if ((bp = el_gets(el, &num)) == NULL || num == 0)
			return (0);
		sigemptyset(&nset);
		sigaddset(&nset, SIGINT);
		sigprocmask(SIG_BLOCK, &nset, &oset);
		if (skipchars < num) {
			bp += skipchars;
			num -= skipchars;
		}
		skipchars = 0;
		sigprocmask(SIG_SETMASK, &oset, NULL);
		if (num > maxlen) {
			el_push(el, bp + maxlen);
			num = maxlen;
		}
		memcpy(buf, bp, num);
		history(hist, &he, H_ENTER, bp);
		el_get(el, EL_EDITMODE, &use_el);
	} else {
		int c = '*';
		for (num = 0; num < maxlen &&
		    (c = getc(yyin)) != EOF && c != '\n'; ++num)
			buf[num] = (char) c;
		if (c == '\n')
			buf[num++] = (char) c;
		if (c == EOF && ferror(yyin))
			YY_FATAL_ERROR( "input in flex scanner failed" );
	}
	return (num);
}


