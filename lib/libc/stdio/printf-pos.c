/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)vfprintf.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This is the code responsible for handling positional arguments
 * (%m$ and %m$.n$) for vfprintf() and vfwprintf().
 */

#include "namespace.h"
#include <sys/types.h>

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "un-namespace.h"
#include "printflocal.h"

#ifdef	NL_ARGMAX
#define	MAX_POSARG	NL_ARGMAX
#else
#define	MAX_POSARG	65536
#endif

/*
 * Type ids for argument type table.
 */
enum typeid {
	T_UNUSED, TP_SHORT, T_INT, T_U_INT, TP_INT,
	T_LONG, T_U_LONG, TP_LONG, T_LLONG, T_U_LLONG, TP_LLONG,
	T_PTRDIFFT, TP_PTRDIFFT, T_SSIZET, T_SIZET, TP_SSIZET,
	T_INTMAXT, T_UINTMAXT, TP_INTMAXT, TP_VOID, TP_CHAR, TP_SCHAR,
	T_DOUBLE, T_LONG_DOUBLE, T_WINT, TP_WCHAR
};

/* An expandable array of types. */
struct typetable {
	enum typeid *table; /* table of types */
	enum typeid stattable[STATIC_ARG_TBL_SIZE];
	u_int tablesize;	/* current size of type table */
	u_int tablemax;		/* largest used index in table */
	u_int nextarg;		/* 1-based argument index */
};

static int	__grow_type_table(struct typetable *);
static void	build_arg_table (struct typetable *, va_list, union arg **);

/*
 * Initialize a struct typetable.
 */
static inline void
inittypes(struct typetable *types)
{
	u_int n;

	types->table = types->stattable;
	types->tablesize = STATIC_ARG_TBL_SIZE;
	types->tablemax = 0; 
	types->nextarg = 1;
	for (n = 0; n < STATIC_ARG_TBL_SIZE; n++)
		types->table[n] = T_UNUSED;
}

/*
 * struct typetable destructor.
 */ 
static inline void
freetypes(struct typetable *types)
{

	if (types->table != types->stattable)
		free (types->table);
}

/*
 * Ensure that there is space to add a new argument type to the type table.
 * Expand the table if necessary. Returns 0 on success.
 */
static inline int
_ensurespace(struct typetable *types)
{

	if (types->nextarg >= types->tablesize) {
		if (__grow_type_table(types))
			return (-1);
	}
	if (types->nextarg > types->tablemax)
		types->tablemax = types->nextarg;
	return (0);
}

/*
 * Add an argument type to the table, expanding if necessary.
 * Returns 0 on success.
 */
static inline int
addtype(struct typetable *types, enum typeid type)
{

	if (_ensurespace(types))
		return (-1);
	types->table[types->nextarg++] = type;
	return (0);
}

static inline int
addsarg(struct typetable *types, int flags)
{

	if (_ensurespace(types))
		return (-1);
	if (flags & INTMAXT)
		types->table[types->nextarg++] = T_INTMAXT;
	else if (flags & SIZET)
		types->table[types->nextarg++] = T_SSIZET;
	else if (flags & PTRDIFFT)
		types->table[types->nextarg++] = T_PTRDIFFT;
	else if (flags & LLONGINT)
		types->table[types->nextarg++] = T_LLONG;
	else if (flags & LONGINT)
		types->table[types->nextarg++] = T_LONG;
	else
		types->table[types->nextarg++] = T_INT;
	return (0);
}

static inline int
adduarg(struct typetable *types, int flags)
{

	if (_ensurespace(types))
		return (-1);
	if (flags & INTMAXT)
		types->table[types->nextarg++] = T_UINTMAXT;
	else if (flags & SIZET)
		types->table[types->nextarg++] = T_SIZET;
	else if (flags & PTRDIFFT)
		types->table[types->nextarg++] = T_SIZET;
	else if (flags & LLONGINT)
		types->table[types->nextarg++] = T_U_LLONG;
	else if (flags & LONGINT)
		types->table[types->nextarg++] = T_U_LONG;
	else
		types->table[types->nextarg++] = T_U_INT;
	return (0);
}

