/*	$OpenBSD: stubs.c,v 1.13 2021/05/16 03:39:27 jsg Exp $	*/
/*	$NetBSD: stubs.c,v 1.14 2003/07/15 00:24:42 lukem Exp $	*/

/*
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Routines that are temporary or do not have a home yet.
 *
 * Created      : 17/09/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/exec.h>
#include <sys/core.h>
#include <sys/kcore.h>
#include <uvm/uvm_extern.h>
#include <machine/bootconfig.h>
#include <machine/cpu.h>
#include <machine/pcb.h>
#include <arm/kcore.h>
#include <arm/machdep.h>

extern dev_t dumpdev;

/*
 * These variables are needed by /sbin/savecore
 */
u_int32_t dumpmag = 0x8fca0101;	/* magic number */
int 	dumpsize = 0;		/* pages */
long	dumplo = 0; 		/* blocks */
cpu_kcore_hdr_t cpu_kcore_hdr;
struct pcb dumppcb;

/*
 * This is called by main to set dumplo and dumpsize.
 * Dumps always skip the first CLBYTES of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */

void dumpconf(void);

void
dumpconf(void)
{
	int nblks, block;

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpsize = physmem;

	/* Always skip the first CLBYTES, in case there is a label there. */
	if (dumplo < ctod(1))
		dumplo = ctod(1);

	/* Put dump at end of partition, and make it fit. */
	if (dumpsize + 1 > dtoc(nblks - dumplo))
		dumpsize = dtoc(nblks - dumplo) - 1;
	if (dumplo < nblks - ctod(dumpsize) - 1)
		dumplo = nblks - ctod(dumpsize) - 1;

	for (block = 0; block < bootconfig.dramblocks; block++) {
		cpu_kcore_hdr.ram_segs[block].start =
		    bootconfig.dram[block].address;
		cpu_kcore_hdr.ram_segs[block].size =
		    ptoa(bootconfig.dram[block].pages);
	}
}

/* This should be moved to machdep.c */

extern char *memhook;		/* XXX */

/*
 * Doadump comes here after turning off memory management and
 * getting on the dump stack, either when called above, or by
 * the auto-restart code.
 */

void
dumpsys(void)
{
	const struct bdevsw *bdev;
	daddr_t blkno;
	int psize;
	int error;
	int addr;
	int block;
	int len;
	vaddr_t dumpspace;
	kcore_seg_t *kseg_p;
	cpu_kcore_hdr_t *chdr_p;
	char dump_hdr[dbtob(1)];	/* assumes header fits in one block */

	/* Save registers. */
	savectx(&dumppcb);
	/* flush everything out of caches */
	cpu_dcache_wbinv_all();
	cpu_sdcache_wbinv_all();

	if (dumpdev == NODEV)
		return;
	if (dumpsize == 0) {
		dumpconf();
		if (dumpsize == 0)
			return;
	}
	if (dumplo <= 0) {
		printf("\ndump to dev %u,%u not possible\n", major(dumpdev),
		    minor(dumpdev));
		return;
	}
	printf("\ndumping to dev %u,%u offset %ld\n", major(dumpdev),
	    minor(dumpdev), dumplo);

	blkno = dumplo;
	dumpspace = (vaddr_t) memhook;

	bdev = bdevsw_lookup(dumpdev);
	if (bdev == NULL || bdev->d_psize == NULL)
		return;
	psize = (*bdev->d_psize)(dumpdev);
	printf("dump ");
	if (psize == -1) {
		printf("area unavailable\n");
		return;
	}

	/* Setup the dump header */
	kseg_p = (kcore_seg_t *)dump_hdr;
	chdr_p = (cpu_kcore_hdr_t *)&dump_hdr[ALIGN(sizeof(*kseg_p))];
	bzero(dump_hdr, sizeof(dump_hdr));

	CORE_SETMAGIC(*kseg_p, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	kseg_p->c_size = sizeof(dump_hdr) - ALIGN(sizeof(*kseg_p));
	*chdr_p = cpu_kcore_hdr;

	error = (*bdev->d_dump)(dumpdev, blkno++, (caddr_t)dump_hdr,
	    sizeof(dump_hdr));
	if (error != 0)
		goto abort;

	len = 0;
	for (block = 0; block < bootconfig.dramblocks && error == 0; ++block) {
		addr = bootconfig.dram[block].address;
		for (;addr < (bootconfig.dram[block].address
		    + (bootconfig.dram[block].pages * PAGE_SIZE));
		     addr += PAGE_SIZE) {
		    	if ((len % (1024*1024)) == 0)
		    		printf("%d ", len / (1024*1024));
			pmap_kenter_pa(dumpspace, addr, PROT_READ);
			pmap_update(pmap_kernel());

			error = (*bdev->d_dump)(dumpdev,
			    blkno, (caddr_t) dumpspace, PAGE_SIZE);
			pmap_kremove(dumpspace, PAGE_SIZE);
			pmap_update(pmap_kernel());
			if (error) break;
			blkno += btodb(PAGE_SIZE);
			len += PAGE_SIZE;
		}
	}

abort:
	switch (error) {
	case ENXIO:
		printf("device bad\n");
		break;

	case EFAULT:
		printf("device not ready\n");
		break;

	case EINVAL:
		printf("area improper\n");
		break;

	case EIO:
		printf("i/o error\n");
		break;

	case EINTR:
		printf("aborted from console\n");
		break;

	default:
		printf("succeeded\n");
		break;
	}
	printf("\n\n");
	delay(1000000);
}
