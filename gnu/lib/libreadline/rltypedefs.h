/* rltypedefs.h -- Type declarations for readline functions. */

/* Copyright (C) 2000 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library, a library for
   reading lines of text with interactive input and history editing.

   The GNU Readline Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The GNU Readline Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */

#ifndef _RL_TYPEDEFS_H_
#define _RL_TYPEDEFS_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Old-style */

#if !defined (_FUNCTION_DEF)
#  define _FUNCTION_DEF

typedef int Function ();
typedef void VFunction ();
typedef char *CPFunction ();
typedef char **CPPFunction ();

#endif /* _FUNCTION_DEF */

/* New style. */

#if !defined (_RL_FUNCTION_TYPEDEF)
#  define _RL_FUNCTION_TYPEDEF

/* Bindable functions */
typedef int rl_command_func_t PARAMS((int, int));

/* Typedefs for the completion system */
typedef char *rl_compentry_func_t PARAMS((const char *, int));
typedef char **rl_completion_func_t PARAMS((const char *, int, int));

typedef char *rl_quote_func_t PARAMS((char *, int, char *));
typedef char *rl_dequote_func_t PARAMS((char *, int));

typedef int rl_compignore_func_t PARAMS((char **));

typedef void rl_compdisp_func_t PARAMS((char **, int, int));

/* Type for input and pre-read hook functions like rl_event_hook */
typedef int rl_hook_func_t PARAMS((void));

/* Input function type */
typedef int rl_getc_func_t PARAMS((FILE *));

/* Generic function that takes a character buffer (which could be the readline
   line buffer) and an index into it (which could be rl_point) and returns
   an int. */
typedef int rl_linebuf_func_t PARAMS((char *, int));

/* `Generic' function pointer typedefs */
typedef int rl_intfunc_t PARAMS((int));
#define rl_ivoidfunc_t rl_hook_func_t
typedef int rl_icpfunc_t PARAMS((char *));
typedef int rl_icppfunc_t PARAMS((char **));

typedef void rl_voidfunc_t PARAMS((void));
typedef void rl_vintfunc_t PARAMS((int));
typedef void rl_vcpfunc_t PARAMS((char *));
typedef void rl_vcppfunc_t PARAMS((char **));
#endif /* _RL_FUNCTION_TYPEDEF */

#ifdef __cplusplus
}
#endif

#endif /* _RL_TYPEDEFS_H_ */
