/* Functions shipped in the ppc64 and x86_64 version of libgcc_s.1.dylib
   in older Mac OS X versions, preserved for backwards compatibility.
   Copyright (C) 2006  Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#if defined (__ppc64__) || defined (__x86_64__)
/* Many of these functions have probably never been used by anyone
   anywhere on these targets, but it's hard to prove this, so they're defined
   here.  None are actually necessary, as demonstrated below by defining
   each function using the operation it implements.  */

typedef long DI;
typedef unsigned long uDI;
typedef int SI;
typedef unsigned int uSI;
typedef int word_type __attribute__ ((mode (__word__)));

DI __ashldi3 (DI x, word_type c);
DI __ashrdi3 (DI x, word_type c);
int __clzsi2 (uSI x);
word_type __cmpdi2 (DI x, DI y);
int __ctzsi2 (uSI x);
DI __divdi3 (DI x, DI y);
uDI __lshrdi3 (uDI x, word_type c);
DI __moddi3 (DI x, DI y);
DI __muldi3 (DI x, DI y);
DI __negdi2 (DI x);
int __paritysi2 (uSI x);
int __popcountsi2 (uSI x);
word_type __ucmpdi2 (uDI x, uDI y);
uDI __udivdi3 (uDI x, uDI y);
uDI __udivmoddi4 (uDI x, uDI y, uDI *r);
uDI __umoddi3 (uDI x, uDI y);

DI __ashldi3 (DI x, word_type c) { return x << c; }
DI __ashrdi3 (DI x, word_type c) { return x >> c; }
int __clzsi2 (uSI x) { return __builtin_clz (x); }
word_type __cmpdi2 (DI x, DI y) { return x < y ? 0 : x == y ? 1 : 2; }
int __ctzsi2 (uSI x) { return __builtin_ctz (x); }
DI __divdi3 (DI x, DI y) { return x / y; }
uDI __lshrdi3 (uDI x, word_type c) { return x >> c; }
DI __moddi3 (DI x, DI y) { return x % y; }
DI __muldi3 (DI x, DI y) { return x * y; }
DI __negdi2 (DI x) { return -x; }
int __paritysi2 (uSI x) { return __builtin_parity (x); }
int __popcountsi2 (uSI x) { return __builtin_popcount (x); }
word_type __ucmpdi2 (uDI x, uDI y) { return x < y ? 0 : x == y ? 1 : 2; }
uDI __udivdi3 (uDI x, uDI y) { return x / y; }
uDI __udivmoddi4 (uDI x, uDI y, uDI *r) { *r = x % y; return x / y; }
uDI __umoddi3 (uDI x, uDI y) { return x % y; }

#endif /* __ppc64__ || __x86_64__ */
