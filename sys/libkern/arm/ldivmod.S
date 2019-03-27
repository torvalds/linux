/*
 * Copyright (C) 2012 Andrew Turner
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
 *
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * These calculate:
 * q = n / m 
 * With a remainer r.
 *
 * They take n in {r0, r1} and m in {r2, r3} then pass them into the
 * helper function. The hepler functions return q in {r0, r1} as
 * required by the API spec however r is returned on the stack. The
 * ABI required us to return r in {r2, r3}.
 *
 * We need to allocate 8 bytes on the stack to store r, the link
 * register, and a pointer to the space where the helper function
 * will write r to. After returning from the helper fuinction we load
 * the old link register and r from the stack and return.
 */
ENTRY_NP(__aeabi_ldivmod)
	sub	sp, sp, #8	/* Space for the remainder */
	stmfd	sp!, {sp, lr}	/* Save a pointer to the above space and lr */
	bl	PIC_SYM(_C_LABEL(__kern_ldivmod), PLT)
	ldr	lr, [sp, #4]	/* Restore lr */
	add	sp, sp, #8	/* Move sp to the remainder value */
	ldmfd	sp!, {r2, r3}	/* Load the remainder */
	RET
END(__aeabi_ldivmod)

ENTRY_NP(__aeabi_uldivmod)
	sub	sp, sp, #8	/* Space for the remainder */
	stmfd	sp!, {sp, lr}	/* Save a pointer to the above space and lr */
	bl	PIC_SYM(_C_LABEL(__qdivrem), PLT)
	ldr	lr, [sp, #4]	/* Restore lr */
	add	sp, sp, #8	/* Move sp to the remainder value */
	ldmfd	sp!, {r2, r3}	/* Load the remainder */
	RET
END(__aeabi_uldivmod)
