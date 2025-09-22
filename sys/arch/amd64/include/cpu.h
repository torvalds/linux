/*	$OpenBSD: cpu.h,v 1.180 2025/04/28 16:18:25 bluhm Exp $	*/
/*	$NetBSD: cpu.h,v 1.1 2003/04/26 18:39:39 fvdl Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *	@(#)cpu.h	5.4 (Berkeley) 5/9/91
 */

#ifndef _MACHINE_CPU_H_
#define _MACHINE_CPU_H_

/*
 * Definitions unique to x86-64 cpu support.
 */
#ifdef _KERNEL
#include <machine/frame.h>
#include <machine/segments.h>		/* USERMODE */
#include <machine/intrdefs.h>
#endif /* _KERNEL */

#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/sensors.h>
#include <sys/srp.h>
#include <uvm/uvm_percpu.h>

#ifdef _KERNEL

/* VMXON region (Intel) */
struct vmxon_region {
        uint32_t        vr_revision;
};

/*
 * VMX for Intel CPUs
 */
struct vmx {
	uint64_t	vmx_cr0_fixed0;
	uint64_t	vmx_cr0_fixed1;
	uint64_t	vmx_cr4_fixed0;
	uint64_t	vmx_cr4_fixed1;
	uint32_t	vmx_vmxon_revision;
	uint32_t	vmx_msr_table_size;
	uint32_t	vmx_cr3_tgt_count;
	uint8_t		vmx_has_l1_flush_msr;
	uint64_t	vmx_invept_mode;
};

/*
 * SVM for AMD CPUs
 */
struct svm {
	uint32_t	svm_max_asid;
	uint8_t		svm_flush_by_asid;
	uint8_t		svm_vmcb_clean;
	uint8_t		svm_decode_assist;
};

union vmm_cpu_cap {
	struct vmx vcc_vmx;
	struct svm vcc_svm;
};

enum cpu_vendor {
    CPUV_UNKNOWN,
    CPUV_AMD,
    CPUV_INTEL,
    CPUV_VIA,
};

/*
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	a	atomic operations
 *	o	owned (read/modified only) by this CPU
 */
struct x86_64_tss;
struct vcpu;
struct cpu_info {
	/*
	 * The beginning of this structure in mapped in the userspace "u-k"
	 * page tables, so that these first couple members can be accessed
	 * from the trampoline code.  The ci_PAGEALIGN member defines where
	 * the part that is *not* visible begins, so don't put anything
	 * above it that must be kept hidden from userspace!
	 */
	u_int64_t	ci_kern_cr3;	/* [o] U+K page table */
	u_int64_t	ci_scratch;	/* [o] for U<-->K transition */

#define ci_PAGEALIGN	ci_dev
	struct device *ci_dev;		/* [I] */
	struct cpu_info *ci_self;	/* [I] */
	struct cpu_info *ci_next;	/* [I] */

	u_int ci_cpuid;			/* [I] */
	u_int ci_apicid;		/* [I] */
	u_int ci_acpi_proc_id;		/* [I] */
	u_int32_t ci_randseed;		/* [o] */

	u_int64_t ci_kern_rsp;		/* [o] kernel-only stack */
	u_int64_t ci_intr_rsp;		/* [o] U<-->K trampoline stack */
	u_int64_t ci_user_cr3;		/* [o] U-K page table */

	/* bits for mitigating Micro-architectural Data Sampling */
	char		ci_mds_tmp[64];	/* [o] 64-byte aligned */
	void		*ci_mds_buf;	/* [I] */

	struct proc *ci_curproc;	/* [o] */
	struct schedstate_percpu ci_schedstate; /* scheduler state */

	struct pmap *ci_proc_pmap;	/* active, non-kernel pmap */
	struct pmap *ci_user_pmap;	/* [o] last pmap used in userspace */
	struct pcb *ci_curpcb;		/* [o] */
	struct pcb *ci_idle_pcb;	/* [o] */

