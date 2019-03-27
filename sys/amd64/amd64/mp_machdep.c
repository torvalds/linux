/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, by Steve Passe
 * Copyright (c) 2003, by Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cpu.h"
#include "opt_ddb.h"
#include "opt_kstack_pages.h"
#include "opt_sched.h"
#include "opt_smp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpuset.h>
#ifdef GPROF 
#include <sys/gmon.h>
#endif
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>

#include <x86/apicreg.h>
#include <machine/clock.h>
#include <machine/cputypes.h>
#include <machine/cpufunc.h>
#include <x86/mca.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/smp.h>
#include <machine/specialreg.h>
#include <machine/tss.h>
#include <x86/ucode.h>
#include <machine/cpu.h>
#include <x86/init.h>

#define WARMBOOT_TARGET		0
#define WARMBOOT_OFF		(KERNBASE + 0x0467)
#define WARMBOOT_SEG		(KERNBASE + 0x0469)

#define CMOS_REG		(0x70)
#define CMOS_DATA		(0x71)
#define BIOS_RESET		(0x0f)
#define BIOS_WARM		(0x0a)

#define GiB(v)			(v ## ULL << 30)

#define	AP_BOOTPT_SZ		(PAGE_SIZE * 3)

extern	struct pcpu __pcpu[];

/* Temporary variables for init_secondary()  */
char *doublefault_stack;
char *mce_stack;
char *nmi_stack;
char *dbg_stack;

/*
 * Local data and functions.
 */

static int	start_ap(int apic_id);

static bool
is_kernel_paddr(vm_paddr_t pa)
{

	return (pa >= trunc_2mpage(btext - KERNBASE) &&
	   pa < round_page(_end - KERNBASE));
}

static bool
is_mpboot_good(vm_paddr_t start, vm_paddr_t end)
{

	return (start + AP_BOOTPT_SZ <= GiB(4) && atop(end) < Maxmem);
}

/*
 * Calculate usable address in base memory for AP trampoline code.
 */
void
mp_bootaddress(vm_paddr_t *physmap, unsigned int *physmap_idx)
{
	vm_paddr_t start, end;
	unsigned int i;
	bool allocated;

	alloc_ap_trampoline(physmap, physmap_idx);

	/*
	 * Find a memory region big enough below the 4GB boundary to
	 * store the initial page tables.  Region must be mapped by
	 * the direct map.
	 *
	 * Note that it needs to be aligned to a page boundary.
	 */
	allocated = false;
	for (i = *physmap_idx; i <= *physmap_idx; i -= 2) {
		/*
		 * First, try to chomp at the start of the physmap region.
		 * Kernel binary might claim it already.
		 */
		start = round_page(physmap[i]);
		end = start + AP_BOOTPT_SZ;
		if (start < end && end <= physmap[i + 1] &&
		    is_mpboot_good(start, end) &&
		    !is_kernel_paddr(start) && !is_kernel_paddr(end - 1)) {
			allocated = true;
			physmap[i] = end;
			break;
		}

		/*
		 * Second, try to chomp at the end.  Again, check
		 * against kernel.
		 */
		end = trunc_page(physmap[i + 1]);
		start = end - AP_BOOTPT_SZ;
		if (start < end && start >= physmap[i] &&
		    is_mpboot_good(start, end) &&
		    !is_kernel_paddr(start) && !is_kernel_paddr(end - 1)) {
			allocated = true;
			physmap[i + 1] = start;
			break;
		}
	}
	if (allocated) {
		mptramp_pagetables = start;
		if (physmap[i] == physmap[i + 1] && *physmap_idx != 0) {
			memmove(&physmap[i], &physmap[i + 2],
			    sizeof(*physmap) * (*physmap_idx - i + 2));
			*physmap_idx -= 2;
		}
	} else {
		mptramp_pagetables = trunc_page(boot_address) - AP_BOOTPT_SZ;
		if (bootverbose)
			printf(
"Cannot find enough space for the initial AP page tables, placing them at %#x",
			    mptramp_pagetables);
	}
}

/*
 * Initialize the IPI handlers and start up the AP's.
 */
