/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2008, Joseph Koshy
 * Copyright (c) 2007 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by A. Joseph Koshy under
 * sponsorship from the FreeBSD Foundation and Google, Inc.
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

#ifndef _SYS_PMC_H_
#define	_SYS_PMC_H_

#include <dev/hwpmc/pmc_events.h>
#include <sys/proc.h>
#include <sys/counter.h>
#include <machine/pmc_mdep.h>
#include <machine/profile.h>
#ifdef _KERNEL
#include <sys/epoch.h>
#include <ck_queue.h>
#endif

#define	PMC_MODULE_NAME		"hwpmc"
#define	PMC_NAME_MAX		64 /* HW counter name size */
#define	PMC_CLASS_MAX		8  /* max #classes of PMCs per-system */

/*
 * Kernel<->userland API version number [MMmmpppp]
 *
 * Major numbers are to be incremented when an incompatible change to
 * the ABI occurs that older clients will not be able to handle.
 *
 * Minor numbers are incremented when a backwards compatible change
 * occurs that allows older correct programs to run unchanged.  For
 * example, when support for a new PMC type is added.
 *
 * The patch version is incremented for every bug fix.
 */
#define	PMC_VERSION_MAJOR	0x09
#define	PMC_VERSION_MINOR	0x03
#define	PMC_VERSION_PATCH	0x0000

#define	PMC_VERSION		(PMC_VERSION_MAJOR << 24 |		\
	PMC_VERSION_MINOR << 16 | PMC_VERSION_PATCH)

#define PMC_CPUID_LEN 64
/* cpu model name for pmu lookup */
extern char pmc_cpuid[PMC_CPUID_LEN];

/*
 * Kinds of CPUs known.
 *
 * We keep track of CPU variants that need to be distinguished in
 * some way for PMC operations.  CPU names are grouped by manufacturer
 * and numbered sparsely in order to minimize changes to the ABI involved
 * when new CPUs are added.
 */

#define	__PMC_CPUS()						\
	__PMC_CPU(AMD_K7,	0x00,	"AMD K7")		\
	__PMC_CPU(AMD_K8,	0x01,	"AMD K8")		\
	__PMC_CPU(INTEL_P5,	0x80,	"Intel Pentium")	\
	__PMC_CPU(INTEL_P6,	0x81,	"Intel Pentium Pro")	\
	__PMC_CPU(INTEL_CL,	0x82,	"Intel Celeron")	\
	__PMC_CPU(INTEL_PII,	0x83,	"Intel Pentium II")	\
	__PMC_CPU(INTEL_PIII,	0x84,	"Intel Pentium III")	\
	__PMC_CPU(INTEL_PM,	0x85,	"Intel Pentium M")	\
	__PMC_CPU(INTEL_PIV,	0x86,	"Intel Pentium IV")	\
	__PMC_CPU(INTEL_CORE,	0x87,	"Intel Core Solo/Duo")	\
	__PMC_CPU(INTEL_CORE2,	0x88,	"Intel Core2")		\
	__PMC_CPU(INTEL_CORE2EXTREME,	0x89,	"Intel Core2 Extreme")	\
	__PMC_CPU(INTEL_ATOM,	0x8A,	"Intel Atom")		\
	__PMC_CPU(INTEL_COREI7, 0x8B,   "Intel Core i7")	\
	__PMC_CPU(INTEL_WESTMERE, 0x8C,   "Intel Westmere")	\
	__PMC_CPU(INTEL_SANDYBRIDGE, 0x8D,   "Intel Sandy Bridge")	\
	__PMC_CPU(INTEL_IVYBRIDGE, 0x8E,   "Intel Ivy Bridge")	\
	__PMC_CPU(INTEL_SANDYBRIDGE_XEON, 0x8F,   "Intel Sandy Bridge Xeon")	\
	__PMC_CPU(INTEL_IVYBRIDGE_XEON, 0x90,   "Intel Ivy Bridge Xeon")	\
	__PMC_CPU(INTEL_HASWELL, 0x91,   "Intel Haswell")	\
	__PMC_CPU(INTEL_ATOM_SILVERMONT, 0x92,	"Intel Atom Silvermont")    \
	__PMC_CPU(INTEL_NEHALEM_EX, 0x93,   "Intel Nehalem Xeon 7500")	\
	__PMC_CPU(INTEL_WESTMERE_EX, 0x94,   "Intel Westmere Xeon E7")	\
	__PMC_CPU(INTEL_HASWELL_XEON, 0x95,   "Intel Haswell Xeon E5 v3") \
	__PMC_CPU(INTEL_BROADWELL, 0x96,   "Intel Broadwell") \
	__PMC_CPU(INTEL_BROADWELL_XEON, 0x97,   "Intel Broadwell Xeon") \
	__PMC_CPU(INTEL_SKYLAKE, 0x98,   "Intel Skylake")		\
	__PMC_CPU(INTEL_SKYLAKE_XEON, 0x99,   "Intel Skylake Xeon")	\
	__PMC_CPU(INTEL_XSCALE,	0x100,	"Intel XScale")		\
	__PMC_CPU(MIPS_24K,     0x200,  "MIPS 24K")		\
	__PMC_CPU(MIPS_OCTEON,  0x201,  "Cavium Octeon")	\
	__PMC_CPU(MIPS_74K,     0x202,  "MIPS 74K")		\
	__PMC_CPU(PPC_7450,     0x300,  "PowerPC MPC7450")	\
	__PMC_CPU(PPC_E500,     0x340,  "PowerPC e500 Core")	\
	__PMC_CPU(PPC_970,      0x380,  "IBM PowerPC 970")	\
	__PMC_CPU(GENERIC, 	0x400,  "Generic")		\
	__PMC_CPU(ARMV7_CORTEX_A5,	0x500,	"ARMv7 Cortex A5")	\
	__PMC_CPU(ARMV7_CORTEX_A7,	0x501,	"ARMv7 Cortex A7")	\
	__PMC_CPU(ARMV7_CORTEX_A8,	0x502,	"ARMv7 Cortex A8")	\
	__PMC_CPU(ARMV7_CORTEX_A9,	0x503,	"ARMv7 Cortex A9")	\
	__PMC_CPU(ARMV7_CORTEX_A15,	0x504,	"ARMv7 Cortex A15")	\
	__PMC_CPU(ARMV7_CORTEX_A17,	0x505,	"ARMv7 Cortex A17")	\
	__PMC_CPU(ARMV8_CORTEX_A53,	0x600,	"ARMv8 Cortex A53")	\
	__PMC_CPU(ARMV8_CORTEX_A57,	0x601,	"ARMv8 Cortex A57")

