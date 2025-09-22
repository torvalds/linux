
/* Install modified versions of certain ANSI-incompatible system header
   files which are fixed to work correctly with ANSI C and placed in a
   directory that GCC will search.

   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2004
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifndef GCC_FIXLIB_H
#define GCC_FIXLIB_H

#include "config.h"
#include "system.h"
#include <signal.h>

#include "xregex.h"
#include "libiberty.h"

#ifndef STDIN_FILENO
# define STDIN_FILENO   0
#endif
#ifndef STDOUT_FILENO
# define STDOUT_FILENO  1
#endif

#if ! defined( SIGCHLD ) && defined( SIGCLD )
#  define SIGCHLD SIGCLD
#endif

#ifndef SIGQUIT
#define SIGQUIT SIGTERM
#endif
#ifndef SIGIOT
#define SIGIOT SIGTERM
#endif
#ifndef SIGPIPE
#define SIGPIPE SIGTERM
#endif
#ifndef SIGALRM
#define SIGALRM SIGTERM
#endif
#ifndef SIGKILL
#define SIGKILL SIGTERM
#endif

typedef int t_success;

#define FAILURE         (-1)
#define SUCCESS           0
#define PROBLEM           1

#define SUCCEEDED(p)    ((p) == SUCCESS)
#define SUCCESSFUL(p)   SUCCEEDED (p)
#define FAILED(p)       ((p) < SUCCESS)
#define HADGLITCH(p)    ((p) > SUCCESS)

#ifndef DEBUG
# define STATIC static
#else
# define STATIC
#endif

#define tSCC static const char
#define tCC  const char
#define tSC  static char

/* If this particular system's header files define the macro `MAXPATHLEN',
   we happily take advantage of it; otherwise we use a value which ought
   to be large enough.  */
#ifndef MAXPATHLEN
# define MAXPATHLEN     4096
#endif

#ifndef EXIT_SUCCESS
# define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
# define EXIT_FAILURE 1
#endif

#define EXIT_BROKEN  3

#define NUL             '\0'

#ifndef NOPROCESS
#define NOPROCESS	((pid_t) -1)
#define NULLPROCESS	((pid_t)0)

#define EXIT_PANIC	99
#endif /* NOPROCESS */

#define IGNORE_ARG(a)   ((void)(a))

typedef enum t_bool
{
  BOOL_FALSE, BOOL_TRUE
} t_bool;

typedef int apply_fix_p_t;  /* Apply Fix Predicate Type */

#define APPLY_FIX 0
#define SKIP_FIX  1

#define ENV_TABLE                                    \
  _ENV_( pz_machine,   BOOL_TRUE, "TARGET_MACHINE",  \
         "output from config.guess" )                \
                                                     \
  _ENV_( pz_orig_dir,  BOOL_TRUE, "ORIGDIR",         \
         "directory of fixincl and applyfix" )       \
                                                     \
  _ENV_( pz_src_dir,   BOOL_TRUE, "SRCDIR",          \
         "directory of original files" )             \
                                                     \
  _ENV_( pz_input_dir, BOOL_TRUE, "INPUT",           \
         "current directory for fixincl" )           \
                                                     \
  _ENV_( pz_dest_dir,  BOOL_TRUE, "DESTDIR",         \
         "output directory" )                        \
                                                     \
  _ENV_( pz_mn_name_pat, BOOL_FALSE, "MN_NAME_PAT",  \
         "regex matching forbidden identifiers" )    \
                                                     \
  _ENV_( pz_verbose,  BOOL_FALSE, "VERBOSE",         \
         "amount of user entertainment" )            \
                                                     \
  _ENV_( pz_find_base, BOOL_TRUE, "FIND_BASE",       \
         "leader to trim from file names" )

#define _ENV_(v,m,n,t)   extern tCC* v;
ENV_TABLE
#undef _ENV_

/*  Test Descriptor

    Each fix may have associated tests that determine
    whether the fix needs to be applied or not.
    Each test has a type (from the te_test_type enumeration);
    associated test text; and, if the test is TT_EGREP or
    the negated form TT_NEGREP, a pointer to the compiled
    version of the text string.

    */
typedef enum
{
  TT_TEST, TT_EGREP, TT_NEGREP, TT_FUNCTION
} te_test_type;

typedef struct test_desc tTestDesc;

struct test_desc
{
  te_test_type type;
  const char *pz_test_text;
  regex_t *p_test_regex;
};

typedef struct patch_desc tPatchDesc;

/*  Fix Descriptor

    Everything you ever wanted to know about how to apply
    a particular fix (which files, how to qualify them,
    how to actually make the fix, etc...)

    NB:  the FD_ defines are BIT FLAGS, even though
         some are mutually exclusive

    */
#define FD_MACH_ONLY      0x0000
#define FD_MACH_IFNOT     0x0001
#define FD_SHELL_SCRIPT   0x0002
#define FD_SUBROUTINE     0x0004
#define FD_REPLACEMENT    0x0008
#define FD_SKIP_TEST      0x8000

typedef struct fix_desc tFixDesc;
struct fix_desc
{
  tCC*        fix_name;       /* Name of the fix */
  tCC*        file_list;      /* List of files it applies to */
  tCC**       papz_machs;     /* List of machine/os-es it applies to */
  int         test_ct;
  int         fd_flags;
  tTestDesc*  p_test_desc;
  tCC**       patch_args;
  long        unused;
};

typedef struct {
  int         type_name_len;
  tCC*        pz_type;
  tCC*        pz_TYPE;
  tCC*        pz_gtype;
} t_gnu_type_map;

extern int gnu_type_map_ct;

#ifdef HAVE_MMAP_FILE
#define UNLOAD_DATA() do { if (curr_data_mapped) { \
  munmap ((void*)pz_curr_data, data_map_size); close (data_map_fd); } \
  else free ((void*)pz_curr_data); } while(0)
#else
#define UNLOAD_DATA() free ((void*)pz_curr_data)
#endif

/*
 *  Exported procedures
 */
char * load_file_data ( FILE* fp );

#ifdef IS_CXX_HEADER_NEEDED
t_bool is_cxx_header ( tCC* filename, tCC* filetext );
#endif /* IS_CXX_HEADER_NEEDED */

#ifdef SKIP_QUOTE_NEEDED
tCC*   skip_quote ( char  q, char* text );
#endif

void   compile_re ( tCC* pat, regex_t* re, int match, tCC *e1, tCC *e2 );

void   apply_fix ( tFixDesc* p_fixd, tCC* filname );
apply_fix_p_t
       run_test ( tCC* t_name, tCC* f_name, tCC* text );

#ifdef SEPARATE_FIX_PROC
char*  make_raw_shell_str ( char* pz_d, tCC* pz_s, size_t smax );
#endif

t_bool mn_get_regexps ( regex_t** label_re, regex_t** name_re, tCC *who );

void   initialize_opts ( void );
#endif /* ! GCC_FIXLIB_H */
