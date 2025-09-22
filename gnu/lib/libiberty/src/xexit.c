/* xexit.c -- Run any exit handlers, then exit.
   Copyright (C) 1994, 95, 1997 Free Software Foundation, Inc.

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
License along with libiberty; see the file COPYING.LIB.  If not, write
to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

/*

@deftypefn Replacement void xexit (int @var{code})

Terminates the program.  If any functions have been registered with
the @code{xatexit} replacement function, they will be called first.
Termination is handled via the system's normal @code{exit} call.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include "libiberty.h"


/* This variable is set by xatexit if it is called.  This way, xmalloc
   doesn't drag xatexit into the link.  */
void (*_xexit_cleanup) (void);

void
xexit (int code)
{
  if (_xexit_cleanup != NULL)
    (*_xexit_cleanup) ();
  exit (code);
}
