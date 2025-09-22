/*	$OpenBSD: hibernate_machdep.c,v 1.52 2024/06/19 13:27:26 jsg Exp $	*/

/*
 * Copyright (c) 2012 Mike Larkin <mlarkin@openbsd.org>
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
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/hibernate.h>
#include <sys/timeout.h>
#include <sys/malloc.h>
#include <sys/kcore.h>
#include <sys/atomic.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_pmemrange.h>

#include <machine/biosvar.h>
#include <machine/cpu.h>
#include <machine/hibernate.h>
#include <machine/pte.h>
#include <machine/pmap.h>

#ifdef MULTIPROCESSOR
#include <machine/mpbiosvar.h>
#endif /* MULTIPROCESSOR */

#include <dev/acpi/acpivar.h>

#include "acpi.h"
#include "wd.h"
#include "ahci.h"
#include "softraid.h"
#include "sd.h"
#include "nvme.h"
#include "sdmmc.h"
#include "ufshci.h"

/* Hibernate support */
void    hibernate_enter_resume_4k_pte(vaddr_t, paddr_t);
void    hibernate_enter_resume_2m_pde(vaddr_t, paddr_t);

extern	caddr_t start, end;
extern	int mem_cluster_cnt;
extern  phys_ram_seg_t mem_clusters[];
extern	bios_memmap_t *bios_memmap;

/*
 * amd64 MD Hibernate functions
 *
 * see amd64 hibernate.h for lowmem layout used during hibernate
 */

/*
 * Returns the hibernate write I/O function to use on this machine
 */
hibio_fn
get_hibernate_io_function(dev_t dev)
{
	char *blkname = findblkname(major(dev));

	if (blkname == NULL)
		return NULL;

#if NWD > 0
	if (strcmp(blkname, "wd") == 0) {
		extern int wd_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		return wd_hibernate_io;
	}
#endif
#if NSD > 0
	if (strcmp(blkname, "sd") == 0) {
		extern struct cfdriver sd_cd;
		extern int ahci_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		extern int nvme_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		extern int sr_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		extern int sdmmc_scsi_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		extern int ufshci_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		struct device *dv = disk_lookup(&sd_cd, DISKUNIT(dev));
		struct {
			const char *driver;
			hibio_fn io_func;
		} sd_io_funcs[] = {
#if NAHCI > 0
			{ "ahci", ahci_hibernate_io },
#endif
#if NNVME > 0
			{ "nvme", nvme_hibernate_io },
#endif
#if NSOFTRAID > 0
			{ "softraid", sr_hibernate_io },
#endif
#if NSDMMC > 0
			{ "sdmmc", sdmmc_scsi_hibernate_io },
#endif
#if NUFSHCI > 0
			{ "ufshci", ufshci_hibernate_io },
#endif
		};

		if (dv && dv->dv_parent && dv->dv_parent->dv_parent) {
			const char *driver = dv->dv_parent->dv_parent->dv_cfdata->
			    cf_driver->cd_name;
			int i;

			for (i = 0; i < nitems(sd_io_funcs); i++) {
				if (strcmp(driver, sd_io_funcs[i].driver) == 0)
					return sd_io_funcs[i].io_func;
			}
		}
	}
#endif /* NSD > 0 */
	return NULL;
}

/*
 * Gather MD-specific data and store into hiber_info
 */
