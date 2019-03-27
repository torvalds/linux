/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
 * Copyright (c) 2007 - 2011 Marius Strobl <marius@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#ifndef	_MACHINE_SMP_H_
#define	_MACHINE_SMP_H_

#ifdef SMP

#define	CPU_TICKSYNC		1
#define	CPU_STICKSYNC		2
#define	CPU_INIT		3
#define	CPU_BOOTSTRAP		4

#ifndef	LOCORE

#include <sys/param.h>
#include <sys/cpuset.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>

#include <machine/atomic.h>
#include <machine/intr_machdep.h>
#include <machine/tte.h>

#define	IDR_BUSY			0x0000000000000001ULL
#define	IDR_NACK			0x0000000000000002ULL
#define	IDR_CHEETAH_ALL_BUSY		0x5555555555555555ULL
#define	IDR_CHEETAH_ALL_NACK		(~IDR_CHEETAH_ALL_BUSY)
#define	IDR_CHEETAH_MAX_BN_PAIRS	32
#define	IDR_JALAPENO_MAX_BN_PAIRS	4

#define	IDC_ITID_SHIFT			14
#define	IDC_BN_SHIFT			24

#define	IPI_AST		PIL_AST
#define	IPI_RENDEZVOUS	PIL_RENDEZVOUS
#define	IPI_PREEMPT	PIL_PREEMPT
#define	IPI_HARDCLOCK	PIL_HARDCLOCK
#define	IPI_STOP	PIL_STOP
#define	IPI_STOP_HARD	PIL_STOP

#define	IPI_RETRIES	5000

struct cpu_start_args {
	u_int	csa_count;
	u_int	csa_mid;
	u_int	csa_state;
	vm_offset_t csa_pcpu;
	u_long	csa_tick;
	u_long	csa_stick;
	u_long	csa_ver;
	struct	tte csa_ttes[PCPU_PAGES];
};

struct ipi_cache_args {
	cpuset_t ica_mask;
	vm_paddr_t ica_pa;
};

struct ipi_rd_args {
	cpuset_t ira_mask;
	register_t *ira_val;
};

struct ipi_tlb_args {
	cpuset_t ita_mask;
	struct	pmap *ita_pmap;
	u_long	ita_start;
	u_long	ita_end;
};
#define	ita_va	ita_start

struct pcb;
struct pcpu;

extern struct pcb stoppcbs[];

void	cpu_mp_bootstrap(struct pcpu *pc);
void	cpu_mp_shutdown(void);

typedef	void cpu_ipi_selected_t(cpuset_t, u_long, u_long, u_long);
extern	cpu_ipi_selected_t *cpu_ipi_selected;
typedef	void cpu_ipi_single_t(u_int, u_long, u_long, u_long);
extern	cpu_ipi_single_t *cpu_ipi_single;

void	mp_init(void);

extern	struct mtx ipi_mtx;
extern	struct ipi_cache_args ipi_cache_args;
extern	struct ipi_rd_args ipi_rd_args;
extern	struct ipi_tlb_args ipi_tlb_args;

extern	char *mp_tramp_code;
extern	u_long mp_tramp_code_len;
extern	u_long mp_tramp_tlb_slots;
extern	u_long mp_tramp_func;

extern	void mp_startup(void);

extern	char tl_ipi_cheetah_dcache_page_inval[];
extern	char tl_ipi_spitfire_dcache_page_inval[];
extern	char tl_ipi_spitfire_icache_page_inval[];

extern	char tl_ipi_level[];

extern	char tl_ipi_stick_rd[];
extern	char tl_ipi_tick_rd[];

extern	char tl_ipi_tlb_context_demap[];
extern	char tl_ipi_tlb_page_demap[];
extern	char tl_ipi_tlb_range_demap[];

static __inline void
ipi_all_but_self(u_int ipi)
{
	cpuset_t cpus;

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return;
	cpus = all_cpus;
	sched_pin();
	CPU_CLR(PCPU_GET(cpuid), &cpus);
	mtx_lock_spin(&ipi_mtx);
	cpu_ipi_selected(cpus, 0, (u_long)tl_ipi_level, ipi);
	mtx_unlock_spin(&ipi_mtx);
	sched_unpin();
}

static __inline void
ipi_selected(cpuset_t cpus, u_int ipi)
{

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0 ||
	    CPU_EMPTY(&cpus)))
		return;
	mtx_lock_spin(&ipi_mtx);
	cpu_ipi_selected(cpus, 0, (u_long)tl_ipi_level, ipi);
	mtx_unlock_spin(&ipi_mtx);
}

static __inline void
ipi_cpu(int cpu, u_int ipi)
{

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return;
	mtx_lock_spin(&ipi_mtx);
	cpu_ipi_single(cpu, 0, (u_long)tl_ipi_level, ipi);
	mtx_unlock_spin(&ipi_mtx);
}

#if defined(_MACHINE_PMAP_H_) && defined(_SYS_MUTEX_H_)

static __inline void *
ipi_dcache_page_inval(void *func, vm_paddr_t pa)
{
	struct ipi_cache_args *ica;

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return (NULL);
	sched_pin();
	ica = &ipi_cache_args;
	mtx_lock_spin(&ipi_mtx);
	ica->ica_mask = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &ica->ica_mask);
	ica->ica_pa = pa;
	cpu_ipi_selected(ica->ica_mask, 0, (u_long)func, (u_long)ica);
	return (&ica->ica_mask);
}

