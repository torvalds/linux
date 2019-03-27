/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2008 Semihalf, Grzegorz Bernacki
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
 *
 * from: FreeBSD: src/sys/arm/arm/minidump_machdep.c v214223
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/msgbuf.h>
#include <sys/watchdog.h>
#include <sys/vmmeter.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_page.h>
#include <vm/vm_phys.h>
#include <machine/atomic.h>
#include <machine/elf.h>
#include <machine/md_var.h>
#include <machine/vmparam.h>
#include <machine/minidump.h>
#include <machine/cache.h>

CTASSERT(sizeof(struct kerneldumpheader) == 512);

uint32_t *vm_page_dump;
int vm_page_dump_size;

static struct kerneldumpheader kdh;

/* Handle chunked writes. */
static uint64_t counter, progress, dumpsize;
/* Just auxiliary bufffer */
static char tmpbuffer[PAGE_SIZE];

extern pd_entry_t *kernel_segmap;

CTASSERT(sizeof(*vm_page_dump) == 4);

static int
is_dumpable(vm_paddr_t pa)
{
	vm_page_t m;
	int i;

	if ((m = vm_phys_paddr_to_vm_page(pa)) != NULL)
		return ((m->flags & PG_NODUMP) == 0);
	for (i = 0; dump_avail[i] != 0 || dump_avail[i + 1] != 0; i += 2) {
		if (pa >= dump_avail[i] && pa < dump_avail[i + 1])
			return (1);
	}
	return (0);
}

void
dump_add_page(vm_paddr_t pa)
{
	int idx, bit;

	pa >>= PAGE_SHIFT;
	idx = pa >> 5;		/* 2^5 = 32 */
	bit = pa & 31;
	atomic_set_int(&vm_page_dump[idx], 1ul << bit);
}

void
dump_drop_page(vm_paddr_t pa)
{
	int idx, bit;

	pa >>= PAGE_SHIFT;
	idx = pa >> 5;		/* 2^5 = 32 */
	bit = pa & 31;
	atomic_clear_int(&vm_page_dump[idx], 1ul << bit);
}

static struct {
	int min_per;
	int max_per;
	int visited;
} progress_track[10] = {
	{  0,  10, 0},
	{ 10,  20, 0},
	{ 20,  30, 0},
	{ 30,  40, 0},
	{ 40,  50, 0},
	{ 50,  60, 0},
	{ 60,  70, 0},
	{ 70,  80, 0},
	{ 80,  90, 0},
	{ 90, 100, 0}
};

static void
report_progress(uint64_t progress, uint64_t dumpsize)
{
	int sofar, i;

	sofar = 100 - ((progress * 100) / dumpsize);
	for (i = 0; i < nitems(progress_track); i++) {
		if (sofar < progress_track[i].min_per ||
		    sofar > progress_track[i].max_per)
			continue;
		if (progress_track[i].visited)
			return;
		progress_track[i].visited = 1;
		printf("..%d%%", sofar);
		return;
	}
}

static int
write_buffer(struct dumperinfo *di, char *ptr, size_t sz)
{
	size_t len;
	int error, c;
	u_int maxdumpsz;

	maxdumpsz = di->maxiosize;

	if (maxdumpsz == 0)	/* seatbelt */
		maxdumpsz = PAGE_SIZE;

	error = 0;

	while (sz) {
		len = min(maxdumpsz, sz);
		counter += len;
		progress -= len;

		if (counter >> 22) {
			report_progress(progress, dumpsize);
			counter &= (1<<22) - 1;
		}

		wdog_kern_pat(WD_LASTVAL);

		if (ptr) {
			error = dump_append(di, ptr, 0, len);
			if (error)
				return (error);
			ptr += len;
			sz -= len;
		} else {
			panic("pa is not supported");
		}

		/* Check for user abort. */
		c = cncheckc();
		if (c == 0x03)
			return (ECANCELED);
		if (c != -1)
			printf(" (CTRL-C to abort) ");
	}

	return (0);
}

