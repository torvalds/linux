/*
 * Copyright (c) 2002 - 2009 Tony Finch <dot@dotat.at>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This code was derived from software contributed to Berkeley by Dave Yost.
 * It was rewritten to support ANSI C by Tony Finch. The original version
 * of unifdef carried the 4-clause BSD copyright licence. None of its code
 * remains in this version (though some of the names remain) so it now
 * carries a more liberal licence.
 *
 * The latest version is available from http://dotat.at/prog/unifdef
 */

static const char * const copyright[] = {
    "@(#) Copyright (c) 2002 - 2009 Tony Finch <dot@dotat.at>\n",
    "$dotat: unifdef/unifdef.c,v 1.190 2009/11/27 17:21:26 fanf2 Exp $",
};

/*
 * unifdef - remove ifdef'ed lines
 *
 *  Wishlist:
 *      provide an option which will append the name of the
 *        appropriate symbol after #else's and #endif's
 *      provide an option which will check symbols after
 *        #else's and #endif's to see that they match their
 *        corresponding #ifdef or #ifndef
 *
 *   The first two items above require better buffer handling, which would
 *     also make it possible to handle all "dodgy" directives correctly.
 */

#include <ctype.h>
#include <err.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* types of input lines: */
typedef enum {
	LT_TRUEI,		/* a true #if with ignore flag */
	LT_FALSEI,		/* a false #if with ignore flag */
	LT_IF,			/* an unknown #if */
	LT_TRUE,		/* a true #if */
	LT_FALSE,		/* a false #if */
	LT_ELIF,		/* an unknown #elif */
	LT_ELTRUE,		/* a true #elif */
	LT_ELFALSE,		/* a false #elif */
	LT_ELSE,		/* #else */
	LT_ENDIF,		/* #endif */
	LT_DODGY,		/* flag: directive is not on one line */
	LT_DODGY_LAST = LT_DODGY + LT_ENDIF,
	LT_PLAIN,		/* ordinary line */
	LT_EOF,			/* end of file */
	LT_ERROR,		/* unevaluable #if */
	LT_COUNT
} Linetype;

static char const * const linetype_name[] = {
	"TRUEI", "FALSEI", "IF", "TRUE", "FALSE",
	"ELIF", "ELTRUE", "ELFALSE", "ELSE", "ENDIF",
	"DODGY TRUEI", "DODGY FALSEI",
	"DODGY IF", "DODGY TRUE", "DODGY FALSE",
	"DODGY ELIF", "DODGY ELTRUE", "DODGY ELFALSE",
	"DODGY ELSE", "DODGY ENDIF",
	"PLAIN", "EOF", "ERROR"
};

/* state of #if processing */
typedef enum {
	IS_OUTSIDE,
	IS_FALSE_PREFIX,	/* false #if followed by false #elifs */
	IS_TRUE_PREFIX,		/* first non-false #(el)if is true */
	IS_PASS_MIDDLE,		/* first non-false #(el)if is unknown */
	IS_FALSE_MIDDLE,	/* a false #elif after a pass state */
	IS_TRUE_MIDDLE,		/* a true #elif after a pass state */
	IS_PASS_ELSE,		/* an else after a pass state */
	IS_FALSE_ELSE,		/* an else after a true state */
	IS_TRUE_ELSE,		/* an else after only false states */
	IS_FALSE_TRAILER,	/* #elifs after a true are false */
	IS_COUNT
} Ifstate;

static char const * const ifstate_name[] = {
	"OUTSIDE", "FALSE_PREFIX", "TRUE_PREFIX",
	"PASS_MIDDLE", "FALSE_MIDDLE", "TRUE_MIDDLE",
	"PASS_ELSE", "FALSE_ELSE", "TRUE_ELSE",
	"FALSE_TRAILER"
};

/* state of comment parser */
typedef enum {
	NO_COMMENT = false,	/* outside a comment */
	C_COMMENT,		/* in a comment like this one */
	CXX_COMMENT,		/* between // and end of line */
	STARTING_COMMENT,	/* just after slash-backslash-newline */
	FINISHING_COMMENT,	/* star-backslash-newline in a C comment */
	CHAR_LITERAL,		/* inside '' */
	STRING_LITERAL		/* inside "" */
} Comment_state;

static char const * const comment_name[] = {
	"NO", "C", "CXX", "STARTING", "FINISHING", "CHAR", "STRING"
};

/* state of preprocessor line parser */
typedef enum {
	LS_START,		/* only space and comments on this line */
	LS_HASH,		/* only space, comments, and a hash */
	LS_DIRTY		/* this line can't be a preprocessor line */
} Line_state;

static char const * const linestate_name[] = {
	"START", "HASH", "DIRTY"
};