int
get_hibernate_info_md(union hibernate_info *hiber_info)
{
	int i;
	bios_memmap_t *bmp;

	/* Calculate memory ranges */
	hiber_info->nranges = mem_cluster_cnt;
	hiber_info->image_size = 0;

	for (i = 0; i < mem_cluster_cnt; i++) {
		hiber_info->ranges[i].base = mem_clusters[i].start;
		hiber_info->ranges[i].end = mem_clusters[i].size + mem_clusters[i].start;
		hiber_info->image_size += hiber_info->ranges[i].end -
		    hiber_info->ranges[i].base;
	}

#if NACPI > 0
	/* Record ACPI trampoline code page */
	if (hiber_info->nranges >= nitems(hiber_info->ranges))
		return (1);
	hiber_info->ranges[hiber_info->nranges].base = ACPI_TRAMPOLINE;
	hiber_info->ranges[hiber_info->nranges].end =
	    hiber_info->ranges[hiber_info->nranges].base + PAGE_SIZE;
	hiber_info->image_size += PAGE_SIZE;
	hiber_info->nranges++;

	/* Record ACPI trampoline data page */
	if (hiber_info->nranges >= nitems(hiber_info->ranges))
		return (1);
	hiber_info->ranges[hiber_info->nranges].base = ACPI_TRAMP_DATA;
	hiber_info->ranges[hiber_info->nranges].end =
	    hiber_info->ranges[hiber_info->nranges].base + PAGE_SIZE;
	hiber_info->image_size += PAGE_SIZE;
	hiber_info->nranges++;
#endif
#ifdef MULTIPROCESSOR
	/* Record MP trampoline code page */
	if (hiber_info->nranges >= nitems(hiber_info->ranges))
		return (1);
	hiber_info->ranges[hiber_info->nranges].base = MP_TRAMPOLINE;
	hiber_info->ranges[hiber_info->nranges].end =
	    hiber_info->ranges[hiber_info->nranges].base + PAGE_SIZE;
	hiber_info->image_size += PAGE_SIZE;
	hiber_info->nranges++;

	/* Record MP trampoline data page */
	if (hiber_info->nranges >= nitems(hiber_info->ranges))
		return (1);
	hiber_info->ranges[hiber_info->nranges].base =
		MP_TRAMP_DATA;
	hiber_info->ranges[hiber_info->nranges].end =
	    hiber_info->ranges[hiber_info->nranges].base + PAGE_SIZE;
	hiber_info->image_size += PAGE_SIZE;
	hiber_info->nranges++;
#endif

	for (bmp = bios_memmap; bmp->type != BIOS_MAP_END; bmp++) {
		/* Skip non-NVS ranges (already processed) */
		if (bmp->type != BIOS_MAP_NVS)
			continue;
		if (hiber_info->nranges >= nitems(hiber_info->ranges))
			return (1);

		i = hiber_info->nranges;
		hiber_info->ranges[i].base = round_page(bmp->addr);
		hiber_info->ranges[i].end = trunc_page(bmp->addr + bmp->size);
		hiber_info->image_size += hiber_info->ranges[i].end -
			hiber_info->ranges[i].base;
		hiber_info->nranges++;
	}

	hibernate_sort_ranges(hiber_info);

	return (0);
}

/*
 * Enter a mapping for va->pa in the resume pagetable, using
 * the specified size.
 *
 * size : 0 if a 4KB mapping is desired
 *        1 if a 2MB mapping is desired
 */
void
hibernate_enter_resume_mapping(vaddr_t va, paddr_t pa, int size)
{
	if (size)
		return hibernate_enter_resume_2m_pde(va, pa);
	else
		return hibernate_enter_resume_4k_pte(va, pa);
}

/*
 * Enter a 2MB PDE mapping for the supplied VA/PA into the resume-time pmap
 */
void
hibernate_enter_resume_2m_pde(vaddr_t va, paddr_t pa)
{
	pt_entry_t *pde, npde;

	if (va < NBPD_L4) {
		if (va < NBPD_L3) {
			/* First 512GB and 1GB are already mapped */
			pde = (pt_entry_t *)(HIBERNATE_PD_LOW +
				(pl2_pi(va) * sizeof(pt_entry_t)));
			npde = (pa & PG_LGFRAME) |
				PG_RW | PG_V | PG_M | PG_PS | PG_U;
			*pde = npde;
		} else {
			/* Map the 1GB containing region */
			pde = (pt_entry_t *)(HIBERNATE_PDPT_LOW +
				(pl3_pi(va) * sizeof(pt_entry_t)));
			npde = (HIBERNATE_PD_LOW2) | PG_RW | PG_V;
			*pde = npde;

			/* Map 2MB page */
			pde = (pt_entry_t *)(HIBERNATE_PD_LOW2 +
				(pl2_pi(va) * sizeof(pt_entry_t)));
			npde = (pa & PG_LGFRAME) |
				PG_RW | PG_V | PG_M | PG_PS | PG_U;
			*pde = npde;
		}
	} else {
		/* First map the 512GB containing region */
		pde = (pt_entry_t *)(HIBERNATE_PML4T +
			(pl4_pi(va) * sizeof(pt_entry_t)));
		npde = (HIBERNATE_PDPT_HI) | PG_RW | PG_V;
		*pde = npde;

		/* Map the 1GB containing region */
		pde = (pt_entry_t *)(HIBERNATE_PDPT_HI +
			(pl3_pi(va) * sizeof(pt_entry_t)));
		npde = (HIBERNATE_PD_HI) | PG_RW | PG_V;
		*pde = npde;

		/* Map the 2MB page */
		pde = (pt_entry_t *)(HIBERNATE_PD_HI +
			(pl2_pi(va) * sizeof(pt_entry_t)));
		npde = (pa & PG_LGFRAME) | PG_RW | PG_V | PG_PS;
		*pde = npde;
	}
}

