/* Definitions of target machine for GNU compiler for Renesas / SuperH SH 
   non-Linux embedded targets.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Contributed by J"orn Rennecke <joern.rennecke@superh.com>

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

#undef USER_LABEL_PREFIX
#define USER_LABEL_PREFIX "_"

#undef TARGET_POSIX_IO

/* While the speed-optimized implementations of udivsi3_i4i / sdivsi3_i4i
   in libgcc are not available for SH2, the space-optimized ones in
   libgcc-Os-4-200 are.  Thus, when not optimizing for space, link
   libgcc-Os-4-200 after libgcc, so that -mdiv=call-table works for -m2.  */
#define LIBGCC_SPEC "%{!shared: \
  %{m4-100*:-lic_invalidate_array_4-100} \
  %{m4-200*:-lic_invalidate_array_4-200} \
  %{m4a*:-lic_invalidate_array_4a}} \
  %{Os: -lgcc-Os-4-200} \
  -lgcc \
  %{!Os: -lgcc-Os-4-200}"
