/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_watchdog.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/sysctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/kerneldump.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/dump.h>
#include <machine/elf.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/armreg.h>
#include <machine/vmparam.h>	/* For KERNVIRTADDR */

int do_minidump = 1;
SYSCTL_INT(_debug, OID_AUTO, minidump, CTLFLAG_RWTUN, &do_minidump, 0,
    "Enable mini crash dumps");

void
dumpsys_wbinv_all(void)
{

	/*
	 * Make sure we write coherent data.  Note that in the SMP case this
	 * only operates on the L1 cache of the current CPU, but all other CPUs
	 * have already been stopped, and their flush/invalidate was done as
	 * part of stopping.
	 */
	dcache_wbinv_poc_all();
#ifdef __XSCALE__
	xscale_cache_clean_minidata();
#endif
}

void
dumpsys_map_chunk(vm_paddr_t pa, size_t chunk, void **va)
{
	vm_paddr_t a;
	int i;

	for (i = 0; i < chunk; i++) {
		a = pa + i * PAGE_SIZE;
		*va = pmap_kenter_temporary(trunc_page(a), i);
	}
}

/*
 * Add a header to be used by libkvm to get the va to pa delta
 */
int
dumpsys_write_aux_headers(struct dumperinfo *di)
{
	Elf_Phdr phdr;
	int error;

	bzero(&phdr, sizeof(phdr));
	phdr.p_type = PT_DUMP_DELTA;
	phdr.p_flags = PF_R;			/* XXX */
	phdr.p_offset = 0;
	phdr.p_vaddr = KERNVIRTADDR;
	phdr.p_paddr = pmap_kextract(KERNVIRTADDR);
	phdr.p_filesz = 0;
	phdr.p_memsz = 0;
	phdr.p_align = PAGE_SIZE;

	error = dumpsys_buf_write(di, (char*)&phdr, sizeof(phdr));
	return (error);
}
