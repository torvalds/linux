/* VMS dwarf2 section sequentializer.
   Copyright (C) 2001 Free Software Foundation, Inc.
   Contributed by Douglas B. Rupp (rupp@gnat.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* Linking with this file forces Dwarf2 debug sections to be
   sequentially loaded by the VMS linker, enabling GDB to read them.  */

.section	.debug_abbrev,NOWRT
		.align 0
		.globl	$dwarf2.debug_abbrev
$dwarf2.debug_abbrev:
	
.section	.debug_aranges,NOWRT
		.align 0
		.globl	$dwarf2.debug_aranges
$dwarf2.debug_aranges:
	
.section	.debug_frame,NOWRT
		.align 0
		.globl	$dwarf2.debug_frame
$dwarf2.debug_frame:		
	
.section	.debug_info,NOWRT
		.align 0
		.globl	$dwarf2.debug_info
$dwarf2.debug_info:		
	
.section	.debug_line,NOWRT
		.align 0
		.globl	$dwarf2.debug_line
$dwarf2.debug_line:		
	
.section	.debug_loc,NOWRT
		.align 0
		.globl	$dwarf2.debug_loc
$dwarf2.debug_loc:		
	
.section	.debug_macinfo,NOWRT
		.align 0
		.globl	$dwarf2.debug_macinfo
$dwarf2.debug_macinfo:		
	
.section	.debug_pubnames,NOWRT
		.align 0
		.globl	$dwarf2.debug_pubnames
$dwarf2.debug_pubnames:		
	
.section	.debug_str,NOWRT
		.align 0
		.globl	$dwarf2.debug_str
$dwarf2.debug_str:		
	
.section	.debug_zzzzzz,NOWRT
		.align 0
		.globl	$dwarf2.debug_zzzzzz
$dwarf2.debug_zzzzzz:		
