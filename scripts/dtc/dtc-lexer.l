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

%option noyywrap nounput noinput never-interactive

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

YYLTYPE yylloc;

/* CAUTION: this will stop working if we ever use yyless() or yyunput() */
#define	YY_USER_ACTION \
	{ \
		srcpos_update(&yylloc, yytext, yyleng); \
	}

/*#define LEXDEBUG	1*/

#ifdef LEXDEBUG
#define DPRINT(fmt, ...)	fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DPRINT(fmt, ...)	do { } while (0)
#endif

static int dts_version = 1;

#define BEGIN_DEFAULT()		DPRINT("<V1>\n"); \
				BEGIN(V1); \

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
			DPRINT("String: %s\n", yytext);
			yylval.data = data_copy_escape_string(yytext+1,
					yyleng-2);
			return DT_STRING;
		}

<*>"/dts-v1/"	{
			DPRINT("Keyword: /dts-v1/\n");
			dts_version = 1;
			BEGIN_DEFAULT();
			return DT_V1;
		}

<*>"/memreserve/"	{
			DPRINT("Keyword: /memreserve/\n");
			BEGIN_DEFAULT();
			return DT_MEMRESERVE;
		}

<*>{LABEL}:	{
			DPRINT("Label: %s\n", yytext);
			yylval.labelref = xstrdup(yytext);
			yylval.labelref[yyleng-1] = '\0';
			return DT_LABEL;
		}

<V1>[0-9]+|0[xX][0-9a-fA-F]+      {
			yylval.literal = xstrdup(yytext);
			DPRINT("Literal: '%s'\n", yylval.literal);
			return DT_LITERAL;
		}

<*>\&{LABEL}	{	/* label reference */
			DPRINT("Ref: %s\n", yytext+1);
			yylval.labelref = xstrdup(yytext+1);
			return DT_REF;
		}

<*>"&{/"{PATHCHAR}+\}	{	/* new-style path reference */
			yytext[yyleng-1] = '\0';
			DPRINT("Ref: %s\n", yytext+2);
			yylval.labelref = xstrdup(yytext+2);
			return DT_REF;
		}

<BYTESTRING>[0-9a-fA-F]{2} {
			yylval.byte = strtol(yytext, NULL, 16);
			DPRINT("Byte: %02x\n", (int)yylval.byte);
			return DT_BYTE;
		}

<BYTESTRING>"]"	{
			DPRINT("/BYTESTRING\n");
			BEGIN_DEFAULT();
			return ']';
		}

<PROPNODENAME>{PROPNODECHAR}+ {
			DPRINT("PropNodeName: %s\n", yytext);
			yylval.propnodename = xstrdup(yytext);
			BEGIN_DEFAULT();
			return DT_PROPNODENAME;
		}

"/incbin/"	{
			DPRINT("Binary Include\n");
			return DT_INCBIN;
		}

<*>{WS}+	/* eat whitespace */
<*>{COMMENT}+	/* eat C-style comments */
<*>{LINECOMMENT}+ /* eat C++-style comments */

<*>.		{
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

static void push_input_file(const char *filename)
{
	assert(filename);

	srcfile_push(filename);

	yyin = current_srcfile->f;

	yypush_buffer_state(yy_create_buffer(yyin, YY_BUF_SIZE));
}


static int pop_input_file(void)
{
	if (srcfile_pop() == 0)
		return 0;

	yypop_buffer_state();
	yyin = current_srcfile->f;

	return 1;
}
