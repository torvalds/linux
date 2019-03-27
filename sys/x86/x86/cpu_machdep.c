/*-
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1992 Terrence R. Lambert.
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)machdep.c	7.4 (Berkeley) 6/3/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_acpi.h"
#include "opt_atpic.h"
#include "opt_cpu.h"
#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_isa.h"
#include "opt_kdb.h"
#include "opt_kstack_pages.h"
#include "opt_maxmem.h"
#include "opt_mp_watchdog.h"
#include "opt_platform.h"
#ifdef __i386__
#include "opt_apic.h"
#endif

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cputypes.h>
#include <machine/specialreg.h>
#include <machine/md_var.h>
#include <machine/mp_watchdog.h>
#include <machine/tss.h>
#ifdef SMP
#include <machine/smp.h>
#endif
#ifdef CPU_ELAN
#include <machine/elan_mmcr.h>
#endif
#include <x86/acpica_machdep.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>

#include <isa/isareg.h>

#include <contrib/dev/acpica/include/acpi.h>

#define	STATE_RUNNING	0x0
#define	STATE_MWAIT	0x1
#define	STATE_SLEEPING	0x2

#ifdef SMP
static u_int	cpu_reset_proxyid;
static volatile u_int	cpu_reset_proxy_active;
#endif


/*
 * Machine dependent boot() routine
 *
 * I haven't seen anything to put here yet
 * Possibly some stuff might be grafted back here from boot()
 */
void
cpu_boot(int howto)
{
}

/*
 * Flush the D-cache for non-DMA I/O so that the I-cache can
 * be made coherent later.
 */
void
cpu_flush_dcache(void *ptr, size_t len)
{
	/* Not applicable */
}

void
acpi_cpu_c1(void)
{

	__asm __volatile("sti; hlt");
}

/*
 * Use mwait to pause execution while waiting for an interrupt or
 * another thread to signal that there is more work.
 *
 * NOTE: Interrupts will cause a wakeup; however, this function does
 * not enable interrupt handling. The caller is responsible to enable
 * interrupts.
 */
void
acpi_cpu_idle_mwait(uint32_t mwait_hint)
{
	int *state;
	uint64_t v;

	/*
	 * A comment in Linux patch claims that 'CPUs run faster with
	 * speculation protection disabled. All CPU threads in a core
	 * must disable speculation protection for it to be
	 * disabled. Disable it while we are idle so the other
	 * hyperthread can run fast.'
	 *
	 * XXXKIB.  Software coordination mode should be supported,
	 * but all Intel CPUs provide hardware coordination.
	 */

	state = (int *)PCPU_PTR(monitorbuf);
	KASSERT(atomic_load_int(state) == STATE_SLEEPING,
	    ("cpu_mwait_cx: wrong monitorbuf state"));
	atomic_store_int(state, STATE_MWAIT);
	if (PCPU_GET(ibpb_set) || hw_ssb_active) {
		v = rdmsr(MSR_IA32_SPEC_CTRL);
		wrmsr(MSR_IA32_SPEC_CTRL, v & ~(IA32_SPEC_CTRL_IBRS |
		    IA32_SPEC_CTRL_STIBP | IA32_SPEC_CTRL_SSBD));
	} else {
		v = 0;
	}
	cpu_monitor(state, 0, 0);
	if (atomic_load_int(state) == STATE_MWAIT)
		cpu_mwait(MWAIT_INTRBREAK, mwait_hint);

	/*
	 * SSB cannot be disabled while we sleep, or rather, if it was
	 * disabled, the sysctl thread will bind to our cpu to tweak
	 * MSR.
	 */
	if (v != 0)
		wrmsr(MSR_IA32_SPEC_CTRL, v);

	/*
	 * We should exit on any event that interrupts mwait, because
	 * that event might be a wanted interrupt.
	 */
	atomic_store_int(state, STATE_RUNNING);
}