/*
 * Add * arguments to the type array.
 */
static inline int
addaster(struct typetable *types, char **fmtp)
{
	char *cp;
	u_int n2;

	n2 = 0;
	cp = *fmtp;
	while (is_digit(*cp)) {
		n2 = 10 * n2 + to_digit(*cp);
		cp++;
	}
	if (*cp == '$') {
		u_int hold = types->nextarg;
		types->nextarg = n2;
		if (addtype(types, T_INT))
			return (-1);
		types->nextarg = hold;
		*fmtp = ++cp;
	} else {
		if (addtype(types, T_INT))
			return (-1);
	}
	return (0);
}

static inline int
addwaster(struct typetable *types, wchar_t **fmtp)
{
	wchar_t *cp;
	u_int n2;

	n2 = 0;
	cp = *fmtp;
	while (is_digit(*cp)) {
		n2 = 10 * n2 + to_digit(*cp);
		cp++;
	}
	if (*cp == '$') {
		u_int hold = types->nextarg;
		types->nextarg = n2;
		if (addtype(types, T_INT))
			return (-1);
		types->nextarg = hold;
		*fmtp = ++cp;
	} else {
		if (addtype(types, T_INT))
			return (-1);
	}
	return (0);
}

/*
 * Find all arguments when a positional parameter is encountered.  Returns a
 * table, indexed by argument number, of pointers to each arguments.  The
 * initial argument table should be an array of STATIC_ARG_TBL_SIZE entries.
 * It will be replaces with a malloc-ed one if it overflows.
 * Returns 0 on success. On failure, returns nonzero and sets errno.
 */ 
