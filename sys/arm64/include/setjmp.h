/*-
 * Copyright (c) 2014 Andrew Turner
 * Copyright (c) 2014-2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
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

#ifndef _MACHINE_SETJMP_H_
#define	_MACHINE_SETJMP_H_

#include <sys/cdefs.h>

/*
 * We need to store:
 *  - A magic value to differentiate the buffers
 *  - The stack pointer
 *  - The link register
 *  - 11 general purpose registers
 *  - 8 floating point registers
 *  - The signal mask (128 bits)
 * i.e. 24 64-bit words, round this up to 31(+1) 128-bit words to allow for
 * CPU extensions with larger registers and stronger alignment requirements.
 *
 * The registers to save are: r19 to r29, and d8 to d15.
 */
#define	_JBLEN		31
#define	_JB_SIGMASK	22

/* This should only be needed in libc and may change */
#ifdef __ASSEMBLER__
#define	_JB_MAGIC__SETJMP	0xfb5d25837d7ff700
#define	_JB_MAGIC_SETJMP	0xfb5d25837d7ff701
#endif

#ifndef __ASSEMBLER__
/*
 * jmp_buf and sigjmp_buf are encapsulated in different structs to force
 * compile-time diagnostics for mismatches.  The structs are the same
 * internally to avoid some run-time errors for mismatches.
 */
#if __BSD_VISIBLE || __POSIX_VISIBLE || __XSI_VISIBLE
typedef struct _sigjmp_buf { __int128_t _sjb[_JBLEN + 1]; } sigjmp_buf[1];
#endif

typedef struct _jmp_buf { __int128_t _jb[_JBLEN + 1]; } jmp_buf[1];
#endif /* __ASSEMBLER__ */

#endif /* !_MACHINE_SETJMP_H_ */
