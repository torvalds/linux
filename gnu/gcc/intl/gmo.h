/* Description of GNU message catalog format: general file layout.
   Copyright (C) 1995, 1997, 2000-2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU Library General Public License as published
   by the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301,
   USA.  */

#ifndef _GETTEXT_H
#define _GETTEXT_H 1

#include <limits.h>

/* @@ end of prolog @@ */

/* The magic number of the GNU message catalog format.  */
#define _MAGIC 0x950412de
#define _MAGIC_SWAPPED 0xde120495

/* Revision number of the currently used .mo (binary) file format.  */
#define MO_REVISION_NUMBER 0

/* The following contortions are an attempt to use the C preprocessor
   to determine an unsigned integral type that is 32 bits wide.  An
   alternative approach is to use autoconf's AC_CHECK_SIZEOF macro, but
   as of version autoconf-2.13, the AC_CHECK_SIZEOF macro doesn't work
   when cross-compiling.  */

#if __STDC__
# define UINT_MAX_32_BITS 4294967295U
#else
# define UINT_MAX_32_BITS 0xFFFFFFFF
#endif

/* If UINT_MAX isn't defined, assume it's a 32-bit type.
   This should be valid for all systems GNU cares about because
   that doesn't include 16-bit systems, and only modern systems
   (that certainly have <limits.h>) have 64+-bit integral types.  */

#ifndef UINT_MAX
# define UINT_MAX UINT_MAX_32_BITS
#endif

#if UINT_MAX == UINT_MAX_32_BITS
typedef unsigned nls_uint32;
#else
# if USHRT_MAX == UINT_MAX_32_BITS
typedef unsigned short nls_uint32;
# else
#  if ULONG_MAX == UINT_MAX_32_BITS
typedef unsigned long nls_uint32;
#  else
  /* The following line is intended to throw an error.  Using #error is
     not portable enough.  */
  "Cannot determine unsigned 32-bit data type."
#  endif
# endif
#endif


/* Header for binary .mo file format.  */
struct mo_file_header
{
  /* The magic number.  */
  nls_uint32 magic;
  /* The revision number of the file format.  */
  nls_uint32 revision;

  /* The following are only used in .mo files with major revision 0.  */

  /* The number of strings pairs.  */
  nls_uint32 nstrings;
  /* Offset of table with start offsets of original strings.  */
  nls_uint32 orig_tab_offset;
  /* Offset of table with start offsets of translated strings.  */
  nls_uint32 trans_tab_offset;
  /* Size of hash table.  */
  nls_uint32 hash_tab_size;
  /* Offset of first hash table entry.  */
  nls_uint32 hash_tab_offset;

  /* The following are only used in .mo files with minor revision >= 1.  */

  /* The number of system dependent segments.  */
  nls_uint32 n_sysdep_segments;
  /* Offset of table describing system dependent segments.  */
  nls_uint32 sysdep_segments_offset;
  /* The number of system dependent strings pairs.  */
  nls_uint32 n_sysdep_strings;
  /* Offset of table with start offsets of original sysdep strings.  */
  nls_uint32 orig_sysdep_tab_offset;
  /* Offset of table with start offsets of translated sysdep strings.  */
  nls_uint32 trans_sysdep_tab_offset;
};

/* Descriptor for static string contained in the binary .mo file.  */
struct string_desc
{
  /* Length of addressed string, not including the trailing NUL.  */
  nls_uint32 length;
  /* Offset of string in file.  */
  nls_uint32 offset;
};

/* The following are only used in .mo files with minor revision >= 1.  */

/* Descriptor for system dependent string segment.  */
struct sysdep_segment
{
  /* Length of addressed string, including the trailing NUL.  */
  nls_uint32 length;
  /* Offset of string in file.  */
  nls_uint32 offset;
};

/* Descriptor for system dependent string.  */
struct sysdep_string
{
  /* Offset of static string segments in file.  */
  nls_uint32 offset;
  /* Alternating sequence of static and system dependent segments.
     The last segment is a static segment, including the trailing NUL.  */
  struct segment_pair
  {
    /* Size of static segment.  */
    nls_uint32 segsize;
    /* Reference to system dependent string segment, or ~0 at the end.  */
    nls_uint32 sysdepref;
  } segments[1];
};

/* Marker for the end of the segments[] array.  This has the value 0xFFFFFFFF,
   regardless whether 'int' is 16 bit, 32 bit, or 64 bit.  */
#define SEGMENTS_END ((nls_uint32) ~0)

/* @@ begin of epilog @@ */

#endif	/* gettext.h  */
