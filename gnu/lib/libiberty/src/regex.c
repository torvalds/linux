/* Extended regular expression matching and search library,
   version 0.12.
   (Implements POSIX draft P1003.2/D11.2, except for some of the
   internationalization features.)

   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001,
   2002, 2005 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301 USA.  */

/* This file has been modified for usage in libiberty.  It includes "xregex.h"
   instead of <regex.h>.  The "xregex.h" header file renames all external
   routines with an "x" prefix so they do not collide with the native regex
   routines or with other components regex routines. */
/* AIX requires this to be the first thing in the file. */
#if defined _AIX && !defined __GNUC__ && !defined REGEX_MALLOC
  #pragma alloca
#endif

#undef	_GNU_SOURCE
#define _GNU_SOURCE

#ifndef INSIDE_RECURSION
# ifdef HAVE_CONFIG_H
#  include <config.h>
# endif
#endif

#include <ansidecl.h>

#ifndef INSIDE_RECURSION

# if defined STDC_HEADERS && !defined emacs
#  include <stddef.h>
# else
/* We need this for `regex.h', and perhaps for the Emacs include files.  */
#  include <sys/types.h>
# endif

# define WIDE_CHAR_SUPPORT (HAVE_WCTYPE_H && HAVE_WCHAR_H && HAVE_BTOWC)

/* For platform which support the ISO C amendement 1 functionality we
   support user defined character classes.  */
# if defined _LIBC || WIDE_CHAR_SUPPORT
/* Solaris 2.5 has a bug: <wchar.h> must be included before <wctype.h>.  */
#  include <wchar.h>
#  include <wctype.h>
# endif

# ifdef _LIBC
/* We have to keep the namespace clean.  */
#  define regfree(preg) __regfree (preg)
#  define regexec(pr, st, nm, pm, ef) __regexec (pr, st, nm, pm, ef)
#  define regcomp(preg, pattern, cflags) __regcomp (preg, pattern, cflags)
#  define regerror(errcode, preg, errbuf, errbuf_size) \
	__regerror(errcode, preg, errbuf, errbuf_size)
#  define re_set_registers(bu, re, nu, st, en) \
	__re_set_registers (bu, re, nu, st, en)
#  define re_match_2(bufp, string1, size1, string2, size2, pos, regs, stop) \
	__re_match_2 (bufp, string1, size1, string2, size2, pos, regs, stop)
#  define re_match(bufp, string, size, pos, regs) \
	__re_match (bufp, string, size, pos, regs)
#  define re_search(bufp, string, size, startpos, range, regs) \
	__re_search (bufp, string, size, startpos, range, regs)
#  define re_compile_pattern(pattern, length, bufp) \
	__re_compile_pattern (pattern, length, bufp)
#  define re_set_syntax(syntax) __re_set_syntax (syntax)
#  define re_search_2(bufp, st1, s1, st2, s2, startpos, range, regs, stop) \
	__re_search_2 (bufp, st1, s1, st2, s2, startpos, range, regs, stop)
#  define re_compile_fastmap(bufp) __re_compile_fastmap (bufp)

#  define btowc __btowc

/* We are also using some library internals.  */
#  include <locale/localeinfo.h>
#  include <locale/elem-hash.h>
#  include <langinfo.h>
#  include <locale/coll-lookup.h>
# endif

/* This is for other GNU distributions with internationalized messages.  */
# if (HAVE_LIBINTL_H && ENABLE_NLS) || defined _LIBC
#  include <libintl.h>
#  ifdef _LIBC
#   undef gettext
#   define gettext(msgid) __dcgettext ("libc", msgid, LC_MESSAGES)
#  endif
# else
#  define gettext(msgid) (msgid)
# endif

# ifndef gettext_noop
/* This define is so xgettext can find the internationalizable
   strings.  */
#  define gettext_noop(String) String
# endif

/* The `emacs' switch turns on certain matching commands
   that make sense only in Emacs. */
# ifdef emacs

#  include "lisp.h"
#  include "buffer.h"
#  include "syntax.h"

# else  /* not emacs */

/* If we are not linking with Emacs proper,
   we can't use the relocating allocator
   even if config.h says that we can.  */
#  undef REL_ALLOC

#  if defined STDC_HEADERS || defined _LIBC
#   include <stdlib.h>
#  else
char *malloc ();
char *realloc ();
#  endif

/* When used in Emacs's lib-src, we need to get bzero and bcopy somehow.
   If nothing else has been done, use the method below.  */
#  ifdef INHIBIT_STRING_HEADER
#   if !(defined HAVE_BZERO && defined HAVE_BCOPY)
#    if !defined bzero && !defined bcopy
#     undef INHIBIT_STRING_HEADER
#    endif
#   endif
#  endif

/* This is the normal way of making sure we have a bcopy and a bzero.
   This is used in most programs--a few other programs avoid this
   by defining INHIBIT_STRING_HEADER.  */
#  ifndef INHIBIT_STRING_HEADER
#   if defined HAVE_STRING_H || defined STDC_HEADERS || defined _LIBC
#    include <string.h>
#    ifndef bzero
#     ifndef _LIBC
#      define bzero(s, n)	(memset (s, '\0', n), (s))
#     else
#      define bzero(s, n)	__bzero (s, n)
#     endif
#    endif
#   else
#    include <strings.h>
#    ifndef memcmp
#     define memcmp(s1, s2, n)	bcmp (s1, s2, n)
#    endif
#    ifndef memcpy
#     define memcpy(d, s, n)	(bcopy (s, d, n), (d))
#    endif
#   endif
#  endif

/* Define the syntax stuff for \<, \>, etc.  */

/* This must be nonzero for the wordchar and notwordchar pattern
   commands in re_match_2.  */
#  ifndef Sword
#   define Sword 1
#  endif

#  ifdef SWITCH_ENUM_BUG
#   define SWITCH_ENUM_CAST(x) ((int)(x))
#  else
#   define SWITCH_ENUM_CAST(x) (x)
#  endif

# endif /* not emacs */

# if defined _LIBC || HAVE_LIMITS_H
#  include <limits.h>
# endif

# ifndef MB_LEN_MAX
#  define MB_LEN_MAX 1
# endif

/* Get the interface, including the syntax bits.  */
# include "xregex.h"  /* change for libiberty */

/* isalpha etc. are used for the character classes.  */
# include <ctype.h>

/* Jim Meyering writes:

   "... Some ctype macros are valid only for character codes that
   isascii says are ASCII (SGI's IRIX-4.0.5 is one such system --when
   using /bin/cc or gcc but without giving an ansi option).  So, all
   ctype uses should be through macros like ISPRINT...  If
   STDC_HEADERS is defined, then autoconf has verified that the ctype
   macros don't need to be guarded with references to isascii. ...
   Defining isascii to 1 should let any compiler worth its salt
   eliminate the && through constant folding."
   Solaris defines some of these symbols so we must undefine them first.  */

# undef ISASCII
# if defined STDC_HEADERS || (!defined isascii && !defined HAVE_ISASCII)
#  define ISASCII(c) 1
# else
#  define ISASCII(c) isascii(c)
# endif

# ifdef isblank
#  define ISBLANK(c) (ISASCII (c) && isblank (c))
# else
#  define ISBLANK(c) ((c) == ' ' || (c) == '\t')
# endif
# ifdef isgraph
#  define ISGRAPH(c) (ISASCII (c) && isgraph (c))
# else
#  define ISGRAPH(c) (ISASCII (c) && isprint (c) && !isspace (c))
# endif

# undef ISPRINT
# define ISPRINT(c) (ISASCII (c) && isprint (c))
# define ISDIGIT(c) (ISASCII (c) && isdigit (c))
# define ISALNUM(c) (ISASCII (c) && isalnum (c))
# define ISALPHA(c) (ISASCII (c) && isalpha (c))
# define ISCNTRL(c) (ISASCII (c) && iscntrl (c))
# define ISLOWER(c) (ISASCII (c) && islower (c))
# define ISPUNCT(c) (ISASCII (c) && ispunct (c))
# define ISSPACE(c) (ISASCII (c) && isspace (c))
# define ISUPPER(c) (ISASCII (c) && isupper (c))
# define ISXDIGIT(c) (ISASCII (c) && isxdigit (c))

# ifdef _tolower
#  define TOLOWER(c) _tolower(c)
# else
#  define TOLOWER(c) tolower(c)
# endif

# ifndef NULL
#  define NULL (void *)0
# endif

/* We remove any previous definition of `SIGN_EXTEND_CHAR',
   since ours (we hope) works properly with all combinations of
   machines, compilers, `char' and `unsigned char' argument types.
   (Per Bothner suggested the basic approach.)  */
# undef SIGN_EXTEND_CHAR
# if __STDC__
#  define SIGN_EXTEND_CHAR(c) ((signed char) (c))
# else  /* not __STDC__ */
/* As in Harbison and Steele.  */
#  define SIGN_EXTEND_CHAR(c) ((((unsigned char) (c)) ^ 128) - 128)
# endif

# ifndef emacs
/* How many characters in the character set.  */
#  define CHAR_SET_SIZE 256

#  ifdef SYNTAX_TABLE

extern char *re_syntax_table;

#  else /* not SYNTAX_TABLE */

static char re_syntax_table[CHAR_SET_SIZE];

static void init_syntax_once (void);

static void
init_syntax_once (void)
{
   register int c;
   static int done = 0;

   if (done)
     return;
   bzero (re_syntax_table, sizeof re_syntax_table);

   for (c = 0; c < CHAR_SET_SIZE; ++c)
     if (ISALNUM (c))
	re_syntax_table[c] = Sword;

   re_syntax_table['_'] = Sword;

   done = 1;
}

#  endif /* not SYNTAX_TABLE */

#  define SYNTAX(c) re_syntax_table[(unsigned char) (c)]

# endif /* emacs */

/* Integer type for pointers.  */
# if !defined _LIBC && !defined HAVE_UINTPTR_T
typedef unsigned long int uintptr_t;
# endif

/* Should we use malloc or alloca?  If REGEX_MALLOC is not defined, we
   use `alloca' instead of `malloc'.  This is because using malloc in
   re_search* or re_match* could cause memory leaks when C-g is used in
   Emacs; also, malloc is slower and causes storage fragmentation.  On
   the other hand, malloc is more portable, and easier to debug.

   Because we sometimes use alloca, some routines have to be macros,
   not functions -- `alloca'-allocated space disappears at the end of the
   function it is called in.  */

# ifdef REGEX_MALLOC

#  define REGEX_ALLOCATE malloc
#  define REGEX_REALLOCATE(source, osize, nsize) realloc (source, nsize)
#  define REGEX_FREE free

# else /* not REGEX_MALLOC  */

/* Emacs already defines alloca, sometimes.  */
#  ifndef alloca

/* Make alloca work the best possible way.  */
#   ifdef __GNUC__
#    define alloca __builtin_alloca
#   else /* not __GNUC__ */
#    if HAVE_ALLOCA_H
#     include <alloca.h>
#    endif /* HAVE_ALLOCA_H */
#   endif /* not __GNUC__ */

#  endif /* not alloca */

#  define REGEX_ALLOCATE alloca

/* Assumes a `char *destination' variable.  */
#  define REGEX_REALLOCATE(source, osize, nsize)			\
  (destination = (char *) alloca (nsize),				\
   memcpy (destination, source, osize))

/* No need to do anything to free, after alloca.  */
#  define REGEX_FREE(arg) ((void)0) /* Do nothing!  But inhibit gcc warning.  */

# endif /* not REGEX_MALLOC */

/* Define how to allocate the failure stack.  */

# if defined REL_ALLOC && defined REGEX_MALLOC

#  define REGEX_ALLOCATE_STACK(size)				\
  r_alloc (&failure_stack_ptr, (size))
#  define REGEX_REALLOCATE_STACK(source, osize, nsize)		\
  r_re_alloc (&failure_stack_ptr, (nsize))
#  define REGEX_FREE_STACK(ptr)					\
  r_alloc_free (&failure_stack_ptr)

# else /* not using relocating allocator */

#  ifdef REGEX_MALLOC

#   define REGEX_ALLOCATE_STACK malloc
#   define REGEX_REALLOCATE_STACK(source, osize, nsize) realloc (source, nsize)
#   define REGEX_FREE_STACK free

#  else /* not REGEX_MALLOC */

#   define REGEX_ALLOCATE_STACK alloca

#   define REGEX_REALLOCATE_STACK(source, osize, nsize)			\
   REGEX_REALLOCATE (source, osize, nsize)
/* No need to explicitly free anything.  */
#   define REGEX_FREE_STACK(arg)

#  endif /* not REGEX_MALLOC */
# endif /* not using relocating allocator */


/* True if `size1' is non-NULL and PTR is pointing anywhere inside
   `string1' or just past its end.  This works if PTR is NULL, which is
   a good thing.  */
# define FIRST_STRING_P(ptr) 					\
  (size1 && string1 <= (ptr) && (ptr) <= string1 + size1)

/* (Re)Allocate N items of type T using malloc, or fail.  */
# define TALLOC(n, t) ((t *) malloc ((n) * sizeof (t)))
# define RETALLOC(addr, n, t) ((addr) = (t *) realloc (addr, (n) * sizeof (t)))
# define RETALLOC_IF(addr, n, t) \
  if (addr) RETALLOC((addr), (n), t); else (addr) = TALLOC ((n), t)
# define REGEX_TALLOC(n, t) ((t *) REGEX_ALLOCATE ((n) * sizeof (t)))

# define BYTEWIDTH 8 /* In bits.  */

# define STREQ(s1, s2) ((strcmp (s1, s2) == 0))

# undef MAX
# undef MIN
# define MAX(a, b) ((a) > (b) ? (a) : (b))
# define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef char boolean;
# define false 0
# define true 1

static reg_errcode_t byte_regex_compile (const char *pattern, size_t size,
                                         reg_syntax_t syntax,
                                         struct re_pattern_buffer *bufp);

static int byte_re_match_2_internal (struct re_pattern_buffer *bufp,
                                     const char *string1, int size1,
                                     const char *string2, int size2,
                                     int pos,
                                     struct re_registers *regs,
                                     int stop);
static int byte_re_search_2 (struct re_pattern_buffer *bufp,
                             const char *string1, int size1,
                             const char *string2, int size2,
                             int startpos, int range,
                             struct re_registers *regs, int stop);
static int byte_re_compile_fastmap (struct re_pattern_buffer *bufp);

#ifdef MBS_SUPPORT
static reg_errcode_t wcs_regex_compile (const char *pattern, size_t size,
                                        reg_syntax_t syntax,
                                        struct re_pattern_buffer *bufp);


static int wcs_re_match_2_internal (struct re_pattern_buffer *bufp,
                                    const char *cstring1, int csize1,
                                    const char *cstring2, int csize2,
                                    int pos,
                                    struct re_registers *regs,
                                    int stop,
                                    wchar_t *string1, int size1,
                                    wchar_t *string2, int size2,
                                    int *mbs_offset1, int *mbs_offset2);
static int wcs_re_search_2 (struct re_pattern_buffer *bufp,
                            const char *string1, int size1,
                            const char *string2, int size2,
                            int startpos, int range,
                            struct re_registers *regs, int stop);
static int wcs_re_compile_fastmap (struct re_pattern_buffer *bufp);
#endif

/* These are the command codes that appear in compiled regular
   expressions.  Some opcodes are followed by argument bytes.  A
   command code can specify any interpretation whatsoever for its
   arguments.  Zero bytes may appear in the compiled regular expression.  */

typedef enum
{
  no_op = 0,

  /* Succeed right away--no more backtracking.  */
  succeed,

        /* Followed by one byte giving n, then by n literal bytes.  */
  exactn,

# ifdef MBS_SUPPORT
	/* Same as exactn, but contains binary data.  */
  exactn_bin,
# endif

        /* Matches any (more or less) character.  */
  anychar,

        /* Matches any one char belonging to specified set.  First
           following byte is number of bitmap bytes.  Then come bytes
           for a bitmap saying which chars are in.  Bits in each byte
           are ordered low-bit-first.  A character is in the set if its
           bit is 1.  A character too large to have a bit in the map is
           automatically not in the set.  */
        /* ifdef MBS_SUPPORT, following element is length of character
	   classes, length of collating symbols, length of equivalence
	   classes, length of character ranges, and length of characters.
	   Next, character class element, collating symbols elements,
	   equivalence class elements, range elements, and character
	   elements follow.
	   See regex_compile function.  */
  charset,

        /* Same parameters as charset, but match any character that is
           not one of those specified.  */
  charset_not,

        /* Start remembering the text that is matched, for storing in a
           register.  Followed by one byte with the register number, in
           the range 0 to one less than the pattern buffer's re_nsub
           field.  Then followed by one byte with the number of groups
           inner to this one.  (This last has to be part of the
           start_memory only because we need it in the on_failure_jump
           of re_match_2.)  */
  start_memory,

        /* Stop remembering the text that is matched and store it in a
           memory register.  Followed by one byte with the register
           number, in the range 0 to one less than `re_nsub' in the
           pattern buffer, and one byte with the number of inner groups,
           just like `start_memory'.  (We need the number of inner
           groups here because we don't have any easy way of finding the
           corresponding start_memory when we're at a stop_memory.)  */
  stop_memory,

        /* Match a duplicate of something remembered. Followed by one
           byte containing the register number.  */
  duplicate,

        /* Fail unless at beginning of line.  */
  begline,

        /* Fail unless at end of line.  */
  endline,

        /* Succeeds if at beginning of buffer (if emacs) or at beginning
           of string to be matched (if not).  */
  begbuf,

        /* Analogously, for end of buffer/string.  */
  endbuf,

        /* Followed by two byte relative address to which to jump.  */
  jump,

	/* Same as jump, but marks the end of an alternative.  */
  jump_past_alt,

        /* Followed by two-byte relative address of place to resume at
           in case of failure.  */
        /* ifdef MBS_SUPPORT, the size of address is 1.  */
  on_failure_jump,

        /* Like on_failure_jump, but pushes a placeholder instead of the
           current string position when executed.  */
  on_failure_keep_string_jump,

        /* Throw away latest failure point and then jump to following
           two-byte relative address.  */
        /* ifdef MBS_SUPPORT, the size of address is 1.  */
  pop_failure_jump,

        /* Change to pop_failure_jump if know won't have to backtrack to
           match; otherwise change to jump.  This is used to jump
           back to the beginning of a repeat.  If what follows this jump
           clearly won't match what the repeat does, such that we can be
           sure that there is no use backtracking out of repetitions
           already matched, then we change it to a pop_failure_jump.
           Followed by two-byte address.  */
        /* ifdef MBS_SUPPORT, the size of address is 1.  */
  maybe_pop_jump,

        /* Jump to following two-byte address, and push a dummy failure
           point. This failure point will be thrown away if an attempt
           is made to use it for a failure.  A `+' construct makes this
           before the first repeat.  Also used as an intermediary kind
           of jump when compiling an alternative.  */
        /* ifdef MBS_SUPPORT, the size of address is 1.  */
  dummy_failure_jump,

	/* Push a dummy failure point and continue.  Used at the end of
	   alternatives.  */
  push_dummy_failure,

        /* Followed by two-byte relative address and two-byte number n.
           After matching N times, jump to the address upon failure.  */
        /* ifdef MBS_SUPPORT, the size of address is 1.  */
  succeed_n,

        /* Followed by two-byte relative address, and two-byte number n.
           Jump to the address N times, then fail.  */
        /* ifdef MBS_SUPPORT, the size of address is 1.  */
  jump_n,

        /* Set the following two-byte relative address to the
           subsequent two-byte number.  The address *includes* the two
           bytes of number.  */
        /* ifdef MBS_SUPPORT, the size of address is 1.  */
  set_number_at,

  wordchar,	/* Matches any word-constituent character.  */
  notwordchar,	/* Matches any char that is not a word-constituent.  */

  wordbeg,	/* Succeeds if at word beginning.  */
  wordend,	/* Succeeds if at word end.  */

  wordbound,	/* Succeeds if at a word boundary.  */
  notwordbound	/* Succeeds if not at a word boundary.  */

# ifdef emacs
  ,before_dot,	/* Succeeds if before point.  */
  at_dot,	/* Succeeds if at point.  */
  after_dot,	/* Succeeds if after point.  */

	/* Matches any character whose syntax is specified.  Followed by
           a byte which contains a syntax code, e.g., Sword.  */
  syntaxspec,

	/* Matches any character whose syntax is not that specified.  */
  notsyntaxspec
# endif /* emacs */
} re_opcode_t;
#endif /* not INSIDE_RECURSION */


#ifdef BYTE
# define CHAR_T char
# define UCHAR_T unsigned char
# define COMPILED_BUFFER_VAR bufp->buffer
# define OFFSET_ADDRESS_SIZE 2
# define PREFIX(name) byte_##name
# define ARG_PREFIX(name) name
# define PUT_CHAR(c) putchar (c)
#else
# ifdef WCHAR
#  define CHAR_T wchar_t
#  define UCHAR_T wchar_t
#  define COMPILED_BUFFER_VAR wc_buffer
#  define OFFSET_ADDRESS_SIZE 1 /* the size which STORE_NUMBER macro use */
#  define CHAR_CLASS_SIZE ((__alignof__(wctype_t)+sizeof(wctype_t))/sizeof(CHAR_T)+1)
#  define PREFIX(name) wcs_##name
#  define ARG_PREFIX(name) c##name
/* Should we use wide stream??  */
#  define PUT_CHAR(c) printf ("%C", c);
#  define TRUE 1
#  define FALSE 0
# else
#  ifdef MBS_SUPPORT
#   define WCHAR
#   define INSIDE_RECURSION
#   include "regex.c"
#   undef INSIDE_RECURSION
#  endif
#  define BYTE
#  define INSIDE_RECURSION
#  include "regex.c"
#  undef INSIDE_RECURSION
# endif
#endif

#ifdef INSIDE_RECURSION
/* Common operations on the compiled pattern.  */

/* Store NUMBER in two contiguous bytes starting at DESTINATION.  */
/* ifdef MBS_SUPPORT, we store NUMBER in 1 element.  */

# ifdef WCHAR
#  define STORE_NUMBER(destination, number)				\
  do {									\
    *(destination) = (UCHAR_T)(number);				\
  } while (0)
# else /* BYTE */
#  define STORE_NUMBER(destination, number)				\
  do {									\
    (destination)[0] = (number) & 0377;					\
    (destination)[1] = (number) >> 8;					\
  } while (0)
# endif /* WCHAR */

/* Same as STORE_NUMBER, except increment DESTINATION to
   the byte after where the number is stored.  Therefore, DESTINATION
   must be an lvalue.  */
/* ifdef MBS_SUPPORT, we store NUMBER in 1 element.  */

# define STORE_NUMBER_AND_INCR(destination, number)			\
  do {									\
    STORE_NUMBER (destination, number);					\
    (destination) += OFFSET_ADDRESS_SIZE;				\
  } while (0)

/* Put into DESTINATION a number stored in two contiguous bytes starting
   at SOURCE.  */
/* ifdef MBS_SUPPORT, we store NUMBER in 1 element.  */

# ifdef WCHAR
#  define EXTRACT_NUMBER(destination, source)				\
  do {									\
    (destination) = *(source);						\
  } while (0)
# else /* BYTE */
#  define EXTRACT_NUMBER(destination, source)				\
  do {									\
    (destination) = *(source) & 0377;					\
    (destination) += SIGN_EXTEND_CHAR (*((source) + 1)) << 8;		\
  } while (0)
# endif

# ifdef DEBUG
static void PREFIX(extract_number) (int *dest, UCHAR_T *source);
static void
PREFIX(extract_number) (int *dest, UCHAR_T *source)
{
#  ifdef WCHAR
  *dest = *source;
#  else /* BYTE */
  int temp = SIGN_EXTEND_CHAR (*(source + 1));
  *dest = *source & 0377;
  *dest += temp << 8;
#  endif
}

#  ifndef EXTRACT_MACROS /* To debug the macros.  */
#   undef EXTRACT_NUMBER
#   define EXTRACT_NUMBER(dest, src) PREFIX(extract_number) (&dest, src)
#  endif /* not EXTRACT_MACROS */

# endif /* DEBUG */

/* Same as EXTRACT_NUMBER, except increment SOURCE to after the number.
   SOURCE must be an lvalue.  */

# define EXTRACT_NUMBER_AND_INCR(destination, source)			\
  do {									\
    EXTRACT_NUMBER (destination, source);				\
    (source) += OFFSET_ADDRESS_SIZE; 					\
  } while (0)

# ifdef DEBUG
static void PREFIX(extract_number_and_incr) (int *destination,
                                             UCHAR_T **source);
static void
PREFIX(extract_number_and_incr) (int *destination, UCHAR_T **source)
{
  PREFIX(extract_number) (destination, *source);
  *source += OFFSET_ADDRESS_SIZE;
}

#  ifndef EXTRACT_MACROS
#   undef EXTRACT_NUMBER_AND_INCR
#   define EXTRACT_NUMBER_AND_INCR(dest, src) \
  PREFIX(extract_number_and_incr) (&dest, &src)
#  endif /* not EXTRACT_MACROS */

# endif /* DEBUG */



/* If DEBUG is defined, Regex prints many voluminous messages about what
   it is doing (if the variable `debug' is nonzero).  If linked with the
   main program in `iregex.c', you can enter patterns and strings
   interactively.  And if linked with the main program in `main.c' and
   the other test files, you can run the already-written tests.  */

# ifdef DEBUG

#  ifndef DEFINED_ONCE

/* We use standard I/O for debugging.  */
#   include <stdio.h>

/* It is useful to test things that ``must'' be true when debugging.  */
#   include <assert.h>

static int debug;

#   define DEBUG_STATEMENT(e) e
#   define DEBUG_PRINT1(x) if (debug) printf (x)
#   define DEBUG_PRINT2(x1, x2) if (debug) printf (x1, x2)
#   define DEBUG_PRINT3(x1, x2, x3) if (debug) printf (x1, x2, x3)
#   define DEBUG_PRINT4(x1, x2, x3, x4) if (debug) printf (x1, x2, x3, x4)
#  endif /* not DEFINED_ONCE */

#  define DEBUG_PRINT_COMPILED_PATTERN(p, s, e) 			\
  if (debug) PREFIX(print_partial_compiled_pattern) (s, e)
#  define DEBUG_PRINT_DOUBLE_STRING(w, s1, sz1, s2, sz2)		\
  if (debug) PREFIX(print_double_string) (w, s1, sz1, s2, sz2)


/* Print the fastmap in human-readable form.  */

#  ifndef DEFINED_ONCE
void
print_fastmap (char *fastmap)
{
  unsigned was_a_range = 0;
  unsigned i = 0;

  while (i < (1 << BYTEWIDTH))
    {
      if (fastmap[i++])
	{
	  was_a_range = 0;
          putchar (i - 1);
          while (i < (1 << BYTEWIDTH)  &&  fastmap[i])
            {
              was_a_range = 1;
              i++;
            }
	  if (was_a_range)
            {
              printf ("-");
              putchar (i - 1);
            }
        }
    }
  putchar ('\n');
}
#  endif /* not DEFINED_ONCE */


/* Print a compiled pattern string in human-readable form, starting at
   the START pointer into it and ending just before the pointer END.  */

void
PREFIX(print_partial_compiled_pattern) (UCHAR_T *start, UCHAR_T *end)
{
  int mcnt, mcnt2;
  UCHAR_T *p1;
  UCHAR_T *p = start;
  UCHAR_T *pend = end;

  if (start == NULL)
    {
      printf ("(null)\n");
      return;
    }

  /* Loop over pattern commands.  */
  while (p < pend)
    {
#  ifdef _LIBC
      printf ("%td:\t", p - start);
#  else
      printf ("%ld:\t", (long int) (p - start));
#  endif

      switch ((re_opcode_t) *p++)
	{
        case no_op:
          printf ("/no_op");
          break;

	case exactn:
	  mcnt = *p++;
          printf ("/exactn/%d", mcnt);
          do
	    {
              putchar ('/');
	      PUT_CHAR (*p++);
            }
          while (--mcnt);
          break;

#  ifdef MBS_SUPPORT
	case exactn_bin:
	  mcnt = *p++;
	  printf ("/exactn_bin/%d", mcnt);
          do
	    {
	      printf("/%lx", (long int) *p++);
            }
          while (--mcnt);
          break;
#  endif /* MBS_SUPPORT */

	case start_memory:
          mcnt = *p++;
          printf ("/start_memory/%d/%ld", mcnt, (long int) *p++);
          break;

	case stop_memory:
          mcnt = *p++;
	  printf ("/stop_memory/%d/%ld", mcnt, (long int) *p++);
          break;

	case duplicate:
	  printf ("/duplicate/%ld", (long int) *p++);
	  break;

	case anychar:
	  printf ("/anychar");
	  break;

	case charset:
        case charset_not:
          {
#  ifdef WCHAR
	    int i, length;
	    wchar_t *workp = p;
	    printf ("/charset [%s",
	            (re_opcode_t) *(workp - 1) == charset_not ? "^" : "");
	    p += 5;
	    length = *workp++; /* the length of char_classes */
	    for (i=0 ; i<length ; i++)
	      printf("[:%lx:]", (long int) *p++);
	    length = *workp++; /* the length of collating_symbol */
	    for (i=0 ; i<length ;)
	      {
		printf("[.");
		while(*p != 0)
		  PUT_CHAR((i++,*p++));
		i++,p++;
		printf(".]");
	      }
	    length = *workp++; /* the length of equivalence_class */
	    for (i=0 ; i<length ;)
	      {
		printf("[=");
		while(*p != 0)
		  PUT_CHAR((i++,*p++));
		i++,p++;
		printf("=]");
	      }
	    length = *workp++; /* the length of char_range */
	    for (i=0 ; i<length ; i++)
	      {
		wchar_t range_start = *p++;
		wchar_t range_end = *p++;
		printf("%C-%C", range_start, range_end);
	      }
	    length = *workp++; /* the length of char */
	    for (i=0 ; i<length ; i++)
	      printf("%C", *p++);
	    putchar (']');
#  else
            register int c, last = -100;
	    register int in_range = 0;

	    printf ("/charset [%s",
	            (re_opcode_t) *(p - 1) == charset_not ? "^" : "");

            assert (p + *p < pend);

            for (c = 0; c < 256; c++)
	      if (c / 8 < *p
		  && (p[1 + (c/8)] & (1 << (c % 8))))
		{
		  /* Are we starting a range?  */
		  if (last + 1 == c && ! in_range)
		    {
		      putchar ('-');
		      in_range = 1;
		    }
		  /* Have we broken a range?  */
		  else if (last + 1 != c && in_range)
              {
		      putchar (last);
		      in_range = 0;
		    }

		  if (! in_range)
		    putchar (c);

		  last = c;
              }

	    if (in_range)
	      putchar (last);

	    putchar (']');

	    p += 1 + *p;
#  endif /* WCHAR */
	  }
	  break;

	case begline:
	  printf ("/begline");
          break;

	case endline:
          printf ("/endline");
          break;

	case on_failure_jump:
          PREFIX(extract_number_and_incr) (&mcnt, &p);
#  ifdef _LIBC
  	  printf ("/on_failure_jump to %td", p + mcnt - start);
#  else
  	  printf ("/on_failure_jump to %ld", (long int) (p + mcnt - start));
#  endif
          break;

	case on_failure_keep_string_jump:
          PREFIX(extract_number_and_incr) (&mcnt, &p);
#  ifdef _LIBC
  	  printf ("/on_failure_keep_string_jump to %td", p + mcnt - start);
#  else
  	  printf ("/on_failure_keep_string_jump to %ld",
		  (long int) (p + mcnt - start));
#  endif
          break;

	case dummy_failure_jump:
          PREFIX(extract_number_and_incr) (&mcnt, &p);
#  ifdef _LIBC
  	  printf ("/dummy_failure_jump to %td", p + mcnt - start);
#  else
  	  printf ("/dummy_failure_jump to %ld", (long int) (p + mcnt - start));
#  endif
          break;

	case push_dummy_failure:
          printf ("/push_dummy_failure");
          break;

        case maybe_pop_jump:
          PREFIX(extract_number_and_incr) (&mcnt, &p);
#  ifdef _LIBC
  	  printf ("/maybe_pop_jump to %td", p + mcnt - start);
#  else
  	  printf ("/maybe_pop_jump to %ld", (long int) (p + mcnt - start));
#  endif
	  break;

        case pop_failure_jump:
	  PREFIX(extract_number_and_incr) (&mcnt, &p);
#  ifdef _LIBC
  	  printf ("/pop_failure_jump to %td", p + mcnt - start);
#  else
  	  printf ("/pop_failure_jump to %ld", (long int) (p + mcnt - start));
#  endif
	  break;

        case jump_past_alt:
	  PREFIX(extract_number_and_incr) (&mcnt, &p);
#  ifdef _LIBC
  	  printf ("/jump_past_alt to %td", p + mcnt - start);
#  else
  	  printf ("/jump_past_alt to %ld", (long int) (p + mcnt - start));
#  endif
	  break;

        case jump:
	  PREFIX(extract_number_and_incr) (&mcnt, &p);
#  ifdef _LIBC
  	  printf ("/jump to %td", p + mcnt - start);
#  else
  	  printf ("/jump to %ld", (long int) (p + mcnt - start));
#  endif
	  break;

        case succeed_n:
          PREFIX(extract_number_and_incr) (&mcnt, &p);
	  p1 = p + mcnt;
          PREFIX(extract_number_and_incr) (&mcnt2, &p);
#  ifdef _LIBC
	  printf ("/succeed_n to %td, %d times", p1 - start, mcnt2);
#  else
	  printf ("/succeed_n to %ld, %d times",
		  (long int) (p1 - start), mcnt2);
#  endif
          break;

        case jump_n:
          PREFIX(extract_number_and_incr) (&mcnt, &p);
	  p1 = p + mcnt;
          PREFIX(extract_number_and_incr) (&mcnt2, &p);
	  printf ("/jump_n to %d, %d times", p1 - start, mcnt2);
          break;

        case set_number_at:
          PREFIX(extract_number_and_incr) (&mcnt, &p);
	  p1 = p + mcnt;
          PREFIX(extract_number_and_incr) (&mcnt2, &p);
#  ifdef _LIBC
	  printf ("/set_number_at location %td to %d", p1 - start, mcnt2);
#  else
	  printf ("/set_number_at location %ld to %d",
		  (long int) (p1 - start), mcnt2);
#  endif
          break;

        case wordbound:
	  printf ("/wordbound");
	  break;

	case notwordbound:
	  printf ("/notwordbound");
          break;

	case wordbeg:
	  printf ("/wordbeg");
	  break;

	case wordend:
	  printf ("/wordend");
	  break;

#  ifdef emacs
	case before_dot:
	  printf ("/before_dot");
          break;

	case at_dot:
	  printf ("/at_dot");
          break;

	case after_dot:
	  printf ("/after_dot");
          break;

	case syntaxspec:
          printf ("/syntaxspec");
	  mcnt = *p++;
	  printf ("/%d", mcnt);
          break;

	case notsyntaxspec:
          printf ("/notsyntaxspec");
	  mcnt = *p++;
	  printf ("/%d", mcnt);
	  break;
#  endif /* emacs */

	case wordchar:
	  printf ("/wordchar");
          break;

	case notwordchar:
	  printf ("/notwordchar");
          break;

	case begbuf:
	  printf ("/begbuf");
          break;

	case endbuf:
	  printf ("/endbuf");
          break;

        default:
          printf ("?%ld", (long int) *(p-1));
	}

      putchar ('\n');
    }

#  ifdef _LIBC
  printf ("%td:\tend of pattern.\n", p - start);
#  else
  printf ("%ld:\tend of pattern.\n", (long int) (p - start));
#  endif
}