/* Get current clock frequency for the given cpu id. */
int
cpu_est_clockrate(int cpu_id, uint64_t *rate)
{
	uint64_t tsc1, tsc2;
	uint64_t acnt, mcnt, perf;
	register_t reg;

	if (pcpu_find(cpu_id) == NULL || rate == NULL)
		return (EINVAL);
#ifdef __i386__
	if ((cpu_feature & CPUID_TSC) == 0)
		return (EOPNOTSUPP);
#endif

	/*
	 * If TSC is P-state invariant and APERF/MPERF MSRs do not exist,
	 * DELAY(9) based logic fails.
	 */
	if (tsc_is_invariant && !tsc_perf_stat)
		return (EOPNOTSUPP);

#ifdef SMP
	if (smp_cpus > 1) {
		/* Schedule ourselves on the indicated cpu. */
		thread_lock(curthread);
		sched_bind(curthread, cpu_id);
		thread_unlock(curthread);
	}
#endif

	/* Calibrate by measuring a short delay. */
	reg = intr_disable();
	if (tsc_is_invariant) {
		wrmsr(MSR_MPERF, 0);
		wrmsr(MSR_APERF, 0);
		tsc1 = rdtsc();
		DELAY(1000);
		mcnt = rdmsr(MSR_MPERF);
		acnt = rdmsr(MSR_APERF);
		tsc2 = rdtsc();
		intr_restore(reg);
		perf = 1000 * acnt / mcnt;
		*rate = (tsc2 - tsc1) * perf;
	} else {
		tsc1 = rdtsc();
		DELAY(1000);
		tsc2 = rdtsc();
		intr_restore(reg);
		*rate = (tsc2 - tsc1) * 1000;
	}

#ifdef SMP
	if (smp_cpus > 1) {
		thread_lock(curthread);
		sched_unbind(curthread);
		thread_unlock(curthread);
	}
#endif

	return (0);
}

/*
 * Shutdown the CPU as much as possible
 */
void
cpu_halt(void)
{
	for (;;)
		halt();
}

static void
cpu_reset_real(void)
{
	struct region_descriptor null_idt;
	int b;

	disable_intr();
#ifdef CPU_ELAN
	if (elan_mmcr != NULL)
		elan_mmcr->RESCFG = 1;
#endif
#ifdef __i386__
	if (cpu == CPU_GEODE1100) {
		/* Attempt Geode's own reset */
		outl(0xcf8, 0x80009044ul);
		outl(0xcfc, 0xf);
	}
#endif
#if !defined(BROKEN_KEYBOARD_RESET)
	/*
	 * Attempt to do a CPU reset via the keyboard controller,
	 * do not turn off GateA20, as any machine that fails
	 * to do the reset here would then end up in no man's land.
	 */
	outb(IO_KBD + 4, 0xFE);
	DELAY(500000);	/* wait 0.5 sec to see if that did it */
#endif

	/*
	 * Attempt to force a reset via the Reset Control register at
	 * I/O port 0xcf9.  Bit 2 forces a system reset when it
	 * transitions from 0 to 1.  Bit 1 selects the type of reset
	 * to attempt: 0 selects a "soft" reset, and 1 selects a
	 * "hard" reset.  We try a "hard" reset.  The first write sets
	 * bit 1 to select a "hard" reset and clears bit 2.  The
	 * second write forces a 0 -> 1 transition in bit 2 to trigger
	 * a reset.
	 */
	outb(0xcf9, 0x2);
	outb(0xcf9, 0x6);
	DELAY(500000);  /* wait 0.5 sec to see if that did it */

	/*
	 * Attempt to force a reset via the Fast A20 and Init register
	 * at I/O port 0x92.  Bit 1 serves as an alternate A20 gate.
	 * Bit 0 asserts INIT# when set to 1.  We are careful to only
	 * preserve bit 1 while setting bit 0.  We also must clear bit
	 * 0 before setting it if it isn't already clear.
	 */
	b = inb(0x92);
	if (b != 0xff) {
		if ((b & 0x1) != 0)
			outb(0x92, b & 0xfe);
		outb(0x92, b | 0x1);
		DELAY(500000);  /* wait 0.5 sec to see if that did it */
	}

	printf("No known reset method worked, attempting CPU shutdown\n");
	DELAY(1000000); /* wait 1 sec for printf to complete */

	/* Wipe the IDT. */
	null_idt.rd_limit = 0;
	null_idt.rd_base = 0;
	lidt(&null_idt);

	/* "good night, sweet prince .... <THUNK!>" */
	breakpoint();

	/* NOTREACHED */
	while(1);
}

