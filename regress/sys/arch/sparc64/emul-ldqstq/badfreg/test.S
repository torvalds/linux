/*	$OpenBSD: test.S,v 1.1 2003/07/12 04:08:33 jason Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <machine/asm.h>
#include <machine/trap.h>
#include <machine/frame.h>

ENTRY(test_ldq_f2_simm13)
	.word 0xc5122000	! ldq [%o0 + 0], %f2
	retl
	nop

ENTRY(test_ldq_f2_g0)
	.word 0xc5120000	! ldq [%o0 + %g0], %f2
	retl
	nop

ENTRY(test_stq_f2_simm13)
	.word 0xc5322000	! stq %f2, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f2_g0)
	.word 0xc5320000	! stq %f2, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f6_simm13)
	.word 0xcd122000	! ldq [%o0 + 0], %f6
	retl
	nop

ENTRY(test_ldq_f6_g0)
	.word 0xcd120000	! ldq [%o0 + %g0], %f6
	retl
	nop

ENTRY(test_stq_f6_simm13)
	.word 0xcd322000	! stq %f6, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f6_g0)
	.word 0xcd320000	! stq %f6, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f10_simm13)
	.word 0xd5122000	! ldq [%o0 + 0], %f10
	retl
	nop

ENTRY(test_ldq_f10_g0)
	.word 0xd5120000	! ldq [%o0 + %g0], %f10
	retl
	nop

ENTRY(test_stq_f10_simm13)
	.word 0xd5322000	! stq %f10, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f10_g0)
	.word 0xd5320000	! stq %f10, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f14_simm13)
	.word 0xdd122000	! ldq [%o0 + 0], %f14
	retl
	nop

ENTRY(test_ldq_f14_g0)
	.word 0xdd120000	! ldq [%o0 + %g0], %f14
	retl
	nop

ENTRY(test_stq_f14_simm13)
	.word 0xdd322000	! stq %f14, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f14_g0)
	.word 0xdd320000	! stq %f14, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f18_simm13)
	.word 0xe5122000	! ldq [%o0 + 0], %f18
	retl
	nop

ENTRY(test_ldq_f18_g0)
	.word 0xe5120000	! ldq [%o0 + %g0], %f18
	retl
	nop

ENTRY(test_stq_f18_simm13)
	.word 0xe5322000	! stq %f18, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f18_g0)
	.word 0xe5320000	! stq %f18, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f22_simm13)
	.word 0xed122000	! ldq [%o0 + 0], %f22
	retl
	nop

ENTRY(test_ldq_f22_g0)
	.word 0xed120000	! ldq [%o0 + %g0], %f22
	retl
	nop

ENTRY(test_stq_f22_simm13)
	.word 0xed322000	! stq %f22, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f22_g0)
	.word 0xed320000	! stq %f22, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f26_simm13)
	.word 0xf5122000	! ldq [%o0 + 0], %f26
	retl
	nop

ENTRY(test_ldq_f26_g0)
	.word 0xf5120000	! ldq [%o0 + %g0], %f26
	retl
	nop

ENTRY(test_stq_f26_simm13)
	.word 0xf5322000	! stq %f26, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f26_g0)
	.word 0xf5320000	! stq %f26, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f30_simm13)
	.word 0xfd122000	! ldq [%o0 + 0], %f30
	retl
	nop

ENTRY(test_ldq_f30_g0)
	.word 0xfd120000	! ldq [%o0 + %g0], %f30
	retl
	nop

ENTRY(test_stq_f30_simm13)
	.word 0xfd322000	! stq %f30, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f30_g0)
	.word 0xfd320000	! stq %f30, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f34_simm13)
	.word 0xc7122000	! ldq [%o0 + 0], %f34
	retl
	nop

ENTRY(test_ldq_f34_g0)
	.word 0xc7120000	! ldq [%o0 + %g0], %f34
	retl
	nop

ENTRY(test_stq_f34_simm13)
	.word 0xc7322000	! stq %f34, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f34_g0)
	.word 0xc7320000	! stq %f34, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f38_simm13)
	.word 0xcf122000	! ldq [%o0 + 0], %f38
	retl
	nop

ENTRY(test_ldq_f38_g0)
	.word 0xcf120000	! ldq [%o0 + %g0], %f38
	retl
	nop

ENTRY(test_stq_f38_simm13)
	.word 0xcf322000	! stq %f38, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f38_g0)
	.word 0xcf320000	! stq %f38, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f42_simm13)
	.word 0xd7122000	! ldq [%o0 + 0], %f42
	retl
	nop

ENTRY(test_ldq_f42_g0)
	.word 0xd7120000	! ldq [%o0 + %g0], %f42
	retl
	nop

ENTRY(test_stq_f42_simm13)
	.word 0xd7322000	! stq %f42, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f42_g0)
	.word 0xd7320000	! stq %f42, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f46_simm13)
	.word 0xdf122000	! ldq [%o0 + 0], %f46
	retl
	nop

ENTRY(test_ldq_f46_g0)
	.word 0xdf120000	! ldq [%o0 + %g0], %f46
	retl
	nop

ENTRY(test_stq_f46_simm13)
	.word 0xdf322000	! stq %f46, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f46_g0)
	.word 0xdf320000	! stq %f46, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f50_simm13)
	.word 0xe7122000	! ldq [%o0 + 0], %f50
	retl
	nop

ENTRY(test_ldq_f50_g0)
	.word 0xe7120000	! ldq [%o0 + %g0], %f50
	retl
	nop

ENTRY(test_stq_f50_simm13)
	.word 0xe7322000	! stq %f50, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f50_g0)
	.word 0xe7320000	! stq %f50, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f54_simm13)
	.word 0xef122000	! ldq [%o0 + 0], %f54
	retl
	nop

ENTRY(test_ldq_f54_g0)
	.word 0xef120000	! ldq [%o0 + %g0], %f54
	retl
	nop

ENTRY(test_stq_f54_simm13)
	.word 0xef322000	! stq %f54, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f54_g0)
	.word 0xef320000	! stq %f54, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f58_simm13)
	.word 0xf7122000	! ldq [%o0 + 0], %f58
	retl
	nop

ENTRY(test_ldq_f58_g0)
	.word 0xf7120000	! ldq [%o0 + %g0], %f58
	retl
	nop

ENTRY(test_stq_f58_simm13)
	.word 0xf7322000	! stq %f58, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f58_g0)
	.word 0xf7320000	! stq %f58, [%o0 + %g0]
	retl
	nop

ENTRY(test_ldq_f62_simm13)
	.word 0xff122000	! ldq [%o0 + 0], %f62
	retl
	nop

ENTRY(test_ldq_f62_g0)
	.word 0xff120000	! ldq [%o0 + %g0], %f62
	retl
	nop

ENTRY(test_stq_f62_simm13)
	.word 0xff322000	! stq %f62, [%o0 + 0]
	retl
	nop

ENTRY(test_stq_f62_g0)
	.word 0xff320000	! stq %f62, [%o0 + %g0]
	retl
	nop