void
PREFIX(print_compiled_pattern) (struct re_pattern_buffer *bufp)
{
  UCHAR_T *buffer = (UCHAR_T*) bufp->buffer;

  PREFIX(print_partial_compiled_pattern) (buffer, buffer
				  + bufp->used / sizeof(UCHAR_T));
  printf ("%ld bytes used/%ld bytes allocated.\n",
	  bufp->used, bufp->allocated);

  if (bufp->fastmap_accurate && bufp->fastmap)
    {
      printf ("fastmap: ");
      print_fastmap (bufp->fastmap);
    }

#  ifdef _LIBC
  printf ("re_nsub: %Zd\t", bufp->re_nsub);
#  else
  printf ("re_nsub: %ld\t", (long int) bufp->re_nsub);
#  endif
  printf ("regs_alloc: %d\t", bufp->regs_allocated);
  printf ("can_be_null: %d\t", bufp->can_be_null);
  printf ("newline_anchor: %d\n", bufp->newline_anchor);
  printf ("no_sub: %d\t", bufp->no_sub);
  printf ("not_bol: %d\t", bufp->not_bol);
  printf ("not_eol: %d\t", bufp->not_eol);
  printf ("syntax: %lx\n", bufp->syntax);
  /* Perhaps we should print the translate table?  */
}


void
PREFIX(print_double_string) (const CHAR_T *where, const CHAR_T *string1,
                             int size1, const CHAR_T *string2, int size2)
{
  int this_char;

  if (where == NULL)
    printf ("(null)");
  else
    {
      int cnt;

      if (FIRST_STRING_P (where))
        {
          for (this_char = where - string1; this_char < size1; this_char++)
	    PUT_CHAR (string1[this_char]);

          where = string2;
        }

      cnt = 0;
      for (this_char = where - string2; this_char < size2; this_char++)
	{
	  PUT_CHAR (string2[this_char]);
	  if (++cnt > 100)
	    {
	      fputs ("...", stdout);
	      break;
	    }
	}
    }
}

#  ifndef DEFINED_ONCE
void
printchar (int c)
{
  putc (c, stderr);
}
#  endif

# else /* not DEBUG */

#  ifndef DEFINED_ONCE
#   undef assert
#   define assert(e)

#   define DEBUG_STATEMENT(e)
#   define DEBUG_PRINT1(x)
#   define DEBUG_PRINT2(x1, x2)
#   define DEBUG_PRINT3(x1, x2, x3)
#   define DEBUG_PRINT4(x1, x2, x3, x4)
#  endif /* not DEFINED_ONCE */
#  define DEBUG_PRINT_COMPILED_PATTERN(p, s, e)
#  define DEBUG_PRINT_DOUBLE_STRING(w, s1, sz1, s2, sz2)

# endif /* not DEBUG */



# ifdef WCHAR
/* This  convert a multibyte string to a wide character string.
   And write their correspondances to offset_buffer(see below)
   and write whether each wchar_t is binary data to is_binary.
   This assume invalid multibyte sequences as binary data.
   We assume offset_buffer and is_binary is already allocated
   enough space.  */

static size_t convert_mbs_to_wcs (CHAR_T *dest, const unsigned char* src,
				  size_t len, int *offset_buffer,
				  char *is_binary);
static size_t
convert_mbs_to_wcs (CHAR_T *dest, const unsigned char*src, size_t len,
                    int *offset_buffer, char *is_binary)
     /* It hold correspondances between src(char string) and
	dest(wchar_t string) for optimization.
	e.g. src  = "xxxyzz"
             dest = {'X', 'Y', 'Z'}
	      (each "xxx", "y" and "zz" represent one multibyte character
	       corresponding to 'X', 'Y' and 'Z'.)
	  offset_buffer = {0, 0+3("xxx"), 0+3+1("y"), 0+3+1+2("zz")}
	  	        = {0, 3, 4, 6}
     */
{
  wchar_t *pdest = dest;
  const unsigned char *psrc = src;
  size_t wc_count = 0;

  mbstate_t mbs;
  int i, consumed;
  size_t mb_remain = len;
  size_t mb_count = 0;

  /* Initialize the conversion state.  */
  memset (&mbs, 0, sizeof (mbstate_t));

  offset_buffer[0] = 0;
  for( ; mb_remain > 0 ; ++wc_count, ++pdest, mb_remain -= consumed,
	 psrc += consumed)
    {
#ifdef _LIBC
      consumed = __mbrtowc (pdest, psrc, mb_remain, &mbs);
#else
      consumed = mbrtowc (pdest, psrc, mb_remain, &mbs);
#endif

      if (consumed <= 0)
	/* failed to convert. maybe src contains binary data.
	   So we consume 1 byte manualy.  */
	{
	  *pdest = *psrc;
	  consumed = 1;
	  is_binary[wc_count] = TRUE;
	}
      else
	is_binary[wc_count] = FALSE;
      /* In sjis encoding, we use yen sign as escape character in
	 place of reverse solidus. So we convert 0x5c(yen sign in
	 sjis) to not 0xa5(yen sign in UCS2) but 0x5c(reverse
	 solidus in UCS2).  */
      if (consumed == 1 && (int) *psrc == 0x5c && (int) *pdest == 0xa5)
	*pdest = (wchar_t) *psrc;

      offset_buffer[wc_count + 1] = mb_count += consumed;
    }

  /* Fill remain of the buffer with sentinel.  */
  for (i = wc_count + 1 ; i <= len ; i++)
    offset_buffer[i] = mb_count + 1;

  return wc_count;
}

# endif /* WCHAR */

#else /* not INSIDE_RECURSION */

/* Set by `re_set_syntax' to the current regexp syntax to recognize.  Can
   also be assigned to arbitrarily: each pattern buffer stores its own
   syntax, so it can be changed between regex compilations.  */
/* This has no initializer because initialized variables in Emacs
   become read-only after dumping.  */
reg_syntax_t re_syntax_options;


/* Specify the precise syntax of regexps for compilation.  This provides
   for compatibility for various utilities which historically have
   different, incompatible syntaxes.

   The argument SYNTAX is a bit mask comprised of the various bits
   defined in regex.h.  We return the old syntax.  */

reg_syntax_t
re_set_syntax (reg_syntax_t syntax)
{
  reg_syntax_t ret = re_syntax_options;

  re_syntax_options = syntax;
# ifdef DEBUG
  if (syntax & RE_DEBUG)
    debug = 1;
  else if (debug) /* was on but now is not */
    debug = 0;
# endif /* DEBUG */
  return ret;
}
# ifdef _LIBC
weak_alias (__re_set_syntax, re_set_syntax)
# endif

/* This table gives an error message for each of the error codes listed
   in regex.h.  Obviously the order here has to be same as there.
   POSIX doesn't require that we do anything for REG_NOERROR,
   but why not be nice?  */

static const char *re_error_msgid[] =
  {
    gettext_noop ("Success"),	/* REG_NOERROR */
    gettext_noop ("No match"),	/* REG_NOMATCH */
    gettext_noop ("Invalid regular expression"), /* REG_BADPAT */
    gettext_noop ("Invalid collation character"), /* REG_ECOLLATE */
    gettext_noop ("Invalid character class name"), /* REG_ECTYPE */
    gettext_noop ("Trailing backslash"), /* REG_EESCAPE */
    gettext_noop ("Invalid back reference"), /* REG_ESUBREG */
    gettext_noop ("Unmatched [ or [^"),	/* REG_EBRACK */
    gettext_noop ("Unmatched ( or \\("), /* REG_EPAREN */
    gettext_noop ("Unmatched \\{"), /* REG_EBRACE */
    gettext_noop ("Invalid content of \\{\\}"), /* REG_BADBR */
    gettext_noop ("Invalid range end"),	/* REG_ERANGE */
    gettext_noop ("Memory exhausted"), /* REG_ESPACE */
    gettext_noop ("Invalid preceding regular expression"), /* REG_BADRPT */
    gettext_noop ("Premature end of regular expression"), /* REG_EEND */
    gettext_noop ("Regular expression too big"), /* REG_ESIZE */
    gettext_noop ("Unmatched ) or \\)") /* REG_ERPAREN */
  };

#endif /* INSIDE_RECURSION */

#ifndef DEFINED_ONCE
/* Avoiding alloca during matching, to placate r_alloc.  */

/* Define MATCH_MAY_ALLOCATE unless we need to make sure that the
   searching and matching functions should not call alloca.  On some
   systems, alloca is implemented in terms of malloc, and if we're
   using the relocating allocator routines, then malloc could cause a
   relocation, which might (if the strings being searched are in the
   ralloc heap) shift the data out from underneath the regexp
   routines.

   Here's another reason to avoid allocation: Emacs
   processes input from X in a signal handler; processing X input may
   call malloc; if input arrives while a matching routine is calling
   malloc, then we're scrod.  But Emacs can't just block input while
   calling matching routines; then we don't notice interrupts when
   they come in.  So, Emacs blocks input around all regexp calls
   except the matching calls, which it leaves unprotected, in the
   faith that they will not malloc.  */

/* Normally, this is fine.  */
# define MATCH_MAY_ALLOCATE

/* When using GNU C, we are not REALLY using the C alloca, no matter
   what config.h may say.  So don't take precautions for it.  */
# ifdef __GNUC__
#  undef C_ALLOCA
# endif

/* The match routines may not allocate if (1) they would do it with malloc
   and (2) it's not safe for them to use malloc.
   Note that if REL_ALLOC is defined, matching would not use malloc for the
   failure stack, but we would still use it for the register vectors;
   so REL_ALLOC should not affect this.  */
# if (defined C_ALLOCA || defined REGEX_MALLOC) && defined emacs
#  undef MATCH_MAY_ALLOCATE
# endif
#endif /* not DEFINED_ONCE */

#ifdef INSIDE_RECURSION
/* Failure stack declarations and macros; both re_compile_fastmap and
   re_match_2 use a failure stack.  These have to be macros because of
   REGEX_ALLOCATE_STACK.  */


/* Number of failure points for which to initially allocate space
   when matching.  If this number is exceeded, we allocate more
   space, so it is not a hard limit.  */
# ifndef INIT_FAILURE_ALLOC
#  define INIT_FAILURE_ALLOC 5
# endif

/* Roughly the maximum number of failure points on the stack.  Would be
   exactly that if always used MAX_FAILURE_ITEMS items each time we failed.
   This is a variable only so users of regex can assign to it; we never
   change it ourselves.  */

# ifdef INT_IS_16BIT

#  ifndef DEFINED_ONCE
#   if defined MATCH_MAY_ALLOCATE
/* 4400 was enough to cause a crash on Alpha OSF/1,
   whose default stack limit is 2mb.  */
long int re_max_failures = 4000;
#   else
long int re_max_failures = 2000;
#   endif
#  endif

union PREFIX(fail_stack_elt)
{
  UCHAR_T *pointer;
  long int integer;
};

typedef union PREFIX(fail_stack_elt) PREFIX(fail_stack_elt_t);

typedef struct
{
  PREFIX(fail_stack_elt_t) *stack;
  unsigned long int size;
  unsigned long int avail;		/* Offset of next open position.  */
} PREFIX(fail_stack_type);

# else /* not INT_IS_16BIT */

#  ifndef DEFINED_ONCE
#   if defined MATCH_MAY_ALLOCATE
/* 4400 was enough to cause a crash on Alpha OSF/1,
   whose default stack limit is 2mb.  */
int re_max_failures = 4000;
#   else
int re_max_failures = 2000;
#   endif
#  endif

union PREFIX(fail_stack_elt)
{
  UCHAR_T *pointer;
  int integer;
};

typedef union PREFIX(fail_stack_elt) PREFIX(fail_stack_elt_t);

typedef struct
{
  PREFIX(fail_stack_elt_t) *stack;
  unsigned size;
  unsigned avail;			/* Offset of next open position.  */
} PREFIX(fail_stack_type);

# endif /* INT_IS_16BIT */

# ifndef DEFINED_ONCE
#  define FAIL_STACK_EMPTY()     (fail_stack.avail == 0)
#  define FAIL_STACK_PTR_EMPTY() (fail_stack_ptr->avail == 0)
#  define FAIL_STACK_FULL()      (fail_stack.avail == fail_stack.size)
# endif


/* Define macros to initialize and free the failure stack.
   Do `return -2' if the alloc fails.  */

# ifdef MATCH_MAY_ALLOCATE
#  define INIT_FAIL_STACK()						\
  do {									\
    fail_stack.stack = (PREFIX(fail_stack_elt_t) *)		\
      REGEX_ALLOCATE_STACK (INIT_FAILURE_ALLOC * sizeof (PREFIX(fail_stack_elt_t))); \
									\
    if (fail_stack.stack == NULL)				\
      return -2;							\
									\
    fail_stack.size = INIT_FAILURE_ALLOC;			\
    fail_stack.avail = 0;					\
  } while (0)

#  define RESET_FAIL_STACK()  REGEX_FREE_STACK (fail_stack.stack)
# else
#  define INIT_FAIL_STACK()						\
  do {									\
    fail_stack.avail = 0;					\
  } while (0)

#  define RESET_FAIL_STACK()
# endif


/* Double the size of FAIL_STACK, up to approximately `re_max_failures' items.

   Return 1 if succeeds, and 0 if either ran out of memory
   allocating space for it or it was already too large.

   REGEX_REALLOCATE_STACK requires `destination' be declared.   */

# define DOUBLE_FAIL_STACK(fail_stack)					\
  ((fail_stack).size > (unsigned) (re_max_failures * MAX_FAILURE_ITEMS)	\
   ? 0									\
   : ((fail_stack).stack = (PREFIX(fail_stack_elt_t) *)			\
        REGEX_REALLOCATE_STACK ((fail_stack).stack, 			\
          (fail_stack).size * sizeof (PREFIX(fail_stack_elt_t)),	\
          ((fail_stack).size << 1) * sizeof (PREFIX(fail_stack_elt_t))),\
									\
      (fail_stack).stack == NULL					\
      ? 0								\
      : ((fail_stack).size <<= 1, 					\
         1)))


/* Push pointer POINTER on FAIL_STACK.
   Return 1 if was able to do so and 0 if ran out of memory allocating
   space to do so.  */
# define PUSH_PATTERN_OP(POINTER, FAIL_STACK)				\
  ((FAIL_STACK_FULL ()							\
    && !DOUBLE_FAIL_STACK (FAIL_STACK))					\
   ? 0									\
   : ((FAIL_STACK).stack[(FAIL_STACK).avail++].pointer = POINTER,	\
      1))

/* Push a pointer value onto the failure stack.
   Assumes the variable `fail_stack'.  Probably should only
   be called from within `PUSH_FAILURE_POINT'.  */
# define PUSH_FAILURE_POINTER(item)					\
  fail_stack.stack[fail_stack.avail++].pointer = (UCHAR_T *) (item)

/* This pushes an integer-valued item onto the failure stack.
   Assumes the variable `fail_stack'.  Probably should only
   be called from within `PUSH_FAILURE_POINT'.  */
# define PUSH_FAILURE_INT(item)					\
  fail_stack.stack[fail_stack.avail++].integer = (item)

/* Push a fail_stack_elt_t value onto the failure stack.
   Assumes the variable `fail_stack'.  Probably should only
   be called from within `PUSH_FAILURE_POINT'.  */
# define PUSH_FAILURE_ELT(item)					\
  fail_stack.stack[fail_stack.avail++] =  (item)

/* These three POP... operations complement the three PUSH... operations.
   All assume that `fail_stack' is nonempty.  */
# define POP_FAILURE_POINTER() fail_stack.stack[--fail_stack.avail].pointer
# define POP_FAILURE_INT() fail_stack.stack[--fail_stack.avail].integer
# define POP_FAILURE_ELT() fail_stack.stack[--fail_stack.avail]

/* Used to omit pushing failure point id's when we're not debugging.  */
# ifdef DEBUG
#  define DEBUG_PUSH PUSH_FAILURE_INT
#  define DEBUG_POP(item_addr) *(item_addr) = POP_FAILURE_INT ()
# else
#  define DEBUG_PUSH(item)
#  define DEBUG_POP(item_addr)
# endif


/* Push the information about the state we will need
   if we ever fail back to it.

   Requires variables fail_stack, regstart, regend, reg_info, and
   num_regs_pushed be declared.  DOUBLE_FAIL_STACK requires `destination'
   be declared.

   Does `return FAILURE_CODE' if runs out of memory.  */

# define PUSH_FAILURE_POINT(pattern_place, string_place, failure_code)	\
  do {									\
    char *destination;							\
    /* Must be int, so when we don't save any registers, the arithmetic	\
       of 0 + -1 isn't done as unsigned.  */				\
    /* Can't be int, since there is not a shred of a guarantee that int	\
       is wide enough to hold a value of something to which pointer can	\
       be assigned */							\
    active_reg_t this_reg;						\
    									\
    DEBUG_STATEMENT (failure_id++);					\
    DEBUG_STATEMENT (nfailure_points_pushed++);				\
    DEBUG_PRINT2 ("\nPUSH_FAILURE_POINT #%u:\n", failure_id);		\
    DEBUG_PRINT2 ("  Before push, next avail: %d\n", (fail_stack).avail);\
    DEBUG_PRINT2 ("                     size: %d\n", (fail_stack).size);\
									\
    DEBUG_PRINT2 ("  slots needed: %ld\n", NUM_FAILURE_ITEMS);		\
    DEBUG_PRINT2 ("     available: %d\n", REMAINING_AVAIL_SLOTS);	\
									\
    /* Ensure we have enough space allocated for what we will push.  */	\
    while (REMAINING_AVAIL_SLOTS < NUM_FAILURE_ITEMS)			\
      {									\
        if (!DOUBLE_FAIL_STACK (fail_stack))				\
          return failure_code;						\
									\
        DEBUG_PRINT2 ("\n  Doubled stack; size now: %d\n",		\
		       (fail_stack).size);				\
        DEBUG_PRINT2 ("  slots available: %d\n", REMAINING_AVAIL_SLOTS);\
      }									\
									\
    /* Push the info, starting with the registers.  */			\
    DEBUG_PRINT1 ("\n");						\
									\
    if (1)								\
      for (this_reg = lowest_active_reg; this_reg <= highest_active_reg; \
	   this_reg++)							\
	{								\
	  DEBUG_PRINT2 ("  Pushing reg: %lu\n", this_reg);		\
	  DEBUG_STATEMENT (num_regs_pushed++);				\
									\
	  DEBUG_PRINT2 ("    start: %p\n", regstart[this_reg]);		\
	  PUSH_FAILURE_POINTER (regstart[this_reg]);			\
									\
	  DEBUG_PRINT2 ("    end: %p\n", regend[this_reg]);		\
	  PUSH_FAILURE_POINTER (regend[this_reg]);			\
									\
	  DEBUG_PRINT2 ("    info: %p\n      ",				\
			reg_info[this_reg].word.pointer);		\
	  DEBUG_PRINT2 (" match_null=%d",				\
			REG_MATCH_NULL_STRING_P (reg_info[this_reg]));	\
	  DEBUG_PRINT2 (" active=%d", IS_ACTIVE (reg_info[this_reg]));	\
	  DEBUG_PRINT2 (" matched_something=%d",			\
			MATCHED_SOMETHING (reg_info[this_reg]));	\
	  DEBUG_PRINT2 (" ever_matched=%d",				\
			EVER_MATCHED_SOMETHING (reg_info[this_reg]));	\
	  DEBUG_PRINT1 ("\n");						\
	  PUSH_FAILURE_ELT (reg_info[this_reg].word);			\
	}								\
									\
    DEBUG_PRINT2 ("  Pushing  low active reg: %ld\n", lowest_active_reg);\
    PUSH_FAILURE_INT (lowest_active_reg);				\
									\
    DEBUG_PRINT2 ("  Pushing high active reg: %ld\n", highest_active_reg);\
    PUSH_FAILURE_INT (highest_active_reg);				\
									\
    DEBUG_PRINT2 ("  Pushing pattern %p:\n", pattern_place);		\
    DEBUG_PRINT_COMPILED_PATTERN (bufp, pattern_place, pend);		\
    PUSH_FAILURE_POINTER (pattern_place);				\
									\
    DEBUG_PRINT2 ("  Pushing string %p: `", string_place);		\
    DEBUG_PRINT_DOUBLE_STRING (string_place, string1, size1, string2,   \
				 size2);				\
    DEBUG_PRINT1 ("'\n");						\
    PUSH_FAILURE_POINTER (string_place);				\
									\
    DEBUG_PRINT2 ("  Pushing failure id: %u\n", failure_id);		\
    DEBUG_PUSH (failure_id);						\
  } while (0)

# ifndef DEFINED_ONCE
/* This is the number of items that are pushed and popped on the stack
   for each register.  */
#  define NUM_REG_ITEMS  3

/* Individual items aside from the registers.  */
#  ifdef DEBUG
#   define NUM_NONREG_ITEMS 5 /* Includes failure point id.  */
#  else
#   define NUM_NONREG_ITEMS 4
#  endif

/* We push at most this many items on the stack.  */
/* We used to use (num_regs - 1), which is the number of registers
   this regexp will save; but that was changed to 5
   to avoid stack overflow for a regexp with lots of parens.  */
#  define MAX_FAILURE_ITEMS (5 * NUM_REG_ITEMS + NUM_NONREG_ITEMS)

/* We actually push this many items.  */
#  define NUM_FAILURE_ITEMS				\
  (((0							\
     ? 0 : highest_active_reg - lowest_active_reg + 1)	\
    * NUM_REG_ITEMS)					\
   + NUM_NONREG_ITEMS)

/* How many items can still be added to the stack without overflowing it.  */
#  define REMAINING_AVAIL_SLOTS ((fail_stack).size - (fail_stack).avail)
# endif /* not DEFINED_ONCE */


/* Pops what PUSH_FAIL_STACK pushes.

   We restore into the parameters, all of which should be lvalues:
     STR -- the saved data position.
     PAT -- the saved pattern position.
     LOW_REG, HIGH_REG -- the highest and lowest active registers.
     REGSTART, REGEND -- arrays of string positions.
     REG_INFO -- array of information about each subexpression.

   Also assumes the variables `fail_stack' and (if debugging), `bufp',
   `pend', `string1', `size1', `string2', and `size2'.  */
# define POP_FAILURE_POINT(str, pat, low_reg, high_reg, regstart, regend, reg_info)\
{									\
  DEBUG_STATEMENT (unsigned failure_id;)				\
  active_reg_t this_reg;						\
  const UCHAR_T *string_temp;						\
									\
  assert (!FAIL_STACK_EMPTY ());					\
									\
  /* Remove failure points and point to how many regs pushed.  */	\
  DEBUG_PRINT1 ("POP_FAILURE_POINT:\n");				\
  DEBUG_PRINT2 ("  Before pop, next avail: %d\n", fail_stack.avail);	\
  DEBUG_PRINT2 ("                    size: %d\n", fail_stack.size);	\
									\
  assert (fail_stack.avail >= NUM_NONREG_ITEMS);			\
									\
  DEBUG_POP (&failure_id);						\
  DEBUG_PRINT2 ("  Popping failure id: %u\n", failure_id);		\
									\
  /* If the saved string location is NULL, it came from an		\
     on_failure_keep_string_jump opcode, and we want to throw away the	\
     saved NULL, thus retaining our current position in the string.  */	\
  string_temp = POP_FAILURE_POINTER ();					\
  if (string_temp != NULL)						\
    str = (const CHAR_T *) string_temp;					\
									\
  DEBUG_PRINT2 ("  Popping string %p: `", str);				\
  DEBUG_PRINT_DOUBLE_STRING (str, string1, size1, string2, size2);	\
  DEBUG_PRINT1 ("'\n");							\
									\
  pat = (UCHAR_T *) POP_FAILURE_POINTER ();				\
  DEBUG_PRINT2 ("  Popping pattern %p:\n", pat);			\
  DEBUG_PRINT_COMPILED_PATTERN (bufp, pat, pend);			\
									\
  /* Restore register info.  */						\
  high_reg = (active_reg_t) POP_FAILURE_INT ();				\
  DEBUG_PRINT2 ("  Popping high active reg: %ld\n", high_reg);		\
									\
  low_reg = (active_reg_t) POP_FAILURE_INT ();				\
  DEBUG_PRINT2 ("  Popping  low active reg: %ld\n", low_reg);		\
									\
  if (1)								\
    for (this_reg = high_reg; this_reg >= low_reg; this_reg--)		\
      {									\
	DEBUG_PRINT2 ("    Popping reg: %ld\n", this_reg);		\
									\
	reg_info[this_reg].word = POP_FAILURE_ELT ();			\
	DEBUG_PRINT2 ("      info: %p\n",				\
		      reg_info[this_reg].word.pointer);			\
									\
	regend[this_reg] = (const CHAR_T *) POP_FAILURE_POINTER ();	\
	DEBUG_PRINT2 ("      end: %p\n", regend[this_reg]);		\
									\
	regstart[this_reg] = (const CHAR_T *) POP_FAILURE_POINTER ();	\
	DEBUG_PRINT2 ("      start: %p\n", regstart[this_reg]);		\
      }									\
  else									\
    {									\
      for (this_reg = highest_active_reg; this_reg > high_reg; this_reg--) \
	{								\
	  reg_info[this_reg].word.integer = 0;				\
	  regend[this_reg] = 0;						\
	  regstart[this_reg] = 0;					\
	}								\
      highest_active_reg = high_reg;					\
    }									\
									\
  set_regs_matched_done = 0;						\
  DEBUG_STATEMENT (nfailure_points_popped++);				\
} /* POP_FAILURE_POINT */

/* Structure for per-register (a.k.a. per-group) information.
   Other register information, such as the
   starting and ending positions (which are addresses), and the list of
   inner groups (which is a bits list) are maintained in separate
   variables.

   We are making a (strictly speaking) nonportable assumption here: that
   the compiler will pack our bit fields into something that fits into
   the type of `word', i.e., is something that fits into one item on the
   failure stack.  */


/* Declarations and macros for re_match_2.  */

typedef union
{
  PREFIX(fail_stack_elt_t) word;
  struct
  {
      /* This field is one if this group can match the empty string,
         zero if not.  If not yet determined,  `MATCH_NULL_UNSET_VALUE'.  */
# define MATCH_NULL_UNSET_VALUE 3
    unsigned match_null_string_p : 2;
    unsigned is_active : 1;
    unsigned matched_something : 1;
    unsigned ever_matched_something : 1;
  } bits;
} PREFIX(register_info_type);

# ifndef DEFINED_ONCE
#  define REG_MATCH_NULL_STRING_P(R)  ((R).bits.match_null_string_p)
#  define IS_ACTIVE(R)  ((R).bits.is_active)
#  define MATCHED_SOMETHING(R)  ((R).bits.matched_something)
#  define EVER_MATCHED_SOMETHING(R)  ((R).bits.ever_matched_something)


/* Call this when have matched a real character; it sets `matched' flags
   for the subexpressions which we are currently inside.  Also records
   that those subexprs have matched.  */
#  define SET_REGS_MATCHED()						\
  do									\
    {									\
      if (!set_regs_matched_done)					\
	{								\
	  active_reg_t r;						\
	  set_regs_matched_done = 1;					\
	  for (r = lowest_active_reg; r <= highest_active_reg; r++)	\
	    {								\
	      MATCHED_SOMETHING (reg_info[r])				\
		= EVER_MATCHED_SOMETHING (reg_info[r])			\
		= 1;							\
	    }								\
	}								\
    }									\
  while (0)
# endif /* not DEFINED_ONCE */

/* Registers are set to a sentinel when they haven't yet matched.  */
static CHAR_T PREFIX(reg_unset_dummy);
# define REG_UNSET_VALUE (&PREFIX(reg_unset_dummy))
# define REG_UNSET(e) ((e) == REG_UNSET_VALUE)

/* Subroutine declarations and macros for regex_compile.  */
static void PREFIX(store_op1) (re_opcode_t op, UCHAR_T *loc, int arg);
static void PREFIX(store_op2) (re_opcode_t op, UCHAR_T *loc,
                               int arg1, int arg2);
static void PREFIX(insert_op1) (re_opcode_t op, UCHAR_T *loc,
                                int arg, UCHAR_T *end);
static void PREFIX(insert_op2) (re_opcode_t op, UCHAR_T *loc,
                                int arg1, int arg2, UCHAR_T *end);
static boolean PREFIX(at_begline_loc_p) (const CHAR_T *pattern,
                                         const CHAR_T *p,
                                         reg_syntax_t syntax);
static boolean PREFIX(at_endline_loc_p) (const CHAR_T *p,
                                         const CHAR_T *pend,
                                         reg_syntax_t syntax);
# ifdef WCHAR
static reg_errcode_t wcs_compile_range (CHAR_T range_start,
                                        const CHAR_T **p_ptr,
                                        const CHAR_T *pend,
                                        char *translate,
                                        reg_syntax_t syntax,
                                        UCHAR_T *b,
                                        CHAR_T *char_set);
static void insert_space (int num, CHAR_T *loc, CHAR_T *end);
# else /* BYTE */
static reg_errcode_t byte_compile_range (unsigned int range_start,
                                         const char **p_ptr,
                                         const char *pend,
                                         char *translate,
                                         reg_syntax_t syntax,
                                         unsigned char *b);
# endif /* WCHAR */

/* Fetch the next character in the uncompiled pattern---translating it
   if necessary.  Also cast from a signed character in the constant
   string passed to us by the user to an unsigned char that we can use
   as an array index (in, e.g., `translate').  */
/* ifdef MBS_SUPPORT, we translate only if character <= 0xff,
   because it is impossible to allocate 4GB array for some encodings
   which have 4 byte character_set like UCS4.  */
# ifndef PATFETCH
#  ifdef WCHAR
#   define PATFETCH(c)							\
  do {if (p == pend) return REG_EEND;					\
    c = (UCHAR_T) *p++;							\
    if (translate && (c <= 0xff)) c = (UCHAR_T) translate[c];		\
  } while (0)
#  else /* BYTE */
#   define PATFETCH(c)							\
  do {if (p == pend) return REG_EEND;					\
    c = (unsigned char) *p++;						\
    if (translate) c = (unsigned char) translate[c];			\
  } while (0)
#  endif /* WCHAR */
# endif

/* Fetch the next character in the uncompiled pattern, with no
   translation.  */
# define PATFETCH_RAW(c)						\
  do {if (p == pend) return REG_EEND;					\
    c = (UCHAR_T) *p++; 	       					\
  } while (0)

/* Go backwards one character in the pattern.  */
# define PATUNFETCH p--


/* If `translate' is non-null, return translate[D], else just D.  We
   cast the subscript to translate because some data is declared as
   `char *', to avoid warnings when a string constant is passed.  But
   when we use a character as a subscript we must make it unsigned.  */
/* ifdef MBS_SUPPORT, we translate only if character <= 0xff,
   because it is impossible to allocate 4GB array for some encodings
   which have 4 byte character_set like UCS4.  */

# ifndef TRANSLATE
#  ifdef WCHAR
#   define TRANSLATE(d) \
  ((translate && ((UCHAR_T) (d)) <= 0xff) \
   ? (char) translate[(unsigned char) (d)] : (d))
# else /* BYTE */
#   define TRANSLATE(d) \
  (translate ? (char) translate[(unsigned char) (d)] : (char) (d))
#  endif /* WCHAR */
# endif


/* Macros for outputting the compiled pattern into `buffer'.  */

/* If the buffer isn't allocated when it comes in, use this.  */
# define INIT_BUF_SIZE  (32 * sizeof(UCHAR_T))

/* Make sure we have at least N more bytes of space in buffer.  */
# ifdef WCHAR
#  define GET_BUFFER_SPACE(n)						\
    while (((unsigned long)b - (unsigned long)COMPILED_BUFFER_VAR	\
            + (n)*sizeof(CHAR_T)) > bufp->allocated)			\
      EXTEND_BUFFER ()
# else /* BYTE */
#  define GET_BUFFER_SPACE(n)						\
    while ((unsigned long) (b - bufp->buffer + (n)) > bufp->allocated)	\
      EXTEND_BUFFER ()
# endif /* WCHAR */

/* Make sure we have one more byte of buffer space and then add C to it.  */
# define BUF_PUSH(c)							\
  do {									\
    GET_BUFFER_SPACE (1);						\
    *b++ = (UCHAR_T) (c);						\
  } while (0)


/* Ensure we have two more bytes of buffer space and then append C1 and C2.  */
# define BUF_PUSH_2(c1, c2)						\
  do {									\
    GET_BUFFER_SPACE (2);						\
    *b++ = (UCHAR_T) (c1);						\
    *b++ = (UCHAR_T) (c2);						\
  } while (0)


/* As with BUF_PUSH_2, except for three bytes.  */
# define BUF_PUSH_3(c1, c2, c3)						\
  do {									\
    GET_BUFFER_SPACE (3);						\
    *b++ = (UCHAR_T) (c1);						\
    *b++ = (UCHAR_T) (c2);						\
    *b++ = (UCHAR_T) (c3);						\
  } while (0)

