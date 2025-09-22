/* Copyright (C) 2000, 2001 Free Software Foundation, Inc.
   This file was adapted from glibc sources.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

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
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* The code in sections .init and .fini is supposed to be a single
   regular function.  The function in .init is called directly from
   start in crt1.asm.  The function in .fini is atexit()ed in crt1.asm
   too.

   crti.asm contributes the prologue of a function to these sections,
   and crtn.asm comes up the epilogue.  STARTFILE_SPEC should list
   crti.o before any other object files that might add code to .init
   or .fini sections, and ENDFILE_SPEC should list crtn.o after any
   such object files.  */

	.section .init
/* The alignment below can't be smaller, otherwise the mova below
   breaks.  Yes, we might align just the label, but then we'd be
   exchanging an alignment here for one there, since the code fragment
   below ensures 4-byte alignment on __ELF__.  */
#ifdef __ELF__
	.p2align 2
#else
	.p2align 1
#endif
	.global	 _init
_init:
#if __SHMEDIA__
	addi	r15, -16, r15
	st.q	r15, 8, r14
	st.q	r15, 0, r18
	add	r15, r63, r14
#elif __SH5__ && ! __SHMEDIA__
	mov	r15,r0
	add	#-8,r15
	mov.l	r14,@-r0
	sts.l	pr,@-r0
	mov	r15,r14
	nop
#else
#ifdef __ELF__
	mov.l	r12,@-r15
	mova	0f,r0
	mov.l	0f,r12
#endif
	mov.l	r14,@-r15
#ifdef __ELF__
	add	r0,r12
#endif
	sts.l	pr,@-r15
#ifdef __ELF__
	bra	1f
#endif
	mov	r15,r14
#ifdef __ELF__
0:	.long	_GLOBAL_OFFSET_TABLE_
1:
#endif
#endif /* __SHMEDIA__ */

	.section .fini
/* The alignment below can't be smaller, otherwise the mova below
   breaks.  Yes, we might align just the label, but then we'd be
   exchanging an alignment here for one there, since the code fragment
   below ensures 4-byte alignment on __ELF__.  */
#ifdef __ELF__
	.p2align 2
#else
	.p2align 1
#endif
	.global  _fini
_fini:	
#if __SHMEDIA__
	addi	r15, -16, r15
	st.q	r15, 8, r14
	st.q	r15, 0, r18
	add	r15, r63, r14
#elif __SH5__ && ! __SHMEDIA__
	mov	r15,r0
	add	#-8,r15
	mov.l	r14,@-r0
	sts.l	pr,@-r0
	mov	r15,r14
	nop
#else
#ifdef __ELF__
	mov.l	r12,@-r15
	mova	0f,r0
	mov.l	0f,r12
#endif
	mov.l	r14,@-r15
#ifdef __ELF__
	add	r0,r12
#endif
	sts.l	pr,@-r15
#ifdef __ELF__
	bra	1f
#endif
	mov	r15,r14
#ifdef __ELF__
0:	.long	_GLOBAL_OFFSET_TABLE_
1:
#endif
#endif /* __SHMEDIA__ */