	u_int	ci_pflags;		/* [o] */
#define CPUPF_USERSEGS		0x01	/* CPU has curproc's segs and FS.base */
#define CPUPF_USERXSTATE	0x02	/* CPU has curproc's xsave state */

	struct intrsource *ci_isources[MAX_INTR_SOURCES];
	u_int64_t	ci_ipending;
	int		ci_ilevel;
	int		ci_idepth;
	int		ci_handled_intr_level;
	u_int64_t	ci_imask[NIPL];
	u_int64_t	ci_iunmask[NIPL];
#ifdef DIAGNOSTIC
	int		ci_mutex_level;
#endif

	volatile u_int	ci_flags;	/* [a] */
	u_int32_t	ci_ipis;	/* [a] */

	enum cpu_vendor	ci_vendor;		/* [I] mapped from cpuid(0) */
	u_int32_t       ci_cpuid_level;         /* [I] cpuid(0).eax */
	u_int32_t	ci_feature_flags;	/* [I] */
	u_int32_t	ci_feature_eflags;	/* [I] */
	u_int32_t	ci_feature_sefflags_ebx;/* [I] */
	u_int32_t	ci_feature_sefflags_ecx;/* [I] */
	u_int32_t	ci_feature_sefflags_edx;/* [I] */
	u_int32_t	ci_feature_amdspec_ebx;	/* [I] */
	u_int32_t	ci_feature_amdsev_eax;	/* [I] */
	u_int32_t	ci_feature_amdsev_ebx;	/* [I] */
	u_int32_t	ci_feature_amdsev_ecx;	/* [I] */
	u_int32_t	ci_feature_amdsev_edx;	/* [I] */
	u_int32_t	ci_feature_tpmflags;	/* [I] */
	u_int32_t	ci_pnfeatset;		/* [I] */
	u_int32_t	ci_efeature_eax;	/* [I] */
	u_int32_t	ci_efeature_ecx;	/* [I] */
	u_int32_t	ci_brand[12];		/* [I] */
	u_int32_t	ci_signature;		/* [I] */
	u_int32_t	ci_family;		/* [I] */
	u_int32_t	ci_model;		/* [I] */
	u_int32_t	ci_cflushsz;		/* [I] */

	int		ci_inatomic;		/* [o] */

#define __HAVE_CPU_TOPOLOGY
	u_int32_t	ci_smt_id;		/* [I] */
	u_int32_t	ci_core_id;		/* [I] */
	u_int32_t	ci_pkg_id;		/* [I] */

	struct cpu_functions *ci_func;		/* [I] */
	void (*cpu_setup)(struct cpu_info *);	/* [I] */

	struct device	*ci_acpicpudev;		/* [I] */
	volatile u_int	ci_mwait;		/* [a] */
#define	MWAIT_IN_IDLE		0x1	/* don't need IPI to wake */
#define	MWAIT_KEEP_IDLING	0x2	/* cleared by other cpus to wake me */
#define	MWAIT_ONLY		0x4	/* set if all idle states use mwait */
#define	MWAIT_IDLING	(MWAIT_IN_IDLE | MWAIT_KEEP_IDLING)

	int		ci_want_resched;

	struct	x86_64_tss *ci_tss;		/* [o] */
	void		*ci_gdt;		/* [o] */

	volatile int	ci_ddb_paused;
#define CI_DDB_RUNNING		0
#define CI_DDB_SHOULDSTOP	1
#define CI_DDB_STOPPED		2
#define CI_DDB_ENTERDDB		3
#define CI_DDB_INDDB		4

#ifdef MULTIPROCESSOR
	struct srp_hazard	ci_srp_hazards[SRP_HAZARD_NUM];
#define __HAVE_UVM_PERCPU
	struct uvm_pmr_cache	ci_uvm;		/* [o] page cache */
#endif

