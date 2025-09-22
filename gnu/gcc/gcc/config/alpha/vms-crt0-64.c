/* VMS 64bit crt0 returning VMS style condition codes .
   Copyright (C) 2001 Free Software Foundation, Inc.
   Contributed by Douglas B. Rupp (rupp@gnat.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#if !defined(__DECC)
You Lose! This file can only be compiled with DEC C.
#else

/* This file can only be compiled with DEC C, due to the call to
   lib$establish and the pragmas pointer_size.  */

#pragma __pointer_size short

#include <stdlib.h>
#include <string.h>
#include <ssdef.h>

extern void decc$main ();

extern int main ();

static int
handler (sigargs, mechargs)
     void *sigargs;
     void *mechargs;
{
  return SS$_RESIGNAL;
}

int
__main (arg1, arg2, arg3, image_file_desc, arg5, arg6)
     void *arg1, *arg2, *arg3;
     void *image_file_desc;
     void *arg5, *arg6;
{
  int argc;
  char **argv;
  char **envp;

#pragma __pointer_size long

  int i;
  char **long_argv;
  char **long_envp;

#pragma __pointer_size short

  lib$establish (handler);
  decc$main (arg1, arg2, arg3, image_file_desc,
	     arg5, arg6, &argc, &argv, &envp);

#pragma __pointer_size long

  /* Reallocate argv with 64 bit pointers.  */
  long_argv = (char **) malloc (sizeof (char *) * (argc + 1));

  for (i = 0; i < argc; i++)
    long_argv[i] = strdup (argv[i]);

  long_argv[argc] = (char *) 0;

  long_envp = (char **) malloc (sizeof (char *) * 5);

  for (i = 0; envp[i]; i++)
    long_envp[i] = strdup (envp[i]);

  long_envp[i] = (char *) 0;

#pragma __pointer_size short

  return main (argc, long_argv, long_envp);
}
#endif
