/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *
 *	from: @(#)isa.h	5.7 (Berkeley) 5/9/91
 * $FreeBSD$
 */

#ifndef _ISA_ISA_H_
#define	_ISA_ISA_H_

/*
 * ISA Bus conventions
 */

/*
 * Input / Output Port Assignments
 */
#ifndef IO_ISABEGIN
#define	IO_ISABEGIN	0x000		/* 0x000 - Beginning of I/O Registers */
#define	IO_ICU1		0x020		/* 8259A Interrupt Controller #1 */
#define	IO_KBD		0x060		/* 8042 Keyboard */
#define	IO_RTC		0x070		/* RTC */
#define	IO_ICU2		0x0A0		/* 8259A Interrupt Controller #2 */

#define	IO_MDA		0x3B0		/* Monochome Adapter */
#define	IO_VGA		0x3C0		/* E/VGA Ports */
#define	IO_CGA		0x3D0		/* CGA Ports */

#endif /* !IO_ISABEGIN */

/*
 * Input / Output Port Sizes
 */
#define	IO_CGASIZE	12		/* CGA controllers */
#define	IO_MDASIZE	12		/* Monochrome display controllers */
#define	IO_VGASIZE	16		/* VGA controllers */

#endif /* !_ISA_ISA_H_ */
