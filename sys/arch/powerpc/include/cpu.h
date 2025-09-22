/*	$OpenBSD: cpu.h,v 1.80 2025/04/07 15:43:00 gkoehler Exp $	*/
/*	$NetBSD: cpu.h,v 1.1 1996/09/30 16:34:21 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef	_POWERPC_CPU_H_
#define	_POWERPC_CPU_H_

#include <machine/frame.h>

#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/sched.h>
#include <sys/srp.h>

struct cpu_info {
	struct device *ci_dev;		/* our device */
	struct schedstate_percpu ci_schedstate; /* scheduler state */

	struct proc *ci_curproc;

	struct pcb *ci_curpcb;
	struct pmap *ci_curpm;
	struct proc *ci_fpuproc;
	struct proc *ci_vecproc;
	int ci_cpuid;

	volatile int ci_want_resched;
	volatile int ci_cpl;
	volatile int ci_ipending;
	volatile int ci_dec_deferred;

	volatile int	ci_flags;
#define	CI_FLAGS_SLEEPING		2

#if defined(MULTIPROCESSOR)
	struct srp_hazard ci_srp_hazards[SRP_HAZARD_NUM];
#endif

	int ci_idepth;
	char *ci_intstk;
#define CPUSAVE_LEN	8
	register_t ci_tempsave[CPUSAVE_LEN];
	register_t ci_ddbsave[CPUSAVE_LEN];
#define DISISAVE_LEN	4
	register_t ci_disisave[DISISAVE_LEN];

	struct clockqueue ci_queue;

	volatile int    ci_ddb_paused;
#define	CI_DDB_RUNNING	0
#define	CI_DDB_SHOULDSTOP	1
#define	CI_DDB_STOPPED		2
#define	CI_DDB_ENTERDDB		3
#define	CI_DDB_INDDB		4

	u_int32_t ci_randseed;

#ifdef DIAGNOSTIC
	int	ci_mutex_level;
#endif
#ifdef GPROF
	struct gmonparam *ci_gmon;
	struct clockintr ci_gmonclock;
#endif
	char ci_panicbuf[512];
};

static __inline struct cpu_info *
curcpu(void)
{
	struct cpu_info *ci;

	__asm volatile ("mfsprg %0,0" : "=r"(ci));
	return ci;
}

#define	curpcb			(curcpu()->ci_curpcb)
#define	curpm			(curcpu()->ci_curpm)

#define CPU_INFO_UNIT(ci)	((ci)->ci_dev ? (ci)->ci_dev->dv_unit : 0)

#ifdef MULTIPROCESSOR

#define PPC_MAXPROCS		4

static __inline int
cpu_number(void)
{
	int pir;

	pir = curcpu()->ci_cpuid;
	return pir;
}

void	cpu_boot_secondary_processors(void);

#define CPU_IS_PRIMARY(ci)	((ci)->ci_cpuid == 0)
#define CPU_IS_RUNNING(ci)	1
#define CPU_INFO_ITERATOR		int
#define CPU_INFO_FOREACH(cii, ci)					\
	for (cii = 0, ci = &cpu_info[0]; cii < ncpusfound; cii++, ci++)

void cpu_unidle(struct cpu_info *);

#else

#define PPC_MAXPROCS		1

#define cpu_number()		0

#define CPU_IS_PRIMARY(ci)	1
#define CPU_IS_RUNNING(ci)	1
#define CPU_INFO_ITERATOR		int
#define CPU_INFO_FOREACH(cii, ci)					\
	for (cii = 0, ci = curcpu(); ci != NULL; ci = NULL)

#define cpu_unidle(ci)

#endif

#define CPU_BUSY_CYCLE()	do {} while (0)

#define MAXCPUS	PPC_MAXPROCS

extern struct cpu_info cpu_info[PPC_MAXPROCS];

#define	CLKF_USERMODE(frame)	(((frame)->srr1 & PSL_PR) != 0)
#define	CLKF_PC(frame)		((frame)->srr0)
#define	CLKF_INTR(frame)	((frame)->depth > 1)

extern int ppc_cpuidle;
extern int ppc_proc_is_64b;
extern int ppc_nobat;

void	cpu_bootstrap(void);

static inline unsigned int
cpu_rnd_messybits(void)
{
	unsigned int hi, lo;

	__asm volatile("mftbu %0; mftb %1" : "=r" (hi), "=r" (lo));

	return (hi ^ lo);
}

/*
 * This is used during profiling to integrate system time.
 */
#define	PROC_PC(p)		(trapframe(p)->srr0)
#define	PROC_STACK(p)		(trapframe(p)->fixreg[1])