#ifdef SMP
static void
cpu_reset_proxy(void)
{

	cpu_reset_proxy_active = 1;
	while (cpu_reset_proxy_active == 1)
		ia32_pause(); /* Wait for other cpu to see that we've started */

	printf("cpu_reset_proxy: Stopped CPU %d\n", cpu_reset_proxyid);
	DELAY(1000000);
	cpu_reset_real();
}
#endif

void
cpu_reset(void)
{
#ifdef SMP
	cpuset_t map;
	u_int cnt;

	if (smp_started) {
		map = all_cpus;
		CPU_CLR(PCPU_GET(cpuid), &map);
		CPU_NAND(&map, &stopped_cpus);
		if (!CPU_EMPTY(&map)) {
			printf("cpu_reset: Stopping other CPUs\n");
			stop_cpus(map);
		}

		if (PCPU_GET(cpuid) != 0) {
			cpu_reset_proxyid = PCPU_GET(cpuid);
			cpustop_restartfunc = cpu_reset_proxy;
			cpu_reset_proxy_active = 0;
			printf("cpu_reset: Restarting BSP\n");

			/* Restart CPU #0. */
			CPU_SETOF(0, &started_cpus);
			wmb();

			cnt = 0;
			while (cpu_reset_proxy_active == 0 && cnt < 10000000) {
				ia32_pause();
				cnt++;	/* Wait for BSP to announce restart */
			}
			if (cpu_reset_proxy_active == 0) {
				printf("cpu_reset: Failed to restart BSP\n");
			} else {
				cpu_reset_proxy_active = 2;
				while (1)
					ia32_pause();
				/* NOTREACHED */
			}
		}

		DELAY(1000000);
	}
#endif
	cpu_reset_real();
	/* NOTREACHED */
}

bool
cpu_mwait_usable(void)
{

	return ((cpu_feature2 & CPUID2_MON) != 0 && ((cpu_mon_mwait_flags &
	    (CPUID5_MON_MWAIT_EXT | CPUID5_MWAIT_INTRBREAK)) ==
	    (CPUID5_MON_MWAIT_EXT | CPUID5_MWAIT_INTRBREAK)));
}

void (*cpu_idle_hook)(sbintime_t) = NULL;	/* ACPI idle hook. */
static int	cpu_ident_amdc1e = 0;	/* AMD C1E supported. */
static int	idle_mwait = 1;		/* Use MONITOR/MWAIT for short idle. */
SYSCTL_INT(_machdep, OID_AUTO, idle_mwait, CTLFLAG_RWTUN, &idle_mwait,
    0, "Use MONITOR/MWAIT for short idle");

static void
cpu_idle_acpi(sbintime_t sbt)
{
	int *state;

	state = (int *)PCPU_PTR(monitorbuf);
	atomic_store_int(state, STATE_SLEEPING);

	/* See comments in cpu_idle_hlt(). */
	disable_intr();
	if (sched_runnable())
		enable_intr();
	else if (cpu_idle_hook)
		cpu_idle_hook(sbt);
	else
		acpi_cpu_c1();
	atomic_store_int(state, STATE_RUNNING);
}

static void
cpu_idle_hlt(sbintime_t sbt)
{
	int *state;

	state = (int *)PCPU_PTR(monitorbuf);
	atomic_store_int(state, STATE_SLEEPING);

	/*
	 * Since we may be in a critical section from cpu_idle(), if
	 * an interrupt fires during that critical section we may have
	 * a pending preemption.  If the CPU halts, then that thread
	 * may not execute until a later interrupt awakens the CPU.
	 * To handle this race, check for a runnable thread after
	 * disabling interrupts and immediately return if one is
	 * found.  Also, we must absolutely guarentee that hlt is
	 * the next instruction after sti.  This ensures that any
	 * interrupt that fires after the call to disable_intr() will
	 * immediately awaken the CPU from hlt.  Finally, please note
	 * that on x86 this works fine because of interrupts enabled only
	 * after the instruction following sti takes place, while IF is set
	 * to 1 immediately, allowing hlt instruction to acknowledge the
	 * interrupt.
	 */
	disable_intr();
	if (sched_runnable())
		enable_intr();
	else
		acpi_cpu_c1();
	atomic_store_int(state, STATE_RUNNING);
}