int
minidumpsys(struct dumperinfo *di)
{
	struct minidumphdr mdhdr;
	uint32_t ptesize;
	uint32_t bits;
	vm_paddr_t pa;
	vm_offset_t prev_pte = 0;
	uint32_t count = 0;
	vm_offset_t va;
	pt_entry_t *pte;
	int i, bit, error;
	void *dump_va;

	/* Flush cache */
	mips_dcache_wbinv_all();

	counter = 0;
	/* Walk page table pages, set bits in vm_page_dump */
	ptesize = 0;

	for (va = VM_MIN_KERNEL_ADDRESS; va < kernel_vm_end; va += NBPDR) {
		ptesize += PAGE_SIZE;
		pte = pmap_pte(kernel_pmap, va);
		KASSERT(pte != NULL, ("pte for %jx is NULL", (uintmax_t)va));
		for (i = 0; i < NPTEPG; i++) {
			if (pte_test(&pte[i], PTE_V)) {
				pa = TLBLO_PTE_TO_PA(pte[i]);
				if (is_dumpable(pa))
					dump_add_page(pa);
			}
		}
	}

	/*
	 * Now mark pages from 0 to phys_avail[0], that's where kernel 
	 * and pages allocated by pmap_steal reside
	 */
	for (pa = 0; pa < phys_avail[0]; pa += PAGE_SIZE) {
		if (is_dumpable(pa))
			dump_add_page(pa);
	}

	/* Calculate dump size. */
	dumpsize = ptesize;
	dumpsize += round_page(msgbufp->msg_size);
	dumpsize += round_page(vm_page_dump_size);
	for (i = 0; i < vm_page_dump_size / sizeof(*vm_page_dump); i++) {
		bits = vm_page_dump[i];
		while (bits) {
			bit = ffs(bits) - 1;
			pa = (((uint64_t)i * sizeof(*vm_page_dump) * NBBY) +
			    bit) * PAGE_SIZE;
			/* Clear out undumpable pages now if needed */
			if (is_dumpable(pa))
				dumpsize += PAGE_SIZE;
			else
				dump_drop_page(pa);
			bits &= ~(1ul << bit);
		}
	}
	dumpsize += PAGE_SIZE;

	progress = dumpsize;

	/* Initialize mdhdr */
	bzero(&mdhdr, sizeof(mdhdr));
	strcpy(mdhdr.magic, MINIDUMP_MAGIC);
	mdhdr.version = MINIDUMP_VERSION;
	mdhdr.msgbufsize = msgbufp->msg_size;
	mdhdr.bitmapsize = vm_page_dump_size;
	mdhdr.ptesize = ptesize;
	mdhdr.kernbase = VM_MIN_KERNEL_ADDRESS;

	dump_init_header(di, &kdh, KERNELDUMPMAGIC, KERNELDUMP_MIPS_VERSION,
	    dumpsize);

	error = dump_start(di, &kdh);
	if (error != 0)
		goto fail;

	printf("Dumping %llu out of %ju MB:", (long long)dumpsize >> 20,
	    ptoa((uintmax_t)physmem) / 1048576);

	/* Dump my header */
	bzero(tmpbuffer, sizeof(tmpbuffer));
	bcopy(&mdhdr, tmpbuffer, sizeof(mdhdr));
	error = write_buffer(di, tmpbuffer, PAGE_SIZE);
	if (error)
		goto fail;

	/* Dump msgbuf up front */
	error = write_buffer(di, (char *)msgbufp->msg_ptr, 
	    round_page(msgbufp->msg_size));
	if (error)
		goto fail;

	/* Dump bitmap */
	error = write_buffer(di, (char *)vm_page_dump,
	    round_page(vm_page_dump_size));
	if (error)
		goto fail;

	/* Dump kernel page table pages */
	for (va = VM_MIN_KERNEL_ADDRESS; va < kernel_vm_end; va += NBPDR) {
		pte = pmap_pte(kernel_pmap, va);
		KASSERT(pte != NULL, ("pte for %jx is NULL", (uintmax_t)va));
		if (!count) {
			prev_pte = (vm_offset_t)pte;
			count++;
		} else {
			if ((vm_offset_t)pte == (prev_pte + count * PAGE_SIZE))
				count++;
			else {
				error = write_buffer(di, (char*)prev_pte,
				    count * PAGE_SIZE);
				if (error)
					goto fail;
				count = 1;
				prev_pte = (vm_offset_t)pte;
			}
		}
	}

	if (count) {
		error = write_buffer(di, (char*)prev_pte, count * PAGE_SIZE);
		if (error)
			goto fail;
		count = 0;
		prev_pte = 0;
	}

	/* Dump memory chunks  page by page*/
	for (i = 0; i < vm_page_dump_size / sizeof(*vm_page_dump); i++) {
		bits = vm_page_dump[i];
		while (bits) {
			bit = ffs(bits) - 1;
			pa = (((uint64_t)i * sizeof(*vm_page_dump) * NBBY) +
			    bit) * PAGE_SIZE;
			dump_va = pmap_kenter_temporary(pa, 0);
			error = write_buffer(di, dump_va, PAGE_SIZE);
			if (error)
				goto fail;
			pmap_kenter_temporary_free(pa);
			bits &= ~(1ul << bit);
		}
	}

	error = dump_finish(di, &kdh);
	if (error != 0)
		goto fail;

	printf("\nDump complete\n");
	return (0);

fail:
	if (error < 0)
		error = -error;

	if (error == ECANCELED)
		printf("\nDump aborted\n");
	else if (error == E2BIG || error == ENOSPC)
		printf("\nDump failed. Partition too small.\n");
	else
		printf("\n** DUMP FAILED (ERROR %d) **\n", error);
	return (error);
}
