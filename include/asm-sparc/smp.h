/* smp.h: Sparc specific SMP stuff.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SMP_H
#define _SPARC_SMP_H

#include <linux/threads.h>
#include <asm/head.h>
#include <asm/btfixup.h>

#ifndef __ASSEMBLY__

#include <linux/cpumask.h>

#endif /* __ASSEMBLY__ */

#ifdef CONFIG_SMP

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>
#include <asm/asi.h>
#include <asm/atomic.h>

/*
 *	Private routines/data
 */
 
extern unsigned char boot_cpu_id;
extern cpumask_t phys_cpu_present_map;
#define cpu_possible_map phys_cpu_present_map

typedef void (*smpfunc_t)(unsigned long, unsigned long, unsigned long,
		       unsigned long, unsigned long);

/*
 *	General functions that each host system must provide.
 */
 
void sun4m_init_smp(void);
void sun4d_init_smp(void);

void smp_callin(void);
void smp_boot_cpus(void);
void smp_store_cpu_info(int);

struct seq_file;
void smp_bogo(struct seq_file *);
void smp_info(struct seq_file *);

BTFIXUPDEF_CALL(void, smp_cross_call, smpfunc_t, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long)
BTFIXUPDEF_CALL(void, smp_message_pass, int, int, unsigned long, int)
BTFIXUPDEF_CALL(int, __hard_smp_processor_id, void)
BTFIXUPDEF_BLACKBOX(hard_smp_processor_id)
BTFIXUPDEF_BLACKBOX(load_current)

#define smp_cross_call(func,arg1,arg2,arg3,arg4,arg5) BTFIXUP_CALL(smp_cross_call)(func,arg1,arg2,arg3,arg4,arg5)
#define smp_message_pass(target,msg,data,wait) BTFIXUP_CALL(smp_message_pass)(target,msg,data,wait)

static inline void xc0(smpfunc_t func) { smp_cross_call(func, 0, 0, 0, 0, 0); }
static inline void xc1(smpfunc_t func, unsigned long arg1)
{ smp_cross_call(func, arg1, 0, 0, 0, 0); }
static inline void xc2(smpfunc_t func, unsigned long arg1, unsigned long arg2)
{ smp_cross_call(func, arg1, arg2, 0, 0, 0); }
static inline void xc3(smpfunc_t func, unsigned long arg1, unsigned long arg2,
			   unsigned long arg3)
{ smp_cross_call(func, arg1, arg2, arg3, 0, 0); }
static inline void xc4(smpfunc_t func, unsigned long arg1, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4)
{ smp_cross_call(func, arg1, arg2, arg3, arg4, 0); }
static inline void xc5(smpfunc_t func, unsigned long arg1, unsigned long arg2,
			   unsigned long arg3, unsigned long arg4, unsigned long arg5)
{ smp_cross_call(func, arg1, arg2, arg3, arg4, arg5); }

static inline int smp_call_function(void (*func)(void *info), void *info, int nonatomic, int wait)
{
	xc1((smpfunc_t)func, (unsigned long)info);
	return 0;
}

static inline int cpu_logical_map(int cpu)
{
	return cpu;
}

static inline int hard_smp4m_processor_id(void)
{
	int cpuid;

	__asm__ __volatile__("rd %%tbr, %0\n\t"
			     "srl %0, 12, %0\n\t"
			     "and %0, 3, %0\n\t" :
			     "=&r" (cpuid));
	return cpuid;
}

static inline int hard_smp4d_processor_id(void)
{
	int cpuid;

	__asm__ __volatile__("lda [%%g0] %1, %0\n\t" :
			     "=&r" (cpuid) : "i" (ASI_M_VIKING_TMP1));
	return cpuid;
}

#ifndef MODULE
static inline int hard_smp_processor_id(void)
{
	int cpuid;

	/* Black box - sun4m
		__asm__ __volatile__("rd %%tbr, %0\n\t"
				     "srl %0, 12, %0\n\t"
				     "and %0, 3, %0\n\t" :
				     "=&r" (cpuid));
	             - sun4d
	   	__asm__ __volatile__("lda [%g0] ASI_M_VIKING_TMP1, %0\n\t"
	   			     "nop; nop" :
	   			     "=&r" (cpuid));
	   See btfixup.h and btfixupprep.c to understand how a blackbox works.
	 */
	__asm__ __volatile__("sethi %%hi(___b_hard_smp_processor_id), %0\n\t"
			     "sethi %%hi(boot_cpu_id), %0\n\t"
			     "ldub [%0 + %%lo(boot_cpu_id)], %0\n\t" :
			     "=&r" (cpuid));
	return cpuid;
}
#else
static inline int hard_smp_processor_id(void)
{
	int cpuid;
	
	__asm__ __volatile__("mov %%o7, %%g1\n\t"
			     "call ___f___hard_smp_processor_id\n\t"
			     " nop\n\t"
			     "mov %%g2, %0\n\t" : "=r"(cpuid) : : "g1", "g2");
	return cpuid;
}
#endif

#define raw_smp_processor_id()		(current_thread_info()->cpu)

#define prof_multiplier(__cpu)		cpu_data(__cpu).multiplier
#define prof_counter(__cpu)		cpu_data(__cpu).counter

#endif /* !(__ASSEMBLY__) */

/* Sparc specific messages. */
#define MSG_CROSS_CALL         0x0005       /* run func on cpus */

/* Empirical PROM processor mailbox constants.  If the per-cpu mailbox
 * contains something other than one of these then the ipi is from
 * Linux's active_kernel_processor.  This facility exists so that
 * the boot monitor can capture all the other cpus when one catches
 * a watchdog reset or the user enters the monitor using L1-A keys.
 */
#define MBOX_STOPCPU          0xFB
#define MBOX_IDLECPU          0xFC
#define MBOX_IDLECPU2         0xFD
#define MBOX_STOPCPU2         0xFE

#endif /* SMP */

#define NO_PROC_ID            0xFF

#endif /* !(_SPARC_SMP_H) */
