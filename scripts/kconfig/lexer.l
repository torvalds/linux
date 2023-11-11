/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 */
%option nostdinit noyywrap never-interactive full ecs
%option 8bit nodefault yylineno
%x ASSIGN_VAL HELP STRING
%{

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lkc.h"
#include "parser.tab.h"

#define YY_DECL		static int yylex1(void)

#define START_STRSIZE	16

static struct {
	struct file *file;
	int lineno;
} current_pos;

static int prev_prev_token = T_EOL;
static int prev_token = T_EOL;
static char *text;
static int text_size, text_asize;

struct buffer {
	struct buffer *parent;
	YY_BUFFER_STATE state;
};

static struct buffer *current_buf;

static int last_ts, first_ts;

static char *expand_token(const char *in, size_t n);
static void append_expanded_string(const char *in);
static void zconf_endhelp(void);
static void zconf_endfile(void);

static void new_string(void)
{
	text = xmalloc(START_STRSIZE);
	text_asize = START_STRSIZE;
	text_size = 0;
	*text = 0;
}

static void append_string(const char *str, int size)
{
	int new_size = text_size + size + 1;
	if (new_size > text_asize) {
		new_size += START_STRSIZE - 1;
		new_size &= -START_STRSIZE;
		text = xrealloc(text, new_size);
		text_asize = new_size;
	}
	memcpy(text + text_size, str, size);
	text_size += size;
	text[text_size] = 0;
}

static void alloc_string(const char *str, int size)
{
	text = xmalloc(size + 1);
	memcpy(text, str, size);
	text[size] = 0;
}

static void warn_ignored_character(char chr)
{
	fprintf(stderr,
	        "%s:%d:warning: ignoring unsupported character '%c'\n",
	        current_file->name, yylineno, chr);
}
%}

n	[A-Za-z0-9_-]

%%
	char open_quote = 0;

#.*			/* ignore comment */
[ \t]*			/* whitespaces */
\\\n			/* escaped new line */
\n			return T_EOL;
"bool"			return T_BOOL;
"choice"		return T_CHOICE;
"comment"		return T_COMMENT;
"config"		return T_CONFIG;
"def_bool"		return T_DEF_BOOL;
"def_tristate"		return T_DEF_TRISTATE;
"default"		return T_DEFAULT;
"depends"		return T_DEPENDS;
"endchoice"		return T_ENDCHOICE;
"endif"			return T_ENDIF;
"endmenu"		return T_ENDMENU;
"help"			return T_HELP;
"hex"			return T_HEX;
"if"			return T_IF;
"imply"			return T_IMPLY;
"int"			return T_INT;
"mainmenu"		return T_MAINMENU;
"menu"			return T_MENU;
"menuconfig"		return T_MENUCONFIG;
"modules"		return T_MODULES;
"on"			return T_ON;
"optional"		return T_OPTIONAL;
"prompt"		return T_PROMPT;
"range"			return T_RANGE;
"select"		return T_SELECT;
"source"		return T_SOURCE;
"string"		return T_STRING;
"tristate"		return T_TRISTATE;
"visible"		return T_VISIBLE;
"||"			return T_OR;
"&&"			return T_AND;
"="			return T_EQUAL;
"!="			return T_UNEQUAL;
"<"			return T_LESS;
"<="			return T_LESS_EQUAL;
">"			return T_GREATER;
">="			return T_GREATER_EQUAL;
"!"			return T_NOT;
"("			return T_OPEN_PAREN;
")"			return T_CLOSE_PAREN;
":="			return T_COLON_EQUAL;
"+="			return T_PLUS_EQUAL;
\"|\'			{
				open_quote = yytext[0];
				new_string();
				BEGIN(STRING);
			}
{n}+			{
				alloc_string(yytext, yyleng);
				yylval.string = text;
				return T_WORD;
			}
