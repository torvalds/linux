/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Peter Wemm
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
#include <sys/cons.h>
#include <sys/kernel.h>
#include <sys/kerneldump.h>
#include <sys/msgbuf.h>
#include <sys/watchdog.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/atomic.h>
#include <machine/elf.h>
#include <machine/md_var.h>
#include <machine/vmparam.h>
#include <machine/minidump.h>

CTASSERT(sizeof(struct kerneldumpheader) == 512);

#define	MD_ALIGN(x)	(((off_t)(x) + PAGE_MASK) & ~PAGE_MASK)
#define	DEV_ALIGN(x)	roundup2((off_t)(x), DEV_BSIZE)

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
	if ((sz % PAGE_SIZE) != 0) {
		printf("size not page aligned\n");
		return (EINVAL);
	}
	if (ptr != NULL && pa != 0) {
		printf("cant have both va and pa!\n");
		return (EINVAL);
	}
	if (pa != 0 && (((uintptr_t)ptr) % PAGE_SIZE) != 0) {
		printf("address not page aligned\n");
		return (EINVAL);
	}
	if (ptr != NULL) {
		/* If we're doing a virtual dump, flush any pre-existing pa pages */
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
		if (counter >> 24) {
			printf(" %lld", PG2MB(progress >> PAGE_SHIFT));
			counter &= (1<<24) - 1;
		}

		wdog_kern_pat(WD_LASTVAL);

		if (ptr) {
			error = dump_append(di, ptr, 0, len);
			if (error)
				return (error);
			ptr += len;
			sz -= len;
		} else {
			for (i = 0; i < len; i += PAGE_SIZE)
				dump_va = pmap_kenter_temporary(pa + i, (i + fragsz) >> PAGE_SHIFT);
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

/* A fake page table page, to avoid having to handle both 4K and 2M pages */
static pt_entry_t fakept[NPTEPG];

#ifdef PMAP_PAE_COMP
#define	minidumpsys	minidumpsys_pae
#define	IdlePTD		IdlePTD_pae
#else
#define	minidumpsys	minidumpsys_nopae
#define	IdlePTD		IdlePTD_nopae
#endif

int
minidumpsys(struct dumperinfo *di)
{
	uint64_t dumpsize;
	uint32_t ptesize;
	vm_offset_t va;
	int error;
	uint32_t bits;
	uint64_t pa;
	pd_entry_t *pd;
	pt_entry_t *pt;
	int i, j, k, bit;
	struct minidumphdr mdhdr;

	counter = 0;
	/* Walk page table pages, set bits in vm_page_dump */
	ptesize = 0;
	for (va = KERNBASE; va < kernel_vm_end; va += NBPDR) {
		/*
		 * We always write a page, even if it is zero. Each
		 * page written corresponds to 2MB of space
		 */
		ptesize += PAGE_SIZE;
		pd = IdlePTD;	/* always mapped! */
		j = va >> PDRSHIFT;
		if ((pd[j] & (PG_PS | PG_V)) == (PG_PS | PG_V))  {
			/* This is an entire 2M page. */
			pa = pd[j] & PG_PS_FRAME;
			for (k = 0; k < NPTEPG; k++) {
				if (is_dumpable(pa))
					dump_add_page(pa);
				pa += PAGE_SIZE;
			}
			continue;
		}
		if ((pd[j] & PG_V) == PG_V) {
			/* set bit for each valid page in this 2MB block */
			pt = pmap_kenter_temporary(pd[j] & PG_FRAME, 0);
			for (k = 0; k < NPTEPG; k++) {
				if ((pt[k] & PG_V) == PG_V) {
					pa = pt[k] & PG_FRAME;
					if (is_dumpable(pa))
						dump_add_page(pa);
				}
			}
		} else {
			/* nothing, we're going to dump a null page */
		}
	}

	/* Calculate dump size. */
	dumpsize = ptesize;
	dumpsize += round_page(msgbufp->msg_size);
	dumpsize += round_page(vm_page_dump_size);
	for (i = 0; i < vm_page_dump_size / sizeof(*vm_page_dump); i++) {
		bits = vm_page_dump[i];
		while (bits) {
			bit = bsfl(bits);
			pa = (((uint64_t)i * sizeof(*vm_page_dump) * NBBY) + bit) * PAGE_SIZE;
			/* Clear out undumpable pages now if needed */
			if (is_dumpable(pa)) {
				dumpsize += PAGE_SIZE;
			} else {
				dump_drop_page(pa);
			}
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
	mdhdr.paemode = pae_mode;

	dump_init_header(di, &kdh, KERNELDUMPMAGIC, KERNELDUMP_I386_VERSION,
	    dumpsize);

	error = dump_start(di, &kdh);
	if (error != 0)
		goto fail;

	printf("Physical memory: %ju MB\n", ptoa((uintmax_t)physmem) / 1048576);
	printf("Dumping %llu MB:", (long long)dumpsize >> 20);

	/* Dump my header */
	bzero(&fakept, sizeof(fakept));
	bcopy(&mdhdr, &fakept, sizeof(mdhdr));
	error = blk_write(di, (char *)&fakept, 0, PAGE_SIZE);
	if (error)
		goto fail;

	/* Dump msgbuf up front */
	error = blk_write(di, (char *)msgbufp->msg_ptr, 0, round_page(msgbufp->msg_size));
	if (error)
		goto fail;

	/* Dump bitmap */
	error = blk_write(di, (char *)vm_page_dump, 0, round_page(vm_page_dump_size));
	if (error)
		goto fail;

	/* Dump kernel page table pages */
	for (va = KERNBASE; va < kernel_vm_end; va += NBPDR) {
		/* We always write a page, even if it is zero */
		pd = IdlePTD;	/* always mapped! */
		j = va >> PDRSHIFT;
		if ((pd[j] & (PG_PS | PG_V)) == (PG_PS | PG_V))  {
			/* This is a single 2M block. Generate a fake PTP */
			pa = pd[j] & PG_PS_FRAME;
			for (k = 0; k < NPTEPG; k++) {
				fakept[k] = (pa + (k * PAGE_SIZE)) | PG_V | PG_RW | PG_A | PG_M;
			}
			error = blk_write(di, (char *)&fakept, 0, PAGE_SIZE);
			if (error)
				goto fail;
			/* flush, in case we reuse fakept in the same block */
			error = blk_flush(di);
			if (error)
				goto fail;
			continue;
		}
		if ((pd[j] & PG_V) == PG_V) {
			pa = pd[j] & PG_FRAME;
			error = blk_write(di, 0, pa, PAGE_SIZE);
			if (error)
				goto fail;
		} else {
			bzero(fakept, sizeof(fakept));
			error = blk_write(di, (char *)&fakept, 0, PAGE_SIZE);
			if (error)
				goto fail;
			/* flush, in case we reuse fakept in the same block */
			error = blk_flush(di);
			if (error)
				goto fail;
		}
	}

	/* Dump memory chunks */
	/* XXX cluster it up and use blk_dump() */
	for (i = 0; i < vm_page_dump_size / sizeof(*vm_page_dump); i++) {
		bits = vm_page_dump[i];
		while (bits) {
			bit = bsfl(bits);
			pa = (((uint64_t)i * sizeof(*vm_page_dump) * NBBY) + bit) * PAGE_SIZE;
			error = blk_write(di, 0, pa, PAGE_SIZE);
			if (error)
				goto fail;
			bits &= ~(1ul << bit);
		}
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
