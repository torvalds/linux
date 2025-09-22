/* ANSI and traditional C compatability macros
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.
   This file is part of the GNU C Library.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* ANSI and traditional C compatibility macros

   ANSI C is assumed if __STDC__ is #defined.

   Macro		ANSI C definition	Traditional C definition
   -----		---- - ----------	----------- - ----------
   ANSI_PROTOTYPES	1			not defined
   PTR			`void *'		`char *'
   PTRCONST		`void *const'		`char *'
   LONG_DOUBLE		`long double'		`double'
   const		not defined		`'
   volatile		not defined		`'
   signed		not defined		`'
   VA_START(ap, var)	va_start(ap, var)	va_start(ap)

   Note that it is safe to write "void foo();" indicating a function
   with no return value, in all K+R compilers we have been able to test.

   For declaring functions with prototypes, we also provide these:

   PARAMS ((prototype))
   -- for functions which take a fixed number of arguments.  Use this
   when declaring the function.  When defining the function, write a
   K+R style argument list.  For example:

	char *strcpy PARAMS ((char *dest, char *source));
	...
	char *
	strcpy (dest, source)
	     char *dest;
	     char *source;
	{ ... }


   VPARAMS ((prototype, ...))
   -- for functions which take a variable number of arguments.  Use
   PARAMS to declare the function, VPARAMS to define it.  For example:

	int printf PARAMS ((const char *format, ...));
	...
	int
	printf VPARAMS ((const char *format, ...))
	{
	   ...
	}

   For writing functions which take variable numbers of arguments, we
   also provide the VA_OPEN, VA_CLOSE, and VA_FIXEDARG macros.  These
   hide the differences between K+R <varargs.h> and C89 <stdarg.h> more
   thoroughly than the simple VA_START() macro mentioned above.

   VA_OPEN and VA_CLOSE are used *instead of* va_start and va_end.
   Immediately after VA_OPEN, put a sequence of VA_FIXEDARG calls
   corresponding to the list of fixed arguments.  Then use va_arg
   normally to get the variable arguments, or pass your va_list object
   around.  You do not declare the va_list yourself; VA_OPEN does it
   for you.

   Here is a complete example:

	int
	printf VPARAMS ((const char *format, ...))
	{
	   int result;

	   VA_OPEN (ap, format);
	   VA_FIXEDARG (ap, const char *, format);

	   result = vfprintf (stdout, format, ap);
	   VA_CLOSE (ap);

	   return result;
	}


   You can declare variables either before or after the VA_OPEN,
   VA_FIXEDARG sequence.  Also, VA_OPEN and VA_CLOSE are the beginning
   and end of a block.  They must appear at the same nesting level,
   and any variables declared after VA_OPEN go out of scope at
   VA_CLOSE.  Unfortunately, with a K+R compiler, that includes the
   argument list.  You can have multiple instances of VA_OPEN/VA_CLOSE
   pairs in a single function in case you need to traverse the
   argument list more than once.

   For ease of writing code which uses GCC extensions but needs to be
   portable to other compilers, we provide the GCC_VERSION macro that
   simplifies testing __GNUC__ and __GNUC_MINOR__ together, and various
   wrappers around __attribute__.  Also, __extension__ will be #defined
   to nothing if it doesn't work.  See below.

   This header also defines a lot of obsolete macros:
   CONST, VOLATILE, SIGNED, PROTO, EXFUN, DEFUN, DEFUN_VOID,
   AND, DOTS, NOARGS.  Don't use them.  */

#ifndef	_ANSIDECL_H
#define _ANSIDECL_H	1

/* Every source file includes this file,
   so they will all get the switch for lint.  */
/* LINTLIBRARY */

/* Using MACRO(x,y) in cpp #if conditionals does not work with some
   older preprocessors.  Thus we can't define something like this:

#define HAVE_GCC_VERSION(MAJOR, MINOR) \
  (__GNUC__ > (MAJOR) || (__GNUC__ == (MAJOR) && __GNUC_MINOR__ >= (MINOR)))

and then test "#if HAVE_GCC_VERSION(2,7)".

So instead we use the macro below and test it against specific values.  */

/* This macro simplifies testing whether we are using gcc, and if it
   is of a particular minimum version. (Both major & minor numbers are
   significant.)  This macro will evaluate to 0 if we are not using
   gcc at all.  */
#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 1000 + __GNUC_MINOR__)
#endif /* GCC_VERSION */

#if defined (__STDC__) || defined (_AIX) || (defined (__mips) && defined (_SYSTYPE_SVR4)) || defined(_WIN32) || (defined(__alpha) && defined(__cplusplus))
/* All known AIX compilers implement these things (but don't always
   define __STDC__).  The RISC/OS MIPS compiler defines these things
   in SVR4 mode, but does not define __STDC__.  */
/* eraxxon@alumni.rice.edu: The Compaq C++ compiler, unlike many other
   C++ compilers, does not define __STDC__, though it acts as if this
   was so. (Verified versions: 5.7, 6.2, 6.3, 6.5) */

#define ANSI_PROTOTYPES	1
#define PTR		void *
#define PTRCONST	void *const
#define LONG_DOUBLE	long double

