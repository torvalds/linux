/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Peter Wemm
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
 * from: FreeBSD: src/sys/i386/i386/minidump_machdep.c,v 1.6 2008/08/17 23:27:27
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_watchdog.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/msgbuf.h>
#ifdef SW_WATCHDOG
#include <sys/watchdog.h>
#endif
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/atomic.h>
#include <machine/cpu.h>
#include <machine/elf.h>
#include <machine/md_var.h>
#include <machine/minidump.h>
#include <machine/vmparam.h>

CTASSERT(sizeof(struct kerneldumpheader) == 512);

uint32_t *vm_page_dump;
int vm_page_dump_size;

static struct kerneldumpheader kdh;

/* Handle chunked writes. */
static size_t fragsz;
static void *dump_va;
static uint64_t counter, progress;

CTASSERT(sizeof(*vm_page_dump) == 4);

static int
is_dumpable(vm_paddr_t pa)
{
	int i;

	for (i = 0; dump_avail[i] != 0 || dump_avail[i + 1] != 0; i += 2) {
		if (pa >= dump_avail[i] && pa < dump_avail[i + 1])
			return (1);
	}
	return (0);
}

#define PG2MB(pgs) (((pgs) + (1 << 8) - 1) >> 8)

static int
blk_flush(struct dumperinfo *di)
{
	int error;

	if (fragsz == 0)
		return (0);

	error = dump_append(di, dump_va, 0, fragsz);
	fragsz = 0;
	return (error);
}