enum pmc_cputype {
#undef	__PMC_CPU
#define	__PMC_CPU(S,V,D)	PMC_CPU_##S = V,
	__PMC_CPUS()
};

#define	PMC_CPU_FIRST	PMC_CPU_AMD_K7
#define	PMC_CPU_LAST	PMC_CPU_GENERIC

/*
 * Classes of PMCs
 */

#define	__PMC_CLASSES()							\
	__PMC_CLASS(TSC,	0x00,	"CPU Timestamp counter")	\
	__PMC_CLASS(K7,		0x01,	"AMD K7 performance counters")	\
	__PMC_CLASS(K8,		0x02,	"AMD K8 performance counters")	\
	__PMC_CLASS(P5,		0x03,	"Intel Pentium counters")	\
	__PMC_CLASS(P6,		0x04,	"Intel Pentium Pro counters")	\
	__PMC_CLASS(P4,		0x05,	"Intel Pentium-IV counters")	\
	__PMC_CLASS(IAF,	0x06,	"Intel Core2/Atom, fixed function") \
	__PMC_CLASS(IAP,	0x07,	"Intel Core...Atom, programmable") \
	__PMC_CLASS(UCF,	0x08,	"Intel Uncore fixed function")	\
	__PMC_CLASS(UCP,	0x09,	"Intel Uncore programmable")	\
	__PMC_CLASS(XSCALE,	0x0A,	"Intel XScale counters")	\
	__PMC_CLASS(MIPS24K,	0x0B,	"MIPS 24K")			\
	__PMC_CLASS(OCTEON,	0x0C,	"Cavium Octeon")		\
	__PMC_CLASS(PPC7450,	0x0D,	"Motorola MPC7450 class")	\
	__PMC_CLASS(PPC970,	0x0E,	"IBM PowerPC 970 class")	\
	__PMC_CLASS(SOFT,	0x0F,	"Software events")		\
	__PMC_CLASS(ARMV7,	0x10,	"ARMv7")			\
	__PMC_CLASS(ARMV8,	0x11,	"ARMv8")			\
	__PMC_CLASS(MIPS74K,	0x12,	"MIPS 74K")			\
	__PMC_CLASS(E500,	0x13,	"Freescale e500 class")

enum pmc_class {
#undef  __PMC_CLASS
#define	__PMC_CLASS(S,V,D)	PMC_CLASS_##S = V,
	__PMC_CLASSES()
};

#define	PMC_CLASS_FIRST	PMC_CLASS_TSC
#define	PMC_CLASS_LAST	PMC_CLASS_E500

/*
 * A PMC can be in the following states:
 *
 * Hardware states:
 *   DISABLED   -- administratively prohibited from being used.
 *   FREE       -- HW available for use
 * Software states:
 *   ALLOCATED  -- allocated
 *   STOPPED    -- allocated, but not counting events
 *   RUNNING    -- allocated, and in operation; 'pm_runcount'
 *                 holds the number of CPUs using this PMC at
 *                 a given instant
 *   DELETED    -- being destroyed
 */

#define	__PMC_HWSTATES()			\
	__PMC_STATE(DISABLED)			\
	__PMC_STATE(FREE)

#define	__PMC_SWSTATES()			\
	__PMC_STATE(ALLOCATED)			\
	__PMC_STATE(STOPPED)			\
	__PMC_STATE(RUNNING)			\
	__PMC_STATE(DELETED)

#define	__PMC_STATES()				\
	__PMC_HWSTATES()			\
	__PMC_SWSTATES()

enum pmc_state {
#undef	__PMC_STATE
#define	__PMC_STATE(S)	PMC_STATE_##S,
	__PMC_STATES()
	__PMC_STATE(MAX)
};

#define	PMC_STATE_FIRST	PMC_STATE_DISABLED
#define	PMC_STATE_LAST	PMC_STATE_DELETED

/*
 * An allocated PMC may used as a 'global' counter or as a
 * 'thread-private' one.  Each such mode of use can be in either
 * statistical sampling mode or in counting mode.  Thus a PMC in use
 *
 * SS i.e., SYSTEM STATISTICAL  -- system-wide statistical profiling
 * SC i.e., SYSTEM COUNTER      -- system-wide counting mode
 * TS i.e., THREAD STATISTICAL  -- thread virtual, statistical profiling
 * TC i.e., THREAD COUNTER      -- thread virtual, counting mode
 *
 * Statistical profiling modes rely on the PMC periodically delivering
 * a interrupt to the CPU (when the configured number of events have
 * been measured), so the PMC must have the ability to generate
 * interrupts.
 *
 * In counting modes, the PMC counts its configured events, with the
 * value of the PMC being read whenever needed by its owner process.
 *
 * The thread specific modes "virtualize" the PMCs -- the PMCs appear
 * to be thread private and count events only when the profiled thread
 * actually executes on the CPU.
 *
 * The system-wide "global" modes keep the PMCs running all the time
 * and are used to measure the behaviour of the whole system.
 */

#define	__PMC_MODES()				\
	__PMC_MODE(SS,	0)			\
	__PMC_MODE(SC,	1)			\
	__PMC_MODE(TS,	2)			\
	__PMC_MODE(TC,	3)

enum pmc_mode {
#undef	__PMC_MODE
#define	__PMC_MODE(M,N)	PMC_MODE_##M = N,
	__PMC_MODES()
};

#define	PMC_MODE_FIRST	PMC_MODE_SS
#define	PMC_MODE_LAST	PMC_MODE_TC

#define	PMC_IS_COUNTING_MODE(mode)				\
	((mode) == PMC_MODE_SC || (mode) == PMC_MODE_TC)
#define	PMC_IS_SYSTEM_MODE(mode)				\
	((mode) == PMC_MODE_SS || (mode) == PMC_MODE_SC)
#define	PMC_IS_SAMPLING_MODE(mode)				\
	((mode) == PMC_MODE_SS || (mode) == PMC_MODE_TS)
#define	PMC_IS_VIRTUAL_MODE(mode)				\
	((mode) == PMC_MODE_TS || (mode) == PMC_MODE_TC)

/*
 * PMC row disposition
 */

#define	__PMC_DISPOSITIONS(N)					\
	__PMC_DISP(STANDALONE)	/* global/disabled counters */	\
	__PMC_DISP(FREE)	/* free/available */		\
	__PMC_DISP(THREAD)	/* thread-virtual PMCs */	\
	__PMC_DISP(UNKNOWN)	/* sentinel */

enum pmc_disp {
#undef	__PMC_DISP
#define	__PMC_DISP(D)	PMC_DISP_##D ,
	__PMC_DISPOSITIONS()
};

