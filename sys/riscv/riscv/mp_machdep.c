/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * Copyright (c) 2016 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include "opt_kstack_pages.h"
#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_map.h>

#include <machine/intr.h>
#include <machine/smp.h>
#include <machine/sbi.h>

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_cpu.h>
#endif

boolean_t ofw_cpu_reg(phandle_t node, u_int, cell_t *);

extern struct pcpu __pcpu[];

uint32_t __riscv_boot_ap[MAXCPU];

static enum {
	CPUS_UNKNOWN,
#ifdef FDT
	CPUS_FDT,
#endif
} cpu_enum_method;

static device_identify_t riscv64_cpu_identify;
static device_probe_t riscv64_cpu_probe;
static device_attach_t riscv64_cpu_attach;

static int ipi_handler(void *);

struct mtx ap_boot_mtx;
struct pcb stoppcbs[MAXCPU];

#ifdef INVARIANTS
static uint32_t cpu_reg[MAXCPU][2];
#endif
static device_t cpu_list[MAXCPU];

void mpentry(unsigned long cpuid);
void init_secondary(uint64_t);

uint8_t secondary_stacks[MAXCPU - 1][PAGE_SIZE * KSTACK_PAGES] __aligned(16);

/* Set to 1 once we're ready to let the APs out of the pen. */
volatile int aps_ready = 0;

/* Temporary variables for init_secondary()  */
void *dpcpu[MAXCPU - 1];

static device_method_t riscv64_cpu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	riscv64_cpu_identify),
	DEVMETHOD(device_probe,		riscv64_cpu_probe),
	DEVMETHOD(device_attach,	riscv64_cpu_attach),

	DEVMETHOD_END
};

static devclass_t riscv64_cpu_devclass;
static driver_t riscv64_cpu_driver = {
	"riscv64_cpu",
	riscv64_cpu_methods,
	0
};

DRIVER_MODULE(riscv64_cpu, cpu, riscv64_cpu_driver, riscv64_cpu_devclass, 0, 0);

static void
riscv64_cpu_identify(driver_t *driver, device_t parent)
{

	if (device_find_child(parent, "riscv64_cpu", -1) != NULL)
		return;
	if (BUS_ADD_CHILD(parent, 0, "riscv64_cpu", -1) == NULL)
		device_printf(parent, "add child failed\n");
}

static int
riscv64_cpu_probe(device_t dev)
{
	u_int cpuid;

	cpuid = device_get_unit(dev);
	if (cpuid >= MAXCPU || cpuid > mp_maxid)
		return (EINVAL);

	device_quiet(dev);
	return (0);
}

static int
riscv64_cpu_attach(device_t dev)
{
	const uint32_t *reg;
	size_t reg_size;
	u_int cpuid;
	int i;

	cpuid = device_get_unit(dev);

	if (cpuid >= MAXCPU || cpuid > mp_maxid)
		return (EINVAL);
	KASSERT(cpu_list[cpuid] == NULL, ("Already have cpu %u", cpuid));

	reg = cpu_get_cpuid(dev, &reg_size);
	if (reg == NULL)
		return (EINVAL);

	if (bootverbose) {
		device_printf(dev, "register <");
		for (i = 0; i < reg_size; i++)
			printf("%s%x", (i == 0) ? "" : " ", reg[i]);
		printf(">\n");
	}

	/* Set the device to start it later */
	cpu_list[cpuid] = dev;

	return (0);
}

static void
release_aps(void *dummy __unused)
{
	u_long mask;
	int cpu, i;

	if (mp_ncpus == 1)
		return;

	/* Setup the IPI handler */
	riscv_setup_ipihandler(ipi_handler);

	atomic_store_rel_int(&aps_ready, 1);

	/* Wake up the other CPUs */
	mask = 0;

	for (i = 1; i < mp_ncpus; i++)
		mask |= (1 << i);

	sbi_send_ipi(&mask);

	printf("Release APs\n");

	for (i = 0; i < 2000; i++) {
		if (smp_started) {
			for (cpu = 0; cpu <= mp_maxid; cpu++) {
				if (CPU_ABSENT(cpu))
					continue;
			}
			return;
		}
		DELAY(1000);
	}

	printf("APs not started\n");
}
SYSINIT(start_aps, SI_SUB_SMP, SI_ORDER_FIRST, release_aps, NULL);

void
init_secondary(uint64_t cpu)
{
	struct pcpu *pcpup;

	/* Setup the pcpu pointer */
	pcpup = &__pcpu[cpu];
	__asm __volatile("mv gp, %0" :: "r"(pcpup));

	/* Workaround: make sure wfi doesn't halt the hart */
	csr_set(sie, SIE_SSIE);
	csr_set(sip, SIE_SSIE);

	/* Spin until the BSP releases the APs */
	while (!aps_ready)
		__asm __volatile("wfi");

	/* Initialize curthread */
	KASSERT(PCPU_GET(idlethread) != NULL, ("no idle thread"));
	pcpup->pc_curthread = pcpup->pc_idlethread;
	pcpup->pc_curpcb = pcpup->pc_idlethread->td_pcb;

	/*
	 * Identify current CPU. This is necessary to setup
	 * affinity registers and to provide support for
	 * runtime chip identification.
	 */
	identify_cpu();

	/* Enable software interrupts */
	riscv_unmask_ipi();

	/* Start per-CPU event timers. */
	cpu_initclocks_ap();

	/* Enable external (PLIC) interrupts */
	csr_set(sie, SIE_SEIE);

	/* Activate process 0's pmap. */
	pmap_activate_boot(vmspace_pmap(proc0.p_vmspace));

	mtx_lock_spin(&ap_boot_mtx);

	atomic_add_rel_32(&smp_cpus, 1);

	if (smp_cpus == mp_ncpus) {
		/* enable IPI's, tlb shootdown, freezes etc */
		atomic_store_rel_int(&smp_started, 1);
	}

	mtx_unlock_spin(&ap_boot_mtx);

	/* Enter the scheduler */
	sched_throw(NULL);

	panic("scheduler returned us to init_secondary");
	/* NOTREACHED */
}

