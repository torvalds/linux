/*	$OpenBSD: sparc64.h,v 1.14 2018/08/28 00:00:42 dlg Exp $	*/
/*	$NetBSD: sparc64.h,v 1.3 2000/10/20 05:47:03 mrg Exp $	*/

/*
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_MACHINE_SPARC64_H_
#define	_MACHINE_SPARC64_H_

struct mem_region {
	u_int64_t start;
	u_int64_t size;
};

int prom_set_trap_table(vaddr_t tba, paddr_t mmfsa);
paddr_t prom_vtop(vaddr_t vaddr);
vaddr_t prom_claim_virt(vaddr_t vaddr, int len);
vaddr_t prom_alloc_virt(int len, int align);
int prom_free_virt(vaddr_t vaddr, int len);
int prom_unmap_virt(vaddr_t vaddr, int len);
int prom_map_phys(paddr_t paddr, off_t size, vaddr_t vaddr, int mode);
paddr_t prom_alloc_phys(int len, int align);
paddr_t prom_claim_phys(paddr_t phys, int len);
int prom_free_phys(paddr_t paddr, int len);
paddr_t prom_get_msgbuf(int len, int align);
int prom_itlb_load(int index, u_int64_t data, vaddr_t vaddr);
int prom_dtlb_load(int index, u_int64_t data, vaddr_t vaddr);
void prom_start_cpu(int cpu, void *func, long arg);
void prom_start_cpu_by_cpuid(int cpu, void *func, long arg);
const char *prom_serengeti_set_console_input(const char *);
uint64_t prom_set_sun4v_api_version(uint64_t, uint64_t, uint64_t, uint64_t *);
void prom_sun4v_soft_state_supported(void);

/*
 * Debug
 */
void prom_printf(const char *, ...);
#endif	/* _MACHINE_SPARC64_H_ */
