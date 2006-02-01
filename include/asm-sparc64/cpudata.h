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
extern void per_cpu_patch(void);
extern void setup_tba(void);

#endif /* !(__ASSEMBLY__) */

#define TRAP_PER_CPU_THREAD	0x00
#define TRAP_PER_CPU_PGD_PADDR	0x08

#define TRAP_BLOCK_SZ_SHIFT	6

/* Clobbers %g1, loads %g6 with local processor's cpuid */
#define __GET_CPUID			\
	ba,pt	%xcc, __get_cpu_id;	\
	 rd	%pc, %g1;

/* Clobbers %g1, current address space PGD phys address into %g7.  */
#define TRAP_LOAD_PGD_PHYS			\
	__GET_CPUID				\
	sllx	%g6, TRAP_BLOCK_SZ_SHIFT, %g6;	\
	sethi	%hi(trap_block), %g7;		\
	or	%g7, %lo(trap_block), %g7;	\
	add	%g7, %g6, %g7;			\
	ldx	[%g7 + TRAP_PER_CPU_PGD_PADDR], %g7;

/* Clobbers %g1, loads local processor's IRQ work area into %g6.  */
#define TRAP_LOAD_IRQ_WORK			\
	__GET_CPUID				\
	sethi	%hi(__irq_work), %g1;		\
	sllx	%g6, 6, %g6;			\
	or	%g1, %lo(__irq_work), %g1;	\
	add	%g1, %g6, %g6;

/* Clobbers %g1, loads %g6 with current thread info pointer.  */
#define TRAP_LOAD_THREAD_REG			\
	__GET_CPUID				\
	sllx	%g6, TRAP_BLOCK_SZ_SHIFT, %g6;	\
	sethi	%hi(trap_block), %g1;		\
	or	%g1, %lo(trap_block), %g1;	\
	ldx	[%g1 + %g6], %g6;

/* Given the current thread info pointer in %g6, load the per-cpu
 * area base of the current processor into %g5.  REG1, REG2, and REG3 are
 * clobbered.
 *
 * You absolutely cannot use %g5 as a temporary in this code.  The
 * reason is that traps can happen during execution, and return from
 * trap will load the fully resolved %g5 per-cpu base.  This can corrupt
 * the calculations done by the macro mid-stream.
 */
#ifdef CONFIG_SMP
#define LOAD_PER_CPU_BASE(REG1, REG2, REG3)		\
	ldub	[%g6 + TI_CPU], REG1;			\
	sethi	%hi(__per_cpu_shift), REG3;		\
	sethi	%hi(__per_cpu_base), REG2;		\
	ldx	[REG3 + %lo(__per_cpu_shift)], REG3;	\
	ldx	[REG2 + %lo(__per_cpu_base)], REG2;	\
	sllx	REG1, REG3, REG3;			\
	add	REG3, REG2, %g5;
#else
#define LOAD_PER_CPU_BASE(REG1, REG2, REG3)
#endif

#endif /* _SPARC64_CPUDATA_H */
