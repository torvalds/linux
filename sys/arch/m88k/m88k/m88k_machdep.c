/*	$OpenBSD: m88k_machdep.c,v 1.74 2025/04/30 12:35:37 visa Exp $	*/
/*
 * Copyright (c) 1998, 1999, 2000, 2001 Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * Mach Operating System
 * Copyright (c) 1993-1991 Carnegie Mellon University
 * Copyright (c) 1991 OMRON Corporation
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/msgbuf.h>
#include <sys/exec.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#ifdef MULTIPROCESSOR
#include <sys/mplock.h>
#endif

#include <machine/asm.h>
#include <machine/asm_macro.h>
#include <machine/atomic.h>
#include <machine/cmmu.h>
#include <machine/cpu.h>
#include <machine/reg.h>
#ifdef M88100
#include <machine/m88100.h>
#endif

#include <uvm/uvm_extern.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>
#endif /* DDB */

typedef struct {
	u_int32_t word_one, word_two;
} m88k_exception_vector_area;

void	dumpconf(void);
void	dumpsys(void);
void	regdump(struct trapframe *f);
void	*vector_init(m88k_exception_vector_area *, u_int32_t *, int);
void	atomic_init(void);

/*
 * CMMU and CPU variables
 */

#ifdef MULTIPROCESSOR
cpuid_t	master_cpu;
__cpu_simple_lock_t cmmu_cpu_lock = __SIMPLELOCK_UNLOCKED;
#endif

struct cpu_info m88k_cpus[MAX_CPUS] = {
#ifndef MULTIPROCESSOR
	{ .ci_flags = CIF_ALIVE | CIF_PRIMARY }
#endif
};
const struct cmmu_p *cmmu;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = IPL_NONE;

/*
 * Set registers on exec.
 * Clear all except sp and pc.
 */
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct trapframe *tf = (struct trapframe *)USER_REGS(p);

	/*
	 * Setup proper floating-point settings. This is necessary because
	 * we will return through the exception path, which only saves the
	 * integer registers, and not through cpu_switchto() (which saves
	 * fcr62 and fcr63 in the pcb).  This is safe to do here since the
	 * FPU is enabled and the kernel doesn't use it.
	 */
	__asm__ volatile ("fstcr %r0, %fcr0");
	__asm__ volatile ("fstcr %r0, %fcr62");
	__asm__ volatile ("fstcr %r0, %fcr63");

	memset(tf, 0, sizeof *tf);

#ifdef M88110
	if (CPU_IS88110) {
		/*
		 * user mode, interrupts enabled,
		 * graphics unit, fp enabled
		 */
		tf->tf_epsr = PSR_SFD;
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
		/*
		 * user mode, interrupts enabled,
		 * no graphics unit, fp enabled
		 */
		tf->tf_epsr = PSR_SFD | PSR_SFD2;
	}
#endif

	/*
	 * We want to start executing at pack->ep_entry. The way to
	 * do this is force the processor to fetch from ep_entry.
	 *
	 * However, since we will return through m{88100,88110}_syscall(),
	 * we need to setup registers so that the success return, when
	 * ``incrementing'' the instruction pointers, will cause the
	 * binary to start at the expected address.
	 *
	 * This relies on the fact that binaries start with
	 *
	 *	br.n	1f
	 *	 or	r2, r0, r30
	 * 1:
	 *
	 * So the first two instructions can be skipped.
	 */
#ifdef M88110
	if (CPU_IS88110) {
                /*
                 * The first instruction we want to run is at
                 * ep_entry + 8. m88110_syscall() returning EJUSTRETURN
                 * will resume at exip + 4, so make it point to
                 * ep_entry + 4.
                 */
		tf->tf_exip = (pack->ep_entry & XIP_ADDR) + 4;
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
                /*
                 * The first instruction we want to run is at
                 * ep_entry + 8. m88100_syscall() returning EJUSTRETURN
                 * will resume at snip, so make it point to
                 * ep_entry + 8.
                 */
		tf->tf_snip = ((pack->ep_entry + 8) & FIP_ADDR) | FIP_V;
		tf->tf_sfip = tf->tf_snip + 4;
	}
#endif
	tf->tf_r[2] = stack;
	tf->tf_r[31] = stack;
}