/* PARAMS is often defined elsewhere (e.g. by libintl.h), so wrap it in
   a #ifndef.  */
#ifndef PARAMS
#define PARAMS(ARGS)		ARGS
#endif

#define VPARAMS(ARGS)		ARGS
#define VA_START(VA_LIST, VAR)	va_start(VA_LIST, VAR)

/* variadic function helper macros */
/* "struct Qdmy" swallows the semicolon after VA_OPEN/VA_FIXEDARG's
   use without inhibiting further decls and without declaring an
   actual variable.  */
#define VA_OPEN(AP, VAR)	{ va_list AP; va_start(AP, VAR); { struct Qdmy
#define VA_CLOSE(AP)		} va_end(AP); }
#define VA_FIXEDARG(AP, T, N)	struct Qdmy
 
#undef const
#undef volatile
#undef signed

/* inline requires special treatment; it's in C99, and GCC >=2.7 supports
   it too, but it's not in C89.  */
#undef inline
#if __STDC_VERSION__ > 199901L
/* it's a keyword */
#else
# if GCC_VERSION >= 2007
#  define inline __inline__   /* __inline__ prevents -pedantic warnings */
# else
#  define inline  /* nothing */
# endif
#endif

/* These are obsolete.  Do not use.  */
#ifndef IN_GCC
#define CONST		const
#define VOLATILE	volatile
#define SIGNED		signed

#define PROTO(type, name, arglist)	type name arglist
#define EXFUN(name, proto)		name proto
#define DEFUN(name, arglist, args)	name(args)
#define DEFUN_VOID(name)		name(void)
#define AND		,
#define DOTS		, ...
#define NOARGS		void
#endif /* ! IN_GCC */

#else	/* Not ANSI C.  */

#undef  ANSI_PROTOTYPES
#define PTR		char *
#define PTRCONST	PTR
#define LONG_DOUBLE	double

#define PARAMS(args)		()
#define VPARAMS(args)		(va_alist) va_dcl
#define VA_START(va_list, var)	va_start(va_list)

#define VA_OPEN(AP, VAR)		{ va_list AP; va_start(AP); { struct Qdmy
#define VA_CLOSE(AP)			} va_end(AP); }
#define VA_FIXEDARG(AP, TYPE, NAME)	TYPE NAME = va_arg(AP, TYPE)

/* some systems define these in header files for non-ansi mode */
#undef const
#undef volatile
#undef signed
#undef inline
#define const
#define volatile
#define signed
#define inline

#ifndef IN_GCC
#define CONST
#define VOLATILE
#define SIGNED

#define PROTO(type, name, arglist)	type name ()
#define EXFUN(name, proto)		name()
#define DEFUN(name, arglist, args)	name arglist args;
#define DEFUN_VOID(name)		name()
#define AND		;
#define DOTS
#define NOARGS
#endif /* ! IN_GCC */

#endif	/* ANSI C.  */

/* Define macros for some gcc attributes.  This permits us to use the
   macros freely, and know that they will come into play for the
   version of gcc in which they are supported.  */

#if (GCC_VERSION < 2007)
# define __attribute__(x)
#endif

/* Attribute __malloc__ on functions was valid as of gcc 2.96. */
#ifndef ATTRIBUTE_MALLOC
# if (GCC_VERSION >= 2096)
#  define ATTRIBUTE_MALLOC __attribute__ ((__malloc__))
# else
#  define ATTRIBUTE_MALLOC
# endif /* GNUC >= 2.96 */
#endif /* ATTRIBUTE_MALLOC */

/* Attributes on labels were valid as of gcc 2.93. */
#ifndef ATTRIBUTE_UNUSED_LABEL
# if (!defined (__cplusplus) && GCC_VERSION >= 2093)
#  define ATTRIBUTE_UNUSED_LABEL ATTRIBUTE_UNUSED
# else
#  define ATTRIBUTE_UNUSED_LABEL
# endif /* !__cplusplus && GNUC >= 2.93 */
#endif /* ATTRIBUTE_UNUSED_LABEL */

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__ ((__unused__))
#endif /* ATTRIBUTE_UNUSED */

/* Before GCC 3.4, the C++ frontend couldn't parse attributes placed after the
   identifier name.  */
#if ! defined(__cplusplus) || (GCC_VERSION >= 3004)
# define ARG_UNUSED(NAME) NAME ATTRIBUTE_UNUSED
#else /* !__cplusplus || GNUC >= 3.4 */
# define ARG_UNUSED(NAME) NAME
#endif /* !__cplusplus || GNUC >= 3.4 */

#ifndef ATTRIBUTE_NORETURN
#define ATTRIBUTE_NORETURN __attribute__ ((__noreturn__))
#endif /* ATTRIBUTE_NORETURN */

/* Attribute `nonnull' was valid as of gcc 3.3.  */
#ifndef ATTRIBUTE_NONNULL
# if (GCC_VERSION >= 3003)
#  define ATTRIBUTE_NONNULL(m) __attribute__ ((__nonnull__ (m)))
# else
#  define ATTRIBUTE_NONNULL(m)
# endif /* GNUC >= 3.3 */
#endif /* ATTRIBUTE_NONNULL */

