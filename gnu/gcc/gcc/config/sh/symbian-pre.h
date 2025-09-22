/* Definitions for the Symbian OS running on an SH part.
   This file is included before any other target specific headers.

   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Contributed by Red Hat.

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
   Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Enable Symbian specific code.  */
#define SYMBIAN		1

/* Default to using the Renesas ABI.  */
#define TARGET_ABI_DEFAULT	MASK_HITACHI

#define SUBTARGET_CPP_SPEC ""

/* Get tree.c to declare merge_dllimport_decl_attributes().  */
#define TARGET_DLLIMPORT_DECL_ATTRIBUTES 1

/* The Symbian OS currently does not support exception handling.  */
#define SUBTARGET_CC1PLUS_SPEC "-fno-exceptions"

/* Create constructor/destructor sections without the writable flag.
   Symbian puts them into the text segment and munges them later on.  */
#define CTORS_SECTION_ASM_OP	"\t.section\t.ctors,\"ax\",@progbits"
#define DTORS_SECTION_ASM_OP	"\t.section\t.dtors,\"ax\",@progbits"
