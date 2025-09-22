/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
   more details.

   You should have received a copy of the GNU Lesser General Public License 
   along with libgomp; see the file COPYING.LIB.  If not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files, some
   of which are compiled with GCC, to produce an executable, this library
   does not by itself cause the resulting executable to be covered by the
   GNU General Public License.  This exception does not however invalidate
   any other reasons why the executable file might be covered by the GNU
   General Public License.  */

/* This file contains routines used to signal errors.  Most places in the
   OpenMP API do not make any provision for failure, so we can't just
   defer the decision on reporting the problem to the user; we must do it
   ourselves or not at all.  */
/* ??? Is this about what other implementations do?  Assume stderr hasn't
   been pointed somewhere unsafe?  */

#include "libgomp.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


static void
gomp_verror (const char *fmt, va_list list)
{
  fputs ("\nlibgomp: ", stderr);
  vfprintf (stderr, fmt, list);
  fputc ('\n', stderr);
}

void
gomp_error (const char *fmt, ...)
{
  va_list list;

  va_start (list, fmt);
  gomp_verror (fmt, list);
  va_end (list);
}

void
gomp_fatal (const char *fmt, ...)
{
  va_list list;

  va_start (list, fmt);
  gomp_verror (fmt, list);
  va_end (list);

  exit (EXIT_FAILURE);
}
