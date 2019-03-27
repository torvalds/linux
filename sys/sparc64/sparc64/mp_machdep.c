/*-
 * SPDX-License-Identifier: BSD-3-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Berkeley Software Design, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Berkeley Software Design Inc's name may not be used to endorse or
 *    promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BERKELEY SOFTWARE DESIGN INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL BERKELEY SOFTWARE DESIGN INC BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * from BSDI: locore.s,v 1.36.2.15 1999/08/23 22:34:41 cp Exp
 */
/*-
 * Copyright (c) 2002 Jake Burkholder.
 * Copyright (c) 2007 - 2010 Marius Strobl <marius@FreeBSD.org>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/lock.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>

#include <dev/ofw/openfirm.h>

#include <machine/asi.h>
#include <machine/atomic.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/ofw_machdep.h>
#include <machine/pcb.h>
#include <machine/smp.h>
#include <machine/tick.h>
#include <machine/tlb.h>
#include <machine/tsb.h>
#include <machine/tte.h>
#include <machine/ver.h>

#define	SUNW_STARTCPU		"SUNW,start-cpu"
#define	SUNW_STOPSELF		"SUNW,stop-self"

static ih_func_t cpu_ipi_ast;
static ih_func_t cpu_ipi_hardclock;
static ih_func_t cpu_ipi_preempt;
static ih_func_t cpu_ipi_stop;

/*
 * Argument area used to pass data to non-boot processors as they start up.
 * This must be statically initialized with a known invalid CPU module ID,
 * since the other processors will use it before the boot CPU enters the
 * kernel.
 */
struct	cpu_start_args cpu_start_args = { 0, -1, -1, 0, 0, 0 };
struct	ipi_cache_args ipi_cache_args;
struct	ipi_rd_args ipi_rd_args;
struct	ipi_tlb_args ipi_tlb_args;
struct	pcb stoppcbs[MAXCPU];

struct	mtx ipi_mtx;

cpu_ipi_selected_t *cpu_ipi_selected;
cpu_ipi_single_t *cpu_ipi_single;

static u_int cpuid_to_mid[MAXCPU];
static u_int cpuids = 1;
static volatile cpuset_t shutdown_cpus;
static char ipi_pbuf[CPUSETBUFSIZ];
static vm_offset_t mp_tramp;

static void ap_count(phandle_t node, u_int mid, u_int cpu_impl);
static void ap_start(phandle_t node, u_int mid, u_int cpu_impl);
static void cpu_mp_unleash(void *v);
static void foreach_ap(phandle_t node, void (*func)(phandle_t node,
    u_int mid, u_int cpu_impl));
static void sun4u_startcpu(phandle_t cpu, void *func, u_long arg);

static cpu_ipi_selected_t cheetah_ipi_selected;
static cpu_ipi_single_t cheetah_ipi_single;
static cpu_ipi_selected_t jalapeno_ipi_selected;
static cpu_ipi_single_t jalapeno_ipi_single;
static cpu_ipi_selected_t spitfire_ipi_selected;
static cpu_ipi_single_t spitfire_ipi_single;

SYSINIT(cpu_mp_unleash, SI_SUB_SMP, SI_ORDER_FIRST, cpu_mp_unleash, NULL);

void
mp_init(void)
{
	struct tte *tp;
	int i;

	mp_tramp = (vm_offset_t)OF_claim(NULL, PAGE_SIZE, PAGE_SIZE);
	if (mp_tramp == (vm_offset_t)-1)
		panic("%s", __func__);
	bcopy(mp_tramp_code, (void *)mp_tramp, mp_tramp_code_len);
	*(vm_offset_t *)(mp_tramp + mp_tramp_tlb_slots) = kernel_tlb_slots;
	*(vm_offset_t *)(mp_tramp + mp_tramp_func) = (vm_offset_t)mp_startup;
	tp = (struct tte *)(mp_tramp + mp_tramp_code_len);
	for (i = 0; i < kernel_tlb_slots; i++) {
		tp[i].tte_vpn = TV_VPN(kernel_tlbs[i].te_va, TS_4M);
		tp[i].tte_data = TD_V | TD_4M | TD_PA(kernel_tlbs[i].te_pa) |
		    TD_L | TD_CP | TD_CV | TD_P | TD_W;
	}
	for (i = 0; i < PAGE_SIZE; i += sizeof(vm_offset_t))
		flush(mp_tramp + i);
}

