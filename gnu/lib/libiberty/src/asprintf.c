/* Like sprintf but provides a pointer to malloc'd storage, which must
   be freed by the caller.
   Copyright (C) 1997, 2003 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions.

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
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"

#include <stdarg.h>

/*

@deftypefn Extension int asprintf (char **@var{resptr}, const char *@var{format}, ...)

Like @code{sprintf}, but instead of passing a pointer to a buffer, you
pass a pointer to a pointer.  This function will compute the size of
the buffer needed, allocate memory with @code{malloc}, and store a
pointer to the allocated memory in @code{*@var{resptr}}.  The value
returned is the same as @code{sprintf} would return.  If memory could
not be allocated, minus one is returned and @code{NULL} is stored in
@code{*@var{resptr}}.

@end deftypefn

*/

int
asprintf (char **buf, const char *fmt, ...)
{
  int status;
  VA_OPEN (ap, fmt);
  VA_FIXEDARG (ap, char **, buf);
  VA_FIXEDARG (ap, const char *, fmt);
  status = vasprintf (buf, fmt, ap);
  VA_CLOSE (ap);
  return status;
}