void	delay(unsigned);
#define	DELAY(n)		delay(n)

#define	aston(p)		((p)->p_md.md_astpending = 1)

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 */
#define	need_resched(ci) \
do {									\
	ci->ci_want_resched = 1;					\
	if (ci->ci_curproc != NULL)					\
		aston(ci->ci_curproc);					\
} while (0)
#define clear_resched(ci) (ci)->ci_want_resched = 0

#define	need_proftick(p)	aston(p)

void	signotify(struct proc *);

extern char *bootpath;

#ifndef	CACHELINESIZE
#define	CACHELINESIZE	32			/* For now		XXX */
#endif

static __inline void
syncicache(void *from, size_t len)
{
	size_t	by, i;

	by = CACHELINESIZE;
	i = 0;
	do {
		__asm volatile ("dcbst %0,%1" :: "r"(from), "r"(i));
		i += by;
	} while (i < len);
	__asm volatile ("sync");
	i = 0;
	do {
		__asm volatile ("icbi %0,%1" :: "r"(from), "r"(i));
		i += by;
	} while (i < len);
	__asm volatile ("sync; isync");
}

static __inline void
invdcache(void *from, int len)
{
	int l;
	char *p = from;

	len = len + (((u_int32_t) from) & (CACHELINESIZE - 1));
	l = len;

	do {
		__asm volatile ("dcbi 0,%0" :: "r"(p));
		p += CACHELINESIZE;
	} while ((l -= CACHELINESIZE) > 0);
	__asm volatile ("sync");
}

static __inline void
flushdcache(void *from, int len)
{
	int l;
	char *p = from;

	len = len + (((u_int32_t) from) & (CACHELINESIZE - 1));
	l = len;

	do {
		__asm volatile ("dcbf 0,%0" :: "r"(p));
		p += CACHELINESIZE;
	} while ((l -= CACHELINESIZE) > 0);
	__asm volatile ("sync");
}

