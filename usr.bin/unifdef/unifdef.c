/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 - 2015 Tony Finch <dot@dotat.at>
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
 * unifdef - remove ifdef'ed lines
 *
 * This code was derived from software contributed to Berkeley by Dave Yost.
 * It was rewritten to support ANSI C by Tony Finch. The original version
 * of unifdef carried the 4-clause BSD copyright licence. None of its code
 * remains in this version (though some of the names remain) so it now
 * carries a more liberal licence.
 *
 *  Wishlist:
 *      provide an option which will append the name of the
 *        appropriate symbol after #else's and #endif's
 *      provide an option which will check symbols after
 *        #else's and #endif's to see that they match their
 *        corresponding #ifdef or #ifndef
 *
 *   These require better buffer handling, which would also make
 *   it possible to handle all "dodgy" directives correctly.
 */

#include "unifdef.h"

static const char copyright[] =
    "@(#) $Version: unifdef-2.11 $\n"
    "@(#) $FreeBSD$\n"
    "@(#) $Author: Tony Finch (dot@dotat.at) $\n"
    "@(#) $URL: https://dotat.at/prog/unifdef $\n"
;

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

#define linetype_if2elif(lt) ((Linetype)(lt - LT_IF + LT_ELIF))
#define linetype_2dodgy(lt) ((Linetype)(lt + LT_DODGY))

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
#define	MAXSYMS         16384			/* maximum number of symbols */

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
static bool             inplace;		/* -m: modify in place */
static bool             iocccok;		/* -e: fewer IOCCC errors */
static bool             strictlogic;		/* -K: keep ambiguous #ifs */
static bool             killconsts;		/* -k: eval constant #ifs */
static bool             lnnum;			/* -n: add #line directives */
static bool             symlist;		/* -s: output symbol list */
static bool             symdepth;		/* -S: output symbol depth */
static bool             text;			/* -t: this is a text file */

static const char      *symname[MAXSYMS];	/* symbol name */
static const char      *value[MAXSYMS];		/* -Dsym=value */
static bool             ignore[MAXSYMS];	/* -iDsym or -iUsym */
static int              nsyms;			/* number of symbols */

static FILE            *input;			/* input file pointer */
static const char      *filename;		/* input file name */
static int              linenum;		/* current line number */
static const char      *linefile;		/* file name for #line */
static FILE            *output;			/* output file pointer */
static const char      *ofilename;		/* output file name */
static const char      *backext;		/* backup extension */
static char            *tempname;		/* avoid splatting input */

static char             tline[MAXLINE+EDITSLOP];/* input buffer plus space */
static char            *keyword;		/* used for editing #elif's */

/*
 * When processing a file, the output's newline style will match the
 * input's, and unifdef correctly handles CRLF or LF endings whatever
 * the platform's native style. The stdio streams are opened in binary
 * mode to accommodate platforms whose native newline style is CRLF.
 * When the output isn't a processed input file (when it is error /
 * debug / diagnostic messages) then unifdef uses native line endings.
 */

static const char      *newline;		/* input file format */
static const char       newline_unix[] = "\n";
static const char       newline_crlf[] = "\r\n";

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
static bool             zerosyms;		/* to format symdepth output */
static bool             firstsym;		/* ditto */

static int              exitmode;		/* exit status mode */
static int              exitstat;		/* program exit status */
static bool             altered;		/* was this file modified? */

static void             addsym1(bool, bool, char *);
static void             addsym2(bool, const char *, const char *);
static char            *astrcat(const char *, const char *);
static void             cleantemp(void);
static void             closeio(void);
static void             debug(const char *, ...);
static void             debugsym(const char *, int);
static bool             defundef(void);
static void             defundefile(const char *);
static void             done(void);
static void             error(const char *);
static int              findsym(const char **);
static void             flushline(bool);
static void             hashline(void);
static void             help(void);
static Linetype         ifeval(const char **);
static void             ignoreoff(void);
static void             ignoreon(void);
static void             indirectsym(void);
static void             keywordedit(const char *);
static const char      *matchsym(const char *, const char *);
static void             nest(void);
static Linetype         parseline(void);
static void             process(void);
static void             processinout(const char *, const char *);
static const char      *skipargs(const char *);
static const char      *skipcomment(const char *);
static const char      *skiphash(void);
static const char      *skipline(const char *);
static const char      *skipsym(const char *);
static void             state(Ifstate);
static void             unnest(void);
static void             usage(void);
static void             version(void);
static const char      *xstrdup(const char *, const char *);

