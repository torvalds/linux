/* dbxout.h - Various declarations for functions found in dbxout.c
   Copyright (C) 1998, 1999, 2000, 2003, 2004
   Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#ifndef GCC_DBXOUT_H
#define GCC_DBXOUT_H

extern int dbxout_symbol (tree, int);
extern void dbxout_parms (tree);
extern void dbxout_reg_parms (tree);
extern int dbxout_syms (tree);

/* Language description for N_SO stabs.  */
#define N_SO_AS          1
#define N_SO_C           2
#define N_SO_ANSI_C      3
#define N_SO_CC          4 /* c++*/
#define N_SO_FORTRAN     5
#define N_SO_PASCAL      6
#define N_SO_FORTRAN90   7
#define N_SO_OBJC        50
#define N_SO_OBJCPLUS    51

#endif /* GCC_DBXOUT_H */