/*
 * Minimum translation limits from ISO/IEC 9899:1999 5.2.4.1
 */
#define	MAXDEPTH        64			/* maximum #if nesting */
#define	MAXLINE         4096			/* maximum length of line */
#define	MAXSYMS         4096			/* maximum number of symbols */

/*
 * Sometimes when editing a keyword the replacement text is longer, so
 * we leave some space at the end of the tline buffer to accommodate this.
 */
#define	EDITSLOP        10

/*
 * Globals.
 */

static bool             compblank;		/* -B: compress blank lines */
static bool             lnblank;		/* -b: blank deleted lines */
static bool             complement;		/* -c: do the complement */
static bool             debugging;		/* -d: debugging reports */
static bool             iocccok;		/* -e: fewer IOCCC errors */
static bool             strictlogic;		/* -K: keep ambiguous #ifs */
static bool             killconsts;		/* -k: eval constant #ifs */
static bool             lnnum;			/* -n: add #line directives */
static bool             symlist;		/* -s: output symbol list */
static bool             text;			/* -t: this is a text file */

static const char      *symname[MAXSYMS];	/* symbol name */
static const char      *value[MAXSYMS];		/* -Dsym=value */
static bool             ignore[MAXSYMS];	/* -iDsym or -iUsym */
static int              nsyms;			/* number of symbols */

static FILE            *input;			/* input file pointer */
static const char      *filename;		/* input file name */
static int              linenum;		/* current line number */

static char             tline[MAXLINE+EDITSLOP];/* input buffer plus space */
static char            *keyword;		/* used for editing #elif's */

static Comment_state    incomment;		/* comment parser state */
static Line_state       linestate;		/* #if line parser state */
static Ifstate          ifstate[MAXDEPTH];	/* #if processor state */
static bool             ignoring[MAXDEPTH];	/* ignore comments state */
static int              stifline[MAXDEPTH];	/* start of current #if */
static int              depth;			/* current #if nesting */
static int              delcount;		/* count of deleted lines */
static unsigned         blankcount;		/* count of blank lines */
static unsigned         blankmax;		/* maximum recent blankcount */
static bool             constexpr;		/* constant #if expression */

static int              exitstat;		/* program exit status */

static void             addsym(bool, bool, char *);
static void             debug(const char *, ...);
static void             done(void);
static void             error(const char *);
static int              findsym(const char *);
static void             flushline(bool);
static Linetype         parseline(void);
static Linetype         ifeval(const char **);
static void             ignoreoff(void);
static void             ignoreon(void);
static void             keywordedit(const char *);
static void             nest(void);
static void             process(void);
static const char      *skipargs(const char *);
static const char      *skipcomment(const char *);
static const char      *skipsym(const char *);
static void             state(Ifstate);
static int              strlcmp(const char *, const char *, size_t);
static void             unnest(void);
static void             usage(void);

#define endsym(c) (!isalnum((unsigned char)c) && c != '_')

/*
 * The main program.
 */
int
main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "i:D:U:I:BbcdeKklnst")) != -1)
		switch (opt) {
		case 'i': /* treat stuff controlled by these symbols as text */
			/*
			 * For strict backwards-compatibility the U or D
			 * should be immediately after the -i but it doesn't
			 * matter much if we relax that requirement.
			 */
			opt = *optarg++;
			if (opt == 'D')
				addsym(true, true, optarg);
			else if (opt == 'U')
				addsym(true, false, optarg);
			else
				usage();
			break;
		case 'D': /* define a symbol */
			addsym(false, true, optarg);
			break;
		case 'U': /* undef a symbol */
			addsym(false, false, optarg);
			break;
		case 'I':
			/* no-op for compatibility with cpp */
			break;
		case 'B': /* compress blank lines around removed section */
			compblank = true;
			break;
		case 'b': /* blank deleted lines instead of omitting them */
		case 'l': /* backwards compatibility */
			lnblank = true;
			break;
		case 'c': /* treat -D as -U and vice versa */
			complement = true;
			break;
		case 'd':
			debugging = true;
			break;
		case 'e': /* fewer errors from dodgy lines */
			iocccok = true;
			break;
		case 'K': /* keep ambiguous #ifs */
			strictlogic = true;
			break;
		case 'k': /* process constant #ifs */
			killconsts = true;
			break;
		case 'n': /* add #line directive after deleted lines */
			lnnum = true;
			break;
		case 's': /* only output list of symbols that control #ifs */
			symlist = true;
			break;
		case 't': /* don't parse C comments */
			text = true;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (compblank && lnblank)
		errx(2, "-B and -b are mutually exclusive");
	if (argc > 1) {
		errx(2, "can only do one file");
	} else if (argc == 1 && strcmp(*argv, "-") != 0) {
		filename = *argv;
		input = fopen(filename, "r");
		if (input == NULL)
			err(2, "can't open %s", filename);
	} else {
		filename = "[stdin]";
		input = stdin;
	}
	process();
	abort(); /* bug */
}

