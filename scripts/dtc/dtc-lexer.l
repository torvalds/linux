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

%x BYTESTRING
%x PROPNODENAME
%s V1

PROPNODECHAR	[a-zA-Z0-9,._+*#?@-]
PATHCHAR	({PROPNODECHAR}|[/])
LABEL		[a-zA-Z_][a-zA-Z0-9_]*
STRING		\"([^\\"]|\\.)*\"
CHAR_LITERAL	'([^']|\\')*'
WS		[[:space:]]
COMMENT		"/*"([^*]|\*+[^*/])*\*+"/"
LINECOMMENT	"//".*\n

%{
#include "dtc.h"
#include "srcpos.h"
#include "dtc-parser.tab.h"

YYLTYPE yylloc;
extern bool treesource_error;

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
static bool pop_input_file(void);
static void PRINTF(1, 2) lexical_error(const char *fmt, ...);

%}

%%
<*>"/include/"{WS}*{STRING} {
			char *name = strchr(yytext, '\"') + 1;
			yytext[yyleng-1] = '\0';
			push_input_file(name);
		}

<*>^"#"(line)?[ \t]+[0-9]+[ \t]+{STRING}([ \t]+[0-9]+)? {
			char *line, *fnstart, *fnend;
			struct data fn;
			/* skip text before line # */
			line = yytext;
			while (!isdigit((unsigned char)*line))
				line++;

			/* regexp ensures that first and list "
			 * in the whole yytext are those at
			 * beginning and end of the filename string */
			fnstart = memchr(yytext, '"', yyleng);
			for (fnend = yytext + yyleng - 1;
			     *fnend != '"'; fnend--)
				;
			assert(fnstart && fnend && (fnend > fnstart));

			fn = data_copy_escape_string(fnstart + 1,
						     fnend - fnstart - 1);

			/* Don't allow nuls in filenames */
			if (memchr(fn.val, '\0', fn.len - 1))
				lexical_error("nul in line number directive");

			/* -1 since #line is the number of the next line */
			srcpos_set_line(xstrdup(fn.val), atoi(line) - 1);
			data_free(fn);
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

<*>"/plugin/"	{
			DPRINT("Keyword: /plugin/\n");
			return DT_PLUGIN;
		}

<*>"/memreserve/"	{
			DPRINT("Keyword: /memreserve/\n");
			BEGIN_DEFAULT();
			return DT_MEMRESERVE;
		}

<*>"/bits/"	{
			DPRINT("Keyword: /bits/\n");
			BEGIN_DEFAULT();
			return DT_BITS;
		}

<*>"/delete-property/"	{
			DPRINT("Keyword: /delete-property/\n");
			DPRINT("<PROPNODENAME>\n");
			BEGIN(PROPNODENAME);
			return DT_DEL_PROP;
		}

<*>"/delete-node/"	{
			DPRINT("Keyword: /delete-node/\n");
			DPRINT("<PROPNODENAME>\n");
			BEGIN(PROPNODENAME);
			return DT_DEL_NODE;
		}

<*>"/omit-if-no-ref/"	{
			DPRINT("Keyword: /omit-if-no-ref/\n");
			DPRINT("<PROPNODENAME>\n");
			BEGIN(PROPNODENAME);
			return DT_OMIT_NO_REF;
		}

<*>{LABEL}:	{
			DPRINT("Label: %s\n", yytext);
			yylval.labelref = xstrdup(yytext);
			yylval.labelref[yyleng-1] = '\0';
			return DT_LABEL;
		}

<V1>([0-9]+|0[xX][0-9a-fA-F]+)(U|L|UL|LL|ULL)? {
			char *e;
			DPRINT("Integer Literal: '%s'\n", yytext);

			errno = 0;
			yylval.integer = strtoull(yytext, &e, 0);

			if (*e && e[strspn(e, "UL")]) {
				lexical_error("Bad integer literal '%s'",
					      yytext);
			}

			if (errno == ERANGE)
				lexical_error("Integer literal '%s' out of range",
					      yytext);
			else
				/* ERANGE is the only strtoull error triggerable
				 *  by strings matching the pattern */
				assert(errno == 0);
			return DT_LITERAL;
		}

<*>{CHAR_LITERAL}	{
			struct data d;
			DPRINT("Character literal: %s\n", yytext);

			d = data_copy_escape_string(yytext+1, yyleng-2);
			if (d.len == 1) {
				lexical_error("Empty character literal");
				yylval.integer = 0;
			} else {
				yylval.integer = (unsigned char)d.val[0];

				if (d.len > 2)
					lexical_error("Character literal has %d"
						      " characters instead of 1",
						      d.len - 1);
			}

			data_free(d);
			return DT_CHAR_LITERAL;
		}

<*>\&{LABEL}	{	/* label reference */
			DPRINT("Ref: %s\n", yytext+1);
			yylval.labelref = xstrdup(yytext+1);
			return DT_REF;
		}

<*>"&{/"{PATHCHAR}*\}	{	/* new-style path reference */
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

<PROPNODENAME>\\?{PROPNODECHAR}+ {
			DPRINT("PropNodeName: %s\n", yytext);
			yylval.propnodename = xstrdup((yytext[0] == '\\') ?
							yytext + 1 : yytext);
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

<*>"<<"		{ return DT_LSHIFT; };
<*>">>"		{ return DT_RSHIFT; };
<*>"<="		{ return DT_LE; };
<*>">="		{ return DT_GE; };
<*>"=="		{ return DT_EQ; };
<*>"!="		{ return DT_NE; };
<*>"&&"		{ return DT_AND; };
<*>"||"		{ return DT_OR; };

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


static bool pop_input_file(void)
{
	if (srcfile_pop() == 0)
		return false;

	yypop_buffer_state();
	yyin = current_srcfile->f;

	return true;
}

static void lexical_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	srcpos_verror(&yylloc, "Lexical error", fmt, ap);
	va_end(ap);

	treesource_error = true;
}
