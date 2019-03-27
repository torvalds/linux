/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Marcel Moolenaar
 * Copyright (c) 2002 Thomas Moestl
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <machine/dump.h>
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/kerneldump.h>
#include <machine/ofw_mem.h>
#include <machine/tsb.h>
#include <machine/tlb.h>

static off_t fileofs;

extern struct dump_pa dump_map[DUMPSYS_MD_PA_NPAIRS];

int do_minidump = 0;

void
dumpsys_map_chunk(vm_paddr_t pa, size_t chunk __unused, void **va)
{

	*va = (void *)TLB_PHYS_TO_DIRECT(pa);
}

static int
reg_write(struct dumperinfo *di, vm_paddr_t pa, vm_size_t size)
{
	struct sparc64_dump_reg r;

	r.dr_pa = pa;
	r.dr_size = size;
	r.dr_offs = fileofs;
	fileofs += size;
	return (dumpsys_buf_write(di, (char *)&r, sizeof(r)));
}

int
dumpsys(struct dumperinfo *di)
{
	static struct kerneldumpheader kdh;
	struct sparc64_dump_hdr hdr;
	vm_size_t size, hdrsize;
	int error, i, nreg;

	/* Set up dump_map and calculate dump size. */
	size = 0;
	nreg = sparc64_nmemreg;
	memset(dump_map, 0, sizeof(dump_map));
	for (i = 0; i < nreg; i++) {
		dump_map[i].pa_start = sparc64_memreg[i].mr_start;
		size += dump_map[i].pa_size = sparc64_memreg[i].mr_size;
	}
	/* Account for the header size. */
	hdrsize = roundup2(sizeof(hdr) + sizeof(struct sparc64_dump_reg) * nreg,
	    DEV_BSIZE);
	size += hdrsize;

	dump_init_header(di, &kdh, KERNELDUMPMAGIC, KERNELDUMP_SPARC64_VERSION,
	    size);

	error = dump_start(di, &kdh);
	if (error != 0)
		goto fail;

	printf("Dumping %lu MB (%d chunks)\n", (u_long)(size >> 20), nreg);

	/* Dump the private header. */
	hdr.dh_hdr_size = hdrsize;
	hdr.dh_tsb_pa = tsb_kernel_phys;
	hdr.dh_tsb_size = tsb_kernel_size;
	hdr.dh_tsb_mask = tsb_kernel_mask;
	hdr.dh_nregions = nreg;

	if (dumpsys_buf_write(di, (char *)&hdr, sizeof(hdr)) != 0)
		goto fail;

	fileofs = hdrsize;
	/* Now, write out the region descriptors. */
	for (i = 0; i < nreg; i++) {
		error = reg_write(di, sparc64_memreg[i].mr_start,
		    sparc64_memreg[i].mr_size);
		if (error != 0)
			goto fail;
	}
	dumpsys_buf_flush(di);

	/* Dump memory chunks. */
	error = dumpsys_foreach_chunk(dumpsys_cb_dumpdata, di);
	if (error < 0)
		goto fail;

	error = dump_finish(di, &kdh);
	if (error != 0)
		goto fail;

	printf("\nDump complete\n");
	return (0);

 fail:
	if (error < 0)
		error = -error;

	/* XXX It should look more like VMS :-) */
	printf("** DUMP FAILED (ERROR %d) **\n", error);
	return (error);
}