static void
usage(void)
{
	fprintf(stderr, "usage: unifdef [-BbcdeKknst] [-Ipath]"
	    " [-Dsym[=val]] [-Usym] [-iDsym[=val]] [-iUsym] ... [file]\n");
	exit(2);
}

/*
 * A state transition function alters the global #if processing state
 * in a particular way. The table below is indexed by the current
 * processing state and the type of the current line.
 *
 * Nesting is handled by keeping a stack of states; some transition
 * functions increase or decrease the depth. They also maintain the
 * ignore state on a stack. In some complicated cases they have to
 * alter the preprocessor directive, as follows.
 *
 * When we have processed a group that starts off with a known-false
 * #if/#elif sequence (which has therefore been deleted) followed by a
 * #elif that we don't understand and therefore must keep, we edit the
 * latter into a #if to keep the nesting correct.
 *
 * When we find a true #elif in a group, the following block will
 * always be kept and the rest of the sequence after the next #elif or
 * #else will be discarded. We edit the #elif into a #else and the
 * following directive to #endif since this has the desired behaviour.
 *
 * "Dodgy" directives are split across multiple lines, the most common
 * example being a multi-line comment hanging off the right of the
 * directive. We can handle them correctly only if there is no change
 * from printing to dropping (or vice versa) caused by that directive.
 * If the directive is the first of a group we have a choice between
 * failing with an error, or passing it through unchanged instead of
 * evaluating it. The latter is not the default to avoid questions from
 * users about unifdef unexpectedly leaving behind preprocessor directives.
 */
typedef void state_fn(void);

/* report an error */
static void Eelif (void) { error("Inappropriate #elif"); }
static void Eelse (void) { error("Inappropriate #else"); }
static void Eendif(void) { error("Inappropriate #endif"); }
static void Eeof  (void) { error("Premature EOF"); }
static void Eioccc(void) { error("Obfuscated preprocessor control line"); }
/* plain line handling */
static void print (void) { flushline(true); }
static void drop  (void) { flushline(false); }
/* output lacks group's start line */
static void Strue (void) { drop();  ignoreoff(); state(IS_TRUE_PREFIX); }
static void Sfalse(void) { drop();  ignoreoff(); state(IS_FALSE_PREFIX); }
static void Selse (void) { drop();               state(IS_TRUE_ELSE); }
/* print/pass this block */
static void Pelif (void) { print(); ignoreoff(); state(IS_PASS_MIDDLE); }
static void Pelse (void) { print();              state(IS_PASS_ELSE); }
static void Pendif(void) { print(); unnest(); }
/* discard this block */
static void Dfalse(void) { drop();  ignoreoff(); state(IS_FALSE_TRAILER); }
static void Delif (void) { drop();  ignoreoff(); state(IS_FALSE_MIDDLE); }
static void Delse (void) { drop();               state(IS_FALSE_ELSE); }
static void Dendif(void) { drop();  unnest(); }
/* first line of group */
static void Fdrop (void) { nest();  Dfalse(); }
static void Fpass (void) { nest();  Pelif(); }
static void Ftrue (void) { nest();  Strue(); }
static void Ffalse(void) { nest();  Sfalse(); }
/* variable pedantry for obfuscated lines */
static void Oiffy (void) { if (!iocccok) Eioccc(); Fpass(); ignoreon(); }
static void Oif   (void) { if (!iocccok) Eioccc(); Fpass(); }
static void Oelif (void) { if (!iocccok) Eioccc(); Pelif(); }
/* ignore comments in this block */
static void Idrop (void) { Fdrop();  ignoreon(); }
static void Itrue (void) { Ftrue();  ignoreon(); }
static void Ifalse(void) { Ffalse(); ignoreon(); }
/* edit this line */
static void Mpass (void) { strncpy(keyword, "if  ", 4); Pelif(); }
static void Mtrue (void) { keywordedit("else\n");  state(IS_TRUE_MIDDLE); }
static void Melif (void) { keywordedit("endif\n"); state(IS_FALSE_TRAILER); }
static void Melse (void) { keywordedit("endif\n"); state(IS_FALSE_ELSE); }