/* Store a jump with opcode OP at LOC to location TO.  We store a
   relative address offset by the three bytes the jump itself occupies.  */
# define STORE_JUMP(op, loc, to) \
 PREFIX(store_op1) (op, loc, (int) ((to) - (loc) - (1 + OFFSET_ADDRESS_SIZE)))

/* Likewise, for a two-argument jump.  */
# define STORE_JUMP2(op, loc, to, arg) \
  PREFIX(store_op2) (op, loc, (int) ((to) - (loc) - (1 + OFFSET_ADDRESS_SIZE)), arg)

/* Like `STORE_JUMP', but for inserting.  Assume `b' is the buffer end.  */
# define INSERT_JUMP(op, loc, to) \
  PREFIX(insert_op1) (op, loc, (int) ((to) - (loc) - (1 + OFFSET_ADDRESS_SIZE)), b)

/* Like `STORE_JUMP2', but for inserting.  Assume `b' is the buffer end.  */
# define INSERT_JUMP2(op, loc, to, arg) \
  PREFIX(insert_op2) (op, loc, (int) ((to) - (loc) - (1 + OFFSET_ADDRESS_SIZE)),\
	      arg, b)

/* This is not an arbitrary limit: the arguments which represent offsets
   into the pattern are two bytes long.  So if 2^16 bytes turns out to
   be too small, many things would have to change.  */
/* Any other compiler which, like MSC, has allocation limit below 2^16
   bytes will have to use approach similar to what was done below for
   MSC and drop MAX_BUF_SIZE a bit.  Otherwise you may end up
   reallocating to 0 bytes.  Such thing is not going to work too well.
   You have been warned!!  */
# ifndef DEFINED_ONCE
#  if defined _MSC_VER  && !defined WIN32
/* Microsoft C 16-bit versions limit malloc to approx 65512 bytes.
   The REALLOC define eliminates a flurry of conversion warnings,
   but is not required. */
#   define MAX_BUF_SIZE  65500L
#   define REALLOC(p,s) realloc ((p), (size_t) (s))
#  else
#   define MAX_BUF_SIZE (1L << 16)
#   define REALLOC(p,s) realloc ((p), (s))
#  endif

/* Extend the buffer by twice its current size via realloc and
   reset the pointers that pointed into the old block to point to the
   correct places in the new one.  If extending the buffer results in it
   being larger than MAX_BUF_SIZE, then flag memory exhausted.  */
#  if __BOUNDED_POINTERS__
#   define SET_HIGH_BOUND(P) (__ptrhigh (P) = __ptrlow (P) + bufp->allocated)
#   define MOVE_BUFFER_POINTER(P) \
  (__ptrlow (P) += incr, SET_HIGH_BOUND (P), __ptrvalue (P) += incr)
#   define ELSE_EXTEND_BUFFER_HIGH_BOUND	\
  else						\
    {						\
      SET_HIGH_BOUND (b);			\
      SET_HIGH_BOUND (begalt);			\
      if (fixup_alt_jump)			\
	SET_HIGH_BOUND (fixup_alt_jump);	\
      if (laststart)				\
	SET_HIGH_BOUND (laststart);		\
      if (pending_exact)			\
	SET_HIGH_BOUND (pending_exact);		\
    }
#  else
#   define MOVE_BUFFER_POINTER(P) (P) += incr
#   define ELSE_EXTEND_BUFFER_HIGH_BOUND
#  endif
# endif /* not DEFINED_ONCE */

# ifdef WCHAR
#  define EXTEND_BUFFER()						\
  do {									\
    UCHAR_T *old_buffer = COMPILED_BUFFER_VAR;				\
    int wchar_count;							\
    if (bufp->allocated + sizeof(UCHAR_T) > MAX_BUF_SIZE)		\
      return REG_ESIZE;							\
    bufp->allocated <<= 1;						\
    if (bufp->allocated > MAX_BUF_SIZE)					\
      bufp->allocated = MAX_BUF_SIZE;					\
    /* How many characters the new buffer can have?  */			\
    wchar_count = bufp->allocated / sizeof(UCHAR_T);			\
    if (wchar_count == 0) wchar_count = 1;				\
    /* Truncate the buffer to CHAR_T align.  */			\
    bufp->allocated = wchar_count * sizeof(UCHAR_T);			\
    RETALLOC (COMPILED_BUFFER_VAR, wchar_count, UCHAR_T);		\
    bufp->buffer = (char*)COMPILED_BUFFER_VAR;				\
    if (COMPILED_BUFFER_VAR == NULL)					\
      return REG_ESPACE;						\
    /* If the buffer moved, move all the pointers into it.  */		\
    if (old_buffer != COMPILED_BUFFER_VAR)				\
      {									\
	int incr = COMPILED_BUFFER_VAR - old_buffer;			\
	MOVE_BUFFER_POINTER (b);					\
	MOVE_BUFFER_POINTER (begalt);					\
	if (fixup_alt_jump)						\
	  MOVE_BUFFER_POINTER (fixup_alt_jump);				\
	if (laststart)							\
	  MOVE_BUFFER_POINTER (laststart);				\
	if (pending_exact)						\
	  MOVE_BUFFER_POINTER (pending_exact);				\
      }									\
    ELSE_EXTEND_BUFFER_HIGH_BOUND					\
  } while (0)
# else /* BYTE */
#  define EXTEND_BUFFER()						\
  do {									\
    UCHAR_T *old_buffer = COMPILED_BUFFER_VAR;				\
    if (bufp->allocated == MAX_BUF_SIZE)				\
      return REG_ESIZE;							\
    bufp->allocated <<= 1;						\
    if (bufp->allocated > MAX_BUF_SIZE)					\
      bufp->allocated = MAX_BUF_SIZE;					\
    bufp->buffer = (UCHAR_T *) REALLOC (COMPILED_BUFFER_VAR,		\
						bufp->allocated);	\
    if (COMPILED_BUFFER_VAR == NULL)					\
      return REG_ESPACE;						\
    /* If the buffer moved, move all the pointers into it.  */		\
    if (old_buffer != COMPILED_BUFFER_VAR)				\
      {									\
	int incr = COMPILED_BUFFER_VAR - old_buffer;			\
	MOVE_BUFFER_POINTER (b);					\
	MOVE_BUFFER_POINTER (begalt);					\
	if (fixup_alt_jump)						\
	  MOVE_BUFFER_POINTER (fixup_alt_jump);				\
	if (laststart)							\
	  MOVE_BUFFER_POINTER (laststart);				\
	if (pending_exact)						\
	  MOVE_BUFFER_POINTER (pending_exact);				\
      }									\
    ELSE_EXTEND_BUFFER_HIGH_BOUND					\
  } while (0)
# endif /* WCHAR */

# ifndef DEFINED_ONCE
/* Since we have one byte reserved for the register number argument to
   {start,stop}_memory, the maximum number of groups we can report
   things about is what fits in that byte.  */
#  define MAX_REGNUM 255

/* But patterns can have more than `MAX_REGNUM' registers.  We just
   ignore the excess.  */
typedef unsigned regnum_t;


/* Macros for the compile stack.  */

/* Since offsets can go either forwards or backwards, this type needs to
   be able to hold values from -(MAX_BUF_SIZE - 1) to MAX_BUF_SIZE - 1.  */
/* int may be not enough when sizeof(int) == 2.  */
typedef long pattern_offset_t;

typedef struct
{
  pattern_offset_t begalt_offset;
  pattern_offset_t fixup_alt_jump;
  pattern_offset_t inner_group_offset;
  pattern_offset_t laststart_offset;
  regnum_t regnum;
} compile_stack_elt_t;


typedef struct
{
  compile_stack_elt_t *stack;
  unsigned size;
  unsigned avail;			/* Offset of next open position.  */
} compile_stack_type;


#  define INIT_COMPILE_STACK_SIZE 32

#  define COMPILE_STACK_EMPTY  (compile_stack.avail == 0)
#  define COMPILE_STACK_FULL  (compile_stack.avail == compile_stack.size)

/* The next available element.  */
#  define COMPILE_STACK_TOP (compile_stack.stack[compile_stack.avail])

# endif /* not DEFINED_ONCE */

/* Set the bit for character C in a list.  */
# ifndef DEFINED_ONCE
#  define SET_LIST_BIT(c)                               \
  (b[((unsigned char) (c)) / BYTEWIDTH]               \
   |= 1 << (((unsigned char) c) % BYTEWIDTH))
# endif /* DEFINED_ONCE */

/* Get the next unsigned number in the uncompiled pattern.  */
# define GET_UNSIGNED_NUMBER(num) \
  {									\
    while (p != pend)							\
      {									\
	PATFETCH (c);							\
	if (c < '0' || c > '9')						\
	  break;							\
	if (num <= RE_DUP_MAX)						\
	  {								\
	    if (num < 0)						\
	      num = 0;							\
	    num = num * 10 + c - '0';					\
	  }								\
      }									\
  }

# ifndef DEFINED_ONCE
#  if defined _LIBC || WIDE_CHAR_SUPPORT
/* The GNU C library provides support for user-defined character classes
   and the functions from ISO C amendement 1.  */
#   ifdef CHARCLASS_NAME_MAX
#    define CHAR_CLASS_MAX_LENGTH CHARCLASS_NAME_MAX
#   else
/* This shouldn't happen but some implementation might still have this
   problem.  Use a reasonable default value.  */
#    define CHAR_CLASS_MAX_LENGTH 256
#   endif

#   ifdef _LIBC
#    define IS_CHAR_CLASS(string) __wctype (string)
#   else
#    define IS_CHAR_CLASS(string) wctype (string)
#   endif
#  else
#   define CHAR_CLASS_MAX_LENGTH  6 /* Namely, `xdigit'.  */

#   define IS_CHAR_CLASS(string)					\
   (STREQ (string, "alpha") || STREQ (string, "upper")			\
    || STREQ (string, "lower") || STREQ (string, "digit")		\
    || STREQ (string, "alnum") || STREQ (string, "xdigit")		\
    || STREQ (string, "space") || STREQ (string, "print")		\
    || STREQ (string, "punct") || STREQ (string, "graph")		\
    || STREQ (string, "cntrl") || STREQ (string, "blank"))
#  endif
# endif /* DEFINED_ONCE */

# ifndef MATCH_MAY_ALLOCATE

/* If we cannot allocate large objects within re_match_2_internal,
   we make the fail stack and register vectors global.
   The fail stack, we grow to the maximum size when a regexp
   is compiled.
   The register vectors, we adjust in size each time we
   compile a regexp, according to the number of registers it needs.  */

static PREFIX(fail_stack_type) fail_stack;

/* Size with which the following vectors are currently allocated.
   That is so we can make them bigger as needed,
   but never make them smaller.  */
#  ifdef DEFINED_ONCE
static int regs_allocated_size;

static const char **     regstart, **     regend;
static const char ** old_regstart, ** old_regend;
static const char **best_regstart, **best_regend;
static const char **reg_dummy;
#  endif /* DEFINED_ONCE */

static PREFIX(register_info_type) *PREFIX(reg_info);
static PREFIX(register_info_type) *PREFIX(reg_info_dummy);

/* Make the register vectors big enough for NUM_REGS registers,
   but don't make them smaller.  */

static void
PREFIX(regex_grow_registers) (int num_regs)
{
  if (num_regs > regs_allocated_size)
    {
      RETALLOC_IF (regstart,	 num_regs, const char *);
      RETALLOC_IF (regend,	 num_regs, const char *);
      RETALLOC_IF (old_regstart, num_regs, const char *);
      RETALLOC_IF (old_regend,	 num_regs, const char *);
      RETALLOC_IF (best_regstart, num_regs, const char *);
      RETALLOC_IF (best_regend,	 num_regs, const char *);
      RETALLOC_IF (PREFIX(reg_info), num_regs, PREFIX(register_info_type));
      RETALLOC_IF (reg_dummy,	 num_regs, const char *);
      RETALLOC_IF (PREFIX(reg_info_dummy), num_regs, PREFIX(register_info_type));

      regs_allocated_size = num_regs;
    }
}

# endif /* not MATCH_MAY_ALLOCATE */

# ifndef DEFINED_ONCE
static boolean group_in_compile_stack (compile_stack_type compile_stack,
                                       regnum_t regnum);
# endif /* not DEFINED_ONCE */

/* `regex_compile' compiles PATTERN (of length SIZE) according to SYNTAX.
   Returns one of error codes defined in `regex.h', or zero for success.

   Assumes the `allocated' (and perhaps `buffer') and `translate'
   fields are set in BUFP on entry.

   If it succeeds, results are put in BUFP (if it returns an error, the
   contents of BUFP are undefined):
     `buffer' is the compiled pattern;
     `syntax' is set to SYNTAX;
     `used' is set to the length of the compiled pattern;
     `fastmap_accurate' is zero;
     `re_nsub' is the number of subexpressions in PATTERN;
     `not_bol' and `not_eol' are zero;

   The `fastmap' and `newline_anchor' fields are neither
   examined nor set.  */

/* Return, freeing storage we allocated.  */
# ifdef WCHAR
#  define FREE_STACK_RETURN(value)		\
  return (free(pattern), free(mbs_offset), free(is_binary), free (compile_stack.stack), value)
# else
#  define FREE_STACK_RETURN(value)		\
  return (free (compile_stack.stack), value)
# endif /* WCHAR */

static reg_errcode_t
PREFIX(regex_compile) (const char *ARG_PREFIX(pattern),
                       size_t ARG_PREFIX(size), reg_syntax_t syntax,
                       struct re_pattern_buffer *bufp)
{
  /* We fetch characters from PATTERN here.  Even though PATTERN is
     `char *' (i.e., signed), we declare these variables as unsigned, so
     they can be reliably used as array indices.  */
  register UCHAR_T c, c1;

#ifdef WCHAR
  /* A temporary space to keep wchar_t pattern and compiled pattern.  */
  CHAR_T *pattern, *COMPILED_BUFFER_VAR;
  size_t size;
  /* offset buffer for optimization. See convert_mbs_to_wc.  */
  int *mbs_offset = NULL;
  /* It hold whether each wchar_t is binary data or not.  */
  char *is_binary = NULL;
  /* A flag whether exactn is handling binary data or not.  */
  char is_exactn_bin = FALSE;
#endif /* WCHAR */

  /* A random temporary spot in PATTERN.  */
  const CHAR_T *p1;

  /* Points to the end of the buffer, where we should append.  */
  register UCHAR_T *b;

  /* Keeps track of unclosed groups.  */
  compile_stack_type compile_stack;

  /* Points to the current (ending) position in the pattern.  */
#ifdef WCHAR
  const CHAR_T *p;
  const CHAR_T *pend;
#else /* BYTE */
  const CHAR_T *p = pattern;
  const CHAR_T *pend = pattern + size;
#endif /* WCHAR */

  /* How to translate the characters in the pattern.  */
  RE_TRANSLATE_TYPE translate = bufp->translate;

  /* Address of the count-byte of the most recently inserted `exactn'
     command.  This makes it possible to tell if a new exact-match
     character can be added to that command or if the character requires
     a new `exactn' command.  */
  UCHAR_T *pending_exact = 0;

  /* Address of start of the most recently finished expression.
     This tells, e.g., postfix * where to find the start of its
     operand.  Reset at the beginning of groups and alternatives.  */
  UCHAR_T *laststart = 0;

  /* Address of beginning of regexp, or inside of last group.  */
  UCHAR_T *begalt;

  /* Address of the place where a forward jump should go to the end of
     the containing expression.  Each alternative of an `or' -- except the
     last -- ends with a forward jump of this sort.  */
  UCHAR_T *fixup_alt_jump = 0;

  /* Counts open-groups as they are encountered.  Remembered for the
     matching close-group on the compile stack, so the same register
     number is put in the stop_memory as the start_memory.  */
  regnum_t regnum = 0;

#ifdef WCHAR
  /* Initialize the wchar_t PATTERN and offset_buffer.  */
  p = pend = pattern = TALLOC(csize + 1, CHAR_T);
  mbs_offset = TALLOC(csize + 1, int);
  is_binary = TALLOC(csize + 1, char);
  if (pattern == NULL || mbs_offset == NULL || is_binary == NULL)
    {
      free(pattern);
      free(mbs_offset);
      free(is_binary);
      return REG_ESPACE;
    }
  pattern[csize] = L'\0';	/* sentinel */
  size = convert_mbs_to_wcs(pattern, cpattern, csize, mbs_offset, is_binary);
  pend = p + size;
  if (size < 0)
    {
      free(pattern);
      free(mbs_offset);
      free(is_binary);
      return REG_BADPAT;
    }
#endif

#ifdef DEBUG
  DEBUG_PRINT1 ("\nCompiling pattern: ");
  if (debug)
    {
      unsigned debug_count;

      for (debug_count = 0; debug_count < size; debug_count++)
        PUT_CHAR (pattern[debug_count]);
      putchar ('\n');
    }
#endif /* DEBUG */

  /* Initialize the compile stack.  */
  compile_stack.stack = TALLOC (INIT_COMPILE_STACK_SIZE, compile_stack_elt_t);
  if (compile_stack.stack == NULL)
    {
#ifdef WCHAR
      free(pattern);
      free(mbs_offset);
      free(is_binary);
#endif
      return REG_ESPACE;
    }

  compile_stack.size = INIT_COMPILE_STACK_SIZE;
  compile_stack.avail = 0;

  /* Initialize the pattern buffer.  */
  bufp->syntax = syntax;
  bufp->fastmap_accurate = 0;
  bufp->not_bol = bufp->not_eol = 0;

  /* Set `used' to zero, so that if we return an error, the pattern
     printer (for debugging) will think there's no pattern.  We reset it
     at the end.  */
  bufp->used = 0;

  /* Always count groups, whether or not bufp->no_sub is set.  */
  bufp->re_nsub = 0;

#if !defined emacs && !defined SYNTAX_TABLE
  /* Initialize the syntax table.  */
   init_syntax_once ();
#endif

  if (bufp->allocated == 0)
    {
      if (bufp->buffer)
	{ /* If zero allocated, but buffer is non-null, try to realloc
             enough space.  This loses if buffer's address is bogus, but
             that is the user's responsibility.  */
#ifdef WCHAR
	  /* Free bufp->buffer and allocate an array for wchar_t pattern
	     buffer.  */
          free(bufp->buffer);
          COMPILED_BUFFER_VAR = TALLOC (INIT_BUF_SIZE/sizeof(UCHAR_T),
					UCHAR_T);
#else
          RETALLOC (COMPILED_BUFFER_VAR, INIT_BUF_SIZE, UCHAR_T);
#endif /* WCHAR */
        }
      else
        { /* Caller did not allocate a buffer.  Do it for them.  */
          COMPILED_BUFFER_VAR = TALLOC (INIT_BUF_SIZE / sizeof(UCHAR_T),
					UCHAR_T);
        }

      if (!COMPILED_BUFFER_VAR) FREE_STACK_RETURN (REG_ESPACE);
#ifdef WCHAR
      bufp->buffer = (char*)COMPILED_BUFFER_VAR;
#endif /* WCHAR */
      bufp->allocated = INIT_BUF_SIZE;
    }
#ifdef WCHAR
  else
    COMPILED_BUFFER_VAR = (UCHAR_T*) bufp->buffer;
#endif

  begalt = b = COMPILED_BUFFER_VAR;

