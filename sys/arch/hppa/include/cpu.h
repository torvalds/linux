/*	$OpenBSD: cpu.h,v 1.107 2024/10/23 18:45:34 miod Exp $	*/

/*
 * Copyright (c) 2000-2004 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */
/* 
 * Copyright (c) 1988-1994, The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: cpu.h 1.19 94/12/16$
 */

#ifndef	_MACHINE_CPU_H_
#define	_MACHINE_CPU_H_

#ifdef _KERNEL
#include <machine/trap.h>
#include <machine/frame.h>
#include <machine/reg.h>
#endif /* _KERNEL */

/*
 * CPU types and features
 */
#define	HPPA_FTRS_TLBU		0x00000001
#define	HPPA_FTRS_BTLBU		0x00000002
#define	HPPA_FTRS_HVT		0x00000004
#define	HPPA_FTRS_W32B		0x00000008

#ifndef _LOCORE
#ifdef _KERNEL
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/queue.h>
#include <sys/sched.h>
#include <sys/srp.h>

#include <machine/mutex.h>

/*
 * Note that the alignment of ci_trap_save is important since we want to keep
 * it within a single cache line. As a result, it must be kept as the first
 * entry within the cpu_info struct.
 */
struct cpu_info {
	register_t	ci_trap_save[16];

	struct device	*ci_dev;
	int		ci_cpuid;
	hppa_hpa_t	ci_hpa;
	volatile int	ci_flags;

	struct proc	*ci_curproc;
	paddr_t		ci_fpu_state;		/* Process FPU state. */
	paddr_t		ci_stack;

#if defined(MULTIPROCESSOR)
	struct srp_hazard ci_srp_hazards[SRP_HAZARD_NUM];
#endif

	register_t	ci_psw;			/* Processor Status Word. */
	volatile int	ci_cpl;
	volatile u_long	ci_mask;		/* Hardware interrupt mask. */
	volatile u_long	ci_ipending;
	volatile int	ci_idepth;
	int		ci_want_resched;

	volatile u_long	ci_ipi;			/* IPIs pending. */
	struct mutex	ci_ipi_mtx;

	struct schedstate_percpu ci_schedstate;
	u_int32_t	ci_randseed;
#ifdef DIAGNOSTIC
	int		ci_mutex_level;
#endif
#ifdef GPROF
	struct gmonparam *ci_gmon;
	struct clockintr ci_gmonclock;
#endif
	struct clockqueue ci_queue;
	char		ci_panicbuf[512];
} __attribute__((__aligned__(64)));

#define		CPUF_RUNNING	0x0001		/* CPU is running. */

#ifdef MULTIPROCESSOR
#define		HPPA_MAXCPUS	4
#else
#define		HPPA_MAXCPUS	1
#endif

extern struct cpu_info cpu_info[HPPA_MAXCPUS];

#define MAXCPUS		HPPA_MAXCPUS

#ifdef MULTIPROCESSOR

static __inline struct cpu_info *
curcpu(void)
{
	struct cpu_info *ci;

	asm volatile ("mfctl    %%cr29, %0" : "=r"(ci));

	return ci;
}

#define cpu_number()		(curcpu()->ci_cpuid)

#define CPU_INFO_UNIT(ci)	((ci)->ci_dev ? (ci)->ci_dev->dv_unit : 0)
#define CPU_IS_PRIMARY(ci)	((ci)->ci_cpuid == 0)
#define CPU_IS_RUNNING(ci)	((ci)->ci_flags & CPUF_RUNNING)

#else

#define	curcpu()		(&cpu_info[0])

#define cpu_number()		0

#define CPU_INFO_UNIT(ci)	0
#define CPU_IS_PRIMARY(ci)	1
#define CPU_IS_RUNNING(ci)	1

#endif

#define	CPU_INFO_ITERATOR	int
#define CPU_INFO_FOREACH(cii, ci) \
	for (cii = 0, ci = &cpu_info[0]; cii < ncpus; cii++, ci++)

#define CPU_BUSY_CYCLE()	__asm volatile ("" ::: "memory")

/* types */
enum hppa_cpu_type {
	hpcxs, hpcxt, hpcxta, hpcxl, hpcxl2, hpcxu, hpcxu2, hpcxw
};
extern enum hppa_cpu_type cpu_type;
extern const char *cpu_typename;
extern int cpu_hvers;
#endif
#endif

/*
 * COPR/SFUs
 */
