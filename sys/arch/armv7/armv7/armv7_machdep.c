/*	$OpenBSD: armv7_machdep.c,v 1.66 2023/07/05 08:15:34 jsg Exp $ */
/*	$NetBSD: lubbock_machdep.c,v 1.2 2003/07/15 00:25:06 lukem Exp $ */

/*
 * Copyright (c) 2002, 2003  Genetec Corporation.  All rights reserved.
 * Written by Hiroyuki Bessho for Genetec Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of Genetec Corporation may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY GENETEC CORPORATION ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL GENETEC CORPORATION
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Machine dependant functions for kernel setup for
 * Intel DBPXA250 evaluation board (a.k.a. Lubbock).
 * Based on iq80310_machhdep.c
 */
/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
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
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1997,1998 Mark Brinicombe.
 * Copyright (c) 1997,1998 Causality Limited.
 * All rights reserved.
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
 * Machine dependant functions for kernel setup for ARMv7 boards using
 * u-boot/EFI firmware.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/reboot.h>

#include <machine/db_machdep.h>
#include <machine/bootconfig.h>
#include <machine/machine_reg.h>
#include <machine/bus.h>

#include <arm/undefined.h>
#include <arm/machdep.h>
#include <arm/armv7/armv7var.h>
#include <armv7/armv7/armv7_machdep.h>

#include <dev/cons.h>
#include <dev/efi/efi.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <net/if.h>

#include <ddb/db_extern.h>

/* Kernel text starts 2MB in from the bottom of the kernel address space. */
#define	KERNEL_TEXT_BASE	(KERNEL_BASE + 0x00000000)
#define	KERNEL_VM_BASE		(KERNEL_BASE + 0x04000000)
#define KERNEL_VM_SIZE		VM_KERNEL_SPACE_SIZE

/*
 * Address to call from cpu_reset() to reset the machine.
 * This is machine architecture dependant as it varies depending
 * on where the ROM appears when you turn the MMU off.
 */

/* Define various stack sizes in pages */
#define IRQ_STACK_SIZE	1
#define ABT_STACK_SIZE	1
#define UND_STACK_SIZE	1

BootConfig bootconfig;		/* Boot config storage */
char *boot_args = NULL;
char *boot_file = "";
uint8_t *bootmac = NULL;
u_int cpu_reset_address = 0;

vaddr_t physical_freestart;
int physmem;

/* Physical and virtual addresses for some global pages */
pv_addr_t systempage;
pv_addr_t irqstack;
pv_addr_t undstack;
pv_addr_t abtstack;
extern pv_addr_t kernelstack;

vaddr_t msgbufphys;

extern u_int data_abort_handler_address;
extern u_int prefetch_abort_handler_address;
extern u_int undefined_handler_address;

#define KERNEL_PT_SYS		0	/* Page table for mapping proc0 zero page */
#define KERNEL_PT_KERNEL	1	/* Page table for mapping kernel */
#define	KERNEL_PT_KERNEL_NUM	32
#define KERNEL_PT_VMDATA	(KERNEL_PT_KERNEL+KERNEL_PT_KERNEL_NUM)
				        /* Page tables for mapping kernel VM */
#define	KERNEL_PT_VMDATA_NUM	8	/* start with 16MB of KVM */
#define NUM_KERNEL_PTS		(KERNEL_PT_VMDATA + KERNEL_PT_VMDATA_NUM)

pv_addr_t kernel_pt_table[NUM_KERNEL_PTS];

extern struct user *proc0paddr;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = 0;

/* Prototypes */

int	bootstrap_bs_map(void *, uint64_t, bus_size_t, int,
    bus_space_handle_t *);
void	collect_kernel_args(const char *);
void	process_kernel_args(void);
void	consinit(void);

bs_protos(bs_notimpl);

int stdout_node;
int stdout_speed;

void (*cpuresetfn)(void);
void (*powerdownfn)(void);

/*
 * void boot(int howto, char *bootstr)
 *
 * Reboots the system
 *
 * Deal with any syncing, unmounting, dumping and shutdown hooks,
 * then reset the CPU.
 */