void
cpu_mp_start(void)
{
	int i;

	/* Initialize the logical ID to APIC ID table. */
	for (i = 0; i < MAXCPU; i++) {
		cpu_apic_ids[i] = -1;
		cpu_ipi_pending[i] = 0;
	}

	/* Install an inter-CPU IPI for TLB invalidation */
	if (pmap_pcid_enabled) {
		if (invpcid_works) {
			setidt(IPI_INVLTLB, pti ?
			    IDTVEC(invltlb_invpcid_pti_pti) :
			    IDTVEC(invltlb_invpcid_nopti), SDT_SYSIGT,
			    SEL_KPL, 0);
			setidt(IPI_INVLPG, pti ? IDTVEC(invlpg_invpcid_pti) :
			    IDTVEC(invlpg_invpcid), SDT_SYSIGT, SEL_KPL, 0);
			setidt(IPI_INVLRNG, pti ? IDTVEC(invlrng_invpcid_pti) :
			    IDTVEC(invlrng_invpcid), SDT_SYSIGT, SEL_KPL, 0);
		} else {
			setidt(IPI_INVLTLB, pti ? IDTVEC(invltlb_pcid_pti) :
			    IDTVEC(invltlb_pcid), SDT_SYSIGT, SEL_KPL, 0);
			setidt(IPI_INVLPG, pti ? IDTVEC(invlpg_pcid_pti) :
			    IDTVEC(invlpg_pcid), SDT_SYSIGT, SEL_KPL, 0);
			setidt(IPI_INVLRNG, pti ? IDTVEC(invlrng_pcid_pti) :
			    IDTVEC(invlrng_pcid), SDT_SYSIGT, SEL_KPL, 0);
		}
	} else {
		setidt(IPI_INVLTLB, pti ? IDTVEC(invltlb_pti) : IDTVEC(invltlb),
		    SDT_SYSIGT, SEL_KPL, 0);
		setidt(IPI_INVLPG, pti ? IDTVEC(invlpg_pti) : IDTVEC(invlpg),
		    SDT_SYSIGT, SEL_KPL, 0);
		setidt(IPI_INVLRNG, pti ? IDTVEC(invlrng_pti) : IDTVEC(invlrng),
		    SDT_SYSIGT, SEL_KPL, 0);
	}

	/* Install an inter-CPU IPI for cache invalidation. */
	setidt(IPI_INVLCACHE, pti ? IDTVEC(invlcache_pti) : IDTVEC(invlcache),
	    SDT_SYSIGT, SEL_KPL, 0);

	/* Install an inter-CPU IPI for all-CPU rendezvous */
	setidt(IPI_RENDEZVOUS, pti ? IDTVEC(rendezvous_pti) :
	    IDTVEC(rendezvous), SDT_SYSIGT, SEL_KPL, 0);

	/* Install generic inter-CPU IPI handler */
	setidt(IPI_BITMAP_VECTOR, pti ? IDTVEC(ipi_intr_bitmap_handler_pti) :
	    IDTVEC(ipi_intr_bitmap_handler), SDT_SYSIGT, SEL_KPL, 0);

	/* Install an inter-CPU IPI for CPU stop/restart */
	setidt(IPI_STOP, pti ? IDTVEC(cpustop_pti) : IDTVEC(cpustop),
	    SDT_SYSIGT, SEL_KPL, 0);

	/* Install an inter-CPU IPI for CPU suspend/resume */
	setidt(IPI_SUSPEND, pti ? IDTVEC(cpususpend_pti) : IDTVEC(cpususpend),
	    SDT_SYSIGT, SEL_KPL, 0);

	/* Set boot_cpu_id if needed. */
	if (boot_cpu_id == -1) {
		boot_cpu_id = PCPU_GET(apic_id);
		cpu_info[boot_cpu_id].cpu_bsp = 1;
	} else
		KASSERT(boot_cpu_id == PCPU_GET(apic_id),
		    ("BSP's APIC ID doesn't match boot_cpu_id"));

	/* Probe logical/physical core configuration. */
	topo_probe();

	assign_cpu_ids();

	/* Start each Application Processor */
	init_ops.start_all_aps();

	set_interrupt_apic_ids();
}


/*
 * AP CPU's call this to initialize themselves.
 */