#define	HPPA_FPUS	0xc0
#define	HPPA_FPUVER(w)	(((w) & 0x003ff800) >> 11)
#define	HPPA_FPU_OP(w)	((w) >> 26)
#define	HPPA_FPU_UNMPL	0x01	/* exception reg, the rest is << 1 */
#define	HPPA_FPU_ILL	0x80	/* software-only */
#define	HPPA_FPU_I	0x01
#define	HPPA_FPU_U	0x02
#define	HPPA_FPU_O	0x04
#define	HPPA_FPU_Z	0x08
#define	HPPA_FPU_V	0x10
#define	HPPA_FPU_D	0x20
#define	HPPA_FPU_T	0x40
#define	HPPA_FPU_XMASK	0x7f
#define	HPPA_FPU_T_POS	25
#define	HPPA_FPU_RM	0x00000600
#define	HPPA_FPU_CQ	0x00fff800
#define	HPPA_FPU_C	0x04000000
#define	HPPA_FPU_FLSH	27
#define	HPPA_FPU_INIT	(0)
#define	HPPA_FPU_FORK(s) ((s) & ~((u_int64_t)(HPPA_FPU_XMASK)<<32))
#define	HPPA_PMSFUS	0x20	/* ??? */

/*
 * Exported definitions unique to hp700/PA-RISC cpu support.
 */

#define	HPPA_PGALIAS	0x00400000
#define	HPPA_PGAOFF	0x003fffff

#define	HPPA_IOBEGIN    0xf0000000
#define	HPPA_IOLEN      0x10000000
#define	HPPA_PDC_LOW	0xef000000
#define	HPPA_PDC_HIGH	0xf1000000
#define	HPPA_IOBCAST	0xfffc0000
#define	HPPA_LBCAST	0xfffc0000
#define	HPPA_GBCAST	0xfffe0000
#define	HPPA_FPA	0xfff80000
#define	HPPA_FLEX_DATA	0xfff80001
#define	HPPA_DMA_ENABLE	0x00000001
#define	HPPA_FLEX_MASK	0xfffc0000
#define	HPPA_FLEX_SIZE	(1 + ~HPPA_FLEX_MASK)
#define	HPPA_FLEX(a)	(((a) & HPPA_FLEX_MASK) >> 18)
#define	HPPA_SPA_ENABLE	0x00000020
#define	HPPA_NMODSPBUS	64

#define	clockframe		trapframe
#define	CLKF_PC(framep)		((framep)->tf_iioq_head)
#define	CLKF_INTR(framep)	((framep)->tf_flags & TFF_INTR)
#define	CLKF_USERMODE(framep)	((framep)->tf_flags & T_USER)

#define	need_proftick(p)	setsoftast(p)
#define	PROC_PC(p)		((p)->p_md.md_regs->tf_iioq_head & ~HPPA_PC_PRIV_MASK)
#define	PROC_STACK(p)		((p)->p_md.md_regs->tf_sp)

#ifndef _LOCORE
#ifdef _KERNEL

#define DELAY(x) delay(x)

extern int (*cpu_desidhash)(void);

void	signotify(struct proc *);
void	delay(u_int us);
void	hppa_init(paddr_t start);
void	trap(int type, struct trapframe *frame);
int	spcopy(pa_space_t ssp, const void *src,
		    pa_space_t dsp, void *dst, size_t size);
int	spcopy32(pa_space_t ssp, const uint32_t *src,
		    pa_space_t dsp, uint32_t *dst);
int	spstrcpy(pa_space_t ssp, const void *src,
		      pa_space_t dsp, void *dst, size_t size, size_t *rsize);
int	copy_on_fault(void);
void	proc_trampoline(void);
int	cpu_dumpsize(void);
int	cpu_dump(void);

static inline unsigned int
cpu_rnd_messybits(void)
{
        unsigned int __itmr;

	__asm volatile("mfctl %1,%0": "=r" (__itmr) : "i" (CR_ITMR));

        return (__itmr);
}

#ifdef MULTIPROCESSOR
void	cpu_boot_secondary_processors(void);
void	cpu_hw_init(void);
void	cpu_hatch(void);
void	cpu_unidle(struct cpu_info *);
#else
#define	cpu_unidle(ci)
#endif

extern void need_resched(struct cpu_info *);
#define clear_resched(ci) 	(ci)->ci_want_resched = 0

#endif

/*
 * Boot arguments stuff
 */

#define	BOOTARG_LEN	PAGE_SIZE
#define	BOOTARG_OFF	0x10000

/*
 * CTL_MACHDEP definitions.
 */
#define	CPU_CONSDEV		1	/* dev_t: console terminal device */
#define	CPU_FPU			2	/* int: fpu present/enabled */
#define	CPU_LED_BLINK		3	/* int: twiddle heartbeat LED/LCD */
#define	CPU_MAXID		4	/* number of valid machdep ids */

#define CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "console_device", CTLTYPE_STRUCT }, \
	{ "fpu", CTLTYPE_INT }, \
	{ "led_blink", CTLTYPE_INT }, \
}

#ifdef _KERNEL
#include <sys/queue.h>

#ifdef MULTIPROCESSOR
#include <sys/mplock.h>
#endif

struct blink_led {
	void (*bl_func)(void *, int);
	void *bl_arg;
	SLIST_ENTRY(blink_led) bl_next;
};

extern void blink_led_register(struct blink_led *);
#endif

#define copyinsn(p, v, ip) copyin32((v), (ip))

#endif

#endif /* _MACHINE_CPU_H_ */