({n}|$)+		{
				/* this token includes at least one '$' */
				yylval.string = expand_token(yytext, yyleng);
				if (strlen(yylval.string))
					return T_WORD;
				free(yylval.string);
			}
.			warn_ignored_character(*yytext);

<ASSIGN_VAL>{
	[^[:blank:]\n]+.*	{
		alloc_string(yytext, yyleng);
		yylval.string = text;
		return T_ASSIGN_VAL;
	}
	\n	{ BEGIN(INITIAL); return T_EOL; }
	.
}

<STRING>{
	"$".*	append_expanded_string(yytext);
	[^$'"\\\n]+	{
		append_string(yytext, yyleng);
	}
	\\.?	{
		append_string(yytext + 1, yyleng - 1);
	}
	\'|\"	{
		if (open_quote == yytext[0]) {
			BEGIN(INITIAL);
			yylval.string = text;
			return T_WORD_QUOTE;
		} else
			append_string(yytext, 1);
	}
	\n	{
		fprintf(stderr,
			"%s:%d:warning: multi-line strings not supported\n",
			zconf_curname(), zconf_lineno());
		unput('\n');
		BEGIN(INITIAL);
		yylval.string = text;
		return T_WORD_QUOTE;
	}
	<<EOF>>	{
		BEGIN(INITIAL);
		yylval.string = text;
		return T_WORD_QUOTE;
	}
}

<HELP>{
	[ \t]+	{
		int ts, i;

		ts = 0;
		for (i = 0; i < yyleng; i++) {
			if (yytext[i] == '\t')
				ts = (ts & ~7) + 8;
			else
				ts++;
		}
		last_ts = ts;
		if (first_ts) {
			if (ts < first_ts) {
				zconf_endhelp();
				return T_HELPTEXT;
			}
			ts -= first_ts;
			while (ts > 8) {
				append_string("        ", 8);
				ts -= 8;
			}
			append_string("        ", ts);
		}
	}
	[ \t]*\n/[^ \t\n] {
		zconf_endhelp();
		return T_HELPTEXT;
	}
	[ \t]*\n	{
		append_string("\n", 1);
	}
	[^ \t\n].* {
		while (yyleng) {
			if ((yytext[yyleng-1] != ' ') && (yytext[yyleng-1] != '\t'))
				break;
			yyleng--;
		}
		append_string(yytext, yyleng);
		if (!first_ts)
			first_ts = last_ts;
	}
	<<EOF>>	{
		zconf_endhelp();
		return T_HELPTEXT;
	}
}

<<EOF>>	{
	BEGIN(INITIAL);

	if (prev_token != T_EOL && prev_token != T_HELPTEXT)
		fprintf(stderr, "%s:%d:warning: no new line at end of file\n",
			current_file->name, yylineno);

	if (current_file) {
		zconf_endfile();
		return T_EOL;
	}
	fclose(yyin);
	yyterminate();
}

%%

/* second stage lexer */
int yylex(void)
{
	int token;

repeat:
	token = yylex1();

	if (prev_token == T_EOL || prev_token == T_HELPTEXT) {
		if (token == T_EOL) {
			/* Do not pass unneeded T_EOL to the parser. */
			goto repeat;
		} else {
			/*
			 * For the parser, update file/lineno at the first token
			 * of each statement. Generally, \n is a statement
			 * terminator in Kconfig, but it is not always true
			 * because \n could be escaped by a backslash.
			 */
			current_pos.file = current_file;
			current_pos.lineno = yylineno;
		}
	}

	if (prev_prev_token == T_EOL && prev_token == T_WORD &&
	    (token == T_EQUAL || token == T_COLON_EQUAL || token == T_PLUS_EQUAL))
		BEGIN(ASSIGN_VAL);

	prev_prev_token = prev_token;
	prev_token = token;

	return token;
}

