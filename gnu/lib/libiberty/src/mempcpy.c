/* Implement the mempcpy function.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Written by Kaveh R. Ghazi <ghazi@caip.rutgers.edu>.

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

/*

@deftypefn Supplemental void* mempcpy (void *@var{out}, const void *@var{in}, size_t @var{length})

Copies @var{length} bytes from memory region @var{in} to region
@var{out}.  Returns a pointer to @var{out} + @var{length}.

@end deftypefn

*/

#include <ansidecl.h>
#include <stddef.h>

extern PTR memcpy (PTR, const PTR, size_t);

PTR
mempcpy (PTR dst, const PTR src, size_t len)
{
  return (char *) memcpy (dst, src, len) + len;
}
