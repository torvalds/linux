/*	$OpenBSD: loadfile_subr.c,v 1.3 2013/03/21 21:50:59 deraadt Exp $	*/

/*
 * Copyright (c) 2010 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <lib/libkern/libkern.h>

#include <sys/exec_elf.h>
#include <lib/libsa/loadfile.h>

#include <machine/alpha_cpu.h>
#include <machine/rpb.h>

#define	ptoa(a)	((a) << PAGE_SHIFT)

/*
 * Prevent loading a kernel if it would overlap the SRM.
 */
int
check_phdr(void *hdr)
{
	Elf64_Phdr *phdr = (Elf64_Phdr *)hdr;
	struct rpb *hwrpb = (struct rpb *)HWRPB_ADDR;
	struct mddt *mddtp;
	struct mddt_cluster *memc;
	u_int64_t cstart, cend;
	u_int64_t i;

	mddtp = (struct mddt *)(((caddr_t)hwrpb) + hwrpb->rpb_memdat_off);
	for (i = 0; i < mddtp->mddt_cluster_cnt; i++) {
		memc = &mddtp->mddt_clusters[i];
		if (memc->mddt_usage & MDDT_PALCODE) {
			cstart = ALPHA_PHYS_TO_K0SEG(ptoa(memc->mddt_pfn));
			cend = cstart + ptoa(memc->mddt_pg_cnt);

			if (phdr->p_vaddr + phdr->p_memsz <= cstart ||
			    phdr->p_vaddr >= cend)
				continue;

			printf("SRM console and kernel image would overlap.\n"
			    "Please report this to <alpha@openbsd.org>, "
			    "with the following values:\n"
			    "SRM range: %p-%p\n"
			    "kernel range: %p-%p\n",
			    cstart, cend, phdr->p_vaddr,
			    phdr->p_vaddr + phdr->p_memsz);
			return 1;
		}
	}

	return 0;
}
