/*	$OpenBSD: eval.c,v 1.74 2015/02/05 12:59:57 millert Exp $	*/
/*	$NetBSD: eval.c,v 1.7 1996/11/10 21:21:29 pk Exp $	*/

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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


/*
 * eval.c
 * Facility: m4 macro processor
 * by: oz
 */

#include <sys/types.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include "mdef.h"
#include "stdd.h"
#include "extern.h"
#include "pathnames.h"

static void	dodefn(const char *);
static void	dopushdef(const char *, const char *);
static void	dodump(const char *[], int);
static void	dotrace(const char *[], int, int);
static void	doifelse(const char *[], int);
static int	doincl(const char *);
static int	dopaste(const char *);
static void	dochq(const char *[], int);
static void	dochc(const char *[], int);
static void	dom4wrap(const char *);
static void	dodiv(int);
static void	doundiv(const char *[], int);
static void	dosub(const char *[], int);
static void	map(char *, const char *, const char *, const char *);
static const char *handledash(char *, char *, const char *);
static void	expand_builtin(const char *[], int, int);
static void	expand_macro(const char *[], int);
static void	dump_one_def(const char *, struct macro_definition *);

unsigned long	expansion_id;

/*
 * eval - eval all macros and builtins calls
 *	  argc - number of elements in argv.
 *	  argv - element vector :
 *			argv[0] = definition of a user
 *				  macro or NULL if built-in.
 *			argv[1] = name of the macro or
 *				  built-in.
 *			argv[2] = parameters to user-defined
 *			   .	  macro or built-in.
 *			   .
 *
 * A call in the form of macro-or-builtin() will result in:
 *			argv[0] = nullstr
 *			argv[1] = macro-or-builtin
 *			argv[2] = nullstr
 *
 * argc is 3 for macro-or-builtin() and 2 for macro-or-builtin
 */
void
eval(const char *argv[], int argc, int td, int is_traced)
{
	size_t mark = SIZE_MAX;

	expansion_id++;
	if (td & RECDEF)
		m4errx(1, "expanding recursive definition for %s.", argv[1]);
	if (is_traced)
		mark = trace(argv, argc, infile+ilevel);
	if (td == MACRTYPE)
		expand_macro(argv, argc);
	else
		expand_builtin(argv, argc, td);
	if (mark != SIZE_MAX)
		finish_trace(mark);
}

/*
 * expand_builtin - evaluate built-in macros.
 */
