/* Definitions for Renesas little endian M32R cpu.
   Copyright (C) 2003, 2004, 2005
   Free Software Foundation, Inc.

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
   along with GCC; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#define TARGET_LITTLE_ENDIAN 1

#define CPP_ENDIAN_SPEC \
  " %{mbe:-D__BIG_ENDIAN__} %{mbig-endian:-D__BIG_ENDIAN__}" \
  " %{!mbe: %{!mbig-endian:-D__LITTLE_ENDIAN__}}"
                                                                                
#define CC1_ENDIAN_SPEC " %{!mbe: %{!mbig-endian:-mle}}"

#define ASM_ENDIAN_SPEC \
  " %{!mbe: %{!mbig-endian:-EL}} %{mbe:-EB} %{mbig-endian:-EB}"

#define LINK_ENDIAN_SPEC " %{!mbe: %{!mbig-endian:-EL}}"

