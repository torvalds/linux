/* Definitions for non-Linux based StrongARM systems using ELF
   Copyright (C) 1999, 2001 Free Software Foundation, Inc.
   Contributed by Catherine Moore <clm@cygnus.com>

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Run-time Target Specification.  */
#ifndef TARGET_VERSION
#define TARGET_VERSION	fputs (" (StrongARM/ELF non-Linux)", stderr);
#endif

#ifndef SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT 		TARGET_CPU_strongarm
#endif