/*
 * Enter a 4KB PTE mapping for the supplied VA/PA into the resume-time pmap.
 */
void
hibernate_enter_resume_4k_pte(vaddr_t va, paddr_t pa)
{
	pt_entry_t *pde, npde;

	/* Mappings entered here must be in the first 2MB VA */
	KASSERT(va < NBPD_L2);

	/* Map the page */
	pde = (pt_entry_t *)(HIBERNATE_PT_LOW +
		(pl1_pi(va) * sizeof(pt_entry_t)));
	npde = (pa & PMAP_PA_MASK) | PG_RW | PG_V | PG_M | PG_U;
	*pde = npde;
}

/*
 * Create the resume-time page table. This table maps the image(pig) area,
 * the kernel text area, and various utility pages for use during resume,
 * since we cannot overwrite the resuming kernel's page table during inflate
 * and expect things to work properly.
 */
void
hibernate_populate_resume_pt(union hibernate_info *hib_info,
    paddr_t image_start, paddr_t image_end)
{
	int phys_page_number, i;
	paddr_t pa;
	vaddr_t kern_start_2m_va, kern_end_2m_va, page;
	vaddr_t piglet_start_va, piglet_end_va;
	pt_entry_t *pde, npde;

	/* Identity map MMU pages */
	pmap_kenter_pa(HIBERNATE_PML4T, HIBERNATE_PML4T, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PDPT_LOW, HIBERNATE_PDPT_LOW, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PDPT_HI, HIBERNATE_PDPT_HI, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PD_LOW, HIBERNATE_PD_LOW, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PD_LOW2, HIBERNATE_PD_LOW2, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PD_HI, HIBERNATE_PD_HI, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PT_LOW, HIBERNATE_PT_LOW, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PT_LOW2, HIBERNATE_PT_LOW2, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PT_HI, HIBERNATE_PT_HI, PROT_MASK);

	/* Identity map 3 pages for stack */
	pmap_kenter_pa(HIBERNATE_STACK_PAGE, HIBERNATE_STACK_PAGE, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_STACK_PAGE - PAGE_SIZE,
		HIBERNATE_STACK_PAGE - PAGE_SIZE, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_STACK_PAGE - 2*PAGE_SIZE,
		HIBERNATE_STACK_PAGE - 2*PAGE_SIZE, PROT_MASK);
	pmap_activate(curproc);

	bzero((caddr_t)HIBERNATE_PML4T, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PDPT_LOW, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PDPT_HI, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PD_LOW, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PD_LOW2, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PD_HI, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PT_LOW, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PT_LOW2, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PT_HI, PAGE_SIZE);
	bzero((caddr_t)(HIBERNATE_STACK_PAGE - 3*PAGE_SIZE) , 3*PAGE_SIZE);

	/* First 512GB PML4E */
	pde = (pt_entry_t *)(HIBERNATE_PML4T +
		(pl4_pi(0) * sizeof(pt_entry_t)));
	npde = (HIBERNATE_PDPT_LOW) | PG_RW | PG_V;
	*pde = npde;

	/* First 1GB PDPTE */
	pde = (pt_entry_t *)(HIBERNATE_PDPT_LOW +
		(pl3_pi(0) * sizeof(pt_entry_t)));
	npde = (HIBERNATE_PD_LOW) | PG_RW | PG_V;
	*pde = npde;

	/* PD for first 2MB */
	pde = (pt_entry_t *)(HIBERNATE_PD_LOW +
		(pl2_pi(0) * sizeof(pt_entry_t)));
	npde = (HIBERNATE_PT_LOW) | PG_RW | PG_V;
	*pde = npde;

	/*
	 * Identity map low physical pages.
	 * See arch/amd64/include/hibernate_var.h for page ranges used here.
	 */
	for (i = ACPI_TRAMPOLINE; i <= HIBERNATE_HIBALLOC_PAGE; i += PAGE_SIZE)
		hibernate_enter_resume_mapping(i, i, 0);

	/*
	 * Map current kernel VA range using 2MB pages
	 */
	kern_start_2m_va = (vaddr_t)&start & ~(PAGE_MASK_L2);
	kern_end_2m_va = (vaddr_t)&end & ~(PAGE_MASK_L2);

	/* amd64 kernels load at 16MB phys (on the 8th 2mb page) */
	phys_page_number = 8;

	for (page = kern_start_2m_va; page <= kern_end_2m_va;
	    page += NBPD_L2, phys_page_number++) {
		pa = (paddr_t)(phys_page_number * NBPD_L2);
		hibernate_enter_resume_mapping(page, pa, 1);
	}

	/*
	 * Identity map the piglet using 2MB pages.
	 */
	phys_page_number = hib_info->piglet_pa / NBPD_L2;

	/* VA == PA */
	piglet_start_va = hib_info->piglet_pa;
	piglet_end_va = piglet_start_va + HIBERNATE_CHUNK_SIZE * 4;

	for (page = piglet_start_va; page <= piglet_end_va;
	    page += NBPD_L2, phys_page_number++) {
		pa = (paddr_t)(phys_page_number * NBPD_L2);
		hibernate_enter_resume_mapping(page, pa, 1);
	}

	/* Unmap MMU pages (stack remains mapped) */
	pmap_kremove(HIBERNATE_PML4T, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PDPT_LOW, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PDPT_HI, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PD_LOW, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PD_LOW2, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PD_HI, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PT_LOW, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PT_LOW2, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PT_HI, PAGE_SIZE);

	pmap_activate(curproc);
}