#define	PMC_DISP_FIRST	PMC_DISP_STANDALONE
#define	PMC_DISP_LAST	PMC_DISP_THREAD

/*
 * Counter capabilities
 *
 * __PMC_CAPS(NAME, VALUE, DESCRIPTION)
 */

#define	__PMC_CAPS()							\
	__PMC_CAP(INTERRUPT,	0, "generate interrupts")		\
	__PMC_CAP(USER,		1, "count user-mode events")		\
	__PMC_CAP(SYSTEM,	2, "count system-mode events")		\
	__PMC_CAP(EDGE,		3, "do edge detection of events")	\
	__PMC_CAP(THRESHOLD,	4, "ignore events below a threshold")	\
	__PMC_CAP(READ,		5, "read PMC counter")			\
	__PMC_CAP(WRITE,	6, "reprogram PMC counter")		\
	__PMC_CAP(INVERT,	7, "invert comparison sense")		\
	__PMC_CAP(QUALIFIER,	8, "further qualify monitored events")	\
	__PMC_CAP(PRECISE,	9, "perform precise sampling")		\
	__PMC_CAP(TAGGING,	10, "tag upstream events")		\
	__PMC_CAP(CASCADE,	11, "cascade counters")

enum pmc_caps
{
#undef	__PMC_CAP
#define	__PMC_CAP(NAME, VALUE, DESCR)	PMC_CAP_##NAME = (1 << VALUE) ,
	__PMC_CAPS()
};

#define	PMC_CAP_FIRST		PMC_CAP_INTERRUPT
#define	PMC_CAP_LAST		PMC_CAP_CASCADE

/*
 * PMC Event Numbers
 *
 * These are generated from the definitions in "dev/hwpmc/pmc_events.h".
 */

enum pmc_event {
#undef	__PMC_EV
#undef	__PMC_EV_BLOCK
#define	__PMC_EV_BLOCK(C,V)	PMC_EV_ ## C ## __BLOCK_START = (V) - 1 ,
#define	__PMC_EV(C,N)		PMC_EV_ ## C ## _ ## N ,
	__PMC_EVENTS()
};

/*
 * PMC SYSCALL INTERFACE
 */

/*
 * "PMC_OPS" -- these are the commands recognized by the kernel
 * module, and are used when performing a system call from userland.
 */
#define	__PMC_OPS()							\
	__PMC_OP(CONFIGURELOG, "Set log file")				\
	__PMC_OP(FLUSHLOG, "Flush log file")				\
	__PMC_OP(GETCPUINFO, "Get system CPU information")		\
	__PMC_OP(GETDRIVERSTATS, "Get driver statistics")		\
	__PMC_OP(GETMODULEVERSION, "Get module version")		\
	__PMC_OP(GETPMCINFO, "Get per-cpu PMC information")		\
	__PMC_OP(PMCADMIN, "Set PMC state")				\
	__PMC_OP(PMCALLOCATE, "Allocate and configure a PMC")		\
	__PMC_OP(PMCATTACH, "Attach a PMC to a process")		\
	__PMC_OP(PMCDETACH, "Detach a PMC from a process")		\
	__PMC_OP(PMCGETMSR, "Get a PMC's hardware address")		\
	__PMC_OP(PMCRELEASE, "Release a PMC")				\
	__PMC_OP(PMCRW, "Read/Set a PMC")				\
	__PMC_OP(PMCSETCOUNT, "Set initial count/sampling rate")	\
	__PMC_OP(PMCSTART, "Start a PMC")				\
	__PMC_OP(PMCSTOP, "Stop a PMC")					\
	__PMC_OP(WRITELOG, "Write a cookie to the log file")		\
	__PMC_OP(CLOSELOG, "Close log file")				\
	__PMC_OP(GETDYNEVENTINFO, "Get dynamic events list")


enum pmc_ops {
#undef	__PMC_OP
#define	__PMC_OP(N, D)	PMC_OP_##N,
	__PMC_OPS()
};


/*
 * Flags used in operations on PMCs.
 */

#define	PMC_F_UNUSED1		0x00000001 /* unused */
#define	PMC_F_DESCENDANTS	0x00000002 /*OP ALLOCATE track descendants */
#define	PMC_F_LOG_PROCCSW	0x00000004 /*OP ALLOCATE track ctx switches */
#define	PMC_F_LOG_PROCEXIT	0x00000008 /*OP ALLOCATE log proc exits */
#define	PMC_F_NEWVALUE		0x00000010 /*OP RW write new value */
#define	PMC_F_OLDVALUE		0x00000020 /*OP RW get old value */

/* V2 API */
#define	PMC_F_CALLCHAIN		0x00000080 /*OP ALLOCATE capture callchains */
#define	PMC_F_USERCALLCHAIN	0x00000100 /*OP ALLOCATE use userspace stack */

/* internal flags */
#define	PMC_F_ATTACHED_TO_OWNER	0x00010000 /*attached to owner*/
#define	PMC_F_NEEDS_LOGFILE	0x00020000 /*needs log file */
#define	PMC_F_ATTACH_DONE	0x00040000 /*attached at least once */

#define	PMC_CALLCHAIN_DEPTH_MAX	512

#define	PMC_CC_F_USERSPACE	0x01	   /*userspace callchain*/

/*
 * Cookies used to denote allocated PMCs, and the values of PMCs.
 */

typedef uint32_t	pmc_id_t;
typedef uint64_t	pmc_value_t;

#define	PMC_ID_INVALID		(~ (pmc_id_t) 0)

/*
 * PMC IDs have the following format:
 *
 * +-----------------------+-------+-----------+
 * |   CPU      | PMC MODE | CLASS | ROW INDEX |
 * +-----------------------+-------+-----------+
 *
 * where CPU is 12 bits, MODE 8, CLASS 4, and ROW INDEX 8  Field 'CPU'
 * is set to the requested CPU for system-wide PMCs or PMC_CPU_ANY for
 * process-mode PMCs.  Field 'PMC MODE' is the allocated PMC mode.
 * Field 'PMC CLASS' is the class of the PMC.  Field 'ROW INDEX' is the
 * row index for the PMC.
 *
 * The 'ROW INDEX' ranges over 0..NWPMCS where NHWPMCS is the total
 * number of hardware PMCs on this cpu.
 */


#define	PMC_ID_TO_ROWINDEX(ID)	((ID) & 0xFF)
#define	PMC_ID_TO_CLASS(ID)	(((ID) & 0xF00) >> 8)
#define	PMC_ID_TO_MODE(ID)	(((ID) & 0xFF000) >> 12)
#define	PMC_ID_TO_CPU(ID)	(((ID) & 0xFFF00000) >> 20)
#define	PMC_ID_MAKE_ID(CPU,MODE,CLASS,ROWINDEX)			\
	((((CPU) & 0xFFF) << 20) | (((MODE) & 0xFF) << 12) |	\
	(((CLASS) & 0xF) << 8) | ((ROWINDEX) & 0xFF))