static void
foreach_ap(phandle_t node, void (*func)(phandle_t node, u_int mid,
    u_int cpu_impl))
{
	static char type[sizeof("cpu")];
	phandle_t child;
	uint32_t cpu_impl, portid;

	/* There's no need to traverse the whole OFW tree twice. */
	if (mp_maxid > 0 && cpuids > mp_maxid)
		return;

	for (; node != 0; node = OF_peer(node)) {
		child = OF_child(node);
		if (child > 0)
			foreach_ap(child, func);
		else {
			if (OF_getprop(node, "device_type", type,
			    sizeof(type)) <= 0)
				continue;
			if (strcmp(type, "cpu") != 0)
				continue;
			if (OF_getprop(node, "implementation#", &cpu_impl,
			    sizeof(cpu_impl)) <= 0)
				panic("%s: couldn't determine CPU "
				    "implementation", __func__);
			if (OF_getprop(node, cpu_portid_prop(cpu_impl),
			    &portid, sizeof(portid)) <= 0)
				panic("%s: couldn't determine CPU port ID",
				    __func__);
			if (portid == PCPU_GET(mid))
				continue;
			(*func)(node, portid, cpu_impl);
		}
	}
}

/*
 * Probe for other CPUs.
 */
void
cpu_mp_setmaxid(void)
{

	CPU_SETOF(curcpu, &all_cpus);
	mp_ncpus = 1;

	foreach_ap(OF_child(OF_peer(0)), ap_count);
	mp_ncpus = MIN(mp_ncpus, MAXCPU);
	mp_maxid = mp_ncpus - 1;
}

static void
ap_count(phandle_t node __unused, u_int mid __unused, u_int cpu_impl __unused)
{

	mp_ncpus++;
}

int
cpu_mp_probe(void)
{

	return (mp_maxid > 0);
}

struct cpu_group *
cpu_topo(void)
{

	return (smp_topo_none());
}

static void
sun4u_startcpu(phandle_t cpu, void *func, u_long arg)
{
	static struct {
		cell_t	name;
		cell_t	nargs;
		cell_t	nreturns;
		cell_t	cpu;
		cell_t	func;
		cell_t	arg;
	} args = {
		(cell_t)SUNW_STARTCPU,
		3,
	};

	args.cpu = cpu;
	args.func = (cell_t)func;
	args.arg = (cell_t)arg;
	ofw_entry(&args);
}

/*
 * Fire up any non-boot processors.
 */
void
cpu_mp_start(void)
{
	u_int cpu_impl, isjbus;

	mtx_init(&ipi_mtx, "ipi", NULL, MTX_SPIN);

	isjbus = 0;
	cpu_impl = PCPU_GET(impl);
	if (cpu_impl == CPU_IMPL_ULTRASPARCIIIi ||
	    cpu_impl == CPU_IMPL_ULTRASPARCIIIip) {
		isjbus = 1;
		cpu_ipi_selected = jalapeno_ipi_selected;
		cpu_ipi_single = jalapeno_ipi_single;
	} else if (cpu_impl == CPU_IMPL_SPARC64V ||
	    cpu_impl >= CPU_IMPL_ULTRASPARCIII) {
		cpu_ipi_selected = cheetah_ipi_selected;
		cpu_ipi_single = cheetah_ipi_single;
	} else {
		cpu_ipi_selected = spitfire_ipi_selected;
		cpu_ipi_single = spitfire_ipi_single;
	}

	intr_setup(PIL_AST, cpu_ipi_ast, -1, NULL, NULL);
	intr_setup(PIL_RENDEZVOUS, (ih_func_t *)smp_rendezvous_action,
	    -1, NULL, NULL);
	intr_setup(PIL_STOP, cpu_ipi_stop, -1, NULL, NULL);
	intr_setup(PIL_PREEMPT, cpu_ipi_preempt, -1, NULL, NULL);
	intr_setup(PIL_HARDCLOCK, cpu_ipi_hardclock, -1, NULL, NULL);

	cpuid_to_mid[curcpu] = PCPU_GET(mid);

	foreach_ap(OF_child(OF_peer(0)), ap_start);
	KASSERT(!isjbus || mp_ncpus <= IDR_JALAPENO_MAX_BN_PAIRS,
	    ("%s: can only IPI a maximum of %d JBus-CPUs",
	    __func__, IDR_JALAPENO_MAX_BN_PAIRS));
}

