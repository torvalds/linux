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
 *	from: @(#)tss.h	5.4 (Berkeley) 1/18/91
 * $FreeBSD$
 */

#ifndef _MACHINE_TSS_H_
#define _MACHINE_TSS_H_ 1

/*
 * amd64 Context Data Type
 *
 * The alignment is pretty messed up here due to reuse of the original 32 bit
 * fields.  It might be worth trying to set the tss on a +4 byte offset to
 * make the 64 bit fields aligned in practice.
 */
struct amd64tss {
	u_int32_t	tss_rsvd0;
	u_int64_t	tss_rsp0 __packed; 	/* kernel stack pointer ring 0 */
	u_int64_t	tss_rsp1 __packed; 	/* kernel stack pointer ring 1 */
	u_int64_t	tss_rsp2 __packed; 	/* kernel stack pointer ring 2 */
	u_int32_t	tss_rsvd1;
	u_int32_t	tss_rsvd2;
	u_int64_t	tss_ist1 __packed;	/* Interrupt stack table 1 */
	u_int64_t	tss_ist2 __packed;	/* Interrupt stack table 2 */
	u_int64_t	tss_ist3 __packed;	/* Interrupt stack table 3 */
	u_int64_t	tss_ist4 __packed;	/* Interrupt stack table 4 */
	u_int64_t	tss_ist5 __packed;	/* Interrupt stack table 5 */
	u_int64_t	tss_ist6 __packed;	/* Interrupt stack table 6 */
	u_int64_t	tss_ist7 __packed;	/* Interrupt stack table 7 */
	u_int32_t	tss_rsvd3;
	u_int32_t	tss_rsvd4;
	u_int16_t	tss_rsvd5;
	u_int16_t	tss_iobase;	/* io bitmap offset */
};

#ifdef _KERNEL
extern struct amd64tss common_tss[];
#endif

#endif /* _MACHINE_TSS_H_ */
