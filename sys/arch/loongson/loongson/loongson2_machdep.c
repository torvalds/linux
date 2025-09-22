/*	$OpenBSD: loongson2_machdep.c,v 1.17 2019/12/20 13:34:41 visa Exp $	*/

/*
 * Copyright (c) 2009, 2010 Miodrag Vallat.
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
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/cpu.h>
#include <machine/loongson2.h>
#include <machine/memconf.h>
#include <machine/pmon.h>

#ifdef HIBERNATE
#include <machine/hibernate_var.h>
#endif /* HIBERNATE */

#include <loongson/dev/bonitoreg.h>

extern struct phys_mem_desc mem_layout[MAXMEMSEGS];
extern paddr_t loongson_memlo_alias;

int	is_memory_range(paddr_t, psize_t, psize_t);
void	loongson2e_setup(u_long, u_long);
void	loongson2f_setup(u_long, u_long);
void	loongson2f_setup_window(uint, uint, uint64_t, uint64_t, uint64_t, uint);

/* PCI view of CPU memory */
paddr_t loongson_dma_base = 0;

/*
 * Canonical crossbow assignments on Loongson 2F based designs.
 * Might need to move to a per-design header file in the future.
 */

#define	MASTER_CPU		0
#define	MASTER_PCI		1

#define	WINDOW_CPU_LOW		0
#define	WINDOW_CPU_PCILO	1
#define	WINDOW_CPU_PCIHI	2
#define	WINDOW_CPU_DDR		3

#define	WINDOW_PCI_DDR		0

#define	DDR_PHYSICAL_BASE	0x0000000000000000UL	/* memory starts at 0 */
#define	DDR_PHYSICAL_SIZE	0x0000000080000000UL	/* up to 2GB */
#define	DDR_WINDOW_BASE		0x0000000080000000UL	/* mapped at 2GB */

#define	PCI_RESOURCE_BASE	0x0000000000000000UL
#define	PCI_RESOURCE_SIZE	0x0000000080000000UL

#define	PCI_DDR_BASE		0x0000000080000000UL	/* PCI->DDR at 2GB */

/*
 * Setup memory mappings for Loongson 2E processors.
 */

void
loongson2e_setup(u_long memlo, u_long memhi)
{
	if (memhi > ((DDR_PHYSICAL_SIZE - BONITO_PCIHI_BASE) >> 20)) {
		pmon_printf("WARNING! %d MB of memory will not be used",
		    memhi - ((DDR_PHYSICAL_SIZE - BONITO_PCIHI_BASE) >> 20));
		memhi = (DDR_PHYSICAL_SIZE - BONITO_PCIHI_BASE) >> 20;
	}

	memlo = atop(memlo << 20);
	memhi = atop(memhi << 20);
	physmem = memlo + memhi;

	/* do NOT stomp on exception area */
	mem_layout[0].mem_first_page = atop(DDR_PHYSICAL_BASE) + 1;
	mem_layout[0].mem_last_page = atop(DDR_PHYSICAL_BASE) + memlo;

	if (memhi != 0) {
		mem_layout[1].mem_first_page = atop(BONITO_PCIHI_BASE);
		mem_layout[1].mem_last_page = atop(BONITO_PCIHI_BASE) +
		    memhi;
	}

	loongson_dma_base = PCI_DDR_BASE ^ DDR_PHYSICAL_BASE;
}

/*
 * Setup memory mappings for Loongson 2F processors.
 */

