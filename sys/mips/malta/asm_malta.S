/*-
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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
 * $FreeBSD$
 */

#include <machine/asm.h>

#define	VPECONF0_MVP	(1 << 1)
#define	VPECONF0_VPA	(1 << 0)

	.set noreorder

#ifdef SMP
/*
 * This function must be implemented in assembly because it is called early
 * in AP boot without a valid stack.
 */
LEAF(platform_processor_id)
	.set push
	.set mips32r2
	mfc0	v0, $15, 1
	jr	ra
	andi	v0, 0x1f
	.set pop
END(platform_processor_id)

LEAF(malta_cpu_configure)
	.set push
	.set mips32r2
	.set noat
	li	t2, (VPECONF0_MVP | VPECONF0_VPA)
	move	$1, t2
	jr	ra
	.word	0x41810000 | (1 << 11) | 2	# mttc0 t2, $1, 2
	.set pop
END(malta_cpu_configure)

/*
 * Called on APs to wait until they are told to launch.
 */
LEAF(malta_ap_wait)
	jal	platform_processor_id
	nop

	PTR_LA	t1, malta_ap_boot

1:
	ll	t0, 0(t1)
	bne	v0, t0, 1b
	nop

	move	t0, zero
	sc	t0, 0(t1)

	beqz	t0, 1b
	nop

	j	mpentry
	nop
END(malta_ap_wait)
#endif
