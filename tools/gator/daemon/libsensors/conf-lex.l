%{
/*
    conf-lex.l - Part of libsensors, a Linux library for reading sensor data.
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> 

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
    MA 02110-1301 USA.
*/

#include <stdlib.h>
#include <string.h>

#include "general.h"
#include "data.h"
#include "conf-parse.h"
#include "error.h"
#include "scanner.h"

static int buffer_count;
static int buffer_max;
static char *buffer;

char sensors_lex_error[100];

const char *sensors_yyfilename;
int sensors_yylineno;

#define buffer_malloc() sensors_malloc_array(&buffer,&buffer_count,\
                                             &buffer_max,1)
#define buffer_free() sensors_free_array(&buffer,&buffer_count,\
                                         &buffer_max)
#define buffer_add_char(c) sensors_add_array_el(c,&buffer,\
                                                &buffer_count,\
                                                &buffer_max,1)
#define buffer_add_string(s) sensors_add_array_els(s,strlen(s),\
                                                   &buffer, \
                                                   &buffer_count,&buffer_max,1)

%}

 /* Scanner for configuration files */

%option nodefault
%option noyywrap
%option nounput

 /* All states are exclusive */

%x MIDDLE
%x STRING
%x ERR

 /* Any whitespace-like character */

BLANK		[ \f\r\t\v]

IDCHAR		[[:alnum:]_]

 /* Note: `10', `10.4' and `.4' are valid, `10.' is not */

FLOAT   [[:digit:]]*\.?[[:digit:]]+

 /* Only positive whole numbers are recognized here */

NUM	0|([1-9][[:digit:]]*)


%%

 /*
  * STATE: INITIAL
  */

<INITIAL>{

<<EOF>>		{ /* EOF from this state terminates */
		  return 0;
		}

{BLANK}+	; /* eat as many blanks as possible at once */

{BLANK}*\n	{ /* eat a bare newline (possibly preceded by blanks) */
		  sensors_yylineno++;
		}

 /* comments */

#.*		; /* eat the rest of the line after comment char */

#.*\n		{ /* eat the rest of the line after comment char */
		  sensors_yylineno++;
		}

 /*
  * Keywords must be followed by whitespace - eat that too.
  * If there isn't trailing whitespace, we still need to
  * accept it as lexically correct (even though the parser
  * will reject it anyway.)
  */

label{BLANK}*	{
		  sensors_yylval.line.filename = sensors_yyfilename;
		  sensors_yylval.line.lineno = sensors_yylineno;
		  BEGIN(MIDDLE);
		  return LABEL;
		}

set{BLANK}*	{
		  sensors_yylval.line.filename = sensors_yyfilename;
		  sensors_yylval.line.lineno = sensors_yylineno;
		  BEGIN(MIDDLE);
		  return SET;
		}

compute{BLANK}*	{
		  sensors_yylval.line.filename = sensors_yyfilename;
		  sensors_yylval.line.lineno = sensors_yylineno;
		  BEGIN(MIDDLE);
		  return COMPUTE;
		}

bus{BLANK}*	{
		  sensors_yylval.line.filename = sensors_yyfilename;
		  sensors_yylval.line.lineno = sensors_yylineno;
		  BEGIN(MIDDLE);
		  return BUS;
		}

chip{BLANK}*	{
		  sensors_yylval.line.filename = sensors_yyfilename;
		  sensors_yylval.line.lineno = sensors_yylineno;
		  BEGIN(MIDDLE);
		  return CHIP;
		}

ignore{BLANK}*	{
		  sensors_yylval.line.filename = sensors_yyfilename;
		  sensors_yylval.line.lineno = sensors_yylineno;
		  BEGIN(MIDDLE);
		  return IGNORE;
		}

 /* Anything else at the beginning of a line is an error */

[a-z]+		|
.		{
		  BEGIN(ERR);
		  strcpy(sensors_lex_error,"Invalid keyword");
		  return ERROR;
		}
}

 /*
  * STATE: ERROR
  */

<ERR>{

.*		; /* eat whatever is left on this line */

\n		{
		  BEGIN(INITIAL);
		  sensors_yylineno++;
		  return EOL;
		}
}

 /*
  * STATE: MIDDLE
  */

<MIDDLE>{

{BLANK}+	; /* eat as many blanks as possible at once */

\n		{ /* newline here sends EOL token to parser */
		  BEGIN(INITIAL);
		  sensors_yylineno++;
		  return EOL;
		}

<<EOF>>		{ /* EOF here sends EOL token to parser also */
		  BEGIN(INITIAL);
		  return EOL;
		}

\\{BLANK}*\n	{ /* eat an escaped newline with no state change */
		  sensors_yylineno++;
		}

 /* comments */

#.*		; /* eat the rest of the line after comment char */

#.*\n		{ /* eat the rest of the line after comment char */
		  BEGIN(INITIAL);
		  sensors_yylineno++;
		  return EOL;
		}

 /* A number */