void
expand_builtin(const char *argv[], int argc, int td)
{
	int c, n;
	int ac;
	static int sysval = 0;

#ifdef DEBUG
	printf("argc = %d\n", argc);
	for (n = 0; n < argc; n++)
		printf("argv[%d] = %s\n", n, argv[n]);
	fflush(stdout);
#endif

 /*
  * if argc == 3 and argv[2] is null, then we
  * have macro-or-builtin() type call. We adjust
  * argc to avoid further checking..
  */
 /* we keep the initial value for those built-ins that differentiate
  * between builtin() and builtin.
  */
	ac = argc;

	if (argc == 3 && !*(argv[2]) && !mimic_gnu)
		argc--;

	switch (td & TYPEMASK) {

	case DEFITYPE:
		if (argc > 2)
			dodefine(argv[2], (argc > 3) ? argv[3] : null);
		break;

	case PUSDTYPE:
		if (argc > 2)
			dopushdef(argv[2], (argc > 3) ? argv[3] : null);
		break;

	case DUMPTYPE:
		dodump(argv, argc);
		break;

	case TRACEONTYPE:
		dotrace(argv, argc, 1);
		break;

	case TRACEOFFTYPE:
		dotrace(argv, argc, 0);
		break;

	case EXPRTYPE:
	/*
	 * doexpr - evaluate arithmetic
	 * expression
	 */
	{
		int base = 10;
		int maxdigits = 0;
		const char *errstr;

		if (argc > 3) {
			base = strtonum(argv[3], 2, 36, &errstr);
			if (errstr) {
				m4errx(1, "expr: base %s invalid.", argv[3]);
			}
		}
		if (argc > 4) {
			maxdigits = strtonum(argv[4], 0, INT_MAX, &errstr);
			if (errstr) {
				m4errx(1, "expr: maxdigits %s invalid.", argv[4]);
			}
		}
		if (argc > 2)
			pbnumbase(expr(argv[2]), base, maxdigits);
		break;
	}

	case IFELTYPE:
		if (argc > 4)
			doifelse(argv, argc);
		break;

	case IFDFTYPE:
	/*
	 * doifdef - select one of two
	 * alternatives based on the existence of
	 * another definition
	 */
		if (argc > 3) {
			if (lookup_macro_definition(argv[2]) != NULL)
				pbstr(argv[3]);
			else if (argc > 4)
				pbstr(argv[4]);
		}
		break;

	case LENGTYPE:
	/*
	 * dolen - find the length of the
	 * argument
	 */
		pbnum((argc > 2) ? strlen(argv[2]) : 0);
		break;

	case INCRTYPE:
	/*
	 * doincr - increment the value of the
	 * argument
	 */
		if (argc > 2)
			pbnum(atoi(argv[2]) + 1);
		break;

	case DECRTYPE:
	/*
	 * dodecr - decrement the value of the
	 * argument
	 */
		if (argc > 2)
			pbnum(atoi(argv[2]) - 1);
		break;

	case SYSCTYPE:
	/*
	 * dosys - execute system command
	 */
		if (argc > 2) {
			fflush(stdout);
			sysval = system(argv[2]);
		}
		break;

	case SYSVTYPE:
	/*
	 * dosysval - return value of the last
	 * system call.
	 *
	 */
		pbnum(sysval);
		break;

	case ESYSCMDTYPE:
		if (argc > 2)
			doesyscmd(argv[2]);
		break;
	case INCLTYPE:
		if (argc > 2) {
			if (!doincl(argv[2])) {
				if (mimic_gnu) {
					warn("%s at line %lu: include(%s)",
					    CURRENT_NAME, CURRENT_LINE, argv[2]);
					exit_code = 1;
				} else
					err(1, "%s at line %lu: include(%s)",
					    CURRENT_NAME, CURRENT_LINE, argv[2]);
			}
		}
		break;

	case SINCTYPE:
		if (argc > 2)
			(void) doincl(argv[2]);
		break;
#ifdef EXTENDED
	case PASTTYPE:
		if (argc > 2)
			if (!dopaste(argv[2]))
				err(1, "%s at line %lu: paste(%s)", 
				    CURRENT_NAME, CURRENT_LINE, argv[2]);
		break;

	case SPASTYPE:
		if (argc > 2)
			(void) dopaste(argv[2]);
		break;
	case FORMATTYPE:
		doformat(argv, argc);
		break;
#endif
	case CHNQTYPE:
		dochq(argv, ac);
		break;

	case CHNCTYPE:
		dochc(argv, argc);
		break;

	case SUBSTYPE:
	/*
	 * dosub - select substring
	 *
	 */
		if (argc > 3)
			dosub(argv, argc);
		break;

	case SHIFTYPE:
	/*
	 * doshift - push back all arguments
	 * except the first one (i.e. skip
	 * argv[2])
	 */
		if (argc > 3) {
			for (n = argc - 1; n > 3; n--) {
				pbstr(rquote);
				pbstr(argv[n]);
				pbstr(lquote);
				pushback(COMMA);
			}
			pbstr(rquote);
			pbstr(argv[3]);
			pbstr(lquote);
		}
		break;

	case DIVRTYPE:
		if (argc > 2 && (n = atoi(argv[2])) != 0)
			dodiv(n);
		else {
			active = stdout;
			oindex = 0;
		}
		break;

	case UNDVTYPE:
		doundiv(argv, argc);
		break;

	case DIVNTYPE:
	/*
	 * dodivnum - return the number of
	 * current output diversion
	 */
		pbnum(oindex);
		break;

	case UNDFTYPE:
	/*
	 * doundefine - undefine a previously
	 * defined macro(s) or m4 keyword(s).
	 */
		if (argc > 2)
			for (n = 2; n < argc; n++)
				macro_undefine(argv[n]);
		break;

	case POPDTYPE:
	/*
	 * dopopdef - remove the topmost
	 * definitions of macro(s) or m4
	 * keyword(s).
	 */
		if (argc > 2)
			for (n = 2; n < argc; n++)
				macro_popdef(argv[n]);
		break;

	case MKTMTYPE:
	/*
	 * dotemp - create a temporary file
	 */
		if (argc > 2) {
			int fd;
			char *temp;

			temp = xstrdup(argv[2]);

			fd = mkstemp(temp);
			if (fd == -1)
				err(1,
	    "%s at line %lu: couldn't make temp file %s",
	    CURRENT_NAME, CURRENT_LINE, argv[2]);
			close(fd);
			pbstr(temp);
			free(temp);
		}
		break;

	case TRNLTYPE:
	/*
	 * dotranslit - replace all characters in
	 * the source string that appears in the
	 * "from" string with the corresponding
	 * characters in the "to" string.
	 */
		if (argc > 3) {
			char *temp;

			temp = xalloc(strlen(argv[2])+1, NULL);
			if (argc > 4)
				map(temp, argv[2], argv[3], argv[4]);
			else
				map(temp, argv[2], argv[3], null);
			pbstr(temp);
			free(temp);
		} else if (argc > 2)
			pbstr(argv[2]);
		break;

	case INDXTYPE:
	/*
	 * doindex - find the index of the second
	 * argument string in the first argument
	 * string. -1 if not present.
	 */
		pbnum((argc > 3) ? indx(argv[2], argv[3]) : -1);
		break;

	case ERRPTYPE:
	/*
	 * doerrp - print the arguments to stderr
	 * file
	 */
		if (argc > 2) {
			for (n = 2; n < argc; n++)
				fprintf(stderr, "%s ", argv[n]);
			fprintf(stderr, "\n");
		}
		break;

	case DNLNTYPE:
	/*
	 * dodnl - eat-up-to and including
	 * newline
	 */
		while ((c = gpbc()) != '\n' && c != EOF)
			;
		break;

	case M4WRTYPE:
	/*
	 * dom4wrap - set up for
	 * wrap-up/wind-down activity
	 */
		if (argc > 2)
			dom4wrap(argv[2]);
		break;

	case EXITTYPE:
	/*
	 * doexit - immediate exit from m4.
	 */
		killdiv();
		exit((argc > 2) ? atoi(argv[2]) : 0);
		break;

	case DEFNTYPE:
		if (argc > 2)
			for (n = 2; n < argc; n++)
				dodefn(argv[n]);
		break;

	case INDIRTYPE:	/* Indirect call */
		if (argc > 2)
			doindir(argv, argc);
		break;

	case BUILTINTYPE: /* Builtins only */
		if (argc > 2)
			dobuiltin(argv, argc);
		break;

	case PATSTYPE:
		if (argc > 2)
			dopatsubst(argv, argc);
		break;
	case REGEXPTYPE:
		if (argc > 2)
			doregexp(argv, argc);
		break;
	case LINETYPE:
		doprintlineno(infile+ilevel);
		break;
	case FILENAMETYPE:
		doprintfilename(infile+ilevel);
		break;
	case SELFTYPE:
		pbstr(rquote);
		pbstr(argv[1]);
		pbstr(lquote);
		break;
	default:
		m4errx(1, "eval: major botch.");
		break;
	}
}