__dead void
boot(int howto)
{
	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	/* Disable console buffering */
/*	cnpollc(1);*/

	/*
	 * If RB_NOSYNC was not specified sync the discs.
	 * Note: Unless cold is set to 1 here, syslogd will die during the
	 * unmount.  It looks like syslogd is getting woken up only to find
	 * that it cannot page part of the binary in as the filesystem has
	 * been unmounted.
	 */
	if ((howto & RB_NOSYNC) == 0)
		bootsync(howto);

	if_downall();

	uvm_shutdown();
	splhigh();
	cold = 1;

	if ((howto & (RB_DUMP | RB_HALT)) == RB_DUMP)
		dumpsys();

haltsys:
	config_suspend_all(DVACT_POWERDOWN);

	/* Make sure IRQ's are disabled */
	intr_disable();

	if ((howto & RB_HALT) != 0) {
		if ((howto & RB_POWERDOWN) != 0) {
			printf("\nAttempting to power down...\n");
			delay(500000);
			if (powerdownfn)
				(*powerdownfn)();
		}

		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cngetc();
	}

doreset:
	printf("rebooting...\n");
	delay(500000);
	if (cpuresetfn)
		(*cpuresetfn)();
	printf("reboot failed; spinning\n");
	for (;;)
		continue;
	/* NOTREACHED */
}

static __inline
pd_entry_t *
read_ttb(void)
{
  long ttb;

  __asm volatile("mrc	p15, 0, %0, c2, c0, 0" : "=r" (ttb));


  return (pd_entry_t *)(ttb & ~((1<<14)-1));
}

#define VERBOSE_INIT_ARM

/*
 * simple memory mapping function used in early bootstrap stage
 * before pmap is initialized.
 * ignores cacheability and does map the sections with nocache.
 */
static vaddr_t section_free = 0xfd000000; /* XXX - huh */

int
bootstrap_bs_map(void *t, uint64_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	u_long startpa, pa, endpa;
	vaddr_t va;
	pd_entry_t *pagedir = read_ttb();
	/* This assumes PA==VA for page directory */

	va = section_free;

	startpa = bpa & ~L1_S_OFFSET;
	endpa = (bpa + size) & ~L1_S_OFFSET;
	if ((bpa + size) & L1_S_OFFSET)
		endpa += L1_S_SIZE;

	*bshp = (bus_space_handle_t)(va + (bpa - startpa));

	for (pa = startpa; pa < endpa; pa += L1_S_SIZE, va += L1_S_SIZE)
		pmap_map_section((vaddr_t)pagedir, va, pa,
		    PROT_READ | PROT_WRITE, PTE_NOCACHE);

	cpu_tlb_flushD();

	section_free = va;

	return 0;
}

static void
copy_io_area_map(pd_entry_t *new_pd)
{
	pd_entry_t *cur_pd = read_ttb();
	vaddr_t va;

	for (va = MACHINE_IO_AREA_VBASE;
	     (cur_pd[va>>L1_S_SHIFT] & L1_TYPE_MASK) == L1_TYPE_S;
	     va += L1_S_SIZE) {

		new_pd[va>>L1_S_SHIFT] = cur_pd[va>>L1_S_SHIFT];
		if (va == (ARM_VECTORS_HIGH & ~(0x00400000 - 1)))
			break; /* STUPID */

	}
}

uint64_t mmap_start;
uint32_t mmap_size;
uint32_t mmap_desc_size;
uint32_t mmap_desc_ver;

EFI_MEMORY_DESCRIPTOR *mmap;

/*
 * u_int initarm(...)
 *
 * Initial entry point on startup. This gets called before main() is
 * entered.
 * It should be responsible for setting up everything that must be
 * in place when main is called.
 * This includes
 *   Taking a copy of the FDT.
 *   Initialising the physical console so characters can be printed.
 *   Setting up page tables for the kernel.
 */