#ifdef DDB
int longformat = 1;
void
regdump(struct trapframe *f)
{
#define R(i) f->tf_r[i]
	printf("R00-05: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	       R(0),R(1),R(2),R(3),R(4),R(5));
	printf("R06-11: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	       R(6),R(7),R(8),R(9),R(10),R(11));
	printf("R12-17: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	       R(12),R(13),R(14),R(15),R(16),R(17));
	printf("R18-23: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	       R(18),R(19),R(20),R(21),R(22),R(23));
	printf("R24-29: 0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx  0x%08lx\n",
	       R(24),R(25),R(26),R(27),R(28),R(29));
	printf("R30-31: 0x%08lx  0x%08lx\n",R(30),R(31));
#ifdef M88110
	if (CPU_IS88110) {
		printf("exip %lx enip %lx\n", f->tf_exip, f->tf_enip);
	}
#endif
#ifdef M88100
	if (CPU_IS88100) {
		printf("sxip %lx snip %lx sfip %lx\n",
		    f->tf_sxip, f->tf_snip, f->tf_sfip);
	}
	if (CPU_IS88100 && ISSET(f->tf_dmt0, DMT_VALID)) {
		/* print dmt stuff for data access fault */
		printf("fault type %ld\n", (f->tf_dpfsr >> 16) & 0x7);
		dae_print((u_int *)f);
	}
	if (CPU_IS88100 && longformat != 0) {
		printf("fpsr %lx fpcr %lx epsr %lx ssbr %lx\n",
		    f->tf_fpsr, f->tf_fpcr, f->tf_epsr, f->tf_ssbr);
		printf("fpecr %lx fphs1 %lx fpls1 %lx fphs2 %lx fpls2 %lx\n",
		    f->tf_fpecr, f->tf_fphs1, f->tf_fpls1,
		    f->tf_fphs2, f->tf_fpls2);
		printf("fppt %lx fprh %lx fprl %lx fpit %lx\n",
		    f->tf_fppt, f->tf_fprh, f->tf_fprl, f->tf_fpit);
		printf("vector %ld mask %lx flags %lx scratch1 %lx cpu %p\n",
		    f->tf_vector, f->tf_mask, f->tf_flags,
		    f->tf_scratch1, f->tf_cpu);
	}
#endif
#ifdef M88110
	if (CPU_IS88110 && longformat != 0) {
		printf("fpsr %lx fpcr %lx fpecr %lx epsr %lx\n",
		    f->tf_fpsr, f->tf_fpcr, f->tf_fpecr, f->tf_epsr);
		printf("dsap %lx duap %lx dsr %lx dlar %lx dpar %lx\n",
		    f->tf_dsap, f->tf_duap, f->tf_dsr, f->tf_dlar, f->tf_dpar);
		printf("isap %lx iuap %lx isr %lx ilar %lx ipar %lx\n",
		    f->tf_isap, f->tf_iuap, f->tf_isr, f->tf_ilar, f->tf_ipar);
		printf("vector %ld mask %lx flags %lx scratch1 %lx cpu %p\n",
		    f->tf_vector, f->tf_mask, f->tf_flags,
		    f->tf_scratch1, f->tf_cpu);
	}
#endif
}
#endif	/* DDB */

/*
 * Set up the cpu_info pointer and the cpu number for the current processor.
 */
struct cpu_info *
set_cpu_number(cpuid_t number)
{
	struct cpu_info *ci;

#ifdef MULTIPROCESSOR
	ci = &m88k_cpus[number];
#else
	ci = &m88k_cpus[0];
#endif
	ci->ci_cpuid = number;

	__asm__ volatile ("stcr %0, %%cr17" :: "r" (ci));
	flush_pipeline();
	return ci;
}

/*
 * Notify the current process (p) that it has a signal pending,
 * process as soon as possible.
 */
void
signotify(struct proc *p)
{
	aston(p);
	cpu_unidle(p->p_cpu);
}

#ifdef MULTIPROCESSOR
void
cpu_unidle(struct cpu_info *ci)
{
	if (ci != curcpu())
		m88k_send_ipi(CI_IPI_NOTIFY, ci->ci_cpuid);
}
#endif

/*
 * Preempt the current process if in interrupt from user mode,
 * or after the current trap/syscall if in system mode.
 */
void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	/* There's a risk we'll be called before the idle threads start */
	if (ci->ci_curproc != NULL) {
		aston(ci->ci_curproc);
		if (ci != curcpu())
			cpu_unidle(ci);
	}
}

/*
 * Generic soft interrupt interface
 */

void	dosoftint(int);
int	softpending;

void
dosoftint(int sir)
{
	int q, mask;

#ifdef MULTIPROCESSOR
	__mp_lock(&kernel_lock);
#endif

	for (q = NSOFTINTR - 1; q >= 0; q--) {
		mask = 1 << q;
		if (mask & sir)
			softintr_dispatch(q);
	}

#ifdef MULTIPROCESSOR
	__mp_unlock(&kernel_lock);
#endif
}

void
softintr(int si)
{
	atomic_setbits_int(&softpending, 1 << si);
}

int
spl0()
{
	int sir;
	int s;

	/*
	 * Try to avoid potentially expensive setipl calls if nothing
	 * seems to be pending.
	 */
	if ((sir = atomic_clear_int(&softpending)) != 0) {
		s = setipl(IPL_SOFTINT);
		dosoftint(sir);
		setipl(IPL_NONE);
	} else
		s = setipl(IPL_NONE);

	return (s);
}

#define EMPTY_BR	0xc0000000	/* empty "br" instruction */
#define NO_OP 		0xf4005800	/* "or r0, r0, r0" */

#define BRANCH(FROM, TO) \
	(EMPTY_BR | ((((vaddr_t)(TO) - (vaddr_t)(FROM)) >> 2) & 0x03ffffff))

#define SET_VECTOR_88100(NUM, VALUE) \
	do { \
		vbr[NUM].word_one = NO_OP; \
		vbr[NUM].word_two = BRANCH(&vbr[NUM].word_two, VALUE); \
	} while (0)

#define SET_VECTOR_88110(NUM, VALUE) \
	do { \
		vbr[NUM].word_one = BRANCH(&vbr[NUM].word_one, VALUE); \
		vbr[NUM].word_two = NO_OP; \
	} while (0)

/*
 * vector_init(vector, vector_init_list, bootstrap)
 *
 * This routine sets up the m88k vector table for the running processor,
 * as well as the atomic operation routines for multiprocessor kernels.
 * This is the first C code to run, before anything is initialized.
 *
 * I would add an extra four bytes to the exception vectors page pointed
 * to by the vbr, since the 88100 may execute the first instruction of the
 * next trap handler, as documented in its Errata. Processing trap #511
 * would then fall into the next page, unless the address computation wraps,
 * or software traps can not trigger the issue - the Errata does not provide
 * more detail. And since the MVME BUG does not add an extra NOP after its
 * VBR page, I'll assume this is safe for now -- miod
 */
void *
vector_init(m88k_exception_vector_area *vbr, u_int32_t *vector_init_list,
    int bootstrap)
{
	u_int num;
	u_int32_t vec;

	switch (cputyp) {
	default:
#ifdef M88110
	case CPU_88110:
	    {
		extern void m88110_sigsys(void);
		extern void m88110_syscall_handler(void);
		extern void m88110_cache_flush_handler(void);
		extern void m88110_stepbpt(void);
		extern void m88110_userbpt(void);

		for (num = 0; (vec = vector_init_list[num]) != 0; num++)
			SET_VECTOR_88110(num, vec);

		if (bootstrap)
			SET_VECTOR_88110(0x03, vector_init_list[num + 1]);

		for (; num < 512; num++)
			SET_VECTOR_88110(num, m88110_sigsys);

		SET_VECTOR_88110(450, m88110_syscall_handler);
		SET_VECTOR_88110(451, m88110_cache_flush_handler);
		/*
		 * GCC will by default produce explicit trap 503
		 * for division by zero
		 */
		SET_VECTOR_88110(503, vector_init_list[8]);
		SET_VECTOR_88110(504, m88110_stepbpt);
		SET_VECTOR_88110(511, m88110_userbpt);
	    }
		break;
#endif
#ifdef M88100
	case CPU_88100:
	    {
		extern void sigsys(void);
		extern void syscall_handler(void);
		extern void cache_flush_handler(void);
		extern void stepbpt(void);
		extern void userbpt(void);

		for (num = 0; (vec = vector_init_list[num]) != 0; num++)
			SET_VECTOR_88100(num, vec);

		if (bootstrap)
			SET_VECTOR_88100(0x03, vector_init_list[num + 1]);

		for (; num < 512; num++)
			SET_VECTOR_88100(num, sigsys);

		SET_VECTOR_88100(450, syscall_handler);
		SET_VECTOR_88100(451, cache_flush_handler);
		/*
		 * GCC will by default produce explicit trap 503
		 * for division by zero
		 */
		SET_VECTOR_88100(503, vector_init_list[8]);
		SET_VECTOR_88100(504, stepbpt);
		SET_VECTOR_88100(511, userbpt);
	    }
		break;
#endif
	}

	return vbr;
}

#ifdef MULTIPROCESSOR
/*
 * void atomic_init(void);
 *
 * This routine sets up proper atomic operation code for SMP kernels
 * with both 88100 and 88110 support compiled-in. This is crucial enough
 * to have to be done as early as possible.
 * This is among the first C code to run, before anything is initialized.
 */
void
atomic_init()
{
#if defined(M88100) && defined(M88110)
	if (cputyp == CPU_88100) {
		extern uint32_t __atomic_lock[];
		extern uint32_t __atomic_lock_88100[], __atomic_lock_88100_end[];
		extern uint32_t __atomic_unlock[];
		extern uint32_t __atomic_unlock_88100[], __atomic_unlock_88100_end[];

		uint32_t *s, *e, *d;

		d = __atomic_lock;
		s = __atomic_lock_88100;
		e = __atomic_lock_88100_end;
		while (s != e)
				*d++ = *s++;

		d = __atomic_unlock;
		s = __atomic_unlock_88100;
		e = __atomic_unlock_88100_end;
		while (s != e)
				*d++ = *s++;
	}
#endif	/* M88100 && M88110 */
}
#endif	/* MULTIPROCESSOR */
