/* smp.h: PPC specific SMP stuff.
 *
 * Original was a copy of sparc smp.h.  Now heavily modified
 * for PPC.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996-2001 Cort Dougan <cort@fsmlabs.com>
 */
#ifdef __KERNEL__
#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/cpumask.h>
#include <linux/threads.h>

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

struct cpuinfo_PPC {
	unsigned long loops_per_jiffy;
	unsigned long pvr;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
};

extern struct cpuinfo_PPC cpu_data[];
extern cpumask_t cpu_online_map;
extern cpumask_t cpu_possible_map;
extern unsigned long smp_proc_in_lock[];
extern volatile unsigned long cpu_callin_map[];
extern int smp_tb_synchronized;

extern void smp_send_tlb_invalidate(int);
extern void smp_send_xmon_break(int cpu);
struct pt_regs;
extern void smp_message_recv(int, struct pt_regs *);

extern int __cpu_disable(void);
extern void __cpu_die(unsigned int cpu);
extern void cpu_die(void) __attribute__((noreturn));

#define NO_PROC_ID		0xFF            /* No processor magic marker */
#define PROC_CHANGE_PENALTY	20

#define raw_smp_processor_id()	(current_thread_info()->cpu)

extern int __cpu_up(unsigned int cpu);

extern int smp_hw_index[];
#define hard_smp_processor_id() (smp_hw_index[smp_processor_id()])

struct klock_info_struct {
	unsigned long kernel_flag;
	unsigned char akp;
};

extern struct klock_info_struct klock_info;
#define KLOCK_HELD       0xffffffff
#define KLOCK_CLEAR      0x0

#endif /* __ASSEMBLY__ */

#else /* !(CONFIG_SMP) */

static inline void cpu_die(void) { }

#endif /* !(CONFIG_SMP) */

#endif /* !(_PPC_SMP_H) */
#endif /* __KERNEL__ */
