/*	$OpenBSD: extern.h,v 1.54 2014/05/12 19:11:19 espie Exp $	*/
/*	$NetBSD: extern.h,v 1.3 1996/01/13 23:25:24 pk Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1991, 1993
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
 *
 *	@(#)extern.h	8.1 (Berkeley) 6/6/93
 * $FreeBSD$
 */

/* eval.c */
extern void	eval(const char *[], int, int, int);
extern void	dodefine(const char *, const char *);
extern unsigned long expansion_id;

/* expr.c */
extern int	expr(const char *);

/* gnum4.c */
extern void	addtoincludepath(const char *);
extern struct input_file *fopen_trypath(struct input_file *, const char *);
extern void doindir(const char *[], int);
extern void dobuiltin(const char *[], int);
extern void dopatsubst(const char *[], int);
extern void doregexp(const char *[], int);

extern void doprintlineno(struct input_file *);
extern void doprintfilename(struct input_file *);

extern void doesyscmd(const char *);
extern void getdivfile(const char *);
extern void doformat(const char *[], int);

/* look.c */

#define FLAG_UNTRACED 0
#define FLAG_TRACED 1
#define FLAG_NO_TRACE 2

extern void	init_macros(void);
extern ndptr	lookup(const char *);
extern void mark_traced(const char *, int);
extern struct ohash macros;

extern struct macro_definition *lookup_macro_definition(const char *);
extern void	macro_define(const char *, const char *);
extern void	macro_pushdef(const char *, const char *);
extern void	macro_popdef(const char *);
extern void	macro_undefine(const char *);
extern void	setup_builtin(const char *, unsigned int);
extern void	macro_for_all(void (*)(const char *, struct macro_definition *));
#define macro_getdef(p)		((p)->d)
#define macro_name(p)		((p)->name)
#define macro_builtin_type(p)	((p)->builtin_type)
#define is_traced(p) ((p)->trace_flags == FLAG_NO_TRACE ? (trace_flags & TRACE_ALL) : (p)->trace_flags)

extern ndptr macro_getbuiltin(const char *);

/* main.c */
extern void outputstr(const char *);
extern void do_emit_synchline(void);
extern int exit_code;
#define emit_synchline() do { if (synch_lines) do_emit_synchline(); } while(0)

/* misc.c */
extern void	chrsave(int);
extern char	*compute_prevep(void);
extern void	getdiv(int);
extern ptrdiff_t indx(const char *, const char *);
extern void	initspaces(void);
extern void	killdiv(void);
extern void	onintr(int);
extern void	pbnum(int);
extern void	pbnumbase(int, int, int);
extern void	pbunsigned(unsigned long);
extern void	pbstr(const char *);
extern void	pushback(int);
extern void	*xalloc(size_t, const char *, ...) __printf0like(2, 3);
extern void	*xcalloc(size_t, size_t, const char *, ...) __printf0like(3, 4);
extern void	*xrealloc(void *, size_t, const char *, ...) __printf0like(3, 4);
extern void	*xreallocarray(void *, size_t, size_t, const char *, ...) __printf0like(4, 5);
extern char	*xstrdup(const char *);
extern void	usage(void);
extern void	resizedivs(int);
extern size_t	buffer_mark(void);
extern void	dump_buffer(FILE *, size_t);
extern void	m4errx(int, const char *, ...) __dead2 __printf0like(2, 3);

extern int	obtain_char(struct input_file *);
extern void	set_input(struct input_file *, FILE *, const char *);
extern void	release_input(struct input_file *);

/* speeded-up versions of chrsave/pushback */
#define PUSHBACK(c)				\
	do {					\
		if (bp >= endpbb)		\
			enlarge_bufspace();	\
		*bp++ = (c);			\
	} while(0)

#define CHRSAVE(c)				\
	do {					\
		if (ep >= endest)		\
			enlarge_strspace();	\
		*ep++ = (c);			\
	} while(0)

/* and corresponding exposure for local symbols */
extern void enlarge_bufspace(void);
extern void enlarge_strspace(void);
extern unsigned char *endpbb;
extern char *endest;

/* trace.c */
extern unsigned int trace_flags;
#define TRACE_ALL	512
extern void trace_file(const char *);
extern size_t trace(const char **, int, struct input_file *);
extern void finish_trace(size_t);
extern void set_trace_flags(const char *);
extern FILE *traceout;

extern stae *mstack;		/* stack of m4 machine */
extern char *sstack;		/* shadow stack, for string space extension */
extern FILE *active;		/* active output file pointer */
extern struct input_file infile[];/* input file stack (0=stdin) */
extern FILE **outfile;		/* diversion array(0=bitbucket) */
extern int maxout;		/* maximum number of diversions */
extern int fp;			/* m4 call frame pointer */
extern int ilevel;		/* input file stack pointer */
extern int oindex;		/* diversion index. */
extern int sp;			/* current m4 stack pointer */
extern unsigned char *bp;	/* first available character */
extern unsigned char *buf;	/* push-back buffer */
extern unsigned char *bufbase;	/* buffer base for this ilevel */
extern unsigned char *bbase[];	/* buffer base per ilevel */
extern char ecommt[MAXCCHARS+1];/* end character for comment */
extern char *ep;		/* first free char in strspace */
extern char lquote[MAXCCHARS+1];/* left quote character (`) */
extern char **m4wraps;		/* m4wrap string default. */
extern int maxwraps;		/* size of m4wraps array */
extern int wrapindex;		/* current index in m4wraps */

extern const char *null;	/* as it says.. just a null. */
extern char rquote[MAXCCHARS+1];/* right quote character (') */
extern char scommt[MAXCCHARS+1];/* start character for comment */
extern int  synch_lines;	/* line synchronisation directives */

extern int mimic_gnu;		/* behaves like gnu-m4 */
extern int prefix_builtins;	/* prefix builtin macros with m4_ */

