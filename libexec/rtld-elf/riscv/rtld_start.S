/*-
 * Copyright (c) 2015-2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * This software was developed by the University of Cambridge Computer
 * Laboratory as part of the CTSRD Project, with support from the UK Higher
 * Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * func_ptr_type
 * _rtld(Elf_Addr *sp, func_ptr_type *exit_proc, Obj_Entry **objp)
 */

ENTRY(.rtld_start)
	mv	s0, a0		/* Put ps_strings in a callee-saved register */
	mv	s1, sp		/* And the stack pointer */

	addi	sp, sp, -16	/* Make room for obj_main & exit proc */

	mv	a1, sp		/* exit_proc */
	addi	a2, a1, 8	/* obj_main */
	jal	_rtld		/* Call the loader */
	mv	t0, a0		/* Backup the entry point */

	ld	a2, 0(sp)	/* Load cleanup */
	ld	a1, 8(sp)	/* Load obj_main */
	mv	a0, s0		/* Restore ps_strings */
	mv	sp, s1		/* Restore the stack pointer */
	jr	t0		/* Jump to the entry point */
END(.rtld_start)

/*
 * t0 = obj pointer
 * t1 = reloc offset
 */
ENTRY(_rtld_bind_start)
	/* Save the arguments and ra */
	addi	sp, sp, -(8 * 17)
	sd	a0, (8 * 0)(sp)
	sd	a1, (8 * 1)(sp)
	sd	a2, (8 * 2)(sp)
	sd	a3, (8 * 3)(sp)
	sd	a4, (8 * 4)(sp)
	sd	a5, (8 * 5)(sp)
	sd	a6, (8 * 6)(sp)
	sd	a7, (8 * 7)(sp)
	sd	ra, (8 * 8)(sp)

#ifdef __riscv_float_abi_double
	/* Save any floating-point arguments */
	fsd	fa0, (8 * 9)(sp)
	fsd	fa1, (8 * 10)(sp)
	fsd	fa2, (8 * 11)(sp)
	fsd	fa3, (8 * 12)(sp)
	fsd	fa4, (8 * 13)(sp)
	fsd	fa5, (8 * 14)(sp)
	fsd	fa6, (8 * 15)(sp)
	fsd	fa7, (8 * 16)(sp)
#endif

	/* Reloc offset is 3x of the .got.plt offset */
	slli	a1, t1, 1	/* Mult items by 2 */
	add	a1, a1, t1	/* Plus item */

	/* Load obj */
	mv	a0, t0

	/* Call into rtld */
	jal	_rtld_bind

	/* Backup the address to branch to */
	mv	t0, a0

	/* Restore the arguments and ra */
	ld	a0, (8 * 0)(sp)
	ld	a1, (8 * 1)(sp)
	ld	a2, (8 * 2)(sp)
	ld	a3, (8 * 3)(sp)
	ld	a4, (8 * 4)(sp)
	ld	a5, (8 * 5)(sp)
	ld	a6, (8 * 6)(sp)
	ld	a7, (8 * 7)(sp)
	ld	ra, (8 * 8)(sp)

#ifdef __riscv_float_abi_double
	/* Restore floating-point arguments */
	fld	fa0, (8 * 9)(sp)
	fld	fa1, (8 * 10)(sp)
	fld	fa2, (8 * 11)(sp)
	fld	fa3, (8 * 12)(sp)
	fld	fa4, (8 * 13)(sp)
	fld	fa5, (8 * 14)(sp)
	fld	fa6, (8 * 15)(sp)
	fld	fa7, (8 * 16)(sp)
#endif
	addi	sp, sp, (8 * 17)

	/* Call into the correct function */
	jr	t0
END(_rtld_bind_start)
