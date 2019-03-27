/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1990 William F. Jolitz
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
 *	from: @(#)segments.h	7.1 (Berkeley) 5/9/91
 * $FreeBSD$
 */

#ifndef _MACHINE_SEGMENTS_H_
#define	_MACHINE_SEGMENTS_H_

/*
 * AMD64 Segmentation Data Structures and definitions
 */

#include <x86/segments.h>

/*
 * System segment descriptors (128 bit wide)
 */
struct	system_segment_descriptor {
	u_int64_t sd_lolimit:16;	/* segment extent (lsb) */
	u_int64_t sd_lobase:24;		/* segment base address (lsb) */
	u_int64_t sd_type:5;		/* segment type */
	u_int64_t sd_dpl:2;		/* segment descriptor priority level */
	u_int64_t sd_p:1;		/* segment descriptor present */
	u_int64_t sd_hilimit:4;		/* segment extent (msb) */
	u_int64_t sd_xx0:3;		/* unused */
	u_int64_t sd_gran:1;		/* limit granularity (byte/page units)*/
	u_int64_t sd_hibase:40 __packed;/* segment base address  (msb) */
	u_int64_t sd_xx1:8;
	u_int64_t sd_mbz:5;		/* MUST be zero */
	u_int64_t sd_xx2:19;
} __packed;

/*
 * Software definitions are in this convenient format,
 * which are translated into inconvenient segment descriptors
 * when needed to be used by the 386 hardware
 */

struct	soft_segment_descriptor {
	unsigned long ssd_base;		/* segment base address  */
	unsigned long ssd_limit;	/* segment extent */
	unsigned long ssd_type:5;	/* segment type */
	unsigned long ssd_dpl:2;	/* segment descriptor priority level */
	unsigned long ssd_p:1;		/* segment descriptor present */
	unsigned long ssd_long:1;	/* long mode (for %cs) */
	unsigned long ssd_def32:1;	/* default 32 vs 16 bit size */
	unsigned long ssd_gran:1;	/* limit granularity (byte/page units)*/
} __packed;

/*
 * region descriptors, used to load gdt/idt tables before segments yet exist.
 */
struct region_descriptor {
	uint64_t rd_limit:16;		/* segment extent */
	uint64_t rd_base:64 __packed;	/* base address  */
} __packed;

#ifdef _KERNEL
extern struct user_segment_descriptor gdt[];
extern struct soft_segment_descriptor gdt_segs[];
extern struct gate_descriptor *idt;
extern struct region_descriptor r_gdt, r_idt;

void	lgdt(struct region_descriptor *rdp);
void	sdtossd(struct user_segment_descriptor *sdp,
	    struct soft_segment_descriptor *ssdp);
void	ssdtosd(struct soft_segment_descriptor *ssdp,
	    struct user_segment_descriptor *sdp);
void	ssdtosyssd(struct soft_segment_descriptor *ssdp,
	    struct system_segment_descriptor *sdp);
void	update_gdt_gsbase(struct thread *td, uint32_t base);
void	update_gdt_fsbase(struct thread *td, uint32_t base);
#endif /* _KERNEL */

#endif /* !_MACHINE_SEGMENTS_H_ */
