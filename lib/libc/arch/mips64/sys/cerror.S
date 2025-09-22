/*	$OpenBSD: cerror.S,v 1.7 2016/05/07 19:05:22 guenther Exp $ */
/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "SYS.h"

FRAMESZ= 4*REGSZ
S0OFF= FRAMESZ-3*REGSZ
GPOFF= FRAMESZ-2*REGSZ
RAOFF= FRAMESZ-1*REGSZ

	.hidden	_thread_cb
	.hidden	CERROR

NON_LEAF(CERROR, FRAMESZ, ra)
	PTR_SUBU sp, FRAMESZ
#if !__PIC__ || !notyet
	SETUP_GP64(GPOFF, CERROR)
#endif
	.set	reorder
	REG_S	ra, RAOFF(sp)
	REG_S	s0, S0OFF(sp)

	move	s0, v0
	dla	v0, _thread_cb
	ld	t9, 0(v0)
	jal	t9

	sw	s0, 0(v0)

	LI	v0, -1
	REG_L	s0, S0OFF(sp)
	REG_L	ra, RAOFF(sp)
#if !__PIC__ || !notyet
	RESTORE_GP64
#endif
	PTR_ADDU sp, FRAMESZ
	j	ra
END(CERROR)
