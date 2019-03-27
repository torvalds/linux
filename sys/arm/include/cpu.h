/* $NetBSD: cpu.h,v 1.2 2001/02/23 21:23:52 reinoud Exp $ */
/* $FreeBSD$ */

#ifndef MACHINE_CPU_H
#define MACHINE_CPU_H

#include <machine/armreg.h>
#include <machine/frame.h>

void	cpu_halt(void);
void	swi_vm(void *);

#ifdef _KERNEL
#if __ARM_ARCH >= 6
#include <machine/cpu-v6.h>
#else
#include <machine/cpu-v4.h>
#endif /* __ARM_ARCH >= 6 */

static __inline uint64_t
get_cyclecount(void)
{
#if __ARM_ARCH >= 6
#if (__ARM_ARCH > 6) && defined(DEV_PMU)
	if (pmu_attched) {
		u_int cpu;
		uint64_t h, h2;
		uint32_t l, r;

		cpu = PCPU_GET(cpuid);
		h = (uint64_t)atomic_load_acq_32(&ccnt_hi[cpu]);
		l = cp15_pmccntr_get();
		/* In case interrupts are disabled we need to check for overflow. */
		r = cp15_pmovsr_get();
		if (r & PMU_OVSR_C) {
			atomic_add_32(&ccnt_hi[cpu], 1);
			/* Clear the event. */
			cp15_pmovsr_set(PMU_OVSR_C);
		}
		/* Make sure there was no wrap-around while we read the lo half. */
		h2 = (uint64_t)atomic_load_acq_32(&ccnt_hi[cpu]);
		if (h != h2)
			l = cp15_pmccntr_get();
		return (h2 << 32 | l);
	} else
#endif
		return cp15_pmccntr_get();
#else /* No performance counters, so use binuptime(9). This is slooooow */
	struct bintime bt;

	binuptime(&bt);
	return ((uint64_t)bt.sec << 56 | bt.frac >> 8);
#endif
}
#endif

#define TRAPF_USERMODE(frame)	((frame->tf_spsr & PSR_MODE) == PSR_USR32_MODE)

#define TRAPF_PC(tfp)		((tfp)->tf_pc)

#define cpu_getstack(td)	((td)->td_frame->tf_usr_sp)
#define cpu_setstack(td, sp)	((td)->td_frame->tf_usr_sp = (sp))
#define cpu_spinwait()		/* nothing */
#define	cpu_lock_delay()	DELAY(1)

#define ARM_NVEC		8
#define ARM_VEC_ALL		0xffffffff

extern vm_offset_t vector_page;

/*
 * Params passed into initarm. If you change the size of this you will
 * need to update locore.S to allocate more memory on the stack before
 * it calls initarm.
 */
struct arm_boot_params {
	register_t	abp_size;	/* Size of this structure */
	register_t	abp_r0;		/* r0 from the boot loader */
	register_t	abp_r1;		/* r1 from the boot loader */
	register_t	abp_r2;		/* r2 from the boot loader */
	register_t	abp_r3;		/* r3 from the boot loader */
	vm_offset_t	abp_physaddr;	/* The kernel physical address */
	vm_offset_t	abp_pagetable;	/* The early page table */
};

void	arm_vector_init(vm_offset_t, int);
void	fork_trampoline(void);
void	identify_arm_cpu(void);
void	*initarm(struct arm_boot_params *);

extern char btext[];
extern char etext[];
int badaddr_read(void *, size_t, void *);
#endif /* !MACHINE_CPU_H */
