/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Olivier Houchard <cognet@FreeBSD.org>
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
 * $FreeBSD$
 */

#ifndef	_MACHINE_ASMACROS_H_
#define	_MACHINE_ASMACROS_H_

#include <machine/asm.h>

#ifdef _KERNEL

#ifdef LOCORE

#if __ARM_ARCH >= 6
#define GET_CURTHREAD_PTR(tmp) \
    	mrc	p15, 0, tmp, c13, c0, 4
#else
#define GET_CURTHREAD_PTR(tmp)	\
	ldr	tmp, =_C_LABEL(__pcpu);\
	ldr	tmp, [tmp, #PC_CURTHREAD]
#endif

#define	ELFNOTE(section, type, vendor, desctype, descdata...)	  \
	.pushsection section					; \
	    .balign 4						; \
	    .long 2f - 1f		/* namesz */		; \
	    .long 4f - 3f		/* descsz */		; \
	    .long type			/* type */		; \
	    1: .asciz vendor		/* vendor name */	; \
	    2: .balign 4					; \
	    3:  desctype descdata	/* node */		; \
	    4: .balign 4					; \
	.popsection

#endif /* LOCORE */

#endif /* _KERNEL */

#endif /* !_MACHINE_ASMACROS_H_ */
