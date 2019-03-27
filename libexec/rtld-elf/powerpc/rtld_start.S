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

_ENTRY(.rtld_start)
	stwu    %r1,-48(%r1)	/* 16-byte aligned stack for reg saves +
				exit_proc & obj _rtld args +
				backchain & lrsave stack frame */
	stw     %r3,16(%r1)	/*  argc */
	stw     %r4,20(%r1)	/*  argv */
	stw     %r5,24(%r1)	/*  envp */
/*	stw     %r6,28(%r1)   *//*  obj (always 0) */
/*	stw     %r7,32(%r1)   *//*  cleanup (always 0) */
	stw     %r8,36(%r1)	/*  ps_strings */

	/*
	 * Perform initial relocation of ld-elf.so. Not as easy as it
	 * sounds.
	 *  - perform small forward branch to put PC into link reg
	 *  - use link-time constants to determine offset to the
	 *    _DYNAMIC section and the GOT. Add these to the PC to
	 *    convert to absolute addresses.
	 *  - sync icache to allow execution of the SVR4 ABI-specified
	 *    blrl instruction preceding the GOT
	 *  - Use this instruction to determine the GOT absolute address
	 *  - read GOT[0], which is the SVR4 ABI-specified link-time
	 *    value of _DYNAMIC. Subtract this value from the absolute
	 *    value to determine the load address
	 *  - call reloc_non_plt_self() to fix up ld-elf.so's relocations
	 */
	bl	1f
	.long	_DYNAMIC-.
	.long	_GLOBAL_OFFSET_TABLE_-.	/* branch lr + 4 */
1:
	mflr	%r3		/* PC value at .long */
	lwz	%r4,4(%r3)
	add	%r4,%r4,%r3	/* &_GLOBAL_OFFSET_TABLE-4, blrl insn. */
	dcbst   %r0,%r4         /* sync i-cache with d-cache */
	sync
	icbi    %r0,%r4
	isync

	lwz	%r4,0(%r3)	/* offset to _DYNAMIC */
	add	%r3,%r4,%r3	/* r3 = &_DYNAMIC, absolute value */

	bl	_GLOBAL_OFFSET_TABLE_@local-4
	mflr	%r4		/* &_GLOBAL_OFFSET_TABLE_, absolute value */
	lwz	%r4,0(%r4)	/* linker &_DYNAMIC, from got[0] */
	subf	%r4,%r4,%r3	/* subtract to calculate relocbase */

	bl	reloc_non_plt_self@plt /* reloc_non_plt_self(&_DYNAMIC,base) */

	/*
	 * The _rtld() function likes to see a stack layout containing
	 * { argc, argv[0], argv[1] ... argv[N], 0, env[0], ... , env[N] }
	 * Since the PowerPC stack was 16-byte aligned at exec time, the
	 * original stack layout has to be found by moving back a word
	 * from the argv pointer.
	 */
        lwz     %r4,20(%r1)	/* restore argv */
        addi    %r3,%r4,-4	/* locate argc ptr, &argv[-1] */

	addi	%r4,%r1,8	/* &exit_proc on stack */
	addi	%r5,%r1,12	/* &obj_main on stack */

	bl      _rtld@plt	/* &_start = _rtld(sp, &exit_proc, &obj_main)*/
	mtlr    %r3

	/*
	 * Restore args, with new obj/exit proc
	 */
	lwz     %r3,16(%r1)     /* argc */
	lwz     %r4,20(%r1)	/* argv */
	lwz     %r5,24(%r1)	/* envp */
	lwz     %r6,12(%r1)	/* obj */
	lwz     %r7,8(%r1)	/* exit proc */
	lwz     %r8,36(%r1)	/* ps_strings */
        addi    %r1,%r1,48	/* restore original stackptr */

	blrl	/* _start(argc, argv, envp, obj, cleanup, ps_strings) */

	li      %r0,1		/* _exit() */
	sc

/*
 * _rtld_bind_start()
 *
 * Call into the MI binder. This routine is reached via the PLT call cell,
 * and then _rtld_powerpc_pltresolve().
 * On entry, %r11 contains the index of the PLT cell, and %r12 contains
 * a pointer to the ELF object for the file.
 *  Save all registers, call into the binder to resolve and fixup the external
 * routine, and then transfer to the external routine on return.
 */
	.globl  _rtld_bind

_ENTRY(_rtld_bind_start)
	stwu    %r1,-160(%r1)		# stack space for 29 regs + r0/lr/cr
	stw     %r0,20(%r1)		# save r0
	mflr    %r0
	stw     %r0,16(%r1)		# save lr
	mfcr    %r0
	stw     %r0,12(%r1)		# save cr
	stmw    %r3,24(%r1)		# save r3-r31

	mr      %r3,%r12		# obj
	mulli   %r4,%r11,12		# rela index * sizeof(Elf_Rela)
	bl      _rtld_bind@PLT		# target addr = _rtld_bind(obj, reloff)
	mtctr   %r3			# move absolute target addr into ctr

        lmw     %r3,24(%r1)		# restore r3-r31
        lwz     %r0,12(%r1)		# restore cr
        mtcr    %r0
        lwz     %r0,16(%r1)		# restore lr
        mtlr    %r0
        lwz     %r0,20(%r1)		# restore r0

        addi    %r1,%r1,160		# restore stack
        bctr				# jump to target


/*
 * _rtld_powerpc_pltresolve()
 *
 *  This routine is copied into the latter part of the 72-byte reserved
 * area at the start of the PLT. The absolute address of the _rtld_bind_start
 * routine, and the ELF object for the loaded file, are inserted into
 * the code by the reloc.c:init_pltgot() routine.
 *  The first time an external routine is called, the PLT slot will
 * set up %r11 to the offset of the slot, and will jump to this routine.
 * The ELF object is shifted into %r11, and _rtld_bind_start is called
 * to complete the binding.
 */
_ENTRY(_rtld_powerpc_pltlongresolve)
	lis	%r12,0			# lis	12,jmptab@ha
	addi    %r12,%r12,0		# addi  12,12,jmptab@l
	subf	%r11,%r12,%r11		# reloff
	li	%r12,2
	srw	%r11,%r11,%r12		# index = reloff/sizeof(Elf_Addr)
_ENTRY(_rtld_powerpc_pltresolve)
        lis     %r12,0			# lis   12,_rtld_bind_start@ha
        addi    %r12,%r12,0		# addi  12,12,_rtld_bind_start@l
        mtctr   %r12
        lis     %r12,0			# lis   12,obj@ha
        addi    %r12,%r12,0		# addi  12,12,obj@l
        bctr

/*
 * _rtld_powerpc_pltcall()
 *
 *  This routine is copied into the 72-byte reserved area at the
 * start of the PLT. The reloc.c:init_pltgot() routine inserts
 * the absolute address of the jumptable.
 *  Control is transferred to this routine when the binder has
 * located the external routine, but determined that it is > 32Mb
 * from the PLT slot. Code is inserted into the PLT slot to set up
 * %r11 with the jumptable index, and jump to here, where the
 * absolute address of the external routine is loaded from the
 * jumptable and transferred to
 */
_ENTRY(_rtld_powerpc_pltcall)
        slwi    %r11,%r11,2		# jmptab offset = index * 4
        addis   %r11,%r11,0		# addis 11,11,jmptab@ha
        lwz     %r11,0(%r11)		# lwz   11,jmptab@l(11)
        mtctr   %r11
        bctr				# (*jmptab[index])()

	.section .note.GNU-stack,"",%progbits