/*
 * Data structures for system calls supported by the pmc driver.
 */

/*
 * OP PMCALLOCATE
 *
 * Allocate a PMC on the named CPU.
 */

#define	PMC_CPU_ANY	~0

struct pmc_op_pmcallocate {
	uint32_t	pm_caps;	/* PMC_CAP_* */
	uint32_t	pm_cpu;		/* CPU number or PMC_CPU_ANY */
	enum pmc_class	pm_class;	/* class of PMC desired */
	enum pmc_event	pm_ev;		/* [enum pmc_event] desired */
	uint32_t	pm_flags;	/* additional modifiers PMC_F_* */
	enum pmc_mode	pm_mode;	/* desired mode */
	pmc_id_t	pm_pmcid;	/* [return] process pmc id */
	pmc_value_t	pm_count;	/* initial/sample count */

	union pmc_md_op_pmcallocate pm_md; /* MD layer extensions */
};

/*
 * OP PMCADMIN
 *
 * Set the administrative state (i.e., whether enabled or disabled) of
 * a PMC 'pm_pmc' on CPU 'pm_cpu'.  Note that 'pm_pmc' specifies an
 * absolute PMC number and need not have been first allocated by the
 * calling process.
 */

struct pmc_op_pmcadmin {
	int		pm_cpu;		/* CPU# */
	uint32_t	pm_flags;	/* flags */
	int		pm_pmc;         /* PMC# */
	enum pmc_state  pm_state;	/* desired state */
};

/*
 * OP PMCATTACH / OP PMCDETACH
 *
 * Attach/detach a PMC and a process.
 */

struct pmc_op_pmcattach {
	pmc_id_t	pm_pmc;		/* PMC to attach to */
	pid_t		pm_pid;		/* target process */
};

/*
 * OP PMCSETCOUNT
 *
 * Set the sampling rate (i.e., the reload count) for statistical counters.
 * 'pm_pmcid' need to have been previously allocated using PMCALLOCATE.
 */

struct pmc_op_pmcsetcount {
	pmc_value_t	pm_count;	/* initial/sample count */
	pmc_id_t	pm_pmcid;	/* PMC id to set */
};


/*
 * OP PMCRW
 *
 * Read the value of a PMC named by 'pm_pmcid'.  'pm_pmcid' needs
 * to have been previously allocated using PMCALLOCATE.
 */


struct pmc_op_pmcrw {
	uint32_t	pm_flags;	/* PMC_F_{OLD,NEW}VALUE*/
	pmc_id_t	pm_pmcid;	/* pmc id */
	pmc_value_t	pm_value;	/* new&returned value */
};


/*
 * OP GETPMCINFO
 *
 * retrieve PMC state for a named CPU.  The caller is expected to
 * allocate 'npmc' * 'struct pmc_info' bytes of space for the return
 * values.
 */

struct pmc_info {
	char		pm_name[PMC_NAME_MAX]; /* pmc name */
	enum pmc_class	pm_class;	/* enum pmc_class */
	int		pm_enabled;	/* whether enabled */
	enum pmc_disp	pm_rowdisp;	/* FREE, THREAD or STANDLONE */
	pid_t		pm_ownerpid;	/* owner, or -1 */
	enum pmc_mode	pm_mode;	/* current mode [enum pmc_mode] */
	enum pmc_event	pm_event;	/* current event */
	uint32_t	pm_flags;	/* current flags */
	pmc_value_t	pm_reloadcount;	/* sampling counters only */
};

struct pmc_op_getpmcinfo {
	int32_t		pm_cpu;		/* 0 <= cpu < mp_maxid */
	struct pmc_info	pm_pmcs[];	/* space for 'npmc' structures */
};


/*
 * OP GETCPUINFO
 *
 * Retrieve system CPU information.
 */


struct pmc_classinfo {
	enum pmc_class	pm_class;	/* class id */
	uint32_t	pm_caps;	/* counter capabilities */
	uint32_t	pm_width;	/* width of the PMC */
	uint32_t	pm_num;		/* number of PMCs in class */
};

struct pmc_op_getcpuinfo {
	enum pmc_cputype pm_cputype; /* what kind of CPU */
	uint32_t	pm_ncpu;    /* max CPU number */
	uint32_t	pm_npmc;    /* #PMCs per CPU */
	uint32_t	pm_nclass;  /* #classes of PMCs */
	struct pmc_classinfo  pm_classes[PMC_CLASS_MAX];
};

/*
 * OP CONFIGURELOG
 *
 * Configure a log file for writing system-wide statistics to.
 */

struct pmc_op_configurelog {
	int		pm_flags;
	int		pm_logfd;   /* logfile fd (or -1) */
};

/*
 * OP GETDRIVERSTATS
 *
 * Retrieve pmc(4) driver-wide statistics.
 */
#ifdef _KERNEL
struct pmc_driverstats {
	counter_u64_t	pm_intr_ignored;	/* #interrupts ignored */
	counter_u64_t	pm_intr_processed;	/* #interrupts processed */
	counter_u64_t	pm_intr_bufferfull;	/* #interrupts with ENOSPC */
	counter_u64_t	pm_syscalls;		/* #syscalls */
	counter_u64_t	pm_syscall_errors;	/* #syscalls with errors */
	counter_u64_t	pm_buffer_requests;	/* #buffer requests */
	counter_u64_t	pm_buffer_requests_failed; /* #failed buffer requests */
	counter_u64_t	pm_log_sweeps;		/* #sample buffer processing
						   passes */
	counter_u64_t	pm_merges;		/* merged k+u */
	counter_u64_t	pm_overwrites;		/* UR overwrites */
};
#endif

struct pmc_op_getdriverstats {
	unsigned int	pm_intr_ignored;	/* #interrupts ignored */
	unsigned int	pm_intr_processed;	/* #interrupts processed */
	unsigned int	pm_intr_bufferfull;	/* #interrupts with ENOSPC */
	unsigned int	pm_syscalls;		/* #syscalls */
	unsigned int	pm_syscall_errors;	/* #syscalls with errors */
	unsigned int	pm_buffer_requests;	/* #buffer requests */
	unsigned int	pm_buffer_requests_failed; /* #failed buffer requests */
	unsigned int	pm_log_sweeps;		/* #sample buffer processing
						   passes */
};

/*
 * OP RELEASE / OP START / OP STOP
 *
 * Simple operations on a PMC id.
 */