{FLOAT}		{
		  sensors_yylval.value = atof(sensors_yytext);
		  return FLOAT;
		}

 /* Some operators */

"+"		return '+';
"-"		return '-';
"*"		return '*';
"/"		return '/';
"("		return '(';
")"		return ')';
","		return ',';
"@"		return '@';
"^"		return '^';
"`"		return '`';

 /* Quoted string */

\"		{
		  buffer_malloc();
		  BEGIN(STRING);
		}

 /* A normal, unquoted identifier */

{IDCHAR}+	{
		  sensors_yylval.name = strdup(sensors_yytext);
		  if (! sensors_yylval.name)
		    sensors_fatal_error("conf-lex.l",
                                        "Allocating a new string");
		  
		  return NAME;
		}

 /* anything else is bogus */

.		|
[[:digit:]]*\.	|
\\{BLANK}*	{
		  BEGIN(ERR);
		  return ERROR;
		}
}

 /*
  * STATE: STRING
  */

<STRING>{

 /* Oops, newline or EOF while in a string is not good */

\n		|
\\\n		{
		  buffer_add_char("\0");
		  strcpy(sensors_lex_error,
			"No matching double quote.");
		  buffer_free();
		  yyless(0);
		  BEGIN(ERR);
		  return ERROR;
		}

<<EOF>>		{
		  strcpy(sensors_lex_error,
			"Reached end-of-file without a matching double quote.");
		  buffer_free();
		  BEGIN(MIDDLE);
		  return ERROR;
		}

 /* At the end */

\"\"		{
		  buffer_add_char("\0");
		  strcpy(sensors_lex_error,
			"Quoted strings must be separated by whitespace.");
		  buffer_free();
		  BEGIN(ERR);
		  return ERROR;
		}
		
\"		{
		  buffer_add_char("\0");
		  sensors_yylval.name = strdup(buffer);
		  if (! sensors_yylval.name)
		    sensors_fatal_error("conf-lex.l",
                                        "Allocating a new string");
		  buffer_free();
		  BEGIN(MIDDLE);
		  return NAME;
		}

\\a		buffer_add_char("\a");
\\b		buffer_add_char("\b");
\\f		buffer_add_char("\f");
\\n		buffer_add_char("\n");
\\r		buffer_add_char("\r");
\\t		buffer_add_char("\t");
\\v		buffer_add_char("\v");

 /* Other escapes: just copy the character behind the slash */

\\.		{
		  buffer_add_char(&sensors_yytext[1]);
		}

 /* Anything else (including a bare '\' which may be followed by EOF) */

\\		|
[^\\\n\"]+	{
		  buffer_add_string(sensors_yytext);
		}
}

%%

/*
	Do the buffer handling manually.  This allows us to scan as many
	config files as we need to, while cleaning up properly after each
	one.  The "BEGIN(0)" line ensures that we start in the default state,
	even if e.g. the previous config file was syntactically broken.

	Returns 0 if successful, !0 otherwise.
*/

static YY_BUFFER_STATE scan_buf = (YY_BUFFER_STATE)0;

int sensors_scanner_init(FILE *input, const char *filename)
{
	BEGIN(0);
	if (!(scan_buf = sensors_yy_create_buffer(input, YY_BUF_SIZE)))
		return -1;

	sensors_yy_switch_to_buffer(scan_buf);
	sensors_yyfilename = filename;
	sensors_yylineno = 1;
	return 0;
}

void sensors_scanner_exit(void)
{
	sensors_yy_delete_buffer(scan_buf);
	scan_buf = (YY_BUFFER_STATE)0;

/* As of flex 2.5.9, yylex_destroy() must be called when done with the
   scaller, otherwise we'll leak memory. */
#if defined(YY_FLEX_MAJOR_VERSION) && defined(YY_FLEX_MINOR_VERSION) && defined(YY_FLEX_SUBMINOR_VERSION)
#if YY_FLEX_MAJOR_VERSION > 2 || \
    (YY_FLEX_MAJOR_VERSION == 2 && (YY_FLEX_MINOR_VERSION > 5 || \
				    (YY_FLEX_MINOR_VERSION == 5 && YY_FLEX_SUBMINOR_VERSION >= 9)))
	sensors_yylex_destroy();
#endif
#endif
}

