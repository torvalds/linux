/*	$OpenBSD: hibernate_machdep.c,v 1.62 2024/06/19 13:27:26 jsg Exp $	*/

/*
 * Copyright (c) 2011 Mike Larkin <mlarkin@openbsd.org>
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
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/hibernate.h>

#include <uvm/uvm_extern.h>

#include <machine/biosvar.h>
#include <machine/hibernate.h>
#include <machine/kcore.h>
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
#include "sdmmc.h"

/* Hibernate support */
void    hibernate_enter_resume_4k_pte(vaddr_t, paddr_t);
void    hibernate_enter_resume_4k_pde(vaddr_t);
void    hibernate_enter_resume_4m_pde(vaddr_t, paddr_t);

extern	caddr_t start, end;
extern	int ndumpmem;
extern  struct dumpmem dumpmem[];
extern	bios_memmap_t *bios_memmap;

/*
 * Hibernate always uses non-PAE page tables during resume, so
 * redefine masks and pt_entry_t sizes in case PAE is in use.
 */
#define PAGE_MASK_L2    (NBPD - 1)
typedef uint32_t pt_entry_t;

/*
 * i386 MD Hibernate functions
 *
 * see i386 hibernate.h for lowmem layout used during hibernate
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
		extern int sr_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		extern int sdmmc_scsi_hibernate_io(dev_t dev, daddr_t blkno,
		    vaddr_t addr, size_t size, int op, void *page);
		struct device *dv = disk_lookup(&sd_cd, DISKUNIT(dev));
		struct {
			const char *driver;
			hibio_fn io_func;
		} sd_io_funcs[] = {
#if NAHCI > 0
			{ "ahci", ahci_hibernate_io },
#endif
#if NSOFTRAID > 0
			{ "softraid", sr_hibernate_io },
#endif
#if NSDMMC > 0
			{ "sdmmc", sdmmc_scsi_hibernate_io },
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
	hiber_info->nranges = ndumpmem;
	hiber_info->image_size = 0;

	for (i = 0; i < ndumpmem; i++) {
		hiber_info->ranges[i].base = dumpmem[i].start * PAGE_SIZE;
		hiber_info->ranges[i].end = dumpmem[i].end * PAGE_SIZE;
		hiber_info->image_size += hiber_info->ranges[i].end -
		    hiber_info->ranges[i].base;
	}

	/* Record lowmem PTP page */
	if (hiber_info->nranges >= nitems(hiber_info->ranges))
		return (1);
	hiber_info->ranges[hiber_info->nranges].base = PTP0_PA;
	hiber_info->ranges[hiber_info->nranges].end =
	    hiber_info->ranges[hiber_info->nranges].base + PAGE_SIZE;
	hiber_info->image_size += PAGE_SIZE;
	hiber_info->nranges++;

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
	hiber_info->ranges[hiber_info->nranges].base = MP_TRAMP_DATA;
	hiber_info->ranges[hiber_info->nranges].end =
	    hiber_info->ranges[hiber_info->nranges].base + PAGE_SIZE;
	hiber_info->image_size += PAGE_SIZE;
	hiber_info->nranges++;
#endif /* MULTIPROCESSOR */

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
 *        1 if a 4MB mapping is desired
 */
void
hibernate_enter_resume_mapping(vaddr_t va, paddr_t pa, int size)
{
	if (size)
		return hibernate_enter_resume_4m_pde(va, pa);
	else
		return hibernate_enter_resume_4k_pte(va, pa);
}

/*
 * Enter a 4MB PDE mapping for the supplied VA/PA into the resume-time pmap
 */
void
hibernate_enter_resume_4m_pde(vaddr_t va, paddr_t pa)
{
	pt_entry_t *pde, npde;

	pde = s4pde_4m(va);
	npde = (pa & HIB_PD_MASK) | PG_RW | PG_V | PG_M | PG_PS;
	*pde = npde;
}

/*
 * Enter a 4KB PTE mapping for the supplied VA/PA into the resume-time pmap.
 */
void
hibernate_enter_resume_4k_pte(vaddr_t va, paddr_t pa)
{
	pt_entry_t *pte, npte;

	pte = s4pte_4k(va);
	npte = (pa & PMAP_PA_MASK) | PG_RW | PG_V | PG_M;
	*pte = npte;
}

/*
 * Enter a 4KB PDE mapping for the supplied VA into the resume-time pmap.
 */
