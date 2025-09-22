/* tilde.h: Externally available variables and function in libtilde.a. */

/* Copyright (C) 1992 Free Software Foundation, Inc.

   This file contains the Readline Library (the Library), a set of
   routines for providing Emacs style line input to programs that ask
   for it.

   The Library is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The Library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */

#if !defined (_TILDE_H_)
#  define _TILDE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* A function can be defined using prototypes and compile on both ANSI C
   and traditional C compilers with something like this:
	extern char *func PARAMS((char *, char *, int)); */

#if !defined (PARAMS)
#  if defined (__STDC__) || defined (__GNUC__) || defined (__cplusplus)
#    define PARAMS(protos) protos
#  else
#    define PARAMS(protos) ()
#  endif
#endif

typedef char *tilde_hook_func_t PARAMS((char *));

/* If non-null, this contains the address of a function that the application
   wants called before trying the standard tilde expansions.  The function
   is called with the text sans tilde, and returns a malloc()'ed string
   which is the expansion, or a NULL pointer if the expansion fails. */
extern tilde_hook_func_t *tilde_expansion_preexpansion_hook;

/* If non-null, this contains the address of a function to call if the
   standard meaning for expanding a tilde fails.  The function is called
   with the text (sans tilde, as in "foo"), and returns a malloc()'ed string
   which is the expansion, or a NULL pointer if there is no expansion. */
extern tilde_hook_func_t *tilde_expansion_failure_hook;

/* When non-null, this is a NULL terminated array of strings which
   are duplicates for a tilde prefix.  Bash uses this to expand
   `=~' and `:~'. */
extern char **tilde_additional_prefixes;

/* When non-null, this is a NULL terminated array of strings which match
   the end of a username, instead of just "/".  Bash sets this to
   `:' and `=~'. */
extern char **tilde_additional_suffixes;

/* Return a new string which is the result of tilde expanding STRING. */
extern char *tilde_expand PARAMS((const char *));

/* Do the work of tilde expansion on FILENAME.  FILENAME starts with a
   tilde.  If there is no expansion, call tilde_expansion_failure_hook. */
extern char *tilde_expand_word PARAMS((const char *));

#ifdef __cplusplus
}
#endif

#endif /* _TILDE_H_ */