static int
ipi_handler(void *arg)
{
	u_int ipi_bitmap;
	u_int cpu, ipi;
	int bit;

	sbi_clear_ipi();

	cpu = PCPU_GET(cpuid);

	mb();

	ipi_bitmap = atomic_readandclear_int(PCPU_PTR(pending_ipis));
	if (ipi_bitmap == 0)
		return (FILTER_HANDLED);

	while ((bit = ffs(ipi_bitmap))) {
		bit = (bit - 1);
		ipi = (1 << bit);
		ipi_bitmap &= ~ipi;

		mb();

		switch (ipi) {
		case IPI_AST:
			CTR0(KTR_SMP, "IPI_AST");
			break;
		case IPI_PREEMPT:
			CTR1(KTR_SMP, "%s: IPI_PREEMPT", __func__);
			sched_preempt(curthread);
			break;
		case IPI_RENDEZVOUS:
			CTR0(KTR_SMP, "IPI_RENDEZVOUS");
			smp_rendezvous_action();
			break;
		case IPI_STOP:
		case IPI_STOP_HARD:
			CTR0(KTR_SMP, (ipi == IPI_STOP) ? "IPI_STOP" : "IPI_STOP_HARD");
			savectx(&stoppcbs[cpu]);

			/* Indicate we are stopped */
			CPU_SET_ATOMIC(cpu, &stopped_cpus);

			/* Wait for restart */
			while (!CPU_ISSET(cpu, &started_cpus))
				cpu_spinwait();

			CPU_CLR_ATOMIC(cpu, &started_cpus);
			CPU_CLR_ATOMIC(cpu, &stopped_cpus);
			CTR0(KTR_SMP, "IPI_STOP (restart)");

			/*
			 * The kernel debugger might have set a breakpoint,
			 * so flush the instruction cache.
			 */
			fence_i();
			break;
		case IPI_HARDCLOCK:
			CTR1(KTR_SMP, "%s: IPI_HARDCLOCK", __func__);
			hardclockintr();
			break;
		default:
			panic("Unknown IPI %#0x on cpu %d", ipi, curcpu);
		}
	}

	return (FILTER_HANDLED);
}

struct cpu_group *
cpu_topo(void)
{

	return (smp_topo_none());
}

/* Determine if we running MP machine */
int
cpu_mp_probe(void)
{

	return (mp_ncpus > 1);
}

#ifdef FDT
static boolean_t
cpu_init_fdt(u_int id, phandle_t node, u_int addr_size, pcell_t *reg)
{
	uint64_t target_cpu;
	struct pcpu *pcpup;

	/* Check we are able to start this cpu */
	if (id > mp_maxid)
		return (0);

	KASSERT(id < MAXCPU, ("Too many CPUs"));

	KASSERT(addr_size == 1 || addr_size == 2, ("Invalid register size"));
#ifdef INVARIANTS
	cpu_reg[id][0] = reg[0];
	if (addr_size == 2)
		cpu_reg[id][1] = reg[1];
#endif

	target_cpu = reg[0];
	if (addr_size == 2) {
		target_cpu <<= 32;
		target_cpu |= reg[1];
	}

	pcpup = &__pcpu[id];

	/* We are already running on cpu 0 */
	if (id == 0) {
		return (1);
	}

	pcpu_init(pcpup, id, sizeof(struct pcpu));

	dpcpu[id - 1] = (void *)kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);
	dpcpu_init(dpcpu[id - 1], id);

	printf("Starting CPU %u (%lx)\n", id, target_cpu);
	__riscv_boot_ap[id] = 1;

	CPU_SET(id, &all_cpus);

	return (1);
}
#endif

/* Initialize and fire up non-boot processors */
void
cpu_mp_start(void)
{

	mtx_init(&ap_boot_mtx, "ap boot", NULL, MTX_SPIN);

	CPU_SET(0, &all_cpus);

	switch(cpu_enum_method) {
#ifdef FDT
	case CPUS_FDT:
		ofw_cpu_early_foreach(cpu_init_fdt, true);
		break;
#endif
	case CPUS_UNKNOWN:
		break;
	}
}

/* Introduce rest of cores to the world */
void
cpu_mp_announce(void)
{
}

void
cpu_mp_setmaxid(void)
{
#ifdef FDT
	int cores;

	cores = ofw_cpu_early_foreach(NULL, false);
	if (cores > 0) {
		cores = MIN(cores, MAXCPU);
		if (bootverbose)
			printf("Found %d CPUs in the device tree\n", cores);
		mp_ncpus = cores;
		mp_maxid = cores - 1;
		cpu_enum_method = CPUS_FDT;
		return;
	}
#endif

	if (bootverbose)
		printf("No CPU data, limiting to 1 core\n");
	mp_ncpus = 1;
	mp_maxid = 0;
}
