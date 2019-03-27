/*      $NetBSD: rtld_start.S,v 1.4 2001/09/26 04:06:43 mycroft Exp $   */

/*-
 * Copyright (C) 1998   Tsubai Masanari
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <machine/asm.h>

.extern _GLOBAL_OFFSET_TABLE_
.extern _DYNAMIC

_ENTRY(_rtld_start)
	stdu    %r1,-144(%r1)	/* 16-byte aligned stack for reg saves +
				exit_proc & obj _rtld args +
				backchain & lrsave stack frame */
	std     %r3,96(%r1)	/*  argc */
	std     %r4,104(%r1)	/*  argv */
	std     %r5,112(%r1)	/*  envp */
/*	std     %r6,120(%r1)   *//*  obj (always 0) */
/*	std     %r7,128(%r1)   *//*  cleanup (always 0) */
	std     %r8,136(%r1)	/*  ps_strings */

	/*
	 * Perform initial relocation of ld-elf.so. Not as easy as it
	 * sounds.
	 *  - perform small forward branch to put PC into link reg
	 *  - use link-time constants to determine offset to the
	 *    _DYNAMIC section and the GOT. Add these to the PC to
	 *    convert to absolute addresses.
	 *  - call reloc_non_plt_self() to fix up ld-elf.so's relocations
	 */

	bl	1f
	.llong	_DYNAMIC-.
1:
	mflr	%r3		/* PC value at .llong */
	ld	%r4,0(%r3)	/* offset to _DYNAMIC */
	add	%r3,%r4,%r3	/* r3 = &_DYNAMIC, absolute value */

	ld	%r4,-0x8000(%r2) /* First TOC entry is TOC base */
	subf	%r4,%r4,%r2	/* Subtract from real TOC base to get base */

	bl	reloc_non_plt_self /* reloc_non_plt_self(&_DYNAMIC,base) */
	nop

	/*
	 * The _rtld() function likes to see a stack layout containing
	 * { argc, argv[0], argv[1] ... argv[N], 0, env[0], ... , env[N] }
	 * Since the PowerPC stack was 16-byte aligned at exec time, the
	 * original stack layout has to be found by moving back a word
	 * from the argv pointer.
	 */
	ld	%r4,104(%r1)
        addi    %r3,%r4,-8	/* locate argc ptr, &argv[-1] */
	addi	%r4,%r1,128	/* &exit_proc on stack */
	addi	%r5,%r1,120	/* &obj_main on stack */

	bl      _rtld		/* &_start = _rtld(sp, &exit_proc, &obj_main)*/
	nop
#if !defined(_CALL_ELF) || _CALL_ELF == 1
	ld	%r2,8(%r3)
	ld	%r11,16(%r3)
	ld	%r3,0(%r3)
#else
	mr	%r12,%r3
#endif
	mtlr    %r3

	/*
	 * Restore args, with new obj/exit proc
	 */
	ld      %r3,96(%r1)     /* argc */
	ld      %r4,104(%r1)	/* argv */
	ld      %r5,112(%r1)	/* envp */
	ld      %r6,120(%r1)	/* obj */
	ld      %r7,128(%r1)	/* exit proc */
	ld      %r8,136(%r1)	/* ps_strings */

	blrl	/* _start(argc, argv, envp, obj, cleanup, ps_strings) */

	li      %r0,1		/* _exit() */
	sc

/*
 * _rtld_bind_start()
 *
 * Call into the MI binder. This routine is reached via the PLT call cell
 *
 * On entry, %r11 contains an object pointer and %r0 contains the PLT index.
 *
 * Save all registers, call into the binder to resolve and fixup the external
 * routine, and then transfer to the external routine on return.
 */
	.globl  _rtld_bind

_ENTRY(_rtld_bind_start)
	mr	%r12,%r0		# save r0 (index) immediately to r12
	mflr    %r0
	std     %r0,16(%r1)		# save lr
	mfcr    %r0
	std     %r0,8(%r1)		# save cr

	stdu    %r1,-48-12*8(%r1)	# stack space for 8 regs + header
					#   + 2 save regs
	std	%r3,64+0*8(%r1)		# save r3-r10 (arguments)
	std	%r4,64+1*8(%r1)
	std	%r5,64+2*8(%r1)
	std	%r6,64+3*8(%r1)
	std	%r7,64+4*8(%r1)
	std	%r8,64+5*8(%r1)
	std	%r9,64+6*8(%r1)
	std	%r10,64+7*8(%r1)

	mr	%r3,%r11
	mulli	%r4,%r12,24		# Multiply index by sizeof(Elf_Rela)

	bl      _rtld_bind		# target addr = _rtld_bind(obj, reloff)
	nop

#if !defined(_CALL_ELF) || _CALL_ELF == 1
	ld	%r2,8(%r3)
	ld	%r11,16(%r3)
	ld	%r3,0(%r3)
#else 
	mr	%r12,%r3
#endif
	mtctr   %r3			# move absolute target addr into ctr

	ld	%r3,64+0*8(%r1)		# restore r3-r10
	ld	%r4,64+1*8(%r1)
	ld	%r5,64+2*8(%r1)
	ld	%r6,64+3*8(%r1)
	ld	%r7,64+4*8(%r1)
	ld	%r8,64+5*8(%r1)
	ld	%r9,64+6*8(%r1)
	ld	%r10,64+7*8(%r1)

	ld	%r1,0(%r1)		# restore stack
	ld	%r0,8(%r1)		# restore cr
	mtcr    %r0
	ld	%r0,16(%r1)		# restore lr
	mtlr    %r0

	bctr				# jump to target

	.section .note.GNU-stack,"",%progbits