int
__find_arguments (const char *fmt0, va_list ap, union arg **argtable)
{
	char *fmt;		/* format string */
	int ch;			/* character from fmt */
	u_int n;		/* handy integer (short term usage) */
	int error;
	int flags;		/* flags as above */
	struct typetable types;	/* table of types */

	fmt = (char *)fmt0;
	inittypes(&types);
	error = 0;

	/*
	 * Scan the format for conversions (`%' character).
	 */
	for (;;) {
		while ((ch = *fmt) != '\0' && ch != '%')
			fmt++;
		if (ch == '\0')
			goto done;
		fmt++;		/* skip over '%' */

		flags = 0;

rflag:		ch = *fmt++;
reswitch:	switch (ch) {
		case ' ':
		case '#':
			goto rflag;
		case '*':
			if ((error = addaster(&types, &fmt)))
				goto error;
			goto rflag;
		case '-':
		case '+':
		case '\'':
			goto rflag;
		case '.':
			if ((ch = *fmt++) == '*') {
				if ((error = addaster(&types, &fmt)))
					goto error;
				goto rflag;
			}
			while (is_digit(ch)) {
				ch = *fmt++;
			}
			goto reswitch;
		case '0':
			goto rflag;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do {
				n = 10 * n + to_digit(ch);
				/* Detect overflow */
				if (n > MAX_POSARG) {
					error = -1;
					goto error;
				}
				ch = *fmt++;
			} while (is_digit(ch));
			if (ch == '$') {
				types.nextarg = n;
				goto rflag;
			}
			goto reswitch;
#ifndef NO_FLOATING_POINT
		case 'L':
			flags |= LONGDBL;
			goto rflag;
#endif
		case 'h':
			if (flags & SHORTINT) {
				flags &= ~SHORTINT;
				flags |= CHARINT;
			} else
				flags |= SHORTINT;
			goto rflag;
		case 'j':
			flags |= INTMAXT;
			goto rflag;
		case 'l':
			if (flags & LONGINT) {
				flags &= ~LONGINT;
				flags |= LLONGINT;
			} else
				flags |= LONGINT;
			goto rflag;
		case 'q':
			flags |= LLONGINT;	/* not necessarily */
			goto rflag;
		case 't':
			flags |= PTRDIFFT;
			goto rflag;
		case 'z':
			flags |= SIZET;
			goto rflag;
		case 'C':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'c':
			error = addtype(&types,
					(flags & LONGINT) ? T_WINT : T_INT);
			if (error)
				goto error;
			break;
		case 'D':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'd':
		case 'i':
			if ((error = addsarg(&types, flags)))
				goto error;
			break;
#ifndef NO_FLOATING_POINT
		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'f':
		case 'g':
		case 'G':
			error = addtype(&types,
			    (flags & LONGDBL) ? T_LONG_DOUBLE : T_DOUBLE);
			if (error)
				goto error;
			break;
#endif /* !NO_FLOATING_POINT */
		case 'n':
			if (flags & INTMAXT)
				error = addtype(&types, TP_INTMAXT);
			else if (flags & PTRDIFFT)
				error = addtype(&types, TP_PTRDIFFT);
			else if (flags & SIZET)
				error = addtype(&types, TP_SSIZET);
			else if (flags & LLONGINT)
				error = addtype(&types, TP_LLONG);
			else if (flags & LONGINT)
				error = addtype(&types, TP_LONG);
			else if (flags & SHORTINT)
				error = addtype(&types, TP_SHORT);
			else if (flags & CHARINT)
				error = addtype(&types, TP_SCHAR);
			else
				error = addtype(&types, TP_INT);
			if (error)
				goto error;
			continue;	/* no output */
		case 'O':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'o':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		case 'p':
			if ((error = addtype(&types, TP_VOID)))
				goto error;
			break;
		case 'S':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 's':
			error = addtype(&types,
					(flags & LONGINT) ? TP_WCHAR : TP_CHAR);
			if (error)
				goto error;
			break;
		case 'U':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'u':
		case 'X':
		case 'x':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		default:	/* "%?" prints ?, unless ? is NUL */
			if (ch == '\0')
				goto done;
			break;
		}
	}
done:
	build_arg_table(&types, ap, argtable);
error:
	freetypes(&types);
	return (error || *argtable == NULL);
}

