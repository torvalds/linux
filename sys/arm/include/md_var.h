/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1995 Bruce D. Evans.
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
 * 3. Neither the name of the author nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *	from: FreeBSD: src/sys/i386/include/md_var.h,v 1.40 2001/07/12
 * $FreeBSD$
 */

#ifndef	_MACHINE_MD_VAR_H_
#define	_MACHINE_MD_VAR_H_

extern long Maxmem;
extern char sigcode[];
extern int szsigcode;
extern uint32_t *vm_page_dump;
extern int vm_page_dump_size;
extern u_long elf_hwcap;
extern u_long elf_hwcap2;

extern int (*_arm_memcpy)(void *, void *, int, int);
extern int (*_arm_bzero)(void *, int, int);

extern int _min_memcpy_size;
extern int _min_bzero_size;

#define DST_IS_USER	0x1
#define SRC_IS_USER	0x2
#define IS_PHYSICAL	0x4

enum cpu_class {
	CPU_CLASS_NONE,
	CPU_CLASS_ARM9TDMI,
	CPU_CLASS_ARM9ES,
	CPU_CLASS_ARM9EJS,
	CPU_CLASS_ARM10E,
	CPU_CLASS_ARM10EJ,
	CPU_CLASS_CORTEXA,
	CPU_CLASS_KRAIT,
	CPU_CLASS_XSCALE,
	CPU_CLASS_ARM11J,
	CPU_CLASS_MARVELL
};
extern enum cpu_class cpu_class;

struct dumperinfo;
extern int busdma_swi_pending;
void busdma_swi(void);
void dump_add_page(vm_paddr_t);
void dump_drop_page(vm_paddr_t);
int minidumpsys(struct dumperinfo *);

extern uint32_t initial_fpscr;

#endif /* !_MACHINE_MD_VAR_H_ */