struct pmc_op_simple {
	pmc_id_t	pm_pmcid;
};

/*
 * OP WRITELOG
 *
 * Flush the current log buffer and write 4 bytes of user data to it.
 */

struct pmc_op_writelog {
	uint32_t	pm_userdata;
};

/*
 * OP GETMSR
 *
 * Retrieve the machine specific address associated with the allocated
 * PMC.  This number can be used subsequently with a read-performance-counter
 * instruction.
 */

struct pmc_op_getmsr {
	uint32_t	pm_msr;		/* machine specific address */
	pmc_id_t	pm_pmcid;	/* allocated pmc id */
};

/*
 * OP GETDYNEVENTINFO
 *
 * Retrieve a PMC dynamic class events list.
 */

struct pmc_dyn_event_descr {
	char		pm_ev_name[PMC_NAME_MAX];
	enum pmc_event	pm_ev_code;
};

struct pmc_op_getdyneventinfo {
	enum pmc_class			pm_class;
	unsigned int			pm_nevent;
	struct pmc_dyn_event_descr	pm_events[PMC_EV_DYN_COUNT];
};

#ifdef _KERNEL

#include <sys/malloc.h>
#include <sys/sysctl.h>
#include <sys/_cpuset.h>

#include <machine/frame.h>

#define	PMC_HASH_SIZE				1024
#define	PMC_MTXPOOL_SIZE			2048
#define	PMC_LOG_BUFFER_SIZE			256
#define	PMC_NLOGBUFFERS_PCPU			32
#define	PMC_NSAMPLES				256
#define	PMC_CALLCHAIN_DEPTH			128
#define	PMC_THREADLIST_MAX			128

#define PMC_SYSCTL_NAME_PREFIX "kern." PMC_MODULE_NAME "."

/*
 * Locking keys
 *
 * (b) - pmc_bufferlist_mtx (spin lock)
 * (k) - pmc_kthread_mtx (sleep lock)
 * (o) - po->po_mtx (spin lock)
 * (g) - global_epoch_preempt (epoch)
 * (p) - pmc_sx (sx)
 */

/*
 * PMC commands
 */

struct pmc_syscall_args {
	register_t	pmop_code;	/* one of PMC_OP_* */
	void		*pmop_data;	/* syscall parameter */
};

/*
 * Interface to processor specific s1tuff
 */

/*
 * struct pmc_descr
 *
 * Machine independent (i.e., the common parts) of a human readable
 * PMC description.
 */

struct pmc_descr {
	char		pd_name[PMC_NAME_MAX]; /* name */
	uint32_t	pd_caps;	/* capabilities */
	enum pmc_class	pd_class;	/* class of the PMC */
	uint32_t	pd_width;	/* width in bits */
};

/*
 * struct pmc_target
 *
 * This structure records all the target processes associated with a
 * PMC.
 */

struct pmc_target {
	LIST_ENTRY(pmc_target)	pt_next;
	struct pmc_process	*pt_process; /* target descriptor */
};

/*
 * struct pmc
 *
 * Describes each allocated PMC.
 *
 * Each PMC has precisely one owner, namely the process that allocated
 * the PMC.
 *
 * A PMC may be attached to multiple target processes.  The
 * 'pm_targets' field links all the target processes being monitored
 * by this PMC.
 *
 * The 'pm_savedvalue' field is protected by a mutex.
 *
 * On a multi-cpu machine, multiple target threads associated with a
 * process-virtual PMC could be concurrently executing on different
 * CPUs.  The 'pm_runcount' field is atomically incremented every time
 * the PMC gets scheduled on a CPU and atomically decremented when it
 * get descheduled.  Deletion of a PMC is only permitted when this
 * field is '0'.
 *
 */
struct pmc_pcpu_state {
	uint8_t pps_stalled;
	uint8_t pps_cpustate;
} __aligned(CACHE_LINE_SIZE);
struct pmc {
	LIST_HEAD(,pmc_target)	pm_targets;	/* list of target processes */
	LIST_ENTRY(pmc)		pm_next;	/* owner's list */

	/*
	 * System-wide PMCs are allocated on a CPU and are not moved
	 * around.  For system-wide PMCs we record the CPU the PMC was
	 * allocated on in the 'CPU' field of the pmc ID.
	 *
	 * Virtual PMCs run on whichever CPU is currently executing
	 * their targets' threads.  For these PMCs we need to save
	 * their current PMC counter values when they are taken off
	 * CPU.
	 */

	union {
		pmc_value_t	pm_savedvalue;	/* Virtual PMCS */
	} pm_gv;

	/*
	 * For sampling mode PMCs, we keep track of the PMC's "reload
	 * count", which is the counter value to be loaded in when
	 * arming the PMC for the next counting session.  For counting
	 * modes on PMCs that are read-only (e.g., the x86 TSC), we
	 * keep track of the initial value at the start of
	 * counting-mode operation.
	 */

	union {
		pmc_value_t	pm_reloadcount;	/* sampling PMC modes */
		pmc_value_t	pm_initial;	/* counting PMC modes */
	} pm_sc;

	struct pmc_pcpu_state *pm_pcpu_state;
	volatile cpuset_t pm_cpustate;	/* CPUs where PMC should be active */
	uint32_t	pm_caps;	/* PMC capabilities */
	enum pmc_event	pm_event;	/* event being measured */
	uint32_t	pm_flags;	/* additional flags PMC_F_... */
	struct pmc_owner *pm_owner;	/* owner thread state */
	counter_u64_t		pm_runcount;	/* #cpus currently on */
	enum pmc_state	pm_state;	/* current PMC state */
	uint32_t	pm_overflowcnt;	/* count overflow interrupts */

	/*
	 * The PMC ID field encodes the row-index for the PMC, its
	 * mode, class and the CPU# associated with the PMC.
	 */

	pmc_id_t	pm_id;		/* allocated PMC id */
	enum pmc_class pm_class;

	/* md extensions */
	union pmc_md_pmc	pm_md;
};

/*
 * Accessor macros for 'struct pmc'
 */

#define	PMC_TO_MODE(P)		PMC_ID_TO_MODE((P)->pm_id)
#define	PMC_TO_CLASS(P)		PMC_ID_TO_CLASS((P)->pm_id)
#define	PMC_TO_ROWINDEX(P)	PMC_ID_TO_ROWINDEX((P)->pm_id)
#define	PMC_TO_CPU(P)		PMC_ID_TO_CPU((P)->pm_id)

/*
 * struct pmc_threadpmcstate
 *
 * Record per-PMC, per-thread state.
 */
struct pmc_threadpmcstate {
	pmc_value_t	pt_pmcval;	/* per-thread reload count */
};

