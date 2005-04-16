%option backup nostdinit noyywrap never-interactive full ecs
%option 8bit backup nodefault perf-report perf-report
%x COMMAND HELP STRING PARAM
%{
/*
 * Copyright (C) 2002 Roman Zippel <zippel@linux-m68k.org>
 * Released under the terms of the GNU GPL v2.0.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

#define START_STRSIZE	16

char *text;
static char *text_ptr;
static int text_size, text_asize;

struct buffer {
        struct buffer *parent;
        YY_BUFFER_STATE state;
};

struct buffer *current_buf;

static int last_ts, first_ts;

static void zconf_endhelp(void);
static struct buffer *zconf_endfile(void);

void new_string(void)
{
	text = malloc(START_STRSIZE);
	text_asize = START_STRSIZE;
	text_ptr = text;
	text_size = 0;
	*text_ptr = 0;
}

void append_string(const char *str, int size)
{
	int new_size = text_size + size + 1;
	if (new_size > text_asize) {
		text = realloc(text, new_size);
		text_asize = new_size;
		text_ptr = text + text_size;
	}
	memcpy(text_ptr, str, size);
	text_ptr += size;
	text_size += size;
	*text_ptr = 0;
}

void alloc_string(const char *str, int size)
{
	text = malloc(size + 1);
	memcpy(text, str, size);
	text[size] = 0;
}
%}

ws	[ \n\t]
n	[A-Za-z0-9_]

%%
	int str = 0;
	int ts, i;

[ \t]*#.*\n	current_file->lineno++;
[ \t]*#.*

[ \t]*\n	current_file->lineno++; return T_EOL;

[ \t]+	{
	BEGIN(COMMAND);
}

.	{
	unput(yytext[0]);
	BEGIN(COMMAND);
}


<COMMAND>{
	"mainmenu"		BEGIN(PARAM); return T_MAINMENU;
	"menu"			BEGIN(PARAM); return T_MENU;
	"endmenu"		BEGIN(PARAM); return T_ENDMENU;
	"source"		BEGIN(PARAM); return T_SOURCE;
	"choice"		BEGIN(PARAM); return T_CHOICE;
	"endchoice"		BEGIN(PARAM); return T_ENDCHOICE;
	"comment"		BEGIN(PARAM); return T_COMMENT;
	"config"		BEGIN(PARAM); return T_CONFIG;
	"menuconfig"		BEGIN(PARAM); return T_MENUCONFIG;
	"help"			BEGIN(PARAM); return T_HELP;
	"if"			BEGIN(PARAM); return T_IF;
	"endif"			BEGIN(PARAM); return T_ENDIF;
	"depends"		BEGIN(PARAM); return T_DEPENDS;
	"requires"		BEGIN(PARAM); return T_REQUIRES;
	"optional"		BEGIN(PARAM); return T_OPTIONAL;
	"default"		BEGIN(PARAM); return T_DEFAULT;
	"prompt"		BEGIN(PARAM); return T_PROMPT;
	"tristate"		BEGIN(PARAM); return T_TRISTATE;
	"def_tristate"		BEGIN(PARAM); return T_DEF_TRISTATE;
	"bool"			BEGIN(PARAM); return T_BOOLEAN;
	"boolean"		BEGIN(PARAM); return T_BOOLEAN;
	"def_bool"		BEGIN(PARAM); return T_DEF_BOOLEAN;
	"def_boolean"		BEGIN(PARAM); return T_DEF_BOOLEAN;
	"int"			BEGIN(PARAM); return T_INT;
	"hex"			BEGIN(PARAM); return T_HEX;
	"string"		BEGIN(PARAM); return T_STRING;
	"select"		BEGIN(PARAM); return T_SELECT;
	"enable"		BEGIN(PARAM); return T_SELECT;
	"range"			BEGIN(PARAM); return T_RANGE;
	{n}+	{
		alloc_string(yytext, yyleng);
		zconflval.string = text;
		return T_WORD;
	}
	.
	\n	current_file->lineno++; BEGIN(INITIAL);
}

<PARAM>{
	"&&"	return T_AND;
	"||"	return T_OR;
	"("	return T_OPEN_PAREN;
	")"	return T_CLOSE_PAREN;
	"!"	return T_NOT;
	"="	return T_EQUAL;
	"!="	return T_UNEQUAL;
	"if"	return T_IF;
	"on"	return T_ON;
	\"|\'	{
		str = yytext[0];
		new_string();
		BEGIN(STRING);
	}
	\n	BEGIN(INITIAL); current_file->lineno++; return T_EOL;
	---	/* ignore */
	({n}|[-/.])+	{
		alloc_string(yytext, yyleng);
		zconflval.string = text;
		return T_WORD;
	}
	#.*	/* comment */
	\\\n	current_file->lineno++;
	.
	<<EOF>> {
		BEGIN(INITIAL);
	}
}