#define endsym(c) (!isalnum((unsigned char)c) && c != '_')

/*
 * The main program.
 */
int
main(int argc, char *argv[])
{
	int opt;

	while ((opt = getopt(argc, argv, "i:D:U:f:I:M:o:x:bBcdehKklmnsStV")) != -1)
		switch (opt) {
		case 'i': /* treat stuff controlled by these symbols as text */
			/*
			 * For strict backwards-compatibility the U or D
			 * should be immediately after the -i but it doesn't
			 * matter much if we relax that requirement.
			 */
			opt = *optarg++;
			if (opt == 'D')
				addsym1(true, true, optarg);
			else if (opt == 'U')
				addsym1(true, false, optarg);
			else
				usage();
			break;
		case 'D': /* define a symbol */
			addsym1(false, true, optarg);
			break;
		case 'U': /* undef a symbol */
			addsym1(false, false, optarg);
			break;
		case 'I': /* no-op for compatibility with cpp */
			break;
		case 'b': /* blank deleted lines instead of omitting them */
		case 'l': /* backwards compatibility */
			lnblank = true;
			break;
		case 'B': /* compress blank lines around removed section */
			compblank = true;
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
		case 'f': /* definitions file */
			defundefile(optarg);
			break;
		case 'h':
			help();
			break;
		case 'K': /* keep ambiguous #ifs */
			strictlogic = true;
			break;
		case 'k': /* process constant #ifs */
			killconsts = true;
			break;
		case 'm': /* modify in place */
			inplace = true;
			break;
		case 'M': /* modify in place and keep backup */
			inplace = true;
			if (strlen(optarg) > 0)
				backext = optarg;
			break;
		case 'n': /* add #line directive after deleted lines */
			lnnum = true;
			break;
		case 'o': /* output to a file */
			ofilename = optarg;
			break;
		case 's': /* only output list of symbols that control #ifs */
			symlist = true;
			break;
		case 'S': /* list symbols with their nesting depth */
			symlist = symdepth = true;
			break;
		case 't': /* don't parse C comments */
			text = true;
			break;
		case 'V':
			version();
			break;
		case 'x':
			exitmode = atoi(optarg);
			if(exitmode < 0 || exitmode > 2)
				usage();
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (compblank && lnblank)
		errx(2, "-B and -b are mutually exclusive");
	if (symlist && (ofilename != NULL || inplace || argc > 1))
		errx(2, "-s only works with one input file");
	if (argc > 1 && ofilename != NULL)
		errx(2, "-o cannot be used with multiple input files");
	if (argc > 1 && !inplace)
		errx(2, "multiple input files require -m or -M");
	if (argc == 0 && inplace)
		errx(2, "-m requires an input file");
	if (argc == 0)
		argc = 1;
	if (argc == 1 && !inplace && ofilename == NULL)
		ofilename = "-";
	indirectsym();

	atexit(cleantemp);
	if (ofilename != NULL)
		processinout(*argv, ofilename);
	else while (argc-- > 0) {
		processinout(*argv, *argv);
		argv++;
	}
	switch(exitmode) {
	case(0): exit(exitstat);
	case(1): exit(!exitstat);
	case(2): exit(0);
	default: abort(); /* bug */
	}
}

/*
 * File logistics.
 */
static void
processinout(const char *ifn, const char *ofn)
{
	struct stat st;

	if (ifn == NULL || strcmp(ifn, "-") == 0) {
		filename = "[stdin]";
		linefile = NULL;
		input = fbinmode(stdin);
	} else {
		filename = ifn;
		linefile = ifn;
		input = fopen(ifn, "rb");
		if (input == NULL)
			err(2, "can't open %s", ifn);
	}
	if (strcmp(ofn, "-") == 0) {
		output = fbinmode(stdout);
		process();
		return;
	}
	if (stat(ofn, &st) < 0) {
		output = fopen(ofn, "wb");
		if (output == NULL)
			err(2, "can't create %s", ofn);
		process();
		return;
	}

	tempname = astrcat(ofn, ".XXXXXX");
	output = mktempmode(tempname, st.st_mode);
	if (output == NULL)
		err(2, "can't create %s", tempname);

	process();

	if (backext != NULL) {
		char *backname = astrcat(ofn, backext);
		if (rename(ofn, backname) < 0)
			err(2, "can't rename \"%s\" to \"%s\"", ofn, backname);
		free(backname);
	}
	/* leave file unmodified if unifdef made no changes */
	if (!altered && backext == NULL) {
		if (remove(tempname) < 0)
			warn("can't remove \"%s\"", tempname);
	} else if (replace(tempname, ofn) < 0)
		err(2, "can't rename \"%s\" to \"%s\"", tempname, ofn);
	free(tempname);
	tempname = NULL;
}

/*
 * For cleaning up if there is an error.
 */
static void
cleantemp(void)
{
	if (tempname != NULL)
		remove(tempname);
}

/*
 * Self-identification functions.
 */

static void
version(void)
{
	const char *c = copyright;
	for (;;) {
		while (*++c != '$')
			if (*c == '\0')
				exit(0);
		while (*++c != '$')
			putc(*c, stderr);
		putc('\n', stderr);
	}
}

static void
synopsis(FILE *fp)
{
	fprintf(fp,
	    "usage:	unifdef [-bBcdehKkmnsStV] [-x{012}] [-Mext] [-opath] \\\n"
	    "		[-[i]Dsym[=val]] [-[i]Usym] [-fpath] ... [file] ...\n");
}

static void
usage(void)
{
	synopsis(stderr);
	exit(2);
}

static void
help(void)
{
	synopsis(stdout);
	printf(
	    "	-Dsym=val  define preprocessor symbol with given value\n"
	    "	-Dsym      define preprocessor symbol with value 1\n"
	    "	-Usym	   preprocessor symbol is undefined\n"
	    "	-iDsym=val \\  ignore C strings and comments\n"
	    "	-iDsym      ) in sections controlled by these\n"
	    "	-iUsym	   /  preprocessor symbols\n"
	    "	-fpath	file containing #define and #undef directives\n"
	    "	-b	blank lines instead of deleting them\n"
	    "	-B	compress blank lines around deleted section\n"
	    "	-c	complement (invert) keep vs. delete\n"
	    "	-d	debugging mode\n"
	    "	-e	ignore multiline preprocessor directives\n"
	    "	-h	print help\n"
	    "	-Ipath	extra include file path (ignored)\n"
	    "	-K	disable && and || short-circuiting\n"
	    "	-k	process constant #if expressions\n"
	    "	-Mext	modify in place and keep backups\n"
	    "	-m	modify input files in place\n"
	    "	-n	add #line directives to output\n"
	    "	-opath	output file name\n"
	    "	-S	list #if control symbols with nesting\n"
	    "	-s	list #if control symbols\n"
	    "	-t	ignore C strings and comments\n"
	    "	-V	print version\n"
	    "	-x{012}	exit status mode\n"
	);
	exit(0);
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
 * latter into a #if to keep the nesting correct. We use memcpy() to
 * overwrite the 4 byte token "elif" with "if  " without a '\0' byte.
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
/* modify this line */
static void Mpass (void) { memcpy(keyword, "if  ", 4); Pelif(); }
static void Mtrue (void) { keywordedit("else");  state(IS_TRUE_MIDDLE); }
static void Melif (void) { keywordedit("endif"); state(IS_FALSE_TRAILER); }
static void Melse (void) { keywordedit("endif"); state(IS_FALSE_ELSE); }

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
	snprintf(keyword, tline + sizeof(tline) - keyword,
	    "%s%s", replacement, newline);
	altered = true;
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
 * The last state transition function. When this is called,
 * lineval == LT_EOF, so the process() loop will terminate.
 */
static void
done(void)
{
	if (incomment)
		error("EOF in comment");
	closeio();
}

/*
 * Write a line to the output or not, according to command line options.
 * If writing fails, closeio() will print the error and exit.
 */
static void
flushline(bool keep)
{
	if (symlist)
		return;
	if (keep ^ complement) {
		bool blankline = tline[strspn(tline, " \t\r\n")] == '\0';
		if (blankline && compblank && blankcount != blankmax) {
			delcount += 1;
			blankcount += 1;
		} else {
			if (lnnum && delcount > 0)
				hashline();
			if (fputs(tline, output) == EOF)
				closeio();
			delcount = 0;
			blankmax = blankcount = blankline ? blankcount + 1 : 0;
		}
	} else {
		if (lnblank && fputs(newline, output) == EOF)
			closeio();
		altered = true;
		delcount += 1;
		blankcount = 0;
	}
	if (debugging && fflush(output) == EOF)
		closeio();
}

/*
 * Format of #line directives depends on whether we know the input filename.
 */
static void
hashline(void)
{
	int e;

	if (linefile == NULL)
		e = fprintf(output, "#line %d%s", linenum, newline);
	else
		e = fprintf(output, "#line %d \"%s\"%s",
		    linenum, linefile, newline);
	if (e < 0)
		closeio();
}

/*
 * Flush the output and handle errors.
 */
static void
closeio(void)
{
	/* Tidy up after findsym(). */
	if (symdepth && !zerosyms)
		printf("\n");
	if (output != NULL && (ferror(output) || fclose(output) == EOF))
			err(2, "%s: can't write to output", filename);
	fclose(input);
}

/*
 * The driver for the state machine.
 */
static void
process(void)
{
	Linetype lineval = LT_PLAIN;
	/* When compressing blank lines, act as if the file
	   is preceded by a large number of blank lines. */
	blankmax = blankcount = 1000;
	zerosyms = true;
	newline = NULL;
	linenum = 0;
	altered = false;
	while (lineval != LT_EOF) {
		lineval = parseline();
		trans_table[ifstate[depth]][lineval]();
		debug("process line %d %s -> %s depth %d",
		    linenum, linetype_name[lineval],
		    ifstate_name[ifstate[depth]], depth);
	}
	exitstat |= altered;
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
	Linetype retval;
	Comment_state wascomment;

	wascomment = incomment;
	cp = skiphash();
	if (cp == NULL)
		return (LT_EOF);
	if (newline == NULL) {
		if (strrchr(tline, '\n') == strrchr(tline, '\r') + 1)
			newline = newline_crlf;
		else
			newline = newline_unix;
	}
	if (*cp == '\0') {
		retval = LT_PLAIN;
		goto done;
	}
	keyword = tline + (cp - tline);
	if ((cp = matchsym("ifdef", keyword)) != NULL ||
	    (cp = matchsym("ifndef", keyword)) != NULL) {
		cp = skipcomment(cp);
		if ((cursym = findsym(&cp)) < 0)
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
	} else if ((cp = matchsym("if", keyword)) != NULL)
		retval = ifeval(&cp);
	else if ((cp = matchsym("elif", keyword)) != NULL)
		retval = linetype_if2elif(ifeval(&cp));
	else if ((cp = matchsym("else", keyword)) != NULL)
		retval = LT_ELSE;
	else if ((cp = matchsym("endif", keyword)) != NULL)
		retval = LT_ENDIF;
	else {
		cp = skipsym(keyword);
		/* no way can we deal with a continuation inside a keyword */
		if (strncmp(cp, "\\\r\n", 3) == 0 ||
		    strncmp(cp, "\\\n", 2) == 0)
			Eioccc();
		cp = skipline(cp);
		retval = LT_PLAIN;
		goto done;
	}
	cp = skipcomment(cp);
	if (*cp != '\0') {
		cp = skipline(cp);
		if (retval == LT_TRUE || retval == LT_FALSE ||
		    retval == LT_TRUEI || retval == LT_FALSEI)
			retval = LT_IF;
		if (retval == LT_ELTRUE || retval == LT_ELFALSE)
			retval = LT_ELIF;
	}
	/* the following can happen if the last line of the file lacks a
	   newline or if there is too much whitespace in a directive */
	if (linestate == LS_HASH) {
		long len = cp - tline;
		if (fgets(tline + len, MAXLINE - len, input) == NULL) {
			if (ferror(input))
				err(2, "can't read %s", filename);
			/* append the missing newline at eof */
			strcpy(tline + len, newline);
			cp += strlen(newline);
			linestate = LS_START;
		} else {
			linestate = LS_DIRTY;
		}
	}
	if (retval != LT_PLAIN && (wascomment || linestate != LS_START)) {
		retval = linetype_2dodgy(retval);
		linestate = LS_DIRTY;
	}
done:
	debug("parser line %d state %s comment %s line", linenum,
	    comment_name[incomment], linestate_name[linestate]);
	return (retval);
}

/*
 * These are the binary operators that are supported by the expression
 * evaluator.
 */
static Linetype op_strict(long *p, long v, Linetype at, Linetype bt) {
	if(at == LT_IF || bt == LT_IF) return (LT_IF);
	return (*p = v, v ? LT_TRUE : LT_FALSE);
}
static Linetype op_lt(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a < b, at, bt);
}
static Linetype op_gt(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a > b, at, bt);
}
static Linetype op_le(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a <= b, at, bt);
}
static Linetype op_ge(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a >= b, at, bt);
}
static Linetype op_eq(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a == b, at, bt);
}
static Linetype op_ne(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a != b, at, bt);
}
static Linetype op_or(long *p, Linetype at, long a, Linetype bt, long b) {
	if (!strictlogic && (at == LT_TRUE || bt == LT_TRUE))
		return (*p = 1, LT_TRUE);
	return op_strict(p, a || b, at, bt);
}
static Linetype op_and(long *p, Linetype at, long a, Linetype bt, long b) {
	if (!strictlogic && (at == LT_FALSE || bt == LT_FALSE))
		return (*p = 0, LT_FALSE);
	return op_strict(p, a && b, at, bt);
}
static Linetype op_blsh(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a << b, at, bt);
}
static Linetype op_brsh(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a >> b, at, bt);
}
static Linetype op_add(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a + b, at, bt);
}
static Linetype op_sub(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a - b, at, bt);
}
static Linetype op_mul(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a * b, at, bt);
}
static Linetype op_div(long *p, Linetype at, long a, Linetype bt, long b) {
	if (bt != LT_TRUE) {
		debug("eval division by zero");
		return (LT_ERROR);
	}
	return op_strict(p, a / b, at, bt);
}
static Linetype op_mod(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a % b, at, bt);
}
static Linetype op_bor(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a | b, at, bt);
}
static Linetype op_bxor(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a ^ b, at, bt);
}
static Linetype op_band(long *p, Linetype at, long a, Linetype bt, long b) {
	return op_strict(p, a & b, at, bt);
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

typedef Linetype eval_fn(const struct ops *, long *, const char **);

static eval_fn eval_table, eval_unary;

/*
 * The precedence table. Expressions involving binary operators are evaluated
 * in a table-driven way by eval_table. When it evaluates a subexpression it
 * calls the inner function with its first argument pointing to the next
 * element of the table. Innermost expressions have special non-table-driven
 * handling.
 *
 * The stop characters help with lexical analysis: an operator is not
 * recognized if it is followed by one of the stop characters because
 * that would make it a different operator.
 */
struct op {
	const char *str;
	Linetype (*fn)(long *, Linetype, long, Linetype, long);
	const char *stop;
};
struct ops {
	eval_fn *inner;
	struct op op[5];
};
static const struct ops eval_ops[] = {
	{ eval_table, { { "||", op_or, NULL } } },
	{ eval_table, { { "&&", op_and, NULL } } },
	{ eval_table, { { "|", op_bor, "|" } } },
	{ eval_table, { { "^", op_bxor, NULL } } },
	{ eval_table, { { "&", op_band, "&" } } },
	{ eval_table, { { "==", op_eq, NULL },
			{ "!=", op_ne, NULL } } },
	{ eval_table, { { "<=", op_le, NULL },
			{ ">=", op_ge, NULL },
			{ "<", op_lt, "<=" },
			{ ">", op_gt, ">=" } } },
	{ eval_table, { { "<<", op_blsh, NULL },
			{ ">>", op_brsh, NULL } } },
	{ eval_table, { { "+", op_add, NULL },
			{ "-", op_sub, NULL } } },
	{ eval_unary, { { "*", op_mul, NULL },
			{ "/", op_div, NULL },
			{ "%", op_mod, NULL } } },
};

/* Current operator precedence level */
static long prec(const struct ops *ops)
{
	return (ops - eval_ops);
}

/*
 * Function for evaluating the innermost parts of expressions,
 * viz. !expr (expr) number defined(symbol) symbol
 * We reset the constexpr flag in the last two cases.
 */
static Linetype
eval_unary(const struct ops *ops, long *valp, const char **cpp)
{
	const char *cp;
	char *ep;
	int sym;
	bool defparen;
	Linetype lt;

	cp = skipcomment(*cpp);
	if (*cp == '!') {
		debug("eval%d !", prec(ops));
		cp++;
		lt = eval_unary(ops, valp, &cp);
		if (lt == LT_ERROR)
			return (LT_ERROR);
		if (lt != LT_IF) {
			*valp = !*valp;
			lt = *valp ? LT_TRUE : LT_FALSE;
		}
	} else if (*cp == '~') {
		debug("eval%d ~", prec(ops));
		cp++;
		lt = eval_unary(ops, valp, &cp);
		if (lt == LT_ERROR)
			return (LT_ERROR);
		if (lt != LT_IF) {
			*valp = ~(*valp);
			lt = *valp ? LT_TRUE : LT_FALSE;
		}
	} else if (*cp == '-') {
		debug("eval%d -", prec(ops));
		cp++;
		lt = eval_unary(ops, valp, &cp);
		if (lt == LT_ERROR)
			return (LT_ERROR);
		if (lt != LT_IF) {
			*valp = -(*valp);
			lt = *valp ? LT_TRUE : LT_FALSE;
		}
	} else if (*cp == '(') {
		cp++;
		debug("eval%d (", prec(ops));
		lt = eval_table(eval_ops, valp, &cp);
		if (lt == LT_ERROR)
			return (LT_ERROR);
		cp = skipcomment(cp);
		if (*cp++ != ')')
			return (LT_ERROR);
	} else if (isdigit((unsigned char)*cp)) {
		debug("eval%d number", prec(ops));
		*valp = strtol(cp, &ep, 0);
		if (ep == cp)
			return (LT_ERROR);
		lt = *valp ? LT_TRUE : LT_FALSE;
		cp = ep;
	} else if (matchsym("defined", cp) != NULL) {
		cp = skipcomment(cp+7);
		if (*cp == '(') {
			cp = skipcomment(cp+1);
			defparen = true;
		} else {
			defparen = false;
		}
		sym = findsym(&cp);
		cp = skipcomment(cp);
		if (defparen && *cp++ != ')') {
			debug("eval%d defined missing ')'", prec(ops));
			return (LT_ERROR);
		}
		if (sym < 0) {
			debug("eval%d defined unknown", prec(ops));
			lt = LT_IF;
		} else {
			debug("eval%d defined %s", prec(ops), symname[sym]);
			*valp = (value[sym] != NULL);
			lt = *valp ? LT_TRUE : LT_FALSE;
		}
		constexpr = false;
	} else if (!endsym(*cp)) {
		debug("eval%d symbol", prec(ops));
		sym = findsym(&cp);
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
		debug("eval%d bad expr", prec(ops));
		return (LT_ERROR);
	}

	*cpp = cp;
	debug("eval%d = %d", prec(ops), *valp);
	return (lt);
}

/*
 * Table-driven evaluation of binary operators.
 */
static Linetype
eval_table(const struct ops *ops, long *valp, const char **cpp)
{
	const struct op *op;
	const char *cp;
	long val = 0;
	Linetype lt, rt;

	debug("eval%d", prec(ops));
	cp = *cpp;
	lt = ops->inner(ops+1, valp, &cp);
	if (lt == LT_ERROR)
		return (LT_ERROR);
	for (;;) {
		cp = skipcomment(cp);
		for (op = ops->op; op->str != NULL; op++) {
			if (strncmp(cp, op->str, strlen(op->str)) == 0) {
				/* assume only one-char operators have stop chars */
				if (op->stop != NULL && cp[1] != '\0' &&
				    strchr(op->stop, cp[1]) != NULL)
					continue;
				else
					break;
			}
		}
		if (op->str == NULL)
			break;
		cp += strlen(op->str);
		debug("eval%d %s", prec(ops), op->str);
		rt = ops->inner(ops+1, &val, &cp);
		if (rt == LT_ERROR)
			return (LT_ERROR);
		lt = op->fn(valp, lt, *valp, rt, val);
	}

	*cpp = cp;
	debug("eval%d = %d", prec(ops), *valp);
	debug("eval%d lt = %s", prec(ops), linetype_name[lt]);
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
	Linetype ret;
	long val = 0;

	debug("eval %s", *cpp);
	constexpr = killconsts ? false : true;
	ret = eval_table(eval_ops, &val, cpp);
	debug("eval = %d", val);
	return (constexpr ? LT_IF : ret == LT_ERROR ? LT_IF : ret);
}

/*
 * Read a line and examine its initial part to determine if it is a
 * preprocessor directive. Returns NULL on EOF, or a pointer to a
 * preprocessor directive name, or a pointer to the zero byte at the
 * end of the line.
 */
static const char *
skiphash(void)
{
	const char *cp;

	linenum++;
	if (fgets(tline, MAXLINE, input) == NULL) {
		if (ferror(input))
			err(2, "can't read %s", filename);
		else
			return (NULL);
	}
	cp = skipcomment(tline);
	if (linestate == LS_START && *cp == '#') {
		linestate = LS_HASH;
		return (skipcomment(cp + 1));
	} else if (*cp == '\0') {
		return (cp);
	} else {
		return (skipline(cp));
	}
}

/*
 * Mark a line dirty and consume the rest of it, keeping track of the
 * lexical state.
 */
static const char *
skipline(const char *cp)
{
	const char *pcp;
	if (*cp != '\0')
		linestate = LS_DIRTY;
	while (*cp != '\0') {
		cp = skipcomment(pcp = cp);
		if (pcp == cp)
			cp++;
	}
	return (cp);
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
		if (strncmp(cp, "\\\r\n", 3) == 0)
			cp += 3;
		else if (strncmp(cp, "\\\n", 2) == 0)
			cp += 2;
		else switch (incomment) {
		case NO_COMMENT:
			if (strncmp(cp, "/\\\r\n", 4) == 0) {
				incomment = STARTING_COMMENT;
				cp += 4;
			} else if (strncmp(cp, "/\\\n", 3) == 0) {
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
			} else if (strchr(" \r\t", *cp) != NULL) {
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
					error("Unterminated char literal");
				else
					error("Unterminated string literal");
			} else
				cp += 1;
			continue;
		case C_COMMENT:
			if (strncmp(cp, "*\\\r\n", 4) == 0) {
				incomment = FINISHING_COMMENT;
				cp += 4;
			} else if (strncmp(cp, "*\\\n", 3) == 0) {
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
 * Skip whitespace and take a copy of any following identifier.
 */
static const char *
getsym(const char **cpp)
{
	const char *cp = *cpp, *sym;

	cp = skipcomment(cp);
	cp = skipsym(sym = cp);
	if (cp == sym)
		return NULL;
	*cpp = cp;
	return (xstrdup(sym, cp));
}

/*
 * Check that s (a symbol) matches the start of t, and that the
 * following character in t is not a symbol character. Returns a
 * pointer to the following character in t if there is a match,
 * otherwise NULL.
 */
static const char *
matchsym(const char *s, const char *t)
{
	while (*s != '\0' && *t != '\0')
		if (*s != *t)
			return (NULL);
		else
			++s, ++t;
	if (*s == '\0' && endsym(*t))
		return(t);
	else
		return(NULL);
}

/*
 * Look for the symbol in the symbol table. If it is found, we return
 * the symbol table index, else we return -1.
 */
static int
findsym(const char **strp)
{
	const char *str;
	int symind;

	str = *strp;
	*strp = skipsym(str);
	if (symlist) {
		if (*strp == str)
			return (-1);
		if (symdepth && firstsym)
			printf("%s%3d", zerosyms ? "" : "\n", depth);
		firstsym = zerosyms = false;
		printf("%s%.*s%s",
		       symdepth ? " " : "",
		       (int)(*strp-str), str,
		       symdepth ? "" : "\n");
		/* we don't care about the value of the symbol */
		return (0);
	}
	for (symind = 0; symind < nsyms; ++symind) {
		if (matchsym(symname[symind], str) != NULL) {
			debugsym("findsym", symind);
			return (symind);
		}
	}
	return (-1);
}

/*
 * Resolve indirect symbol values to their final definitions.
 */
static void
indirectsym(void)
{
	const char *cp;
	int changed, sym, ind;

	do {
		changed = 0;
		for (sym = 0; sym < nsyms; ++sym) {
			if (value[sym] == NULL)
				continue;
			cp = value[sym];
			ind = findsym(&cp);
			if (ind == -1 || ind == sym ||
			    *cp != '\0' ||
			    value[ind] == NULL ||
			    value[ind] == value[sym])
				continue;
			debugsym("indir...", sym);
			value[sym] = value[ind];
			debugsym("...ectsym", sym);
			changed++;
		}
	} while (changed);
}

/*
 * Add a symbol to the symbol table, specified with the format sym=val
 */
static void
addsym1(bool ignorethis, bool definethis, char *symval)
{
	const char *sym, *val;

	sym = symval;
	val = skipsym(sym);
	if (definethis && *val == '=') {
		symval[val - sym] = '\0';
		val = val + 1;
	} else if (*val == '\0') {
		val = definethis ? "1" : NULL;
	} else {
		usage();
	}
	addsym2(ignorethis, sym, val);
}

/*
 * Add a symbol to the symbol table.
 */
static void
addsym2(bool ignorethis, const char *sym, const char *val)
{
	const char *cp = sym;
	int symind;

	symind = findsym(&cp);
	if (symind < 0) {
		if (nsyms >= MAXSYMS)
			errx(2, "too many symbols");
		symind = nsyms++;
	}
	ignore[symind] = ignorethis;
	symname[symind] = sym;
	value[symind] = val;
	debugsym("addsym", symind);
}

static void
debugsym(const char *why, int symind)
{
	debug("%s %s%c%s", why, symname[symind],
	    value[symind] ? '=' : ' ',
	    value[symind] ? value[symind] : "undef");
}

/*
 * Add symbols to the symbol table from a file containing
 * #define and #undef preprocessor directives.
 */
static void
defundefile(const char *fn)
{
	filename = fn;
	input = fopen(fn, "rb");
	if (input == NULL)
		err(2, "can't open %s", fn);
	linenum = 0;
	while (defundef())
		;
	if (ferror(input))
		err(2, "can't read %s", filename);
	else
		fclose(input);
	if (incomment)
		error("EOF in comment");
}

/*
 * Read and process one #define or #undef directive
 */
static bool
defundef(void)
{
	const char *cp, *kw, *sym, *val, *end;

	cp = skiphash();
	if (cp == NULL)
		return (false);
	if (*cp == '\0')
		goto done;
	/* strip trailing whitespace, and do a fairly rough check to
	   avoid unsupported multi-line preprocessor directives */
	end = cp + strlen(cp);
	while (end > tline && strchr(" \t\n\r", end[-1]) != NULL)
		--end;
	if (end > tline && end[-1] == '\\')
		Eioccc();

	kw = cp;
	if ((cp = matchsym("define", kw)) != NULL) {
		sym = getsym(&cp);
		if (sym == NULL)
			error("Missing macro name in #define");
		if (*cp == '(') {
			val = "1";
		} else {
			cp = skipcomment(cp);
			val = (cp < end) ? xstrdup(cp, end) : "";
		}
		debug("#define");
		addsym2(false, sym, val);
	} else if ((cp = matchsym("undef", kw)) != NULL) {
		sym = getsym(&cp);
		if (sym == NULL)
			error("Missing macro name in #undef");
		cp = skipcomment(cp);
		debug("#undef");
		addsym2(false, sym, NULL);
	} else {
		error("Unrecognized preprocessor directive");
	}
	skipline(cp);
done:
	debug("parser line %d state %s comment %s line", linenum,
	    comment_name[incomment], linestate_name[linestate]);
	return (true);
}

/*
 * Concatenate two strings into new memory, checking for failure.
 */
static char *
astrcat(const char *s1, const char *s2)
{
	char *s;
	int len;
	size_t size;

	len = snprintf(NULL, 0, "%s%s", s1, s2);
	if (len < 0)
		err(2, "snprintf");
	size = (size_t)len + 1;
	s = (char *)malloc(size);
	if (s == NULL)
		err(2, "malloc");
	snprintf(s, size, "%s%s", s1, s2);
	return (s);
}

/*
 * Duplicate a segment of a string, checking for failure.
 */
static const char *
xstrdup(const char *start, const char *end)
{
	size_t n;
	char *s;

	if (end < start) abort(); /* bug */
	n = (size_t)(end - start) + 1;
	s = malloc(n);
	if (s == NULL)
		err(2, "malloc");
	snprintf(s, n, "%s", start);
	return (s);
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
	closeio();
	errx(2, "Output may be truncated");
}
