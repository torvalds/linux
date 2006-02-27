/* cpudata.h: Per-cpu parameters.
 *
 * Copyright (C) 2003, 2005, 2006 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC64_CPUDATA_H
#define _SPARC64_CPUDATA_H

#ifndef __ASSEMBLY__

#include <linux/percpu.h>
#include <linux/threads.h>

typedef struct {
	/* Dcache line 1 */
	unsigned int	__softirq_pending; /* must be 1st, see rtrap.S */
	unsigned int	multiplier;
	unsigned int	counter;
	unsigned int	idle_volume;
	unsigned long	clock_tick;	/* %tick's per second */
	unsigned long	udelay_val;

	/* Dcache line 2, rarely used */
	unsigned int	dcache_size;
	unsigned int	dcache_line_size;
	unsigned int	icache_size;
	unsigned int	icache_line_size;
	unsigned int	ecache_size;
	unsigned int	ecache_line_size;
	unsigned int	__pad3;
	unsigned int	__pad4;
} cpuinfo_sparc;

DECLARE_PER_CPU(cpuinfo_sparc, __cpu_data);
#define cpu_data(__cpu)		per_cpu(__cpu_data, (__cpu))
#define local_cpu_data()	__get_cpu_var(__cpu_data)

/* Trap handling code needs to get at a few critical values upon
 * trap entry and to process TSB misses.  These cannot be in the
 * per_cpu() area as we really need to lock them into the TLB and
 * thus make them part of the main kernel image.  As a result we
 * try to make this as small as possible.
 *
 * This is padded out and aligned to 64-bytes to avoid false sharing
 * on SMP.
 */

/* If you modify the size of this structure, please update
 * TRAP_BLOCK_SZ_SHIFT below.
 */
struct thread_info;
struct trap_per_cpu {
/* D-cache line 1 */
	struct thread_info	*thread;
	unsigned long		pgd_paddr;
	unsigned long		__pad1[2];

/* D-cache line 2 */
	unsigned long		__pad2[4];
} __attribute__((aligned(64)));
extern struct trap_per_cpu trap_block[NR_CPUS];
extern void init_cur_cpu_trap(void);
extern void setup_tba(void);

#ifdef CONFIG_SMP
struct cpuid_patch_entry {
	unsigned int	addr;
	unsigned int	cheetah_safari[4];
	unsigned int	cheetah_jbus[4];
	unsigned int	starfire[4];
};
extern struct cpuid_patch_entry __cpuid_patch, __cpuid_patch_end;
#endif

#endif /* !(__ASSEMBLY__) */

#define TRAP_PER_CPU_THREAD	0x00
#define TRAP_PER_CPU_PGD_PADDR	0x08

#define TRAP_BLOCK_SZ_SHIFT	6

#ifdef CONFIG_SMP

#define __GET_CPUID(REG)				\
	/* Spitfire implementation (default). */	\
661:	ldxa		[%g0] ASI_UPA_CONFIG, REG;	\
	srlx		REG, 17, REG;			\
	 and		REG, 0x1f, REG;			\
	nop;						\
	.section	.cpuid_patch, "ax";		\
	/* Instruction location. */			\
	.word		661b;				\
	/* Cheetah Safari implementation. */		\
	ldxa		[%g0] ASI_SAFARI_CONFIG, REG;	\
	srlx		REG, 17, REG;			\
	and		REG, 0x3ff, REG;		\
	nop;						\
	/* Cheetah JBUS implementation. */		\
	ldxa		[%g0] ASI_JBUS_CONFIG, REG;	\
	srlx		REG, 17, REG;			\
	and		REG, 0x1f, REG;			\
	nop;						\
	/* Starfire implementation. */			\
	sethi		%hi(0x1fff40000d0 >> 9), REG;	\
	sllx		REG, 9, REG;			\
	or		REG, 0xd0, REG;			\
	lduwa		[REG] ASI_PHYS_BYPASS_EC_E, REG;\
	.previous;

/* Clobbers %g1, current address space PGD phys address into %g7.  */
#define TRAP_LOAD_PGD_PHYS			\
	__GET_CPUID(%g1)			\
	sethi	%hi(trap_block), %g7;		\
	sllx	%g1, TRAP_BLOCK_SZ_SHIFT, %g1;	\
	or	%g7, %lo(trap_block), %g7;	\
	add	%g7, %g1, %g7;			\
	ldx	[%g7 + TRAP_PER_CPU_PGD_PADDR], %g7;

/* Clobbers %g1, loads local processor's IRQ work area into %g6.  */
#define TRAP_LOAD_IRQ_WORK			\
	__GET_CPUID(%g1)			\
	sethi	%hi(__irq_work), %g6;		\
	sllx	%g1, 6, %g1;			\
	or	%g6, %lo(__irq_work), %g6;	\
	add	%g6, %g1, %g6;

/* Clobbers %g1, loads %g6 with current thread info pointer.  */
#define TRAP_LOAD_THREAD_REG			\
	__GET_CPUID(%g1)			\
	sethi	%hi(trap_block), %g6;		\
	sllx	%g1, TRAP_BLOCK_SZ_SHIFT, %g1;	\
	or	%g6, %lo(trap_block), %g6;	\
	ldx	[%g6 + %g1], %g6;

/* Given the current thread info pointer in %g6, load the per-cpu
 * area base of the current processor into %g5.  REG1, REG2, and REG3 are
 * clobbered.
 *
 * You absolutely cannot use %g5 as a temporary in this code.  The
 * reason is that traps can happen during execution, and return from
 * trap will load the fully resolved %g5 per-cpu base.  This can corrupt
 * the calculations done by the macro mid-stream.
 */
#define LOAD_PER_CPU_BASE(REG1, REG2, REG3)		\
	ldub	[%g6 + TI_CPU], REG1;			\
	sethi	%hi(__per_cpu_shift), REG3;		\
	sethi	%hi(__per_cpu_base), REG2;		\
	ldx	[REG3 + %lo(__per_cpu_shift)], REG3;	\
	ldx	[REG2 + %lo(__per_cpu_base)], REG2;	\
	sllx	REG1, REG3, REG3;			\
	add	REG3, REG2, %g5;

#else

/* Uniprocessor versions, we know the cpuid is zero.  */
#define TRAP_LOAD_PGD_PHYS			\
	sethi	%hi(trap_block), %g7;		\
	or	%g7, %lo(trap_block), %g7;	\
	ldx	[%g7 + TRAP_PER_CPU_PGD_PADDR], %g7;

#define TRAP_LOAD_IRQ_WORK			\
	sethi	%hi(__irq_work), %g6;		\
	or	%g6, %lo(__irq_work), %g6;

#define TRAP_LOAD_THREAD_REG			\
	sethi	%hi(trap_block), %g6;		\
	ldx	[%g6 + %lo(trap_block)], %g6;

/* No per-cpu areas on uniprocessor, so no need to load %g5.  */
#define LOAD_PER_CPU_BASE(REG1, REG2, REG3)

#endif /* !(CONFIG_SMP) */

#endif /* _SPARC64_CPUDATA_H */