static state_fn * const trans_table[IS_COUNT][LT_COUNT] = {
/* IS_OUTSIDE */
{ Itrue, Ifalse,Fpass, Ftrue, Ffalse,Eelif, Eelif, Eelif, Eelse, Eendif,
  Oiffy, Oiffy, Fpass, Oif,   Oif,   Eelif, Eelif, Eelif, Eelse, Eendif,
  print, done,  abort },
/* IS_FALSE_PREFIX */
{ Idrop, Idrop, Fdrop, Fdrop, Fdrop, Mpass, Strue, Sfalse,Selse, Dendif,
  Idrop, Idrop, Fdrop, Fdrop, Fdrop, Mpass, Eioccc,Eioccc,Eioccc,Eioccc,
  drop,  Eeof,  abort },
/* IS_TRUE_PREFIX */
{ Itrue, Ifalse,Fpass, Ftrue, Ffalse,Dfalse,Dfalse,Dfalse,Delse, Dendif,
  Oiffy, Oiffy, Fpass, Oif,   Oif,   Eioccc,Eioccc,Eioccc,Eioccc,Eioccc,
  print, Eeof,  abort },
/* IS_PASS_MIDDLE */
{ Itrue, Ifalse,Fpass, Ftrue, Ffalse,Pelif, Mtrue, Delif, Pelse, Pendif,
  Oiffy, Oiffy, Fpass, Oif,   Oif,   Pelif, Oelif, Oelif, Pelse, Pendif,
  print, Eeof,  abort },
/* IS_FALSE_MIDDLE */
{ Idrop, Idrop, Fdrop, Fdrop, Fdrop, Pelif, Mtrue, Delif, Pelse, Pendif,
  Idrop, Idrop, Fdrop, Fdrop, Fdrop, Eioccc,Eioccc,Eioccc,Eioccc,Eioccc,
  drop,  Eeof,  abort },
/* IS_TRUE_MIDDLE */
{ Itrue, Ifalse,Fpass, Ftrue, Ffalse,Melif, Melif, Melif, Melse, Pendif,
  Oiffy, Oiffy, Fpass, Oif,   Oif,   Eioccc,Eioccc,Eioccc,Eioccc,Pendif,
  print, Eeof,  abort },
/* IS_PASS_ELSE */
{ Itrue, Ifalse,Fpass, Ftrue, Ffalse,Eelif, Eelif, Eelif, Eelse, Pendif,
  Oiffy, Oiffy, Fpass, Oif,   Oif,   Eelif, Eelif, Eelif, Eelse, Pendif,
  print, Eeof,  abort },
/* IS_FALSE_ELSE */
{ Idrop, Idrop, Fdrop, Fdrop, Fdrop, Eelif, Eelif, Eelif, Eelse, Dendif,
  Idrop, Idrop, Fdrop, Fdrop, Fdrop, Eelif, Eelif, Eelif, Eelse, Eioccc,
  drop,  Eeof,  abort },
/* IS_TRUE_ELSE */
{ Itrue, Ifalse,Fpass, Ftrue, Ffalse,Eelif, Eelif, Eelif, Eelse, Dendif,
  Oiffy, Oiffy, Fpass, Oif,   Oif,   Eelif, Eelif, Eelif, Eelse, Eioccc,
  print, Eeof,  abort },
/* IS_FALSE_TRAILER */
{ Idrop, Idrop, Fdrop, Fdrop, Fdrop, Dfalse,Dfalse,Dfalse,Delse, Dendif,
  Idrop, Idrop, Fdrop, Fdrop, Fdrop, Dfalse,Dfalse,Dfalse,Delse, Eioccc,
  drop,  Eeof,  abort }
/*TRUEI  FALSEI IF     TRUE   FALSE  ELIF   ELTRUE ELFALSE ELSE  ENDIF
  TRUEI  FALSEI IF     TRUE   FALSE  ELIF   ELTRUE ELFALSE ELSE  ENDIF (DODGY)
  PLAIN  EOF    ERROR */
};

/*
 * State machine utility functions
 */
static void
done(void)
{
	if (incomment)
		error("EOF in comment");
	exit(exitstat);
}
static void
ignoreoff(void)
{
	if (depth == 0)
		abort(); /* bug */
	ignoring[depth] = ignoring[depth-1];
}
static void
ignoreon(void)
{
	ignoring[depth] = true;
}
static void
keywordedit(const char *replacement)
{
	size_t size = tline + sizeof(tline) - keyword;
	char *dst = keyword;
	const char *src = replacement;
	if (size != 0) {
		while ((--size != 0) && (*src != '\0'))
			*dst++ = *src++;
		*dst = '\0';
	}
	print();
}
static void
nest(void)
{
	if (depth > MAXDEPTH-1)
		abort(); /* bug */
	if (depth == MAXDEPTH-1)
		error("Too many levels of nesting");
	depth += 1;
	stifline[depth] = linenum;
}
static void
unnest(void)
{
	if (depth == 0)
		abort(); /* bug */
	depth -= 1;
}
static void
state(Ifstate is)
{
	ifstate[depth] = is;
}

