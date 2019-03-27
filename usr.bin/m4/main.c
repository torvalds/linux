/*	$OpenBSD: main.c,v 1.86 2015/11/03 16:21:47 deraadt Exp $	*/
/*	$NetBSD: main.c,v 1.12 1997/02/08 23:54:49 cgd Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ozan Yigit at York University.
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
 */

/*
 * main.c
 * Facility: m4 macro processor
 * by: oz
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <signal.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <ohash.h>
#include "mdef.h"
#include "stdd.h"
#include "extern.h"
#include "pathnames.h"

stae *mstack;			/* stack of m4 machine         */
char *sstack;			/* shadow stack, for string space extension */
static size_t STACKMAX;		/* current maximum size of stack */
int sp;				/* current m4  stack pointer   */
int fp;				/* m4 call frame pointer       */
struct input_file infile[MAXINP];/* input file stack (0=stdin)  */
FILE **outfile;			/* diversion array(0=bitbucket)*/
int maxout;
FILE *active;			/* active output file pointer  */
int ilevel = 0;			/* input file stack pointer    */
int oindex = 0;			/* diversion index..	       */
const char *null = "";                /* as it says.. just a null..  */
char **m4wraps = NULL;		/* m4wraps array.	       */
int maxwraps = 0;		/* size of m4wraps array       */
int wrapindex = 0;		/* current offset in m4wraps   */
char lquote[MAXCCHARS+1] = {LQUOTE};	/* left quote character  (`)   */
char rquote[MAXCCHARS+1] = {RQUOTE};	/* right quote character (')   */
char scommt[MAXCCHARS+1] = {SCOMMT};	/* start character for comment */
char ecommt[MAXCCHARS+1] = {ECOMMT};	/* end character for comment   */
int  synch_lines = 0;		/* line synchronisation for C preprocessor */
int  prefix_builtins = 0;	/* -P option to prefix builtin keywords */

struct keyblk {
        const char    *knam;          /* keyword name */
        int     ktyp;           /* keyword type */
};

static struct keyblk keywrds[] = {	/* m4 keywords to be installed */
	{ "include",      INCLTYPE },
	{ "sinclude",     SINCTYPE },
	{ "define",       DEFITYPE },
	{ "defn",         DEFNTYPE },
	{ "divert",       DIVRTYPE | NOARGS },
	{ "expr",         EXPRTYPE },
	{ "eval",         EXPRTYPE },
	{ "substr",       SUBSTYPE },
	{ "ifelse",       IFELTYPE },
	{ "ifdef",        IFDFTYPE },
	{ "len",          LENGTYPE },
	{ "incr",         INCRTYPE },
	{ "decr",         DECRTYPE },
	{ "dnl",          DNLNTYPE | NOARGS },
	{ "changequote",  CHNQTYPE | NOARGS },
	{ "changecom",    CHNCTYPE | NOARGS },
	{ "index",        INDXTYPE },
#ifdef EXTENDED
	{ "paste",        PASTTYPE },
	{ "spaste",       SPASTYPE },
	/* Newer extensions, needed to handle gnu-m4 scripts */
	{ "indir",        INDIRTYPE},
	{ "builtin",      BUILTINTYPE},
	{ "patsubst",	  PATSTYPE},
	{ "regexp",	  REGEXPTYPE},
	{ "esyscmd",	  ESYSCMDTYPE},
	{ "__file__",	  FILENAMETYPE | NOARGS},
	{ "__line__",	  LINETYPE | NOARGS},
#endif
	{ "popdef",       POPDTYPE },
	{ "pushdef",      PUSDTYPE },
	{ "dumpdef",      DUMPTYPE | NOARGS },
	{ "shift",        SHIFTYPE | NOARGS },
	{ "translit",     TRNLTYPE },
	{ "undefine",     UNDFTYPE },
	{ "undivert",     UNDVTYPE | NOARGS },
	{ "divnum",       DIVNTYPE | NOARGS },
	{ "maketemp",     MKTMTYPE },
	{ "mkstemp",      MKTMTYPE },
	{ "errprint",     ERRPTYPE | NOARGS },
	{ "m4wrap",       M4WRTYPE | NOARGS },
	{ "m4exit",       EXITTYPE | NOARGS },
	{ "syscmd",       SYSCTYPE },
	{ "sysval",       SYSVTYPE | NOARGS },
	{ "traceon",	  TRACEONTYPE | NOARGS },
	{ "traceoff",	  TRACEOFFTYPE | NOARGS },