/* wchar version of __find_arguments. */
int
__find_warguments (const wchar_t *fmt0, va_list ap, union arg **argtable)
{
	wchar_t *fmt;		/* format string */
	wchar_t ch;		/* character from fmt */
	u_int n;		/* handy integer (short term usage) */
	int error;
	int flags;		/* flags as above */
	struct typetable types;	/* table of types */

	fmt = (wchar_t *)fmt0;
	inittypes(&types);
	error = 0;

	/*
	 * Scan the format for conversions (`%' character).
	 */
	for (;;) {
		while ((ch = *fmt) != '\0' && ch != '%')
			fmt++;
		if (ch == '\0')
			goto done;
		fmt++;		/* skip over '%' */

		flags = 0;

rflag:		ch = *fmt++;
reswitch:	switch (ch) {
		case ' ':
		case '#':
			goto rflag;
		case '*':
			if ((error = addwaster(&types, &fmt)))
				goto error;
			goto rflag;
		case '-':
		case '+':
		case '\'':
			goto rflag;
		case '.':
			if ((ch = *fmt++) == '*') {
				if ((error = addwaster(&types, &fmt)))
					goto error;
				goto rflag;
			}
			while (is_digit(ch)) {
				ch = *fmt++;
			}
			goto reswitch;
		case '0':
			goto rflag;
		case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			n = 0;
			do {
				n = 10 * n + to_digit(ch);
				/* Detect overflow */
				if (n > MAX_POSARG) {
					error = -1;
					goto error;
				}
				ch = *fmt++;
			} while (is_digit(ch));
			if (ch == '$') {
				types.nextarg = n;
				goto rflag;
			}
			goto reswitch;
#ifndef NO_FLOATING_POINT
		case 'L':
			flags |= LONGDBL;
			goto rflag;
#endif
		case 'h':
			if (flags & SHORTINT) {
				flags &= ~SHORTINT;
				flags |= CHARINT;
			} else
				flags |= SHORTINT;
			goto rflag;
		case 'j':
			flags |= INTMAXT;
			goto rflag;
		case 'l':
			if (flags & LONGINT) {
				flags &= ~LONGINT;
				flags |= LLONGINT;
			} else
				flags |= LONGINT;
			goto rflag;
		case 'q':
			flags |= LLONGINT;	/* not necessarily */
			goto rflag;
		case 't':
			flags |= PTRDIFFT;
			goto rflag;
		case 'z':
			flags |= SIZET;
			goto rflag;
		case 'C':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'c':
			error = addtype(&types,
					(flags & LONGINT) ? T_WINT : T_INT);
			if (error)
				goto error;
			break;
		case 'D':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'd':
		case 'i':
			if ((error = addsarg(&types, flags)))
				goto error;
			break;
#ifndef NO_FLOATING_POINT
		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'f':
		case 'g':
		case 'G':
			error = addtype(&types,
			    (flags & LONGDBL) ? T_LONG_DOUBLE : T_DOUBLE);
			if (error)
				goto error;
			break;
#endif /* !NO_FLOATING_POINT */
		case 'n':
			if (flags & INTMAXT)
				error = addtype(&types, TP_INTMAXT);
			else if (flags & PTRDIFFT)
				error = addtype(&types, TP_PTRDIFFT);
			else if (flags & SIZET)
				error = addtype(&types, TP_SSIZET);
			else if (flags & LLONGINT)
				error = addtype(&types, TP_LLONG);
			else if (flags & LONGINT)
				error = addtype(&types, TP_LONG);
			else if (flags & SHORTINT)
				error = addtype(&types, TP_SHORT);
			else if (flags & CHARINT)
				error = addtype(&types, TP_SCHAR);
			else
				error = addtype(&types, TP_INT);
			if (error)
				goto error;
			continue;	/* no output */
		case 'O':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'o':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		case 'p':
			if ((error = addtype(&types, TP_VOID)))
				goto error;
			break;
		case 'S':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 's':
			error = addtype(&types,
			    (flags & LONGINT) ? TP_WCHAR : TP_CHAR);
			if (error)
				goto error;
			break;
		case 'U':
			flags |= LONGINT;
			/*FALLTHROUGH*/
		case 'u':
		case 'X':
		case 'x':
			if ((error = adduarg(&types, flags)))
				goto error;
			break;
		default:	/* "%?" prints ?, unless ? is NUL */
			if (ch == '\0')
				goto done;
			break;
		}
	}
done:
	build_arg_table(&types, ap, argtable);
error:
	freetypes(&types);
	return (error || *argtable == NULL);
}

/*
 * Increase the size of the type table. Returns 0 on success.
 */
static int
__grow_type_table(struct typetable *types)
{
	enum typeid *const oldtable = types->table;
	const int oldsize = types->tablesize;
	enum typeid *newtable;
	u_int n, newsize;

	/* Detect overflow */
	if (types->nextarg > NL_ARGMAX)
		return (-1);

	newsize = oldsize * 2;
	if (newsize < types->nextarg + 1)
		newsize = types->nextarg + 1;
	if (oldsize == STATIC_ARG_TBL_SIZE) {
		if ((newtable = malloc(newsize * sizeof(enum typeid))) == NULL)
			return (-1);
		bcopy(oldtable, newtable, oldsize * sizeof(enum typeid));
	} else {
		newtable = reallocarray(oldtable, newsize, sizeof(enum typeid));
		if (newtable == NULL)
			return (-1);
	}
	for (n = oldsize; n < newsize; n++)
		newtable[n] = T_UNUSED;

	types->table = newtable;
	types->tablesize = newsize;

	return (0);
}