static char *expand_token(const char *in, size_t n)
{
	char *out;
	int c;
	char c2;
	const char *rest, *end;

	new_string();
	append_string(in, n);

	/* get the whole line because we do not know the end of token. */
	while ((c = input()) != EOF) {
		if (c == '\n') {
			unput(c);
			break;
		}
		c2 = c;
		append_string(&c2, 1);
	}

	rest = text;
	out = expand_one_token(&rest);

	/* push back unused characters to the input stream */
	end = rest + strlen(rest);
	while (end > rest)
		unput(*--end);

	free(text);

	return out;
}

static void append_expanded_string(const char *str)
{
	const char *end;
	char *res;

	str++;

	res = expand_dollar(&str);

	/* push back unused characters to the input stream */
	end = str + strlen(str);
	while (end > str)
		unput(*--end);

	append_string(res, strlen(res));

	free(res);
}

void zconf_starthelp(void)
{
	new_string();
	last_ts = first_ts = 0;
	BEGIN(HELP);
}

static void zconf_endhelp(void)
{
	yylval.string = text;
	BEGIN(INITIAL);
}


/*
 * Try to open specified file with following names:
 * ./name
 * $(srctree)/name
 * The latter is used when srctree is separate from objtree
 * when compiling the kernel.
 * Return NULL if file is not found.
 */
FILE *zconf_fopen(const char *name)
{
	char *env, fullname[PATH_MAX+1];
	FILE *f;

	f = fopen(name, "r");
	if (!f && name != NULL && name[0] != '/') {
		env = getenv(SRCTREE);
		if (env) {
			snprintf(fullname, sizeof(fullname),
				 "%s/%s", env, name);
			f = fopen(fullname, "r");
		}
	}
	return f;
}

void zconf_initscan(const char *name)
{
	yyin = zconf_fopen(name);
	if (!yyin) {
		fprintf(stderr, "can't find file %s\n", name);
		exit(1);
	}

	current_buf = xmalloc(sizeof(*current_buf));
	memset(current_buf, 0, sizeof(*current_buf));

	current_file = file_lookup(name);
	yylineno = 1;
}

void zconf_nextfile(const char *name)
{
	struct file *iter;
	struct file *file = file_lookup(name);
	struct buffer *buf = xmalloc(sizeof(*buf));
	memset(buf, 0, sizeof(*buf));

	current_buf->state = YY_CURRENT_BUFFER;
	yyin = zconf_fopen(file->name);
	if (!yyin) {
		fprintf(stderr, "%s:%d: can't open file \"%s\"\n",
			zconf_curname(), zconf_lineno(), file->name);
		exit(1);
	}
	yy_switch_to_buffer(yy_create_buffer(yyin, YY_BUF_SIZE));
	buf->parent = current_buf;
	current_buf = buf;

	current_file->lineno = yylineno;
	file->parent = current_file;

	for (iter = current_file; iter; iter = iter->parent) {
		if (!strcmp(iter->name, file->name)) {
			fprintf(stderr,
				"Recursive inclusion detected.\n"
				"Inclusion path:\n"
				"  current file : %s\n", file->name);
			iter = file;
			do {
				iter = iter->parent;
				fprintf(stderr, "  included from: %s:%d\n",
					iter->name, iter->lineno - 1);
			} while (strcmp(iter->name, file->name));
			exit(1);
		}
	}

	yylineno = 1;
	current_file = file;
}

static void zconf_endfile(void)
{
	struct buffer *parent;

	current_file = current_file->parent;
	if (current_file)
		yylineno = current_file->lineno;

	parent = current_buf->parent;
	if (parent) {
		fclose(yyin);
		yy_delete_buffer(YY_CURRENT_BUFFER);
		yy_switch_to_buffer(parent->state);
	}
	free(current_buf);
	current_buf = parent;
}

int zconf_lineno(void)
{
	return current_pos.lineno;
}

const char *zconf_curname(void)
{
	return current_pos.file ? current_pos.file->name : "<none>";
}
