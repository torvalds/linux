%{
/*	$OpenBSD: scan.l,v 1.25 2023/06/16 16:14:34 tb Exp $	*/
/*	$NetBSD: scan.l,v 1.13 1997/02/02 21:12:37 thorpej Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)scan.l	8.1 (Berkeley) 6/6/93
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "config.h"
#include "gram.h"

int	yyline;
const char *yyfile;
const char *lastfile;

/*
 * Data for returning to previous files from include files.
 */
struct incl {
	struct	incl *in_prev;	/* previous includes in effect, if any */
	YY_BUFFER_STATE in_buf;	/* previous lex state */
	const char *in_fname;	/* previous file name */
	int	in_lineno;	/* previous line number */
	int	in_ateof;	/* token to insert at EOF */
};
static struct incl *incl;
static int endinclude(void);

%}

%option noyywrap

PATH	[A-Za-z_0-9]*[./][-A-Za-z_0-9./\$\{\}]*
WORD	[A-Za-z_][-A-Za-z_0-9]*

%%
		/* Local variables for yylex() */
		int tok;

and		return AND;
at		return AT;
attach		return ATTACH;
build		return BUILD;
compile-with	return COMPILE_WITH;
config		return CONFIG;
define		return DEFINE;
defopt		return DEFOPT;
device		return DEVICE;
disable		return DISABLE;
enable		return ENABLE;
dumps		return DUMPS;
file		return XFILE;
flags		return FLAGS;
include		return INCLUDE;
machine		return XMACHINE;
major		return MAJOR;
makeoptions	return MAKEOPTIONS;
makeoption	return MAKEOPTIONS;
maxpartitions	return MAXPARTITIONS;
maxusers	return MAXUSERS;
minor		return MINOR;
needs-count	return NEEDS_COUNT;
needs-flag	return NEEDS_FLAG;
object		return XOBJECT;
on		return ON;
options		return OPTIONS;
option		return OPTIONS;
pseudo-device	return PSEUDO_DEVICE;
root		return ROOT;
source		return SOURCE;
swap		return SWAP;
with		return WITH;
rmoption	return RMOPTIONS;
rmoptions	return RMOPTIONS;

{PATH}		{
		yylval.str = intern(yytext);
		return PATHNAME;
	}
{WORD}		{
		yylval.str = intern(yytext);
		return WORD;
	}

\"\"		{
		yylval.str = intern("");
		return EMPTY;
	}
\"([^"\n]|\\\")+ {
		tok = input();  /* eat closing quote */
		yylval.str = intern(yytext + 1);
		if (tok != '"') {
			error("closing quote missing");
			unput(tok);
		}
		return WORD;
	}
0[0-7]*	{
		yylval.val = strtol(yytext, NULL, 8);
		return NUMBER;
	}
0[xX][0-9a-fA-F]+ {
		yylval.val = strtoul(yytext + 2, NULL, 16);
		return NUMBER;
	}
[1-9][0-9]* {
		yylval.val = strtol(yytext, NULL, 10);
		return NUMBER;
	}
\n[ \t] {
		/*
		 * Note: newline followed by whitespace is always a
		 * continuation of the previous line, so do NOT
		 * return a token in this case.
		 */
		yyline++;
	}
\n	{
		yyline++;
		return '\n';
	}
#.*	{ /* ignored (comment) */; }
[ \t]+	{ /* ignored (white space) */; }
.	{ return yytext[0]; }
<<EOF>> {
		if (incl == NULL)
			return YY_NULL;
		tok = endinclude();
		if (tok)
			return tok;
		/* otherwise continue scanning */
	}

%%

/*
 * Open the "main" file (conffile).
 */
int
firstfile(const char *fname)
{

	if ((yyin = fopen(fname, "r")) == NULL)
		return (-1);
	yyfile = conffile = fname;
	yyline = 1;
	return (0);
}

/*
 * Open the named file for inclusion at the current point.  Returns 0 on
 * success (file opened and previous state pushed), nonzero on failure
 * (fopen failed, complaint made).  The `ateof' parameter controls the
 * token to be inserted at the end of the include file (i.e. ENDFILE).
 * If ateof == 0 then nothing is inserted.
 */
int
include(const char *fname, int ateof)
{
	FILE *fp;
	struct incl *in;
	char *s;
	static int havedirs;

	if (havedirs == 0) {
		havedirs = 1;
		setupdirs();
	}

	/* Kludge until files.* files are fixed. */
	if (strncmp(fname, "../../../", 9) == 0)
		fname += 9;

	s = (*fname == '/') ? strdup(fname) : sourcepath(fname);
	if ((fp = fopen(s, "r")) == NULL) {
		error("cannot open %s for reading: %s\n", s, strerror(errno));
		free(s);
		return (-1);
	}
	in = emalloc(sizeof *in);
	in->in_prev = incl;
	in->in_buf = YY_CURRENT_BUFFER;
	in->in_fname = yyfile;
	in->in_lineno = yyline;
	in->in_ateof = ateof;
	incl = in;
	yy_switch_to_buffer(yy_create_buffer(fp, YY_BUF_SIZE));
	yyfile = intern(s);
	yyline = 1;
	free(s);
	return (0);
}

/*
 * Terminate the most recent inclusion.
 */
static int
endinclude()
{
	struct incl *in;
	int ateof;

	if ((in = incl) == NULL)
		panic("endinclude");
	incl = in->in_prev;
	lastfile = yyfile;
	yy_delete_buffer(YY_CURRENT_BUFFER);
	(void)fclose(yyin);
	yy_switch_to_buffer(in->in_buf);
	yyfile = in->in_fname;
	yyline = in->in_lineno;
	ateof  = in->in_ateof;
	free(in);

	return (ateof);
}

/*
 * Return the current line number.  If yacc has looked ahead and caused
 * us to consume a newline, we have to subtract one.  yychar is yacc's
 * token lookahead, so we can tell.
 */
int
currentline()
{
	extern int yychar;

	return (yyline - (yychar == '\n'));
}