static void
ap_start(phandle_t node, u_int mid, u_int cpu_impl)
{
	volatile struct cpu_start_args *csa;
	struct pcpu *pc;
	register_t s;
	vm_offset_t va;
	u_int cpuid;
	uint32_t clock;

	if (cpuids > mp_maxid)
		return;

	if (OF_getprop(node, "clock-frequency", &clock, sizeof(clock)) <= 0)
		panic("%s: couldn't determine CPU frequency", __func__);
	if (clock != PCPU_GET(clock))
		tick_et_use_stick = 1;

	csa = &cpu_start_args;
	csa->csa_state = 0;
	sun4u_startcpu(node, (void *)mp_tramp, 0);
	s = intr_disable();
	while (csa->csa_state != CPU_TICKSYNC)
		;
	membar(StoreLoad);
	csa->csa_tick = rd(tick);
	if (cpu_impl == CPU_IMPL_SPARC64V ||
	    cpu_impl >= CPU_IMPL_ULTRASPARCIII) {
		while (csa->csa_state != CPU_STICKSYNC)
			;
		membar(StoreLoad);
		csa->csa_stick = rdstick();
	}
	while (csa->csa_state != CPU_INIT)
		;
	csa->csa_tick = csa->csa_stick = 0;
	intr_restore(s);

	cpuid = cpuids++;
	cpuid_to_mid[cpuid] = mid;
	cpu_identify(csa->csa_ver, clock, cpuid);

	va = kmem_malloc(PCPU_PAGES * PAGE_SIZE, M_WAITOK | M_ZERO);
	pc = (struct pcpu *)(va + (PCPU_PAGES * PAGE_SIZE)) - 1;
	pcpu_init(pc, cpuid, sizeof(*pc));
	dpcpu_init((void *)kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO), cpuid);
	pc->pc_addr = va;
	pc->pc_clock = clock;
	pc->pc_impl = cpu_impl;
	pc->pc_mid = mid;
	pc->pc_node = node;

	cache_init(pc);

	CPU_SET(cpuid, &all_cpus);
	intr_add_cpu(cpuid);
}

void
cpu_mp_announce(void)
{

}

static void
cpu_mp_unleash(void *v __unused)
{
	volatile struct cpu_start_args *csa;
	struct pcpu *pc;
	register_t s;
	vm_offset_t va;
	vm_paddr_t pa;
	u_int ctx_inc;
	u_int ctx_min;
	int i;

	ctx_min = TLB_CTX_USER_MIN;
	ctx_inc = (TLB_CTX_USER_MAX - 1) / mp_ncpus;
	csa = &cpu_start_args;
	csa->csa_count = mp_ncpus;
	STAILQ_FOREACH(pc, &cpuhead, pc_allcpu) {
		pc->pc_tlb_ctx = ctx_min;
		pc->pc_tlb_ctx_min = ctx_min;
		pc->pc_tlb_ctx_max = ctx_min + ctx_inc;
		ctx_min += ctx_inc;

		if (pc->pc_cpuid == curcpu)
			continue;
		KASSERT(pc->pc_idlethread != NULL,
		    ("%s: idlethread", __func__));
		pc->pc_curthread = pc->pc_idlethread;
		pc->pc_curpcb = pc->pc_curthread->td_pcb;
		for (i = 0; i < PCPU_PAGES; i++) {
			va = pc->pc_addr + i * PAGE_SIZE;
			pa = pmap_kextract(va);
			if (pa == 0)
				panic("%s: pmap_kextract", __func__);
			csa->csa_ttes[i].tte_vpn = TV_VPN(va, TS_8K);
			csa->csa_ttes[i].tte_data = TD_V | TD_8K | TD_PA(pa) |
			    TD_L | TD_CP | TD_CV | TD_P | TD_W;
		}
		csa->csa_state = 0;
		csa->csa_pcpu = pc->pc_addr;
		csa->csa_mid = pc->pc_mid;
		s = intr_disable();
		while (csa->csa_state != CPU_BOOTSTRAP)
			;
		intr_restore(s);
	}

	membar(StoreLoad);
	csa->csa_count = 0;
}