void
init_secondary(void)
{
	struct pcpu *pc;
	struct nmi_pcpu *np;
	u_int64_t cr0;
	int cpu, gsel_tss, x;
	struct region_descriptor ap_gdt;

	/* Set by the startup code for us to use */
	cpu = bootAP;

	/* Update microcode before doing anything else. */
	ucode_load_ap(cpu);

	/* Init tss */
	common_tss[cpu] = common_tss[0];
	common_tss[cpu].tss_iobase = sizeof(struct amd64tss) +
	    IOPERM_BITMAP_SIZE;
	common_tss[cpu].tss_ist1 = (long)&doublefault_stack[PAGE_SIZE];

	/* The NMI stack runs on IST2. */
	np = ((struct nmi_pcpu *) &nmi_stack[PAGE_SIZE]) - 1;
	common_tss[cpu].tss_ist2 = (long) np;

	/* The MC# stack runs on IST3. */
	np = ((struct nmi_pcpu *) &mce_stack[PAGE_SIZE]) - 1;
	common_tss[cpu].tss_ist3 = (long) np;

	/* The DB# stack runs on IST4. */
	np = ((struct nmi_pcpu *) &dbg_stack[PAGE_SIZE]) - 1;
	common_tss[cpu].tss_ist4 = (long) np;

	/* Prepare private GDT */
	gdt_segs[GPROC0_SEL].ssd_base = (long) &common_tss[cpu];
	for (x = 0; x < NGDT; x++) {
		if (x != GPROC0_SEL && x != (GPROC0_SEL + 1) &&
		    x != GUSERLDT_SEL && x != (GUSERLDT_SEL + 1))
			ssdtosd(&gdt_segs[x], &gdt[NGDT * cpu + x]);
	}
	ssdtosyssd(&gdt_segs[GPROC0_SEL],
	    (struct system_segment_descriptor *)&gdt[NGDT * cpu + GPROC0_SEL]);
	ap_gdt.rd_limit = NGDT * sizeof(gdt[0]) - 1;
	ap_gdt.rd_base =  (long) &gdt[NGDT * cpu];
	lgdt(&ap_gdt);			/* does magic intra-segment return */

	/* Get per-cpu data */
	pc = &__pcpu[cpu];

	/* prime data page for it to use */
	pcpu_init(pc, cpu, sizeof(struct pcpu));
	dpcpu_init(dpcpu, cpu);
	pc->pc_apic_id = cpu_apic_ids[cpu];
	pc->pc_prvspace = pc;
	pc->pc_curthread = 0;
	pc->pc_tssp = &common_tss[cpu];
	pc->pc_commontssp = &common_tss[cpu];
	pc->pc_rsp0 = 0;
	pc->pc_pti_rsp0 = (((vm_offset_t)&pc->pc_pti_stack +
	    PC_PTI_STACK_SZ * sizeof(uint64_t)) & ~0xful);
	pc->pc_tss = (struct system_segment_descriptor *)&gdt[NGDT * cpu +
	    GPROC0_SEL];
	pc->pc_fs32p = &gdt[NGDT * cpu + GUFS32_SEL];
	pc->pc_gs32p = &gdt[NGDT * cpu + GUGS32_SEL];
	pc->pc_ldt = (struct system_segment_descriptor *)&gdt[NGDT * cpu +
	    GUSERLDT_SEL];
	/* See comment in pmap_bootstrap(). */
	pc->pc_pcid_next = PMAP_PCID_KERN + 2;
	pc->pc_pcid_gen = 1;
	common_tss[cpu].tss_rsp0 = 0;

	/* Save the per-cpu pointer for use by the NMI handler. */
	np = ((struct nmi_pcpu *) &nmi_stack[PAGE_SIZE]) - 1;
	np->np_pcpu = (register_t) pc;

	/* Save the per-cpu pointer for use by the MC# handler. */
	np = ((struct nmi_pcpu *) &mce_stack[PAGE_SIZE]) - 1;
	np->np_pcpu = (register_t) pc;

	/* Save the per-cpu pointer for use by the DB# handler. */
	np = ((struct nmi_pcpu *) &dbg_stack[PAGE_SIZE]) - 1;
	np->np_pcpu = (register_t) pc;

	wrmsr(MSR_FSBASE, 0);		/* User value */
	wrmsr(MSR_GSBASE, (u_int64_t)pc);
	wrmsr(MSR_KGSBASE, (u_int64_t)pc);	/* XXX User value while we're in the kernel */
	fix_cpuid();

	lidt(&r_idt);

	gsel_tss = GSEL(GPROC0_SEL, SEL_KPL);
	ltr(gsel_tss);

	/*
	 * Set to a known state:
	 * Set by mpboot.s: CR0_PG, CR0_PE
	 * Set by cpu_setregs: CR0_NE, CR0_MP, CR0_TS, CR0_WP, CR0_AM
	 */
	cr0 = rcr0();
	cr0 &= ~(CR0_CD | CR0_NW | CR0_EM);
	load_cr0(cr0);

	amd64_conf_fast_syscall();

	/* signal our startup to the BSP. */
	mp_naps++;

	/* Spin until the BSP releases the AP's. */
	while (atomic_load_acq_int(&aps_ready) == 0)
		ia32_pause();

	init_secondary_tail();
}