	struct ksensordev	ci_sensordev;
	struct ksensor		ci_sensor;
	struct ksensor		ci_hz_sensor;
	u_int64_t		ci_hz_mperf;
	u_int64_t		ci_hz_aperf;
#if defined(GPROF) || defined(DDBPROF)
	struct gmonparam	*ci_gmon;
	struct clockintr	ci_gmonclock;
#endif
	u_int32_t	ci_vmm_flags;
#define	CI_VMM_VMX	(1 << 0)
#define	CI_VMM_SVM	(1 << 1)
#define	CI_VMM_RVI	(1 << 2)
#define	CI_VMM_EPT	(1 << 3)
#define	CI_VMM_DIS	(1 << 4)
	union		vmm_cpu_cap ci_vmm_cap;
	paddr_t		ci_vmxon_region_pa;
	struct vmxon_region *ci_vmxon_region;
	paddr_t		ci_vmcs_pa;
	struct rwlock	ci_vmcs_lock;
	struct pmap		*ci_ept_pmap;	/* [o] last used EPT pmap */
	struct vcpu		*ci_guest_vcpu;	/* [o] last vcpu resumed */

	char		ci_panicbuf[512];

	struct clockqueue ci_queue;
};

#define CPUF_BSP	0x0001		/* CPU is the original BSP */
#define CPUF_AP		0x0002		/* CPU is an AP */
#define CPUF_SP		0x0004		/* CPU is only processor */
#define CPUF_PRIMARY	0x0008		/* CPU is active primary processor */

#define CPUF_IDENTIFY	0x0010		/* CPU may now identify */
#define CPUF_IDENTIFIED	0x0020		/* CPU has been identified */

#define CPUF_CONST_TSC	0x0040		/* CPU has constant TSC */
#define CPUF_INVAR_TSC	0x0100		/* CPU has invariant TSC */

#define CPUF_PRESENT	0x1000		/* CPU is present */
#define CPUF_RUNNING	0x2000		/* CPU is running */
#define CPUF_PAUSE	0x4000		/* CPU is paused in DDB */
#define CPUF_GO		0x8000		/* CPU should start running */
#define CPUF_PARK	0x10000		/* CPU should self-park in real mode */
#define CPUF_VMM	0x20000		/* CPU is executing in VMM mode */

#define PROC_PC(p)	((p)->p_md.md_regs->tf_rip)
#define PROC_STACK(p)	((p)->p_md.md_regs->tf_rsp)

struct cpu_info_full;
extern struct cpu_info_full cpu_info_full_primary;
#define cpu_info_primary (*(struct cpu_info *)((char *)&cpu_info_full_primary + 4096*2 - offsetof(struct cpu_info, ci_PAGEALIGN)))

extern struct cpu_info *cpu_info_list;

#define CPU_INFO_ITERATOR		int
#define CPU_INFO_FOREACH(cii, ci)	for (cii = 0, ci = cpu_info_list; \
					    ci != NULL; ci = ci->ci_next)

#define CPU_INFO_UNIT(ci)	((ci)->ci_dev ? (ci)->ci_dev->dv_unit : 0)

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 */
extern void need_resched(struct cpu_info *);
#define clear_resched(ci) (ci)->ci_want_resched = 0

#if defined(MULTIPROCESSOR)

#define MAXCPUS		64	/* bitmask */

#define CPU_STARTUP(_ci)	((_ci)->ci_func->start(_ci))
#define CPU_STOP(_ci)		((_ci)->ci_func->stop(_ci))
#define CPU_START_CLEANUP(_ci)	((_ci)->ci_func->cleanup(_ci))

#define curcpu()	({struct cpu_info *__ci;                  \
			asm volatile("movq %%gs:%P1,%0" : "=r" (__ci) \
				:"n" (offsetof(struct cpu_info, ci_self))); \
			__ci;})
#define cpu_number()	(curcpu()->ci_cpuid)

#define CPU_IS_PRIMARY(ci)	((ci)->ci_flags & CPUF_PRIMARY)
#define CPU_IS_RUNNING(ci)	((ci)->ci_flags & CPUF_RUNNING)