void
cpu_mp_bootstrap(struct pcpu *pc)
{
	volatile struct cpu_start_args *csa;

	csa = &cpu_start_args;

	/* Do CPU-specific initialization. */
	if (pc->pc_impl >= CPU_IMPL_ULTRASPARCIII)
		cheetah_init(pc->pc_impl);
	else if (pc->pc_impl == CPU_IMPL_SPARC64V)
		zeus_init(pc->pc_impl);

	/*
	 * Enable the caches.  Note that his may include applying workarounds.
	 */
	cache_enable(pc->pc_impl);

	/*
	 * Clear (S)TICK timer(s) (including NPT) and ensure they are stopped.
	 */
	tick_clear(pc->pc_impl);
	tick_stop(pc->pc_impl);

	/* Set the kernel context. */
	pmap_set_kctx();

	/* Lock the kernel TSB in the TLB if necessary. */
	if (tsb_kernel_ldd_phys == 0)
		pmap_map_tsb();

	/*
	 * Flush all non-locked TLB entries possibly left over by the
	 * firmware.
	 */
	tlb_flush_nonlocked();

	/*
	 * Enable interrupts.
	 * Note that the PIL we be lowered indirectly via sched_throw(NULL)
	 * when fake spinlock held by the idle thread eventually is released.
	 */
	wrpr(pstate, 0, PSTATE_KERNEL);

	smp_cpus++;
	KASSERT(curthread != NULL, ("%s: curthread", __func__));
	printf("SMP: AP CPU #%d Launched!\n", curcpu);

	csa->csa_count--;
	membar(StoreLoad);
	csa->csa_state = CPU_BOOTSTRAP;
	while (csa->csa_count != 0)
		;

	if (smp_cpus == mp_ncpus)
		atomic_store_rel_int(&smp_started, 1);

	/* Start per-CPU event timers. */
	cpu_initclocks_ap();

	/* Ok, now enter the scheduler. */
	sched_throw(NULL);
}

void
cpu_mp_shutdown(void)
{
	cpuset_t cpus;
	int i;

	critical_enter();
	shutdown_cpus = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &shutdown_cpus);
	cpus = shutdown_cpus;

	/* XXX: Stop all the CPUs which aren't already. */
	if (CPU_CMP(&stopped_cpus, &cpus)) {

		/* cpus is just a flat "on" mask without curcpu. */
		CPU_NAND(&cpus, &stopped_cpus);
		stop_cpus(cpus);
	}
	i = 0;
	while (!CPU_EMPTY(&shutdown_cpus)) {
		if (i++ > 100000) {
			printf("timeout shutting down CPUs.\n");
			break;
		}
	}
	critical_exit();
}

static void
cpu_ipi_ast(struct trapframe *tf __unused)
{

}

