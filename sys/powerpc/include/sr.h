/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2002 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_SR_H_
#define	_MACHINE_SR_H_

/*
 * Bit definitions for segment registers.
 *
 * PowerPC Microprocessor Family: The Programming Environments for 32-bit
 * Microprocessors, section 2.3.5
 */

#define	SR_TYPE		0x80000000	/* Type selector */
#define	SR_KS		0x40000000	/* Supervisor-state protection key */
#define	SR_KP		0x20000000	/* User-state protection key */
#define	SR_N		0x10000000	/* No-execute protection */
#define	SR_VSID_MASK	0x00ffffff	/* Virtual Segment ID mask */

/* Kernel segment register usage */
#define	USER_SR		12
#define	KERNEL_SR	13
#define	KERNEL2_SR	14
#define	KERNEL3_SR	15
#define	KERNEL_VSIDBITS	0xfffffUL
#define	KERNEL_SEGMENT	(0xfffff0 + KERNEL_SR)
#define	KERNEL2_SEGMENT	(0xfffff0 + KERNEL2_SR)
#define	EMPTY_SEGMENT	0xfffff0
#ifdef __powerpc64__
#define	USER_ADDR	0xeffffffff0000000UL
#else
#define	USER_ADDR	((uintptr_t)USER_SR << ADDR_SR_SHFT)
#endif
#define	SEGMENT_LENGTH	0x10000000UL
#define	SEGMENT_INVMASK	0x0fffffffUL
#define	SEGMENT_MASK	~SEGMENT_INVMASK

#endif /* !_MACHINE_SR_H_ */
