/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Wind River Systems, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#ifndef _SYS_PCPU_H_
#define	_SYS_PCPU_H_

#ifdef LOCORE
#error "no assembler-serviceable parts inside"
#endif

#include <sys/_cpuset.h>
#include <sys/_lock.h>
#include <sys/_mutex.h>
#include <sys/_sx.h>
#include <sys/queue.h>
#include <sys/_rmlock.h>
#include <sys/resource.h>
#include <machine/pcpu.h>

#define	DPCPU_SETNAME		"set_pcpu"
#define	DPCPU_SYMPREFIX		"pcpu_entry_"

#ifdef _KERNEL

/*
 * Define a set for pcpu data.
 */
extern uintptr_t *__start_set_pcpu;
__GLOBL(__start_set_pcpu);
extern uintptr_t *__stop_set_pcpu;
__GLOBL(__stop_set_pcpu);

/*
 * Array of dynamic pcpu base offsets.  Indexed by id.
 */
extern uintptr_t dpcpu_off[];

/*
 * Convenience defines.
 */
#define	DPCPU_START		((uintptr_t)&__start_set_pcpu)
#define	DPCPU_STOP		((uintptr_t)&__stop_set_pcpu)
#define	DPCPU_BYTES		(DPCPU_STOP - DPCPU_START)
#define	DPCPU_MODMIN		2048
#define	DPCPU_SIZE		roundup2(DPCPU_BYTES, PAGE_SIZE)
#define	DPCPU_MODSIZE		(DPCPU_SIZE - (DPCPU_BYTES - DPCPU_MODMIN))

/*
 * Declaration and definition.
 */
#define	DPCPU_NAME(n)		pcpu_entry_##n
#define	DPCPU_DECLARE(t, n)	extern t DPCPU_NAME(n)
/* struct _hack is to stop this from being used with the static keyword. */
#define	DPCPU_DEFINE(t, n)	\
    struct _hack; t DPCPU_NAME(n) __section(DPCPU_SETNAME) __used
#if defined(KLD_MODULE) && (defined(__aarch64__) || defined(__riscv))
/*
 * On some architectures the compiler will use PC-relative load to
 * find the address of DPCPU data with the static keyword. We then
 * use this to find the offset of the data in a per-CPU region.
 * This works for in the kernel as we can allocate the space ahead
 * of time, however modules need to allocate a sepatate space and
 * then use relocations to fix the address of the data. As
 * PC-relative data doesn't have a relocation there is nothing for
 * the kernel module linker to fix so data is accessed from the
 * wrong location.
 *
 * This is a workaround until a better solution can be found.
 *
 * VNET_DEFINE_STATIC also has the same workaround.
 */
#define	DPCPU_DEFINE_STATIC(t, n)	\
    t DPCPU_NAME(n) __section(DPCPU_SETNAME) __used
#else
#define	DPCPU_DEFINE_STATIC(t, n)	\
    static t DPCPU_NAME(n) __section(DPCPU_SETNAME) __used
#endif

/*
 * Accessors with a given base.
 */
#define	_DPCPU_PTR(b, n)						\
    (__typeof(DPCPU_NAME(n))*)((b) + (uintptr_t)&DPCPU_NAME(n))
#define	_DPCPU_GET(b, n)	(*_DPCPU_PTR(b, n))
#define	_DPCPU_SET(b, n, v)	(*_DPCPU_PTR(b, n) = v)

/*
 * Accessors for the current cpu.
 */
#define	DPCPU_PTR(n)		_DPCPU_PTR(PCPU_GET(dynamic), n)
#define	DPCPU_GET(n)		(*DPCPU_PTR(n))
#define	DPCPU_SET(n, v)		(*DPCPU_PTR(n) = v)

/*
 * Accessors for remote cpus.
 */
#define	DPCPU_ID_PTR(i, n)	_DPCPU_PTR(dpcpu_off[(i)], n)
#define	DPCPU_ID_GET(i, n)	(*DPCPU_ID_PTR(i, n))
#define	DPCPU_ID_SET(i, n, v)	(*DPCPU_ID_PTR(i, n) = v)

/*
 * Utility macros.
 */
#define	DPCPU_SUM(n) __extension__					\
({									\
	u_int _i;							\
	__typeof(*DPCPU_PTR(n)) sum;					\
									\
	sum = 0;							\
	CPU_FOREACH(_i) {						\
		sum += *DPCPU_ID_PTR(_i, n);				\
	}								\
	sum;								\
})

#define	DPCPU_VARSUM(n, var) __extension__				\
({									\
	u_int _i;							\
	__typeof((DPCPU_PTR(n))->var) sum;				\
									\
	sum = 0;							\
	CPU_FOREACH(_i) {						\
		sum += (DPCPU_ID_PTR(_i, n))->var;			\
	}								\
	sum;								\
})

