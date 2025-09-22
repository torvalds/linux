/*	$NetBSD: loadfile.h,v 1.1 1999/04/28 09:08:50 christos Exp $	 */
/*	$OpenBSD: loadfile.h,v 1.17 2023/04/25 12:46:13 dv Exp $	 */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <zlib.h>

#include "vmd.h"

#ifndef _LOADFILE_H_
#define _LOADFILE_H_

/*
 * Array indices in the u_long position array
 */
#define MARK_START	0
#define MARK_ENTRY	1
#define	MARK_NSYM	2
#define MARK_SYM	3
#define	MARK_END	4
#define	MARK_RANDOM	5
#define	MARK_ERANDOM	6
#define	MARK_MAX	7

/*
 * Bit flags for sections to load
 */
#define	LOAD_TEXT	0x0001
#define	LOAD_TEXTA	0x0002
#define	LOAD_DATA	0x0004
#define	LOAD_BSS	0x0008
#define	LOAD_SYM	0x0010
#define	LOAD_HDR	0x0020
#define	LOAD_RANDOM	0x0040
#define LOAD_ALL	0x007f

#define	COUNT_TEXT	0x0100
#define	COUNT_TEXTA	0x0200
#define	COUNT_DATA	0x0400
#define	COUNT_BSS	0x0800
#define	COUNT_SYM	0x1000
#define	COUNT_HDR	0x2000
#define	COUNT_RANDOM	0x4000
#define COUNT_ALL	0x7f00

#define LOWMEM_KB 640

#define BOOTARGS_PAGE 0x2000
#define STACK_PAGE 0xF000
#define GDT_PAGE 0x10000
#define PML4_PAGE 0x11000
#define PML3_PAGE 0x12000
#define PML2_PAGE 0x13000
#define NPTE_PG (PAGE_SIZE / sizeof(uint64_t))

int	loadfile_elf(gzFile, struct vmd_vm *, struct vcpu_reg_state *,
	    unsigned int);

size_t mread(gzFile, paddr_t, size_t);

#endif /* _LOADFILE_H_ */