static void
cpu_ipi_stop(struct trapframe *tf __unused)
{
	u_int cpuid;

	CTR2(KTR_SMP, "%s: stopped %d", __func__, curcpu);
	sched_pin();
	savectx(&stoppcbs[curcpu]);
	cpuid = PCPU_GET(cpuid);
	CPU_SET_ATOMIC(cpuid, &stopped_cpus);
	while (!CPU_ISSET(cpuid, &started_cpus)) {
		if (CPU_ISSET(cpuid, &shutdown_cpus)) {
			CPU_CLR_ATOMIC(cpuid, &shutdown_cpus);
			(void)intr_disable();
			for (;;)
				;
		}
	}
	CPU_CLR_ATOMIC(cpuid, &started_cpus);
	CPU_CLR_ATOMIC(cpuid, &stopped_cpus);
	sched_unpin();
	CTR2(KTR_SMP, "%s: restarted %d", __func__, curcpu);
}

static void
cpu_ipi_preempt(struct trapframe *tf __unused)
{

	sched_preempt(curthread);
}

static void
cpu_ipi_hardclock(struct trapframe *tf)
{
	struct trapframe *oldframe;
	struct thread *td;

	critical_enter();
	td = curthread;
	td->td_intr_nesting_level++;
	oldframe = td->td_intr_frame;
	td->td_intr_frame = tf;
	hardclockintr();
	td->td_intr_frame = oldframe;
	td->td_intr_nesting_level--;
	critical_exit();
}

static void
spitfire_ipi_selected(cpuset_t cpus, u_long d0, u_long d1, u_long d2)
{
	u_int cpu;

	while ((cpu = CPU_FFS(&cpus)) != 0) {
		cpu--;
		CPU_CLR(cpu, &cpus);
		spitfire_ipi_single(cpu, d0, d1, d2);
	}
}

static void
spitfire_ipi_single(u_int cpu, u_long d0, u_long d1, u_long d2)
{
	register_t s;
	u_long ids;
	u_int mid;
	int i;

	mtx_assert(&ipi_mtx, MA_OWNED);
	KASSERT(cpu != curcpu, ("%s: CPU can't IPI itself", __func__));
	KASSERT((ldxa(0, ASI_INTR_DISPATCH_STATUS) & IDR_BUSY) == 0,
	    ("%s: outstanding dispatch", __func__));

	mid = cpuid_to_mid[cpu];
	for (i = 0; i < IPI_RETRIES; i++) {
		s = intr_disable();
		stxa(AA_SDB_INTR_D0, ASI_SDB_INTR_W, d0);
		stxa(AA_SDB_INTR_D1, ASI_SDB_INTR_W, d1);
		stxa(AA_SDB_INTR_D2, ASI_SDB_INTR_W, d2);
		membar(Sync);
		stxa(AA_INTR_SEND | (mid << IDC_ITID_SHIFT),
		    ASI_SDB_INTR_W, 0);
		/*
		 * Workaround for SpitFire erratum #54; do a dummy read
		 * from a SDB internal register before the MEMBAR #Sync
		 * for the write to ASI_SDB_INTR_W (requiring another
		 * MEMBAR #Sync in order to make sure the write has
		 * occurred before the load).
		 */
		membar(Sync);
		(void)ldxa(AA_SDB_CNTL_HIGH, ASI_SDB_CONTROL_R);
		membar(Sync);
		while (((ids = ldxa(0, ASI_INTR_DISPATCH_STATUS)) &
		    IDR_BUSY) != 0)
			;
		intr_restore(s);
		if ((ids & (IDR_BUSY | IDR_NACK)) == 0)
			return;
	}
	if (kdb_active != 0 || panicstr != NULL)
		printf("%s: couldn't send IPI to module 0x%u\n",
		    __func__, mid);
	else
		panic("%s: couldn't send IPI to module 0x%u",
		    __func__, mid);
}

