/*	$OpenBSD: elf.h,v 1.3 2024/07/14 09:48:49 jca Exp $	*/

/*-
 * Copyright (c) 1996-1997 John D. Polstra.
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
 */

#ifndef	_MACHINE_ELF_H_
#define	_MACHINE_ELF_H_

/*
 * ELF definitions for the RISC-V architecture.
 */

#ifdef _KERNEL
# define __HAVE_CPU_HWCAP
extern unsigned long	hwcap;
#endif /* _KERNEL */

/* Flags passed in AT_HWCAP */
#define	HWCAP_ISA_BIT(c)	(1 << ((c) - 'a'))
#define	HWCAP_ISA_I		HWCAP_ISA_BIT('i')
#define	HWCAP_ISA_M		HWCAP_ISA_BIT('m')
#define	HWCAP_ISA_A		HWCAP_ISA_BIT('a')
#define	HWCAP_ISA_F		HWCAP_ISA_BIT('f')
#define	HWCAP_ISA_D		HWCAP_ISA_BIT('d')
#define	HWCAP_ISA_C		HWCAP_ISA_BIT('c')
#define	HWCAP_ISA_G		\
    (HWCAP_ISA_I | HWCAP_ISA_M | HWCAP_ISA_A | HWCAP_ISA_F | HWCAP_ISA_D)

#endif /* !_MACHINE_ELF_H_ */