#define	DPCPU_ZERO(n) do {						\
	u_int _i;							\
									\
	CPU_FOREACH(_i) {						\
		bzero(DPCPU_ID_PTR(_i, n), sizeof(*DPCPU_PTR(n)));	\
	}								\
} while(0)

#endif /* _KERNEL */

/*
 * This structure maps out the global data that needs to be kept on a
 * per-cpu basis.  The members are accessed via the PCPU_GET/SET/PTR
 * macros defined in <machine/pcpu.h>.  Machine dependent fields are
 * defined in the PCPU_MD_FIELDS macro defined in <machine/pcpu.h>.
 */
struct pcpu {
	struct thread	*pc_curthread;		/* Current thread */
	struct thread	*pc_idlethread;		/* Idle thread */
	struct thread	*pc_fpcurthread;	/* Fp state owner */
	struct thread	*pc_deadthread;		/* Zombie thread or NULL */
	struct pcb	*pc_curpcb;		/* Current pcb */
	uint64_t	pc_switchtime;		/* cpu_ticks() at last csw */
	int		pc_switchticks;		/* `ticks' at last csw */
	u_int		pc_cpuid;		/* This cpu number */
	STAILQ_ENTRY(pcpu) pc_allcpu;
	struct lock_list_entry *pc_spinlocks;
	long		pc_cp_time[CPUSTATES];	/* statclock ticks */
	struct device	*pc_device;
	void		*pc_netisr;		/* netisr SWI cookie */
	int		pc_unused1;		/* unused field */
	int		pc_domain;		/* Memory domain. */
	struct rm_queue	pc_rm_queue;		/* rmlock list of trackers */
	uintptr_t	pc_dynamic;		/* Dynamic per-cpu data area */
	uint64_t	pc_early_dummy_counter;	/* Startup time counter(9) */

	/*
	 * Keep MD fields last, so that CPU-specific variations on a
	 * single architecture don't result in offset variations of
	 * the machine-independent fields of the pcpu.  Even though
	 * the pcpu structure is private to the kernel, some ports
	 * (e.g., lsof, part of gtop) define _KERNEL and include this
	 * header.  While strictly speaking this is wrong, there's no
	 * reason not to keep the offsets of the MI fields constant
	 * if only to make kernel debugging easier.
	 */
	PCPU_MD_FIELDS;
} __aligned(CACHE_LINE_SIZE);

#ifdef _KERNEL

STAILQ_HEAD(cpuhead, pcpu);

extern struct cpuhead cpuhead;
extern struct pcpu *cpuid_to_pcpu[];

#define	curcpu		PCPU_GET(cpuid)
#define	curproc		(curthread->td_proc)
#ifndef curthread
#define	curthread	PCPU_GET(curthread)
#endif
#define	curvidata	PCPU_GET(vidata)

#define UMA_PCPU_ALLOC_SIZE		PAGE_SIZE

#ifdef CTASSERT
#if defined(__i386__) || defined(__amd64__)
/* Required for counters(9) to work on x86. */
CTASSERT(sizeof(struct pcpu) == UMA_PCPU_ALLOC_SIZE);
#else
/*
 * To minimize memory waste in per-cpu UMA zones, size of struct pcpu
 * should be denominator of PAGE_SIZE.
 */
CTASSERT((PAGE_SIZE / sizeof(struct pcpu)) * sizeof(struct pcpu) == PAGE_SIZE);
#endif	/* UMA_PCPU_ALLOC_SIZE && x86 */
#endif	/* CTASSERT */

/* Accessor to elements allocated via UMA_ZONE_PCPU zone. */
static inline void *
zpcpu_get(void *base)
{

	return ((char *)(base) + UMA_PCPU_ALLOC_SIZE * curcpu);
}

static inline void *
zpcpu_get_cpu(void *base, int cpu)
{

	return ((char *)(base) + UMA_PCPU_ALLOC_SIZE * cpu);
}

/*
 * Machine dependent callouts.  cpu_pcpu_init() is responsible for
 * initializing machine dependent fields of struct pcpu, and
 * db_show_mdpcpu() is responsible for handling machine dependent
 * fields for the DDB 'show pcpu' command.
 */
void	cpu_pcpu_init(struct pcpu *pcpu, int cpuid, size_t size);
void	db_show_mdpcpu(struct pcpu *pcpu);

void	*dpcpu_alloc(int size);
void	dpcpu_copy(void *s, int size);
void	dpcpu_free(void *s, int size);
void	dpcpu_init(void *dpcpu, int cpuid);
void	pcpu_destroy(struct pcpu *pcpu);
struct	pcpu *pcpu_find(u_int cpuid);
void	pcpu_init(struct pcpu *pcpu, int cpuid, size_t size);

#endif /* _KERNEL */

#endif /* !_SYS_PCPU_H_ */