extern struct cpu_info *cpu_info[MAXCPUS];

void cpu_boot_secondary_processors(void);

void cpu_kick(struct cpu_info *);
void cpu_unidle(struct cpu_info *);

#define CPU_BUSY_CYCLE()	__asm volatile("pause": : : "memory")

#else /* !MULTIPROCESSOR */

#define MAXCPUS		1

#ifdef _KERNEL
#define curcpu()		(&cpu_info_primary)

#define cpu_kick(ci)
#define cpu_unidle(ci)

#define CPU_BUSY_CYCLE()	__asm volatile ("" ::: "memory")

#endif

/*
 * definitions of cpu-dependent requirements
 * referenced in generic code
 */
#define	cpu_number()		0
#define CPU_IS_PRIMARY(ci)	1
#define CPU_IS_RUNNING(ci)	1

#endif	/* MULTIPROCESSOR */

#include <machine/cpufunc.h>
#include <machine/psl.h>

static inline unsigned int
cpu_rnd_messybits(void)
{
	unsigned int hi, lo;

	__asm volatile("rdtsc" : "=d" (hi), "=a" (lo));

	return (hi ^ lo);
}

#endif /* _KERNEL */

#ifdef MULTIPROCESSOR
#include <sys/mplock.h>
#endif

#define aston(p)	((p)->p_md.md_astpending = 1)

#define curpcb		curcpu()->ci_curpcb

/*
 * Arguments to hardclock, softclock and statclock
 * encapsulate the previous machine state in an opaque
 * clockframe; for now, use generic intrframe.
 */
#define clockframe intrframe

#define	CLKF_USERMODE(frame)	USERMODE((frame)->if_cs, (frame)->if_rflags)
#define CLKF_PC(frame)		((frame)->if_rip)
#define CLKF_INTR(frame)	(curcpu()->ci_idepth > 1)

/*
 * Give a profiling tick to the current process when the user profiling
 * buffer pages are invalid.  On the i386, request an ast to send us
 * through usertrap(), marking the proc as needing a profiling tick.
 */
#define	need_proftick(p)	aston(p)

void signotify(struct proc *);

/*
 * We need a machine-independent name for this.
 */
extern void (*delay_func)(int);
void delay_fini(void (*)(int));
void delay_init(void (*)(int), int);
struct timeval;

#define DELAY(x)		(*delay_func)(x)
#define delay(x)		(*delay_func)(x)


#ifdef _KERNEL
/* cpu.c */
extern int cpu_feature;
extern int cpu_ebxfeature;
extern int cpu_ecxfeature;
extern int ecpu_ecxfeature;
extern int cpu_sev_guestmode;
extern int cpu_id;
extern char cpu_vendor[];
extern int cpuid_level;
extern int cpu_meltdown;
extern u_int cpu_mwait_size;
extern u_int cpu_mwait_states;

int	cpu_suspend_primary(void);

/* cacheinfo.c */
void	x86_print_cacheinfo(struct cpu_info *);

/* identcpu.c */
void	identifycpu(struct cpu_info *);
int	cpu_amd64speed(int *);
extern int cpuspeed;
extern int amd64_pos_cbit;
extern int amd64_min_noes_asid;

/* machdep.c */
void	dumpconf(void);
void	cpu_set_vendor(struct cpu_info *, int _level, const char *_vendor);
void	cpu_reset(void);
void	x86_64_proc0_tss_ldt_init(void);
int	amd64_pa_used(paddr_t);
#define	cpu_idle_enter()	do { /* nothing */ } while (0)
extern void (*cpu_idle_cycle_fcn)(void);
extern void (*cpu_suspend_cycle_fcn)(void);
#define	cpu_idle_cycle()	(*cpu_idle_cycle_fcn)()
#define	cpu_idle_leave()	do { /* nothing */ } while (0)
extern void (*initclock_func)(void);
extern void (*startclock_func)(void);

