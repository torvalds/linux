%{
/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)lang.l	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include "y.tab.h"
#include "config.h"

/*
 * Data for returning to previous files from include files.
 */
struct incl {
	struct	incl *in_prev; 	/* previous includes in effect, if any */
	YY_BUFFER_STATE in_buf;	/* previous lex state */
	const	char *in_fname;	/* previous file name */
	int	in_lineno;	/* previous line number */
	int	in_ateof;	/* token to insert at EOF */
};
static struct	incl *inclp;
static const	char *lastfile;

/*
 * Key word table
 */

struct kt {
	const char *kt_name;
	int kt_val;
} key_words[] = {
	{ "config",	CONFIG },
	{ "cpu",	CPU },
	{ "nocpu",	NOCPU },
	{ "device",	DEVICE },
	{ "devices",	DEVICE },
	{ "nodevice",	NODEVICE },
	{ "nodevices",	NODEVICE },
	{ "env",	ENV },
	{ "envvar",	ENVVAR },
	{ "hints",	HINTS },
	{ "ident",	IDENT },
	{ "machine",	ARCH }, /* MACHINE is defined in /sys/param.h */
	{ "makeoption",	MAKEOPTIONS },
	{ "makeoptions", MAKEOPTIONS },
	{ "nomakeoption", NOMAKEOPTION },
	{ "nomakeoptions", NOMAKEOPTION },
	{ "maxusers",	MAXUSERS },
	{ "profile",	PROFILE },
	{ "option",	OPTIONS },
	{ "options",	OPTIONS },
	{ "nooption",	NOOPTION },
	{ "nooptions",	NOOPTION },
	{ "include",	INCLUDE },
	{ "files", 	FILES },
	{ 0, 0 },
};


static int endinclude(void);
int include(const char *, int);
int kw_lookup(char *);
unsigned int octal(const char *);
unsigned int hex(const char *);
int yyerror(const char *);

#define YY_DECL int yylex(void)
%}

%option nounput
%option noinput

ID	[A-Za-z_][-A-Za-z_0-9]*
PATH	[./][-/.%^A-Za-z_0-9]+
%START TOEOL
%START ENVC
%%
<ENVC>[^#\n]*	{
			BEGIN 0;
			yylval.str = strdup(yytext);
			return ENVLINE;
		}
{ID}		{
			int i;

			BEGIN 0;
			if ((i = kw_lookup(yytext)) == -1)
			{
				yylval.str = strdup(yytext);
				return ID;
			}
			/* We'll glom onto the rest of an envvar line */
			if (i == ENVVAR)
				BEGIN ENVC;
			return i;
		}
\\\"[^"]*\\\"	{
			BEGIN 0;
			yytext[yyleng-2] = '"';
			yytext[yyleng-1] = '\0';
			yylval.str = strdup(yytext + 1);
			return ID;
		}
\"[^"]+\"	{
			BEGIN 0;
			yytext[yyleng-1] = '\0';
			yylval.str = strdup(yytext + 1);
			return ID;
		}
<TOEOL>[^# \t\n]*	{
			BEGIN 0;
			yylval.str = strdup(yytext);
			return ID;
		}
0[0-7]*		{
			yylval.val = octal(yytext);
			return NUMBER;
		}
0x[0-9a-fA-F]+	{
			yylval.val = hex(yytext);
			return NUMBER;
		}
-?[1-9][0-9]*	{
			yylval.val = atoi(yytext);
			return NUMBER;
		}
"?"		{
			yylval.val = -1;
			return NUMBER;
		}
\n/[ \t]	{
			yyline++;
		}
\n		{
			yyline++;
			return SEMICOLON;
		}