static void
cpu_idle_mwait(sbintime_t sbt)
{
	int *state;

	state = (int *)PCPU_PTR(monitorbuf);
	atomic_store_int(state, STATE_MWAIT);

	/* See comments in cpu_idle_hlt(). */
	disable_intr();
	if (sched_runnable()) {
		atomic_store_int(state, STATE_RUNNING);
		enable_intr();
		return;
	}

	cpu_monitor(state, 0, 0);
	if (atomic_load_int(state) == STATE_MWAIT)
		__asm __volatile("sti; mwait" : : "a" (MWAIT_C1), "c" (0));
	else
		enable_intr();
	atomic_store_int(state, STATE_RUNNING);
}

static void
cpu_idle_spin(sbintime_t sbt)
{
	int *state;
	int i;

	state = (int *)PCPU_PTR(monitorbuf);
	atomic_store_int(state, STATE_RUNNING);

	/*
	 * The sched_runnable() call is racy but as long as there is
	 * a loop missing it one time will have just a little impact if any 
	 * (and it is much better than missing the check at all).
	 */
	for (i = 0; i < 1000; i++) {
		if (sched_runnable())
			return;
		cpu_spinwait();
	}
}

/*
 * C1E renders the local APIC timer dead, so we disable it by
 * reading the Interrupt Pending Message register and clearing
 * both C1eOnCmpHalt (bit 28) and SmiOnCmpHalt (bit 27).
 * 
 * Reference:
 *   "BIOS and Kernel Developer's Guide for AMD NPT Family 0Fh Processors"
 *   #32559 revision 3.00+
 */
#define	MSR_AMDK8_IPM		0xc0010055
#define	AMDK8_SMIONCMPHALT	(1ULL << 27)
#define	AMDK8_C1EONCMPHALT	(1ULL << 28)
#define	AMDK8_CMPHALT		(AMDK8_SMIONCMPHALT | AMDK8_C1EONCMPHALT)

void
cpu_probe_amdc1e(void)
{

	/*
	 * Detect the presence of C1E capability mostly on latest
	 * dual-cores (or future) k8 family.
	 */
	if (cpu_vendor_id == CPU_VENDOR_AMD &&
	    (cpu_id & 0x00000f00) == 0x00000f00 &&
	    (cpu_id & 0x0fff0000) >=  0x00040000) {
		cpu_ident_amdc1e = 1;
	}
}

void (*cpu_idle_fn)(sbintime_t) = cpu_idle_acpi;

void
cpu_idle(int busy)
{
	uint64_t msr;
	sbintime_t sbt = -1;

	CTR2(KTR_SPARE2, "cpu_idle(%d) at %d",
	    busy, curcpu);
#ifdef MP_WATCHDOG
	ap_watchdog(PCPU_GET(cpuid));
#endif

	/* If we are busy - try to use fast methods. */
	if (busy) {
		if ((cpu_feature2 & CPUID2_MON) && idle_mwait) {
			cpu_idle_mwait(busy);
			goto out;
		}
	}

	/* If we have time - switch timers into idle mode. */
	if (!busy) {
		critical_enter();
		sbt = cpu_idleclock();
	}

	/* Apply AMD APIC timer C1E workaround. */
	if (cpu_ident_amdc1e && cpu_disable_c3_sleep) {
		msr = rdmsr(MSR_AMDK8_IPM);
		if (msr & AMDK8_CMPHALT)
			wrmsr(MSR_AMDK8_IPM, msr & ~AMDK8_CMPHALT);
	}

	/* Call main idle method. */
	cpu_idle_fn(sbt);

	/* Switch timers back into active mode. */
	if (!busy) {
		cpu_activeclock();
		critical_exit();
	}
out:
	CTR2(KTR_SPARE2, "cpu_idle(%d) at %d done",
	    busy, curcpu);
}