/*
 * struct pmc_thread
 *
 * Record a 'target' thread being profiled.
 */
struct pmc_thread {
	LIST_ENTRY(pmc_thread) pt_next;		/* linked list */
	struct thread	*pt_td;			/* target thread */
	struct pmc_threadpmcstate pt_pmcs[];	/* per-PMC state */
};

/*
 * struct pmc_process
 *
 * Record a 'target' process being profiled.
 *
 * The target process being profiled could be different from the owner
 * process which allocated the PMCs.  Each target process descriptor
 * is associated with NHWPMC 'struct pmc *' pointers.  Each PMC at a
 * given hardware row-index 'n' will use slot 'n' of the 'pp_pmcs[]'
 * array.  The size of this structure is thus PMC architecture
 * dependent.
 *
 */

struct pmc_targetstate {
	struct pmc	*pp_pmc;   /* target PMC */
	pmc_value_t	pp_pmcval; /* per-process value */
};

struct pmc_process {
	LIST_ENTRY(pmc_process) pp_next;	/* hash chain */
	LIST_HEAD(,pmc_thread) pp_tds;		/* list of threads */
	struct mtx	*pp_tdslock;		/* lock on pp_tds thread list */
	int		pp_refcnt;		/* reference count */
	uint32_t	pp_flags;		/* flags PMC_PP_* */
	struct proc	*pp_proc;		/* target process */
	struct pmc_targetstate pp_pmcs[];       /* NHWPMCs */
};

#define	PMC_PP_ENABLE_MSR_ACCESS	0x00000001

/*
 * struct pmc_owner
 *
 * We associate a PMC with an 'owner' process.
 *
 * A process can be associated with 0..NCPUS*NHWPMC PMCs during its
 * lifetime, where NCPUS is the numbers of CPUS in the system and
 * NHWPMC is the number of hardware PMCs per CPU.  These are
 * maintained in the list headed by the 'po_pmcs' to save on space.
 *
 */

struct pmc_owner  {
	LIST_ENTRY(pmc_owner)	po_next;	/* hash chain */
	CK_LIST_ENTRY(pmc_owner)	po_ssnext;	/* (g/p) list of SS PMC owners */
	LIST_HEAD(, pmc)	po_pmcs;	/* owned PMC list */
	TAILQ_HEAD(, pmclog_buffer) po_logbuffers; /* (o) logbuffer list */
	struct mtx		po_mtx;		/* spin lock for (o) */
	struct proc		*po_owner;	/* owner proc */
	uint32_t		po_flags;	/* (k) flags PMC_PO_* */
	struct proc		*po_kthread;	/* (k) helper kthread */
	struct file		*po_file;	/* file reference */
	int			po_error;	/* recorded error */
	short			po_sscount;	/* # SS PMCs owned */
	short			po_logprocmaps;	/* global mappings done */
	struct pmclog_buffer	*po_curbuf[MAXCPU];	/* current log buffer */
};

#define	PMC_PO_OWNS_LOGFILE		0x00000001 /* has a log file */
#define	PMC_PO_SHUTDOWN			0x00000010 /* in the process of shutdown */
#define	PMC_PO_INITIAL_MAPPINGS_DONE	0x00000020

/*
 * struct pmc_hw -- describe the state of the PMC hardware
 *
 * When in use, a HW PMC is associated with one allocated 'struct pmc'
 * pointed to by field 'phw_pmc'.  When inactive, this field is NULL.
 *
 * On an SMP box, one or more HW PMC's in process virtual mode with
 * the same 'phw_pmc' could be executing on different CPUs.  In order
 * to handle this case correctly, we need to ensure that only
 * incremental counts get added to the saved value in the associated
 * 'struct pmc'.  The 'phw_save' field is used to keep the saved PMC
 * value at the time the hardware is started during this context
 * switch (i.e., the difference between the new (hardware) count and
 * the saved count is atomically added to the count field in 'struct
 * pmc' at context switch time).
 *
 */

struct pmc_hw {
	uint32_t	phw_state;	/* see PHW_* macros below */
	struct pmc	*phw_pmc;	/* current thread PMC */
};

#define	PMC_PHW_RI_MASK		0x000000FF
#define	PMC_PHW_CPU_SHIFT	8
#define	PMC_PHW_CPU_MASK	0x0000FF00
#define	PMC_PHW_FLAGS_SHIFT	16
#define	PMC_PHW_FLAGS_MASK	0xFFFF0000

#define	PMC_PHW_INDEX_TO_STATE(ri)	((ri) & PMC_PHW_RI_MASK)
#define	PMC_PHW_STATE_TO_INDEX(state)	((state) & PMC_PHW_RI_MASK)
#define	PMC_PHW_CPU_TO_STATE(cpu)	(((cpu) << PMC_PHW_CPU_SHIFT) & \
	PMC_PHW_CPU_MASK)
#define	PMC_PHW_STATE_TO_CPU(state)	(((state) & PMC_PHW_CPU_MASK) >> \
	PMC_PHW_CPU_SHIFT)
#define	PMC_PHW_FLAGS_TO_STATE(flags)	(((flags) << PMC_PHW_FLAGS_SHIFT) & \
	PMC_PHW_FLAGS_MASK)
#define	PMC_PHW_STATE_TO_FLAGS(state)	(((state) & PMC_PHW_FLAGS_MASK) >> \
	PMC_PHW_FLAGS_SHIFT)
#define	PMC_PHW_FLAG_IS_ENABLED		(PMC_PHW_FLAGS_TO_STATE(0x01))
#define	PMC_PHW_FLAG_IS_SHAREABLE	(PMC_PHW_FLAGS_TO_STATE(0x02))

/*
 * struct pmc_sample
 *
 * Space for N (tunable) PC samples and associated control data.
 */

struct pmc_sample {
	uint16_t		ps_nsamples;	/* callchain depth */
	uint16_t		ps_nsamples_actual;
	uint16_t		ps_cpu;		/* cpu number */
	uint16_t		ps_flags;	/* other flags */
	lwpid_t			ps_tid;		/* thread id */
	pid_t			ps_pid;		/* process PID or -1 */
	int		ps_ticks; /* ticks at sample time */
	/* pad */
	struct thread		*ps_td;		/* which thread */
	struct pmc		*ps_pmc;	/* interrupting PMC */
	uintptr_t		*ps_pc;		/* (const) callchain start */
	uint64_t		ps_tsc;		/* tsc value */
};

#define 	PMC_SAMPLE_FREE		((uint16_t) 0)
#define 	PMC_USER_CALLCHAIN_PENDING	((uint16_t) 0xFFFF)