  /* Loop through the uncompiled pattern until we're at the end.  */
  while (p != pend)
    {
      PATFETCH (c);

      switch (c)
        {
        case '^':
          {
            if (   /* If at start of pattern, it's an operator.  */
                   p == pattern + 1
                   /* If context independent, it's an operator.  */
                || syntax & RE_CONTEXT_INDEP_ANCHORS
                   /* Otherwise, depends on what's come before.  */
                || PREFIX(at_begline_loc_p) (pattern, p, syntax))
              BUF_PUSH (begline);
            else
              goto normal_char;
          }
          break;


        case '$':
          {
            if (   /* If at end of pattern, it's an operator.  */
                   p == pend
                   /* If context independent, it's an operator.  */
                || syntax & RE_CONTEXT_INDEP_ANCHORS
                   /* Otherwise, depends on what's next.  */
                || PREFIX(at_endline_loc_p) (p, pend, syntax))
               BUF_PUSH (endline);
             else
               goto normal_char;
           }
           break;


	case '+':
        case '?':
          if ((syntax & RE_BK_PLUS_QM)
              || (syntax & RE_LIMITED_OPS))
            goto normal_char;
        handle_plus:
        case '*':
          /* If there is no previous pattern... */
          if (!laststart)
            {
              if (syntax & RE_CONTEXT_INVALID_OPS)
                FREE_STACK_RETURN (REG_BADRPT);
              else if (!(syntax & RE_CONTEXT_INDEP_OPS))
                goto normal_char;
            }

          {
            /* Are we optimizing this jump?  */
            boolean keep_string_p = false;

            /* 1 means zero (many) matches is allowed.  */
            char zero_times_ok = 0, many_times_ok = 0;

            /* If there is a sequence of repetition chars, collapse it
               down to just one (the right one).  We can't combine
               interval operators with these because of, e.g., `a{2}*',
               which should only match an even number of `a's.  */

            for (;;)
              {
                zero_times_ok |= c != '+';
                many_times_ok |= c != '?';

                if (p == pend)
                  break;

                PATFETCH (c);

                if (c == '*'
                    || (!(syntax & RE_BK_PLUS_QM) && (c == '+' || c == '?')))
                  ;

                else if (syntax & RE_BK_PLUS_QM  &&  c == '\\')
                  {
                    if (p == pend) FREE_STACK_RETURN (REG_EESCAPE);

                    PATFETCH (c1);
                    if (!(c1 == '+' || c1 == '?'))
                      {
                        PATUNFETCH;
                        PATUNFETCH;
                        break;
                      }

                    c = c1;
                  }
                else
                  {
                    PATUNFETCH;
                    break;
                  }

                /* If we get here, we found another repeat character.  */
               }

            /* Star, etc. applied to an empty pattern is equivalent
               to an empty pattern.  */
            if (!laststart)
              break;

            /* Now we know whether or not zero matches is allowed
               and also whether or not two or more matches is allowed.  */
            if (many_times_ok)
              { /* More than one repetition is allowed, so put in at the
                   end a backward relative jump from `b' to before the next
                   jump we're going to put in below (which jumps from
                   laststart to after this jump).

                   But if we are at the `*' in the exact sequence `.*\n',
                   insert an unconditional jump backwards to the .,
                   instead of the beginning of the loop.  This way we only
                   push a failure point once, instead of every time
                   through the loop.  */
                assert (p - 1 > pattern);

                /* Allocate the space for the jump.  */
                GET_BUFFER_SPACE (1 + OFFSET_ADDRESS_SIZE);

                /* We know we are not at the first character of the pattern,
                   because laststart was nonzero.  And we've already
                   incremented `p', by the way, to be the character after
                   the `*'.  Do we have to do something analogous here
                   for null bytes, because of RE_DOT_NOT_NULL?  */
                if (TRANSLATE (*(p - 2)) == TRANSLATE ('.')
		    && zero_times_ok
                    && p < pend && TRANSLATE (*p) == TRANSLATE ('\n')
                    && !(syntax & RE_DOT_NEWLINE))
                  { /* We have .*\n.  */
                    STORE_JUMP (jump, b, laststart);
                    keep_string_p = true;
                  }
                else
                  /* Anything else.  */
                  STORE_JUMP (maybe_pop_jump, b, laststart -
			      (1 + OFFSET_ADDRESS_SIZE));

                /* We've added more stuff to the buffer.  */
                b += 1 + OFFSET_ADDRESS_SIZE;
              }

            /* On failure, jump from laststart to b + 3, which will be the
               end of the buffer after this jump is inserted.  */
	    /* ifdef WCHAR, 'b + 1 + OFFSET_ADDRESS_SIZE' instead of
	       'b + 3'.  */
            GET_BUFFER_SPACE (1 + OFFSET_ADDRESS_SIZE);
            INSERT_JUMP (keep_string_p ? on_failure_keep_string_jump
                                       : on_failure_jump,
                         laststart, b + 1 + OFFSET_ADDRESS_SIZE);
            pending_exact = 0;
            b += 1 + OFFSET_ADDRESS_SIZE;

            if (!zero_times_ok)
              {
                /* At least one repetition is required, so insert a
                   `dummy_failure_jump' before the initial
                   `on_failure_jump' instruction of the loop. This
                   effects a skip over that instruction the first time
                   we hit that loop.  */
                GET_BUFFER_SPACE (1 + OFFSET_ADDRESS_SIZE);
                INSERT_JUMP (dummy_failure_jump, laststart, laststart +
			     2 + 2 * OFFSET_ADDRESS_SIZE);
                b += 1 + OFFSET_ADDRESS_SIZE;
              }
            }
	  break;


	case '.':
          laststart = b;
          BUF_PUSH (anychar);
          break;


        case '[':
          {
            boolean had_char_class = false;
#ifdef WCHAR
	    CHAR_T range_start = 0xffffffff;
#else
	    unsigned int range_start = 0xffffffff;
#endif
            if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

#ifdef WCHAR
	    /* We assume a charset(_not) structure as a wchar_t array.
	       charset[0] = (re_opcode_t) charset(_not)
               charset[1] = l (= length of char_classes)
               charset[2] = m (= length of collating_symbols)
               charset[3] = n (= length of equivalence_classes)
	       charset[4] = o (= length of char_ranges)
	       charset[5] = p (= length of chars)

               charset[6] = char_class (wctype_t)
               charset[6+CHAR_CLASS_SIZE] = char_class (wctype_t)
                         ...
               charset[l+5]  = char_class (wctype_t)

               charset[l+6]  = collating_symbol (wchar_t)
                            ...
               charset[l+m+5]  = collating_symbol (wchar_t)
					ifdef _LIBC we use the index if
					_NL_COLLATE_SYMB_EXTRAMB instead of
					wchar_t string.

               charset[l+m+6]  = equivalence_classes (wchar_t)
                              ...
               charset[l+m+n+5]  = equivalence_classes (wchar_t)
					ifdef _LIBC we use the index in
					_NL_COLLATE_WEIGHT instead of
					wchar_t string.

	       charset[l+m+n+6] = range_start
	       charset[l+m+n+7] = range_end
	                       ...
	       charset[l+m+n+2o+4] = range_start
	       charset[l+m+n+2o+5] = range_end
					ifdef _LIBC we use the value looked up
					in _NL_COLLATE_COLLSEQ instead of
					wchar_t character.

	       charset[l+m+n+2o+6] = char
	                          ...
	       charset[l+m+n+2o+p+5] = char

	     */

	    /* We need at least 6 spaces: the opcode, the length of
               char_classes, the length of collating_symbols, the length of
               equivalence_classes, the length of char_ranges, the length of
               chars.  */
	    GET_BUFFER_SPACE (6);

	    /* Save b as laststart. And We use laststart as the pointer
	       to the first element of the charset here.
	       In other words, laststart[i] indicates charset[i].  */
            laststart = b;

            /* We test `*p == '^' twice, instead of using an if
               statement, so we only need one BUF_PUSH.  */
            BUF_PUSH (*p == '^' ? charset_not : charset);
            if (*p == '^')
              p++;

            /* Push the length of char_classes, the length of
               collating_symbols, the length of equivalence_classes, the
               length of char_ranges and the length of chars.  */
            BUF_PUSH_3 (0, 0, 0);
            BUF_PUSH_2 (0, 0);

            /* Remember the first position in the bracket expression.  */
            p1 = p;

            /* charset_not matches newline according to a syntax bit.  */
            if ((re_opcode_t) b[-6] == charset_not
                && (syntax & RE_HAT_LISTS_NOT_NEWLINE))
	      {
		BUF_PUSH('\n');
		laststart[5]++; /* Update the length of characters  */
	      }

            /* Read in characters and ranges, setting map bits.  */
            for (;;)
              {
                if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

                PATFETCH (c);

                /* \ might escape characters inside [...] and [^...].  */
                if ((syntax & RE_BACKSLASH_ESCAPE_IN_LISTS) && c == '\\')
                  {
                    if (p == pend) FREE_STACK_RETURN (REG_EESCAPE);

                    PATFETCH (c1);
		    BUF_PUSH(c1);
		    laststart[5]++; /* Update the length of chars  */
		    range_start = c1;
                    continue;
                  }

                /* Could be the end of the bracket expression.  If it's
                   not (i.e., when the bracket expression is `[]' so
                   far), the ']' character bit gets set way below.  */
                if (c == ']' && p != p1 + 1)
                  break;

                /* Look ahead to see if it's a range when the last thing
                   was a character class.  */
                if (had_char_class && c == '-' && *p != ']')
                  FREE_STACK_RETURN (REG_ERANGE);

                /* Look ahead to see if it's a range when the last thing
                   was a character: if this is a hyphen not at the
                   beginning or the end of a list, then it's the range
                   operator.  */
                if (c == '-'
                    && !(p - 2 >= pattern && p[-2] == '[')
                    && !(p - 3 >= pattern && p[-3] == '[' && p[-2] == '^')
                    && *p != ']')
                  {
                    reg_errcode_t ret;
		    /* Allocate the space for range_start and range_end.  */
		    GET_BUFFER_SPACE (2);
		    /* Update the pointer to indicate end of buffer.  */
                    b += 2;
                    ret = wcs_compile_range (range_start, &p, pend, translate,
                                         syntax, b, laststart);
                    if (ret != REG_NOERROR) FREE_STACK_RETURN (ret);
                    range_start = 0xffffffff;
                  }
                else if (p[0] == '-' && p[1] != ']')
                  { /* This handles ranges made up of characters only.  */
                    reg_errcode_t ret;

		    /* Move past the `-'.  */
                    PATFETCH (c1);
		    /* Allocate the space for range_start and range_end.  */
		    GET_BUFFER_SPACE (2);
		    /* Update the pointer to indicate end of buffer.  */
                    b += 2;
                    ret = wcs_compile_range (c, &p, pend, translate, syntax, b,
                                         laststart);
                    if (ret != REG_NOERROR) FREE_STACK_RETURN (ret);
		    range_start = 0xffffffff;
                  }

                /* See if we're at the beginning of a possible character
                   class.  */
                else if (syntax & RE_CHAR_CLASSES && c == '[' && *p == ':')
                  { /* Leave room for the null.  */
                    char str[CHAR_CLASS_MAX_LENGTH + 1];

                    PATFETCH (c);
                    c1 = 0;

                    /* If pattern is `[[:'.  */
                    if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

                    for (;;)
                      {
                        PATFETCH (c);
                        if ((c == ':' && *p == ']') || p == pend)
                          break;
			if (c1 < CHAR_CLASS_MAX_LENGTH)
			  str[c1++] = c;
			else
			  /* This is in any case an invalid class name.  */
			  str[0] = '\0';
                      }
                    str[c1] = '\0';

                    /* If isn't a word bracketed by `[:' and `:]':
                       undo the ending character, the letters, and leave
                       the leading `:' and `[' (but store them as character).  */
                    if (c == ':' && *p == ']')
                      {
			wctype_t wt;
			uintptr_t alignedp;

			/* Query the character class as wctype_t.  */
			wt = IS_CHAR_CLASS (str);
			if (wt == 0)
			  FREE_STACK_RETURN (REG_ECTYPE);

                        /* Throw away the ] at the end of the character
                           class.  */
                        PATFETCH (c);

                        if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

			/* Allocate the space for character class.  */
                        GET_BUFFER_SPACE(CHAR_CLASS_SIZE);
			/* Update the pointer to indicate end of buffer.  */
                        b += CHAR_CLASS_SIZE;
			/* Move data which follow character classes
			    not to violate the data.  */
                        insert_space(CHAR_CLASS_SIZE,
				     laststart + 6 + laststart[1],
				     b - 1);
			alignedp = ((uintptr_t)(laststart + 6 + laststart[1])
				    + __alignof__(wctype_t) - 1)
			  	    & ~(uintptr_t)(__alignof__(wctype_t) - 1);
			/* Store the character class.  */
                        *((wctype_t*)alignedp) = wt;
                        /* Update length of char_classes */
                        laststart[1] += CHAR_CLASS_SIZE;

                        had_char_class = true;
                      }
                    else
                      {
                        c1++;
                        while (c1--)
                          PATUNFETCH;
                        BUF_PUSH ('[');
                        BUF_PUSH (':');
                        laststart[5] += 2; /* Update the length of characters  */
			range_start = ':';
                        had_char_class = false;
                      }
                  }
                else if (syntax & RE_CHAR_CLASSES && c == '[' && (*p == '='
							  || *p == '.'))
		  {
		    CHAR_T str[128];	/* Should be large enough.  */
		    CHAR_T delim = *p; /* '=' or '.'  */
# ifdef _LIBC
		    uint32_t nrules =
		      _NL_CURRENT_WORD (LC_COLLATE, _NL_COLLATE_NRULES);
# endif
		    PATFETCH (c);
		    c1 = 0;

		    /* If pattern is `[[=' or '[[.'.  */
		    if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

		    for (;;)
		      {
			PATFETCH (c);
			if ((c == delim && *p == ']') || p == pend)
			  break;
			if (c1 < sizeof (str) - 1)
			  str[c1++] = c;
			else
			  /* This is in any case an invalid class name.  */
			  str[0] = '\0';
                      }
		    str[c1] = '\0';

		    if (c == delim && *p == ']' && str[0] != '\0')
		      {
                        unsigned int i, offset;
			/* If we have no collation data we use the default
			   collation in which each character is in a class
			   by itself.  It also means that ASCII is the
			   character set and therefore we cannot have character
			   with more than one byte in the multibyte
			   representation.  */

                        /* If not defined _LIBC, we push the name and
			   `\0' for the sake of matching performance.  */
			int datasize = c1 + 1;

# ifdef _LIBC
			int32_t idx = 0;
			if (nrules == 0)
# endif
			  {
			    if (c1 != 1)
			      FREE_STACK_RETURN (REG_ECOLLATE);
			  }
# ifdef _LIBC
			else
			  {
			    const int32_t *table;
			    const int32_t *weights;
			    const int32_t *extra;
			    const int32_t *indirect;
			    wint_t *cp;

			    /* This #include defines a local function!  */
#  include <locale/weightwc.h>

			    if(delim == '=')
			      {
				/* We push the index for equivalence class.  */
				cp = (wint_t*)str;

				table = (const int32_t *)
				  _NL_CURRENT (LC_COLLATE,
					       _NL_COLLATE_TABLEWC);
				weights = (const int32_t *)
				  _NL_CURRENT (LC_COLLATE,
					       _NL_COLLATE_WEIGHTWC);
				extra = (const int32_t *)
				  _NL_CURRENT (LC_COLLATE,
					       _NL_COLLATE_EXTRAWC);
				indirect = (const int32_t *)
				  _NL_CURRENT (LC_COLLATE,
					       _NL_COLLATE_INDIRECTWC);

				idx = findidx ((const wint_t**)&cp);
				if (idx == 0 || cp < (wint_t*) str + c1)
				  /* This is no valid character.  */
				  FREE_STACK_RETURN (REG_ECOLLATE);

				str[0] = (wchar_t)idx;
			      }
			    else /* delim == '.' */
			      {
				/* We push collation sequence value
				   for collating symbol.  */
				int32_t table_size;
				const int32_t *symb_table;
				const unsigned char *extra;
				int32_t idx;
				int32_t elem;
				int32_t second;
				int32_t hash;
				char char_str[c1];

				/* We have to convert the name to a single-byte
				   string.  This is possible since the names
				   consist of ASCII characters and the internal
				   representation is UCS4.  */
				for (i = 0; i < c1; ++i)
				  char_str[i] = str[i];

				table_size =
				  _NL_CURRENT_WORD (LC_COLLATE,
						    _NL_COLLATE_SYMB_HASH_SIZEMB);
				symb_table = (const int32_t *)
				  _NL_CURRENT (LC_COLLATE,
					       _NL_COLLATE_SYMB_TABLEMB);
				extra = (const unsigned char *)
				  _NL_CURRENT (LC_COLLATE,
					       _NL_COLLATE_SYMB_EXTRAMB);

				/* Locate the character in the hashing table.  */
				hash = elem_hash (char_str, c1);

				idx = 0;
				elem = hash % table_size;
				second = hash % (table_size - 2);
				while (symb_table[2 * elem] != 0)
				  {
				    /* First compare the hashing value.  */
				    if (symb_table[2 * elem] == hash
					&& c1 == extra[symb_table[2 * elem + 1]]
					&& memcmp (char_str,
						   &extra[symb_table[2 * elem + 1]
							 + 1], c1) == 0)
				      {
					/* Yep, this is the entry.  */
					idx = symb_table[2 * elem + 1];
					idx += 1 + extra[idx];
					break;
				      }

				    /* Next entry.  */
				    elem += second;
				  }

				if (symb_table[2 * elem] != 0)
				  {
				    /* Compute the index of the byte sequence
				       in the table.  */
				    idx += 1 + extra[idx];
				    /* Adjust for the alignment.  */
				    idx = (idx + 3) & ~3;

				    str[0] = (wchar_t) idx + 4;
				  }
				else if (symb_table[2 * elem] == 0 && c1 == 1)
				  {
				    /* No valid character.  Match it as a
				       single byte character.  */
				    had_char_class = false;
				    BUF_PUSH(str[0]);
				    /* Update the length of characters  */
				    laststart[5]++;
				    range_start = str[0];

				    /* Throw away the ] at the end of the
				       collating symbol.  */
				    PATFETCH (c);
				    /* exit from the switch block.  */
				    continue;
				  }
				else
				  FREE_STACK_RETURN (REG_ECOLLATE);
			      }
			    datasize = 1;
			  }
# endif
                        /* Throw away the ] at the end of the equivalence
                           class (or collating symbol).  */
                        PATFETCH (c);

			/* Allocate the space for the equivalence class
			   (or collating symbol) (and '\0' if needed).  */
                        GET_BUFFER_SPACE(datasize);
			/* Update the pointer to indicate end of buffer.  */
                        b += datasize;

			if (delim == '=')
			  { /* equivalence class  */
			    /* Calculate the offset of char_ranges,
			       which is next to equivalence_classes.  */
			    offset = laststart[1] + laststart[2]
			      + laststart[3] +6;
			    /* Insert space.  */
			    insert_space(datasize, laststart + offset, b - 1);

			    /* Write the equivalence_class and \0.  */
			    for (i = 0 ; i < datasize ; i++)
			      laststart[offset + i] = str[i];

			    /* Update the length of equivalence_classes.  */
			    laststart[3] += datasize;
			    had_char_class = true;
			  }
			else /* delim == '.' */
			  { /* collating symbol  */
			    /* Calculate the offset of the equivalence_classes,
			       which is next to collating_symbols.  */
			    offset = laststart[1] + laststart[2] + 6;
			    /* Insert space and write the collationg_symbol
			       and \0.  */
			    insert_space(datasize, laststart + offset, b-1);
			    for (i = 0 ; i < datasize ; i++)
			      laststart[offset + i] = str[i];

			    /* In re_match_2_internal if range_start < -1, we
			       assume -range_start is the offset of the
			       collating symbol which is specified as
			       the character of the range start.  So we assign
			       -(laststart[1] + laststart[2] + 6) to
			       range_start.  */
			    range_start = -(laststart[1] + laststart[2] + 6);
			    /* Update the length of collating_symbol.  */
			    laststart[2] += datasize;
			    had_char_class = false;
			  }
		      }
                    else
                      {
                        c1++;
                        while (c1--)
                          PATUNFETCH;
                        BUF_PUSH ('[');
                        BUF_PUSH (delim);
                        laststart[5] += 2; /* Update the length of characters  */
			range_start = delim;
                        had_char_class = false;
                      }
		  }
                else
                  {
                    had_char_class = false;
		    BUF_PUSH(c);
		    laststart[5]++;  /* Update the length of characters  */
		    range_start = c;
                  }
	      }

#else /* BYTE */
            /* Ensure that we have enough space to push a charset: the
               opcode, the length count, and the bitset; 34 bytes in all.  */
	    GET_BUFFER_SPACE (34);

            laststart = b;

            /* We test `*p == '^' twice, instead of using an if
               statement, so we only need one BUF_PUSH.  */
            BUF_PUSH (*p == '^' ? charset_not : charset);
            if (*p == '^')
              p++;

            /* Remember the first position in the bracket expression.  */
            p1 = p;

            /* Push the number of bytes in the bitmap.  */
            BUF_PUSH ((1 << BYTEWIDTH) / BYTEWIDTH);

            /* Clear the whole map.  */
            bzero (b, (1 << BYTEWIDTH) / BYTEWIDTH);

            /* charset_not matches newline according to a syntax bit.  */
            if ((re_opcode_t) b[-2] == charset_not
                && (syntax & RE_HAT_LISTS_NOT_NEWLINE))
              SET_LIST_BIT ('\n');

            /* Read in characters and ranges, setting map bits.  */
            for (;;)
              {
                if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

                PATFETCH (c);

                /* \ might escape characters inside [...] and [^...].  */
                if ((syntax & RE_BACKSLASH_ESCAPE_IN_LISTS) && c == '\\')
                  {
                    if (p == pend) FREE_STACK_RETURN (REG_EESCAPE);

                    PATFETCH (c1);
                    SET_LIST_BIT (c1);
		    range_start = c1;
                    continue;
                  }

                /* Could be the end of the bracket expression.  If it's
                   not (i.e., when the bracket expression is `[]' so
                   far), the ']' character bit gets set way below.  */
                if (c == ']' && p != p1 + 1)
                  break;

                /* Look ahead to see if it's a range when the last thing
                   was a character class.  */
                if (had_char_class && c == '-' && *p != ']')
                  FREE_STACK_RETURN (REG_ERANGE);

                /* Look ahead to see if it's a range when the last thing
                   was a character: if this is a hyphen not at the
                   beginning or the end of a list, then it's the range
                   operator.  */
                if (c == '-'
                    && !(p - 2 >= pattern && p[-2] == '[')
                    && !(p - 3 >= pattern && p[-3] == '[' && p[-2] == '^')
                    && *p != ']')
                  {
                    reg_errcode_t ret
                      = byte_compile_range (range_start, &p, pend, translate,
					    syntax, b);
                    if (ret != REG_NOERROR) FREE_STACK_RETURN (ret);
		    range_start = 0xffffffff;
                  }

                else if (p[0] == '-' && p[1] != ']')
                  { /* This handles ranges made up of characters only.  */
                    reg_errcode_t ret;

		    /* Move past the `-'.  */
                    PATFETCH (c1);

                    ret = byte_compile_range (c, &p, pend, translate, syntax, b);
                    if (ret != REG_NOERROR) FREE_STACK_RETURN (ret);
		    range_start = 0xffffffff;
                  }

                /* See if we're at the beginning of a possible character
                   class.  */

                else if (syntax & RE_CHAR_CLASSES && c == '[' && *p == ':')
                  { /* Leave room for the null.  */
                    char str[CHAR_CLASS_MAX_LENGTH + 1];

                    PATFETCH (c);
                    c1 = 0;

                    /* If pattern is `[[:'.  */
                    if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

                    for (;;)
                      {
                        PATFETCH (c);
                        if ((c == ':' && *p == ']') || p == pend)
                          break;
			if (c1 < CHAR_CLASS_MAX_LENGTH)
			  str[c1++] = c;
			else
			  /* This is in any case an invalid class name.  */
			  str[0] = '\0';
                      }
                    str[c1] = '\0';

                    /* If isn't a word bracketed by `[:' and `:]':
                       undo the ending character, the letters, and leave
                       the leading `:' and `[' (but set bits for them).  */
                    if (c == ':' && *p == ']')
                      {
# if defined _LIBC || WIDE_CHAR_SUPPORT
                        boolean is_lower = STREQ (str, "lower");
                        boolean is_upper = STREQ (str, "upper");
			wctype_t wt;
                        int ch;

			wt = IS_CHAR_CLASS (str);
			if (wt == 0)
			  FREE_STACK_RETURN (REG_ECTYPE);

                        /* Throw away the ] at the end of the character
                           class.  */
                        PATFETCH (c);

                        if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

                        for (ch = 0; ch < 1 << BYTEWIDTH; ++ch)
			  {
#  ifdef _LIBC
			    if (__iswctype (__btowc (ch), wt))
			      SET_LIST_BIT (ch);
#  else
			    if (iswctype (btowc (ch), wt))
			      SET_LIST_BIT (ch);
#  endif

			    if (translate && (is_upper || is_lower)
				&& (ISUPPER (ch) || ISLOWER (ch)))
			      SET_LIST_BIT (ch);
			  }

                        had_char_class = true;
# else
                        int ch;
                        boolean is_alnum = STREQ (str, "alnum");
                        boolean is_alpha = STREQ (str, "alpha");
                        boolean is_blank = STREQ (str, "blank");
                        boolean is_cntrl = STREQ (str, "cntrl");
                        boolean is_digit = STREQ (str, "digit");
                        boolean is_graph = STREQ (str, "graph");
                        boolean is_lower = STREQ (str, "lower");
                        boolean is_print = STREQ (str, "print");
                        boolean is_punct = STREQ (str, "punct");
                        boolean is_space = STREQ (str, "space");
                        boolean is_upper = STREQ (str, "upper");
                        boolean is_xdigit = STREQ (str, "xdigit");

                        if (!IS_CHAR_CLASS (str))
			  FREE_STACK_RETURN (REG_ECTYPE);

                        /* Throw away the ] at the end of the character
                           class.  */
                        PATFETCH (c);

                        if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

                        for (ch = 0; ch < 1 << BYTEWIDTH; ch++)
                          {
			    /* This was split into 3 if's to
			       avoid an arbitrary limit in some compiler.  */
                            if (   (is_alnum  && ISALNUM (ch))
                                || (is_alpha  && ISALPHA (ch))
                                || (is_blank  && ISBLANK (ch))
                                || (is_cntrl  && ISCNTRL (ch)))
			      SET_LIST_BIT (ch);
			    if (   (is_digit  && ISDIGIT (ch))
                                || (is_graph  && ISGRAPH (ch))
                                || (is_lower  && ISLOWER (ch))
                                || (is_print  && ISPRINT (ch)))
			      SET_LIST_BIT (ch);
			    if (   (is_punct  && ISPUNCT (ch))
                                || (is_space  && ISSPACE (ch))
                                || (is_upper  && ISUPPER (ch))
                                || (is_xdigit && ISXDIGIT (ch)))
			      SET_LIST_BIT (ch);
			    if (   translate && (is_upper || is_lower)
				&& (ISUPPER (ch) || ISLOWER (ch)))
			      SET_LIST_BIT (ch);
                          }
                        had_char_class = true;
# endif	/* libc || wctype.h */
                      }
                    else
                      {
                        c1++;
                        while (c1--)
                          PATUNFETCH;
                        SET_LIST_BIT ('[');
                        SET_LIST_BIT (':');
			range_start = ':';
                        had_char_class = false;
                      }
                  }
                else if (syntax & RE_CHAR_CLASSES && c == '[' && *p == '=')
		  {
		    unsigned char str[MB_LEN_MAX + 1];
# ifdef _LIBC
		    uint32_t nrules =
		      _NL_CURRENT_WORD (LC_COLLATE, _NL_COLLATE_NRULES);
# endif

		    PATFETCH (c);
		    c1 = 0;

		    /* If pattern is `[[='.  */
		    if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

		    for (;;)
		      {
			PATFETCH (c);
			if ((c == '=' && *p == ']') || p == pend)
			  break;
			if (c1 < MB_LEN_MAX)
			  str[c1++] = c;
			else
			  /* This is in any case an invalid class name.  */
			  str[0] = '\0';
                      }
		    str[c1] = '\0';

		    if (c == '=' && *p == ']' && str[0] != '\0')
		      {
			/* If we have no collation data we use the default
			   collation in which each character is in a class
			   by itself.  It also means that ASCII is the
			   character set and therefore we cannot have character
			   with more than one byte in the multibyte
			   representation.  */
# ifdef _LIBC
			if (nrules == 0)
# endif
			  {
			    if (c1 != 1)
			      FREE_STACK_RETURN (REG_ECOLLATE);

			    /* Throw away the ] at the end of the equivalence
			       class.  */
			    PATFETCH (c);

			    /* Set the bit for the character.  */
			    SET_LIST_BIT (str[0]);
			  }
# ifdef _LIBC
			else
			  {
			    /* Try to match the byte sequence in `str' against
			       those known to the collate implementation.
			       First find out whether the bytes in `str' are
			       actually from exactly one character.  */
			    const int32_t *table;
			    const unsigned char *weights;
			    const unsigned char *extra;
			    const int32_t *indirect;
			    int32_t idx;
			    const unsigned char *cp = str;
			    int ch;

			    /* This #include defines a local function!  */
#  include <locale/weight.h>

			    table = (const int32_t *)
			      _NL_CURRENT (LC_COLLATE, _NL_COLLATE_TABLEMB);
			    weights = (const unsigned char *)
			      _NL_CURRENT (LC_COLLATE, _NL_COLLATE_WEIGHTMB);
			    extra = (const unsigned char *)
			      _NL_CURRENT (LC_COLLATE, _NL_COLLATE_EXTRAMB);
			    indirect = (const int32_t *)
			      _NL_CURRENT (LC_COLLATE, _NL_COLLATE_INDIRECTMB);

			    idx = findidx (&cp);
			    if (idx == 0 || cp < str + c1)
			      /* This is no valid character.  */
			      FREE_STACK_RETURN (REG_ECOLLATE);

			    /* Throw away the ] at the end of the equivalence
			       class.  */
			    PATFETCH (c);

			    /* Now we have to go throught the whole table
			       and find all characters which have the same
			       first level weight.

			       XXX Note that this is not entirely correct.
			       we would have to match multibyte sequences
			       but this is not possible with the current
			       implementation.  */
			    for (ch = 1; ch < 256; ++ch)
			      /* XXX This test would have to be changed if we
				 would allow matching multibyte sequences.  */
			      if (table[ch] > 0)
				{
				  int32_t idx2 = table[ch];
				  size_t len = weights[idx2];

				  /* Test whether the lenghts match.  */
				  if (weights[idx] == len)
				    {
				      /* They do.  New compare the bytes of
					 the weight.  */
				      size_t cnt = 0;

				      while (cnt < len
					     && (weights[idx + 1 + cnt]
						 == weights[idx2 + 1 + cnt]))
					++cnt;

				      if (cnt == len)
					/* They match.  Mark the character as
					   acceptable.  */
					SET_LIST_BIT (ch);
				    }
				}
			  }
# endif
			had_char_class = true;
		      }
                    else
                      {
                        c1++;
                        while (c1--)
                          PATUNFETCH;
                        SET_LIST_BIT ('[');
                        SET_LIST_BIT ('=');
			range_start = '=';
                        had_char_class = false;
                      }
		  }
                else if (syntax & RE_CHAR_CLASSES && c == '[' && *p == '.')
		  {
		    unsigned char str[128];	/* Should be large enough.  */
# ifdef _LIBC
		    uint32_t nrules =
		      _NL_CURRENT_WORD (LC_COLLATE, _NL_COLLATE_NRULES);
# endif

		    PATFETCH (c);
		    c1 = 0;

		    /* If pattern is `[[.'.  */
		    if (p == pend) FREE_STACK_RETURN (REG_EBRACK);

		    for (;;)
		      {
			PATFETCH (c);
			if ((c == '.' && *p == ']') || p == pend)
			  break;
			if (c1 < sizeof (str))
			  str[c1++] = c;
			else
			  /* This is in any case an invalid class name.  */
			  str[0] = '\0';
                      }
		    str[c1] = '\0';

		    if (c == '.' && *p == ']' && str[0] != '\0')
		      {
			/* If we have no collation data we use the default
			   collation in which each character is the name
			   for its own class which contains only the one
			   character.  It also means that ASCII is the
			   character set and therefore we cannot have character
			   with more than one byte in the multibyte
			   representation.  */
# ifdef _LIBC
			if (nrules == 0)
# endif
			  {
			    if (c1 != 1)
			      FREE_STACK_RETURN (REG_ECOLLATE);

			    /* Throw away the ] at the end of the equivalence
			       class.  */
			    PATFETCH (c);

			    /* Set the bit for the character.  */
			    SET_LIST_BIT (str[0]);
			    range_start = ((const unsigned char *) str)[0];
			  }
# ifdef _LIBC
			else
			  {
			    /* Try to match the byte sequence in `str' against
			       those known to the collate implementation.
			       First find out whether the bytes in `str' are
			       actually from exactly one character.  */
			    int32_t table_size;
			    const int32_t *symb_table;
			    const unsigned char *extra;
			    int32_t idx;
			    int32_t elem;
			    int32_t second;
			    int32_t hash;

			    table_size =
			      _NL_CURRENT_WORD (LC_COLLATE,
						_NL_COLLATE_SYMB_HASH_SIZEMB);
			    symb_table = (const int32_t *)
			      _NL_CURRENT (LC_COLLATE,
					   _NL_COLLATE_SYMB_TABLEMB);
			    extra = (const unsigned char *)
			      _NL_CURRENT (LC_COLLATE,
					   _NL_COLLATE_SYMB_EXTRAMB);

			    /* Locate the character in the hashing table.  */
			    hash = elem_hash (str, c1);

			    idx = 0;
			    elem = hash % table_size;
			    second = hash % (table_size - 2);
			    while (symb_table[2 * elem] != 0)
			      {
				/* First compare the hashing value.  */
				if (symb_table[2 * elem] == hash
				    && c1 == extra[symb_table[2 * elem + 1]]
				    && memcmp (str,
					       &extra[symb_table[2 * elem + 1]
						     + 1],
					       c1) == 0)
				  {
				    /* Yep, this is the entry.  */
				    idx = symb_table[2 * elem + 1];
				    idx += 1 + extra[idx];
				    break;
				  }

				/* Next entry.  */
				elem += second;
			      }

			    if (symb_table[2 * elem] == 0)
			      /* This is no valid character.  */
			      FREE_STACK_RETURN (REG_ECOLLATE);

			    /* Throw away the ] at the end of the equivalence
			       class.  */
			    PATFETCH (c);

			    /* Now add the multibyte character(s) we found
			       to the accept list.

			       XXX Note that this is not entirely correct.
			       we would have to match multibyte sequences
			       but this is not possible with the current
			       implementation.  Also, we have to match
			       collating symbols, which expand to more than
			       one file, as a whole and not allow the
			       individual bytes.  */
			    c1 = extra[idx++];
			    if (c1 == 1)
			      range_start = extra[idx];
			    while (c1-- > 0)
			      {
				SET_LIST_BIT (extra[idx]);
				++idx;
			      }
			  }
# endif
			had_char_class = false;
		      }
                    else
                      {
                        c1++;
                        while (c1--)
                          PATUNFETCH;
                        SET_LIST_BIT ('[');
                        SET_LIST_BIT ('.');
			range_start = '.';
                        had_char_class = false;
                      }
		  }
                else
                  {
                    had_char_class = false;
                    SET_LIST_BIT (c);
		    range_start = c;
                  }
              }

            /* Discard any (non)matching list bytes that are all 0 at the
               end of the map.  Decrease the map-length byte too.  */
            while ((int) b[-1] > 0 && b[b[-1] - 1] == 0)
              b[-1]--;
            b += b[-1];
#endif /* WCHAR */
          }
          break;


	case '(':
          if (syntax & RE_NO_BK_PARENS)
            goto handle_open;
          else
            goto normal_char;


        case ')':
          if (syntax & RE_NO_BK_PARENS)
            goto handle_close;
          else
            goto normal_char;


        case '\n':
          if (syntax & RE_NEWLINE_ALT)
            goto handle_alt;
          else
            goto normal_char;


	case '|':
          if (syntax & RE_NO_BK_VBAR)
            goto handle_alt;
          else
            goto normal_char;


        case '{':
           if (syntax & RE_INTERVALS && syntax & RE_NO_BK_BRACES)
             goto handle_interval;
           else
             goto normal_char;


        case '\\':
          if (p == pend) FREE_STACK_RETURN (REG_EESCAPE);

          /* Do not translate the character after the \, so that we can
             distinguish, e.g., \B from \b, even if we normally would
             translate, e.g., B to b.  */
          PATFETCH_RAW (c);

          switch (c)
            {
            case '(':
              if (syntax & RE_NO_BK_PARENS)
                goto normal_backslash;

            handle_open:
              bufp->re_nsub++;
              regnum++;

              if (COMPILE_STACK_FULL)
                {
                  RETALLOC (compile_stack.stack, compile_stack.size << 1,
                            compile_stack_elt_t);
                  if (compile_stack.stack == NULL) return REG_ESPACE;

                  compile_stack.size <<= 1;
                }

              /* These are the values to restore when we hit end of this
                 group.  They are all relative offsets, so that if the
                 whole pattern moves because of realloc, they will still
                 be valid.  */
              COMPILE_STACK_TOP.begalt_offset = begalt - COMPILED_BUFFER_VAR;
              COMPILE_STACK_TOP.fixup_alt_jump
                = fixup_alt_jump ? fixup_alt_jump - COMPILED_BUFFER_VAR + 1 : 0;
              COMPILE_STACK_TOP.laststart_offset = b - COMPILED_BUFFER_VAR;
              COMPILE_STACK_TOP.regnum = regnum;

              /* We will eventually replace the 0 with the number of
                 groups inner to this one.  But do not push a
                 start_memory for groups beyond the last one we can
                 represent in the compiled pattern.  */
              if (regnum <= MAX_REGNUM)
                {
                  COMPILE_STACK_TOP.inner_group_offset = b
		    - COMPILED_BUFFER_VAR + 2;
                  BUF_PUSH_3 (start_memory, regnum, 0);
                }

              compile_stack.avail++;

              fixup_alt_jump = 0;
              laststart = 0;
              begalt = b;
	      /* If we've reached MAX_REGNUM groups, then this open
		 won't actually generate any code, so we'll have to
		 clear pending_exact explicitly.  */
	      pending_exact = 0;
              break;


            case ')':
              if (syntax & RE_NO_BK_PARENS) goto normal_backslash;

              if (COMPILE_STACK_EMPTY)
		{
		  if (syntax & RE_UNMATCHED_RIGHT_PAREN_ORD)
		    goto normal_backslash;
		  else
		    FREE_STACK_RETURN (REG_ERPAREN);
		}

            handle_close:
              if (fixup_alt_jump)
                { /* Push a dummy failure point at the end of the
                     alternative for a possible future
                     `pop_failure_jump' to pop.  See comments at
                     `push_dummy_failure' in `re_match_2'.  */
                  BUF_PUSH (push_dummy_failure);

                  /* We allocated space for this jump when we assigned
                     to `fixup_alt_jump', in the `handle_alt' case below.  */
                  STORE_JUMP (jump_past_alt, fixup_alt_jump, b - 1);
                }

              /* See similar code for backslashed left paren above.  */
              if (COMPILE_STACK_EMPTY)
		{
		  if (syntax & RE_UNMATCHED_RIGHT_PAREN_ORD)
		    goto normal_char;
		  else
		    FREE_STACK_RETURN (REG_ERPAREN);
		}

              /* Since we just checked for an empty stack above, this
                 ``can't happen''.  */
              assert (compile_stack.avail != 0);
              {
                /* We don't just want to restore into `regnum', because
                   later groups should continue to be numbered higher,
                   as in `(ab)c(de)' -- the second group is #2.  */
                regnum_t this_group_regnum;

                compile_stack.avail--;
                begalt = COMPILED_BUFFER_VAR + COMPILE_STACK_TOP.begalt_offset;
                fixup_alt_jump
                  = COMPILE_STACK_TOP.fixup_alt_jump
                    ? COMPILED_BUFFER_VAR + COMPILE_STACK_TOP.fixup_alt_jump - 1
                    : 0;
                laststart = COMPILED_BUFFER_VAR + COMPILE_STACK_TOP.laststart_offset;
                this_group_regnum = COMPILE_STACK_TOP.regnum;
		/* If we've reached MAX_REGNUM groups, then this open
		   won't actually generate any code, so we'll have to
		   clear pending_exact explicitly.  */
		pending_exact = 0;

                /* We're at the end of the group, so now we know how many
                   groups were inside this one.  */
                if (this_group_regnum <= MAX_REGNUM)
                  {
		    UCHAR_T *inner_group_loc
                      = COMPILED_BUFFER_VAR + COMPILE_STACK_TOP.inner_group_offset;

                    *inner_group_loc = regnum - this_group_regnum;
                    BUF_PUSH_3 (stop_memory, this_group_regnum,
                                regnum - this_group_regnum);
                  }
              }
              break;


            case '|':					/* `\|'.  */
              if (syntax & RE_LIMITED_OPS || syntax & RE_NO_BK_VBAR)
                goto normal_backslash;
            handle_alt:
              if (syntax & RE_LIMITED_OPS)
                goto normal_char;

              /* Insert before the previous alternative a jump which
                 jumps to this alternative if the former fails.  */
              GET_BUFFER_SPACE (1 + OFFSET_ADDRESS_SIZE);
              INSERT_JUMP (on_failure_jump, begalt,
			   b + 2 + 2 * OFFSET_ADDRESS_SIZE);
              pending_exact = 0;
              b += 1 + OFFSET_ADDRESS_SIZE;

              /* The alternative before this one has a jump after it
                 which gets executed if it gets matched.  Adjust that
                 jump so it will jump to this alternative's analogous
                 jump (put in below, which in turn will jump to the next
                 (if any) alternative's such jump, etc.).  The last such
                 jump jumps to the correct final destination.  A picture:
                          _____ _____
                          |   | |   |
                          |   v |   v
                         a | b   | c

                 If we are at `b', then fixup_alt_jump right now points to a
                 three-byte space after `a'.  We'll put in the jump, set
                 fixup_alt_jump to right after `b', and leave behind three
                 bytes which we'll fill in when we get to after `c'.  */

              if (fixup_alt_jump)
                STORE_JUMP (jump_past_alt, fixup_alt_jump, b);

              /* Mark and leave space for a jump after this alternative,
                 to be filled in later either by next alternative or
                 when know we're at the end of a series of alternatives.  */
              fixup_alt_jump = b;
              GET_BUFFER_SPACE (1 + OFFSET_ADDRESS_SIZE);
              b += 1 + OFFSET_ADDRESS_SIZE;

              laststart = 0;
              begalt = b;
              break;


            case '{':
              /* If \{ is a literal.  */
              if (!(syntax & RE_INTERVALS)
                     /* If we're at `\{' and it's not the open-interval
                        operator.  */
		  || (syntax & RE_NO_BK_BRACES))
                goto normal_backslash;

            handle_interval:
              {
                /* If got here, then the syntax allows intervals.  */

                /* At least (most) this many matches must be made.  */
                int lower_bound = -1, upper_bound = -1;

		/* Place in the uncompiled pattern (i.e., just after
		   the '{') to go back to if the interval is invalid.  */
		const CHAR_T *beg_interval = p;

                if (p == pend)
		  goto invalid_interval;

                GET_UNSIGNED_NUMBER (lower_bound);

                if (c == ',')
                  {
                    GET_UNSIGNED_NUMBER (upper_bound);
		    if (upper_bound < 0)
		      upper_bound = RE_DUP_MAX;
                  }
                else
                  /* Interval such as `{1}' => match exactly once. */
                  upper_bound = lower_bound;

                if (! (0 <= lower_bound && lower_bound <= upper_bound))
		  goto invalid_interval;

                if (!(syntax & RE_NO_BK_BRACES))
                  {
		    if (c != '\\' || p == pend)
		      goto invalid_interval;
                    PATFETCH (c);
                  }

                if (c != '}')
		  goto invalid_interval;

                /* If it's invalid to have no preceding re.  */
                if (!laststart)
                  {
		    if (syntax & RE_CONTEXT_INVALID_OPS
			&& !(syntax & RE_INVALID_INTERVAL_ORD))
                      FREE_STACK_RETURN (REG_BADRPT);
                    else if (syntax & RE_CONTEXT_INDEP_OPS)
                      laststart = b;
                    else
                      goto unfetch_interval;
                  }

                /* We just parsed a valid interval.  */

                if (RE_DUP_MAX < upper_bound)
		  FREE_STACK_RETURN (REG_BADBR);

                /* If the upper bound is zero, don't want to succeed at
                   all; jump from `laststart' to `b + 3', which will be
		   the end of the buffer after we insert the jump.  */
		/* ifdef WCHAR, 'b + 1 + OFFSET_ADDRESS_SIZE'
		   instead of 'b + 3'.  */
                 if (upper_bound == 0)
                   {
                     GET_BUFFER_SPACE (1 + OFFSET_ADDRESS_SIZE);
                     INSERT_JUMP (jump, laststart, b + 1
				  + OFFSET_ADDRESS_SIZE);
                     b += 1 + OFFSET_ADDRESS_SIZE;
                   }

                 /* Otherwise, we have a nontrivial interval.  When
                    we're all done, the pattern will look like:
                      set_number_at <jump count> <upper bound>
                      set_number_at <succeed_n count> <lower bound>
                      succeed_n <after jump addr> <succeed_n count>
                      <body of loop>
                      jump_n <succeed_n addr> <jump count>
                    (The upper bound and `jump_n' are omitted if
                    `upper_bound' is 1, though.)  */
                 else
                   { /* If the upper bound is > 1, we need to insert
                        more at the end of the loop.  */
                     unsigned nbytes = 2 + 4 * OFFSET_ADDRESS_SIZE +
		       (upper_bound > 1) * (2 + 4 * OFFSET_ADDRESS_SIZE);

                     GET_BUFFER_SPACE (nbytes);

                     /* Initialize lower bound of the `succeed_n', even
                        though it will be set during matching by its
                        attendant `set_number_at' (inserted next),
                        because `re_compile_fastmap' needs to know.
                        Jump to the `jump_n' we might insert below.  */
                     INSERT_JUMP2 (succeed_n, laststart,
                                   b + 1 + 2 * OFFSET_ADDRESS_SIZE
				   + (upper_bound > 1) * (1 + 2 * OFFSET_ADDRESS_SIZE)
				   , lower_bound);
                     b += 1 + 2 * OFFSET_ADDRESS_SIZE;

                     /* Code to initialize the lower bound.  Insert
                        before the `succeed_n'.  The `5' is the last two
                        bytes of this `set_number_at', plus 3 bytes of
                        the following `succeed_n'.  */
		     /* ifdef WCHAR, The '1+2*OFFSET_ADDRESS_SIZE'
			is the 'set_number_at', plus '1+OFFSET_ADDRESS_SIZE'
			of the following `succeed_n'.  */
                     PREFIX(insert_op2) (set_number_at, laststart, 1
				 + 2 * OFFSET_ADDRESS_SIZE, lower_bound, b);
                     b += 1 + 2 * OFFSET_ADDRESS_SIZE;

                     if (upper_bound > 1)
                       { /* More than one repetition is allowed, so
                            append a backward jump to the `succeed_n'
                            that starts this interval.

                            When we've reached this during matching,
                            we'll have matched the interval once, so
                            jump back only `upper_bound - 1' times.  */
                         STORE_JUMP2 (jump_n, b, laststart
				      + 2 * OFFSET_ADDRESS_SIZE + 1,
                                      upper_bound - 1);
                         b += 1 + 2 * OFFSET_ADDRESS_SIZE;

                         /* The location we want to set is the second
                            parameter of the `jump_n'; that is `b-2' as
                            an absolute address.  `laststart' will be
                            the `set_number_at' we're about to insert;
                            `laststart+3' the number to set, the source
                            for the relative address.  But we are
                            inserting into the middle of the pattern --
                            so everything is getting moved up by 5.
                            Conclusion: (b - 2) - (laststart + 3) + 5,
                            i.e., b - laststart.

                            We insert this at the beginning of the loop
                            so that if we fail during matching, we'll
                            reinitialize the bounds.  */
                         PREFIX(insert_op2) (set_number_at, laststart,
					     b - laststart,
					     upper_bound - 1, b);
                         b += 1 + 2 * OFFSET_ADDRESS_SIZE;
                       }
                   }
                pending_exact = 0;
		break;

	      invalid_interval:
		if (!(syntax & RE_INVALID_INTERVAL_ORD))
		  FREE_STACK_RETURN (p == pend ? REG_EBRACE : REG_BADBR);
	      unfetch_interval:
		/* Match the characters as literals.  */
		p = beg_interval;
		c = '{';
		if (syntax & RE_NO_BK_BRACES)
		  goto normal_char;
		else
		  goto normal_backslash;
	      }

#ifdef emacs
            /* There is no way to specify the before_dot and after_dot
               operators.  rms says this is ok.  --karl  */
            case '=':
              BUF_PUSH (at_dot);
              break;

            case 's':
              laststart = b;
              PATFETCH (c);
              BUF_PUSH_2 (syntaxspec, syntax_spec_code[c]);
              break;

            case 'S':
              laststart = b;
              PATFETCH (c);
              BUF_PUSH_2 (notsyntaxspec, syntax_spec_code[c]);
              break;
#endif /* emacs */


            case 'w':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              laststart = b;
              BUF_PUSH (wordchar);
              break;


            case 'W':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              laststart = b;
              BUF_PUSH (notwordchar);
              break;


            case '<':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              BUF_PUSH (wordbeg);
              break;

            case '>':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              BUF_PUSH (wordend);
              break;

            case 'b':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              BUF_PUSH (wordbound);
              break;

            case 'B':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              BUF_PUSH (notwordbound);
              break;

            case '`':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              BUF_PUSH (begbuf);
              break;

            case '\'':
	      if (syntax & RE_NO_GNU_OPS)
		goto normal_char;
              BUF_PUSH (endbuf);
              break;

            case '1': case '2': case '3': case '4': case '5':
            case '6': case '7': case '8': case '9':
              if (syntax & RE_NO_BK_REFS)
                goto normal_char;

              c1 = c - '0';

              if (c1 > regnum)
                FREE_STACK_RETURN (REG_ESUBREG);

              /* Can't back reference to a subexpression if inside of it.  */
              if (group_in_compile_stack (compile_stack, (regnum_t) c1))
                goto normal_char;

              laststart = b;
              BUF_PUSH_2 (duplicate, c1);
              break;


            case '+':
            case '?':
              if (syntax & RE_BK_PLUS_QM)
                goto handle_plus;
              else
                goto normal_backslash;

            default:
            normal_backslash:
              /* You might think it would be useful for \ to mean
                 not to translate; but if we don't translate it
                 it will never match anything.  */
              c = TRANSLATE (c);
              goto normal_char;
            }
          break;


	default:
        /* Expects the character in `c'.  */
	normal_char:
	      /* If no exactn currently being built.  */
          if (!pending_exact
#ifdef WCHAR
	      /* If last exactn handle binary(or character) and
		 new exactn handle character(or binary).  */
	      || is_exactn_bin != is_binary[p - 1 - pattern]
#endif /* WCHAR */

              /* If last exactn not at current position.  */
              || pending_exact + *pending_exact + 1 != b

              /* We have only one byte following the exactn for the count.  */
	      || *pending_exact == (1 << BYTEWIDTH) - 1

              /* If followed by a repetition operator.  */
              || *p == '*' || *p == '^'
	      || ((syntax & RE_BK_PLUS_QM)
		  ? *p == '\\' && (p[1] == '+' || p[1] == '?')
		  : (*p == '+' || *p == '?'))
	      || ((syntax & RE_INTERVALS)
                  && ((syntax & RE_NO_BK_BRACES)
		      ? *p == '{'
                      : (p[0] == '\\' && p[1] == '{'))))
	    {
	      /* Start building a new exactn.  */

              laststart = b;

#ifdef WCHAR
	      /* Is this exactn binary data or character? */
	      is_exactn_bin = is_binary[p - 1 - pattern];
	      if (is_exactn_bin)
		  BUF_PUSH_2 (exactn_bin, 0);
	      else
		  BUF_PUSH_2 (exactn, 0);
#else
	      BUF_PUSH_2 (exactn, 0);
#endif /* WCHAR */
	      pending_exact = b - 1;
            }

	  BUF_PUSH (c);
          (*pending_exact)++;
	  break;
        } /* switch (c) */
    } /* while p != pend */