/*
 * Write a line to the output or not, according to command line options.
 */
static void
flushline(bool keep)
{
	if (symlist)
		return;
	if (keep ^ complement) {
		bool blankline = tline[strspn(tline, " \t\n")] == '\0';
		if (blankline && compblank && blankcount != blankmax) {
			delcount += 1;
			blankcount += 1;
		} else {
			if (lnnum && delcount > 0)
				printf("#line %d\n", linenum);
			fputs(tline, stdout);
			delcount = 0;
			blankmax = blankcount = blankline ? blankcount + 1 : 0;
		}
	} else {
		if (lnblank)
			putc('\n', stdout);
		exitstat = 1;
		delcount += 1;
		blankcount = 0;
	}
}

/*
 * The driver for the state machine.
 */
static void
process(void)
{
	Linetype lineval;

	/* When compressing blank lines, act as if the file
	   is preceded by a large number of blank lines. */
	blankmax = blankcount = 1000;
	for (;;) {
		linenum++;
		lineval = parseline();
		trans_table[ifstate[depth]][lineval]();
		debug("process %s -> %s depth %d",
		    linetype_name[lineval],
		    ifstate_name[ifstate[depth]], depth);
	}
}

/*
 * Parse a line and determine its type. We keep the preprocessor line
 * parser state between calls in the global variable linestate, with
 * help from skipcomment().
 */
static Linetype
parseline(void)
{
	const char *cp;
	int cursym;
	int kwlen;
	Linetype retval;
	Comment_state wascomment;

	if (fgets(tline, MAXLINE, input) == NULL)
		return (LT_EOF);
	retval = LT_PLAIN;
	wascomment = incomment;
	cp = skipcomment(tline);
	if (linestate == LS_START) {
		if (*cp == '#') {
			linestate = LS_HASH;
			cp = skipcomment(cp + 1);
		} else if (*cp != '\0')
			linestate = LS_DIRTY;
	}
	if (!incomment && linestate == LS_HASH) {
		keyword = tline + (cp - tline);
		cp = skipsym(cp);
		kwlen = cp - keyword;
		/* no way can we deal with a continuation inside a keyword */
		if (strncmp(cp, "\\\n", 2) == 0)
			Eioccc();
		if (strlcmp("ifdef", keyword, kwlen) == 0 ||
		    strlcmp("ifndef", keyword, kwlen) == 0) {
			cp = skipcomment(cp);
			if ((cursym = findsym(cp)) < 0)
				retval = LT_IF;
			else {
				retval = (keyword[2] == 'n')
				    ? LT_FALSE : LT_TRUE;
				if (value[cursym] == NULL)
					retval = (retval == LT_TRUE)
					    ? LT_FALSE : LT_TRUE;
				if (ignore[cursym])
					retval = (retval == LT_TRUE)
					    ? LT_TRUEI : LT_FALSEI;
			}
			cp = skipsym(cp);
		} else if (strlcmp("if", keyword, kwlen) == 0)
			retval = ifeval(&cp);
		else if (strlcmp("elif", keyword, kwlen) == 0)
			retval = ifeval(&cp) - LT_IF + LT_ELIF;
		else if (strlcmp("else", keyword, kwlen) == 0)
			retval = LT_ELSE;
		else if (strlcmp("endif", keyword, kwlen) == 0)
			retval = LT_ENDIF;
		else {
			linestate = LS_DIRTY;
			retval = LT_PLAIN;
		}
		cp = skipcomment(cp);
		if (*cp != '\0') {
			linestate = LS_DIRTY;
			if (retval == LT_TRUE || retval == LT_FALSE ||
			    retval == LT_TRUEI || retval == LT_FALSEI)
				retval = LT_IF;
			if (retval == LT_ELTRUE || retval == LT_ELFALSE)
				retval = LT_ELIF;
		}
		if (retval != LT_PLAIN && (wascomment || incomment)) {
			retval += LT_DODGY;
			if (incomment)
				linestate = LS_DIRTY;
		}
		/* skipcomment normally changes the state, except
		   if the last line of the file lacks a newline, or
		   if there is too much whitespace in a directive */
		if (linestate == LS_HASH) {
			size_t len = cp - tline;
			if (fgets(tline + len, MAXLINE - len, input) == NULL) {
				/* append the missing newline */
				tline[len+0] = '\n';
				tline[len+1] = '\0';
				cp++;
				linestate = LS_START;
			} else {
				linestate = LS_DIRTY;
			}
		}
	}
	if (linestate == LS_DIRTY) {
		while (*cp != '\0')
			cp = skipcomment(cp + 1);
	}
	debug("parser %s comment %s line",
	    comment_name[incomment], linestate_name[linestate]);
	return (retval);
}

/*
 * These are the binary operators that are supported by the expression
 * evaluator.
 */
