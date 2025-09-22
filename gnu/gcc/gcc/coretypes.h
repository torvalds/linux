/* GCC core type declarations.
   Copyright (C) 2002, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* Provide forward declarations of core types which are referred to by
   most of the compiler.  This allows header files to use these types
   (e.g. in function prototypes) without concern for whether the full
   definitions are visible.  Some other declarations that need to be
   universally visible are here, too.

   In the context of tconfig.h, most of these have special definitions
   which prevent them from being used except in further type
   declarations.  This is a kludge; the right thing is to avoid
   including the "tm.h" header set in the context of tconfig.h, but
   we're not there yet.  */

#ifndef GCC_CORETYPES_H
#define GCC_CORETYPES_H

#define GTY(x)  /* nothing - marker for gengtype */

#ifndef USED_FOR_TARGET

struct bitmap_head_def;
typedef struct bitmap_head_def *bitmap;
struct rtx_def;
typedef struct rtx_def *rtx;
struct rtvec_def;
typedef struct rtvec_def *rtvec;
union tree_node;
typedef union tree_node *tree;
union section;
typedef union section section;

/* Provide forward struct declaration so that we don't have to include
   all of cpplib.h whenever a random prototype includes a pointer.
   Note that the cpp_reader typedef remains part of cpplib.h.  */

struct cpp_reader;

/* The thread-local storage model associated with a given VAR_DECL
   or SYMBOL_REF.  This isn't used much, but both trees and RTL refer
   to it, so it's here.  */
enum tls_model {
  TLS_MODEL_NONE,
  TLS_MODEL_GLOBAL_DYNAMIC,
  TLS_MODEL_LOCAL_DYNAMIC,
  TLS_MODEL_INITIAL_EXEC,
  TLS_MODEL_LOCAL_EXEC
};

#else

struct _dont_use_rtx_here_;
struct _dont_use_rtvec_here_;
union _dont_use_tree_here_;
#define rtx struct _dont_use_rtx_here_ *
#define rtvec struct _dont_use_rtvec_here *
#define tree union _dont_use_tree_here_ *

#endif

#endif /* coretypes.h */