/*
 * expand_macro - user-defined macro expansion
 */
void
expand_macro(const char *argv[], int argc)
{
	const char *t;
	const char *p;
	int n;
	int argno;

	t = argv[0];		       /* defn string as a whole */
	p = t;
	while (*p)
		p++;
	p--;			       /* last character of defn */
	while (p > t) {
		if (*(p - 1) != ARGFLAG)
			PUSHBACK(*p);
		else {
			switch (*p) {

			case '#':
				pbnum(argc - 2);
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				if ((argno = *p - '0') < argc - 1)
					pbstr(argv[argno + 1]);
				break;
			case '*':
				if (argc > 2) {
					for (n = argc - 1; n > 2; n--) {
						pbstr(argv[n]);
						pushback(COMMA);
					}
					pbstr(argv[2]);
				}
				break;
                        case '@':
				if (argc > 2) {
					for (n = argc - 1; n > 2; n--) {
						pbstr(rquote);
						pbstr(argv[n]);
						pbstr(lquote);
						pushback(COMMA);
					}
					pbstr(rquote);
					pbstr(argv[2]);
					pbstr(lquote);
				}
                                break;
			default:
				PUSHBACK(*p);
				PUSHBACK('$');
				break;
			}
			p--;
		}
		p--;
	}
	if (p == t)		       /* do last character */
		PUSHBACK(*p);
}