static Linetype op_strict(int *p, int v, Linetype at, Linetype bt) {
	if(at == LT_IF || bt == LT_IF) return (LT_IF);
	return (*p = v, v ? LT_TRUE : LT_FALSE);
}
static Linetype op_lt(int *p, Linetype at, int a, Linetype bt, int b) {
	return op_strict(p, a < b, at, bt);
}
static Linetype op_gt(int *p, Linetype at, int a, Linetype bt, int b) {
	return op_strict(p, a > b, at, bt);
}
static Linetype op_le(int *p, Linetype at, int a, Linetype bt, int b) {
	return op_strict(p, a <= b, at, bt);
}
static Linetype op_ge(int *p, Linetype at, int a, Linetype bt, int b) {
	return op_strict(p, a >= b, at, bt);
}
static Linetype op_eq(int *p, Linetype at, int a, Linetype bt, int b) {
	return op_strict(p, a == b, at, bt);
}
static Linetype op_ne(int *p, Linetype at, int a, Linetype bt, int b) {
	return op_strict(p, a != b, at, bt);
}
static Linetype op_or(int *p, Linetype at, int a, Linetype bt, int b) {
	if (!strictlogic && (at == LT_TRUE || bt == LT_TRUE))
		return (*p = 1, LT_TRUE);
	return op_strict(p, a || b, at, bt);
}
static Linetype op_and(int *p, Linetype at, int a, Linetype bt, int b) {
	if (!strictlogic && (at == LT_FALSE || bt == LT_FALSE))
		return (*p = 0, LT_FALSE);
	return op_strict(p, a && b, at, bt);
}

/*
 * An evaluation function takes three arguments, as follows: (1) a pointer to
 * an element of the precedence table which lists the operators at the current
 * level of precedence; (2) a pointer to an integer which will receive the
 * value of the expression; and (3) a pointer to a char* that points to the
 * expression to be evaluated and that is updated to the end of the expression
 * when evaluation is complete. The function returns LT_FALSE if the value of
 * the expression is zero, LT_TRUE if it is non-zero, LT_IF if the expression
 * depends on an unknown symbol, or LT_ERROR if there is a parse failure.
 */
struct ops;

typedef Linetype eval_fn(const struct ops *, int *, const char **);

static eval_fn eval_table, eval_unary;

/*
 * The precedence table. Expressions involving binary operators are evaluated
 * in a table-driven way by eval_table. When it evaluates a subexpression it
 * calls the inner function with its first argument pointing to the next
 * element of the table. Innermost expressions have special non-table-driven
 * handling.
 */
static const struct ops {
	eval_fn *inner;
	struct op {
		const char *str;
		Linetype (*fn)(int *, Linetype, int, Linetype, int);
	} op[5];
} eval_ops[] = {
	{ eval_table, { { "||", op_or } } },
	{ eval_table, { { "&&", op_and } } },
	{ eval_table, { { "==", op_eq },
			{ "!=", op_ne } } },
	{ eval_unary, { { "<=", op_le },
			{ ">=", op_ge },
			{ "<", op_lt },
			{ ">", op_gt } } }
};

/*
 * Function for evaluating the innermost parts of expressions,
 * viz. !expr (expr) number defined(symbol) symbol
 * We reset the constexpr flag in the last two cases.
 */
static Linetype
eval_unary(const struct ops *ops, int *valp, const char **cpp)
{
	const char *cp;
	char *ep;
	int sym;
	bool defparen;
	Linetype lt;

	cp = skipcomment(*cpp);
	if (*cp == '!') {
		debug("eval%d !", ops - eval_ops);
		cp++;
		lt = eval_unary(ops, valp, &cp);
		if (lt == LT_ERROR)
			return (LT_ERROR);
		if (lt != LT_IF) {
			*valp = !*valp;
			lt = *valp ? LT_TRUE : LT_FALSE;
		}
	} else if (*cp == '(') {
		cp++;
		debug("eval%d (", ops - eval_ops);
		lt = eval_table(eval_ops, valp, &cp);
		if (lt == LT_ERROR)
			return (LT_ERROR);
		cp = skipcomment(cp);
		if (*cp++ != ')')
			return (LT_ERROR);
	} else if (isdigit((unsigned char)*cp)) {
		debug("eval%d number", ops - eval_ops);
		*valp = strtol(cp, &ep, 0);
		if (ep == cp)
			return (LT_ERROR);
		lt = *valp ? LT_TRUE : LT_FALSE;
		cp = skipsym(cp);
	} else if (strncmp(cp, "defined", 7) == 0 && endsym(cp[7])) {
		cp = skipcomment(cp+7);
		debug("eval%d defined", ops - eval_ops);
		if (*cp == '(') {
			cp = skipcomment(cp+1);
			defparen = true;
		} else {
			defparen = false;
		}
		sym = findsym(cp);
		if (sym < 0) {
			lt = LT_IF;
		} else {
			*valp = (value[sym] != NULL);
			lt = *valp ? LT_TRUE : LT_FALSE;
		}
		cp = skipsym(cp);
		cp = skipcomment(cp);
		if (defparen && *cp++ != ')')
			return (LT_ERROR);
		constexpr = false;
	} else if (!endsym(*cp)) {
		debug("eval%d symbol", ops - eval_ops);
		sym = findsym(cp);
		cp = skipsym(cp);
		if (sym < 0) {
			lt = LT_IF;
			cp = skipargs(cp);
		} else if (value[sym] == NULL) {
			*valp = 0;
			lt = LT_FALSE;
		} else {
			*valp = strtol(value[sym], &ep, 0);
			if (*ep != '\0' || ep == value[sym])
				return (LT_ERROR);
			lt = *valp ? LT_TRUE : LT_FALSE;
			cp = skipargs(cp);
		}
		constexpr = false;
	} else {
		debug("eval%d bad expr", ops - eval_ops);
		return (LT_ERROR);
	}

	*cpp = cp;
	debug("eval%d = %d", ops - eval_ops, *valp);
	return (lt);
}