  /* Through the pattern now.  */

  if (fixup_alt_jump)
    STORE_JUMP (jump_past_alt, fixup_alt_jump, b);

  if (!COMPILE_STACK_EMPTY)
    FREE_STACK_RETURN (REG_EPAREN);

  /* If we don't want backtracking, force success
     the first time we reach the end of the compiled pattern.  */
  if (syntax & RE_NO_POSIX_BACKTRACKING)
    BUF_PUSH (succeed);

#ifdef WCHAR
  free (pattern);
  free (mbs_offset);
  free (is_binary);
#endif
  free (compile_stack.stack);

  /* We have succeeded; set the length of the buffer.  */
#ifdef WCHAR
  bufp->used = (uintptr_t) b - (uintptr_t) COMPILED_BUFFER_VAR;
#else
  bufp->used = b - bufp->buffer;
#endif

#ifdef DEBUG
  if (debug)
    {
      DEBUG_PRINT1 ("\nCompiled pattern: \n");
      PREFIX(print_compiled_pattern) (bufp);
    }
#endif /* DEBUG */

#ifndef MATCH_MAY_ALLOCATE
  /* Initialize the failure stack to the largest possible stack.  This
     isn't necessary unless we're trying to avoid calling alloca in
     the search and match routines.  */
  {
    int num_regs = bufp->re_nsub + 1;

    /* Since DOUBLE_FAIL_STACK refuses to double only if the current size
       is strictly greater than re_max_failures, the largest possible stack
       is 2 * re_max_failures failure points.  */
    if (fail_stack.size < (2 * re_max_failures * MAX_FAILURE_ITEMS))
      {
	fail_stack.size = (2 * re_max_failures * MAX_FAILURE_ITEMS);

# ifdef emacs
	if (! fail_stack.stack)
	  fail_stack.stack
	    = (PREFIX(fail_stack_elt_t) *) xmalloc (fail_stack.size
				    * sizeof (PREFIX(fail_stack_elt_t)));
	else
	  fail_stack.stack
	    = (PREFIX(fail_stack_elt_t) *) xrealloc (fail_stack.stack,
				     (fail_stack.size
				      * sizeof (PREFIX(fail_stack_elt_t))));
# else /* not emacs */
	if (! fail_stack.stack)
	  fail_stack.stack
	    = (PREFIX(fail_stack_elt_t) *) malloc (fail_stack.size
				   * sizeof (PREFIX(fail_stack_elt_t)));
	else
	  fail_stack.stack
	    = (PREFIX(fail_stack_elt_t) *) realloc (fail_stack.stack,
					    (fail_stack.size
				     * sizeof (PREFIX(fail_stack_elt_t))));
# endif /* not emacs */
      }

   PREFIX(regex_grow_registers) (num_regs);
  }
#endif /* not MATCH_MAY_ALLOCATE */

  return REG_NOERROR;
} /* regex_compile */

/* Subroutines for `regex_compile'.  */

/* Store OP at LOC followed by two-byte integer parameter ARG.  */
/* ifdef WCHAR, integer parameter is 1 wchar_t.  */

static void
PREFIX(store_op1) (re_opcode_t op, UCHAR_T *loc, int arg)
{
  *loc = (UCHAR_T) op;
  STORE_NUMBER (loc + 1, arg);
}


/* Like `store_op1', but for two two-byte parameters ARG1 and ARG2.  */
/* ifdef WCHAR, integer parameter is 1 wchar_t.  */

static void
PREFIX(store_op2) (re_opcode_t op, UCHAR_T *loc, int arg1, int arg2)
{
  *loc = (UCHAR_T) op;
  STORE_NUMBER (loc + 1, arg1);
  STORE_NUMBER (loc + 1 + OFFSET_ADDRESS_SIZE, arg2);
}


/* Copy the bytes from LOC to END to open up three bytes of space at LOC
   for OP followed by two-byte integer parameter ARG.  */
/* ifdef WCHAR, integer parameter is 1 wchar_t.  */

static void
PREFIX(insert_op1) (re_opcode_t op, UCHAR_T *loc, int arg, UCHAR_T *end)
{
  register UCHAR_T *pfrom = end;
  register UCHAR_T *pto = end + 1 + OFFSET_ADDRESS_SIZE;

  while (pfrom != loc)
    *--pto = *--pfrom;

  PREFIX(store_op1) (op, loc, arg);
}


/* Like `insert_op1', but for two two-byte parameters ARG1 and ARG2.  */
/* ifdef WCHAR, integer parameter is 1 wchar_t.  */

static void
PREFIX(insert_op2) (re_opcode_t op, UCHAR_T *loc, int arg1,
                    int arg2, UCHAR_T *end)
{
  register UCHAR_T *pfrom = end;
  register UCHAR_T *pto = end + 1 + 2 * OFFSET_ADDRESS_SIZE;

  while (pfrom != loc)
    *--pto = *--pfrom;

  PREFIX(store_op2) (op, loc, arg1, arg2);
}


/* P points to just after a ^ in PATTERN.  Return true if that ^ comes
   after an alternative or a begin-subexpression.  We assume there is at
   least one character before the ^.  */

static boolean
PREFIX(at_begline_loc_p) (const CHAR_T *pattern, const CHAR_T *p,
                          reg_syntax_t syntax)
{
  const CHAR_T *prev = p - 2;
  boolean prev_prev_backslash = prev > pattern && prev[-1] == '\\';

  return
       /* After a subexpression?  */
       (*prev == '(' && (syntax & RE_NO_BK_PARENS || prev_prev_backslash))
       /* After an alternative?  */
    || (*prev == '|' && (syntax & RE_NO_BK_VBAR || prev_prev_backslash));
}


/* The dual of at_begline_loc_p.  This one is for $.  We assume there is
   at least one character after the $, i.e., `P < PEND'.  */

static boolean
PREFIX(at_endline_loc_p) (const CHAR_T *p, const CHAR_T *pend,
                          reg_syntax_t syntax)
{
  const CHAR_T *next = p;
  boolean next_backslash = *next == '\\';
  const CHAR_T *next_next = p + 1 < pend ? p + 1 : 0;

  return
       /* Before a subexpression?  */
       (syntax & RE_NO_BK_PARENS ? *next == ')'
        : next_backslash && next_next && *next_next == ')')
       /* Before an alternative?  */
    || (syntax & RE_NO_BK_VBAR ? *next == '|'
        : next_backslash && next_next && *next_next == '|');
}

#else /* not INSIDE_RECURSION */

/* Returns true if REGNUM is in one of COMPILE_STACK's elements and
   false if it's not.  */

static boolean
group_in_compile_stack (compile_stack_type compile_stack, regnum_t regnum)
{
  int this_element;

  for (this_element = compile_stack.avail - 1;
       this_element >= 0;
       this_element--)
    if (compile_stack.stack[this_element].regnum == regnum)
      return true;

  return false;
}
#endif /* not INSIDE_RECURSION */

#ifdef INSIDE_RECURSION

#ifdef WCHAR
/* This insert space, which size is "num", into the pattern at "loc".
   "end" must point the end of the allocated buffer.  */
static void
insert_space (int num, CHAR_T *loc, CHAR_T *end)
{
  register CHAR_T *pto = end;
  register CHAR_T *pfrom = end - num;

  while (pfrom >= loc)
    *pto-- = *pfrom--;
}
#endif /* WCHAR */

#ifdef WCHAR
static reg_errcode_t
wcs_compile_range (CHAR_T range_start_char, const CHAR_T **p_ptr,
                   const CHAR_T *pend, RE_TRANSLATE_TYPE translate,
                   reg_syntax_t syntax, CHAR_T *b, CHAR_T *char_set)
{
  const CHAR_T *p = *p_ptr;
  CHAR_T range_start, range_end;
  reg_errcode_t ret;
# ifdef _LIBC
  uint32_t nrules;
  uint32_t start_val, end_val;
# endif
  if (p == pend)
    return REG_ERANGE;

# ifdef _LIBC
  nrules = _NL_CURRENT_WORD (LC_COLLATE, _NL_COLLATE_NRULES);
  if (nrules != 0)
    {
      const char *collseq = (const char *) _NL_CURRENT(LC_COLLATE,
						       _NL_COLLATE_COLLSEQWC);
      const unsigned char *extra = (const unsigned char *)
	_NL_CURRENT (LC_COLLATE, _NL_COLLATE_SYMB_EXTRAMB);

      if (range_start_char < -1)
	{
	  /* range_start is a collating symbol.  */
	  int32_t *wextra;
	  /* Retreive the index and get collation sequence value.  */
	  wextra = (int32_t*)(extra + char_set[-range_start_char]);
	  start_val = wextra[1 + *wextra];
	}
      else
	start_val = collseq_table_lookup(collseq, TRANSLATE(range_start_char));

      end_val = collseq_table_lookup (collseq, TRANSLATE (p[0]));

      /* Report an error if the range is empty and the syntax prohibits
	 this.  */
      ret = ((syntax & RE_NO_EMPTY_RANGES)
	     && (start_val > end_val))? REG_ERANGE : REG_NOERROR;

      /* Insert space to the end of the char_ranges.  */
      insert_space(2, b - char_set[5] - 2, b - 1);
      *(b - char_set[5] - 2) = (wchar_t)start_val;
      *(b - char_set[5] - 1) = (wchar_t)end_val;
      char_set[4]++; /* ranges_index */
    }
  else
# endif
    {
      range_start = (range_start_char >= 0)? TRANSLATE (range_start_char):
	range_start_char;
      range_end = TRANSLATE (p[0]);
      /* Report an error if the range is empty and the syntax prohibits
	 this.  */
      ret = ((syntax & RE_NO_EMPTY_RANGES)
	     && (range_start > range_end))? REG_ERANGE : REG_NOERROR;

      /* Insert space to the end of the char_ranges.  */
      insert_space(2, b - char_set[5] - 2, b - 1);
      *(b - char_set[5] - 2) = range_start;
      *(b - char_set[5] - 1) = range_end;
      char_set[4]++; /* ranges_index */
    }
  /* Have to increment the pointer into the pattern string, so the
     caller isn't still at the ending character.  */
  (*p_ptr)++;

  return ret;
}
#else /* BYTE */
/* Read the ending character of a range (in a bracket expression) from the
   uncompiled pattern *P_PTR (which ends at PEND).  We assume the
   starting character is in `P[-2]'.  (`P[-1]' is the character `-'.)
   Then we set the translation of all bits between the starting and
   ending characters (inclusive) in the compiled pattern B.

   Return an error code.

   We use these short variable names so we can use the same macros as
   `regex_compile' itself.  */

static reg_errcode_t
byte_compile_range (unsigned int range_start_char, const char **p_ptr,
                    const char *pend, RE_TRANSLATE_TYPE translate,
                    reg_syntax_t syntax, unsigned char *b)
{
  unsigned this_char;
  const char *p = *p_ptr;
  reg_errcode_t ret;
# if _LIBC
  const unsigned char *collseq;
  unsigned int start_colseq;
  unsigned int end_colseq;
# else
  unsigned end_char;
# endif

  if (p == pend)
    return REG_ERANGE;

  /* Have to increment the pointer into the pattern string, so the
     caller isn't still at the ending character.  */
  (*p_ptr)++;

  /* Report an error if the range is empty and the syntax prohibits this.  */
  ret = syntax & RE_NO_EMPTY_RANGES ? REG_ERANGE : REG_NOERROR;

# if _LIBC
  collseq = (const unsigned char *) _NL_CURRENT (LC_COLLATE,
						 _NL_COLLATE_COLLSEQMB);

  start_colseq = collseq[(unsigned char) TRANSLATE (range_start_char)];
  end_colseq = collseq[(unsigned char) TRANSLATE (p[0])];
  for (this_char = 0; this_char <= (unsigned char) -1; ++this_char)
    {
      unsigned int this_colseq = collseq[(unsigned char) TRANSLATE (this_char)];

      if (start_colseq <= this_colseq && this_colseq <= end_colseq)
	{
	  SET_LIST_BIT (TRANSLATE (this_char));
	  ret = REG_NOERROR;
	}
    }
# else
  /* Here we see why `this_char' has to be larger than an `unsigned
     char' -- we would otherwise go into an infinite loop, since all
     characters <= 0xff.  */
  range_start_char = TRANSLATE (range_start_char);
  /* TRANSLATE(p[0]) is casted to char (not unsigned char) in TRANSLATE,
     and some compilers cast it to int implicitly, so following for_loop
     may fall to (almost) infinite loop.
     e.g. If translate[p[0]] = 0xff, end_char may equals to 0xffffffff.
     To avoid this, we cast p[0] to unsigned int and truncate it.  */
  end_char = ((unsigned)TRANSLATE(p[0]) & ((1 << BYTEWIDTH) - 1));

  for (this_char = range_start_char; this_char <= end_char; ++this_char)
    {
      SET_LIST_BIT (TRANSLATE (this_char));
      ret = REG_NOERROR;
    }
# endif

  return ret;
}
#endif /* WCHAR */

/* re_compile_fastmap computes a ``fastmap'' for the compiled pattern in
   BUFP.  A fastmap records which of the (1 << BYTEWIDTH) possible
   characters can start a string that matches the pattern.  This fastmap
   is used by re_search to skip quickly over impossible starting points.

   The caller must supply the address of a (1 << BYTEWIDTH)-byte data
   area as BUFP->fastmap.

   We set the `fastmap', `fastmap_accurate', and `can_be_null' fields in
   the pattern buffer.

   Returns 0 if we succeed, -2 if an internal error.   */

#ifdef WCHAR
/* local function for re_compile_fastmap.
   truncate wchar_t character to char.  */
static unsigned char truncate_wchar (CHAR_T c);

static unsigned char
truncate_wchar (CHAR_T c)
{
  unsigned char buf[MB_CUR_MAX];
  mbstate_t state;
  int retval;
  memset (&state, '\0', sizeof (state));
# ifdef _LIBC
  retval = __wcrtomb (buf, c, &state);
# else
  retval = wcrtomb (buf, c, &state);
# endif
  return retval > 0 ? buf[0] : (unsigned char) c;
}
#endif /* WCHAR */

static int
PREFIX(re_compile_fastmap) (struct re_pattern_buffer *bufp)
{
  int j, k;
#ifdef MATCH_MAY_ALLOCATE
  PREFIX(fail_stack_type) fail_stack;
#endif
#ifndef REGEX_MALLOC
  char *destination;
#endif

  register char *fastmap = bufp->fastmap;

#ifdef WCHAR
  /* We need to cast pattern to (wchar_t*), because we casted this compiled
     pattern to (char*) in regex_compile.  */
  UCHAR_T *pattern = (UCHAR_T*)bufp->buffer;
  register UCHAR_T *pend = (UCHAR_T*) (bufp->buffer + bufp->used);
#else /* BYTE */
  UCHAR_T *pattern = bufp->buffer;
  register UCHAR_T *pend = pattern + bufp->used;
#endif /* WCHAR */
  UCHAR_T *p = pattern;

#ifdef REL_ALLOC
  /* This holds the pointer to the failure stack, when
     it is allocated relocatably.  */
  fail_stack_elt_t *failure_stack_ptr;
#endif

  /* Assume that each path through the pattern can be null until
     proven otherwise.  We set this false at the bottom of switch
     statement, to which we get only if a particular path doesn't
     match the empty string.  */
  boolean path_can_be_null = true;

  /* We aren't doing a `succeed_n' to begin with.  */
  boolean succeed_n_p = false;

  assert (fastmap != NULL && p != NULL);

  INIT_FAIL_STACK ();
  bzero (fastmap, 1 << BYTEWIDTH);  /* Assume nothing's valid.  */
  bufp->fastmap_accurate = 1;	    /* It will be when we're done.  */
  bufp->can_be_null = 0;

  while (1)
    {
      if (p == pend || *p == (UCHAR_T) succeed)
	{
	  /* We have reached the (effective) end of pattern.  */
	  if (!FAIL_STACK_EMPTY ())
	    {
	      bufp->can_be_null |= path_can_be_null;

	      /* Reset for next path.  */
	      path_can_be_null = true;

	      p = fail_stack.stack[--fail_stack.avail].pointer;

	      continue;
	    }
	  else
	    break;
	}

      /* We should never be about to go beyond the end of the pattern.  */
      assert (p < pend);

      switch (SWITCH_ENUM_CAST ((re_opcode_t) *p++))
	{

        /* I guess the idea here is to simply not bother with a fastmap
           if a backreference is used, since it's too hard to figure out
           the fastmap for the corresponding group.  Setting
           `can_be_null' stops `re_search_2' from using the fastmap, so
           that is all we do.  */
	case duplicate:
	  bufp->can_be_null = 1;
          goto done;


      /* Following are the cases which match a character.  These end
         with `break'.  */

#ifdef WCHAR
	case exactn:
          fastmap[truncate_wchar(p[1])] = 1;
	  break;
#else /* BYTE */
	case exactn:
          fastmap[p[1]] = 1;
	  break;
#endif /* WCHAR */
#ifdef MBS_SUPPORT
	case exactn_bin:
	  fastmap[p[1]] = 1;
	  break;
#endif

#ifdef WCHAR
        /* It is hard to distinguish fastmap from (multi byte) characters
           which depends on current locale.  */
        case charset:
	case charset_not:
	case wordchar:
	case notwordchar:
          bufp->can_be_null = 1;
          goto done;
#else /* BYTE */
        case charset:
          for (j = *p++ * BYTEWIDTH - 1; j >= 0; j--)
	    if (p[j / BYTEWIDTH] & (1 << (j % BYTEWIDTH)))
              fastmap[j] = 1;
	  break;


	case charset_not:
	  /* Chars beyond end of map must be allowed.  */
	  for (j = *p * BYTEWIDTH; j < (1 << BYTEWIDTH); j++)
            fastmap[j] = 1;

	  for (j = *p++ * BYTEWIDTH - 1; j >= 0; j--)
	    if (!(p[j / BYTEWIDTH] & (1 << (j % BYTEWIDTH))))
              fastmap[j] = 1;
          break;


	case wordchar:
	  for (j = 0; j < (1 << BYTEWIDTH); j++)
	    if (SYNTAX (j) == Sword)
	      fastmap[j] = 1;
	  break;


	case notwordchar:
	  for (j = 0; j < (1 << BYTEWIDTH); j++)
	    if (SYNTAX (j) != Sword)
	      fastmap[j] = 1;
	  break;
#endif /* WCHAR */

        case anychar:
	  {
	    int fastmap_newline = fastmap['\n'];

	    /* `.' matches anything ...  */
	    for (j = 0; j < (1 << BYTEWIDTH); j++)
	      fastmap[j] = 1;

	    /* ... except perhaps newline.  */
	    if (!(bufp->syntax & RE_DOT_NEWLINE))
	      fastmap['\n'] = fastmap_newline;

	    /* Return if we have already set `can_be_null'; if we have,
	       then the fastmap is irrelevant.  Something's wrong here.  */
	    else if (bufp->can_be_null)
	      goto done;

	    /* Otherwise, have to check alternative paths.  */
	    break;
	  }

#ifdef emacs
        case syntaxspec:
	  k = *p++;
	  for (j = 0; j < (1 << BYTEWIDTH); j++)
	    if (SYNTAX (j) == (enum syntaxcode) k)
	      fastmap[j] = 1;
	  break;


	case notsyntaxspec:
	  k = *p++;
	  for (j = 0; j < (1 << BYTEWIDTH); j++)
	    if (SYNTAX (j) != (enum syntaxcode) k)
	      fastmap[j] = 1;
	  break;


      /* All cases after this match the empty string.  These end with
         `continue'.  */


	case before_dot:
	case at_dot:
	case after_dot:
          continue;
#endif /* emacs */


        case no_op:
        case begline:
        case endline:
	case begbuf:
	case endbuf:
	case wordbound:
	case notwordbound:
	case wordbeg:
	case wordend:
        case push_dummy_failure:
          continue;


	case jump_n:
        case pop_failure_jump:
	case maybe_pop_jump:
	case jump:
        case jump_past_alt:
	case dummy_failure_jump:
          EXTRACT_NUMBER_AND_INCR (j, p);
	  p += j;
	  if (j > 0)
	    continue;

          /* Jump backward implies we just went through the body of a
             loop and matched nothing.  Opcode jumped to should be
             `on_failure_jump' or `succeed_n'.  Just treat it like an
             ordinary jump.  For a * loop, it has pushed its failure
             point already; if so, discard that as redundant.  */
          if ((re_opcode_t) *p != on_failure_jump
	      && (re_opcode_t) *p != succeed_n)
	    continue;

          p++;
          EXTRACT_NUMBER_AND_INCR (j, p);
          p += j;

          /* If what's on the stack is where we are now, pop it.  */
          if (!FAIL_STACK_EMPTY ()
	      && fail_stack.stack[fail_stack.avail - 1].pointer == p)
            fail_stack.avail--;

          continue;


        case on_failure_jump:
        case on_failure_keep_string_jump:
	handle_on_failure_jump:
          EXTRACT_NUMBER_AND_INCR (j, p);

          /* For some patterns, e.g., `(a?)?', `p+j' here points to the
             end of the pattern.  We don't want to push such a point,
             since when we restore it above, entering the switch will
             increment `p' past the end of the pattern.  We don't need
             to push such a point since we obviously won't find any more
             fastmap entries beyond `pend'.  Such a pattern can match
             the null string, though.  */
          if (p + j < pend)
            {
              if (!PUSH_PATTERN_OP (p + j, fail_stack))
		{
		  RESET_FAIL_STACK ();
		  return -2;
		}
            }
          else
            bufp->can_be_null = 1;

          if (succeed_n_p)
            {
              EXTRACT_NUMBER_AND_INCR (k, p);	/* Skip the n.  */
              succeed_n_p = false;
	    }

          continue;


	case succeed_n:
          /* Get to the number of times to succeed.  */
          p += OFFSET_ADDRESS_SIZE;

          /* Increment p past the n for when k != 0.  */
          EXTRACT_NUMBER_AND_INCR (k, p);
          if (k == 0)
	    {
              p -= 2 * OFFSET_ADDRESS_SIZE;
  	      succeed_n_p = true;  /* Spaghetti code alert.  */
              goto handle_on_failure_jump;
            }
          continue;


	case set_number_at:
          p += 2 * OFFSET_ADDRESS_SIZE;
          continue;


	case start_memory:
        case stop_memory:
	  p += 2;
	  continue;


	default:
          abort (); /* We have listed all the cases.  */
        } /* switch *p++ */

      /* Getting here means we have found the possible starting
         characters for one path of the pattern -- and that the empty
         string does not match.  We need not follow this path further.
         Instead, look at the next alternative (remembered on the
         stack), or quit if no more.  The test at the top of the loop
         does these things.  */
      path_can_be_null = false;
      p = pend;
    } /* while p */

  /* Set `can_be_null' for the last path (also the first path, if the
     pattern is empty).  */
  bufp->can_be_null |= path_can_be_null;

 done:
  RESET_FAIL_STACK ();
  return 0;
}

#else /* not INSIDE_RECURSION */

int
re_compile_fastmap (struct re_pattern_buffer *bufp)
{
# ifdef MBS_SUPPORT
  if (MB_CUR_MAX != 1)
    return wcs_re_compile_fastmap(bufp);
  else
# endif
    return byte_re_compile_fastmap(bufp);
} /* re_compile_fastmap */
#ifdef _LIBC
weak_alias (__re_compile_fastmap, re_compile_fastmap)
#endif


/* Set REGS to hold NUM_REGS registers, storing them in STARTS and
   ENDS.  Subsequent matches using PATTERN_BUFFER and REGS will use
   this memory for recording register information.  STARTS and ENDS
   must be allocated using the malloc library routine, and must each
   be at least NUM_REGS * sizeof (regoff_t) bytes long.

   If NUM_REGS == 0, then subsequent matches should allocate their own
   register data.

   Unless this function is called, the first search or match using
   PATTERN_BUFFER will allocate its own register data, without
   freeing the old data.  */

void
re_set_registers (struct re_pattern_buffer *bufp,
                  struct re_registers *regs, unsigned num_regs,
                  regoff_t *starts, regoff_t *ends)
{
  if (num_regs)
    {
      bufp->regs_allocated = REGS_REALLOCATE;
      regs->num_regs = num_regs;
      regs->start = starts;
      regs->end = ends;
    }
  else
    {
      bufp->regs_allocated = REGS_UNALLOCATED;
      regs->num_regs = 0;
      regs->start = regs->end = (regoff_t *) 0;
    }
}
#ifdef _LIBC
weak_alias (__re_set_registers, re_set_registers)
#endif

/* Searching routines.  */

/* Like re_search_2, below, but only one string is specified, and
   doesn't let you say where to stop matching.  */

int
re_search (struct re_pattern_buffer *bufp, const char *string, int size,
           int startpos, int range, struct re_registers *regs)
{
  return re_search_2 (bufp, NULL, 0, string, size, startpos, range,
		      regs, size);
}
#ifdef _LIBC
weak_alias (__re_search, re_search)
#endif


/* Using the compiled pattern in BUFP->buffer, first tries to match the
   virtual concatenation of STRING1 and STRING2, starting first at index
   STARTPOS, then at STARTPOS + 1, and so on.

   STRING1 and STRING2 have length SIZE1 and SIZE2, respectively.

   RANGE is how far to scan while trying to match.  RANGE = 0 means try
   only at STARTPOS; in general, the last start tried is STARTPOS +
   RANGE.

   In REGS, return the indices of the virtual concatenation of STRING1
   and STRING2 that matched the entire BUFP->buffer and its contained
   subexpressions.

   Do not consider matching one past the index STOP in the virtual
   concatenation of STRING1 and STRING2.

   We return either the position in the strings at which the match was
   found, -1 if no match, or -2 if error (such as failure
   stack overflow).  */

int
re_search_2 (struct re_pattern_buffer *bufp, const char *string1, int size1,
             const char *string2, int size2, int startpos, int range,
             struct re_registers *regs, int stop)
{
# ifdef MBS_SUPPORT
  if (MB_CUR_MAX != 1)
    return wcs_re_search_2 (bufp, string1, size1, string2, size2, startpos,
			    range, regs, stop);
  else
# endif
    return byte_re_search_2 (bufp, string1, size1, string2, size2, startpos,
			     range, regs, stop);
} /* re_search_2 */
#ifdef _LIBC
weak_alias (__re_search_2, re_search_2)
#endif

#endif /* not INSIDE_RECURSION */

#ifdef INSIDE_RECURSION

#ifdef MATCH_MAY_ALLOCATE
# define FREE_VAR(var) if (var) REGEX_FREE (var); var = NULL
#else
# define FREE_VAR(var) if (var) free (var); var = NULL
#endif

#ifdef WCHAR
# define MAX_ALLOCA_SIZE	2000

# define FREE_WCS_BUFFERS() \
  do {									      \
    if (size1 > MAX_ALLOCA_SIZE)					      \
      {									      \
	free (wcs_string1);						      \
	free (mbs_offset1);						      \
      }									      \
    else								      \
      {									      \
	FREE_VAR (wcs_string1);						      \
	FREE_VAR (mbs_offset1);						      \
      }									      \
    if (size2 > MAX_ALLOCA_SIZE) 					      \
      {									      \
	free (wcs_string2);						      \
	free (mbs_offset2);						      \
      }									      \
    else								      \
      {									      \
	FREE_VAR (wcs_string2);						      \
	FREE_VAR (mbs_offset2);						      \
      }									      \
  } while (0)

#endif


static int
PREFIX(re_search_2) (struct re_pattern_buffer *bufp, const char *string1,
                     int size1, const char *string2, int size2,
                     int startpos, int range,
                     struct re_registers *regs, int stop)
{
  int val;
  register char *fastmap = bufp->fastmap;
  register RE_TRANSLATE_TYPE translate = bufp->translate;
  int total_size = size1 + size2;
  int endpos = startpos + range;
#ifdef WCHAR
  /* We need wchar_t* buffers correspond to cstring1, cstring2.  */
  wchar_t *wcs_string1 = NULL, *wcs_string2 = NULL;
  /* We need the size of wchar_t buffers correspond to csize1, csize2.  */
  int wcs_size1 = 0, wcs_size2 = 0;
  /* offset buffer for optimizatoin. See convert_mbs_to_wc.  */
  int *mbs_offset1 = NULL, *mbs_offset2 = NULL;
  /* They hold whether each wchar_t is binary data or not.  */
  char *is_binary = NULL;
#endif /* WCHAR */

  /* Check for out-of-range STARTPOS.  */
  if (startpos < 0 || startpos > total_size)
    return -1;

  /* Fix up RANGE if it might eventually take us outside
     the virtual concatenation of STRING1 and STRING2.
     Make sure we won't move STARTPOS below 0 or above TOTAL_SIZE.  */
  if (endpos < 0)
    range = 0 - startpos;
  else if (endpos > total_size)
    range = total_size - startpos;

  /* If the search isn't to be a backwards one, don't waste time in a
     search for a pattern that must be anchored.  */
  if (bufp->used > 0 && range > 0
      && ((re_opcode_t) bufp->buffer[0] == begbuf
	  /* `begline' is like `begbuf' if it cannot match at newlines.  */
	  || ((re_opcode_t) bufp->buffer[0] == begline
	      && !bufp->newline_anchor)))
    {
      if (startpos > 0)
	return -1;
      else
	range = 1;
    }

#ifdef emacs
  /* In a forward search for something that starts with \=.
     don't keep searching past point.  */
  if (bufp->used > 0 && (re_opcode_t) bufp->buffer[0] == at_dot && range > 0)
    {
      range = PT - startpos;
      if (range <= 0)
	return -1;
    }
#endif /* emacs */

