/* $OpenBSD: cpufunc.h,v 1.4 2018/05/02 15:17:30 patrick Exp $ */
/*-
 * Copyright (c) 2014 Andrew Turner
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
 * $FreeBSD: head/sys/cpu/include/cpufunc.h 299683 2016-05-13 16:03:50Z andrew $
 */

#ifndef	_MACHINE_CPUFUNC_H_
#define	_MACHINE_CPUFUNC_H_

#ifdef _KERNEL

#include <machine/armreg.h>

extern int64_t dcache_line_size;
extern int64_t icache_line_size;
extern int64_t idcache_line_size;
extern int64_t dczva_line_size;

void cpu_setttb(int, paddr_t);
void cpu_tlb_flush(void);
void cpu_tlb_flush_asid(vaddr_t);
void cpu_tlb_flush_all_asid(vaddr_t);
void cpu_tlb_flush_asid_all(vaddr_t);
void cpu_icache_sync_range(vaddr_t, vsize_t);
void cpu_idcache_wbinv_range(vaddr_t, vsize_t);
void cpu_dcache_wbinv_range(vaddr_t, vsize_t);
void cpu_dcache_inv_range(vaddr_t, vsize_t);
void cpu_dcache_wb_range(vaddr_t, vsize_t);

register_t smc_call(register_t, register_t, register_t, register_t);

#endif	/* _KERNEL */
#endif	/* _MACHINE_CPUFUNC_H_ */