static int cpu_idle_apl31_workaround;
SYSCTL_INT(_machdep, OID_AUTO, idle_apl31, CTLFLAG_RW,
    &cpu_idle_apl31_workaround, 0,
    "Apollo Lake APL31 MWAIT bug workaround");

int
cpu_idle_wakeup(int cpu)
{
	int *state;

	state = (int *)pcpu_find(cpu)->pc_monitorbuf;
	switch (atomic_load_int(state)) {
	case STATE_SLEEPING:
		return (0);
	case STATE_MWAIT:
		atomic_store_int(state, STATE_RUNNING);
		return (cpu_idle_apl31_workaround ? 0 : 1);
	case STATE_RUNNING:
		return (1);
	default:
		panic("bad monitor state");
		return (1);
	}
}

/*
 * Ordered by speed/power consumption.
 */
static struct {
	void	*id_fn;
	char	*id_name;
	int	id_cpuid2_flag;
} idle_tbl[] = {
	{ .id_fn = cpu_idle_spin, .id_name = "spin" },
	{ .id_fn = cpu_idle_mwait, .id_name = "mwait",
	    .id_cpuid2_flag = CPUID2_MON },
	{ .id_fn = cpu_idle_hlt, .id_name = "hlt" },
	{ .id_fn = cpu_idle_acpi, .id_name = "acpi" },
};

static int
idle_sysctl_available(SYSCTL_HANDLER_ARGS)
{
	char *avail, *p;
	int error;
	int i;

	avail = malloc(256, M_TEMP, M_WAITOK);
	p = avail;
	for (i = 0; i < nitems(idle_tbl); i++) {
		if (idle_tbl[i].id_cpuid2_flag != 0 &&
		    (cpu_feature2 & idle_tbl[i].id_cpuid2_flag) == 0)
			continue;
		if (strcmp(idle_tbl[i].id_name, "acpi") == 0 &&
		    cpu_idle_hook == NULL)
			continue;
		p += sprintf(p, "%s%s", p != avail ? ", " : "",
		    idle_tbl[i].id_name);
	}
	error = sysctl_handle_string(oidp, avail, 0, req);
	free(avail, M_TEMP);
	return (error);
}

SYSCTL_PROC(_machdep, OID_AUTO, idle_available, CTLTYPE_STRING | CTLFLAG_RD,
    0, 0, idle_sysctl_available, "A", "list of available idle functions");

static bool
cpu_idle_selector(const char *new_idle_name)
{
	int i;

	for (i = 0; i < nitems(idle_tbl); i++) {
		if (idle_tbl[i].id_cpuid2_flag != 0 &&
		    (cpu_feature2 & idle_tbl[i].id_cpuid2_flag) == 0)
			continue;
		if (strcmp(idle_tbl[i].id_name, "acpi") == 0 &&
		    cpu_idle_hook == NULL)
			continue;
		if (strcmp(idle_tbl[i].id_name, new_idle_name))
			continue;
		cpu_idle_fn = idle_tbl[i].id_fn;
		if (bootverbose)
			printf("CPU idle set to %s\n", idle_tbl[i].id_name);
		return (true);
	}
	return (false);
}

static int
cpu_idle_sysctl(SYSCTL_HANDLER_ARGS)
{
	char buf[16], *p;
	int error, i;

	p = "unknown";
	for (i = 0; i < nitems(idle_tbl); i++) {
		if (idle_tbl[i].id_fn == cpu_idle_fn) {
			p = idle_tbl[i].id_name;
			break;
		}
	}
	strncpy(buf, p, sizeof(buf));
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	return (cpu_idle_selector(buf) ? 0 : EINVAL);
}

SYSCTL_PROC(_machdep, OID_AUTO, idle, CTLTYPE_STRING | CTLFLAG_RW, 0, 0,
    cpu_idle_sysctl, "A", "currently selected idle function");

