/*
 * (C) Copyright David Gibson <dwg@au1.ibm.com>, IBM Corporation.  2005.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *                                                                   USA
 */

%option noyywrap noinput nounput yylineno

%x INCLUDE
%x BYTESTRING
%x PROPNODENAME
%s V1

PROPNODECHAR	[a-zA-Z0-9,._+*#?@-]
PATHCHAR	({PROPNODECHAR}|[/])
LABEL		[a-zA-Z_][a-zA-Z0-9_]*
STRING		\"([^\\"]|\\.)*\"
WS		[[:space:]]
COMMENT		"/*"([^*]|\*+[^*/])*\*+"/"
LINECOMMENT	"//".*\n

%{
#include "dtc.h"
#include "srcpos.h"
#include "dtc-parser.tab.h"


/*#define LEXDEBUG	1*/

#ifdef LEXDEBUG
#define DPRINT(fmt, ...)	fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DPRINT(fmt, ...)	do { } while (0)
#endif

static int dts_version; /* = 0 */

#define BEGIN_DEFAULT()	if (dts_version == 0) { \
				DPRINT("<INITIAL>\n"); \
				BEGIN(INITIAL); \
			} else { \
				DPRINT("<V1>\n"); \
				BEGIN(V1); \
			}

static void push_input_file(const char *filename);
static int pop_input_file(void);
%}

%%
<*>"/include/"{WS}*{STRING} {
			char *name = strchr(yytext, '\"') + 1;
			yytext[yyleng-1] = '\0';
			push_input_file(name);
		}

<*><<EOF>>		{
			if (!pop_input_file()) {
				yyterminate();
			}
		}

<*>{STRING}	{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("String: %s\n", yytext);
			yylval.data = data_copy_escape_string(yytext+1,
					yyleng-2);
			yylloc.first_line = yylineno;
			return DT_STRING;
		}

<*>"/dts-v1/"	{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("Keyword: /dts-v1/\n");
			dts_version = 1;
			BEGIN_DEFAULT();
			return DT_V1;
		}

<*>"/memreserve/"	{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("Keyword: /memreserve/\n");
			BEGIN_DEFAULT();
			return DT_MEMRESERVE;
		}

<*>{LABEL}:	{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("Label: %s\n", yytext);
			yylval.labelref = strdup(yytext);
			yylval.labelref[yyleng-1] = '\0';
			return DT_LABEL;
		}

<INITIAL>[bodh]# {
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			if (*yytext == 'b')
				yylval.cbase = 2;
			else if (*yytext == 'o')
				yylval.cbase = 8;
			else if (*yytext == 'd')
				yylval.cbase = 10;
			else
				yylval.cbase = 16;
			DPRINT("Base: %d\n", yylval.cbase);
			return DT_BASE;
		}

<INITIAL>[0-9a-fA-F]+	{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			yylval.literal = strdup(yytext);
			DPRINT("Literal: '%s'\n", yylval.literal);
			return DT_LEGACYLITERAL;
		}

<V1>[0-9]+|0[xX][0-9a-fA-F]+      {
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			yylval.literal = strdup(yytext);
			DPRINT("Literal: '%s'\n", yylval.literal);
			return DT_LITERAL;
		}

\&{LABEL}	{	/* label reference */
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("Ref: %s\n", yytext+1);
			yylval.labelref = strdup(yytext+1);
			return DT_REF;
		}

"&{/"{PATHCHAR}+\}	{	/* new-style path reference */
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			yytext[yyleng-1] = '\0';
			DPRINT("Ref: %s\n", yytext+2);
			yylval.labelref = strdup(yytext+2);
			return DT_REF;
		}

<INITIAL>"&/"{PATHCHAR}+ {	/* old-style path reference */
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("Ref: %s\n", yytext+1);
			yylval.labelref = strdup(yytext+1);
			return DT_REF;
		}

<BYTESTRING>[0-9a-fA-F]{2} {
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			yylval.byte = strtol(yytext, NULL, 16);
			DPRINT("Byte: %02x\n", (int)yylval.byte);
			return DT_BYTE;
		}

<BYTESTRING>"]"	{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("/BYTESTRING\n");
			BEGIN_DEFAULT();
			return ']';
		}

<PROPNODENAME>{PROPNODECHAR}+ {
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("PropNodeName: %s\n", yytext);
			yylval.propnodename = strdup(yytext);
			BEGIN_DEFAULT();
			return DT_PROPNODENAME;
		}

"/incbin/"	{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("Binary Include\n");
			return DT_INCBIN;
		}

<*>{WS}+	/* eat whitespace */
<*>{COMMENT}+	/* eat C-style comments */
<*>{LINECOMMENT}+ /* eat C++-style comments */

<*>.		{
			yylloc.file = srcpos_file;
			yylloc.first_line = yylineno;
			DPRINT("Char: %c (\\x%02x)\n", yytext[0],
				(unsigned)yytext[0]);
			if (yytext[0] == '[') {
				DPRINT("<BYTESTRING>\n");
				BEGIN(BYTESTRING);
			}
			if ((yytext[0] == '{')
			    || (yytext[0] == ';')) {
				DPRINT("<PROPNODENAME>\n");
				BEGIN(PROPNODENAME);
			}
			return yytext[0];
		}

%%


/*
 * Stack of nested include file contexts.
 */

struct incl_file {
	struct dtc_file *file;
	YY_BUFFER_STATE yy_prev_buf;
	int yy_prev_lineno;
	struct incl_file *prev;
};

static struct incl_file *incl_file_stack;


/*
 * Detect infinite include recursion.
 */
#define MAX_INCLUDE_DEPTH	(100)

static int incl_depth = 0;


static void push_input_file(const char *filename)
{
	struct incl_file *incl_file;
	struct dtc_file *newfile;
	struct search_path search, *searchptr = NULL;

	assert(filename);

	if (incl_depth++ >= MAX_INCLUDE_DEPTH)
		die("Includes nested too deeply");

	if (srcpos_file) {
		search.dir = srcpos_file->dir;
		search.next = NULL;
		search.prev = NULL;
		searchptr = &search;
	}

	newfile = dtc_open_file(filename, searchptr);

	incl_file = xmalloc(sizeof(struct incl_file));

	/*
	 * Save current context.
	 */
	incl_file->yy_prev_buf = YY_CURRENT_BUFFER;
	incl_file->yy_prev_lineno = yylineno;
	incl_file->file = srcpos_file;
	incl_file->prev = incl_file_stack;

	incl_file_stack = incl_file;

	/*
	 * Establish new context.
	 */
	srcpos_file = newfile;
	yylineno = 1;
	yyin = newfile->file;
	yy_switch_to_buffer(yy_create_buffer(yyin, YY_BUF_SIZE));
}


static int pop_input_file(void)
{
	struct incl_file *incl_file;

	if (incl_file_stack == 0)
		return 0;

	dtc_close_file(srcpos_file);

	/*
	 * Pop.
	 */
	--incl_depth;
	incl_file = incl_file_stack;
	incl_file_stack = incl_file->prev;

	/*
	 * Recover old context.
	 */
	yy_delete_buffer(YY_CURRENT_BUFFER);
	yy_switch_to_buffer(incl_file->yy_prev_buf);
	yylineno = incl_file->yy_prev_lineno;
	srcpos_file = incl_file->file;
	yyin = incl_file->file ? incl_file->file->file : NULL;

	/*
	 * Free old state.
	 */
	free(incl_file);

	return 1;
}