/*
 * Build the argument table from the completed type table.
 * On malloc failure, *argtable is set to NULL.
 */
static void
build_arg_table(struct typetable *types, va_list ap, union arg **argtable)
{
	u_int n;

	if (types->tablemax >= STATIC_ARG_TBL_SIZE) {
		*argtable = (union arg *)
		    malloc (sizeof (union arg) * (types->tablemax + 1));
		if (*argtable == NULL)
			return;
	}

	(*argtable) [0].intarg = 0;
	for (n = 1; n <= types->tablemax; n++) {
		switch (types->table[n]) {
		    case T_UNUSED: /* whoops! */
			(*argtable) [n].intarg = va_arg (ap, int);
			break;
		    case TP_SCHAR:
			(*argtable) [n].pschararg = va_arg (ap, signed char *);
			break;
		    case TP_SHORT:
			(*argtable) [n].pshortarg = va_arg (ap, short *);
			break;
		    case T_INT:
			(*argtable) [n].intarg = va_arg (ap, int);
			break;
		    case T_U_INT:
			(*argtable) [n].uintarg = va_arg (ap, unsigned int);
			break;
		    case TP_INT:
			(*argtable) [n].pintarg = va_arg (ap, int *);
			break;
		    case T_LONG:
			(*argtable) [n].longarg = va_arg (ap, long);
			break;
		    case T_U_LONG:
			(*argtable) [n].ulongarg = va_arg (ap, unsigned long);
			break;
		    case TP_LONG:
			(*argtable) [n].plongarg = va_arg (ap, long *);
			break;
		    case T_LLONG:
			(*argtable) [n].longlongarg = va_arg (ap, long long);
			break;
		    case T_U_LLONG:
			(*argtable) [n].ulonglongarg = va_arg (ap, unsigned long long);
			break;
		    case TP_LLONG:
			(*argtable) [n].plonglongarg = va_arg (ap, long long *);
			break;
		    case T_PTRDIFFT:
			(*argtable) [n].ptrdiffarg = va_arg (ap, ptrdiff_t);
			break;
		    case TP_PTRDIFFT:
			(*argtable) [n].pptrdiffarg = va_arg (ap, ptrdiff_t *);
			break;
		    case T_SIZET:
			(*argtable) [n].sizearg = va_arg (ap, size_t);
			break;
		    case T_SSIZET:
			(*argtable) [n].sizearg = va_arg (ap, ssize_t);
			break;
		    case TP_SSIZET:
			(*argtable) [n].pssizearg = va_arg (ap, ssize_t *);
			break;
		    case T_INTMAXT:
			(*argtable) [n].intmaxarg = va_arg (ap, intmax_t);
			break;
		    case T_UINTMAXT:
			(*argtable) [n].uintmaxarg = va_arg (ap, uintmax_t);
			break;
		    case TP_INTMAXT:
			(*argtable) [n].pintmaxarg = va_arg (ap, intmax_t *);
			break;
		    case T_DOUBLE:
#ifndef NO_FLOATING_POINT
			(*argtable) [n].doublearg = va_arg (ap, double);
#endif
			break;
		    case T_LONG_DOUBLE:
#ifndef NO_FLOATING_POINT
			(*argtable) [n].longdoublearg = va_arg (ap, long double);
#endif
			break;
		    case TP_CHAR:
			(*argtable) [n].pchararg = va_arg (ap, char *);
			break;
		    case TP_VOID:
			(*argtable) [n].pvoidarg = va_arg (ap, void *);
			break;
		    case T_WINT:
			(*argtable) [n].wintarg = va_arg (ap, wint_t);
			break;
		    case TP_WCHAR:
			(*argtable) [n].pwchararg = va_arg (ap, wchar_t *);
			break;
		}
	}
}