static void
cheetah_ipi_single(u_int cpu, u_long d0, u_long d1, u_long d2)
{
	register_t s;
	u_long ids;
	u_int mid;
	int i;

	mtx_assert(&ipi_mtx, MA_OWNED);
	KASSERT(cpu != curcpu, ("%s: CPU can't IPI itself", __func__));
	KASSERT((ldxa(0, ASI_INTR_DISPATCH_STATUS) &
	    IDR_CHEETAH_ALL_BUSY) == 0,
	    ("%s: outstanding dispatch", __func__));

	mid = cpuid_to_mid[cpu];
	for (i = 0; i < IPI_RETRIES; i++) {
		s = intr_disable();
		stxa(AA_SDB_INTR_D0, ASI_SDB_INTR_W, d0);
		stxa(AA_SDB_INTR_D1, ASI_SDB_INTR_W, d1);
		stxa(AA_SDB_INTR_D2, ASI_SDB_INTR_W, d2);
		membar(Sync);
		stxa(AA_INTR_SEND | (mid << IDC_ITID_SHIFT),
		    ASI_SDB_INTR_W, 0);
		membar(Sync);
		while (((ids = ldxa(0, ASI_INTR_DISPATCH_STATUS)) &
		    IDR_BUSY) != 0)
			;
		intr_restore(s);
		if ((ids & (IDR_BUSY | IDR_NACK)) == 0)
			return;
	}
	if (kdb_active != 0 || panicstr != NULL)
		printf("%s: couldn't send IPI to module 0x%u\n",
		    __func__, mid);
	else
		panic("%s: couldn't send IPI to module 0x%u",
		    __func__, mid);
}

static void
cheetah_ipi_selected(cpuset_t cpus, u_long d0, u_long d1, u_long d2)
{
	register_t s;
	u_long ids;
	u_int bnp;
	u_int cpu;
	int i;

	mtx_assert(&ipi_mtx, MA_OWNED);
	KASSERT(!CPU_EMPTY(&cpus), ("%s: no CPUs to IPI", __func__));
	KASSERT(!CPU_ISSET(curcpu, &cpus), ("%s: CPU can't IPI itself",
	    __func__));
	KASSERT((ldxa(0, ASI_INTR_DISPATCH_STATUS) &
	    IDR_CHEETAH_ALL_BUSY) == 0,
	    ("%s: outstanding dispatch", __func__));

	ids = 0;
	for (i = 0; i < IPI_RETRIES * smp_cpus; i++) {
		s = intr_disable();
		stxa(AA_SDB_INTR_D0, ASI_SDB_INTR_W, d0);
		stxa(AA_SDB_INTR_D1, ASI_SDB_INTR_W, d1);
		stxa(AA_SDB_INTR_D2, ASI_SDB_INTR_W, d2);
		membar(Sync);
		bnp = 0;
		for (cpu = 0; cpu < smp_cpus; cpu++) {
			if (CPU_ISSET(cpu, &cpus)) {
				stxa(AA_INTR_SEND | (cpuid_to_mid[cpu] <<
				    IDC_ITID_SHIFT) | bnp << IDC_BN_SHIFT,
				    ASI_SDB_INTR_W, 0);
				membar(Sync);
				bnp++;
				if (bnp == IDR_CHEETAH_MAX_BN_PAIRS)
					break;
			}
		}
		while (((ids = ldxa(0, ASI_INTR_DISPATCH_STATUS)) &
		    IDR_CHEETAH_ALL_BUSY) != 0)
			;
		intr_restore(s);
		bnp = 0;
		for (cpu = 0; cpu < smp_cpus; cpu++) {
			if (CPU_ISSET(cpu, &cpus)) {
				if ((ids & (IDR_NACK << (2 * bnp))) == 0)
					CPU_CLR(cpu, &cpus);
				bnp++;
			}
		}
		if (CPU_EMPTY(&cpus))
			return;
	}
	if (kdb_active != 0 || panicstr != NULL)
		printf("%s: couldn't send IPI (cpus=%s ids=0x%lu)\n",
		    __func__, cpusetobj_strprint(ipi_pbuf, &cpus), ids);
	else
		panic("%s: couldn't send IPI (cpus=%s ids=0x%lu)",
		    __func__, cpusetobj_strprint(ipi_pbuf, &cpus), ids);
}