void
loongson2f_setup(u_long memlo, u_long memhi)
{
	/*
	 * Because we'll only set up a 2GB window for the PCI bus to
	 * access local memory, we'll limit ourselves to 2GB of usable
	 * memory as well.
	 *
	 * Note that this is a bad justification for this; it should be
	 * possible to setup a 1GB PCI space / 3GB memory access window,
	 * and use bounce buffers if physmem > 3GB; but at the moment
	 * there is no need to solve this problem until Loongson 2F-based
	 * hardware with more than 2GB of memory is commonly encountered.
	 *
	 * Also note that, despite the crossbar window registers being
	 * 64-bit wide, the upper 32-bit always read back as zeroes, so
	 * it is dubious whether it is possible to use more than a 4GB
	 * address space... and thus more than 2GB of physical memory.
	 */

	physmem = memlo + memhi;	/* in MB so far */
	if (physmem > (DDR_PHYSICAL_SIZE >> 20)) {
		pmon_printf("WARNING! %d MB of memory will not be used",
		    physmem - (DDR_PHYSICAL_SIZE >> 20));
		memhi = (DDR_PHYSICAL_SIZE >> 20) - 256;
	}

	memlo = atop(memlo << 20);
	memhi = atop(memhi << 20);
	physmem = memlo + memhi;

	/*
	 * PMON configures the system with only the low 256MB of memory
	 * accessible.
	 *
	 * We need to reprogram the address windows in order to be able to
	 * access the whole memory, both by the local processor and by the
	 * PCI bus.
	 *
	 * To make our life easier, we'll setup the memory as a contiguous
	 * range starting at 2GB, and take into account the fact that the
	 * first 256MB are also aliased at address zero (which is where the
	 * kernel is loaded, really).
	 */

	if (memhi != 0) {
		/* do NOT stomp on exception area */
		mem_layout[0].mem_first_page = atop(DDR_WINDOW_BASE) + 1;
		mem_layout[0].mem_last_page = atop(DDR_WINDOW_BASE) +
		    memlo + memhi;
		loongson_dma_base = PCI_DDR_BASE ^ DDR_WINDOW_BASE;
		loongson_memlo_alias = DDR_WINDOW_BASE;
	} else {
		/* do NOT stomp on exception area */
		mem_layout[0].mem_first_page = atop(DDR_PHYSICAL_BASE) + 1;
		mem_layout[0].mem_last_page = atop(DDR_PHYSICAL_BASE) +
		    memlo /* + memhi */;
		loongson_dma_base = PCI_DDR_BASE ^ DDR_PHYSICAL_BASE;
	}
#ifdef HIBERNATE
	mem_layout[0].mem_first_page += HIBERNATE_RESERVED_PAGES;
#endif

	/*
	 * Allow access to memory beyond 256MB, by programming the
	 * Loongson 2F address window registers.
	 * This also makes sure PCI->DDR accesses can use a contiguous
	 * area regardless of the actual memory size.
	 */

	/*
	 * Master #0 (cpu) window #0 allows access to the low 256MB
	 * of memory at address zero onwards.
	 * This window is inherited from PMON; we set it up just in case.
	 */
	loongson2f_setup_window(MASTER_CPU, WINDOW_CPU_LOW, DDR_PHYSICAL_BASE,
	    ~(0x0fffffffUL), DDR_PHYSICAL_BASE, MASTER_CPU);

	/*
	 * Master #0 (cpu) window #1 allows access to the ``low'' PCI
	 * space (from 0x10000000 to 0x1fffffff).
	 * This window is inherited from PMON; we set it up just in case.
	 */
	loongson2f_setup_window(MASTER_CPU, WINDOW_CPU_PCILO, BONITO_PCILO_BASE,
	    ~(0x0fffffffUL), BONITO_PCILO_BASE, MASTER_PCI);

	/*
	 * Master #1 (PCI) window #0 allows access to the memory space
	 * by PCI devices at addresses 0x80000000 onwards.
	 * This window is inherited from PMON, but its mask might be too
	 * restrictive (256MB) so we make sure it matches our needs.
	 */
	loongson2f_setup_window(MASTER_PCI, WINDOW_PCI_DDR, PCI_DDR_BASE,
	    ~(DDR_PHYSICAL_SIZE - 1), DDR_PHYSICAL_BASE, MASTER_CPU);

	/*
	 * Master #0 (CPU) window #2 allows access to a subset of the ``high''
	 * PCI space (from 0x40000000 to 0x7fffffff only).
	 */
	loongson2f_setup_window(MASTER_CPU, WINDOW_CPU_PCIHI, LS2F_PCIHI_BASE,
	    ~((uint64_t)LS2F_PCIHI_SIZE - 1), LS2F_PCIHI_BASE, MASTER_PCI);

	/*
	 * Master #0 (CPU) window #3 allows access to the whole memory space
	 * at addresses 0x80000000 onwards.
	 */
	loongson2f_setup_window(MASTER_CPU, WINDOW_CPU_DDR, DDR_WINDOW_BASE,
	    ~(DDR_PHYSICAL_SIZE - 1), DDR_PHYSICAL_BASE, MASTER_CPU);

	cpu_cpuspeed = loongson2f_cpuspeed;
}

/*
 * Setup a window in the Loongson2F crossbar.
 */

void
loongson2f_setup_window(uint master, uint window, uint64_t base, uint64_t mask,
    uint64_t mmap, uint slave)
{
	volatile uint64_t *awrreg;

	awrreg = (volatile uint64_t *)PHYS_TO_XKPHYS(
	    LOONGSON_AWR_BASE(master, window), CCA_NC);
	*awrreg = base;
	(void)*awrreg;

	awrreg = (volatile uint64_t *)PHYS_TO_XKPHYS(
	    LOONGSON_AWR_SIZE(master, window), CCA_NC);
	*awrreg = mask;
	(void)*awrreg;

	awrreg = (volatile uint64_t *)PHYS_TO_XKPHYS(
	    LOONGSON_AWR_MMAP(master, window), CCA_NC);
	*awrreg = mmap | slave;
	(void)*awrreg;
}

/*
 * Return whether a given physical address points to managed memory.
 * (used by /dev/mem)
 */

int
is_memory_range(paddr_t pa, psize_t len, psize_t limit)
{
	struct phys_mem_desc *seg;
	uint64_t fp, lp;
	int i;

	fp = atop(pa);
	lp = atop(round_page(pa + len));

	if (limit != 0 && lp > atop(limit))
		return 0;

	/*
	 * Allow access to the low 256MB aliased region on 2F systems,
	 * if we are accessing memory at 2GB onwards.
	 */
	if (pa < 0x10000000 && loongson_ver >= 0x2f) {
		fp += mem_layout[0].mem_first_page - 1;
		lp += mem_layout[0].mem_first_page - 1;
	}

	for (i = 0, seg = mem_layout; i < MAXMEMSEGS; i++, seg++)
		if (fp >= seg->mem_first_page && lp <= seg->mem_last_page)
			return 1;

	return 0;
}

int
loongson2f_cpuspeed(int *freq)
{
	uint32_t step, val;

	val = REGVAL(LOONGSON_CHIP_CONFIG0);
	step = (val & 0x7) + 1;
	*freq = ((bootcpu_hwinfo.clock / 8) * step) / 1000000;

	return 0;
}

void
loongson2f_setperf(int percent)
{
	uint32_t step, val;

	step = percent * 8 / 100;
	if (step < 2)
		step = 2;

	/*
	 * Set clock step.
	 */
	val = REGVAL(LOONGSON_CHIP_CONFIG0);
	val = (val & ~0x7) | (step - 1);
	REGVAL(LOONGSON_CHIP_CONFIG0) = val;
	(void)REGVAL(LOONGSON_CHIP_CONFIG0);
}
