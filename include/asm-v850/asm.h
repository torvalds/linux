/*
 * include/asm-v850/asm.h -- Macros for writing assembly code
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#define G_ENTRY(name)							      \
   .balign 4;								      \
   .globl name;								      \
   .type  name,@function;						      \
   name
#define G_DATA(name)							      \
   .globl name;								      \
   .type  name,@object;							      \
   name
#define END(name)							      \
   .size  name,.-name

#define L_ENTRY(name)							      \
   .balign 4;								      \
   .type  name,@function;						      \
   name
#define L_DATA(name)							      \
   .type  name,@object;							      \
   name