	{ "unix",         SELFTYPE | NOARGS },
};

#define MAXKEYS	(sizeof(keywrds)/sizeof(struct keyblk))

extern int optind;
extern char *optarg;

#define MAXRECORD 50
static struct position {
	char *name;
	unsigned long line;
} quotes[MAXRECORD], paren[MAXRECORD];

static void record(struct position *, int);
static void dump_stack(struct position *, int);

static void macro(void);
static void initkwds(void);
static ndptr inspect(int, char *);
static int do_look_ahead(int, const char *);
static void reallyoutputstr(const char *);
static void reallyputchar(int);

static void enlarge_stack(void);

int main(int, char *[]);

int exit_code = 0;

int
main(int argc, char *argv[])
{
	int c;
	int n;
	char *p;

	if (signal(SIGINT, SIG_IGN) != SIG_IGN)
		signal(SIGINT, onintr);

	init_macros();
	initspaces();
	STACKMAX = INITSTACKMAX;

	mstack = xreallocarray(NULL, STACKMAX, sizeof(stae), NULL);
	sstack = xalloc(STACKMAX, NULL);

	maxout = 0;
	outfile = NULL;
	resizedivs(MAXOUT);

	while ((c = getopt(argc, argv, "gst:d:D:U:o:I:P")) != -1)
		switch(c) {

		case 'D':               /* define something..*/
			for (p = optarg; *p; p++)
				if (*p == '=')
					break;
			if (*p)
				*p++ = EOS;
			dodefine(optarg, p);
			break;
		case 'I':
			addtoincludepath(optarg);
			break;
		case 'P':
			prefix_builtins = 1;
			break;
		case 'U':               /* undefine...       */
			macro_popdef(optarg);
			break;
		case 'g':
			mimic_gnu = 1;
			break;
		case 'd':
			set_trace_flags(optarg);
			break;
		case 's':
			synch_lines = 1;
			break;
		case 't':
			mark_traced(optarg, 1);
			break;
		case 'o':
			trace_file(optarg);
                        break;
		case '?':
			usage();
		}

        argc -= optind;
        argv += optind;

	initkwds();
	if (mimic_gnu)
		setup_builtin("format", FORMATTYPE);

	active = stdout;		/* default active output     */
	bbase[0] = bufbase;
        if (!argc) {
		sp = -1;		/* stack pointer initialized */
		fp = 0;			/* frame pointer initialized */
		set_input(infile+0, stdin, "stdin");
					/* default input (naturally) */
		macro();
	} else
		for (; argc--; ++argv) {
			p = *argv;
			if (p[0] == '-' && p[1] == EOS)
				set_input(infile, stdin, "stdin");
			else if (fopen_trypath(infile, p) == NULL)
				err(1, "%s", p);
			sp = -1;
			fp = 0;
			macro();
			release_input(infile);
		}

	if (wrapindex) {
		int i;

		ilevel = 0;		/* in case m4wrap includes.. */
		bufbase = bp = buf;	/* use the entire buffer   */
		if (mimic_gnu) {
			while (wrapindex != 0) {
				for (i = 0; i < wrapindex; i++)
					pbstr(m4wraps[i]);
				wrapindex =0;
				macro();
			}
		} else {
			for (i = 0; i < wrapindex; i++) {
				pbstr(m4wraps[i]);
				macro();
			}
		}
	}

	if (active != stdout)
		active = stdout;	/* reset output just in case */
	for (n = 1; n < maxout; n++)	/* default wrap-up: undivert */
		if (outfile[n] != NULL)
			getdiv(n);
					/* remove bitbucket if used  */
	if (outfile[0] != NULL) {
		(void) fclose(outfile[0]);
	}

	return exit_code;
}

/*
 * Look ahead for `token'.
 * (on input `t == token[0]')
 * Used for comment and quoting delimiters.
 * Returns 1 if `token' present; copied to output.
 *         0 if `token' not found; all characters pushed back
 */