/*******************************************************************
 * local functions and data
 */

/*
 * start each AP in our list
 */
int
native_start_all_aps(void)
{
	u_int64_t *pt4, *pt3, *pt2;
	u_int32_t mpbioswarmvec;
	int apic_id, cpu, i;
	u_char mpbiosreason;

	mtx_init(&ap_boot_mtx, "ap boot", NULL, MTX_SPIN);

	/* copy the AP 1st level boot code */
	bcopy(mptramp_start, (void *)PHYS_TO_DMAP(boot_address), bootMP_size);

	/* Locate the page tables, they'll be below the trampoline */
	pt4 = (uint64_t *)PHYS_TO_DMAP(mptramp_pagetables);
	pt3 = pt4 + (PAGE_SIZE) / sizeof(u_int64_t);
	pt2 = pt3 + (PAGE_SIZE) / sizeof(u_int64_t);

	/* Create the initial 1GB replicated page tables */
	for (i = 0; i < 512; i++) {
		/* Each slot of the level 4 pages points to the same level 3 page */
		pt4[i] = (u_int64_t)(uintptr_t)(mptramp_pagetables + PAGE_SIZE);
		pt4[i] |= PG_V | PG_RW | PG_U;

		/* Each slot of the level 3 pages points to the same level 2 page */
		pt3[i] = (u_int64_t)(uintptr_t)(mptramp_pagetables + (2 * PAGE_SIZE));
		pt3[i] |= PG_V | PG_RW | PG_U;

		/* The level 2 page slots are mapped with 2MB pages for 1GB. */
		pt2[i] = i * (2 * 1024 * 1024);
		pt2[i] |= PG_V | PG_RW | PG_PS | PG_U;
	}

	/* save the current value of the warm-start vector */
	mpbioswarmvec = *((u_int32_t *) WARMBOOT_OFF);
	outb(CMOS_REG, BIOS_RESET);
	mpbiosreason = inb(CMOS_DATA);

	/* setup a vector to our boot code */
	*((volatile u_short *) WARMBOOT_OFF) = WARMBOOT_TARGET;
	*((volatile u_short *) WARMBOOT_SEG) = (boot_address >> 4);
	outb(CMOS_REG, BIOS_RESET);
	outb(CMOS_DATA, BIOS_WARM);	/* 'warm-start' */

	/* start each AP */
	for (cpu = 1; cpu < mp_ncpus; cpu++) {
		apic_id = cpu_apic_ids[cpu];

		/* allocate and set up an idle stack data page */
		bootstacks[cpu] = (void *)kmem_malloc(kstack_pages * PAGE_SIZE,
		    M_WAITOK | M_ZERO);
		doublefault_stack = (char *)kmem_malloc(PAGE_SIZE, M_WAITOK |
		    M_ZERO);
		mce_stack = (char *)kmem_malloc(PAGE_SIZE, M_WAITOK | M_ZERO);
		nmi_stack = (char *)kmem_malloc(PAGE_SIZE, M_WAITOK | M_ZERO);
		dbg_stack = (char *)kmem_malloc(PAGE_SIZE, M_WAITOK | M_ZERO);
		dpcpu = (void *)kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);

		bootSTK = (char *)bootstacks[cpu] + kstack_pages * PAGE_SIZE - 8;
		bootAP = cpu;

		/* attempt to start the Application Processor */
		if (!start_ap(apic_id)) {
			/* restore the warmstart vector */
			*(u_int32_t *) WARMBOOT_OFF = mpbioswarmvec;
			panic("AP #%d (PHY# %d) failed!", cpu, apic_id);
		}

		CPU_SET(cpu, &all_cpus);	/* record AP in CPU map */
	}

	/* restore the warmstart vector */
	*(u_int32_t *) WARMBOOT_OFF = mpbioswarmvec;

	outb(CMOS_REG, BIOS_RESET);
	outb(CMOS_DATA, mpbiosreason);

	/* number of APs actually started */
	return mp_naps;
}


