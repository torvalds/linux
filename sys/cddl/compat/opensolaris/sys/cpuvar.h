/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _COMPAT_OPENSOLARIS_SYS_CPUVAR_H
#define	_COMPAT_OPENSOLARIS_SYS_CPUVAR_H

#include <sys/mutex.h>
#include <sys/cpuvar_defs.h>

#ifdef _KERNEL

struct cyc_cpu;

typedef struct {
	int		cpuid;
	uint32_t	cpu_flags;
	uint_t		cpu_intr_actv;
	uintptr_t	cpu_dtrace_caller;	/* DTrace: caller, if any */
	hrtime_t	cpu_dtrace_chillmark;	/* DTrace: chill mark time */
	hrtime_t	cpu_dtrace_chilled;	/* DTrace: total chill time */
} solaris_cpu_t; 

/* Some code may choose to redefine this if pcpu_t would be more useful. */
#define cpu_t	solaris_cpu_t
#define	cpu_id	cpuid

extern solaris_cpu_t    solaris_cpu[];

#define	CPU_CACHE_COHERENCE_SIZE	64

/*
 * The cpu_core structure consists of per-CPU state available in any context.
 * On some architectures, this may mean that the page(s) containing the
 * NCPU-sized array of cpu_core structures must be locked in the TLB -- it
 * is up to the platform to assure that this is performed properly.  Note that
 * the structure is sized to avoid false sharing.
 */
#define	CPUC_SIZE		(sizeof (uint16_t) + sizeof (uintptr_t) + \
				sizeof (kmutex_t))
#define	CPUC_SIZE1		roundup(CPUC_SIZE, CPU_CACHE_COHERENCE_SIZE)
#define	CPUC_PADSIZE		CPUC_SIZE1 - CPUC_SIZE

typedef struct cpu_core {
	uint16_t	cpuc_dtrace_flags;	/* DTrace flags */
	uint8_t		cpuc_pad[CPUC_PADSIZE];	/* padding */
	uintptr_t	cpuc_dtrace_illval;	/* DTrace illegal value */
	kmutex_t	cpuc_pid_lock;		/* DTrace pid provider lock */
} cpu_core_t;

extern cpu_core_t cpu_core[];

extern kmutex_t	cpu_lock;
#endif /* _KERNEL */

/*
 * Flags in the CPU structure.
 *
 * These are protected by cpu_lock (except during creation).
 *
 * Offlined-CPUs have three stages of being offline:
 *
 * CPU_ENABLE indicates that the CPU is participating in I/O interrupts
 * that can be directed at a number of different CPUs.  If CPU_ENABLE
 * is off, the CPU will not be given interrupts that can be sent elsewhere,
 * but will still get interrupts from devices associated with that CPU only,
 * and from other CPUs.
 *
 * CPU_OFFLINE indicates that the dispatcher should not allow any threads
 * other than interrupt threads to run on that CPU.  A CPU will not have
 * CPU_OFFLINE set if there are any bound threads (besides interrupts).
 *
 * CPU_QUIESCED is set if p_offline was able to completely turn idle the
 * CPU and it will not have to run interrupt threads.  In this case it'll
 * stay in the idle loop until CPU_QUIESCED is turned off.
 *
 * CPU_FROZEN is used only by CPR to mark CPUs that have been successfully
 * suspended (in the suspend path), or have yet to be resumed (in the resume
 * case).
 *
 * On some platforms CPUs can be individually powered off.
 * The following flags are set for powered off CPUs: CPU_QUIESCED,
 * CPU_OFFLINE, and CPU_POWEROFF.  The following flags are cleared:
 * CPU_RUNNING, CPU_READY, CPU_EXISTS, and CPU_ENABLE.
 */
#define	CPU_RUNNING	0x001		/* CPU running */
#define	CPU_READY	0x002		/* CPU ready for cross-calls */
#define	CPU_QUIESCED	0x004		/* CPU will stay in idle */
#define	CPU_EXISTS	0x008		/* CPU is configured */
#define	CPU_ENABLE	0x010		/* CPU enabled for interrupts */
#define	CPU_OFFLINE	0x020		/* CPU offline via p_online */
#define	CPU_POWEROFF	0x040		/* CPU is powered off */
#define	CPU_FROZEN	0x080		/* CPU is frozen via CPR suspend */
#define	CPU_SPARE	0x100		/* CPU offline available for use */
#define	CPU_FAULTED	0x200		/* CPU offline diagnosed faulty */

typedef enum {
	CPU_INIT,
	CPU_CONFIG,
	CPU_UNCONFIG,
	CPU_ON,
	CPU_OFF,
	CPU_CPUPART_IN,
	CPU_CPUPART_OUT
} cpu_setup_t;

typedef int cpu_setup_func_t(cpu_setup_t, int, void *);


#endif /* _COMPAT_OPENSOLARIS_SYS_CPUVAR_H */