static void
cpu_idle_tun(void *unused __unused)
{
	char tunvar[16];

	if (TUNABLE_STR_FETCH("machdep.idle", tunvar, sizeof(tunvar)))
		cpu_idle_selector(tunvar);
	else if (cpu_vendor_id == CPU_VENDOR_AMD &&
	    CPUID_TO_FAMILY(cpu_id) == 0x17 && CPUID_TO_MODEL(cpu_id) == 0x1) {
		/* Ryzen erratas 1057, 1109. */
		cpu_idle_selector("hlt");
		idle_mwait = 0;
	}

	if (cpu_vendor_id == CPU_VENDOR_INTEL && cpu_id == 0x506c9) {
		/*
		 * Apollo Lake errata APL31 (public errata APL30).
		 * Stores to the armed address range may not trigger
		 * MWAIT to resume execution.  OS needs to use
		 * interrupts to wake processors from MWAIT-induced
		 * sleep states.
		 */
		cpu_idle_apl31_workaround = 1;
	}
	TUNABLE_INT_FETCH("machdep.idle_apl31", &cpu_idle_apl31_workaround);
}
SYSINIT(cpu_idle_tun, SI_SUB_CPU, SI_ORDER_MIDDLE, cpu_idle_tun, NULL);

static int panic_on_nmi = 1;
SYSCTL_INT(_machdep, OID_AUTO, panic_on_nmi, CTLFLAG_RWTUN,
    &panic_on_nmi, 0,
    "Panic on NMI raised by hardware failure");
int nmi_is_broadcast = 1;
SYSCTL_INT(_machdep, OID_AUTO, nmi_is_broadcast, CTLFLAG_RWTUN,
    &nmi_is_broadcast, 0,
    "Chipset NMI is broadcast");
#ifdef KDB
int kdb_on_nmi = 1;
SYSCTL_INT(_machdep, OID_AUTO, kdb_on_nmi, CTLFLAG_RWTUN,
    &kdb_on_nmi, 0,
    "Go to KDB on NMI with unknown source");
#endif

void
nmi_call_kdb(u_int cpu, u_int type, struct trapframe *frame)
{
	bool claimed = false;

#ifdef DEV_ISA
	/* machine/parity/power fail/"kitchen sink" faults */
	if (isa_nmi(frame->tf_err)) {
		claimed = true;
		if (panic_on_nmi)
			panic("NMI indicates hardware failure");
	}
#endif /* DEV_ISA */
#ifdef KDB
	if (!claimed && kdb_on_nmi) {
		/*
		 * NMI can be hooked up to a pushbutton for debugging.
		 */
		printf("NMI/cpu%d ... going to debugger\n", cpu);
		kdb_trap(type, 0, frame);
	}
#endif /* KDB */
}

void
nmi_handle_intr(u_int type, struct trapframe *frame)
{

#ifdef SMP
	if (nmi_is_broadcast) {
		nmi_call_kdb_smp(type, frame);
		return;
	}
#endif
	nmi_call_kdb(PCPU_GET(cpuid), type, frame);
}

int hw_ibrs_active;
int hw_ibrs_disable = 1;

SYSCTL_INT(_hw, OID_AUTO, ibrs_active, CTLFLAG_RD, &hw_ibrs_active, 0,
    "Indirect Branch Restricted Speculation active");

void
hw_ibrs_recalculate(void)
{
	uint64_t v;

	if ((cpu_ia32_arch_caps & IA32_ARCH_CAP_IBRS_ALL) != 0) {
		if (hw_ibrs_disable) {
			v = rdmsr(MSR_IA32_SPEC_CTRL);
			v &= ~(uint64_t)IA32_SPEC_CTRL_IBRS;
			wrmsr(MSR_IA32_SPEC_CTRL, v);
		} else {
			v = rdmsr(MSR_IA32_SPEC_CTRL);
			v |= IA32_SPEC_CTRL_IBRS;
			wrmsr(MSR_IA32_SPEC_CTRL, v);
		}
		return;
	}
	hw_ibrs_active = (cpu_stdext_feature3 & CPUID_STDEXT3_IBPB) != 0 &&
	    !hw_ibrs_disable;
}