  /* Update the fastmap now if not correct already.  */
  if (fastmap && !bufp->fastmap_accurate)
    if (re_compile_fastmap (bufp) == -2)
      return -2;

#ifdef WCHAR
  /* Allocate wchar_t array for wcs_string1 and wcs_string2 and
     fill them with converted string.  */
  if (size1 != 0)
    {
      if (size1 > MAX_ALLOCA_SIZE)
	{
	  wcs_string1 = TALLOC (size1 + 1, CHAR_T);
	  mbs_offset1 = TALLOC (size1 + 1, int);
	  is_binary = TALLOC (size1 + 1, char);
	}
      else
	{
	  wcs_string1 = REGEX_TALLOC (size1 + 1, CHAR_T);
	  mbs_offset1 = REGEX_TALLOC (size1 + 1, int);
	  is_binary = REGEX_TALLOC (size1 + 1, char);
	}
      if (!wcs_string1 || !mbs_offset1 || !is_binary)
	{
	  if (size1 > MAX_ALLOCA_SIZE)
	    {
	      free (wcs_string1);
	      free (mbs_offset1);
	      free (is_binary);
	    }
	  else
	    {
	      FREE_VAR (wcs_string1);
	      FREE_VAR (mbs_offset1);
	      FREE_VAR (is_binary);
	    }
	  return -2;
	}
      wcs_size1 = convert_mbs_to_wcs(wcs_string1, string1, size1,
				     mbs_offset1, is_binary);
      wcs_string1[wcs_size1] = L'\0'; /* for a sentinel  */
      if (size1 > MAX_ALLOCA_SIZE)
	free (is_binary);
      else
	FREE_VAR (is_binary);
    }
  if (size2 != 0)
    {
      if (size2 > MAX_ALLOCA_SIZE)
	{
	  wcs_string2 = TALLOC (size2 + 1, CHAR_T);
	  mbs_offset2 = TALLOC (size2 + 1, int);
	  is_binary = TALLOC (size2 + 1, char);
	}
      else
	{
	  wcs_string2 = REGEX_TALLOC (size2 + 1, CHAR_T);
	  mbs_offset2 = REGEX_TALLOC (size2 + 1, int);
	  is_binary = REGEX_TALLOC (size2 + 1, char);
	}
      if (!wcs_string2 || !mbs_offset2 || !is_binary)
	{
	  FREE_WCS_BUFFERS ();
	  if (size2 > MAX_ALLOCA_SIZE)
	    free (is_binary);
	  else
	    FREE_VAR (is_binary);
	  return -2;
	}
      wcs_size2 = convert_mbs_to_wcs(wcs_string2, string2, size2,
				     mbs_offset2, is_binary);
      wcs_string2[wcs_size2] = L'\0'; /* for a sentinel  */
      if (size2 > MAX_ALLOCA_SIZE)
	free (is_binary);
      else
	FREE_VAR (is_binary);
    }
#endif /* WCHAR */


  /* Loop through the string, looking for a place to start matching.  */
  for (;;)
    {
      /* If a fastmap is supplied, skip quickly over characters that
         cannot be the start of a match.  If the pattern can match the
         null string, however, we don't need to skip characters; we want
         the first null string.  */
      if (fastmap && startpos < total_size && !bufp->can_be_null)
	{
	  if (range > 0)	/* Searching forwards.  */
	    {
	      register const char *d;
	      register int lim = 0;
	      int irange = range;

              if (startpos < size1 && startpos + range >= size1)
                lim = range - (size1 - startpos);

	      d = (startpos >= size1 ? string2 - size1 : string1) + startpos;

              /* Written out as an if-else to avoid testing `translate'
                 inside the loop.  */
	      if (translate)
                while (range > lim
                       && !fastmap[(unsigned char)
				   translate[(unsigned char) *d++]])
                  range--;
	      else
                while (range > lim && !fastmap[(unsigned char) *d++])
                  range--;

	      startpos += irange - range;
	    }
	  else				/* Searching backwards.  */
	    {
	      register CHAR_T c = (size1 == 0 || startpos >= size1
				      ? string2[startpos - size1]
				      : string1[startpos]);

	      if (!fastmap[(unsigned char) TRANSLATE (c)])
		goto advance;
	    }
	}

      /* If can't match the null string, and that's all we have left, fail.  */
      if (range >= 0 && startpos == total_size && fastmap
          && !bufp->can_be_null)
       {
#ifdef WCHAR
         FREE_WCS_BUFFERS ();
#endif
         return -1;
       }

#ifdef WCHAR
      val = wcs_re_match_2_internal (bufp, string1, size1, string2,
				     size2, startpos, regs, stop,
				     wcs_string1, wcs_size1,
				     wcs_string2, wcs_size2,
				     mbs_offset1, mbs_offset2);
#else /* BYTE */
      val = byte_re_match_2_internal (bufp, string1, size1, string2,
				      size2, startpos, regs, stop);
#endif /* BYTE */

#ifndef REGEX_MALLOC
# ifdef C_ALLOCA
      alloca (0);
# endif
#endif

      if (val >= 0)
	{
#ifdef WCHAR
	  FREE_WCS_BUFFERS ();
#endif
	  return startpos;
	}

      if (val == -2)
	{
#ifdef WCHAR
	  FREE_WCS_BUFFERS ();
#endif
	  return -2;
	}

    advance:
      if (!range)
        break;
      else if (range > 0)
        {
          range--;
          startpos++;
        }
      else
        {
          range++;
          startpos--;
        }
    }
#ifdef WCHAR
  FREE_WCS_BUFFERS ();
#endif
  return -1;
}

#ifdef WCHAR
/* This converts PTR, a pointer into one of the search wchar_t strings
   `string1' and `string2' into an multibyte string offset from the
   beginning of that string. We use mbs_offset to optimize.
   See convert_mbs_to_wcs.  */
# define POINTER_TO_OFFSET(ptr)						\
  (FIRST_STRING_P (ptr)							\
   ? ((regoff_t)(mbs_offset1 != NULL? mbs_offset1[(ptr)-string1] : 0))	\
   : ((regoff_t)((mbs_offset2 != NULL? mbs_offset2[(ptr)-string2] : 0)	\
		 + csize1)))
#else /* BYTE */
/* This converts PTR, a pointer into one of the search strings `string1'
   and `string2' into an offset from the beginning of that string.  */
# define POINTER_TO_OFFSET(ptr)			\
  (FIRST_STRING_P (ptr)				\
   ? ((regoff_t) ((ptr) - string1))		\
   : ((regoff_t) ((ptr) - string2 + size1)))
#endif /* WCHAR */

/* Macros for dealing with the split strings in re_match_2.  */

#define MATCHING_IN_FIRST_STRING  (dend == end_match_1)

/* Call before fetching a character with *d.  This switches over to
   string2 if necessary.  */
#define PREFETCH()							\
  while (d == dend)						    	\
    {									\
      /* End of string2 => fail.  */					\
      if (dend == end_match_2) 						\
        goto fail;							\
      /* End of string1 => advance to string2.  */ 			\
      d = string2;						        \
      dend = end_match_2;						\
    }

/* Test if at very beginning or at very end of the virtual concatenation
   of `string1' and `string2'.  If only one string, it's `string2'.  */
#define AT_STRINGS_BEG(d) ((d) == (size1 ? string1 : string2) || !size2)
#define AT_STRINGS_END(d) ((d) == end2)


/* Test if D points to a character which is word-constituent.  We have
   two special cases to check for: if past the end of string1, look at
   the first character in string2; and if before the beginning of
   string2, look at the last character in string1.  */
#ifdef WCHAR
/* Use internationalized API instead of SYNTAX.  */
# define WORDCHAR_P(d)							\
  (iswalnum ((wint_t)((d) == end1 ? *string2				\
           : (d) == string2 - 1 ? *(end1 - 1) : *(d))) != 0		\
   || ((d) == end1 ? *string2						\
       : (d) == string2 - 1 ? *(end1 - 1) : *(d)) == L'_')
#else /* BYTE */
# define WORDCHAR_P(d)							\
  (SYNTAX ((d) == end1 ? *string2					\
           : (d) == string2 - 1 ? *(end1 - 1) : *(d))			\
   == Sword)
#endif /* WCHAR */

/* Disabled due to a compiler bug -- see comment at case wordbound */
#if 0
/* Test if the character before D and the one at D differ with respect
   to being word-constituent.  */
#define AT_WORD_BOUNDARY(d)						\
  (AT_STRINGS_BEG (d) || AT_STRINGS_END (d)				\
   || WORDCHAR_P (d - 1) != WORDCHAR_P (d))
#endif

/* Free everything we malloc.  */
#ifdef MATCH_MAY_ALLOCATE
# ifdef WCHAR
#  define FREE_VARIABLES()						\
  do {									\
    REGEX_FREE_STACK (fail_stack.stack);				\
    FREE_VAR (regstart);						\
    FREE_VAR (regend);							\
    FREE_VAR (old_regstart);						\
    FREE_VAR (old_regend);						\
    FREE_VAR (best_regstart);						\
    FREE_VAR (best_regend);						\
    FREE_VAR (reg_info);						\
    FREE_VAR (reg_dummy);						\
    FREE_VAR (reg_info_dummy);						\
    if (!cant_free_wcs_buf)						\
      {									\
        FREE_VAR (string1);						\
        FREE_VAR (string2);						\
        FREE_VAR (mbs_offset1);						\
        FREE_VAR (mbs_offset2);						\
      }									\
  } while (0)
# else /* BYTE */
#  define FREE_VARIABLES()						\
  do {									\
    REGEX_FREE_STACK (fail_stack.stack);				\
    FREE_VAR (regstart);						\
    FREE_VAR (regend);							\
    FREE_VAR (old_regstart);						\
    FREE_VAR (old_regend);						\
    FREE_VAR (best_regstart);						\
    FREE_VAR (best_regend);						\
    FREE_VAR (reg_info);						\
    FREE_VAR (reg_dummy);						\
    FREE_VAR (reg_info_dummy);						\
  } while (0)
# endif /* WCHAR */
#else
# ifdef WCHAR
#  define FREE_VARIABLES()						\
  do {									\
    if (!cant_free_wcs_buf)						\
      {									\
        FREE_VAR (string1);						\
        FREE_VAR (string2);						\
        FREE_VAR (mbs_offset1);						\
        FREE_VAR (mbs_offset2);						\
      }									\
  } while (0)
# else /* BYTE */
#  define FREE_VARIABLES() ((void)0) /* Do nothing!  But inhibit gcc warning. */
# endif /* WCHAR */
#endif /* not MATCH_MAY_ALLOCATE */

/* These values must meet several constraints.  They must not be valid
   register values; since we have a limit of 255 registers (because
   we use only one byte in the pattern for the register number), we can
   use numbers larger than 255.  They must differ by 1, because of
   NUM_FAILURE_ITEMS above.  And the value for the lowest register must
   be larger than the value for the highest register, so we do not try
   to actually save any registers when none are active.  */
#define NO_HIGHEST_ACTIVE_REG (1 << BYTEWIDTH)
#define NO_LOWEST_ACTIVE_REG (NO_HIGHEST_ACTIVE_REG + 1)

#else /* not INSIDE_RECURSION */
/* Matching routines.  */

#ifndef emacs   /* Emacs never uses this.  */
/* re_match is like re_match_2 except it takes only a single string.  */

int
re_match (struct re_pattern_buffer *bufp, const char *string,
          int size, int pos, struct re_registers *regs)
{
  int result;
# ifdef MBS_SUPPORT
  if (MB_CUR_MAX != 1)
    result = wcs_re_match_2_internal (bufp, NULL, 0, string, size,
				      pos, regs, size,
				      NULL, 0, NULL, 0, NULL, NULL);
  else
# endif
    result = byte_re_match_2_internal (bufp, NULL, 0, string, size,
				  pos, regs, size);
# ifndef REGEX_MALLOC
#  ifdef C_ALLOCA
  alloca (0);
#  endif
# endif
  return result;
}
# ifdef _LIBC
weak_alias (__re_match, re_match)
# endif
#endif /* not emacs */

#endif /* not INSIDE_RECURSION */

#ifdef INSIDE_RECURSION
static boolean PREFIX(group_match_null_string_p) (UCHAR_T **p,
                                                  UCHAR_T *end,
					PREFIX(register_info_type) *reg_info);
static boolean PREFIX(alt_match_null_string_p) (UCHAR_T *p,
                                                UCHAR_T *end,
					PREFIX(register_info_type) *reg_info);
static boolean PREFIX(common_op_match_null_string_p) (UCHAR_T **p,
                                                      UCHAR_T *end,
					PREFIX(register_info_type) *reg_info);
static int PREFIX(bcmp_translate) (const CHAR_T *s1, const CHAR_T *s2,
                                   int len, char *translate);
#else /* not INSIDE_RECURSION */

/* re_match_2 matches the compiled pattern in BUFP against the
   the (virtual) concatenation of STRING1 and STRING2 (of length SIZE1
   and SIZE2, respectively).  We start matching at POS, and stop
   matching at STOP.

   If REGS is non-null and the `no_sub' field of BUFP is nonzero, we
   store offsets for the substring each group matched in REGS.  See the
   documentation for exactly how many groups we fill.

   We return -1 if no match, -2 if an internal error (such as the
   failure stack overflowing).  Otherwise, we return the length of the
   matched substring.  */

int
re_match_2 (struct re_pattern_buffer *bufp, const char *string1, int size1,
            const char *string2, int size2, int pos,
            struct re_registers *regs, int stop)
{
  int result;
# ifdef MBS_SUPPORT
  if (MB_CUR_MAX != 1)
    result = wcs_re_match_2_internal (bufp, string1, size1, string2, size2,
				      pos, regs, stop,
				      NULL, 0, NULL, 0, NULL, NULL);
  else
# endif
    result = byte_re_match_2_internal (bufp, string1, size1, string2, size2,
				  pos, regs, stop);

#ifndef REGEX_MALLOC
# ifdef C_ALLOCA
  alloca (0);
# endif
#endif
  return result;
}
#ifdef _LIBC
weak_alias (__re_match_2, re_match_2)
#endif

#endif /* not INSIDE_RECURSION */

#ifdef INSIDE_RECURSION

#ifdef WCHAR
static int count_mbs_length (int *, int);

/* This check the substring (from 0, to length) of the multibyte string,
   to which offset_buffer correspond. And count how many wchar_t_characters
   the substring occupy. We use offset_buffer to optimization.
   See convert_mbs_to_wcs.  */

static int
count_mbs_length(int *offset_buffer, int length)
{
  int upper, lower;

  /* Check whether the size is valid.  */
  if (length < 0)
    return -1;

  if (offset_buffer == NULL)
    return 0;

  /* If there are no multibyte character, offset_buffer[i] == i.
   Optmize for this case.  */
  if (offset_buffer[length] == length)
    return length;

  /* Set up upper with length. (because for all i, offset_buffer[i] >= i)  */
  upper = length;
  lower = 0;

  while (true)
    {
      int middle = (lower + upper) / 2;
      if (middle == lower || middle == upper)
	break;
      if (offset_buffer[middle] > length)
	upper = middle;
      else if (offset_buffer[middle] < length)
	lower = middle;
      else
	return middle;
    }

  return -1;
}
#endif /* WCHAR */

/* This is a separate function so that we can force an alloca cleanup
   afterwards.  */
#ifdef WCHAR
static int
wcs_re_match_2_internal (struct re_pattern_buffer *bufp,
                         const char *cstring1, int csize1,
                         const char *cstring2, int csize2,
                         int pos,
			 struct re_registers *regs,
                         int stop,
     /* string1 == string2 == NULL means string1/2, size1/2 and
	mbs_offset1/2 need seting up in this function.  */
     /* We need wchar_t* buffers correspond to cstring1, cstring2.  */
                         wchar_t *string1, int size1,
                         wchar_t *string2, int size2,
     /* offset buffer for optimizatoin. See convert_mbs_to_wc.  */
			 int *mbs_offset1, int *mbs_offset2)
#else /* BYTE */
static int
byte_re_match_2_internal (struct re_pattern_buffer *bufp,
                          const char *string1, int size1,
                          const char *string2, int size2,
                          int pos,
			  struct re_registers *regs, int stop)