static int
blk_write(struct dumperinfo *di, char *ptr, vm_paddr_t pa, size_t sz)
{
	size_t len;
	int error, i, c;
	u_int maxdumpsz;

	maxdumpsz = min(di->maxiosize, MAXDUMPPGS * PAGE_SIZE);
	if (maxdumpsz == 0)	/* seatbelt */
		maxdumpsz = PAGE_SIZE;
	error = 0;
	if (ptr != NULL && pa != 0) {
		printf("cant have both va and pa!\n");
		return (EINVAL);
	}
	if (pa != 0) {
		if ((sz % PAGE_SIZE) != 0) {
			printf("size not page aligned\n");
			return (EINVAL);
		}
		if ((pa & PAGE_MASK) != 0) {
			printf("address not page aligned\n");
			return (EINVAL);
		}
	}
	if (ptr != NULL) {
		/* Flush any pre-existing pa pages before a virtual dump. */
		error = blk_flush(di);
		if (error)
			return (error);
	}
	while (sz) {
		len = maxdumpsz - fragsz;
		if (len > sz)
			len = sz;
		counter += len;
		progress -= len;
		if (counter >> 22) {
			printf(" %lld", PG2MB(progress >> PAGE_SHIFT));
			counter &= (1<<22) - 1;
		}

#ifdef SW_WATCHDOG
		wdog_kern_pat(WD_LASTVAL);
#endif
		if (ptr) {
			error = dump_append(di, ptr, 0, len);
			if (error)
				return (error);
			ptr += len;
			sz -= len;
		} else {
			for (i = 0; i < len; i += PAGE_SIZE)
				dump_va = pmap_kenter_temporary(pa + i,
				    (i + fragsz) >> PAGE_SHIFT);
			fragsz += len;
			pa += len;
			sz -= len;
			if (fragsz == maxdumpsz) {
				error = blk_flush(di);
				if (error)
					return (error);
			}
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

/* A buffer for general use. Its size must be one page at least. */
static char dumpbuf[PAGE_SIZE];
CTASSERT(sizeof(dumpbuf) % sizeof(pt2_entry_t) == 0);

int
minidumpsys(struct dumperinfo *di)
{
	struct minidumphdr mdhdr;
	uint64_t dumpsize;
	uint32_t ptesize;
	uint32_t bits;
	uint32_t pa, prev_pa = 0, count = 0;
	vm_offset_t va;
	int i, bit, error;
	char *addr;

	/*
	 * Flush caches.  Note that in the SMP case this operates only on the
	 * current CPU's L1 cache.  Before we reach this point, code in either
	 * the system shutdown or kernel debugger has called stop_cpus() to stop
	 * all cores other than this one.  Part of the ARM handling of
	 * stop_cpus() is to call wbinv_all() on that core's local L1 cache.  So
	 * by time we get to here, all that remains is to flush the L1 for the
	 * current CPU, then the L2.
	 */
	dcache_wbinv_poc_all();

	counter = 0;
	/* Walk page table pages, set bits in vm_page_dump */
	ptesize = 0;
	for (va = KERNBASE; va < kernel_vm_end; va += PAGE_SIZE) {
		pa = pmap_dump_kextract(va, NULL);
		if (pa != 0 && is_dumpable(pa))
			dump_add_page(pa);
		ptesize += sizeof(pt2_entry_t);
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
	mdhdr.kernbase = KERNBASE;
	mdhdr.arch = __ARM_ARCH;
#if __ARM_ARCH >= 6
	mdhdr.mmuformat = MINIDUMP_MMU_FORMAT_V6;
#else
	mdhdr.mmuformat = MINIDUMP_MMU_FORMAT_V4;
#endif
	dump_init_header(di, &kdh, KERNELDUMPMAGIC, KERNELDUMP_ARM_VERSION,
	    dumpsize);

	error = dump_start(di, &kdh);
	if (error != 0)
		goto fail;

	printf("Physical memory: %u MB\n", ptoa((uintmax_t)physmem) / 1048576);
	printf("Dumping %llu MB:", (long long)dumpsize >> 20);

	/* Dump my header */
	bzero(dumpbuf, sizeof(dumpbuf));
	bcopy(&mdhdr, dumpbuf, sizeof(mdhdr));
	error = blk_write(di, dumpbuf, 0, PAGE_SIZE);
	if (error)
		goto fail;

	/* Dump msgbuf up front */
	error = blk_write(di, (char *)msgbufp->msg_ptr, 0,
	    round_page(msgbufp->msg_size));
	if (error)
		goto fail;

	/* Dump bitmap */
	error = blk_write(di, (char *)vm_page_dump, 0,
	    round_page(vm_page_dump_size));
	if (error)
		goto fail;

	/* Dump kernel page table pages */
	addr = dumpbuf;
	for (va = KERNBASE; va < kernel_vm_end; va += PAGE_SIZE) {
		pmap_dump_kextract(va, (pt2_entry_t *)addr);
		addr += sizeof(pt2_entry_t);
		if (addr == dumpbuf + sizeof(dumpbuf)) {
			error = blk_write(di, dumpbuf, 0, sizeof(dumpbuf));
			if (error != 0)
				goto fail;
			addr = dumpbuf;
		}
	}
	if (addr != dumpbuf) {
		error = blk_write(di, dumpbuf, 0, addr - dumpbuf);
		if (error != 0)
			goto fail;
	}

	/* Dump memory chunks */
	for (i = 0; i < vm_page_dump_size / sizeof(*vm_page_dump); i++) {
		bits = vm_page_dump[i];
		while (bits) {
			bit = ffs(bits) - 1;
			pa = (((uint64_t)i * sizeof(*vm_page_dump) * NBBY) +
			    bit) * PAGE_SIZE;
			if (!count) {
				prev_pa = pa;
				count++;
			} else {
				if (pa == (prev_pa + count * PAGE_SIZE))
					count++;
				else {
					error = blk_write(di, NULL, prev_pa,
					    count * PAGE_SIZE);
					if (error)
						goto fail;
					count = 1;
					prev_pa = pa;
				}
			}
			bits &= ~(1ul << bit);
		}
	}
	if (count) {
		error = blk_write(di, NULL, prev_pa, count * PAGE_SIZE);
		if (error)
			goto fail;
		count = 0;
		prev_pa = 0;
	}

	error = blk_flush(di);
	if (error)
		goto fail;

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