static __inline void *
ipi_icache_page_inval(void *func, vm_paddr_t pa)
{
	struct ipi_cache_args *ica;

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return (NULL);
	sched_pin();
	ica = &ipi_cache_args;
	mtx_lock_spin(&ipi_mtx);
	ica->ica_mask = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &ica->ica_mask);
	ica->ica_pa = pa;
	cpu_ipi_selected(ica->ica_mask, 0, (u_long)func, (u_long)ica);
	return (&ica->ica_mask);
}

static __inline void *
ipi_rd(u_int cpu, void *func, u_long *val)
{
	struct ipi_rd_args *ira;

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return (NULL);
	sched_pin();
	ira = &ipi_rd_args;
	mtx_lock_spin(&ipi_mtx);
	CPU_SETOF(cpu, &ira->ira_mask);
	ira->ira_val = val;
	cpu_ipi_single(cpu, 0, (u_long)func, (u_long)ira);
	return (&ira->ira_mask);
}

static __inline void *
ipi_tlb_context_demap(struct pmap *pm)
{
	struct ipi_tlb_args *ita;
	cpuset_t cpus;

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return (NULL);
	sched_pin();
	cpus = pm->pm_active;
	CPU_AND(&cpus, &all_cpus);
	CPU_CLR(PCPU_GET(cpuid), &cpus);
	if (CPU_EMPTY(&cpus)) {
		sched_unpin();
		return (NULL);
	}
	ita = &ipi_tlb_args;
	mtx_lock_spin(&ipi_mtx);
	ita->ita_mask = cpus;
	ita->ita_pmap = pm;
	cpu_ipi_selected(cpus, 0, (u_long)tl_ipi_tlb_context_demap,
	    (u_long)ita);
	return (&ita->ita_mask);
}

static __inline void *
ipi_tlb_page_demap(struct pmap *pm, vm_offset_t va)
{
	struct ipi_tlb_args *ita;
	cpuset_t cpus;

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return (NULL);
	sched_pin();
	cpus = pm->pm_active;
	CPU_AND(&cpus, &all_cpus);
	CPU_CLR(PCPU_GET(cpuid), &cpus);
	if (CPU_EMPTY(&cpus)) {
		sched_unpin();
		return (NULL);
	}
	ita = &ipi_tlb_args;
	mtx_lock_spin(&ipi_mtx);
	ita->ita_mask = cpus;
	ita->ita_pmap = pm;
	ita->ita_va = va;
	cpu_ipi_selected(cpus, 0, (u_long)tl_ipi_tlb_page_demap, (u_long)ita);
	return (&ita->ita_mask);
}

static __inline void *
ipi_tlb_range_demap(struct pmap *pm, vm_offset_t start, vm_offset_t end)
{
	struct ipi_tlb_args *ita;
	cpuset_t cpus;

	if (__predict_false(atomic_load_acq_int(&smp_started) == 0))
		return (NULL);
	sched_pin();
	cpus = pm->pm_active;
	CPU_AND(&cpus, &all_cpus);
	CPU_CLR(PCPU_GET(cpuid), &cpus);
	if (CPU_EMPTY(&cpus)) {
		sched_unpin();
		return (NULL);
	}
	ita = &ipi_tlb_args;
	mtx_lock_spin(&ipi_mtx);
	ita->ita_mask = cpus;
	ita->ita_pmap = pm;
	ita->ita_start = start;
	ita->ita_end = end;
	cpu_ipi_selected(cpus, 0, (u_long)tl_ipi_tlb_range_demap,
	    (u_long)ita);
	return (&ita->ita_mask);
}

static __inline void
ipi_wait(void *cookie)
{
	volatile cpuset_t *mask;

	if (__predict_false((mask = cookie) != NULL)) {
		while (!CPU_EMPTY(mask))
			;
		mtx_unlock_spin(&ipi_mtx);
		sched_unpin();
	}
}

#endif /* _MACHINE_PMAP_H_ && _SYS_MUTEX_H_ */

#endif /* !LOCORE */

#else

#ifndef	LOCORE

static __inline void *
ipi_dcache_page_inval(void *func __unused, vm_paddr_t pa __unused)
{

	return (NULL);
}

static __inline void *
ipi_icache_page_inval(void *func __unused, vm_paddr_t pa __unused)
{

	return (NULL);
}

static __inline void *
ipi_rd(u_int cpu __unused, void *func __unused, u_long *val __unused)
{

	return (NULL);
}

static __inline void *
ipi_tlb_context_demap(struct pmap *pm __unused)
{

	return (NULL);
}

static __inline void *
ipi_tlb_page_demap(struct pmap *pm __unused, vm_offset_t va __unused)
{

	return (NULL);
}

static __inline void *
ipi_tlb_range_demap(struct pmap *pm __unused, vm_offset_t start __unused,
    __unused vm_offset_t end)
{

	return (NULL);
}

static __inline void
ipi_wait(void *cookie __unused)
{

}

static __inline void
tl_ipi_cheetah_dcache_page_inval(void)
{

}

static __inline void
tl_ipi_spitfire_dcache_page_inval(void)
{

}

static __inline void
tl_ipi_spitfire_icache_page_inval(void)
{

}

#endif /* !LOCORE */

#endif /* SMP */

#endif /* !_MACHINE_SMP_H_ */