#.*		{	/* Ignored (comment) */;	}
[ \t\f]*	{	/* Ignored (white space) */;	}
";"		{	return SEMICOLON;		}
","		{	return COMMA;			}
"="		{	BEGIN TOEOL; return EQUALS;	}
"+="		{	BEGIN TOEOL; return PLUSEQUALS;	}
<<EOF>>		{
			int tok;

			if (inclp == NULL)
				return YY_NULL;
			tok = endinclude();
			if (tok != 0)
				return tok;
			/* otherwise continue scanning */
		}
{PATH}		{
			BEGIN 0;
			yylval.str = strdup(yytext);
			return PATH;
		}
.		{	return yytext[0];		}

%%
/*
 * kw_lookup
 *	Look up a string in the keyword table.  Returns a -1 if the
 *	string is not a keyword otherwise it returns the keyword number
 */

int
kw_lookup(char *word)
{
	struct kt *kp;

	for (kp = key_words; kp->kt_name != 0; kp++)
		if (eq(word, kp->kt_name))
			return kp->kt_val;
	return -1;
}

/*
 * Number conversion routines
 */

unsigned int
octal(const char *str)
{
	unsigned int num;

	(void) sscanf(str, "%o", &num);
	return num;
}

unsigned int
hex(const char *str)
{
	unsigned int num;

	(void) sscanf(str+2, "%x", &num);
	return num;
}

void
cfgfile_add(const char *fname)
{
	struct cfgfile *cf;

	cf = calloc(1, sizeof(*cf));
	if (cf == NULL)
		err(EXIT_FAILURE, "calloc");
	assert(cf != NULL);
	asprintf(&cf->cfg_path, "%s", fname);
	STAILQ_INSERT_TAIL(&cfgfiles, cf, cfg_next);
}

void
cfgfile_removeall(void)
{
	struct cfgfile *cf;

	while (!STAILQ_EMPTY(&cfgfiles)) {
		cf = STAILQ_FIRST(&cfgfiles);
		STAILQ_REMOVE_HEAD(&cfgfiles, cfg_next);
		if (cf->cfg_path != NULL)
			free(cf->cfg_path);
		free(cf);
	}
}

/*
 * Open the named file for inclusion at the current point.  Returns 0 on
 * success (file opened and previous state pushed), nonzero on failure
 * (fopen failed, complaint made).  The `ateof' parameter controls the
 * token to be inserted at the end of the include file. If ateof == 0,
 * then nothing is inserted.
 */
int
include(const char *fname, int ateof)
{
	FILE *fp;
	struct incl *in;
	struct includepath* ipath;
	char *fnamebuf;

	fnamebuf = NULL;
	fp = fopen(fname, "r");
	if (fp == NULL && fname[0] != '.' && fname[0] != '/') {
		asprintf(&fnamebuf, "../../conf/%s", fname);
		if (fnamebuf != NULL) {
			fp = fopen(fnamebuf, "r");
			if (fp == NULL)
				free(fnamebuf);
		}
	}
	if (fp == NULL) {
		SLIST_FOREACH(ipath, &includepath, path_next) {
			asprintf(&fnamebuf, "%s/%s", ipath->path, fname);
			if (fnamebuf != NULL) {
				fp = fopen(fnamebuf, "r");
			}
			if (fp != NULL)
				break;
			free(fnamebuf);
		}
	}
	if (fp == NULL) {
		yyerror("cannot open included file");
		return (-1);
	}
	cfgfile_add(fnamebuf == NULL ? fname : fnamebuf);
	in = malloc(sizeof(*in));
	assert(in != NULL);
	in->in_prev = inclp;
	in->in_buf = YY_CURRENT_BUFFER;
	in->in_fname = yyfile;
	in->in_lineno = yyline;
	in->in_ateof = ateof;
	inclp = in;
	yy_switch_to_buffer(yy_create_buffer(fp, YY_BUF_SIZE));
	yyfile = fname;
	yyline = 0;
	return (0);
}

/*
 * Terminate the most recent inclusion.
 */
static int
endinclude(void)
{
	struct incl *in;
	int ateof;

	in = inclp;
	assert(in != NULL);
	inclp = in->in_prev;
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
