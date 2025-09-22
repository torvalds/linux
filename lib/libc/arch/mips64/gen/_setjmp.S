/*	$OpenBSD: _setjmp.S,v 1.8 2018/01/08 16:44:32 visa Exp $ */

/*
 * Copyright (c) 2002 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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

	.globl	__jmpxor

/*
 * _setjmp, _longjmp (not restoring signal state)
 *
 * XXX FPSET should probably be taken from SR setting. hmmm...
 *  GPOFF and FRAMESIZE must be the same for both _setjmp and _longjmp!
 *
 */

FRAMESZ= MKFSIZ(0,4)
GPOFF= FRAMESZ-2*REGSZ

LEAF(_setjmp, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, _setjmp)
	.set	noreorder
	LI	v0, 0xACEDBADE			# sigcontext magic number
	REG_S	v0, _JB_REGS+ZERO*REGSZ(a0)
	REG_S	s0, _JB_REGS+S0*REGSZ(a0)
	REG_S	s1, _JB_REGS+S1*REGSZ(a0)
	REG_S	s2, _JB_REGS+S2*REGSZ(a0)
	REG_S	s3, _JB_REGS+S3*REGSZ(a0)
	REG_S	s4, _JB_REGS+S4*REGSZ(a0)
	REG_S	s5, _JB_REGS+S5*REGSZ(a0)
	REG_S	s6, _JB_REGS+S6*REGSZ(a0)
	REG_S	s7, _JB_REGS+S7*REGSZ(a0)
	REG_S	s8, _JB_REGS+S8*REGSZ(a0)
	LA	t0, __jmpxor			# load cookie addr
	REG_L	v0, 0(t0)			# load gp cookie
	REG_L	v1, GPOFF(sp)
	xor	v0, v0, v1
	REG_S	v0, _JB_REGS+GP*REGSZ(a0)
	REG_L	v0, REGSZ(t0)			# load sp cookie over gp cookie
	PTR_ADDU v1, sp, FRAMESZ
	xor	v0, v0, v1
	REG_S	v0, _JB_REGS+SP*REGSZ(a0)
	REG_L	t0, 2*REGSZ(t0)			# load ra cookie over addr
	xor	t0, ra, t0
	REG_S	t0, _JB_PC(a0)
	cfc1	t0, $31				# overwrite ra cookie
#if _MIPS_FPSET == 32
	sdc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a0)
	sdc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a0)
	sdc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a0)
	sdc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a0)
	sdc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a0)
	sdc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a0)
	sdc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a0)
	sdc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a0)
	sdc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a0)
	sdc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a0)
	sdc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a0)
	sdc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a0)
#else
	swc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a0)
	swc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a0)
	swc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a0)
	swc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a0)
	swc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a0)
	swc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a0)
	swc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a0)
	swc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a0)
	swc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a0)
	swc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a0)
	swc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a0)
	swc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a0)
#endif
	REG_S	t0, _JB_FPREGS+((FSR-F0)*REGSZ)(a0)
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
	j	ra
	move	v0, zero
END_STRONG(_setjmp)

LEAF(_longjmp, FRAMESZ)
	PTR_SUBU sp, FRAMESZ
	SETUP_GP64(GPOFF, _longjmp)
	.set    noreorder
	REG_L	v0, _JB_REGS+ZERO*REGSZ(a0)
	bne	v0, 0xACEDBADE, botch		# jump if error
	LA	v0, __jmpxor			# load cookie addr
	REG_L	v1, 2*REGSZ(v0)			# load ra cookie
	REG_L	ra, _JB_PC(a0)
	xor	ra, ra, v1
	REG_L	s0, _JB_REGS+S0*REGSZ(a0)
	REG_L	s1, _JB_REGS+S1*REGSZ(a0)
	REG_L	s2, _JB_REGS+S2*REGSZ(a0)
	REG_L	s3, _JB_REGS+S3*REGSZ(a0)
	REG_L	s4, _JB_REGS+S4*REGSZ(a0)
	REG_L	s5, _JB_REGS+S5*REGSZ(a0)
	REG_L	s6, _JB_REGS+S6*REGSZ(a0)
	REG_L	s7, _JB_REGS+S7*REGSZ(a0)
	REG_L	s8, _JB_REGS+S8*REGSZ(a0)
	REG_L	v1, 0(v0)			# load gp cookie over ra cookie
	REG_L	gp, _JB_REGS+GP*REGSZ(a0)
	xor	gp, gp, v1
	REG_L	v1, REGSZ(v0)			# load sp cookie over gp cookie
	REG_L	sp, _JB_REGS+SP*REGSZ(a0)
	xor	sp, sp, v1
	REG_L	v1, _JB_FPREGS+((FSR-F0)*REGSZ)(a0)	# overwrite sp cookie
	ctc1	v1, $31
#if _MIPS_FPSET == 32
	ldc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a0)
	ldc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a0)
	ldc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a0)
	ldc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a0)
	ldc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a0)
	ldc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a0)
	ldc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a0)
	ldc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a0)
	ldc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a0)
	ldc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a0)
	ldc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a0)
	ldc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a0)
#else
	lwc1	$f20, _JB_FPREGS+((F20-F0)*REGSZ)(a0)
	lwc1	$f21, _JB_FPREGS+((F21-F0)*REGSZ)(a0)
	lwc1	$f22, _JB_FPREGS+((F22-F0)*REGSZ)(a0)
	lwc1	$f23, _JB_FPREGS+((F23-F0)*REGSZ)(a0)
	lwc1	$f24, _JB_FPREGS+((F24-F0)*REGSZ)(a0)
	lwc1	$f25, _JB_FPREGS+((F25-F0)*REGSZ)(a0)
	lwc1	$f26, _JB_FPREGS+((F26-F0)*REGSZ)(a0)
	lwc1	$f27, _JB_FPREGS+((F27-F0)*REGSZ)(a0)
	lwc1	$f28, _JB_FPREGS+((F28-F0)*REGSZ)(a0)
	lwc1	$f29, _JB_FPREGS+((F29-F0)*REGSZ)(a0)
	lwc1	$f30, _JB_FPREGS+((F30-F0)*REGSZ)(a0)
	lwc1	$f31, _JB_FPREGS+((F31-F0)*REGSZ)(a0)
#endif
	beql	a1, zero, 1f
	li	a1, 1		# only executed if branch taken.
1:
	j	ra
	move	v0, a1

botch:
	jal	_libc_abort
	nop
	RESTORE_GP64
	PTR_ADDU sp, FRAMESZ
END_STRONG(_longjmp)