void
hibernate_enter_resume_4k_pde(vaddr_t va)
{
	pt_entry_t *pde, npde;

	pde = s4pde_4k(va);
	npde = (HIBERNATE_PT_PAGE & PMAP_PA_MASK) | PG_RW | PG_V | PG_M;
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
	vaddr_t kern_start_4m_va, kern_end_4m_va, page;
	vaddr_t piglet_start_va, piglet_end_va;
	struct pmap *kpm = pmap_kernel();

	/* Identity map PD, PT, and stack pages */
	pmap_kenter_pa(HIBERNATE_PT_PAGE, HIBERNATE_PT_PAGE, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_PD_PAGE, HIBERNATE_PD_PAGE, PROT_MASK);
	pmap_kenter_pa(HIBERNATE_STACK_PAGE, HIBERNATE_STACK_PAGE, PROT_MASK);
	pmap_activate(curproc);

	bzero((caddr_t)HIBERNATE_PT_PAGE, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_PD_PAGE, PAGE_SIZE);
	bzero((caddr_t)HIBERNATE_STACK_PAGE, PAGE_SIZE);

	/* PDE for low pages */
	hibernate_enter_resume_4k_pde(0);

	/*
	 * Identity map low physical pages.
	 * See arch/i386/include/hibernate_var.h for page ranges used here.
	 */
	for (i = ACPI_TRAMPOLINE; i <= HIBERNATE_HIBALLOC_PAGE; i += PAGE_SIZE)
		hibernate_enter_resume_mapping(i, i, 0);

	/*
	 * Map current kernel VA range using 4M pages
	 */
	kern_start_4m_va = (vaddr_t)&start & ~(PAGE_MASK_L2);
	kern_end_4m_va = (vaddr_t)&end & ~(PAGE_MASK_L2);

	/* i386 kernels load at 2MB phys (on the 0th 4mb page) */
	phys_page_number = 0;

	for (page = kern_start_4m_va; page <= kern_end_4m_va;
	    page += NBPD, phys_page_number++) {
		pa = (paddr_t)(phys_page_number * NBPD);
		hibernate_enter_resume_mapping(page, pa, 1);
	}

	/*
	 * Identity map the image (pig) area
	 */
	phys_page_number = image_start / NBPD;
	image_start &= ~(PAGE_MASK_L2);
	image_end &= ~(PAGE_MASK_L2);
	for (page = image_start; page <= image_end ;
	    page += NBPD, phys_page_number++) {
		pa = (paddr_t)(phys_page_number * NBPD);
		hibernate_enter_resume_mapping(page, pa, 1);
	}

	/*
	 * Identity map the piglet using 4MB pages.
	 */
	phys_page_number = hib_info->piglet_pa / NBPD;

	/* VA == PA */
	piglet_start_va = hib_info->piglet_pa;
	piglet_end_va = piglet_start_va + HIBERNATE_CHUNK_SIZE * 4;

	for (page = piglet_start_va; page <= piglet_end_va;
	    page += NBPD, phys_page_number++) {
		pa = (paddr_t)(phys_page_number * NBPD);
		hibernate_enter_resume_mapping(page, pa, 1);
	}

	/*
	 * Fill last 8 slots of the new PD with the PAE PDPTEs of the
	 * kernel pmap, such that we can easily switch back into
	 * non-PAE mode.  If we're running in non-PAE mode, this will
	 * just fill the slots with zeroes.
	 */
	((uint64_t *)HIBERNATE_PD_PAGE)[508] = kpm->pm_pdidx[0];
	((uint64_t *)HIBERNATE_PD_PAGE)[509] = kpm->pm_pdidx[1];
	((uint64_t *)HIBERNATE_PD_PAGE)[510] = kpm->pm_pdidx[2];
	((uint64_t *)HIBERNATE_PD_PAGE)[511] = kpm->pm_pdidx[3];

	/* Unmap MMU pages (stack remains mapped) */
	pmap_kremove(HIBERNATE_PT_PAGE, PAGE_SIZE);
	pmap_kremove(HIBERNATE_PD_PAGE, PAGE_SIZE);
	pmap_activate(curproc);
}

/*
 * During inflate, certain pages that contain our bookkeeping information
 * (eg, the chunk table, scratch pages, retguard region, etc) need to be
 * skipped over and not inflated into.
 *
 * Return values:
 *  HIB_MOVE: if the physical page at dest should be moved to the retguard save
 *   region in the piglet
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
 * On i386, the APs have not been hatched at the time hibernate resume is
 * called, so there is no need to quiesce them. We do want to make sure
 * however that we are on the BSP.
 */
void
hibernate_quiesce_cpus(void)
{
	KASSERT(CPU_IS_PRIMARY(curcpu()));
}
#endif /* MULTIPROCESSOR */
