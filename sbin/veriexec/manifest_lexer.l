%{
/*-
 * Copyright (c) 2004-2018, Juniper Networks, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <stdio.h>
#include <string.h>
#include "veriexec.h"
#include "manifest_parser.h"

#define YY_NO_UNPUT

int lineno = 1;
int bol = 1;
extern int parser_version;
 
void yyerror(const char *message);
void warning(const char *message);
int yylex(void);

%}

%%

\n {
	lineno++;
	bol=1;
	return EOL;
}

[/a-zA-Z0-9\.][^ \t\n=]*  {
	yylval.string = strdup(yytext);
	if (bol) {
		bol=0;
		return PATH;
	} else
		return STRING;
}

= {
	return EQ;
}


[ \t\r] ;  /* eat white ones */

#>[0-9]+ {
        /*
         * If we are older than the specified version
         * ignore rest of line, otherwise just discard this token.
         */
        int skip = atoi(&yytext[2]);

        VERBOSE(3, ("%s: skip if %d <= %d\n", yytext, parser_version, skip));
        if (parser_version <= skip) {
		/* treat as a comment, yyless() is cheaper than yyunput() */
		yytext[yyleng - 1] = '#';
		yyless(2);
        }
}

#[^>\n].* ;      /* comment */

.    yyerror("invalid character");

%%

static char *manifest_file = NULL;

struct string_buf {
	const char *buf;
	size_t pos, size;
};

static int
read_string_buf (void *token, char *dest, int count)
{
	struct string_buf *str_buf_p = (struct string_buf *)token;
	ssize_t n;

	if (count < 0)
		return 0;

	n = str_buf_p->size - str_buf_p->pos;
	if (count < n)
		n = count;

	memcpy(dest, str_buf_p->buf + str_buf_p->pos, n);
	str_buf_p->pos += n;

	return n;
}

FILE *
manifest_open (const char *file, const char *file_content)
{
	static struct string_buf str_buf;
    
	if (manifest_file) {
		free(manifest_file);
		fclose(yyin);
	}

	str_buf.buf  = file_content;
	str_buf.pos  = 0;
	str_buf.size = strlen(file_content);
	yyin = fropen(&str_buf, read_string_buf);
	if (yyin) {
		manifest_file = strdup(file);
		lineno = 1;
		manifest_parser_init();
	} else
		manifest_file = NULL;
	return yyin;
}

void
yyerror(const char *string)
{
	fprintf(stderr, "%s: %d: %s at %s\n",
	    manifest_file, lineno, string, yytext);
}

int
yywrap(void)
{
	return (1);
}