#endif /* BYTE */
{
  /* General temporaries.  */
  int mcnt;
  UCHAR_T *p1;
#ifdef WCHAR
  /* They hold whether each wchar_t is binary data or not.  */
  char *is_binary = NULL;
  /* If true, we can't free string1/2, mbs_offset1/2.  */
  int cant_free_wcs_buf = 1;
#endif /* WCHAR */

  /* Just past the end of the corresponding string.  */
  const CHAR_T *end1, *end2;

  /* Pointers into string1 and string2, just past the last characters in
     each to consider matching.  */
  const CHAR_T *end_match_1, *end_match_2;

  /* Where we are in the data, and the end of the current string.  */
  const CHAR_T *d, *dend;

  /* Where we are in the pattern, and the end of the pattern.  */
#ifdef WCHAR
  UCHAR_T *pattern, *p;
  register UCHAR_T *pend;
#else /* BYTE */
  UCHAR_T *p = bufp->buffer;
  register UCHAR_T *pend = p + bufp->used;
#endif /* WCHAR */

  /* Mark the opcode just after a start_memory, so we can test for an
     empty subpattern when we get to the stop_memory.  */
  UCHAR_T *just_past_start_mem = 0;

  /* We use this to map every character in the string.  */
  RE_TRANSLATE_TYPE translate = bufp->translate;

  /* Failure point stack.  Each place that can handle a failure further
     down the line pushes a failure point on this stack.  It consists of
     restart, regend, and reg_info for all registers corresponding to
     the subexpressions we're currently inside, plus the number of such
     registers, and, finally, two char *'s.  The first char * is where
     to resume scanning the pattern; the second one is where to resume
     scanning the strings.  If the latter is zero, the failure point is
     a ``dummy''; if a failure happens and the failure point is a dummy,
     it gets discarded and the next next one is tried.  */
#ifdef MATCH_MAY_ALLOCATE /* otherwise, this is global.  */
  PREFIX(fail_stack_type) fail_stack;
#endif
#ifdef DEBUG
  static unsigned failure_id;
  unsigned nfailure_points_pushed = 0, nfailure_points_popped = 0;
#endif

#ifdef REL_ALLOC
  /* This holds the pointer to the failure stack, when
     it is allocated relocatably.  */
  fail_stack_elt_t *failure_stack_ptr;
#endif

  /* We fill all the registers internally, independent of what we
     return, for use in backreferences.  The number here includes
     an element for register zero.  */
  size_t num_regs = bufp->re_nsub + 1;

  /* The currently active registers.  */
  active_reg_t lowest_active_reg = NO_LOWEST_ACTIVE_REG;
  active_reg_t highest_active_reg = NO_HIGHEST_ACTIVE_REG;

  /* Information on the contents of registers. These are pointers into
     the input strings; they record just what was matched (on this
     attempt) by a subexpression part of the pattern, that is, the
     regnum-th regstart pointer points to where in the pattern we began
     matching and the regnum-th regend points to right after where we
     stopped matching the regnum-th subexpression.  (The zeroth register
     keeps track of what the whole pattern matches.)  */
#ifdef MATCH_MAY_ALLOCATE /* otherwise, these are global.  */
  const CHAR_T **regstart, **regend;
#endif

  /* If a group that's operated upon by a repetition operator fails to
     match anything, then the register for its start will need to be
     restored because it will have been set to wherever in the string we
     are when we last see its open-group operator.  Similarly for a
     register's end.  */
#ifdef MATCH_MAY_ALLOCATE /* otherwise, these are global.  */
  const CHAR_T **old_regstart, **old_regend;
#endif

  /* The is_active field of reg_info helps us keep track of which (possibly
     nested) subexpressions we are currently in. The matched_something
     field of reg_info[reg_num] helps us tell whether or not we have
     matched any of the pattern so far this time through the reg_num-th
     subexpression.  These two fields get reset each time through any
     loop their register is in.  */
#ifdef MATCH_MAY_ALLOCATE /* otherwise, this is global.  */
  PREFIX(register_info_type) *reg_info;
#endif

  /* The following record the register info as found in the above
     variables when we find a match better than any we've seen before.
     This happens as we backtrack through the failure points, which in
     turn happens only if we have not yet matched the entire string. */
  unsigned best_regs_set = false;
#ifdef MATCH_MAY_ALLOCATE /* otherwise, these are global.  */
  const CHAR_T **best_regstart, **best_regend;
#endif

  /* Logically, this is `best_regend[0]'.  But we don't want to have to
     allocate space for that if we're not allocating space for anything
     else (see below).  Also, we never need info about register 0 for
     any of the other register vectors, and it seems rather a kludge to
     treat `best_regend' differently than the rest.  So we keep track of
     the end of the best match so far in a separate variable.  We
     initialize this to NULL so that when we backtrack the first time
     and need to test it, it's not garbage.  */
  const CHAR_T *match_end = NULL;

  /* This helps SET_REGS_MATCHED avoid doing redundant work.  */
  int set_regs_matched_done = 0;

  /* Used when we pop values we don't care about.  */
#ifdef MATCH_MAY_ALLOCATE /* otherwise, these are global.  */
  const CHAR_T **reg_dummy;
  PREFIX(register_info_type) *reg_info_dummy;
#endif

#ifdef DEBUG
  /* Counts the total number of registers pushed.  */
  unsigned num_regs_pushed = 0;
#endif

  DEBUG_PRINT1 ("\n\nEntering re_match_2.\n");

  INIT_FAIL_STACK ();

#ifdef MATCH_MAY_ALLOCATE
  /* Do not bother to initialize all the register variables if there are
     no groups in the pattern, as it takes a fair amount of time.  If
     there are groups, we include space for register 0 (the whole
     pattern), even though we never use it, since it simplifies the
     array indexing.  We should fix this.  */
  if (bufp->re_nsub)
    {
      regstart = REGEX_TALLOC (num_regs, const CHAR_T *);
      regend = REGEX_TALLOC (num_regs, const CHAR_T *);
      old_regstart = REGEX_TALLOC (num_regs, const CHAR_T *);
      old_regend = REGEX_TALLOC (num_regs, const CHAR_T *);
      best_regstart = REGEX_TALLOC (num_regs, const CHAR_T *);
      best_regend = REGEX_TALLOC (num_regs, const CHAR_T *);
      reg_info = REGEX_TALLOC (num_regs, PREFIX(register_info_type));
      reg_dummy = REGEX_TALLOC (num_regs, const CHAR_T *);
      reg_info_dummy = REGEX_TALLOC (num_regs, PREFIX(register_info_type));

      if (!(regstart && regend && old_regstart && old_regend && reg_info
            && best_regstart && best_regend && reg_dummy && reg_info_dummy))
        {
          FREE_VARIABLES ();
          return -2;
        }
    }
  else
    {
      /* We must initialize all our variables to NULL, so that
         `FREE_VARIABLES' doesn't try to free them.  */
      regstart = regend = old_regstart = old_regend = best_regstart
        = best_regend = reg_dummy = NULL;
      reg_info = reg_info_dummy = (PREFIX(register_info_type) *) NULL;
    }
#endif /* MATCH_MAY_ALLOCATE */

  /* The starting position is bogus.  */
#ifdef WCHAR
  if (pos < 0 || pos > csize1 + csize2)
#else /* BYTE */
  if (pos < 0 || pos > size1 + size2)
#endif
    {
      FREE_VARIABLES ();
      return -1;
    }

#ifdef WCHAR
  /* Allocate wchar_t array for string1 and string2 and
     fill them with converted string.  */
  if (string1 == NULL && string2 == NULL)
    {
      /* We need seting up buffers here.  */

      /* We must free wcs buffers in this function.  */
      cant_free_wcs_buf = 0;

      if (csize1 != 0)
	{
	  string1 = REGEX_TALLOC (csize1 + 1, CHAR_T);
	  mbs_offset1 = REGEX_TALLOC (csize1 + 1, int);
	  is_binary = REGEX_TALLOC (csize1 + 1, char);
	  if (!string1 || !mbs_offset1 || !is_binary)
	    {
	      FREE_VAR (string1);
	      FREE_VAR (mbs_offset1);
	      FREE_VAR (is_binary);
	      return -2;
	    }
	}
      if (csize2 != 0)
	{
	  string2 = REGEX_TALLOC (csize2 + 1, CHAR_T);
	  mbs_offset2 = REGEX_TALLOC (csize2 + 1, int);
	  is_binary = REGEX_TALLOC (csize2 + 1, char);
	  if (!string2 || !mbs_offset2 || !is_binary)
	    {
	      FREE_VAR (string1);
	      FREE_VAR (mbs_offset1);
	      FREE_VAR (string2);
	      FREE_VAR (mbs_offset2);
	      FREE_VAR (is_binary);
	      return -2;
	    }
	  size2 = convert_mbs_to_wcs(string2, cstring2, csize2,
				     mbs_offset2, is_binary);
	  string2[size2] = L'\0'; /* for a sentinel  */
	  FREE_VAR (is_binary);
	}
    }

  /* We need to cast pattern to (wchar_t*), because we casted this compiled
     pattern to (char*) in regex_compile.  */
  p = pattern = (CHAR_T*)bufp->buffer;
  pend = (CHAR_T*)(bufp->buffer + bufp->used);

#endif /* WCHAR */

  /* Initialize subexpression text positions to -1 to mark ones that no
     start_memory/stop_memory has been seen for. Also initialize the
     register information struct.  */
  for (mcnt = 1; (unsigned) mcnt < num_regs; mcnt++)
    {
      regstart[mcnt] = regend[mcnt]
        = old_regstart[mcnt] = old_regend[mcnt] = REG_UNSET_VALUE;

      REG_MATCH_NULL_STRING_P (reg_info[mcnt]) = MATCH_NULL_UNSET_VALUE;
      IS_ACTIVE (reg_info[mcnt]) = 0;
      MATCHED_SOMETHING (reg_info[mcnt]) = 0;
      EVER_MATCHED_SOMETHING (reg_info[mcnt]) = 0;
    }

  /* We move `string1' into `string2' if the latter's empty -- but not if
     `string1' is null.  */
  if (size2 == 0 && string1 != NULL)
    {
      string2 = string1;
      size2 = size1;
      string1 = 0;
      size1 = 0;
#ifdef WCHAR
      mbs_offset2 = mbs_offset1;
      csize2 = csize1;
      mbs_offset1 = NULL;
      csize1 = 0;
#endif
    }
  end1 = string1 + size1;
  end2 = string2 + size2;

  /* Compute where to stop matching, within the two strings.  */
#ifdef WCHAR
  if (stop <= csize1)
    {
      mcnt = count_mbs_length(mbs_offset1, stop);
      end_match_1 = string1 + mcnt;
      end_match_2 = string2;
    }
  else
    {
      if (stop > csize1 + csize2)
	stop = csize1 + csize2;
      end_match_1 = end1;
      mcnt = count_mbs_length(mbs_offset2, stop-csize1);
      end_match_2 = string2 + mcnt;
    }
  if (mcnt < 0)
    { /* count_mbs_length return error.  */
      FREE_VARIABLES ();
      return -1;
    }
#else
  if (stop <= size1)
    {
      end_match_1 = string1 + stop;
      end_match_2 = string2;
    }
  else
    {
      end_match_1 = end1;
      end_match_2 = string2 + stop - size1;
    }
#endif /* WCHAR */

  /* `p' scans through the pattern as `d' scans through the data.
     `dend' is the end of the input string that `d' points within.  `d'
     is advanced into the following input string whenever necessary, but
     this happens before fetching; therefore, at the beginning of the
     loop, `d' can be pointing at the end of a string, but it cannot
     equal `string2'.  */
#ifdef WCHAR
  if (size1 > 0 && pos <= csize1)
    {
      mcnt = count_mbs_length(mbs_offset1, pos);
      d = string1 + mcnt;
      dend = end_match_1;
    }
  else
    {
      mcnt = count_mbs_length(mbs_offset2, pos-csize1);
      d = string2 + mcnt;
      dend = end_match_2;
    }

  if (mcnt < 0)
    { /* count_mbs_length return error.  */
      FREE_VARIABLES ();
      return -1;
    }
#else
  if (size1 > 0 && pos <= size1)
    {
      d = string1 + pos;
      dend = end_match_1;
    }
  else
    {
      d = string2 + pos - size1;
      dend = end_match_2;
    }
#endif /* WCHAR */

  DEBUG_PRINT1 ("The compiled pattern is:\n");
  DEBUG_PRINT_COMPILED_PATTERN (bufp, p, pend);
  DEBUG_PRINT1 ("The string to match is: `");
  DEBUG_PRINT_DOUBLE_STRING (d, string1, size1, string2, size2);
  DEBUG_PRINT1 ("'\n");

  /* This loops over pattern commands.  It exits by returning from the
     function if the match is complete, or it drops through if the match
     fails at this starting point in the input data.  */
  for (;;)
    {
#ifdef _LIBC
      DEBUG_PRINT2 ("\n%p: ", p);
#else
      DEBUG_PRINT2 ("\n0x%x: ", p);
#endif

      if (p == pend)
	{ /* End of pattern means we might have succeeded.  */
          DEBUG_PRINT1 ("end of pattern ... ");

	  /* If we haven't matched the entire string, and we want the
             longest match, try backtracking.  */
          if (d != end_match_2)
	    {
	      /* 1 if this match ends in the same string (string1 or string2)
		 as the best previous match.  */
	      boolean same_str_p = (FIRST_STRING_P (match_end)
				    == MATCHING_IN_FIRST_STRING);
	      /* 1 if this match is the best seen so far.  */
	      boolean best_match_p;

	      /* AIX compiler got confused when this was combined
		 with the previous declaration.  */
	      if (same_str_p)
		best_match_p = d > match_end;
	      else
		best_match_p = !MATCHING_IN_FIRST_STRING;

              DEBUG_PRINT1 ("backtracking.\n");

              if (!FAIL_STACK_EMPTY ())
                { /* More failure points to try.  */

                  /* If exceeds best match so far, save it.  */
                  if (!best_regs_set || best_match_p)
                    {
                      best_regs_set = true;
                      match_end = d;

                      DEBUG_PRINT1 ("\nSAVING match as best so far.\n");

                      for (mcnt = 1; (unsigned) mcnt < num_regs; mcnt++)
                        {
                          best_regstart[mcnt] = regstart[mcnt];
                          best_regend[mcnt] = regend[mcnt];
                        }
                    }
                  goto fail;
                }

              /* If no failure points, don't restore garbage.  And if
                 last match is real best match, don't restore second
                 best one. */
              else if (best_regs_set && !best_match_p)
                {
  	        restore_best_regs:
                  /* Restore best match.  It may happen that `dend ==
                     end_match_1' while the restored d is in string2.
                     For example, the pattern `x.*y.*z' against the
                     strings `x-' and `y-z-', if the two strings are
                     not consecutive in memory.  */
                  DEBUG_PRINT1 ("Restoring best registers.\n");

                  d = match_end;
                  dend = ((d >= string1 && d <= end1)
		           ? end_match_1 : end_match_2);

		  for (mcnt = 1; (unsigned) mcnt < num_regs; mcnt++)
		    {
		      regstart[mcnt] = best_regstart[mcnt];
		      regend[mcnt] = best_regend[mcnt];
		    }
                }
            } /* d != end_match_2 */

	succeed_label:
          DEBUG_PRINT1 ("Accepting match.\n");
          /* If caller wants register contents data back, do it.  */
          if (regs && !bufp->no_sub)
	    {
	      /* Have the register data arrays been allocated?  */
              if (bufp->regs_allocated == REGS_UNALLOCATED)
                { /* No.  So allocate them with malloc.  We need one
                     extra element beyond `num_regs' for the `-1' marker
                     GNU code uses.  */
                  regs->num_regs = MAX (RE_NREGS, num_regs + 1);
                  regs->start = TALLOC (regs->num_regs, regoff_t);
                  regs->end = TALLOC (regs->num_regs, regoff_t);
                  if (regs->start == NULL || regs->end == NULL)
		    {
		      FREE_VARIABLES ();
		      return -2;
		    }
                  bufp->regs_allocated = REGS_REALLOCATE;
                }
              else if (bufp->regs_allocated == REGS_REALLOCATE)
                { /* Yes.  If we need more elements than were already
                     allocated, reallocate them.  If we need fewer, just
                     leave it alone.  */
                  if (regs->num_regs < num_regs + 1)
                    {
                      regs->num_regs = num_regs + 1;
                      RETALLOC (regs->start, regs->num_regs, regoff_t);
                      RETALLOC (regs->end, regs->num_regs, regoff_t);
                      if (regs->start == NULL || regs->end == NULL)
			{
			  FREE_VARIABLES ();
			  return -2;
			}
                    }
                }
              else
		{
		  /* These braces fend off a "empty body in an else-statement"
		     warning under GCC when assert expands to nothing.  */
		  assert (bufp->regs_allocated == REGS_FIXED);
		}

              /* Convert the pointer data in `regstart' and `regend' to
                 indices.  Register zero has to be set differently,
                 since we haven't kept track of any info for it.  */
              if (regs->num_regs > 0)
                {
                  regs->start[0] = pos;
#ifdef WCHAR
		  if (MATCHING_IN_FIRST_STRING)
		    regs->end[0] = mbs_offset1 != NULL ?
					mbs_offset1[d-string1] : 0;
		  else
		    regs->end[0] = csize1 + (mbs_offset2 != NULL ?
					     mbs_offset2[d-string2] : 0);
#else
                  regs->end[0] = (MATCHING_IN_FIRST_STRING
				  ? ((regoff_t) (d - string1))
			          : ((regoff_t) (d - string2 + size1)));
#endif /* WCHAR */
                }

              /* Go through the first `min (num_regs, regs->num_regs)'
                 registers, since that is all we initialized.  */
	      for (mcnt = 1; (unsigned) mcnt < MIN (num_regs, regs->num_regs);
		   mcnt++)
		{
                  if (REG_UNSET (regstart[mcnt]) || REG_UNSET (regend[mcnt]))
                    regs->start[mcnt] = regs->end[mcnt] = -1;
                  else
                    {
		      regs->start[mcnt]
			= (regoff_t) POINTER_TO_OFFSET (regstart[mcnt]);
                      regs->end[mcnt]
			= (regoff_t) POINTER_TO_OFFSET (regend[mcnt]);
                    }
		}

              /* If the regs structure we return has more elements than
                 were in the pattern, set the extra elements to -1.  If
                 we (re)allocated the registers, this is the case,
                 because we always allocate enough to have at least one
                 -1 at the end.  */
              for (mcnt = num_regs; (unsigned) mcnt < regs->num_regs; mcnt++)
                regs->start[mcnt] = regs->end[mcnt] = -1;
	    } /* regs && !bufp->no_sub */

          DEBUG_PRINT4 ("%u failure points pushed, %u popped (%u remain).\n",
                        nfailure_points_pushed, nfailure_points_popped,
                        nfailure_points_pushed - nfailure_points_popped);
          DEBUG_PRINT2 ("%u registers pushed.\n", num_regs_pushed);

#ifdef WCHAR
	  if (MATCHING_IN_FIRST_STRING)
	    mcnt = mbs_offset1 != NULL ? mbs_offset1[d-string1] : 0;
	  else
	    mcnt = (mbs_offset2 != NULL ? mbs_offset2[d-string2] : 0) +
			csize1;
          mcnt -= pos;
#else
          mcnt = d - pos - (MATCHING_IN_FIRST_STRING
			    ? string1
			    : string2 - size1);
#endif /* WCHAR */

          DEBUG_PRINT2 ("Returning %d from re_match_2.\n", mcnt);

          FREE_VARIABLES ();
          return mcnt;
        }

      /* Otherwise match next pattern command.  */
      switch (SWITCH_ENUM_CAST ((re_opcode_t) *p++))
	{
        /* Ignore these.  Used to ignore the n of succeed_n's which
           currently have n == 0.  */
        case no_op:
          DEBUG_PRINT1 ("EXECUTING no_op.\n");
          break;

	case succeed:
          DEBUG_PRINT1 ("EXECUTING succeed.\n");
	  goto succeed_label;

        /* Match the next n pattern characters exactly.  The following
           byte in the pattern defines n, and the n bytes after that
           are the characters to match.  */
	case exactn:
#ifdef MBS_SUPPORT
	case exactn_bin:
#endif
	  mcnt = *p++;
          DEBUG_PRINT2 ("EXECUTING exactn %d.\n", mcnt);

          /* This is written out as an if-else so we don't waste time
             testing `translate' inside the loop.  */
          if (translate)
	    {
	      do
		{
		  PREFETCH ();
#ifdef WCHAR
		  if (*d <= 0xff)
		    {
		      if ((UCHAR_T) translate[(unsigned char) *d++]
			  != (UCHAR_T) *p++)
			goto fail;
		    }
		  else
		    {
		      if (*d++ != (CHAR_T) *p++)
			goto fail;
		    }
#else
		  if ((UCHAR_T) translate[(unsigned char) *d++]
		      != (UCHAR_T) *p++)
                    goto fail;
#endif /* WCHAR */
		}
	      while (--mcnt);
	    }
	  else
	    {
	      do
		{
		  PREFETCH ();
		  if (*d++ != (CHAR_T) *p++) goto fail;
		}
	      while (--mcnt);
	    }
	  SET_REGS_MATCHED ();
          break;


        /* Match any character except possibly a newline or a null.  */
	case anychar:
          DEBUG_PRINT1 ("EXECUTING anychar.\n");

          PREFETCH ();

          if ((!(bufp->syntax & RE_DOT_NEWLINE) && TRANSLATE (*d) == '\n')
              || (bufp->syntax & RE_DOT_NOT_NULL && TRANSLATE (*d) == '\000'))
	    goto fail;

          SET_REGS_MATCHED ();
          DEBUG_PRINT2 ("  Matched `%ld'.\n", (long int) *d);
          d++;
	  break;


	case charset:
	case charset_not:
	  {
	    register UCHAR_T c;
#ifdef WCHAR
	    unsigned int i, char_class_length, coll_symbol_length,
              equiv_class_length, ranges_length, chars_length, length;
	    CHAR_T *workp, *workp2, *charset_top;
#define WORK_BUFFER_SIZE 128
            CHAR_T str_buf[WORK_BUFFER_SIZE];
# ifdef _LIBC
	    uint32_t nrules;
# endif /* _LIBC */
#endif /* WCHAR */
	    boolean negate = (re_opcode_t) *(p - 1) == charset_not;

            DEBUG_PRINT2 ("EXECUTING charset%s.\n", negate ? "_not" : "");
	    PREFETCH ();
	    c = TRANSLATE (*d); /* The character to match.  */
#ifdef WCHAR
# ifdef _LIBC
	    nrules = _NL_CURRENT_WORD (LC_COLLATE, _NL_COLLATE_NRULES);
# endif /* _LIBC */
	    charset_top = p - 1;
	    char_class_length = *p++;
	    coll_symbol_length = *p++;
	    equiv_class_length = *p++;
	    ranges_length = *p++;
	    chars_length = *p++;
	    /* p points charset[6], so the address of the next instruction
	       (charset[l+m+n+2o+k+p']) equals p[l+m+n+2*o+p'],
	       where l=length of char_classes, m=length of collating_symbol,
	       n=equivalence_class, o=length of char_range,
	       p'=length of character.  */
	    workp = p;
	    /* Update p to indicate the next instruction.  */
	    p += char_class_length + coll_symbol_length+ equiv_class_length +
              2*ranges_length + chars_length;

            /* match with char_class?  */
	    for (i = 0; i < char_class_length ; i += CHAR_CLASS_SIZE)
	      {
		wctype_t wctype;
		uintptr_t alignedp = ((uintptr_t)workp
				      + __alignof__(wctype_t) - 1)
		  		      & ~(uintptr_t)(__alignof__(wctype_t) - 1);
		wctype = *((wctype_t*)alignedp);
		workp += CHAR_CLASS_SIZE;
# ifdef _LIBC
		if (__iswctype((wint_t)c, wctype))
		  goto char_set_matched;
# else
		if (iswctype((wint_t)c, wctype))
		  goto char_set_matched;
# endif
	      }

            /* match with collating_symbol?  */
# ifdef _LIBC
	    if (nrules != 0)
	      {
		const unsigned char *extra = (const unsigned char *)
		  _NL_CURRENT (LC_COLLATE, _NL_COLLATE_SYMB_EXTRAMB);

		for (workp2 = workp + coll_symbol_length ; workp < workp2 ;
		     workp++)
		  {
		    int32_t *wextra;
		    wextra = (int32_t*)(extra + *workp++);
		    for (i = 0; i < *wextra; ++i)
		      if (TRANSLATE(d[i]) != wextra[1 + i])
			break;

		    if (i == *wextra)
		      {
			/* Update d, however d will be incremented at
			   char_set_matched:, we decrement d here.  */
			d += i - 1;
			goto char_set_matched;
		      }
		  }
	      }
	    else /* (nrules == 0) */
# endif
	      /* If we can't look up collation data, we use wcscoll
		 instead.  */
	      {
		for (workp2 = workp + coll_symbol_length ; workp < workp2 ;)
		  {
		    const CHAR_T *backup_d = d, *backup_dend = dend;
# ifdef _LIBC
		    length = __wcslen (workp);
# else
		    length = wcslen (workp);
# endif

		    /* If wcscoll(the collating symbol, whole string) > 0,
		       any substring of the string never match with the
		       collating symbol.  */
# ifdef _LIBC
		    if (__wcscoll (workp, d) > 0)
# else
		    if (wcscoll (workp, d) > 0)
# endif
		      {
			workp += length + 1;
			continue;
		      }

		    /* First, we compare the collating symbol with
		       the first character of the string.
		       If it don't match, we add the next character to
		       the compare buffer in turn.  */
		    for (i = 0 ; i < WORK_BUFFER_SIZE-1 ; i++, d++)
		      {
			int match;
			if (d == dend)
			  {
			    if (dend == end_match_2)
			      break;
			    d = string2;
			    dend = end_match_2;
			  }

			/* add next character to the compare buffer.  */
			str_buf[i] = TRANSLATE(*d);
			str_buf[i+1] = '\0';

# ifdef _LIBC
			match = __wcscoll (workp, str_buf);
# else
			match = wcscoll (workp, str_buf);
# endif
			if (match == 0)
			  goto char_set_matched;

			if (match < 0)
			  /* (str_buf > workp) indicate (str_buf + X > workp),
			     because for all X (str_buf + X > str_buf).
			     So we don't need continue this loop.  */
			  break;

			/* Otherwise(str_buf < workp),
			   (str_buf+next_character) may equals (workp).
			   So we continue this loop.  */
		      }
		    /* not matched */
		    d = backup_d;
		    dend = backup_dend;
		    workp += length + 1;
		  }
              }
            /* match with equivalence_class?  */
# ifdef _LIBC
	    if (nrules != 0)
	      {
                const CHAR_T *backup_d = d, *backup_dend = dend;
		/* Try to match the equivalence class against
		   those known to the collate implementation.  */
		const int32_t *table;
		const int32_t *weights;
		const int32_t *extra;
		const int32_t *indirect;
		int32_t idx, idx2;
		wint_t *cp;
		size_t len;

		/* This #include defines a local function!  */
#  include <locale/weightwc.h>

		table = (const int32_t *)
		  _NL_CURRENT (LC_COLLATE, _NL_COLLATE_TABLEWC);
		weights = (const wint_t *)
		  _NL_CURRENT (LC_COLLATE, _NL_COLLATE_WEIGHTWC);
		extra = (const wint_t *)
		  _NL_CURRENT (LC_COLLATE, _NL_COLLATE_EXTRAWC);
		indirect = (const int32_t *)
		  _NL_CURRENT (LC_COLLATE, _NL_COLLATE_INDIRECTWC);

		/* Write 1 collating element to str_buf, and
		   get its index.  */
		idx2 = 0;

		for (i = 0 ; idx2 == 0 && i < WORK_BUFFER_SIZE - 1; i++)
		  {
		    cp = (wint_t*)str_buf;
		    if (d == dend)
		      {
			if (dend == end_match_2)
			  break;
			d = string2;
			dend = end_match_2;
		      }
		    str_buf[i] = TRANSLATE(*(d+i));
		    str_buf[i+1] = '\0'; /* sentinel */
		    idx2 = findidx ((const wint_t**)&cp);
		  }

		/* Update d, however d will be incremented at
		   char_set_matched:, we decrement d here.  */
		d = backup_d + ((wchar_t*)cp - (wchar_t*)str_buf - 1);
		if (d >= dend)
		  {
		    if (dend == end_match_2)
			d = dend;
		    else
		      {
			d = string2;
			dend = end_match_2;
		      }
		  }

		len = weights[idx2];

		for (workp2 = workp + equiv_class_length ; workp < workp2 ;
		     workp++)
		  {
		    idx = (int32_t)*workp;
		    /* We already checked idx != 0 in regex_compile. */

		    if (idx2 != 0 && len == weights[idx])
		      {
			int cnt = 0;
			while (cnt < len && (weights[idx + 1 + cnt]
					     == weights[idx2 + 1 + cnt]))
			  ++cnt;

			if (cnt == len)
			  goto char_set_matched;
		      }
		  }
		/* not matched */
                d = backup_d;
                dend = backup_dend;
	      }
	    else /* (nrules == 0) */
# endif
	      /* If we can't look up collation data, we use wcscoll
		 instead.  */
	      {
		for (workp2 = workp + equiv_class_length ; workp < workp2 ;)
		  {
		    const CHAR_T *backup_d = d, *backup_dend = dend;
# ifdef _LIBC
		    length = __wcslen (workp);
# else
		    length = wcslen (workp);
# endif

		    /* If wcscoll(the collating symbol, whole string) > 0,
		       any substring of the string never match with the
		       collating symbol.  */
# ifdef _LIBC
		    if (__wcscoll (workp, d) > 0)
# else
		    if (wcscoll (workp, d) > 0)
# endif
		      {
			workp += length + 1;
			break;
		      }

		    /* First, we compare the equivalence class with
		       the first character of the string.
		       If it don't match, we add the next character to
		       the compare buffer in turn.  */
		    for (i = 0 ; i < WORK_BUFFER_SIZE - 1 ; i++, d++)
		      {
			int match;
			if (d == dend)
			  {
			    if (dend == end_match_2)
			      break;
			    d = string2;
			    dend = end_match_2;
			  }

			/* add next character to the compare buffer.  */
			str_buf[i] = TRANSLATE(*d);
			str_buf[i+1] = '\0';

# ifdef _LIBC
			match = __wcscoll (workp, str_buf);
# else
			match = wcscoll (workp, str_buf);
# endif

			if (match == 0)
			  goto char_set_matched;

			if (match < 0)
			/* (str_buf > workp) indicate (str_buf + X > workp),
			   because for all X (str_buf + X > str_buf).
			   So we don't need continue this loop.  */
			  break;

			/* Otherwise(str_buf < workp),
			   (str_buf+next_character) may equals (workp).
			   So we continue this loop.  */
		      }
		    /* not matched */
		    d = backup_d;
		    dend = backup_dend;
		    workp += length + 1;
		  }
	      }

            /* match with char_range?  */
# ifdef _LIBC
	    if (nrules != 0)
	      {
		uint32_t collseqval;
		const char *collseq = (const char *)
		  _NL_CURRENT(LC_COLLATE, _NL_COLLATE_COLLSEQWC);

		collseqval = collseq_table_lookup (collseq, c);

		for (; workp < p - chars_length ;)
		  {
		    uint32_t start_val, end_val;

		    /* We already compute the collation sequence value
		       of the characters (or collating symbols).  */
		    start_val = (uint32_t) *workp++; /* range_start */
		    end_val = (uint32_t) *workp++; /* range_end */

		    if (start_val <= collseqval && collseqval <= end_val)
		      goto char_set_matched;
		  }
	      }
	    else
# endif
	      {
		/* We set range_start_char at str_buf[0], range_end_char
		   at str_buf[4], and compared char at str_buf[2].  */
		str_buf[1] = 0;
		str_buf[2] = c;
		str_buf[3] = 0;
		str_buf[5] = 0;
		for (; workp < p - chars_length ;)
		  {
		    wchar_t *range_start_char, *range_end_char;

		    /* match if (range_start_char <= c <= range_end_char).  */

		    /* If range_start(or end) < 0, we assume -range_start(end)
		       is the offset of the collating symbol which is specified
		       as the character of the range start(end).  */

		    /* range_start */
		    if (*workp < 0)
		      range_start_char = charset_top - (*workp++);
		    else
		      {
			str_buf[0] = *workp++;
			range_start_char = str_buf;
		      }

		    /* range_end */
		    if (*workp < 0)
		      range_end_char = charset_top - (*workp++);
		    else
		      {
			str_buf[4] = *workp++;
			range_end_char = str_buf + 4;
		      }

# ifdef _LIBC
		    if (__wcscoll (range_start_char, str_buf+2) <= 0
			&& __wcscoll (str_buf+2, range_end_char) <= 0)
# else
		    if (wcscoll (range_start_char, str_buf+2) <= 0
			&& wcscoll (str_buf+2, range_end_char) <= 0)
# endif
		      goto char_set_matched;
		  }
	      }

            /* match with char?  */
	    for (; workp < p ; workp++)
	      if (c == *workp)
		goto char_set_matched;

	    negate = !negate;

	  char_set_matched:
	    if (negate) goto fail;
#else
            /* Cast to `unsigned' instead of `unsigned char' in case the
               bit list is a full 32 bytes long.  */
	    if (c < (unsigned) (*p * BYTEWIDTH)
		&& p[1 + c / BYTEWIDTH] & (1 << (c % BYTEWIDTH)))
	      negate = !negate;

	    p += 1 + *p;

	    if (!negate) goto fail;
#undef WORK_BUFFER_SIZE
#endif /* WCHAR */
	    SET_REGS_MATCHED ();
            d++;
	    break;
	  }


        /* The beginning of a group is represented by start_memory.
           The arguments are the register number in the next byte, and the
           number of groups inner to this one in the next.  The text
           matched within the group is recorded (in the internal
           registers data structure) under the register number.  */
        case start_memory:
	  DEBUG_PRINT3 ("EXECUTING start_memory %ld (%ld):\n",
			(long int) *p, (long int) p[1]);

          /* Find out if this group can match the empty string.  */
	  p1 = p;		/* To send to group_match_null_string_p.  */

          if (REG_MATCH_NULL_STRING_P (reg_info[*p]) == MATCH_NULL_UNSET_VALUE)
            REG_MATCH_NULL_STRING_P (reg_info[*p])
              = PREFIX(group_match_null_string_p) (&p1, pend, reg_info);

          /* Save the position in the string where we were the last time
             we were at this open-group operator in case the group is
             operated upon by a repetition operator, e.g., with `(a*)*b'
             against `ab'; then we want to ignore where we are now in
             the string in case this attempt to match fails.  */
          old_regstart[*p] = REG_MATCH_NULL_STRING_P (reg_info[*p])
                             ? REG_UNSET (regstart[*p]) ? d : regstart[*p]
                             : regstart[*p];
	  DEBUG_PRINT2 ("  old_regstart: %d\n",
			 POINTER_TO_OFFSET (old_regstart[*p]));

          regstart[*p] = d;
	  DEBUG_PRINT2 ("  regstart: %d\n", POINTER_TO_OFFSET (regstart[*p]));

          IS_ACTIVE (reg_info[*p]) = 1;
          MATCHED_SOMETHING (reg_info[*p]) = 0;

	  /* Clear this whenever we change the register activity status.  */
	  set_regs_matched_done = 0;

          /* This is the new highest active register.  */
          highest_active_reg = *p;

          /* If nothing was active before, this is the new lowest active
             register.  */
          if (lowest_active_reg == NO_LOWEST_ACTIVE_REG)
            lowest_active_reg = *p;

          /* Move past the register number and inner group count.  */
          p += 2;
	  just_past_start_mem = p;

          break;


        /* The stop_memory opcode represents the end of a group.  Its
           arguments are the same as start_memory's: the register
           number, and the number of inner groups.  */
	case stop_memory:
	  DEBUG_PRINT3 ("EXECUTING stop_memory %ld (%ld):\n",
			(long int) *p, (long int) p[1]);

          /* We need to save the string position the last time we were at
             this close-group operator in case the group is operated
             upon by a repetition operator, e.g., with `((a*)*(b*)*)*'
             against `aba'; then we want to ignore where we are now in
             the string in case this attempt to match fails.  */
          old_regend[*p] = REG_MATCH_NULL_STRING_P (reg_info[*p])
                           ? REG_UNSET (regend[*p]) ? d : regend[*p]
			   : regend[*p];
	  DEBUG_PRINT2 ("      old_regend: %d\n",
			 POINTER_TO_OFFSET (old_regend[*p]));

          regend[*p] = d;
	  DEBUG_PRINT2 ("      regend: %d\n", POINTER_TO_OFFSET (regend[*p]));

          /* This register isn't active anymore.  */
          IS_ACTIVE (reg_info[*p]) = 0;

	  /* Clear this whenever we change the register activity status.  */
	  set_regs_matched_done = 0;

          /* If this was the only register active, nothing is active
             anymore.  */
          if (lowest_active_reg == highest_active_reg)
            {
              lowest_active_reg = NO_LOWEST_ACTIVE_REG;
              highest_active_reg = NO_HIGHEST_ACTIVE_REG;
            }
          else
            { /* We must scan for the new highest active register, since
                 it isn't necessarily one less than now: consider
                 (a(b)c(d(e)f)g).  When group 3 ends, after the f), the
                 new highest active register is 1.  */
              UCHAR_T r = *p - 1;
              while (r > 0 && !IS_ACTIVE (reg_info[r]))
                r--;

              /* If we end up at register zero, that means that we saved
                 the registers as the result of an `on_failure_jump', not
                 a `start_memory', and we jumped to past the innermost
                 `stop_memory'.  For example, in ((.)*) we save
                 registers 1 and 2 as a result of the *, but when we pop
                 back to the second ), we are at the stop_memory 1.
                 Thus, nothing is active.  */
	      if (r == 0)
                {
                  lowest_active_reg = NO_LOWEST_ACTIVE_REG;
                  highest_active_reg = NO_HIGHEST_ACTIVE_REG;
                }
              else
                highest_active_reg = r;
            }

          /* If just failed to match something this time around with a
             group that's operated on by a repetition operator, try to
             force exit from the ``loop'', and restore the register
             information for this group that we had before trying this
             last match.  */
          if ((!MATCHED_SOMETHING (reg_info[*p])
               || just_past_start_mem == p - 1)
	      && (p + 2) < pend)
            {
              boolean is_a_jump_n = false;

              p1 = p + 2;
              mcnt = 0;
              switch ((re_opcode_t) *p1++)
                {
                  case jump_n:
		    is_a_jump_n = true;
                  case pop_failure_jump:
		  case maybe_pop_jump:
		  case jump:
		  case dummy_failure_jump:
                    EXTRACT_NUMBER_AND_INCR (mcnt, p1);
		    if (is_a_jump_n)
		      p1 += OFFSET_ADDRESS_SIZE;
                    break;

                  default:
                    /* do nothing */ ;
                }
	      p1 += mcnt;

              /* If the next operation is a jump backwards in the pattern
	         to an on_failure_jump right before the start_memory
                 corresponding to this stop_memory, exit from the loop
                 by forcing a failure after pushing on the stack the
                 on_failure_jump's jump in the pattern, and d.  */
              if (mcnt < 0 && (re_opcode_t) *p1 == on_failure_jump
                  && (re_opcode_t) p1[1+OFFSET_ADDRESS_SIZE] == start_memory
		  && p1[2+OFFSET_ADDRESS_SIZE] == *p)
		{
                  /* If this group ever matched anything, then restore
                     what its registers were before trying this last
                     failed match, e.g., with `(a*)*b' against `ab' for
                     regstart[1], and, e.g., with `((a*)*(b*)*)*'
                     against `aba' for regend[3].

                     Also restore the registers for inner groups for,
                     e.g., `((a*)(b*))*' against `aba' (register 3 would
                     otherwise get trashed).  */

                  if (EVER_MATCHED_SOMETHING (reg_info[*p]))
		    {
		      unsigned r;

                      EVER_MATCHED_SOMETHING (reg_info[*p]) = 0;

		      /* Restore this and inner groups' (if any) registers.  */
                      for (r = *p; r < (unsigned) *p + (unsigned) *(p + 1);
			   r++)
                        {
                          regstart[r] = old_regstart[r];

                          /* xx why this test?  */
                          if (old_regend[r] >= regstart[r])
                            regend[r] = old_regend[r];
                        }
                    }
		  p1++;
                  EXTRACT_NUMBER_AND_INCR (mcnt, p1);
                  PUSH_FAILURE_POINT (p1 + mcnt, d, -2);

                  goto fail;
                }
            }

          /* Move past the register number and the inner group count.  */
          p += 2;
          break;


	/* \<digit> has been turned into a `duplicate' command which is
           followed by the numeric value of <digit> as the register number.  */
        case duplicate:
	  {
	    register const CHAR_T *d2, *dend2;
	    int regno = *p++;   /* Get which register to match against.  */
	    DEBUG_PRINT2 ("EXECUTING duplicate %d.\n", regno);

	    /* Can't back reference a group which we've never matched.  */
            if (REG_UNSET (regstart[regno]) || REG_UNSET (regend[regno]))
              goto fail;

            /* Where in input to try to start matching.  */
            d2 = regstart[regno];

            /* Where to stop matching; if both the place to start and
               the place to stop matching are in the same string, then
               set to the place to stop, otherwise, for now have to use
               the end of the first string.  */

            dend2 = ((FIRST_STRING_P (regstart[regno])
		      == FIRST_STRING_P (regend[regno]))
		     ? regend[regno] : end_match_1);
	    for (;;)
	      {
		/* If necessary, advance to next segment in register
                   contents.  */
		while (d2 == dend2)
		  {
		    if (dend2 == end_match_2) break;
		    if (dend2 == regend[regno]) break;

                    /* End of string1 => advance to string2. */
                    d2 = string2;
                    dend2 = regend[regno];
		  }
		/* At end of register contents => success */
		if (d2 == dend2) break;

		/* If necessary, advance to next segment in data.  */
		PREFETCH ();

		/* How many characters left in this segment to match.  */
		mcnt = dend - d;

		/* Want how many consecutive characters we can match in
                   one shot, so, if necessary, adjust the count.  */
                if (mcnt > dend2 - d2)
		  mcnt = dend2 - d2;

		/* Compare that many; failure if mismatch, else move
                   past them.  */
		if (translate
                    ? PREFIX(bcmp_translate) (d, d2, mcnt, translate)
                    : memcmp (d, d2, mcnt*sizeof(UCHAR_T)))
		  goto fail;
		d += mcnt, d2 += mcnt;

		/* Do this because we've match some characters.  */
		SET_REGS_MATCHED ();
	      }
	  }
	  break;


        /* begline matches the empty string at the beginning of the string
           (unless `not_bol' is set in `bufp'), and, if
           `newline_anchor' is set, after newlines.  */
	case begline:
          DEBUG_PRINT1 ("EXECUTING begline.\n");

          if (AT_STRINGS_BEG (d))
            {
              if (!bufp->not_bol) break;
            }
          else if (d[-1] == '\n' && bufp->newline_anchor)
            {
              break;
            }
          /* In all other cases, we fail.  */
          goto fail;


        /* endline is the dual of begline.  */
	case endline:
          DEBUG_PRINT1 ("EXECUTING endline.\n");

          if (AT_STRINGS_END (d))
            {
              if (!bufp->not_eol) break;
            }

          /* We have to ``prefetch'' the next character.  */
          else if ((d == end1 ? *string2 : *d) == '\n'
                   && bufp->newline_anchor)
            {
              break;
            }
          goto fail;


	/* Match at the very beginning of the data.  */
        case begbuf:
          DEBUG_PRINT1 ("EXECUTING begbuf.\n");
          if (AT_STRINGS_BEG (d))
            break;
          goto fail;


	/* Match at the very end of the data.  */
        case endbuf:
          DEBUG_PRINT1 ("EXECUTING endbuf.\n");
	  if (AT_STRINGS_END (d))
	    break;
          goto fail;


        /* on_failure_keep_string_jump is used to optimize `.*\n'.  It
           pushes NULL as the value for the string on the stack.  Then
           `pop_failure_point' will keep the current value for the
           string, instead of restoring it.  To see why, consider
           matching `foo\nbar' against `.*\n'.  The .* matches the foo;
           then the . fails against the \n.  But the next thing we want
           to do is match the \n against the \n; if we restored the
           string value, we would be back at the foo.

           Because this is used only in specific cases, we don't need to
           check all the things that `on_failure_jump' does, to make
           sure the right things get saved on the stack.  Hence we don't
           share its code.  The only reason to push anything on the
           stack at all is that otherwise we would have to change
           `anychar's code to do something besides goto fail in this
           case; that seems worse than this.  */
        case on_failure_keep_string_jump:
          DEBUG_PRINT1 ("EXECUTING on_failure_keep_string_jump");

          EXTRACT_NUMBER_AND_INCR (mcnt, p);
#ifdef _LIBC
          DEBUG_PRINT3 (" %d (to %p):\n", mcnt, p + mcnt);
#else
          DEBUG_PRINT3 (" %d (to 0x%x):\n", mcnt, p + mcnt);
#endif

          PUSH_FAILURE_POINT (p + mcnt, NULL, -2);
          break;


	/* Uses of on_failure_jump:

           Each alternative starts with an on_failure_jump that points
           to the beginning of the next alternative.  Each alternative
           except the last ends with a jump that in effect jumps past
           the rest of the alternatives.  (They really jump to the
           ending jump of the following alternative, because tensioning
           these jumps is a hassle.)

           Repeats start with an on_failure_jump that points past both
           the repetition text and either the following jump or
           pop_failure_jump back to this on_failure_jump.  */
	case on_failure_jump:
        on_failure:
          DEBUG_PRINT1 ("EXECUTING on_failure_jump");

          EXTRACT_NUMBER_AND_INCR (mcnt, p);
#ifdef _LIBC
          DEBUG_PRINT3 (" %d (to %p)", mcnt, p + mcnt);
#else
          DEBUG_PRINT3 (" %d (to 0x%x)", mcnt, p + mcnt);
#endif

          /* If this on_failure_jump comes right before a group (i.e.,
             the original * applied to a group), save the information
             for that group and all inner ones, so that if we fail back
             to this point, the group's information will be correct.
             For example, in \(a*\)*\1, we need the preceding group,
             and in \(zz\(a*\)b*\)\2, we need the inner group.  */

          /* We can't use `p' to check ahead because we push
             a failure point to `p + mcnt' after we do this.  */
          p1 = p;

          /* We need to skip no_op's before we look for the
             start_memory in case this on_failure_jump is happening as
             the result of a completed succeed_n, as in \(a\)\{1,3\}b\1
             against aba.  */
          while (p1 < pend && (re_opcode_t) *p1 == no_op)
            p1++;

          if (p1 < pend && (re_opcode_t) *p1 == start_memory)
            {
              /* We have a new highest active register now.  This will
                 get reset at the start_memory we are about to get to,
                 but we will have saved all the registers relevant to
                 this repetition op, as described above.  */
              highest_active_reg = *(p1 + 1) + *(p1 + 2);
              if (lowest_active_reg == NO_LOWEST_ACTIVE_REG)
                lowest_active_reg = *(p1 + 1);
            }

          DEBUG_PRINT1 (":\n");
          PUSH_FAILURE_POINT (p + mcnt, d, -2);
          break;


        /* A smart repeat ends with `maybe_pop_jump'.
	   We change it to either `pop_failure_jump' or `jump'.  */
        case maybe_pop_jump:
          EXTRACT_NUMBER_AND_INCR (mcnt, p);
          DEBUG_PRINT2 ("EXECUTING maybe_pop_jump %d.\n", mcnt);
          {
	    register UCHAR_T *p2 = p;

            /* Compare the beginning of the repeat with what in the
               pattern follows its end. If we can establish that there
               is nothing that they would both match, i.e., that we
               would have to backtrack because of (as in, e.g., `a*a')
               then we can change to pop_failure_jump, because we'll
               never have to backtrack.

               This is not true in the case of alternatives: in
               `(a|ab)*' we do need to backtrack to the `ab' alternative
               (e.g., if the string was `ab').  But instead of trying to
               detect that here, the alternative has put on a dummy
               failure point which is what we will end up popping.  */

	    /* Skip over open/close-group commands.
	       If what follows this loop is a ...+ construct,
	       look at what begins its body, since we will have to
	       match at least one of that.  */
	    while (1)
	      {
		if (p2 + 2 < pend
		    && ((re_opcode_t) *p2 == stop_memory
			|| (re_opcode_t) *p2 == start_memory))
		  p2 += 3;
		else if (p2 + 2 + 2 * OFFSET_ADDRESS_SIZE < pend
			 && (re_opcode_t) *p2 == dummy_failure_jump)
		  p2 += 2 + 2 * OFFSET_ADDRESS_SIZE;
		else
		  break;
	      }

	    p1 = p + mcnt;
	    /* p1[0] ... p1[2] are the `on_failure_jump' corresponding
	       to the `maybe_finalize_jump' of this case.  Examine what
	       follows.  */

            /* If we're at the end of the pattern, we can change.  */
            if (p2 == pend)
	      {
		/* Consider what happens when matching ":\(.*\)"
		   against ":/".  I don't really understand this code
		   yet.  */
  	        p[-(1+OFFSET_ADDRESS_SIZE)] = (UCHAR_T)
		  pop_failure_jump;
                DEBUG_PRINT1
                  ("  End of pattern: change to `pop_failure_jump'.\n");
              }

            else if ((re_opcode_t) *p2 == exactn
#ifdef MBS_SUPPORT
		     || (re_opcode_t) *p2 == exactn_bin
#endif
		     || (bufp->newline_anchor && (re_opcode_t) *p2 == endline))
	      {
		register UCHAR_T c
                  = *p2 == (UCHAR_T) endline ? '\n' : p2[2];

                if (((re_opcode_t) p1[1+OFFSET_ADDRESS_SIZE] == exactn
#ifdef MBS_SUPPORT
		     || (re_opcode_t) p1[1+OFFSET_ADDRESS_SIZE] == exactn_bin
#endif
		    ) && p1[3+OFFSET_ADDRESS_SIZE] != c)
                  {
  		    p[-(1+OFFSET_ADDRESS_SIZE)] = (UCHAR_T)
		      pop_failure_jump;
#ifdef WCHAR
		      DEBUG_PRINT3 ("  %C != %C => pop_failure_jump.\n",
				    (wint_t) c,
				    (wint_t) p1[3+OFFSET_ADDRESS_SIZE]);
#else
		      DEBUG_PRINT3 ("  %c != %c => pop_failure_jump.\n",
				    (char) c,
				    (char) p1[3+OFFSET_ADDRESS_SIZE]);
#endif
                  }

#ifndef WCHAR
		else if ((re_opcode_t) p1[3] == charset
			 || (re_opcode_t) p1[3] == charset_not)
		  {
		    int negate = (re_opcode_t) p1[3] == charset_not;

		    if (c < (unsigned) (p1[4] * BYTEWIDTH)
			&& p1[5 + c / BYTEWIDTH] & (1 << (c % BYTEWIDTH)))
		      negate = !negate;

                    /* `negate' is equal to 1 if c would match, which means
                        that we can't change to pop_failure_jump.  */
		    if (!negate)
                      {
  		        p[-3] = (unsigned char) pop_failure_jump;
                        DEBUG_PRINT1 ("  No match => pop_failure_jump.\n");
                      }
		  }
#endif /* not WCHAR */
	      }
#ifndef WCHAR
            else if ((re_opcode_t) *p2 == charset)
	      {
		/* We win if the first character of the loop is not part
                   of the charset.  */
                if ((re_opcode_t) p1[3] == exactn
 		    && ! ((int) p2[1] * BYTEWIDTH > (int) p1[5]
 			  && (p2[2 + p1[5] / BYTEWIDTH]
 			      & (1 << (p1[5] % BYTEWIDTH)))))
		  {
		    p[-3] = (unsigned char) pop_failure_jump;
		    DEBUG_PRINT1 ("  No match => pop_failure_jump.\n");
                  }

		else if ((re_opcode_t) p1[3] == charset_not)
		  {
		    int idx;
		    /* We win if the charset_not inside the loop
		       lists every character listed in the charset after.  */
		    for (idx = 0; idx < (int) p2[1]; idx++)
		      if (! (p2[2 + idx] == 0
			     || (idx < (int) p1[4]
				 && ((p2[2 + idx] & ~ p1[5 + idx]) == 0))))
			break;

		    if (idx == p2[1])
                      {
  		        p[-3] = (unsigned char) pop_failure_jump;
                        DEBUG_PRINT1 ("  No match => pop_failure_jump.\n");
                      }
		  }
		else if ((re_opcode_t) p1[3] == charset)
		  {
		    int idx;
		    /* We win if the charset inside the loop
		       has no overlap with the one after the loop.  */
		    for (idx = 0;
			 idx < (int) p2[1] && idx < (int) p1[4];
			 idx++)
		      if ((p2[2 + idx] & p1[5 + idx]) != 0)
			break;

		    if (idx == p2[1] || idx == p1[4])
                      {
  		        p[-3] = (unsigned char) pop_failure_jump;
                        DEBUG_PRINT1 ("  No match => pop_failure_jump.\n");
                      }
		  }
	      }
#endif /* not WCHAR */
	  }
	  p -= OFFSET_ADDRESS_SIZE;	/* Point at relative address again.  */
	  if ((re_opcode_t) p[-1] != pop_failure_jump)
	    {
	      p[-1] = (UCHAR_T) jump;
              DEBUG_PRINT1 ("  Match => jump.\n");
	      goto unconditional_jump;
	    }
        /* Note fall through.  */


	/* The end of a simple repeat has a pop_failure_jump back to
           its matching on_failure_jump, where the latter will push a
           failure point.  The pop_failure_jump takes off failure
           points put on by this pop_failure_jump's matching
           on_failure_jump; we got through the pattern to here from the
           matching on_failure_jump, so didn't fail.  */
        case pop_failure_jump:
          {
            /* We need to pass separate storage for the lowest and
               highest registers, even though we don't care about the
               actual values.  Otherwise, we will restore only one
               register from the stack, since lowest will == highest in
               `pop_failure_point'.  */
            active_reg_t dummy_low_reg, dummy_high_reg;
            UCHAR_T *pdummy = NULL;
            const CHAR_T *sdummy = NULL;

            DEBUG_PRINT1 ("EXECUTING pop_failure_jump.\n");
            POP_FAILURE_POINT (sdummy, pdummy,
                               dummy_low_reg, dummy_high_reg,
                               reg_dummy, reg_dummy, reg_info_dummy);
          }
	  /* Note fall through.  */

	unconditional_jump:
#ifdef _LIBC
	  DEBUG_PRINT2 ("\n%p: ", p);
#else
	  DEBUG_PRINT2 ("\n0x%x: ", p);
#endif
          /* Note fall through.  */

        /* Unconditionally jump (without popping any failure points).  */
        case jump:
	  EXTRACT_NUMBER_AND_INCR (mcnt, p);	/* Get the amount to jump.  */
          DEBUG_PRINT2 ("EXECUTING jump %d ", mcnt);
	  p += mcnt;				/* Do the jump.  */
#ifdef _LIBC
          DEBUG_PRINT2 ("(to %p).\n", p);
#else
          DEBUG_PRINT2 ("(to 0x%x).\n", p);
#endif
	  break;


        /* We need this opcode so we can detect where alternatives end
           in `group_match_null_string_p' et al.  */
        case jump_past_alt:
          DEBUG_PRINT1 ("EXECUTING jump_past_alt.\n");
          goto unconditional_jump;


        /* Normally, the on_failure_jump pushes a failure point, which
           then gets popped at pop_failure_jump.  We will end up at
           pop_failure_jump, also, and with a pattern of, say, `a+', we
           are skipping over the on_failure_jump, so we have to push
           something meaningless for pop_failure_jump to pop.  */
        case dummy_failure_jump:
          DEBUG_PRINT1 ("EXECUTING dummy_failure_jump.\n");
          /* It doesn't matter what we push for the string here.  What
             the code at `fail' tests is the value for the pattern.  */
          PUSH_FAILURE_POINT (NULL, NULL, -2);
          goto unconditional_jump;


        /* At the end of an alternative, we need to push a dummy failure
           point in case we are followed by a `pop_failure_jump', because
           we don't want the failure point for the alternative to be
           popped.  For example, matching `(a|ab)*' against `aab'
           requires that we match the `ab' alternative.  */
        case push_dummy_failure:
          DEBUG_PRINT1 ("EXECUTING push_dummy_failure.\n");
          /* See comments just above at `dummy_failure_jump' about the
             two zeroes.  */
          PUSH_FAILURE_POINT (NULL, NULL, -2);
          break;

        /* Have to succeed matching what follows at least n times.
           After that, handle like `on_failure_jump'.  */
        case succeed_n:
          EXTRACT_NUMBER (mcnt, p + OFFSET_ADDRESS_SIZE);
          DEBUG_PRINT2 ("EXECUTING succeed_n %d.\n", mcnt);

          assert (mcnt >= 0);
          /* Originally, this is how many times we HAVE to succeed.  */
          if (mcnt > 0)
            {
               mcnt--;
	       p += OFFSET_ADDRESS_SIZE;
               STORE_NUMBER_AND_INCR (p, mcnt);
#ifdef _LIBC
               DEBUG_PRINT3 ("  Setting %p to %d.\n", p - OFFSET_ADDRESS_SIZE
			     , mcnt);
#else
               DEBUG_PRINT3 ("  Setting 0x%x to %d.\n", p - OFFSET_ADDRESS_SIZE
			     , mcnt);
#endif
            }
	  else if (mcnt == 0)
            {
#ifdef _LIBC
              DEBUG_PRINT2 ("  Setting two bytes from %p to no_op.\n",
			    p + OFFSET_ADDRESS_SIZE);
#else
              DEBUG_PRINT2 ("  Setting two bytes from 0x%x to no_op.\n",
			    p + OFFSET_ADDRESS_SIZE);
#endif /* _LIBC */

#ifdef WCHAR
	      p[1] = (UCHAR_T) no_op;
#else
	      p[2] = (UCHAR_T) no_op;
              p[3] = (UCHAR_T) no_op;
#endif /* WCHAR */
              goto on_failure;
            }
          break;

        case jump_n:
          EXTRACT_NUMBER (mcnt, p + OFFSET_ADDRESS_SIZE);
          DEBUG_PRINT2 ("EXECUTING jump_n %d.\n", mcnt);

          /* Originally, this is how many times we CAN jump.  */
          if (mcnt)
            {
               mcnt--;
               STORE_NUMBER (p + OFFSET_ADDRESS_SIZE, mcnt);

#ifdef _LIBC
               DEBUG_PRINT3 ("  Setting %p to %d.\n", p + OFFSET_ADDRESS_SIZE,
			     mcnt);
#else
               DEBUG_PRINT3 ("  Setting 0x%x to %d.\n", p + OFFSET_ADDRESS_SIZE,
			     mcnt);
#endif /* _LIBC */
	       goto unconditional_jump;
            }
          /* If don't have to jump any more, skip over the rest of command.  */
	  else
	    p += 2 * OFFSET_ADDRESS_SIZE;
          break;

	case set_number_at:
	  {
            DEBUG_PRINT1 ("EXECUTING set_number_at.\n");

            EXTRACT_NUMBER_AND_INCR (mcnt, p);
            p1 = p + mcnt;
            EXTRACT_NUMBER_AND_INCR (mcnt, p);
#ifdef _LIBC
            DEBUG_PRINT3 ("  Setting %p to %d.\n", p1, mcnt);
#else
            DEBUG_PRINT3 ("  Setting 0x%x to %d.\n", p1, mcnt);
#endif
	    STORE_NUMBER (p1, mcnt);
            break;
          }

#if 0
	/* The DEC Alpha C compiler 3.x generates incorrect code for the
	   test  WORDCHAR_P (d - 1) != WORDCHAR_P (d)  in the expansion of
	   AT_WORD_BOUNDARY, so this code is disabled.  Expanding the
	   macro and introducing temporary variables works around the bug.  */

	case wordbound:
	  DEBUG_PRINT1 ("EXECUTING wordbound.\n");
	  if (AT_WORD_BOUNDARY (d))
	    break;
	  goto fail;

	case notwordbound:
	  DEBUG_PRINT1 ("EXECUTING notwordbound.\n");
	  if (AT_WORD_BOUNDARY (d))
	    goto fail;
	  break;
#else
	case wordbound:
	{
	  boolean prevchar, thischar;

	  DEBUG_PRINT1 ("EXECUTING wordbound.\n");
	  if (AT_STRINGS_BEG (d) || AT_STRINGS_END (d))
	    break;

	  prevchar = WORDCHAR_P (d - 1);
	  thischar = WORDCHAR_P (d);
	  if (prevchar != thischar)
	    break;
	  goto fail;
	}

      case notwordbound:
	{
	  boolean prevchar, thischar;

	  DEBUG_PRINT1 ("EXECUTING notwordbound.\n");
	  if (AT_STRINGS_BEG (d) || AT_STRINGS_END (d))
	    goto fail;

	  prevchar = WORDCHAR_P (d - 1);
	  thischar = WORDCHAR_P (d);
	  if (prevchar != thischar)
	    goto fail;
	  break;
	}
#endif

	case wordbeg:
          DEBUG_PRINT1 ("EXECUTING wordbeg.\n");
	  if (!AT_STRINGS_END (d) && WORDCHAR_P (d)
	      && (AT_STRINGS_BEG (d) || !WORDCHAR_P (d - 1)))
	    break;
          goto fail;

	case wordend:
          DEBUG_PRINT1 ("EXECUTING wordend.\n");
	  if (!AT_STRINGS_BEG (d) && WORDCHAR_P (d - 1)
              && (AT_STRINGS_END (d) || !WORDCHAR_P (d)))
	    break;
          goto fail;

#ifdef emacs
  	case before_dot:
          DEBUG_PRINT1 ("EXECUTING before_dot.\n");
 	  if (PTR_CHAR_POS ((unsigned char *) d) >= point)
  	    goto fail;
  	  break;

  	case at_dot:
          DEBUG_PRINT1 ("EXECUTING at_dot.\n");
 	  if (PTR_CHAR_POS ((unsigned char *) d) != point)
  	    goto fail;
  	  break;

  	case after_dot:
          DEBUG_PRINT1 ("EXECUTING after_dot.\n");
          if (PTR_CHAR_POS ((unsigned char *) d) <= point)
  	    goto fail;
  	  break;

	case syntaxspec:
          DEBUG_PRINT2 ("EXECUTING syntaxspec %d.\n", mcnt);
	  mcnt = *p++;
	  goto matchsyntax;

        case wordchar:
          DEBUG_PRINT1 ("EXECUTING Emacs wordchar.\n");
	  mcnt = (int) Sword;
        matchsyntax:
	  PREFETCH ();
	  /* Can't use *d++ here; SYNTAX may be an unsafe macro.  */
	  d++;
	  if (SYNTAX (d[-1]) != (enum syntaxcode) mcnt)
	    goto fail;
          SET_REGS_MATCHED ();
	  break;

	case notsyntaxspec:
          DEBUG_PRINT2 ("EXECUTING notsyntaxspec %d.\n", mcnt);
	  mcnt = *p++;
	  goto matchnotsyntax;

        case notwordchar:
          DEBUG_PRINT1 ("EXECUTING Emacs notwordchar.\n");
	  mcnt = (int) Sword;
        matchnotsyntax:
	  PREFETCH ();
	  /* Can't use *d++ here; SYNTAX may be an unsafe macro.  */
	  d++;
	  if (SYNTAX (d[-1]) == (enum syntaxcode) mcnt)
	    goto fail;
	  SET_REGS_MATCHED ();
          break;

#else /* not emacs */
	case wordchar:
          DEBUG_PRINT1 ("EXECUTING non-Emacs wordchar.\n");
	  PREFETCH ();
          if (!WORDCHAR_P (d))
            goto fail;
	  SET_REGS_MATCHED ();
          d++;
	  break;

	case notwordchar:
          DEBUG_PRINT1 ("EXECUTING non-Emacs notwordchar.\n");
	  PREFETCH ();
	  if (WORDCHAR_P (d))
            goto fail;
          SET_REGS_MATCHED ();
          d++;
	  break;
#endif /* not emacs */

        default:
          abort ();
	}
      continue;  /* Successfully executed one pattern command; keep going.  */


    /* We goto here if a matching operation fails. */
    fail:
      if (!FAIL_STACK_EMPTY ())
	{ /* A restart point is known.  Restore to that state.  */
          DEBUG_PRINT1 ("\nFAIL:\n");
          POP_FAILURE_POINT (d, p,
                             lowest_active_reg, highest_active_reg,
                             regstart, regend, reg_info);

          /* If this failure point is a dummy, try the next one.  */
          if (!p)
	    goto fail;

          /* If we failed to the end of the pattern, don't examine *p.  */
	  assert (p <= pend);
          if (p < pend)
            {
              boolean is_a_jump_n = false;

              /* If failed to a backwards jump that's part of a repetition
                 loop, need to pop this failure point and use the next one.  */
              switch ((re_opcode_t) *p)
                {
                case jump_n:
                  is_a_jump_n = true;
                case maybe_pop_jump:
                case pop_failure_jump:
                case jump:
                  p1 = p + 1;
                  EXTRACT_NUMBER_AND_INCR (mcnt, p1);
                  p1 += mcnt;

                  if ((is_a_jump_n && (re_opcode_t) *p1 == succeed_n)
                      || (!is_a_jump_n
                          && (re_opcode_t) *p1 == on_failure_jump))
                    goto fail;
                  break;
                default:
                  /* do nothing */ ;
                }
            }

          if (d >= string1 && d <= end1)
	    dend = end_match_1;
        }
      else
        break;   /* Matching at this starting point really fails.  */
    } /* for (;;) */

  if (best_regs_set)
    goto restore_best_regs;

  FREE_VARIABLES ();

  return -1;         			/* Failure to match.  */
} /* re_match_2 */