/*
 * This function starts the AP (application processor) identified
 * by the APIC ID 'physicalCpu'.  It does quite a "song and dance"
 * to accomplish this.  This is necessary because of the nuances
 * of the different hardware we might encounter.  It isn't pretty,
 * but it seems to work.
 */
static int
start_ap(int apic_id)
{
	int vector, ms;
	int cpus;

	/* calculate the vector */
	vector = (boot_address >> 12) & 0xff;

	/* used as a watchpoint to signal AP startup */
	cpus = mp_naps;

	ipi_startup(apic_id, vector);

	/* Wait up to 5 seconds for it to start. */
	for (ms = 0; ms < 5000; ms++) {
		if (mp_naps > cpus)
			return 1;	/* return SUCCESS */
		DELAY(1000);
	}
	return 0;		/* return FAILURE */
}

void
invltlb_invpcid_handler(void)
{
	struct invpcid_descr d;
	uint32_t generation;

#ifdef COUNT_XINVLTLB_HITS
	xhits_gbl[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invltlb_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	generation = smp_tlb_generation;
	d.pcid = smp_tlb_pmap->pm_pcids[PCPU_GET(cpuid)].pm_pcid;
	d.pad = 0;
	d.addr = 0;
	invpcid(&d, smp_tlb_pmap == kernel_pmap ? INVPCID_CTXGLOB :
	    INVPCID_CTX);
	PCPU_SET(smp_tlb_done, generation);
}

void
invltlb_invpcid_pti_handler(void)
{
	struct invpcid_descr d;
	uint32_t generation;

#ifdef COUNT_XINVLTLB_HITS
	xhits_gbl[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invltlb_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	generation = smp_tlb_generation;
	d.pcid = smp_tlb_pmap->pm_pcids[PCPU_GET(cpuid)].pm_pcid;
	d.pad = 0;
	d.addr = 0;
	if (smp_tlb_pmap == kernel_pmap) {
		/*
		 * This invalidation actually needs to clear kernel
		 * mappings from the TLB in the current pmap, but
		 * since we were asked for the flush in the kernel
		 * pmap, achieve it by performing global flush.
		 */
		invpcid(&d, INVPCID_CTXGLOB);
	} else {
		invpcid(&d, INVPCID_CTX);
		d.pcid |= PMAP_PCID_USER_PT;
		invpcid(&d, INVPCID_CTX);
	}
	PCPU_SET(smp_tlb_done, generation);
}

void
invltlb_pcid_handler(void)
{
	uint64_t kcr3, ucr3;
	uint32_t generation, pcid;
  
#ifdef COUNT_XINVLTLB_HITS
	xhits_gbl[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invltlb_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	generation = smp_tlb_generation;	/* Overlap with serialization */
	if (smp_tlb_pmap == kernel_pmap) {
		invltlb_glob();
	} else {
		/*
		 * The current pmap might not be equal to
		 * smp_tlb_pmap.  The clearing of the pm_gen in
		 * pmap_invalidate_all() takes care of TLB
		 * invalidation when switching to the pmap on this
		 * CPU.
		 */
		if (PCPU_GET(curpmap) == smp_tlb_pmap) {
			pcid = smp_tlb_pmap->pm_pcids[PCPU_GET(cpuid)].pm_pcid;
			kcr3 = smp_tlb_pmap->pm_cr3 | pcid;
			ucr3 = smp_tlb_pmap->pm_ucr3;
			if (ucr3 != PMAP_NO_CR3) {
				ucr3 |= PMAP_PCID_USER_PT | pcid;
				pmap_pti_pcid_invalidate(ucr3, kcr3);
			} else
				load_cr3(kcr3);
		}
	}
	PCPU_SET(smp_tlb_done, generation);
}

void
invlpg_invpcid_handler(void)
{
	struct invpcid_descr d;
	uint32_t generation;

#ifdef COUNT_XINVLTLB_HITS
	xhits_pg[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invlpg_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	generation = smp_tlb_generation;	/* Overlap with serialization */
	invlpg(smp_tlb_addr1);
	if (smp_tlb_pmap->pm_ucr3 != PMAP_NO_CR3) {
		d.pcid = smp_tlb_pmap->pm_pcids[PCPU_GET(cpuid)].pm_pcid |
		    PMAP_PCID_USER_PT;
		d.pad = 0;
		d.addr = smp_tlb_addr1;
		invpcid(&d, INVPCID_ADDR);
	}
	PCPU_SET(smp_tlb_done, generation);
}

void
invlpg_pcid_handler(void)
{
	uint64_t kcr3, ucr3;
	uint32_t generation;
	uint32_t pcid;

#ifdef COUNT_XINVLTLB_HITS
	xhits_pg[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invlpg_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	generation = smp_tlb_generation;	/* Overlap with serialization */
	invlpg(smp_tlb_addr1);
	if (smp_tlb_pmap == PCPU_GET(curpmap) &&
	    (ucr3 = smp_tlb_pmap->pm_ucr3) != PMAP_NO_CR3) {
		pcid = smp_tlb_pmap->pm_pcids[PCPU_GET(cpuid)].pm_pcid;
		kcr3 = smp_tlb_pmap->pm_cr3 | pcid | CR3_PCID_SAVE;
		ucr3 |= pcid | PMAP_PCID_USER_PT | CR3_PCID_SAVE;
		pmap_pti_pcid_invlpg(ucr3, kcr3, smp_tlb_addr1);
	}
	PCPU_SET(smp_tlb_done, generation);
}

void
invlrng_invpcid_handler(void)
{
	struct invpcid_descr d;
	vm_offset_t addr, addr2;
	uint32_t generation;

#ifdef COUNT_XINVLTLB_HITS
	xhits_rng[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invlrng_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	addr = smp_tlb_addr1;
	addr2 = smp_tlb_addr2;
	generation = smp_tlb_generation;	/* Overlap with serialization */
	do {
		invlpg(addr);
		addr += PAGE_SIZE;
	} while (addr < addr2);
	if (smp_tlb_pmap->pm_ucr3 != PMAP_NO_CR3) {
		d.pcid = smp_tlb_pmap->pm_pcids[PCPU_GET(cpuid)].pm_pcid |
		    PMAP_PCID_USER_PT;
		d.pad = 0;
		d.addr = smp_tlb_addr1;
		do {
			invpcid(&d, INVPCID_ADDR);
			d.addr += PAGE_SIZE;
		} while (d.addr < addr2);
	}
	PCPU_SET(smp_tlb_done, generation);
}

void
invlrng_pcid_handler(void)
{
	vm_offset_t addr, addr2;
	uint64_t kcr3, ucr3;
	uint32_t generation;
	uint32_t pcid;

#ifdef COUNT_XINVLTLB_HITS
	xhits_rng[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invlrng_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	addr = smp_tlb_addr1;
	addr2 = smp_tlb_addr2;
	generation = smp_tlb_generation;	/* Overlap with serialization */
	do {
		invlpg(addr);
		addr += PAGE_SIZE;
	} while (addr < addr2);
	if (smp_tlb_pmap == PCPU_GET(curpmap) &&
	    (ucr3 = smp_tlb_pmap->pm_ucr3) != PMAP_NO_CR3) {
		pcid = smp_tlb_pmap->pm_pcids[PCPU_GET(cpuid)].pm_pcid;
		kcr3 = smp_tlb_pmap->pm_cr3 | pcid | CR3_PCID_SAVE;
		ucr3 |= pcid | PMAP_PCID_USER_PT | CR3_PCID_SAVE;
		pmap_pti_pcid_invlrng(ucr3, kcr3, smp_tlb_addr1, addr2);
	}
	PCPU_SET(smp_tlb_done, generation);
}