static void
jalapeno_ipi_single(u_int cpu, u_long d0, u_long d1, u_long d2)
{
	register_t s;
	u_long ids;
	u_int busy, busynack, mid;
	int i;

	mtx_assert(&ipi_mtx, MA_OWNED);
	KASSERT(cpu != curcpu, ("%s: CPU can't IPI itself", __func__));
	KASSERT((ldxa(0, ASI_INTR_DISPATCH_STATUS) &
	    IDR_CHEETAH_ALL_BUSY) == 0,
	    ("%s: outstanding dispatch", __func__));

	mid = cpuid_to_mid[cpu];
	busy = IDR_BUSY << (2 * mid);
	busynack = (IDR_BUSY | IDR_NACK) << (2 * mid);
	for (i = 0; i < IPI_RETRIES; i++) {
		s = intr_disable();
		stxa(AA_SDB_INTR_D0, ASI_SDB_INTR_W, d0);
		stxa(AA_SDB_INTR_D1, ASI_SDB_INTR_W, d1);
		stxa(AA_SDB_INTR_D2, ASI_SDB_INTR_W, d2);
		membar(Sync);
		stxa(AA_INTR_SEND | (mid << IDC_ITID_SHIFT),
		    ASI_SDB_INTR_W, 0);
		membar(Sync);
		while (((ids = ldxa(0, ASI_INTR_DISPATCH_STATUS)) &
		    busy) != 0)
			;
		intr_restore(s);
		if ((ids & busynack) == 0)
			return;
	}
	if (kdb_active != 0 || panicstr != NULL)
		printf("%s: couldn't send IPI to module 0x%u\n",
		    __func__, mid);
	else
		panic("%s: couldn't send IPI to module 0x%u",
		    __func__, mid);
}

static void
jalapeno_ipi_selected(cpuset_t cpus, u_long d0, u_long d1, u_long d2)
{
	register_t s;
	u_long ids;
	u_int cpu;
	int i;

	mtx_assert(&ipi_mtx, MA_OWNED);
	KASSERT(!CPU_EMPTY(&cpus), ("%s: no CPUs to IPI", __func__));
	KASSERT(!CPU_ISSET(curcpu, &cpus), ("%s: CPU can't IPI itself",
	    __func__));
	KASSERT((ldxa(0, ASI_INTR_DISPATCH_STATUS) &
	    IDR_CHEETAH_ALL_BUSY) == 0,
	    ("%s: outstanding dispatch", __func__));

	ids = 0;
	for (i = 0; i < IPI_RETRIES * smp_cpus; i++) {
		s = intr_disable();
		stxa(AA_SDB_INTR_D0, ASI_SDB_INTR_W, d0);
		stxa(AA_SDB_INTR_D1, ASI_SDB_INTR_W, d1);
		stxa(AA_SDB_INTR_D2, ASI_SDB_INTR_W, d2);
		membar(Sync);
		for (cpu = 0; cpu < smp_cpus; cpu++) {
			if (CPU_ISSET(cpu, &cpus)) {
				stxa(AA_INTR_SEND | (cpuid_to_mid[cpu] <<
				    IDC_ITID_SHIFT), ASI_SDB_INTR_W, 0);
				membar(Sync);
			}
		}
		while (((ids = ldxa(0, ASI_INTR_DISPATCH_STATUS)) &
		    IDR_CHEETAH_ALL_BUSY) != 0)
			;
		intr_restore(s);
		if ((ids &
		    (IDR_CHEETAH_ALL_BUSY | IDR_CHEETAH_ALL_NACK)) == 0)
			return;
		for (cpu = 0; cpu < smp_cpus; cpu++)
			if (CPU_ISSET(cpu, &cpus))
				if ((ids & (IDR_NACK <<
				    (2 * cpuid_to_mid[cpu]))) == 0)
					CPU_CLR(cpu, &cpus);
	}
	if (kdb_active != 0 || panicstr != NULL)
		printf("%s: couldn't send IPI (cpus=%s ids=0x%lu)\n",
		    __func__, cpusetobj_strprint(ipi_pbuf, &cpus), ids);
	else
		panic("%s: couldn't send IPI (cpus=%s ids=0x%lu)",
		    __func__, cpusetobj_strprint(ipi_pbuf, &cpus), ids);
}