/* Subroutine definitions for re_match_2.  */


/* We are passed P pointing to a register number after a start_memory.

   Return true if the pattern up to the corresponding stop_memory can
   match the empty string, and false otherwise.

   If we find the matching stop_memory, sets P to point to one past its number.
   Otherwise, sets P to an undefined byte less than or equal to END.

   We don't handle duplicates properly (yet).  */

static boolean
PREFIX(group_match_null_string_p) (UCHAR_T **p, UCHAR_T *end,
                                   PREFIX(register_info_type) *reg_info)
{
  int mcnt;
  /* Point to after the args to the start_memory.  */
  UCHAR_T *p1 = *p + 2;

  while (p1 < end)
    {
      /* Skip over opcodes that can match nothing, and return true or
	 false, as appropriate, when we get to one that can't, or to the
         matching stop_memory.  */

      switch ((re_opcode_t) *p1)
        {
        /* Could be either a loop or a series of alternatives.  */
        case on_failure_jump:
          p1++;
          EXTRACT_NUMBER_AND_INCR (mcnt, p1);

          /* If the next operation is not a jump backwards in the
	     pattern.  */

	  if (mcnt >= 0)
	    {
              /* Go through the on_failure_jumps of the alternatives,
                 seeing if any of the alternatives cannot match nothing.
                 The last alternative starts with only a jump,
                 whereas the rest start with on_failure_jump and end
                 with a jump, e.g., here is the pattern for `a|b|c':

                 /on_failure_jump/0/6/exactn/1/a/jump_past_alt/0/6
                 /on_failure_jump/0/6/exactn/1/b/jump_past_alt/0/3
                 /exactn/1/c

                 So, we have to first go through the first (n-1)
                 alternatives and then deal with the last one separately.  */


              /* Deal with the first (n-1) alternatives, which start
                 with an on_failure_jump (see above) that jumps to right
                 past a jump_past_alt.  */

              while ((re_opcode_t) p1[mcnt-(1+OFFSET_ADDRESS_SIZE)] ==
		     jump_past_alt)
                {
                  /* `mcnt' holds how many bytes long the alternative
                     is, including the ending `jump_past_alt' and
                     its number.  */

                  if (!PREFIX(alt_match_null_string_p) (p1, p1 + mcnt -
						(1 + OFFSET_ADDRESS_SIZE),
						reg_info))
                    return false;

                  /* Move to right after this alternative, including the
		     jump_past_alt.  */
                  p1 += mcnt;

                  /* Break if it's the beginning of an n-th alternative
                     that doesn't begin with an on_failure_jump.  */
                  if ((re_opcode_t) *p1 != on_failure_jump)
                    break;

		  /* Still have to check that it's not an n-th
		     alternative that starts with an on_failure_jump.  */
		  p1++;
                  EXTRACT_NUMBER_AND_INCR (mcnt, p1);
                  if ((re_opcode_t) p1[mcnt-(1+OFFSET_ADDRESS_SIZE)] !=
		      jump_past_alt)
                    {
		      /* Get to the beginning of the n-th alternative.  */
                      p1 -= 1 + OFFSET_ADDRESS_SIZE;
                      break;
                    }
                }

              /* Deal with the last alternative: go back and get number
                 of the `jump_past_alt' just before it.  `mcnt' contains
                 the length of the alternative.  */
              EXTRACT_NUMBER (mcnt, p1 - OFFSET_ADDRESS_SIZE);

              if (!PREFIX(alt_match_null_string_p) (p1, p1 + mcnt, reg_info))
                return false;

              p1 += mcnt;	/* Get past the n-th alternative.  */
            } /* if mcnt > 0 */
          break;


        case stop_memory:
	  assert (p1[1] == **p);
          *p = p1 + 2;
          return true;


        default:
          if (!PREFIX(common_op_match_null_string_p) (&p1, end, reg_info))
            return false;
        }
    } /* while p1 < end */

  return false;
} /* group_match_null_string_p */


/* Similar to group_match_null_string_p, but doesn't deal with alternatives:
   It expects P to be the first byte of a single alternative and END one
   byte past the last. The alternative can contain groups.  */

static boolean
PREFIX(alt_match_null_string_p) (UCHAR_T *p, UCHAR_T *end,
                                 PREFIX(register_info_type) *reg_info)
{
  int mcnt;
  UCHAR_T *p1 = p;

  while (p1 < end)
    {
      /* Skip over opcodes that can match nothing, and break when we get
         to one that can't.  */

      switch ((re_opcode_t) *p1)
        {
	/* It's a loop.  */
        case on_failure_jump:
          p1++;
          EXTRACT_NUMBER_AND_INCR (mcnt, p1);
          p1 += mcnt;
          break;

	default:
          if (!PREFIX(common_op_match_null_string_p) (&p1, end, reg_info))
            return false;
        }
    }  /* while p1 < end */

  return true;
} /* alt_match_null_string_p */


/* Deals with the ops common to group_match_null_string_p and
   alt_match_null_string_p.

   Sets P to one after the op and its arguments, if any.  */

static boolean
PREFIX(common_op_match_null_string_p) (UCHAR_T **p, UCHAR_T *end,
                                       PREFIX(register_info_type) *reg_info)
{
  int mcnt;
  boolean ret;
  int reg_no;
  UCHAR_T *p1 = *p;

  switch ((re_opcode_t) *p1++)
    {
    case no_op:
    case begline:
    case endline:
    case begbuf:
    case endbuf:
    case wordbeg:
    case wordend:
    case wordbound:
    case notwordbound:
#ifdef emacs
    case before_dot:
    case at_dot:
    case after_dot:
#endif
      break;

    case start_memory:
      reg_no = *p1;
      assert (reg_no > 0 && reg_no <= MAX_REGNUM);
      ret = PREFIX(group_match_null_string_p) (&p1, end, reg_info);

      /* Have to set this here in case we're checking a group which
         contains a group and a back reference to it.  */

      if (REG_MATCH_NULL_STRING_P (reg_info[reg_no]) == MATCH_NULL_UNSET_VALUE)
        REG_MATCH_NULL_STRING_P (reg_info[reg_no]) = ret;

      if (!ret)
        return false;
      break;

    /* If this is an optimized succeed_n for zero times, make the jump.  */
    case jump:
      EXTRACT_NUMBER_AND_INCR (mcnt, p1);
      if (mcnt >= 0)
        p1 += mcnt;
      else
        return false;
      break;

    case succeed_n:
      /* Get to the number of times to succeed.  */
      p1 += OFFSET_ADDRESS_SIZE;
      EXTRACT_NUMBER_AND_INCR (mcnt, p1);

      if (mcnt == 0)
        {
          p1 -= 2 * OFFSET_ADDRESS_SIZE;
          EXTRACT_NUMBER_AND_INCR (mcnt, p1);
          p1 += mcnt;
        }
      else
        return false;
      break;

    case duplicate:
      if (!REG_MATCH_NULL_STRING_P (reg_info[*p1]))
        return false;
      break;

    case set_number_at:
      p1 += 2 * OFFSET_ADDRESS_SIZE;

    default:
      /* All other opcodes mean we cannot match the empty string.  */
      return false;
  }

  *p = p1;
  return true;
} /* common_op_match_null_string_p */


/* Return zero if TRANSLATE[S1] and TRANSLATE[S2] are identical for LEN
   bytes; nonzero otherwise.  */

static int
PREFIX(bcmp_translate) (const CHAR_T *s1, const CHAR_T *s2, register int len,
                        RE_TRANSLATE_TYPE translate)
{
  register const UCHAR_T *p1 = (const UCHAR_T *) s1;
  register const UCHAR_T *p2 = (const UCHAR_T *) s2;
  while (len)
    {
#ifdef WCHAR
      if (((*p1<=0xff)?translate[*p1++]:*p1++)
	  != ((*p2<=0xff)?translate[*p2++]:*p2++))
	return 1;
#else /* BYTE */
      if (translate[*p1++] != translate[*p2++]) return 1;
#endif /* WCHAR */
      len--;
    }
  return 0;
}


#else /* not INSIDE_RECURSION */

/* Entry points for GNU code.  */

/* re_compile_pattern is the GNU regular expression compiler: it
   compiles PATTERN (of length SIZE) and puts the result in BUFP.
   Returns 0 if the pattern was valid, otherwise an error string.

   Assumes the `allocated' (and perhaps `buffer') and `translate' fields
   are set in BUFP on entry.

   We call regex_compile to do the actual compilation.  */

const char *
re_compile_pattern (const char *pattern, size_t length,
                    struct re_pattern_buffer *bufp)
{
  reg_errcode_t ret;

  /* GNU code is written to assume at least RE_NREGS registers will be set
     (and at least one extra will be -1).  */
  bufp->regs_allocated = REGS_UNALLOCATED;

  /* And GNU code determines whether or not to get register information
     by passing null for the REGS argument to re_match, etc., not by
     setting no_sub.  */
  bufp->no_sub = 0;

  /* Match anchors at newline.  */
  bufp->newline_anchor = 1;

# ifdef MBS_SUPPORT
  if (MB_CUR_MAX != 1)
    ret = wcs_regex_compile (pattern, length, re_syntax_options, bufp);
  else
# endif
    ret = byte_regex_compile (pattern, length, re_syntax_options, bufp);

  if (!ret)
    return NULL;
  return gettext (re_error_msgid[(int) ret]);
}
#ifdef _LIBC
weak_alias (__re_compile_pattern, re_compile_pattern)
#endif

/* Entry points compatible with 4.2 BSD regex library.  We don't define
   them unless specifically requested.  */

#if defined _REGEX_RE_COMP || defined _LIBC

/* BSD has one and only one pattern buffer.  */
static struct re_pattern_buffer re_comp_buf;

char *
#ifdef _LIBC
/* Make these definitions weak in libc, so POSIX programs can redefine
   these names if they don't use our functions, and still use
   regcomp/regexec below without link errors.  */
weak_function
#endif
re_comp (const char *s)
{
  reg_errcode_t ret;

  if (!s)
    {
      if (!re_comp_buf.buffer)
	return (char *) gettext ("No previous regular expression");
      return 0;
    }

  if (!re_comp_buf.buffer)
    {
      re_comp_buf.buffer = (unsigned char *) malloc (200);
      if (re_comp_buf.buffer == NULL)
        return (char *) gettext (re_error_msgid[(int) REG_ESPACE]);
      re_comp_buf.allocated = 200;

      re_comp_buf.fastmap = (char *) malloc (1 << BYTEWIDTH);
      if (re_comp_buf.fastmap == NULL)
	return (char *) gettext (re_error_msgid[(int) REG_ESPACE]);
    }

  /* Since `re_exec' always passes NULL for the `regs' argument, we
     don't need to initialize the pattern buffer fields which affect it.  */

  /* Match anchors at newlines.  */
  re_comp_buf.newline_anchor = 1;

# ifdef MBS_SUPPORT
  if (MB_CUR_MAX != 1)
    ret = wcs_regex_compile (s, strlen (s), re_syntax_options, &re_comp_buf);
  else
# endif
    ret = byte_regex_compile (s, strlen (s), re_syntax_options, &re_comp_buf);

  if (!ret)
    return NULL;

  /* Yes, we're discarding `const' here if !HAVE_LIBINTL.  */
  return (char *) gettext (re_error_msgid[(int) ret]);
}


int
#ifdef _LIBC
weak_function
#endif
re_exec (const char *s)
{
  const int len = strlen (s);
  return
    0 <= re_search (&re_comp_buf, s, len, 0, len, (struct re_registers *) 0);
}

#endif /* _REGEX_RE_COMP */

/* POSIX.2 functions.  Don't define these for Emacs.  */

#ifndef emacs

/* regcomp takes a regular expression as a string and compiles it.

   PREG is a regex_t *.  We do not expect any fields to be initialized,
   since POSIX says we shouldn't.  Thus, we set

     `buffer' to the compiled pattern;
     `used' to the length of the compiled pattern;
     `syntax' to RE_SYNTAX_POSIX_EXTENDED if the
       REG_EXTENDED bit in CFLAGS is set; otherwise, to
       RE_SYNTAX_POSIX_BASIC;
     `newline_anchor' to REG_NEWLINE being set in CFLAGS;
     `fastmap' to an allocated space for the fastmap;
     `fastmap_accurate' to zero;
     `re_nsub' to the number of subexpressions in PATTERN.

   PATTERN is the address of the pattern string.

   CFLAGS is a series of bits which affect compilation.

     If REG_EXTENDED is set, we use POSIX extended syntax; otherwise, we
     use POSIX basic syntax.

     If REG_NEWLINE is set, then . and [^...] don't match newline.
     Also, regexec will try a match beginning after every newline.

     If REG_ICASE is set, then we considers upper- and lowercase
     versions of letters to be equivalent when matching.

     If REG_NOSUB is set, then when PREG is passed to regexec, that
     routine will report only success or failure, and nothing about the
     registers.

   It returns 0 if it succeeds, nonzero if it doesn't.  (See regex.h for
   the return codes and their meanings.)  */

int
regcomp (regex_t *preg, const char *pattern, int cflags)
{
  reg_errcode_t ret;
  reg_syntax_t syntax
    = (cflags & REG_EXTENDED) ?
      RE_SYNTAX_POSIX_EXTENDED : RE_SYNTAX_POSIX_BASIC;

  /* regex_compile will allocate the space for the compiled pattern.  */
  preg->buffer = 0;
  preg->allocated = 0;
  preg->used = 0;

  /* Try to allocate space for the fastmap.  */
  preg->fastmap = (char *) malloc (1 << BYTEWIDTH);

  if (cflags & REG_ICASE)
    {
      int i;

      preg->translate
	= (RE_TRANSLATE_TYPE) malloc (CHAR_SET_SIZE
				      * sizeof (*(RE_TRANSLATE_TYPE)0));
      if (preg->translate == NULL)
        return (int) REG_ESPACE;

      /* Map uppercase characters to corresponding lowercase ones.  */
      for (i = 0; i < CHAR_SET_SIZE; i++)
        preg->translate[i] = ISUPPER (i) ? TOLOWER (i) : i;
    }
  else
    preg->translate = NULL;

  /* If REG_NEWLINE is set, newlines are treated differently.  */
  if (cflags & REG_NEWLINE)
    { /* REG_NEWLINE implies neither . nor [^...] match newline.  */
      syntax &= ~RE_DOT_NEWLINE;
      syntax |= RE_HAT_LISTS_NOT_NEWLINE;
      /* It also changes the matching behavior.  */
      preg->newline_anchor = 1;
    }
  else
    preg->newline_anchor = 0;

  preg->no_sub = !!(cflags & REG_NOSUB);

  /* POSIX says a null character in the pattern terminates it, so we
     can use strlen here in compiling the pattern.  */
# ifdef MBS_SUPPORT
  if (MB_CUR_MAX != 1)
    ret = wcs_regex_compile (pattern, strlen (pattern), syntax, preg);
  else
# endif
    ret = byte_regex_compile (pattern, strlen (pattern), syntax, preg);

  /* POSIX doesn't distinguish between an unmatched open-group and an
     unmatched close-group: both are REG_EPAREN.  */
  if (ret == REG_ERPAREN) ret = REG_EPAREN;

  if (ret == REG_NOERROR && preg->fastmap)
    {
      /* Compute the fastmap now, since regexec cannot modify the pattern
	 buffer.  */
      if (re_compile_fastmap (preg) == -2)
	{
	  /* Some error occurred while computing the fastmap, just forget
	     about it.  */
	  free (preg->fastmap);
	  preg->fastmap = NULL;
	}
    }

  return (int) ret;
}
#ifdef _LIBC
weak_alias (__regcomp, regcomp)
#endif


/* regexec searches for a given pattern, specified by PREG, in the
   string STRING.

   If NMATCH is zero or REG_NOSUB was set in the cflags argument to
   `regcomp', we ignore PMATCH.  Otherwise, we assume PMATCH has at
   least NMATCH elements, and we set them to the offsets of the
   corresponding matched substrings.

   EFLAGS specifies `execution flags' which affect matching: if
   REG_NOTBOL is set, then ^ does not match at the beginning of the
   string; if REG_NOTEOL is set, then $ does not match at the end.

   We return 0 if we find a match and REG_NOMATCH if not.  */

int
regexec (const regex_t *preg, const char *string, size_t nmatch,
         regmatch_t pmatch[], int eflags)
{
  int ret;
  struct re_registers regs;
  regex_t private_preg;
  int len = strlen (string);
  boolean want_reg_info = !preg->no_sub && nmatch > 0;

  private_preg = *preg;

  private_preg.not_bol = !!(eflags & REG_NOTBOL);
  private_preg.not_eol = !!(eflags & REG_NOTEOL);

  /* The user has told us exactly how many registers to return
     information about, via `nmatch'.  We have to pass that on to the
     matching routines.  */
  private_preg.regs_allocated = REGS_FIXED;

  if (want_reg_info)
    {
      regs.num_regs = nmatch;
      regs.start = TALLOC (nmatch * 2, regoff_t);
      if (regs.start == NULL)
        return (int) REG_NOMATCH;
      regs.end = regs.start + nmatch;
    }

  /* Perform the searching operation.  */
  ret = re_search (&private_preg, string, len,
                   /* start: */ 0, /* range: */ len,
                   want_reg_info ? &regs : (struct re_registers *) 0);

  /* Copy the register information to the POSIX structure.  */
  if (want_reg_info)
    {
      if (ret >= 0)
        {
          unsigned r;

          for (r = 0; r < nmatch; r++)
            {
              pmatch[r].rm_so = regs.start[r];
              pmatch[r].rm_eo = regs.end[r];
            }
        }

      /* If we needed the temporary register info, free the space now.  */
      free (regs.start);
    }

  /* We want zero return to mean success, unlike `re_search'.  */
  return ret >= 0 ? (int) REG_NOERROR : (int) REG_NOMATCH;
}
#ifdef _LIBC
weak_alias (__regexec, regexec)
#endif


/* Returns a message corresponding to an error code, ERRCODE, returned
   from either regcomp or regexec.   We don't use PREG here.  */

size_t
regerror (int errcode, const regex_t *preg ATTRIBUTE_UNUSED,
          char *errbuf, size_t errbuf_size)
{
  const char *msg;
  size_t msg_size;

  if (errcode < 0
      || errcode >= (int) (sizeof (re_error_msgid)
			   / sizeof (re_error_msgid[0])))
    /* Only error codes returned by the rest of the code should be passed
       to this routine.  If we are given anything else, or if other regex
       code generates an invalid error code, then the program has a bug.
       Dump core so we can fix it.  */
    abort ();

  msg = gettext (re_error_msgid[errcode]);

  msg_size = strlen (msg) + 1; /* Includes the null.  */

  if (errbuf_size != 0)
    {
      if (msg_size > errbuf_size)
        {
#if defined HAVE_MEMPCPY || defined _LIBC
	  *((char *) mempcpy (errbuf, msg, errbuf_size - 1)) = '\0';
#else
          memcpy (errbuf, msg, errbuf_size - 1);
          errbuf[errbuf_size - 1] = 0;
#endif
        }
      else
        memcpy (errbuf, msg, msg_size);
    }

  return msg_size;
}
#ifdef _LIBC
weak_alias (__regerror, regerror)
#endif


/* Free dynamically allocated space used by PREG.  */

void
regfree (regex_t *preg)
{
  if (preg->buffer != NULL)
    free (preg->buffer);
  preg->buffer = NULL;

  preg->allocated = 0;
  preg->used = 0;

  if (preg->fastmap != NULL)
    free (preg->fastmap);
  preg->fastmap = NULL;
  preg->fastmap_accurate = 0;

  if (preg->translate != NULL)
    free (preg->translate);
  preg->translate = NULL;
}
#ifdef _LIBC
weak_alias (__regfree, regfree)
#endif

#endif /* not emacs  */

#endif /* not INSIDE_RECURSION */


#undef STORE_NUMBER
#undef STORE_NUMBER_AND_INCR
#undef EXTRACT_NUMBER
#undef EXTRACT_NUMBER_AND_INCR

#undef DEBUG_PRINT_COMPILED_PATTERN
#undef DEBUG_PRINT_DOUBLE_STRING

#undef INIT_FAIL_STACK
#undef RESET_FAIL_STACK
#undef DOUBLE_FAIL_STACK
#undef PUSH_PATTERN_OP
#undef PUSH_FAILURE_POINTER
#undef PUSH_FAILURE_INT
#undef PUSH_FAILURE_ELT
#undef POP_FAILURE_POINTER
#undef POP_FAILURE_INT
#undef POP_FAILURE_ELT
#undef DEBUG_PUSH
#undef DEBUG_POP
#undef PUSH_FAILURE_POINT
#undef POP_FAILURE_POINT

#undef REG_UNSET_VALUE
#undef REG_UNSET

#undef PATFETCH
#undef PATFETCH_RAW
#undef PATUNFETCH
#undef TRANSLATE

#undef INIT_BUF_SIZE
#undef GET_BUFFER_SPACE
#undef BUF_PUSH
#undef BUF_PUSH_2
#undef BUF_PUSH_3
#undef STORE_JUMP
#undef STORE_JUMP2
#undef INSERT_JUMP
#undef INSERT_JUMP2
#undef EXTEND_BUFFER
#undef GET_UNSIGNED_NUMBER
#undef FREE_STACK_RETURN

# undef POINTER_TO_OFFSET
# undef MATCHING_IN_FRST_STRING
# undef PREFETCH
# undef AT_STRINGS_BEG
# undef AT_STRINGS_END
# undef WORDCHAR_P
# undef FREE_VAR
# undef FREE_VARIABLES
# undef NO_HIGHEST_ACTIVE_REG
# undef NO_LOWEST_ACTIVE_REG

# undef CHAR_T
# undef UCHAR_T
# undef COMPILED_BUFFER_VAR
# undef OFFSET_ADDRESS_SIZE
# undef CHAR_CLASS_SIZE
# undef PREFIX
# undef ARG_PREFIX
# undef PUT_CHAR
# undef BYTE
# undef WCHAR

# define DEFINED_ONCE