/*
 * Table-driven evaluation of binary operators.
 */
static Linetype
eval_table(const struct ops *ops, int *valp, const char **cpp)
{
	const struct op *op;
	const char *cp;
	int val;
	Linetype lt, rt;

	debug("eval%d", ops - eval_ops);
	cp = *cpp;
	lt = ops->inner(ops+1, valp, &cp);
	if (lt == LT_ERROR)
		return (LT_ERROR);
	for (;;) {
		cp = skipcomment(cp);
		for (op = ops->op; op->str != NULL; op++)
			if (strncmp(cp, op->str, strlen(op->str)) == 0)
				break;
		if (op->str == NULL)
			break;
		cp += strlen(op->str);
		debug("eval%d %s", ops - eval_ops, op->str);
		rt = ops->inner(ops+1, &val, &cp);
		if (rt == LT_ERROR)
			return (LT_ERROR);
		lt = op->fn(valp, lt, *valp, rt, val);
	}

	*cpp = cp;
	debug("eval%d = %d", ops - eval_ops, *valp);
	debug("eval%d lt = %s", ops - eval_ops, linetype_name[lt]);
	return (lt);
}

/*
 * Evaluate the expression on a #if or #elif line. If we can work out
 * the result we return LT_TRUE or LT_FALSE accordingly, otherwise we
 * return just a generic LT_IF.
 */
static Linetype
ifeval(const char **cpp)
{
	int ret;
	int val = 0;

	debug("eval %s", *cpp);
	constexpr = killconsts ? false : true;
	ret = eval_table(eval_ops, &val, cpp);
	debug("eval = %d", val);
	return (constexpr ? LT_IF : ret == LT_ERROR ? LT_IF : ret);
}

/*
 * Skip over comments, strings, and character literals and stop at the
 * next character position that is not whitespace. Between calls we keep
 * the comment state in the global variable incomment, and we also adjust
 * the global variable linestate when we see a newline.
 * XXX: doesn't cope with the buffer splitting inside a state transition.
 */