#define FUNC_SPR(n, name) \
static __inline u_int32_t ppc_mf ## name(void)			\
{								\
	u_int32_t ret;						\
	__asm volatile ("mfspr %0," # n : "=r" (ret));		\
	return ret;						\
}								\
static __inline void ppc_mt ## name(u_int32_t val)		\
{								\
	__asm volatile ("mtspr "# n ",%0" :: "r" (val));	\
}								\

FUNC_SPR(0, mq)
FUNC_SPR(1, xer)
FUNC_SPR(4, rtcu)
FUNC_SPR(5, rtcl)
FUNC_SPR(8, lr)
FUNC_SPR(9, ctr)
FUNC_SPR(18, dsisr)
FUNC_SPR(19, dar)
FUNC_SPR(22, dec)
FUNC_SPR(25, sdr1)
FUNC_SPR(26, srr0)
FUNC_SPR(27, srr1)
FUNC_SPR(256, vrsave)
FUNC_SPR(272, sprg0)
FUNC_SPR(273, sprg1)
FUNC_SPR(274, sprg2)
FUNC_SPR(275, sprg3)
FUNC_SPR(280, asr)
FUNC_SPR(282, ear)
FUNC_SPR(287, pvr)
FUNC_SPR(311, hior)
FUNC_SPR(528, ibat0u)
FUNC_SPR(529, ibat0l)
FUNC_SPR(530, ibat1u)
FUNC_SPR(531, ibat1l)
FUNC_SPR(532, ibat2u)
FUNC_SPR(533, ibat2l)
FUNC_SPR(534, ibat3u)
FUNC_SPR(535, ibat3l)
FUNC_SPR(560, ibat4u)
FUNC_SPR(561, ibat4l)
FUNC_SPR(562, ibat5u)
FUNC_SPR(563, ibat5l)
FUNC_SPR(564, ibat6u)
FUNC_SPR(565, ibat6l)
FUNC_SPR(566, ibat7u)
FUNC_SPR(567, ibat7l)
FUNC_SPR(536, dbat0u)
FUNC_SPR(537, dbat0l)
FUNC_SPR(538, dbat1u)
FUNC_SPR(539, dbat1l)
FUNC_SPR(540, dbat2u)
FUNC_SPR(541, dbat2l)
FUNC_SPR(542, dbat3u)
FUNC_SPR(543, dbat3l)
FUNC_SPR(568, dbat4u)
FUNC_SPR(569, dbat4l)
FUNC_SPR(570, dbat5u)
FUNC_SPR(571, dbat5l)
FUNC_SPR(572, dbat6u)
FUNC_SPR(573, dbat6l)
FUNC_SPR(574, dbat7u)
FUNC_SPR(575, dbat7l)
FUNC_SPR(1009, hid1)
FUNC_SPR(1010, iabr)
FUNC_SPR(1017, l2cr)
FUNC_SPR(1018, l3cr)
FUNC_SPR(1013, dabr)
FUNC_SPR(1023, pir)

static __inline u_int32_t
ppc_mftbl(void)
{
	int ret;
	__asm volatile ("mftb %0" : "=r" (ret));
	return ret;
}


static __inline u_int64_t
ppc_mftb(void)
{
	u_long scratch;
	u_int64_t tb;

	__asm volatile ("1: mftbu %0; mftb %L0; mftbu %1;"
	    " cmpw 0,%0,%1; bne 1b" : "=r"(tb), "=r"(scratch));
	return tb;
}

static __inline void
ppc_mttb(u_int64_t tb)
{
	__asm volatile ("mttbl %0" :: "r"(0));
	__asm volatile ("mttbu %0" :: "r"((u_int32_t)(tb >> 32)));
	__asm volatile ("mttbl %0" :: "r"((u_int32_t)(tb & 0xffffffff)));
}

static __inline u_int32_t
ppc_mfmsr(void)
{
	int ret;
        __asm volatile ("mfmsr %0" : "=r" (ret));
	return ret;
}

static __inline void
ppc_mtmsr(u_int32_t val)
{
        __asm volatile ("mtmsr %0" :: "r" (val));
}

static __inline void
ppc_mtsrin(u_int32_t val, u_int32_t sn_shifted)
{
	__asm volatile ("mtsrin %0,%1" :: "r"(val), "r"(sn_shifted));
}

u_int64_t ppc64_mfscomc(void);
void ppc_mtscomc(u_int32_t);
void ppc64_mtscomc(u_int64_t);
u_int64_t ppc64_mfscomd(void);
void ppc_mtscomd(u_int32_t);
u_int32_t ppc_mfhid0(void);
void ppc_mthid0(u_int32_t);
u_int64_t ppc64_mfhid1(void);
void ppc64_mthid1(u_int64_t);
u_int64_t ppc64_mfhid4(void);
void ppc64_mthid4(u_int64_t);
u_int64_t ppc64_mfhid5(void);
void ppc64_mthid5(u_int64_t);

#include <machine/psl.h>

/*
 * General functions to enable and disable interrupts
 * without having inlined assembly code in many functions.
 */
static __inline void
ppc_intr_enable(int enable)
{
	u_int32_t msr;
	if (enable != 0) {
		msr = ppc_mfmsr();
		msr |= PSL_EE;
		ppc_mtmsr(msr);
	}
}

static __inline int
ppc_intr_disable(void)
{
	u_int32_t emsr, dmsr;
	emsr = ppc_mfmsr();
	dmsr = emsr & ~PSL_EE;
	ppc_mtmsr(dmsr);
	return (emsr & PSL_EE);
}

static inline void
intr_enable(void)
{
	ppc_mtmsr(ppc_mfmsr() | PSL_EE);
}

static __inline u_long
intr_disable(void)
{
	return ppc_intr_disable();
}

static __inline void
intr_restore(u_long s)
{
	ppc_intr_enable(s);
}

int ppc_cpuspeed(int *);

/*
 * PowerPC CPU types
 */
#define	PPC_CPU_MPC601		1
#define	PPC_CPU_MPC603		3
#define	PPC_CPU_MPC604		4
#define	PPC_CPU_MPC603e		6
#define	PPC_CPU_MPC603ev	7
#define	PPC_CPU_MPC750		8
#define	PPC_CPU_MPC604ev	9
#define	PPC_CPU_MPC7400		12
#define	PPC_CPU_IBM970		0x0039
#define	PPC_CPU_IBM970FX	0x003c
#define	PPC_CPU_IBM970MP	0x0044
#define	PPC_CPU_IBM750FX	0x7000
#define	PPC_CPU_MPC7410		0x800c
#define	PPC_CPU_MPC7447A	0x8003
#define	PPC_CPU_MPC7448		0x8004
#define	PPC_CPU_MPC7450		0x8000
#define	PPC_CPU_MPC7455		0x8001
#define	PPC_CPU_MPC7457		0x8002
#define	PPC_CPU_MPC83xx		0x8083

/*
 * This needs to be included late since it relies on definitions higher
 * up in this file.
 */
#if defined(MULTIPROCESSOR) && defined(_KERNEL)
#include <sys/mplock.h>
#endif

#endif	/* _POWERPC_CPU_H_ */