static int
do_look_ahead(int t, const char *token)
{
	int i;

	assert((unsigned char)t == (unsigned char)token[0]);

	for (i = 1; *++token; i++) {
		t = gpbc();
		if (t == EOF || (unsigned char)t != (unsigned char)*token) {
			pushback(t);
			while (--i)
				pushback(*--token);
			return 0;
		}
	}
	return 1;
}

#define LOOK_AHEAD(t, token) (t != EOF &&		\
    (unsigned char)(t)==(unsigned char)(token)[0] &&	\
    do_look_ahead(t,token))

/*
 * macro - the work horse..
 */
static void
macro(void)
{
	char token[MAXTOK+1];
	int t, l;
	ndptr p;
	int  nlpar;

	cycle {
		t = gpbc();

		if (LOOK_AHEAD(t,lquote)) {	/* strip quotes */
			nlpar = 0;
			record(quotes, nlpar++);
			/*
			 * Opening quote: scan forward until matching
			 * closing quote has been found.
			 */
			do {

				l = gpbc();
				if (LOOK_AHEAD(l,rquote)) {
					if (--nlpar > 0)
						outputstr(rquote);
				} else if (LOOK_AHEAD(l,lquote)) {
					record(quotes, nlpar++);
					outputstr(lquote);
				} else if (l == EOF) {
					if (nlpar == 1)
						warnx("unclosed quote:");
					else
						warnx("%d unclosed quotes:", nlpar);
					dump_stack(quotes, nlpar);
					exit(1);
				} else {
					if (nlpar > 0) {
						if (sp < 0)
							reallyputchar(l);
						else
							CHRSAVE(l);
					}
				}
			}
			while (nlpar != 0);
		} else if (sp < 0 && LOOK_AHEAD(t, scommt)) {
			reallyoutputstr(scommt);

			for(;;) {
				t = gpbc();
				if (LOOK_AHEAD(t, ecommt)) {
					reallyoutputstr(ecommt);
					break;
				}
				if (t == EOF)
					break;
				reallyputchar(t);
			}
		} else if (t == '_' || isalpha(t)) {
			p = inspect(t, token);
			if (p != NULL)
				pushback(l = gpbc());
			if (p == NULL || (l != LPAREN &&
			    (macro_getdef(p)->type & NEEDARGS) != 0))
				outputstr(token);
			else {
		/*
		 * real thing.. First build a call frame:
		 */
				pushf(fp);	/* previous call frm */
				pushf(macro_getdef(p)->type); /* type of the call  */
				pushf(is_traced(p));
				pushf(0);	/* parenthesis level */
				fp = sp;	/* new frame pointer */
		/*
		 * now push the string arguments:
		 */
				pushdef(p);			/* defn string */
				pushs1((char *)macro_name(p));	/* macro name  */
				pushs(ep);			/* start next..*/

				if (l != LPAREN && PARLEV == 0) {
				    /* no bracks  */
					chrsave(EOS);

					if (sp == (int)STACKMAX)
						errx(1, "internal stack overflow");
					eval((const char **) mstack+fp+1, 2,
					    CALTYP, TRACESTATUS);

					ep = PREVEP;	/* flush strspace */
					sp = PREVSP;	/* previous sp..  */
					fp = PREVFP;	/* rewind stack...*/
				}
			}
		} else if (t == EOF) {
			if (!mimic_gnu /* you can puke right there */
			    && sp > -1 && ilevel <= 0) {
				warnx( "unexpected end of input, unclosed parenthesis:");
				dump_stack(paren, PARLEV);
				exit(1);
			}
			if (ilevel <= 0)
				break;			/* all done thanks.. */
			release_input(infile+ilevel--);
			emit_synchline();
			bufbase = bbase[ilevel];
			continue;
		} else if (sp < 0) {		/* not in a macro at all */
			reallyputchar(t);	/* output directly..	 */
		}

		else switch(t) {

		case LPAREN:
			if (PARLEV > 0)
				chrsave(t);
			while (isspace(l = gpbc())) /* skip blank, tab, nl.. */
				if (PARLEV > 0)
					chrsave(l);
			pushback(l);
			record(paren, PARLEV++);
			break;

		case RPAREN:
			if (--PARLEV > 0)
				chrsave(t);
			else {			/* end of argument list */
				chrsave(EOS);

				if (sp == (int)STACKMAX)
					errx(1, "internal stack overflow");

				eval((const char **) mstack+fp+1, sp-fp,
				    CALTYP, TRACESTATUS);

				ep = PREVEP;	/* flush strspace */
				sp = PREVSP;	/* previous sp..  */
				fp = PREVFP;	/* rewind stack...*/
			}
			break;

		case COMMA:
			if (PARLEV == 1) {
				chrsave(EOS);		/* new argument   */
				while (isspace(l = gpbc()))
					;
				pushback(l);
				pushs(ep);
			} else
				chrsave(t);
			break;

		default:
			if (LOOK_AHEAD(t, scommt)) {
				char *p;
				for (p = scommt; *p; p++)
					chrsave(*p);
				for(;;) {
					t = gpbc();
					if (LOOK_AHEAD(t, ecommt)) {
						for (p = ecommt; *p; p++)
							chrsave(*p);
						break;
					}
					if (t == EOF)
					    break;
					CHRSAVE(t);
				}
			} else
				CHRSAVE(t);		/* stack the char */
			break;
		}
	}
}