struct pmc_samplebuffer {
	volatile uint64_t		ps_prodidx; /* producer index */
	volatile uint64_t		ps_considx; /* consumer index */
	uintptr_t		*ps_callchains;	/* all saved call chains */
	struct pmc_sample	ps_samples[];	/* array of sample entries */
};

#define PMC_CONS_SAMPLE(psb)					\
	(&(psb)->ps_samples[(psb)->ps_considx & pmc_sample_mask])

#define PMC_CONS_SAMPLE_OFF(psb, off)							\
	(&(psb)->ps_samples[(off) & pmc_sample_mask])

#define PMC_PROD_SAMPLE(psb)					\
	(&(psb)->ps_samples[(psb)->ps_prodidx & pmc_sample_mask])

/*
 * struct pmc_cpustate
 *
 * A CPU is modelled as a collection of HW PMCs with space for additional
 * flags.
 */

struct pmc_cpu {
	uint32_t	pc_state;	/* physical cpu number + flags */
	struct pmc_samplebuffer *pc_sb[3]; /* space for samples */
	struct pmc_hw	*pc_hwpmcs[];	/* 'npmc' pointers */
};

#define	PMC_PCPU_CPU_MASK		0x000000FF
#define	PMC_PCPU_FLAGS_MASK		0xFFFFFF00
#define	PMC_PCPU_FLAGS_SHIFT		8
#define	PMC_PCPU_STATE_TO_CPU(S)	((S) & PMC_PCPU_CPU_MASK)
#define	PMC_PCPU_STATE_TO_FLAGS(S)	(((S) & PMC_PCPU_FLAGS_MASK) >> PMC_PCPU_FLAGS_SHIFT)
#define	PMC_PCPU_FLAGS_TO_STATE(F)	(((F) << PMC_PCPU_FLAGS_SHIFT) & PMC_PCPU_FLAGS_MASK)
#define	PMC_PCPU_CPU_TO_STATE(C)	((C) & PMC_PCPU_CPU_MASK)
#define	PMC_PCPU_FLAG_HTT		(PMC_PCPU_FLAGS_TO_STATE(0x1))

/*
 * struct pmc_binding
 *
 * CPU binding information.
 */

struct pmc_binding {
	int	pb_bound;	/* is bound? */
	int	pb_cpu;		/* if so, to which CPU */
};


struct pmc_mdep;

/*
 * struct pmc_classdep
 *
 * PMC class-dependent operations.
 */
struct pmc_classdep {
	uint32_t	pcd_caps;	/* class capabilities */
	enum pmc_class	pcd_class;	/* class id */
	int		pcd_num;	/* number of PMCs */
	int		pcd_ri;		/* row index of the first PMC in class */
	int		pcd_width;	/* width of the PMC */

	/* configuring/reading/writing the hardware PMCs */
	int (*pcd_config_pmc)(int _cpu, int _ri, struct pmc *_pm);
	int (*pcd_get_config)(int _cpu, int _ri, struct pmc **_ppm);
	int (*pcd_read_pmc)(int _cpu, int _ri, pmc_value_t *_value);
	int (*pcd_write_pmc)(int _cpu, int _ri, pmc_value_t _value);

	/* pmc allocation/release */
	int (*pcd_allocate_pmc)(int _cpu, int _ri, struct pmc *_t,
		const struct pmc_op_pmcallocate *_a);
	int (*pcd_release_pmc)(int _cpu, int _ri, struct pmc *_pm);

	/* starting and stopping PMCs */
	int (*pcd_start_pmc)(int _cpu, int _ri);
	int (*pcd_stop_pmc)(int _cpu, int _ri);

	/* description */
	int (*pcd_describe)(int _cpu, int _ri, struct pmc_info *_pi,
		struct pmc **_ppmc);

	/* class-dependent initialization & finalization */
	int (*pcd_pcpu_init)(struct pmc_mdep *_md, int _cpu);
	int (*pcd_pcpu_fini)(struct pmc_mdep *_md, int _cpu);

	/* machine-specific interface */
	int (*pcd_get_msr)(int _ri, uint32_t *_msr);
};

/*
 * struct pmc_mdep
 *
 * Machine dependent bits needed per CPU type.
 */

struct pmc_mdep  {
	uint32_t	pmd_cputype;    /* from enum pmc_cputype */
	uint32_t	pmd_npmc;	/* number of PMCs per CPU */
	uint32_t	pmd_nclass;	/* number of PMC classes present */

	/*
	 * Machine dependent methods.
	 */

	/* per-cpu initialization and finalization */
	int (*pmd_pcpu_init)(struct pmc_mdep *_md, int _cpu);
	int (*pmd_pcpu_fini)(struct pmc_mdep *_md, int _cpu);

	/* thread context switch in/out */
	int (*pmd_switch_in)(struct pmc_cpu *_p, struct pmc_process *_pp);
	int (*pmd_switch_out)(struct pmc_cpu *_p, struct pmc_process *_pp);

	/* handle a PMC interrupt */
	int (*pmd_intr)(struct trapframe *_tf);

	/*
	 * PMC class dependent information.
	 */
	struct pmc_classdep pmd_classdep[];
};

/*
 * Per-CPU state.  This is an array of 'mp_ncpu' pointers
 * to struct pmc_cpu descriptors.
 */

extern struct pmc_cpu **pmc_pcpu;

/* driver statistics */
extern struct pmc_driverstats pmc_stats;

#if	defined(HWPMC_DEBUG)
#include <sys/ktr.h>

/* debug flags, major flag groups */
struct pmc_debugflags {
	int	pdb_CPU;
	int	pdb_CSW;
	int	pdb_LOG;
	int	pdb_MDP;
	int	pdb_MOD;
	int	pdb_OWN;
	int	pdb_PMC;
	int	pdb_PRC;
	int	pdb_SAM;
};

extern struct pmc_debugflags pmc_debugflags;

#define	KTR_PMC			KTR_SUBSYS

#define	PMC_DEBUG_STRSIZE		128
#define	PMC_DEBUG_DEFAULT_FLAGS		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }

