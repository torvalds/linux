/* 
 Definitions of target machine for gcc for SuperH using target sh-superh-elf,
 
   Copyright 2000 Free Software Foundation, Inc.
   Contributed by Alexandre Oliva <aoliva@redhat.com>
   Modified for SuperH by Richard Shann

This file is part of GNU CC.

GNU CC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU CC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* This header file is used when the vendor name is set to 'superh'.
   It configures the compiler for SH5 only and switches the default
   endianess to little.
   This file is intended to override sh.h, superh.h and sh64.h (which
   should have been included in that order) */


#ifndef _SUPERH_H
 #error superh64.h should not be used without superh.h
#endif

/* We override TARGET_PROCESSOR_SWITCHES in order to remove all the unrequired cpu options */
#undef TARGET_PROCESSOR_SWITCHES
#define TARGET_PROCESSOR_SWITCHES  			\
  {"5-64media",	TARGET_NONE, "" },		\
  {"5-64media", SELECT_SH5_64, "SH5 64-bit SHmedia code" }, \
  {"5-64media-nofpu", TARGET_NONE, "" },	\
  {"5-64media-nofpu", SELECT_SH5_64_NOFPU, "SH5 64-bit FPU-less SHmedia code" }, \
  {"5-32media",	TARGET_NONE, "" },		\
  {"5-32media", SELECT_SH5_32, "SH5 32-bit SHmedia code" }, \
  {"5-32media-nofpu", TARGET_NONE, "" },	\
  {"5-32media-nofpu", SELECT_SH5_32_NOFPU, "SH5 32-bit FPU-less SHmedia code" }, \
  {"5-compact",	TARGET_NONE, "" },		\
  {"5-compact",	SELECT_SH5_COMPACT, "SH5 SHcompact code" }, \
  {"5-compact-nofpu", TARGET_NONE, "" },	\
  {"5-compact-nofpu", SELECT_SH5_COMPACT_NOFPU, "SH5 FPU-less SHcompact code" }