/*
 * output string directly, without pushing it for reparses.
 */
void
outputstr(const char *s)
{
	if (sp < 0)
		reallyoutputstr(s);
	else
		while (*s)
			CHRSAVE(*s++);
}

void
reallyoutputstr(const char *s)
{
	if (synch_lines) {
		while (*s) {
			fputc(*s, active);
			if (*s++ == '\n') {
				infile[ilevel].synch_lineno++;
				if (infile[ilevel].synch_lineno !=
				    infile[ilevel].lineno)
					do_emit_synchline();
			}
		}
	} else
		fputs(s, active);
}

void
reallyputchar(int c)
{
	putc(c, active);
	if (synch_lines && c == '\n') {
		infile[ilevel].synch_lineno++;
		if (infile[ilevel].synch_lineno != infile[ilevel].lineno)
			do_emit_synchline();
	}
}

/*
 * build an input token..
 * consider only those starting with _ or A-Za-z.
 */
static ndptr
inspect(int c, char *tp)
{
	char *name = tp;
	char *etp = tp+MAXTOK;
	ndptr p;

	*tp++ = c;

	while ((isalnum(c = gpbc()) || c == '_') && tp < etp)
		*tp++ = c;
	if (c != EOF)
		PUSHBACK(c);
	*tp = EOS;
	/* token is too long, it won't match anything, but it can still
	 * be output. */
	if (tp == ep) {
		outputstr(name);
		while (isalnum(c = gpbc()) || c == '_') {
			if (sp < 0)
				reallyputchar(c);
			else
				CHRSAVE(c);
		}
		*name = EOS;
		return NULL;
	}

	p = ohash_find(&macros, ohash_qlookupi(&macros, name, (const char **)&tp));
	if (p == NULL)
		return NULL;
	if (macro_getdef(p) == NULL)
		return NULL;
	return p;
}

/*
 * initkwds - initialise m4 keywords as fast as possible.
 * This very similar to install, but without certain overheads,
 * such as calling lookup. Malloc is not used for storing the
 * keyword strings, since we simply use the static pointers
 * within keywrds block.
 */
static void
initkwds(void)
{
	unsigned int type;
	int i;

	for (i = 0; i < (int)MAXKEYS; i++) {
		type = keywrds[i].ktyp & TYPEMASK;
		if ((keywrds[i].ktyp & NOARGS) == 0)
			type |= NEEDARGS;
		setup_builtin(keywrds[i].knam, type);
	}
}

static void
record(struct position *t, int lev)
{
	if (lev < MAXRECORD) {
		t[lev].name = CURRENT_NAME;
		t[lev].line = CURRENT_LINE;
	}
}

static void
dump_stack(struct position *t, int lev)
{
	int i;

	for (i = 0; i < lev; i++) {
		if (i == MAXRECORD) {
			fprintf(stderr, "   ...\n");
			break;
		}
		fprintf(stderr, "   %s at line %lu\n",
			t[i].name, t[i].line);
	}
}


static void
enlarge_stack(void)
{
	STACKMAX += STACKMAX/2;
	mstack = xreallocarray(mstack, STACKMAX, sizeof(stae),
	    "Evaluation stack overflow (%lu)",
	    (unsigned long)STACKMAX);
	sstack = xrealloc(sstack, STACKMAX,
	    "Evaluation stack overflow (%lu)",
	    (unsigned long)STACKMAX);
}