static const char *
skipcomment(const char *cp)
{
	if (text || ignoring[depth]) {
		for (; isspace((unsigned char)*cp); cp++)
			if (*cp == '\n')
				linestate = LS_START;
		return (cp);
	}
	while (*cp != '\0')
		/* don't reset to LS_START after a line continuation */
		if (strncmp(cp, "\\\n", 2) == 0)
			cp += 2;
		else switch (incomment) {
		case NO_COMMENT:
			if (strncmp(cp, "/\\\n", 3) == 0) {
				incomment = STARTING_COMMENT;
				cp += 3;
			} else if (strncmp(cp, "/*", 2) == 0) {
				incomment = C_COMMENT;
				cp += 2;
			} else if (strncmp(cp, "//", 2) == 0) {
				incomment = CXX_COMMENT;
				cp += 2;
			} else if (strncmp(cp, "\'", 1) == 0) {
				incomment = CHAR_LITERAL;
				linestate = LS_DIRTY;
				cp += 1;
			} else if (strncmp(cp, "\"", 1) == 0) {
				incomment = STRING_LITERAL;
				linestate = LS_DIRTY;
				cp += 1;
			} else if (strncmp(cp, "\n", 1) == 0) {
				linestate = LS_START;
				cp += 1;
			} else if (strchr(" \t", *cp) != NULL) {
				cp += 1;
			} else
				return (cp);
			continue;
		case CXX_COMMENT:
			if (strncmp(cp, "\n", 1) == 0) {
				incomment = NO_COMMENT;
				linestate = LS_START;
			}
			cp += 1;
			continue;
		case CHAR_LITERAL:
		case STRING_LITERAL:
			if ((incomment == CHAR_LITERAL && cp[0] == '\'') ||
			    (incomment == STRING_LITERAL && cp[0] == '\"')) {
				incomment = NO_COMMENT;
				cp += 1;
			} else if (cp[0] == '\\') {
				if (cp[1] == '\0')
					cp += 1;
				else
					cp += 2;
			} else if (strncmp(cp, "\n", 1) == 0) {
				if (incomment == CHAR_LITERAL)
					error("unterminated char literal");
				else
					error("unterminated string literal");
			} else
				cp += 1;
			continue;
		case C_COMMENT:
			if (strncmp(cp, "*\\\n", 3) == 0) {
				incomment = FINISHING_COMMENT;
				cp += 3;
			} else if (strncmp(cp, "*/", 2) == 0) {
				incomment = NO_COMMENT;
				cp += 2;
			} else
				cp += 1;
			continue;
		case STARTING_COMMENT:
			if (*cp == '*') {
				incomment = C_COMMENT;
				cp += 1;
			} else if (*cp == '/') {
				incomment = CXX_COMMENT;
				cp += 1;
			} else {
				incomment = NO_COMMENT;
				linestate = LS_DIRTY;
			}
			continue;
		case FINISHING_COMMENT:
			if (*cp == '/') {
				incomment = NO_COMMENT;
				cp += 1;
			} else
				incomment = C_COMMENT;
			continue;
		default:
			abort(); /* bug */
		}
	return (cp);
}

/*
 * Skip macro arguments.
 */
static const char *
skipargs(const char *cp)
{
	const char *ocp = cp;
	int level = 0;
	cp = skipcomment(cp);
	if (*cp != '(')
		return (cp);
	do {
		if (*cp == '(')
			level++;
		if (*cp == ')')
			level--;
		cp = skipcomment(cp+1);
	} while (level != 0 && *cp != '\0');
	if (level == 0)
		return (cp);
	else
	/* Rewind and re-detect the syntax error later. */
		return (ocp);
}

/*
 * Skip over an identifier.
 */
static const char *
skipsym(const char *cp)
{
	while (!endsym(*cp))
		++cp;
	return (cp);
}

/*
 * Look for the symbol in the symbol table. If it is found, we return
 * the symbol table index, else we return -1.
 */
static int
findsym(const char *str)
{
	const char *cp;
	int symind;

	cp = skipsym(str);
	if (cp == str)
		return (-1);
	if (symlist) {
		printf("%.*s\n", (int)(cp-str), str);
		/* we don't care about the value of the symbol */
		return (0);
	}
	for (symind = 0; symind < nsyms; ++symind) {
		if (strlcmp(symname[symind], str, cp-str) == 0) {
			debug("findsym %s %s", symname[symind],
			    value[symind] ? value[symind] : "");
			return (symind);
		}
	}
	return (-1);
}

/*
 * Add a symbol to the symbol table.
 */
static void
addsym(bool ignorethis, bool definethis, char *sym)
{
	int symind;
	char *val;

	symind = findsym(sym);
	if (symind < 0) {
		if (nsyms >= MAXSYMS)
			errx(2, "too many symbols");
		symind = nsyms++;
	}
	symname[symind] = sym;
	ignore[symind] = ignorethis;
	val = sym + (skipsym(sym) - sym);
	if (definethis) {
		if (*val == '=') {
			value[symind] = val+1;
			*val = '\0';
		} else if (*val == '\0')
			value[symind] = "";
		else
			usage();
	} else {
		if (*val != '\0')
			usage();
		value[symind] = NULL;
	}
}

/*
 * Compare s with n characters of t.
 * The same as strncmp() except that it checks that s[n] == '\0'.
 */
static int
strlcmp(const char *s, const char *t, size_t n)
{
	while (n-- && *t != '\0')
		if (*s != *t)
			return ((unsigned char)*s - (unsigned char)*t);
		else
			++s, ++t;
	return ((unsigned char)*s);
}

/*
 * Diagnostics.
 */
static void
debug(const char *msg, ...)
{
	va_list ap;

	if (debugging) {
		va_start(ap, msg);
		vwarnx(msg, ap);
		va_end(ap);
	}
}

static void
error(const char *msg)
{
	if (depth == 0)
		warnx("%s: %d: %s", filename, linenum, msg);
	else
		warnx("%s: %d: %s (#if line %d depth %d)",
		    filename, linenum, msg, stifline[depth], depth);
	errx(2, "output may be truncated");
}