static int
hw_ibrs_disable_handler(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = hw_ibrs_disable;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	hw_ibrs_disable = val != 0;
	hw_ibrs_recalculate();
	return (0);
}
SYSCTL_PROC(_hw, OID_AUTO, ibrs_disable, CTLTYPE_INT | CTLFLAG_RWTUN |
    CTLFLAG_NOFETCH | CTLFLAG_MPSAFE, NULL, 0, hw_ibrs_disable_handler, "I",
    "Disable Indirect Branch Restricted Speculation");

int hw_ssb_active;
int hw_ssb_disable;

SYSCTL_INT(_hw, OID_AUTO, spec_store_bypass_disable_active, CTLFLAG_RD,
    &hw_ssb_active, 0,
    "Speculative Store Bypass Disable active");

static void
hw_ssb_set_one(bool enable)
{
	uint64_t v;

	v = rdmsr(MSR_IA32_SPEC_CTRL);
	if (enable)
		v |= (uint64_t)IA32_SPEC_CTRL_SSBD;
	else
		v &= ~(uint64_t)IA32_SPEC_CTRL_SSBD;
	wrmsr(MSR_IA32_SPEC_CTRL, v);
}

static void
hw_ssb_set(bool enable, bool for_all_cpus)
{
	struct thread *td;
	int bound_cpu, i, is_bound;

	if ((cpu_stdext_feature3 & CPUID_STDEXT3_SSBD) == 0) {
		hw_ssb_active = 0;
		return;
	}
	hw_ssb_active = enable;
	if (for_all_cpus) {
		td = curthread;
		thread_lock(td);
		is_bound = sched_is_bound(td);
		bound_cpu = td->td_oncpu;
		CPU_FOREACH(i) {
			sched_bind(td, i);
			hw_ssb_set_one(enable);
		}
		if (is_bound)
			sched_bind(td, bound_cpu);
		else
			sched_unbind(td);
		thread_unlock(td);
	} else {
		hw_ssb_set_one(enable);
	}
}

void
hw_ssb_recalculate(bool all_cpus)
{

	switch (hw_ssb_disable) {
	default:
		hw_ssb_disable = 0;
		/* FALLTHROUGH */
	case 0: /* off */
		hw_ssb_set(false, all_cpus);
		break;
	case 1: /* on */
		hw_ssb_set(true, all_cpus);
		break;
	case 2: /* auto */
		hw_ssb_set((cpu_ia32_arch_caps & IA32_ARCH_CAP_SSB_NO) != 0 ?
		    false : true, all_cpus);
		break;
	}
}

static int
hw_ssb_disable_handler(SYSCTL_HANDLER_ARGS)
{
	int error, val;

	val = hw_ssb_disable;
	error = sysctl_handle_int(oidp, &val, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	hw_ssb_disable = val;
	hw_ssb_recalculate(true);
	return (0);
}
SYSCTL_PROC(_hw, OID_AUTO, spec_store_bypass_disable, CTLTYPE_INT |
    CTLFLAG_RWTUN | CTLFLAG_NOFETCH | CTLFLAG_MPSAFE, NULL, 0,
    hw_ssb_disable_handler, "I",
    "Speculative Store Bypass Disable (0 - off, 1 - on, 2 - auto");

/*
 * Enable and restore kernel text write permissions.
 * Callers must ensure that disable_wp()/restore_wp() are executed
 * without rescheduling on the same core.
 */
bool
disable_wp(void)
{
	u_int cr0;

	cr0 = rcr0();
	if ((cr0 & CR0_WP) == 0)
		return (false);
	load_cr0(cr0 & ~CR0_WP);
	return (true);
}

void
restore_wp(bool old_wp)
{

	if (old_wp)
		load_cr0(rcr0() | CR0_WP);
}

bool
acpi_get_fadt_bootflags(uint16_t *flagsp)
{
#ifdef DEV_ACPI
	ACPI_TABLE_FADT *fadt;
	vm_paddr_t physaddr;

	physaddr = acpi_find_table(ACPI_SIG_FADT);
	if (physaddr == 0)
		return (false);
	fadt = acpi_map_table(physaddr, ACPI_SIG_FADT);
	if (fadt == NULL)
		return (false);
	*flagsp = fadt->BootFlags;
	acpi_unmap_table(fadt);
	return (true);
#else
	return (false);
#endif
}
