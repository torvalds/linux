/*	$OpenBSD: setjmp.S,v 1.13 2023/12/10 16:45:51 deraadt Exp $ */

/*
 * Copyright (c) 2001-2002 Opsycon AB  (www.opsycon.se / www.opsycon.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Opsycon AB nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "SYS.h"
#include <machine/regnum.h>
#include <machine/setjmp.h>

	.section	.openbsd.randomdata,"aw",@progbits
	.balign	8
	.globl	__jmpxor
	.hidden	__jmpxor
__jmpxor:
	.space	3*REGSZ			# (28/gp, 29/sp, 31/ra)
	.size	__jmpxor, . - __jmpxor
	.type	__jmpxor,@object
	.text

/*
 * setjmp, longjmp implementation for libc. this code depends
 * on the layout of the struct sigcontext in machine/signal.h.
 */

FRAMESZ= MKFSIZ(0,4)
GPOFF= FRAMESZ-2*REGSZ

LEAF(setjmp, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, setjmp)
	.set	noreorder

	move	a2, a0				# save jmpbuf
	li	a0, 1				# how = SIG_BLOCK
	move	a1, zero			# get current signal mask
	li	v0, SYS_sigprocmask
99:	syscall				# mask in v0
	PINSYSCALL(SYS_sigprocmask, 99b)
	bne	a3, zero, botch
	REG_S	v0, _JB_MASK(a2)		# save sc_mask

	LI	v0, 0xACEDBADE			# sigcontext magic number
	REG_S	v0, _JB_REGS+ZERO*REGSZ(a2)
	REG_S	s0, _JB_REGS+S0*REGSZ(a2)
	REG_S	s1, _JB_REGS+S1*REGSZ(a2)
	REG_S	s2, _JB_REGS+S2*REGSZ(a2)
	REG_S	s3, _JB_REGS+S3*REGSZ(a2)
	REG_S	s4, _JB_REGS+S4*REGSZ(a2)
	REG_S	s5, _JB_REGS+S5*REGSZ(a2)
	REG_S	s6, _JB_REGS+S6*REGSZ(a2)
	REG_S	s7, _JB_REGS+S7*REGSZ(a2)
	REG_S	s8, _JB_REGS+S8*REGSZ(a2)
	LA	t0, __jmpxor			# load cookie addr
	REG_L	v0, 0(t0)			# load gp cookie
	REG_L	v1, GPOFF(sp)
	xor	v0, v0, v1
	REG_S	v0, _JB_REGS+GP*REGSZ(a2)
	REG_L	v0, REGSZ(t0)			# load sp cookie over gp cookie
	PTR_ADDU v1, sp, FRAMESZ
	xor	v0, v0, v1
	REG_S	v0, _JB_REGS+SP*REGSZ(a2)
	REG_L	t0, 2*REGSZ(t0)			# load ra cookie over addr
	xor	t0, ra, t0
	REG_S	t0, _JB_PC(a2)
	cfc1	t0, $31				# overwrite ra cookie
#if _MIPS_FPSET == 32
	sdc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a2)
	sdc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a2)
	sdc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a2)
	sdc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a2)
	sdc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a2)
	sdc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a2)
	sdc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a2)
	sdc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a2)
	sdc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a2)
	sdc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a2)
	sdc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a2)
	sdc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a2)
#else
	swc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a2)
	swc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a2)
	swc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a2)
	swc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a2)
	swc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a2)
	swc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a2)
	swc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a2)
	swc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a2)
	swc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a2)
	swc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a2)
	swc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a2)
	swc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a2)
#endif
	REG_S	t0, _JB_FPREGS+((FSR-F0)*REGSZ)(a2)
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	j	ra
	move	v0, zero
END_STRONG(setjmp)

LEAF(longjmp, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, longjmp)
	.set    noreorder

	move	a2, a0				# save jmpbuf
	move	a4, a1				# save val
	REG_L	a1, _JB_MASK(a2)		# load sc_mask
	li	a0, 3				# how = SIG_SETMASK
	li	v0, SYS_sigprocmask
98:	syscall
	PINSYSCALL(SYS_sigprocmask, 98b)
	bne	a3, zero, botch

	REG_L	v0, _JB_REGS+ZERO*REGSZ(a2)
	bne	v0, 0xACEDBADE, botch		# jump if error
	LA	v0, __jmpxor			# load cookie addr
	REG_L	v1, 2*REGSZ(v0)			# load ra cookie
	REG_L	ra, _JB_PC(a2)
	xor	ra, ra, v1
	REG_L	s0, _JB_REGS+S0*REGSZ(a2)
	REG_L	s1, _JB_REGS+S1*REGSZ(a2)
	REG_L	s2, _JB_REGS+S2*REGSZ(a2)
	REG_L	s3, _JB_REGS+S3*REGSZ(a2)
	REG_L	s4, _JB_REGS+S4*REGSZ(a2)
	REG_L	s5, _JB_REGS+S5*REGSZ(a2)
	REG_L	s6, _JB_REGS+S6*REGSZ(a2)
	REG_L	s7, _JB_REGS+S7*REGSZ(a2)
	REG_L	s8, _JB_REGS+S8*REGSZ(a2)
	REG_L	v1, 0(v0)			# load gp cookie over ra cookie
	REG_L	gp, _JB_REGS+GP*REGSZ(a2)
	xor	gp, gp, v1
	REG_L	v1, REGSZ(v0)			# load sp cookie over gp cookie
	REG_L	sp, _JB_REGS+SP*REGSZ(a2)
	xor	sp, sp, v1
	REG_L	v1, _JB_FPREGS+((FSR-F0)*REGSZ)(a2)	# overwrite sp cookie
	ctc1	v1, $31
#if _MIPS_FPSET == 32
	ldc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a2)
	ldc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a2)
	ldc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a2)
	ldc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a2)
	ldc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a2)
	ldc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a2)
	ldc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a2)
	ldc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a2)
	ldc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a2)
	ldc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a2)
	ldc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a2)
	ldc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a2)
#else
	lwc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a2)
	lwc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a2)
	lwc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a2)
	lwc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a2)
	lwc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a2)
	lwc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a2)
	lwc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a2)
	lwc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a2)
	lwc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a2)
	lwc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a2)
	lwc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a2)
	lwc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a2)
#endif
	beql	a4, zero, 1f
	li	a4, 1		# only executed if branch taken.
1:
	j	ra
	move	v0, a4

botch:
	jal	_libc_abort
	nop
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
END_STRONG(longjmp)