struct region_descriptor;
void	lgdt(struct region_descriptor *);

struct pcb;
void	savectx(struct pcb *);
void	proc_trampoline(void);

/* clock.c */
void	startclocks(void);
void	rtcinit(void);
void	rtcstart(void);
void	rtcstop(void);
void	i8254_delay(int);
void	i8254_initclocks(void);
void	i8254_startclock(void);
void	i8254_start_both_clocks(void);
void	i8254_inittimecounter(void);
void	i8254_inittimecounter_simple(void);

/* i8259.c */
void	i8259_default_setup(void);

void cpu_init_msrs(struct cpu_info *);
void cpu_fix_msrs(struct cpu_info *);
void cpu_tsx_disable(struct cpu_info *);

/* dkcsum.c */
void	dkcsumattach(void);

/* bus_machdep.c */
void x86_bus_space_init(void);
void x86_bus_space_mallocok(void);

/* powernow-k8.c */
void k8_powernow_init(struct cpu_info *);
void k8_powernow_setperf(int);

/* k1x-pstate.c */
void k1x_init(struct cpu_info *);
void k1x_setperf(int);

void est_init(struct cpu_info *);
void est_setperf(int);

#ifdef MULTIPROCESSOR
/* mp_setperf.c */
void mp_setperf_init(void);
#endif

#endif /* _KERNEL */

/*
 * CTL_MACHDEP definitions.
 */
#define	CPU_CONSDEV		1	/* dev_t: console terminal device */
#define	CPU_BIOS		2	/* BIOS variables */
#define	CPU_BLK2CHR		3	/* convert blk maj into chr one */
#define	CPU_CHR2BLK		4	/* convert chr maj into blk one */
#define CPU_ALLOWAPERTURE	5	/* allow mmap of /dev/xf86 */
#define CPU_CPUVENDOR		6	/* cpuid vendor string */
#define CPU_CPUID		7	/* cpuid */
#define CPU_CPUFEATURE		8	/* cpuid features */
#define CPU_KBDRESET		10	/* keyboard reset under pcvt */
#define CPU_XCRYPT		12	/* supports VIA xcrypt in userland */
#define CPU_LIDACTION		14	/* action caused by lid close */
#define CPU_FORCEUKBD		15	/* Force ukbd(4) as console keyboard */
#define CPU_TSCFREQ		16	/* TSC frequency */
#define CPU_INVARIANTTSC	17	/* has invariant TSC */
#define CPU_PWRACTION		18	/* action caused by power button */
#define CPU_RETPOLINE		19	/* cpu requires retpoline pattern */
#define CPU_MAXID		20	/* number of valid machdep ids */

#define	CTL_MACHDEP_NAMES { \
	{ 0, 0 }, \
	{ "console_device", CTLTYPE_STRUCT }, \
	{ "bios", CTLTYPE_INT }, \
	{ "blk2chr", CTLTYPE_STRUCT }, \
	{ "chr2blk", CTLTYPE_STRUCT }, \
	{ "allowaperture", CTLTYPE_INT }, \
	{ "cpuvendor", CTLTYPE_STRING }, \
	{ "cpuid", CTLTYPE_INT }, \
	{ "cpufeature", CTLTYPE_INT }, \
	{ 0, 0 }, \
	{ "kbdreset", CTLTYPE_INT }, \
	{ 0, 0 }, \
	{ "xcrypt", CTLTYPE_INT }, \
	{ 0, 0 }, \
	{ "lidaction", CTLTYPE_INT }, \
	{ "forceukbd", CTLTYPE_INT }, \
	{ "tscfreq", CTLTYPE_QUAD }, \
	{ "invarianttsc", CTLTYPE_INT }, \
	{ "pwraction", CTLTYPE_INT }, \
	{ "retpoline", CTLTYPE_INT }, \
}

#endif /* !_MACHINE_CPU_H_ */
