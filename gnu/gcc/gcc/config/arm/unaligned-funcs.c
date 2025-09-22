/* EABI unaligned read/write functions.

   Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by CodeSourcery, LLC.

   This file is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option) any
   later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combine
   executable.)

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

int __aeabi_uread4 (void *);
int __aeabi_uwrite4 (int, void *);
long long __aeabi_uread8 (void *);
long long __aeabi_uwrite8 (long long, void *);

struct __attribute__((packed)) u4 { int data; };
struct __attribute__((packed)) u8 { long long data; };

int
__aeabi_uread4 (void *ptr)
{
  return ((struct u4 *) ptr)->data;
}

int
__aeabi_uwrite4 (int data, void *ptr)
{
  ((struct u4 *) ptr)->data = data;
  return data;
}

long long
__aeabi_uread8 (void *ptr)
{
  return ((struct u8 *) ptr)->data;
}

long long
__aeabi_uwrite8 (long long data, void *ptr)
{
  ((struct u8 *) ptr)->data = data;
  return data;
}
