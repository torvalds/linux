/* Utilities to execute a program in a subprocess (possibly linked by pipes
   with other subprocesses), and wait for it.  Shared logic.
   Copyright (C) 1996, 1997, 1998, 1999, 2000, 2001, 2003, 2004
   Free Software Foundation, Inc.

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If not,
write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifndef PEX_COMMON_H
#define PEX_COMMON_H

#include "config.h"
#include "libiberty.h"
#include <stdio.h>

#define install_error_msg "installation problem, cannot exec `%s'"

/* stdin file number.  */
#define STDIN_FILE_NO 0

/* stdout file number.  */
#define STDOUT_FILE_NO 1

/* stderr file number.  */
#define STDERR_FILE_NO 2

/* value of `pipe': port index for reading.  */
#define READ_PORT 0

/* value of `pipe': port index for writing.  */
#define WRITE_PORT 1

/* The structure used by pex_init and friends.  */

struct pex_obj
{
  /* Flags.  */
  int flags;
  /* Name of calling program, for error messages.  */
  const char *pname;
  /* Base name to use for temporary files.  */
  const char *tempbase;
  /* Pipe to use as stdin for next process.  */
  int next_input;
  /* File name to use as stdin for next process.  */
  char *next_input_name;
  /* Whether next_input_name was allocated using malloc.  */
  int next_input_name_allocated;
  /* Number of child processes.  */
  int count;
  /* PIDs of child processes; array allocated using malloc.  */
  long *children;
  /* Exit statuses of child processes; array allocated using malloc.  */
  int *status;
  /* Time used by child processes; array allocated using malloc.  */
  struct pex_time *time;
  /* Number of children we have already waited for.  */
  int number_waited;
  /* FILE created by pex_input_file.  */
  FILE *input_file;
  /* FILE created by pex_read_output.  */
  FILE *read_output;
  /* Number of temporary files to remove.  */
  int remove_count;
  /* List of temporary files to remove; array allocated using malloc
     of strings allocated using malloc.  */
  char **remove;
  /* Pointers to system dependent functions.  */
  const struct pex_funcs *funcs;
  /* For use by system dependent code.  */
  void *sysdep;
};

/* Functions passed to pex_run_common.  */

struct pex_funcs
{
  /* Open file NAME for reading.  If BINARY is non-zero, open in
     binary mode.  Return >= 0 on success, -1 on error.  */
  int (*open_read) (struct pex_obj *, const char */* name */, int /* binary */);
  /* Open file NAME for writing.  If BINARY is non-zero, open in
     binary mode.  Return >= 0 on success, -1 on error.  */
  int (*open_write) (struct pex_obj *, const char */* name */,
                     int /* binary */);
  /* Execute a child process.  FLAGS, EXECUTABLE, ARGV, ERR are from
     pex_run.  IN, OUT, ERRDES, TOCLOSE are all descriptors, from
     open_read, open_write, or pipe, or they are one of STDIN_FILE_NO,
     STDOUT_FILE_NO or STDERR_FILE_NO; if IN, OUT, and ERRDES are not
     STD*_FILE_NO, they should be closed.  If the descriptor TOCLOSE
     is not -1, and the system supports pipes, TOCLOSE should be
     closed in the child process.  The function should handle the
     PEX_STDERR_TO_STDOUT flag.  Return >= 0 on success, or -1 on
     error and set *ERRMSG and *ERR.  */
  long (*exec_child) (struct pex_obj *, int /* flags */,
                      const char */* executable */, char * const * /* argv */,
                      char * const * /* env */,
                      int /* in */, int /* out */, int /* errdes */,
		      int /* toclose */, const char **/* errmsg */,
		      int */* err */);
  /* Close a descriptor.  Return 0 on success, -1 on error.  */
  int (*close) (struct pex_obj *, int);
  /* Wait for a child to complete, returning exit status in *STATUS
     and time in *TIME (if it is not null).  CHILD is from fork.  DONE
     is 1 if this is called via pex_free.  ERRMSG and ERR are as in
     fork.  Return 0 on success, -1 on error.  */
  int (*wait) (struct pex_obj *, long /* child */, int * /* status */,
               struct pex_time * /* time */, int /* done */,
               const char ** /* errmsg */, int * /* err */);
  /* Create a pipe (only called if PEX_USE_PIPES is set) storing two
     descriptors in P[0] and P[1].  If BINARY is non-zero, open in
     binary mode.  Return 0 on success, -1 on error.  */
  int (*pipe) (struct pex_obj *, int * /* p */, int /* binary */);
  /* Get a FILE pointer to read from a file descriptor (only called if
     PEX_USE_PIPES is set).  If BINARY is non-zero, open in binary
     mode.  Return pointer on success, NULL on error.  */
  FILE * (*fdopenr) (struct pex_obj *, int /* fd */, int /* binary */);
  /* Get a FILE pointer to write to the file descriptor FD (only
     called if PEX_USE_PIPES is set).  If BINARY is non-zero, open in
     binary mode.  Arrange for FD not to be inherited by the child
     processes.  Return pointer on success, NULL on error.  */
  FILE * (*fdopenw) (struct pex_obj *, int /* fd */, int /* binary */);
  /* Free any system dependent data associated with OBJ.  May be
     NULL if there is nothing to do.  */
  void (*cleanup) (struct pex_obj *);
};

extern struct pex_obj *pex_init_common (int, const char *, const char *,
					const struct pex_funcs *);

#endif