<STRING>{
	[^'"\\\n]+/\n	{
		append_string(yytext, yyleng);
		zconflval.string = text;
		return T_WORD_QUOTE;
	}
	[^'"\\\n]+	{
		append_string(yytext, yyleng);
	}
	\\.?/\n	{
		append_string(yytext + 1, yyleng - 1);
		zconflval.string = text;
		return T_WORD_QUOTE;
	}
	\\.?	{
		append_string(yytext + 1, yyleng - 1);
	}
	\'|\"	{
		if (str == yytext[0]) {
			BEGIN(PARAM);
			zconflval.string = text;
			return T_WORD_QUOTE;
		} else
			append_string(yytext, 1);
	}
	\n	{
		printf("%s:%d:warning: multi-line strings not supported\n", zconf_curname(), zconf_lineno());
		current_file->lineno++;
		BEGIN(INITIAL);
		return T_EOL;
	}
	<<EOF>>	{
		BEGIN(INITIAL);
	}
}

<HELP>{
	[ \t]+	{
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
		current_file->lineno++;
		zconf_endhelp();
		return T_HELPTEXT;
	}
	[ \t]*\n	{
		current_file->lineno++;
		append_string("\n", 1);
	}
	[^ \t\n].* {
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
	if (current_buf) {
		zconf_endfile();
		return T_EOF;
	}
	fclose(yyin);
	yyterminate();
}

%%
void zconf_starthelp(void)
{
	new_string();
	last_ts = first_ts = 0;
	BEGIN(HELP);
}

static void zconf_endhelp(void)
{
	zconflval.string = text;
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
	if (!f && name[0] != '/') {
		env = getenv(SRCTREE);
		if (env) {
			sprintf(fullname, "%s/%s", env, name);
			f = fopen(fullname, "r");
		}
	}
	return f;
}

void zconf_initscan(const char *name)
{
	yyin = zconf_fopen(name);
	if (!yyin) {
		printf("can't find file %s\n", name);
		exit(1);
	}

	current_buf = malloc(sizeof(*current_buf));
	memset(current_buf, 0, sizeof(*current_buf));

	current_file = file_lookup(name);
	current_file->lineno = 1;
	current_file->flags = FILE_BUSY;
}

void zconf_nextfile(const char *name)
{
	struct file *file = file_lookup(name);
	struct buffer *buf = malloc(sizeof(*buf));
	memset(buf, 0, sizeof(*buf));

	current_buf->state = YY_CURRENT_BUFFER;
	yyin = zconf_fopen(name);
	if (!yyin) {
		printf("%s:%d: can't open file \"%s\"\n", zconf_curname(), zconf_lineno(), name);
		exit(1);
	}
	yy_switch_to_buffer(yy_create_buffer(yyin, YY_BUF_SIZE));
	buf->parent = current_buf;
	current_buf = buf;

	if (file->flags & FILE_BUSY) {
		printf("recursive scan (%s)?\n", name);
		exit(1);
	}
	if (file->flags & FILE_SCANNED) {
		printf("file %s already scanned?\n", name);
		exit(1);
	}
	file->flags |= FILE_BUSY;
	file->lineno = 1;
	file->parent = current_file;
	current_file = file;
}

static struct buffer *zconf_endfile(void)
{
	struct buffer *parent;

	current_file->flags |= FILE_SCANNED;
	current_file->flags &= ~FILE_BUSY;
	current_file = current_file->parent;

	parent = current_buf->parent;
	if (parent) {
		fclose(yyin);
		yy_delete_buffer(YY_CURRENT_BUFFER);
		yy_switch_to_buffer(parent->state);
	}
	free(current_buf);
	current_buf = parent;

	return parent;
}

int zconf_lineno(void)
{
	if (current_buf)
		return current_file->lineno - 1;
	else
		return 0;
}

char *zconf_curname(void)
{
	if (current_buf)
		return current_file->name;
	else
		return "<none>";
}