/*
 * dodefine - install definition in the table
 */
void
dodefine(const char *name, const char *defn)
{
	if (!*name && !mimic_gnu)
		m4errx(1, "null definition.");
	else
		macro_define(name, defn);
}

/*
 * dodefn - push back a quoted definition of
 *      the given name.
 */
static void
dodefn(const char *name)
{
	struct macro_definition *p;

	if ((p = lookup_macro_definition(name)) != NULL) {
		if ((p->type & TYPEMASK) == MACRTYPE) {
			pbstr(rquote);
			pbstr(p->defn);
			pbstr(lquote);
		} else {
			pbstr(p->defn);
			pbstr(BUILTIN_MARKER);
		}
	}
}

/*
 * dopushdef - install a definition in the hash table
 *      without removing a previous definition. Since
 *      each new entry is entered in *front* of the
 *      hash bucket, it hides a previous definition from
 *      lookup.
 */
static void
dopushdef(const char *name, const char *defn)
{
	if (!*name && !mimic_gnu)
		m4errx(1, "null definition.");
	else
		macro_pushdef(name, defn);
}

/*
 * dump_one_def - dump the specified definition.
 */
static void
dump_one_def(const char *name, struct macro_definition *p)
{
	if (!traceout)
		traceout = stderr;
	if (mimic_gnu) {
		if ((p->type & TYPEMASK) == MACRTYPE)
			fprintf(traceout, "%s:\t%s\n", name, p->defn);
		else {
			fprintf(traceout, "%s:\t<%s>\n", name, p->defn);
		}
	} else
		fprintf(traceout, "`%s'\t`%s'\n", name, p->defn);
}

/*
 * dodumpdef - dump the specified definitions in the hash
 *      table to stderr. If nothing is specified, the entire
 *      hash table is dumped.
 */
static void
dodump(const char *argv[], int argc)
{
	int n;
	struct macro_definition *p;

	if (argc > 2) {
		for (n = 2; n < argc; n++)
			if ((p = lookup_macro_definition(argv[n])) != NULL)
				dump_one_def(argv[n], p);
	} else
		macro_for_all(dump_one_def);
}

/*
 * dotrace - mark some macros as traced/untraced depending upon on.
 */
static void
dotrace(const char *argv[], int argc, int on)
{
	int n;

	if (argc > 2) {
		for (n = 2; n < argc; n++)
			mark_traced(argv[n], on);
	} else
		mark_traced(NULL, on);
}

/*
 * doifelse - select one of two alternatives - loop.
 */