/* Attribute `pure' was valid as of gcc 3.0.  */
#ifndef ATTRIBUTE_PURE
# if (GCC_VERSION >= 3000)
#  define ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define ATTRIBUTE_PURE
# endif /* GNUC >= 3.0 */
#endif /* ATTRIBUTE_PURE */

/* Use ATTRIBUTE_PRINTF when the format specifier must not be NULL.
   This was the case for the `printf' format attribute by itself
   before GCC 3.3, but as of 3.3 we need to add the `nonnull'
   attribute to retain this behavior.  */
#ifndef ATTRIBUTE_PRINTF
#define ATTRIBUTE_PRINTF(m, n) __attribute__ ((__format__ (__printf__, m, n))) ATTRIBUTE_NONNULL(m)
#define ATTRIBUTE_PRINTF_1 ATTRIBUTE_PRINTF(1, 2)
#define ATTRIBUTE_PRINTF_2 ATTRIBUTE_PRINTF(2, 3)
#define ATTRIBUTE_PRINTF_3 ATTRIBUTE_PRINTF(3, 4)
#define ATTRIBUTE_PRINTF_4 ATTRIBUTE_PRINTF(4, 5)
#define ATTRIBUTE_PRINTF_5 ATTRIBUTE_PRINTF(5, 6)
#endif /* ATTRIBUTE_PRINTF */

/* Use ATTRIBUTE_FPTR_PRINTF when the format attribute is to be set on
   a function pointer.  Format attributes were allowed on function
   pointers as of gcc 3.1.  */
#ifndef ATTRIBUTE_FPTR_PRINTF
# if (GCC_VERSION >= 3001)
#  define ATTRIBUTE_FPTR_PRINTF(m, n) ATTRIBUTE_PRINTF(m, n)
# else
#  define ATTRIBUTE_FPTR_PRINTF(m, n)
# endif /* GNUC >= 3.1 */
# define ATTRIBUTE_FPTR_PRINTF_1 ATTRIBUTE_FPTR_PRINTF(1, 2)
# define ATTRIBUTE_FPTR_PRINTF_2 ATTRIBUTE_FPTR_PRINTF(2, 3)
# define ATTRIBUTE_FPTR_PRINTF_3 ATTRIBUTE_FPTR_PRINTF(3, 4)
# define ATTRIBUTE_FPTR_PRINTF_4 ATTRIBUTE_FPTR_PRINTF(4, 5)
# define ATTRIBUTE_FPTR_PRINTF_5 ATTRIBUTE_FPTR_PRINTF(5, 6)
#endif /* ATTRIBUTE_FPTR_PRINTF */

/* Use ATTRIBUTE_NULL_PRINTF when the format specifier may be NULL.  A
   NULL format specifier was allowed as of gcc 3.3.  */
#ifndef ATTRIBUTE_NULL_PRINTF
# if (GCC_VERSION >= 3003)
#  define ATTRIBUTE_NULL_PRINTF(m, n) __attribute__ ((__format__ (__printf__, m, n)))
# else
#  define ATTRIBUTE_NULL_PRINTF(m, n)
# endif /* GNUC >= 3.3 */
# define ATTRIBUTE_NULL_PRINTF_1 ATTRIBUTE_NULL_PRINTF(1, 2)
# define ATTRIBUTE_NULL_PRINTF_2 ATTRIBUTE_NULL_PRINTF(2, 3)
# define ATTRIBUTE_NULL_PRINTF_3 ATTRIBUTE_NULL_PRINTF(3, 4)
# define ATTRIBUTE_NULL_PRINTF_4 ATTRIBUTE_NULL_PRINTF(4, 5)
# define ATTRIBUTE_NULL_PRINTF_5 ATTRIBUTE_NULL_PRINTF(5, 6)
#endif /* ATTRIBUTE_NULL_PRINTF */

/* Attribute `sentinel' was valid as of gcc 3.5.  */
#ifndef ATTRIBUTE_SENTINEL
# if (GCC_VERSION >= 3005)
#  define ATTRIBUTE_SENTINEL __attribute__ ((__sentinel__))
# else
#  define ATTRIBUTE_SENTINEL
# endif /* GNUC >= 3.5 */
#endif /* ATTRIBUTE_SENTINEL */


#ifndef ATTRIBUTE_ALIGNED_ALIGNOF
# if (GCC_VERSION >= 3000)
#  define ATTRIBUTE_ALIGNED_ALIGNOF(m) __attribute__ ((__aligned__ (__alignof__ (m))))
# else
#  define ATTRIBUTE_ALIGNED_ALIGNOF(m)
# endif /* GNUC >= 3.0 */
#endif /* ATTRIBUTE_ALIGNED_ALIGNOF */

/* We use __extension__ in some places to suppress -pedantic warnings
   about GCC extensions.  This feature didn't work properly before
   gcc 2.8.  */
#if GCC_VERSION < 2008
#define __extension__
#endif

#endif	/* ansidecl.h	*/