u_int
initarm(void *arg0, void *arg1, void *arg2, paddr_t loadaddr)
{
	int loop, loop1;
	u_int l1pagetable;
	pv_addr_t kernel_l1pt;
	pv_addr_t fdt, map;
	struct fdt_reg reg;
	paddr_t memstart, memend;
	void *config;
	size_t size;
	void *node;
	extern uint32_t esym; /* &_end if no symbols are loaded */

	/* early bus_space_map support */
	struct bus_space tmp_bs_tag;
	int	(*map_func_save)(void *, uint64_t, bus_size_t, int,
	    bus_space_handle_t *);

	if (arg0)
		esym = (uint32_t)arg0;

	/*
	 * Heads up ... Setup the CPU / MMU / TLB functions
	 */
	if (set_cpufuncs())
		panic("cpu not recognized!");

	/*
	 * Temporarily replace bus_space_map() functions so that
	 * console devices can get mapped.
	 */
	tmp_bs_tag = armv7_bs_tag;
	map_func_save = armv7_bs_tag.bs_map;
	armv7_bs_tag.bs_map = bootstrap_bs_map;
	tmp_bs_tag.bs_map = bootstrap_bs_map;

	/*
	 * Now, map the FDT area.
	 *
	 * As we don't know the size of a possible FDT, map the size of a
	 * typical bootstrap bs map.  The FDT might not be aligned, so this
	 * might take up to two L1_S_SIZEd mappings.
	 *
	 * XXX: There's (currently) no way to unmap a bootstrap mapping, so
	 * we might lose a bit of the bootstrap address space.
	 */
	bootstrap_bs_map(NULL, (bus_addr_t)arg2, L1_S_SIZE, 0,
	    (bus_space_handle_t *)&config);

	if (!fdt_init(config) || fdt_get_size(config) == 0)
		panic("initarm: no FDT");

	node = fdt_find_node("/chosen");
	if (node != NULL) {
		char *prop;
		int len;
		static uint8_t lladdr[6];

		len = fdt_node_property(node, "bootargs", &prop);
		if (len > 0)
			collect_kernel_args(prop);

		len = fdt_node_property(node, "openbsd,boothowto", &prop);
		if (len == sizeof(boothowto))
			boothowto = bemtoh32((uint32_t *)prop);

		len = fdt_node_property(node, "openbsd,bootduid", &prop);
		if (len == sizeof(bootduid))
			memcpy(bootduid, prop, sizeof(bootduid));

		len = fdt_node_property(node, "openbsd,bootmac", &prop);
		if (len == sizeof(lladdr)) {
			memcpy(lladdr, prop, sizeof(lladdr));
			bootmac = lladdr;
		}

		len = fdt_node_property(node, "openbsd,uefi-mmap-start", &prop);
		if (len == sizeof(mmap_start))
			mmap_start = bemtoh64((uint64_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-size", &prop);
		if (len == sizeof(mmap_size))
			mmap_size = bemtoh32((uint32_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-desc-size", &prop);
		if (len == sizeof(mmap_desc_size))
			mmap_desc_size = bemtoh32((uint32_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-desc-ver", &prop);
		if (len == sizeof(mmap_desc_ver))
			mmap_desc_ver = bemtoh32((uint32_t *)prop);

		len = fdt_node_property(node, "openbsd,dma-constraint", &prop);
		if (len == sizeof(uint64_t[2])) {
			dma_constraint.ucr_low = bemtoh64((uint64_t *)prop);
			dma_constraint.ucr_high = bemtoh64((uint64_t *)prop + 1);
		}
	}

	process_kernel_args();

	if (mmap_start != 0)
		bootstrap_bs_map(NULL, mmap_start, mmap_size, 0,
		    (bus_space_handle_t *)&mmap);

	platform_init();

	/* setup a serial console for very early boot */
	consinit();

	/* Talk to the user */
	printf("\nOpenBSD/armv7 booting ...\n");

	printf("arg0 %p arg1 %p arg2 %p\n", arg0, arg1, arg2);

#ifdef RAMDISK_HOOKS
	boothowto |= RB_DFLTROOT;
#endif /* RAMDISK_HOOKS */

	physical_freestart = (((unsigned long)esym - KERNEL_TEXT_BASE + 0xfff) & ~0xfff) + loadaddr;

	/* The bootloader has loaded us ubto a 32MB block. */
	memstart = loadaddr;
	memend = memstart + 32 * 1024 * 1024;

	/*
	 * Okay, the kernel starts 2MB in from the bottom of physical
	 * memory.  We are going to allocate our bootstrap pages downwards
	 * from there.
	 *
	 * We need to allocate some fixed page tables to get the kernel
	 * going.  We allocate one page directory and a number of page
	 * tables and store the physical addresses in the kernel_pt_table
	 * array.
	 *
	 * The kernel page directory must be on a 16K boundary.  The page
	 * tables must be on 4K boundaries.  What we do is allocate the
	 * page directory on the first 16K boundary that we encounter, and
	 * the page tables on 4K boundaries otherwise.  Since we allocate
	 * at least 3 L2 page tables, we are guaranteed to encounter at
	 * least one 16K aligned region.
	 */

#ifdef VERBOSE_INIT_ARM
	printf("Allocating page tables\n");
#endif

	/* Define a macro to simplify memory allocation */
#define	valloc_pages(var, np)				\
	alloc_pages((var).pv_pa, (np));			\
	(var).pv_va = KERNEL_BASE + (var).pv_pa - loadaddr;

#define alloc_pages(var, np)				\
	(var) = physical_freestart;			\
	physical_freestart += ((np) * PAGE_SIZE);	\
	if (physical_freestart > memend)		\
		panic("initarm: out of memory");	\
	memset((char *)(var), 0, ((np) * PAGE_SIZE));

	loop1 = 0;
	kernel_l1pt.pv_pa = 0;
	for (loop = 0; loop <= NUM_KERNEL_PTS; ++loop) {
		/* Are we 16KB aligned for an L1 ? */
		if (((physical_freestart) & (L1_TABLE_SIZE - 1)) == 0
		    && kernel_l1pt.pv_pa == 0) {
			valloc_pages(kernel_l1pt, L1_TABLE_SIZE / PAGE_SIZE);
		} else {
			valloc_pages(kernel_pt_table[loop1],
			    L2_TABLE_SIZE / PAGE_SIZE);
			++loop1;
		}
	}

	/* This should never be able to happen but better confirm that. */
	if (!kernel_l1pt.pv_pa || (kernel_l1pt.pv_pa & (L1_TABLE_SIZE-1)) != 0)
		panic("initarm: Failed to align the kernel page directory");

	/*
	 * Allocate a page for the system page mapped to V0x00000000
	 * This page will just contain the system vectors and can be
	 * shared by all processes.
	 */
	vector_page = ARM_VECTORS_HIGH;
	alloc_pages(systempage.pv_pa, 1);
	systempage.pv_va = vector_page;

	/* Allocate stacks for all modes */
	valloc_pages(irqstack, IRQ_STACK_SIZE);
	valloc_pages(abtstack, ABT_STACK_SIZE);
	valloc_pages(undstack, UND_STACK_SIZE);
	valloc_pages(kernelstack, UPAGES);

	/* Allocate enough pages for cleaning the Mini-Data cache. */

#ifdef VERBOSE_INIT_ARM
	printf("IRQ stack: p0x%08lx v0x%08lx\n", irqstack.pv_pa,
	    irqstack.pv_va);
	printf("ABT stack: p0x%08lx v0x%08lx\n", abtstack.pv_pa,
	    abtstack.pv_va);
	printf("UND stack: p0x%08lx v0x%08lx\n", undstack.pv_pa,
	    undstack.pv_va);
	printf("SVC stack: p0x%08lx v0x%08lx\n", kernelstack.pv_pa,
	    kernelstack.pv_va);
#endif

	/* Relocate the FDT to safe memory. */
	size = fdt_get_size(config);
	valloc_pages(fdt, round_page(size) / PAGE_SIZE);
	memcpy((void *)fdt.pv_pa, config, size);

	/* Relocate the EFI memory map too. */
	if (mmap_start != 0) {
		valloc_pages(map, round_page(mmap_size) / PAGE_SIZE);
		memcpy((void *)map.pv_pa, mmap, mmap_size);
	}

	/*
	 * XXX Defer this to later so that we can reclaim the memory
	 */
	alloc_pages(msgbufphys, round_page(MSGBUFSIZE) / PAGE_SIZE);

	/*
	 * Ok we have allocated physical pages for the primary kernel
	 * page tables
	 */

#ifdef VERBOSE_INIT_ARM
	printf("Creating L1 page table at 0x%08lx\n", kernel_l1pt.pv_pa);
#endif

	/*
	 * Now we start construction of the L1 page table
	 * We start by mapping the L2 page tables into the L1.
	 * This means that we can replace L1 mappings later on if necessary
	 */
	l1pagetable = kernel_l1pt.pv_pa;

	/* Map the L2 pages tables in the L1 page table */
	pmap_link_l2pt(l1pagetable, vector_page & ~(0x00400000 - 1),
	    &kernel_pt_table[KERNEL_PT_SYS]);

	for (loop = 0; loop < KERNEL_PT_KERNEL_NUM; loop++)
		pmap_link_l2pt(l1pagetable, KERNEL_BASE + loop * 0x00400000,
		    &kernel_pt_table[KERNEL_PT_KERNEL + loop]);

	for (loop = 0; loop < KERNEL_PT_VMDATA_NUM; loop++)
		pmap_link_l2pt(l1pagetable, KERNEL_VM_BASE + loop * 0x00400000,
		    &kernel_pt_table[KERNEL_PT_VMDATA + loop]);

	/* update the top of the kernel VM */
	pmap_curmaxkvaddr =
	    KERNEL_VM_BASE + (KERNEL_PT_VMDATA_NUM * 0x00400000);

#ifdef VERBOSE_INIT_ARM
	printf("Mapping kernel\n");
#endif

	/* Now we fill in the L2 pagetable for the kernel static code/data */
	{
		extern char __text_start[], _etext[];
		extern char __rodata_start[], _erodata[];
		size_t textsize = (u_int32_t) (_etext - __text_start);
		size_t rodatasize = (u_int32_t) (_erodata - __rodata_start);
		size_t totalsize = esym - (u_int32_t)__text_start;
		u_int logical;

		textsize = (textsize + PGOFSET) & ~PGOFSET;
		rodatasize = (rodatasize + PGOFSET) & ~PGOFSET;
		totalsize = (totalsize + PGOFSET) & ~PGOFSET;

		logical = 0x00300000;	/* offset of kernel in RAM */

		logical += pmap_map_chunk(l1pagetable, KERNEL_BASE + logical,
		    loadaddr + logical, textsize,
		    PROT_READ | PROT_EXEC, PTE_CACHE);
		logical += pmap_map_chunk(l1pagetable, KERNEL_BASE + logical,
		    loadaddr + logical, rodatasize,
		    PROT_READ, PTE_CACHE);
		logical += pmap_map_chunk(l1pagetable, KERNEL_BASE + logical,
		    loadaddr + logical, totalsize - (textsize + rodatasize),
		    PROT_READ | PROT_WRITE, PTE_CACHE);
	}

#ifdef VERBOSE_INIT_ARM
	printf("Constructing L2 page tables\n");
#endif

	/* Map the stack pages */
	pmap_map_chunk(l1pagetable, irqstack.pv_va, irqstack.pv_pa,
	    IRQ_STACK_SIZE * PAGE_SIZE, PROT_READ | PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, abtstack.pv_va, abtstack.pv_pa,
	    ABT_STACK_SIZE * PAGE_SIZE, PROT_READ | PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, undstack.pv_va, undstack.pv_pa,
	    UND_STACK_SIZE * PAGE_SIZE, PROT_READ | PROT_WRITE, PTE_CACHE);
	pmap_map_chunk(l1pagetable, kernelstack.pv_va, kernelstack.pv_pa,
	    UPAGES * PAGE_SIZE, PROT_READ | PROT_WRITE, PTE_CACHE);

	pmap_map_chunk(l1pagetable, kernel_l1pt.pv_va, kernel_l1pt.pv_pa,
	    L1_TABLE_SIZE, PROT_READ | PROT_WRITE, PTE_PAGETABLE);

	for (loop = 0; loop < NUM_KERNEL_PTS; ++loop) {
		pmap_map_chunk(l1pagetable, kernel_pt_table[loop].pv_va,
		    kernel_pt_table[loop].pv_pa, L2_TABLE_SIZE,
		    PROT_READ | PROT_WRITE, PTE_PAGETABLE);
	}

	/* Map the Mini-Data cache clean area. */

	/* Map the vector page. */
	pmap_map_entry(l1pagetable, vector_page, systempage.pv_pa,
	    PROT_READ | PROT_WRITE, PTE_CACHE);

	/* Map the FDT. */
	pmap_map_chunk(l1pagetable, fdt.pv_va, fdt.pv_pa,
	    round_page(fdt_get_size((void *)fdt.pv_pa)),
	    PROT_READ | PROT_WRITE, PTE_CACHE);

	/* Map the EFI memory map. */
	if (mmap_start != 0) {
		pmap_map_chunk(l1pagetable, map.pv_va, map.pv_pa,
		    round_page(mmap_size),
		    PROT_READ | PROT_WRITE, PTE_CACHE);
		mmap = (void *)map.pv_va;
	}

	/*
	 * map integrated peripherals at same address in l1pagetable
	 * so that we can continue to use console.
	 */
	copy_io_area_map((pd_entry_t *)l1pagetable);

	/*
	 * Now we have the real page tables in place so we can switch to them.
	 * Once this is done we will be running with the REAL kernel page
	 * tables.
	 */
	setttb(kernel_l1pt.pv_pa);
	cpu_tlb_flushID();

	/*
	 * Moved from cpu_startup() as data_abort_handler() references
	 * this during uvm init
	 */
	proc0paddr = (struct user *)kernelstack.pv_va;
	proc0.p_addr = proc0paddr;

	arm32_vector_init(vector_page, ARM_VEC_ALL);

	/*
	 * Pages were allocated during the secondary bootstrap for the
	 * stacks for different CPU modes.
	 * We must now set the r13 registers in the different CPU modes to
	 * point to these stacks.
	 * Since the ARM stacks use STMFD etc. we must set r13 to the top end
	 * of the stack memory.
	 */

	set_stackptr(PSR_IRQ32_MODE,
	    irqstack.pv_va + IRQ_STACK_SIZE * PAGE_SIZE);
	set_stackptr(PSR_ABT32_MODE,
	    abtstack.pv_va + ABT_STACK_SIZE * PAGE_SIZE);
	set_stackptr(PSR_UND32_MODE,
	    undstack.pv_va + UND_STACK_SIZE * PAGE_SIZE);

	/*
	 * Well we should set a data abort handler.
	 * Once things get going this will change as we will need a proper
	 * handler.
	 * Until then we will use a handler that just panics but tells us
	 * why.
	 * Initialisation of the vectors will just panic on a data abort.
	 * This just fills in a slightly better one.
	 */

	data_abort_handler_address = (u_int)data_abort_handler;
	prefetch_abort_handler_address = (u_int)prefetch_abort_handler;
	undefined_handler_address = (u_int)undefinedinstruction_bounce;

	/* Now we can reinit the FDT, using the virtual address. */
	fdt_init((void *)fdt.pv_va);

	/* Initialise the undefined instruction handlers */
#ifdef VERBOSE_INIT_ARM
	printf("undefined ");
#endif
	undefined_init();

	/* Load memory into UVM. */
#ifdef VERBOSE_INIT_ARM
	printf("page ");
#endif
	uvm_setpagesize();        /* initialize PAGE_SIZE-dependent variables */

	/* Make what's left of the initial 32MB block available to UVM. */
	uvm_page_physload(atop(physical_freestart), atop(memend),
	    atop(physical_freestart), atop(memend), 0);
	physmem = atop(memend - memstart);

	/* Make all other physical memory available to UVM. */
	if (mmap && mmap_desc_ver == EFI_MEMORY_DESCRIPTOR_VERSION) {
		EFI_MEMORY_DESCRIPTOR *desc = mmap;
		int i;

		/*
		 * Load all memory marked as EfiConventionalMemory.
		 * Don't bother with blocks smaller than 64KB.  The
		 * initial 64MB memory block should be marked as
		 * EfiLoaderData so it won't be added again here.
		 */
		for (i = 0; i < mmap_size / mmap_desc_size; i++) {
			printf("type 0x%x pa 0x%llx va 0x%llx pages 0x%llx attr 0x%llx\n",
			    desc->Type, desc->PhysicalStart,
			    desc->VirtualStart, desc->NumberOfPages,
			    desc->Attribute);
			if (desc->Type == EfiConventionalMemory &&
			    desc->NumberOfPages >= 16) {
				uvm_page_physload(atop(desc->PhysicalStart),
				    atop(desc->PhysicalStart) +
				    desc->NumberOfPages,
				    atop(desc->PhysicalStart),
				    atop(desc->PhysicalStart) +
				    desc->NumberOfPages, 0);
				physmem += desc->NumberOfPages;
			}
			desc = NextMemoryDescriptor(desc, mmap_desc_size);
		}
	} else {
		paddr_t start, end;
		int i;

		node = fdt_find_node("/memory");
		if (node == NULL)
			panic("%s: no memory specified", __func__);

		for (i = 0; i < VM_PHYSSEG_MAX; i++) {
			if (fdt_get_reg(node, i, &reg))
				break;
			if (reg.size == 0)
				continue;

			start = reg.addr;
			end = MIN(reg.addr + reg.size, (paddr_t)-PAGE_SIZE);

			/*
			 * The initial 32MB block is not excluded, so we need
			 * to make sure we don't add it here.
			 */
			if (start < memend && end > memstart) {
				if (start < memstart) {
					uvm_page_physload(atop(start),
					    atop(memstart), atop(start),
					    atop(memstart), 0);
					physmem += atop(memstart - start);
				}
				if (end > memend) {
					uvm_page_physload(atop(memend),
					    atop(end), atop(memend),
					    atop(end), 0);
					physmem += atop(end - memend);
				}
			} else {
				uvm_page_physload(atop(start), atop(end),
				    atop(start), atop(end), 0);
				physmem += atop(end - start);
			}
		}
	}

	/* Boot strap pmap telling it where the kernel page table is */
#ifdef VERBOSE_INIT_ARM
	printf("pmap ");
#endif
	pmap_bootstrap((pd_entry_t *)kernel_l1pt.pv_va, KERNEL_VM_BASE,
	    KERNEL_VM_BASE + KERNEL_VM_SIZE);

	vector_page_setprot(PROT_READ | PROT_EXEC);

	/*
	 * Restore proper bus_space operation, now that pmap is initialized.
	 */
	armv7_bs_tag.bs_map = map_func_save;

#ifdef DDB
	db_machine_init();

	/* Firmware doesn't load symbols. */
	ddb_init();

	if (boothowto & RB_KDB)
		db_enter();
#endif

	cpu_setup();

	/* We return the new stack pointer address */
	return(kernelstack.pv_va + USPACE_SVC_STACK_TOP);
}

char	bootargs[256];

void
collect_kernel_args(const char *args)
{
	/* Make a local copy of the bootargs */
	strlcpy(bootargs, args, sizeof(bootargs));
}

void
process_kernel_args(void)
{
	char *cp = bootargs;

	if (*cp == 0)
		return;

	boot_file = bootargs;

	/* Skip the kernel image filename */
	while (*cp != ' ' && *cp != 0)
		cp++;

	if (*cp != 0)
		*cp++ = 0;

	while (*cp == ' ')
		cp++;

	boot_args = cp;

	printf("bootfile: %s\n", boot_file);
	printf("bootargs: %s\n", boot_args);

	/* Setup pointer to boot flags */
	while (*cp != '-')
		if (*cp++ == '\0')
			return;

	while (*cp != 0) {
		switch(*cp) {
		case 'a':
			boothowto |= RB_ASKNAME;
			break;
		case 'c':
			boothowto |= RB_CONFIG;
			break;
		case 'd':
			boothowto |= RB_KDB;
			break;
		case 's':
			boothowto |= RB_SINGLE;
			break;
		default:
			printf("unknown option `%c'\n", *cp);
			break;
		}
		cp++;
	}
}

static int
atoi(const char *s)
{
	int n, neg;

	n = 0;
	neg = 0;

	while (*s == '-') {
		s++;
		neg = !neg;
	}

	while (*s != '\0') {
		if (*s < '0' || *s > '9')
			break;

		n = (10 * n) + (*s - '0');
		s++;
	}

	return (neg ? -n : n);
}

void *
fdt_find_cons(const char *name)
{
	char *alias = "serial0";
	char buf[128];
	char *stdout = NULL;
	char *p;
	void *node;

	/* First check if "stdout-path" is set. */
	node = fdt_find_node("/chosen");
	if (node) {
		if (fdt_node_property(node, "stdout-path", &stdout) > 0) {
			if (strchr(stdout, ':') != NULL) {
				strlcpy(buf, stdout, sizeof(buf));
				if ((p = strchr(buf, ':')) != NULL) {
					*p++ = '\0';
					stdout_speed = atoi(p);
				}
				stdout = buf;
			}
			if (stdout[0] != '/') {
				/* It's an alias. */
				alias = stdout;
				stdout = NULL;
			}
		}
	}

	/* Perform alias lookup if necessary. */
	if (stdout == NULL) {
		node = fdt_find_node("/aliases");
		if (node)
			fdt_node_property(node, alias, &stdout);
	}

	/* Lookup the physical address of the interface. */
	if (stdout) {
		node = fdt_find_node(stdout);
		if (node && fdt_is_compatible(node, name)) {
			stdout_node = OF_finddevice(stdout);
			return (node);
		}
	}

	return (NULL);
}

void
consinit(void)
{
	static int consinit_called = 0;

	if (consinit_called != 0)
		return;

	consinit_called = 1;

	platform_init_cons();
}

void
board_startup(void)
{
        if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}
}

unsigned int
cpu_rnd_messybits(void)
{
	struct timespec ts;

	nanotime(&ts);
	return (ts.tv_nsec ^ (ts.tv_sec << 20));
}