#define	PMCDBG0(M, N, L, F) do {					\
	if (pmc_debugflags.pdb_ ## M & (1 << PMC_DEBUG_MIN_ ## N))	\
		CTR0(KTR_PMC, #M ":" #N ":" #L  ": " F);		\
} while (0)
#define	PMCDBG1(M, N, L, F, p1) do {					\
	if (pmc_debugflags.pdb_ ## M & (1 << PMC_DEBUG_MIN_ ## N))	\
		CTR1(KTR_PMC, #M ":" #N ":" #L  ": " F, p1);		\
} while (0)
#define	PMCDBG2(M, N, L, F, p1, p2) do {				\
	if (pmc_debugflags.pdb_ ## M & (1 << PMC_DEBUG_MIN_ ## N))	\
		CTR2(KTR_PMC, #M ":" #N ":" #L  ": " F, p1, p2);	\
} while (0)
#define	PMCDBG3(M, N, L, F, p1, p2, p3) do {				\
	if (pmc_debugflags.pdb_ ## M & (1 << PMC_DEBUG_MIN_ ## N))	\
		CTR3(KTR_PMC, #M ":" #N ":" #L  ": " F, p1, p2, p3);	\
} while (0)
#define	PMCDBG4(M, N, L, F, p1, p2, p3, p4) do {			\
	if (pmc_debugflags.pdb_ ## M & (1 << PMC_DEBUG_MIN_ ## N))	\
		CTR4(KTR_PMC, #M ":" #N ":" #L  ": " F, p1, p2, p3, p4);\
} while (0)
#define	PMCDBG5(M, N, L, F, p1, p2, p3, p4, p5) do {			\
	if (pmc_debugflags.pdb_ ## M & (1 << PMC_DEBUG_MIN_ ## N))	\
		CTR5(KTR_PMC, #M ":" #N ":" #L  ": " F, p1, p2, p3, p4,	\
		    p5);						\
} while (0)
#define	PMCDBG6(M, N, L, F, p1, p2, p3, p4, p5, p6) do {		\
	if (pmc_debugflags.pdb_ ## M & (1 << PMC_DEBUG_MIN_ ## N))	\
		CTR6(KTR_PMC, #M ":" #N ":" #L  ": " F, p1, p2, p3, p4,	\
		    p5, p6);						\
} while (0)
	
/* Major numbers */
#define	PMC_DEBUG_MAJ_CPU		0 /* cpu switches */
#define	PMC_DEBUG_MAJ_CSW		1 /* context switches */
#define	PMC_DEBUG_MAJ_LOG		2 /* logging */
#define	PMC_DEBUG_MAJ_MDP		3 /* machine dependent */
#define	PMC_DEBUG_MAJ_MOD		4 /* misc module infrastructure */
#define	PMC_DEBUG_MAJ_OWN		5 /* owner */
#define	PMC_DEBUG_MAJ_PMC		6 /* pmc management */
#define	PMC_DEBUG_MAJ_PRC		7 /* processes */
#define	PMC_DEBUG_MAJ_SAM		8 /* sampling */

/* Minor numbers */

/* Common (8 bits) */
#define	PMC_DEBUG_MIN_ALL		0 /* allocation */
#define	PMC_DEBUG_MIN_REL		1 /* release */
#define	PMC_DEBUG_MIN_OPS		2 /* ops: start, stop, ... */
#define	PMC_DEBUG_MIN_INI		3 /* init */
#define	PMC_DEBUG_MIN_FND		4 /* find */

/* MODULE */
#define	PMC_DEBUG_MIN_PMH	       14 /* pmc_hook */
#define	PMC_DEBUG_MIN_PMS	       15 /* pmc_syscall */

/* OWN */
#define	PMC_DEBUG_MIN_ORM		8 /* owner remove */
#define	PMC_DEBUG_MIN_OMR		9 /* owner maybe remove */

/* PROCESSES */
#define	PMC_DEBUG_MIN_TLK		8 /* link target */
#define	PMC_DEBUG_MIN_TUL		9 /* unlink target */
#define	PMC_DEBUG_MIN_EXT	       10 /* process exit */
#define	PMC_DEBUG_MIN_EXC	       11 /* process exec */
#define	PMC_DEBUG_MIN_FRK	       12 /* process fork */
#define	PMC_DEBUG_MIN_ATT	       13 /* attach/detach */
#define	PMC_DEBUG_MIN_SIG	       14 /* signalling */

/* CONTEXT SWITCHES */
#define	PMC_DEBUG_MIN_SWI		8 /* switch in */
#define	PMC_DEBUG_MIN_SWO		9 /* switch out */

/* PMC */
#define	PMC_DEBUG_MIN_REG		8 /* pmc register */
#define	PMC_DEBUG_MIN_ALR		9 /* allocate row */

/* MACHINE DEPENDENT LAYER */
#define	PMC_DEBUG_MIN_REA		8 /* read */
#define	PMC_DEBUG_MIN_WRI		9 /* write */
#define	PMC_DEBUG_MIN_CFG	       10 /* config */
#define	PMC_DEBUG_MIN_STA	       11 /* start */
#define	PMC_DEBUG_MIN_STO	       12 /* stop */
#define	PMC_DEBUG_MIN_INT	       13 /* interrupts */

/* CPU */
#define	PMC_DEBUG_MIN_BND		8 /* bind */
#define	PMC_DEBUG_MIN_SEL		9 /* select */

/* LOG */
#define	PMC_DEBUG_MIN_GTB		8 /* get buf */
#define	PMC_DEBUG_MIN_SIO		9 /* schedule i/o */
#define	PMC_DEBUG_MIN_FLS	       10 /* flush */
#define	PMC_DEBUG_MIN_SAM	       11 /* sample */
#define	PMC_DEBUG_MIN_CLO	       12 /* close */

#else
#define	PMCDBG0(M, N, L, F)		/* nothing */
#define	PMCDBG1(M, N, L, F, p1)
#define	PMCDBG2(M, N, L, F, p1, p2)
#define	PMCDBG3(M, N, L, F, p1, p2, p3)
#define	PMCDBG4(M, N, L, F, p1, p2, p3, p4)
#define	PMCDBG5(M, N, L, F, p1, p2, p3, p4, p5)
#define	PMCDBG6(M, N, L, F, p1, p2, p3, p4, p5, p6)
#endif

/* declare a dedicated memory pool */
MALLOC_DECLARE(M_PMC);

/*
 * Functions
 */

struct pmc_mdep *pmc_md_initialize(void);	/* MD init function */
void	pmc_md_finalize(struct pmc_mdep *_md);	/* MD fini function */
int	pmc_getrowdisp(int _ri);
int	pmc_process_interrupt(int _ring, struct pmc *_pm, struct trapframe *_tf);
int	pmc_save_kernel_callchain(uintptr_t *_cc, int _maxsamples,
    struct trapframe *_tf);
int	pmc_save_user_callchain(uintptr_t *_cc, int _maxsamples,
    struct trapframe *_tf);
struct pmc_mdep *pmc_mdep_alloc(int nclasses);
void pmc_mdep_free(struct pmc_mdep *md);
uint64_t pmc_rdtsc(void);
#endif /* _KERNEL */
#endif /* _SYS_PMC_H_ */