static void
doifelse(const char *argv[], int argc)
{
	cycle {
		if (STREQ(argv[2], argv[3]))
			pbstr(argv[4]);
		else if (argc == 6)
			pbstr(argv[5]);
		else if (argc > 6) {
			argv += 3;
			argc -= 3;
			continue;
		}
		break;
	}
}

/*
 * doinclude - include a given file.
 */
static int
doincl(const char *ifile)
{
	if (ilevel + 1 == MAXINP)
		m4errx(1, "too many include files.");
	if (fopen_trypath(infile+ilevel+1, ifile) != NULL) {
		ilevel++;
		bbase[ilevel] = bufbase = bp;
		return (1);
	} else
		return (0);
}

#ifdef EXTENDED
/*
 * dopaste - include a given file without any
 *           macro processing.
 */
static int
dopaste(const char *pfile)
{
	FILE *pf;
	int c;

	if ((pf = fopen(pfile, "r")) != NULL) {
		if (synch_lines)
		    fprintf(active, "#line 1 \"%s\"\n", pfile);
		while ((c = getc(pf)) != EOF)
			putc(c, active);
		(void) fclose(pf);
		emit_synchline();
		return (1);
	} else
		return (0);
}
#endif

/*
 * dochq - change quote characters
 */
static void
dochq(const char *argv[], int ac)
{
	if (ac == 2) {
		lquote[0] = LQUOTE; lquote[1] = EOS;
		rquote[0] = RQUOTE; rquote[1] = EOS;
	} else {
		strlcpy(lquote, argv[2], sizeof(lquote));
		if (ac > 3) {
			strlcpy(rquote, argv[3], sizeof(rquote));
		} else {
			rquote[0] = ECOMMT; rquote[1] = EOS;
		}
	}
}

/*
 * dochc - change comment characters
 */
static void
dochc(const char *argv[], int argc)
{
/* XXX Note that there is no difference between no argument and a single
 * empty argument.
 */
	if (argc == 2) {
		scommt[0] = EOS;
		ecommt[0] = EOS;
	} else {
		strlcpy(scommt, argv[2], sizeof(scommt));
		if (argc == 3) {
			ecommt[0] = ECOMMT; ecommt[1] = EOS;
		} else {
			strlcpy(ecommt, argv[3], sizeof(ecommt));
		}
	}
}

/*
 * dom4wrap - expand text at EOF
 */
static void
dom4wrap(const char *text)
{
	if (wrapindex >= maxwraps) {
		if (maxwraps == 0)
			maxwraps = 16;
		else
			maxwraps *= 2;
		m4wraps = xreallocarray(m4wraps, maxwraps, sizeof(*m4wraps),
		   "too many m4wraps");
	}
	m4wraps[wrapindex++] = xstrdup(text);
}

/*
 * dodivert - divert the output to a temporary file
 */
static void
dodiv(int n)
{
	int fd;

	oindex = n;
	if (n >= maxout) {
		if (mimic_gnu)
			resizedivs(n + 10);
		else
			n = 0;		/* bitbucket */
	}

	if (n < 0)
		n = 0;		       /* bitbucket */
	if (outfile[n] == NULL) {
		char fname[] = _PATH_DIVNAME;

		if ((fd = mkstemp(fname)) < 0 ||
		    unlink(fname) == -1 ||
		    (outfile[n] = fdopen(fd, "w+")) == NULL)
			err(1, "%s: cannot divert", fname);
	}
	active = outfile[n];
}

/*
 * doundivert - undivert a specified output, or all
 *              other outputs, in numerical order.
 */
static void
doundiv(const char *argv[], int argc)
{
	int ind;
	int n;

	if (argc > 2) {
		for (ind = 2; ind < argc; ind++) {
			const char *errstr;
			n = strtonum(argv[ind], 1, INT_MAX, &errstr);
			if (errstr) {
				if (errno == EINVAL && mimic_gnu)
					getdivfile(argv[ind]);
			} else {
				if (n < maxout && outfile[n] != NULL)
					getdiv(n);
			}
		}
	}
	else
		for (n = 1; n < maxout; n++)
			if (outfile[n] != NULL)
				getdiv(n);
}

