/* Stub functions.
   Copyright (C) 2006 Free Software Foundation, Inc.

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

#ifdef L_register_frame_info
struct object;
void  __register_frame_info (const void * __attribute__((unused)),
			     struct object * __attribute__((unused)));
void
__register_frame_info (const void *p, struct object *ob)
{
}
#endif

#ifdef L_deregister_frame_info
void *__deregister_frame_info (const void * __attribute__((unused)));
void *
__deregister_frame_info (const void *p)
{
  return (void *)0;
}
#endif

#ifdef L_cxa_finalize
void __cxa_finalize (void * __attribute__((unused)));
void
__cxa_finalize (void *p)
{
}
#endif

#ifdef L_Jv_RegisterClasses
void _Jv_RegisterClasses (void * __attribute__((unused)));
void
_Jv_RegisterClasses (void *p)
{
}
#endif