/*
 * During inflate, certain pages that contain our bookkeeping information
 * (eg, the chunk table, scratch pages, retguard region, etc) need to be
 * skipped over and not inflated into.
 *
 * Return values:
 *  HIB_MOVE: if the physical page at dest should be moved to the retguard save
 *    region in the piglet
 *  HIB_SKIP: if the physical page at dest should be skipped
 *  0: otherwise (no special treatment needed)
 */
int
hibernate_inflate_skip(union hibernate_info *hib_info, paddr_t dest)
{
	extern paddr_t retguard_start_phys, retguard_end_phys;

	if (dest >= hib_info->piglet_pa &&
	    dest <= (hib_info->piglet_pa + 4 * HIBERNATE_CHUNK_SIZE))
		return (HIB_SKIP);

	if (dest >= retguard_start_phys && dest <= retguard_end_phys)
		return (HIB_MOVE);

	return (0);
}

void
hibernate_enable_intr_machdep(void)
{
	intr_enable();
}

void
hibernate_disable_intr_machdep(void)
{
	intr_disable();
}

#ifdef MULTIPROCESSOR
/*
 * Quiesce CPUs in a multiprocessor machine before resuming. We need to do
 * this since the APs will be hatched (but waiting for CPUF_GO), and we don't
 * want the APs to be executing code and causing side effects during the
 * unpack operation.
 */
void
hibernate_quiesce_cpus(void)
{
	struct cpu_info *ci;
	u_long i;

	KASSERT(CPU_IS_PRIMARY(curcpu()));

	pmap_kenter_pa(ACPI_TRAMPOLINE, ACPI_TRAMPOLINE, PROT_READ | PROT_EXEC);
	pmap_kenter_pa(ACPI_TRAMP_DATA, ACPI_TRAMP_DATA,
		PROT_READ | PROT_WRITE);

	if (curcpu()->ci_feature_sefflags_edx & SEFF0EDX_IBT)
		lcr4(rcr4() & ~CR4_CET);

	for (i = 0; i < MAXCPUS; i++) {
		ci = cpu_info[i];
		if (ci == NULL)
			continue;
		if (ci->ci_idle_pcb == NULL)
			continue;
		if ((ci->ci_flags & CPUF_PRESENT) == 0)
			continue;
		if (ci->ci_flags & (CPUF_BSP | CPUF_SP | CPUF_PRIMARY))
			continue;
		atomic_setbits_int(&ci->ci_flags, CPUF_GO | CPUF_PARK);
	}

	/* Wait a bit for the APs to park themselves */
	delay(500000);

	pmap_kremove(ACPI_TRAMPOLINE, PAGE_SIZE);
	pmap_kremove(ACPI_TRAMP_DATA, PAGE_SIZE);
}
#endif /* MULTIPROCESSOR */