/*
 * dosub - select substring
 */
static void
dosub(const char *argv[], int argc)
{
	const char *ap, *fc, *k;
	int nc;

	ap = argv[2];		       /* target string */
#ifdef EXPR
	fc = ap + expr(argv[3]);       /* first char */
#else
	fc = ap + atoi(argv[3]);       /* first char */
#endif
	nc = strlen(fc);
	if (argc >= 5)
#ifdef EXPR
		nc = min(nc, expr(argv[4]));
#else
		nc = min(nc, atoi(argv[4]));
#endif
	if (fc >= ap && fc < ap + strlen(ap))
		for (k = fc + nc - 1; k >= fc; k--)
			pushback(*k);
}

/*
 * map:
 * map every character of s1 that is specified in from
 * into s3 and replace in s. (source s1 remains untouched)
 *
 * This is derived from the a standard implementation of map(s,from,to) 
 * function of ICON language. Within mapvec, we replace every character 
 * of "from" with the corresponding character in "to". 
 * If "to" is shorter than "from", than the corresponding entries are null, 
 * which means that those characters disappear altogether. 
 */
static void
map(char *dest, const char *src, const char *from, const char *to)
{
	const char *tmp;
	unsigned char sch, dch;
	static char frombis[257];
	static char tobis[257];
	int i;
	char seen[256];
	static unsigned char mapvec[256] = {
	    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	    19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
	    36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
	    53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69,
	    70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86,
	    87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102,
	    103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115,
	    116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
	    129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141,
	    142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154,
	    155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167,
	    168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180,
	    181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193,
	    194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206,
	    207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219,
	    220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232,
	    233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245,
	    246, 247, 248, 249, 250, 251, 252, 253, 254, 255
	};

	if (*src) {
		if (mimic_gnu) {
			/*
			 * expand character ranges on the fly
			 */
			from = handledash(frombis, frombis + 256, from);
			to = handledash(tobis, tobis + 256, to);
		}
		tmp = from;
	/*
	 * create a mapping between "from" and
	 * "to"
	 */
		for (i = 0; i < 256; i++)
			seen[i] = 0;
		while (*from) {
			if (!seen[(unsigned char)(*from)]) {
				mapvec[(unsigned char)(*from)] = (unsigned char)(*to);
				seen[(unsigned char)(*from)] = 1;
			}
			from++;
			if (*to)
				to++;
		}

		while (*src) {
			sch = (unsigned char)(*src++);
			dch = mapvec[sch];
			if ((*dest = (char)dch))
				dest++;
		}
	/*
	 * restore all the changed characters
	 */
		while (*tmp) {
			mapvec[(unsigned char)(*tmp)] = (unsigned char)(*tmp);
			tmp++;
		}
	}
	*dest = '\0';
}


/*
 * handledash:
 *  use buffer to copy the src string, expanding character ranges
 * on the way.
 */
static const char *
handledash(char *buffer, char *end, const char *src)
{
	char *p;

	p = buffer;
	while(*src) {
		if (src[1] == '-' && src[2]) {
			unsigned char i;
			if ((unsigned char)src[0] <= (unsigned char)src[2]) {
				for (i = (unsigned char)src[0]; 
				    i <= (unsigned char)src[2]; i++) {
					*p++ = i;
					if (p == end) {
						*p = '\0';
						return buffer;
					}
				}
			} else {
				for (i = (unsigned char)src[0]; 
				    i >= (unsigned char)src[2]; i--) {
					*p++ = i;
					if (p == end) {
						*p = '\0';
						return buffer;
					}
				}
			}
			src += 3;
		} else
			*p++ = *src++;
		if (p == end)
			break;
	}
	*p = '\0';
	return buffer;
}
