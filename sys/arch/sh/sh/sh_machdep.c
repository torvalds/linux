/*	$OpenBSD: sh_machdep.c,v 1.55 2022/10/30 17:43:40 guenther Exp $	*/
/*	$NetBSD: sh3_machdep.c,v 1.59 2006/03/04 01:13:36 uwe Exp $	*/

/*
 * Copyright (c) 2007 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice, this permission notice, and the disclaimer below
 * appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*-
 * Copyright (c) 1996, 1997, 1998, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
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
 *	@(#)machdep.c	7.4 (Berkeley) 6/3/91
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/buf.h>
#include <sys/exec.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/syscallargs.h>
#include <sys/user.h>
#include <sys/sched.h>
#include <sys/msg.h>
#include <sys/conf.h>
#include <sys/kcore.h>
#include <sys/reboot.h>

#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <sh/cache.h>
#include <sh/clock.h>
#include <sh/fpu.h>
#include <sh/locore.h>
#include <sh/mmu.h>
#include <sh/trap.h>
#include <sh/intr.h>
#include <sh/kcore.h>

/* Our exported CPU info; we can have only one. */
int cpu_arch;
int cpu_product;
char cpu_model[120];

struct vm_map *exec_map;
struct vm_map *phys_map;

int physmem;
struct user *proc0paddr;	/* init_main.c use this. */
struct pcb *curpcb;
struct md_upte *curupte;	/* SH3 wired u-area hack */

#define	VBR	(u_int8_t *)SH3_PHYS_TO_P1SEG(IOM_RAM_BEGIN)
vaddr_t ram_start = SH3_PHYS_TO_P1SEG(IOM_RAM_BEGIN);
/* exception handler holder (sh/sh/vectors.S) */
extern char sh_vector_generic[], sh_vector_generic_end[];
extern char sh_vector_interrupt[], sh_vector_interrupt_end[];
#ifdef SH3
extern char sh3_vector_tlbmiss[], sh3_vector_tlbmiss_end[];
#endif
#ifdef SH4
extern char sh4_vector_tlbmiss[], sh4_vector_tlbmiss_end[];
#endif

/*
 * These variables are needed by /sbin/savecore
 */
u_long dumpmag = 0x8fca0101;	/* magic number */
u_int dumpsize;			/* pages */
long dumplo;	 		/* blocks */
cpu_kcore_hdr_t cpu_kcore_hdr;

void
sh_cpu_init(int arch, int product)
{
	/* CPU type */
	cpu_arch = arch;
	cpu_product = product;

#if defined(SH3) && defined(SH4)
	/* Set register addresses */
	sh_devreg_init();
#endif
	/* Cache access ops. */
	sh_cache_init();

	/* MMU access ops. */
	sh_mmu_init();

	/* Hardclock, RTC initialize. */
	machine_clock_init();

	/* ICU initialize. */
	intc_init();

	/* Exception vector. */
	memcpy(VBR + 0x100, sh_vector_generic,
	    sh_vector_generic_end - sh_vector_generic);
#ifdef SH3
	if (CPU_IS_SH3)
		memcpy(VBR + 0x400, sh3_vector_tlbmiss,
		    sh3_vector_tlbmiss_end - sh3_vector_tlbmiss);
#endif
#ifdef SH4
	if (CPU_IS_SH4)
		memcpy(VBR + 0x400, sh4_vector_tlbmiss,
		    sh4_vector_tlbmiss_end - sh4_vector_tlbmiss);
#endif
	memcpy(VBR + 0x600, sh_vector_interrupt,
	    sh_vector_interrupt_end - sh_vector_interrupt);

	if (!SH_HAS_UNIFIED_CACHE)
		sh_icache_sync_all();

	__asm volatile("ldc %0, vbr" :: "r"(VBR));

	/* kernel stack setup */
	__sh_switch_resume = CPU_IS_SH3 ? sh3_switch_resume : sh4_switch_resume;

	/* Set page size (4KB) */
	uvm_setpagesize();
}

/*
 * void sh_proc0_init(void):
 *	Setup proc0 u-area.
 */
void
sh_proc0_init(void)
{
	struct switchframe *sf;
	vaddr_t u;

	/* Steal process0 u-area */
	u = uvm_pageboot_alloc(USPACE);
	memset((void *)u, 0, USPACE);

	/* Setup proc0 */
	proc0paddr = (struct user *)u;
	proc0.p_addr = proc0paddr;
	/*
	 * u-area map:
	 * |user| .... | .................. |
	 * | PAGE_SIZE | USPACE - PAGE_SIZE |
         *        frame top        stack top
	 * current frame ... r6_bank
	 * stack top     ... r7_bank
	 * current stack ... r15
	 */
	curpcb = proc0.p_md.md_pcb = &proc0.p_addr->u_pcb;
	curupte = proc0.p_md.md_upte;

	sf = &curpcb->pcb_sf;
	sf->sf_r6_bank = u + PAGE_SIZE;
	sf->sf_r7_bank = sf->sf_r15	= u + USPACE;
	__asm volatile("ldc %0, r6_bank" :: "r"(sf->sf_r6_bank));
	__asm volatile("ldc %0, r7_bank" :: "r"(sf->sf_r7_bank));

	proc0.p_md.md_regs = (struct trapframe *)sf->sf_r6_bank - 1;
#ifdef KSTACK_DEBUG
	memset((char *)(u + sizeof(struct user)), 0x5a,
	    PAGE_SIZE - sizeof(struct user));
	memset((char *)(u + PAGE_SIZE), 0xa5, USPACE - PAGE_SIZE);
#endif /* KSTACK_DEBUG */
}

void
sh_startup(void)
{
	vaddr_t minaddr, maxaddr;

	printf("%s", version);

#ifdef DEBUG
	printf("general exception handler:\t%d byte\n",
	    sh_vector_generic_end - sh_vector_generic);
	printf("TLB miss exception handler:\t%d byte\n",
#if defined(SH3) && defined(SH4)
	    CPU_IS_SH3 ? sh3_vector_tlbmiss_end - sh3_vector_tlbmiss :
	    sh4_vector_tlbmiss_end - sh4_vector_tlbmiss
#elif defined(SH3)
	    sh3_vector_tlbmiss_end - sh3_vector_tlbmiss
#elif defined(SH4)
	    sh4_vector_tlbmiss_end - sh4_vector_tlbmiss
#endif
	    );
	printf("interrupt exception handler:\t%d byte\n",
	    sh_vector_interrupt_end - sh_vector_interrupt);
#endif /* DEBUG */

	printf("real mem = %lu (%luMB)\n", ptoa(physmem),
	    ptoa(physmem) / 1024 / 1024);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free) / 1024 / 1024);

	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif 
	}
}

void
dumpconf(void)
{
	cpu_kcore_hdr_t *h = &cpu_kcore_hdr;
	u_int dumpextra, totaldumpsize;		/* in disk blocks */
	u_int seg, nblks;

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpsize = 0;
	for (seg = 0; seg < h->kcore_nsegs; seg++)
		dumpsize += atop(h->kcore_segs[seg].size);
	dumpextra = cpu_dumpsize();

	/* Always skip the first block, in case there is a label there. */
	if (dumplo < btodb(1))
		dumplo = btodb(1);

	/* Put dump at the end of the partition, and make it fit. */
	totaldumpsize = ctod(dumpsize) + dumpextra;
	if (totaldumpsize > nblks - dumplo) {
		totaldumpsize = dbtob(nblks - dumplo);
		dumpsize = dtoc(totaldumpsize - dumpextra);
	}
	if (dumplo < nblks - totaldumpsize)
		dumplo = nblks - totaldumpsize;
}

void
dumpsys(void)
{
	cpu_kcore_hdr_t *h = &cpu_kcore_hdr;
	daddr_t blkno;
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	u_int page = 0;
	paddr_t dumppa;
	u_int seg;
	int rc;
	extern int msgbufmapped;

	/* Don't record dump messages in msgbuf. */
	msgbufmapped = 0;

	/* Make sure dump settings are valid. */
	if (dumpdev == NODEV)
		return;
	if (dumpsize == 0) {
		dumpconf();
		if (dumpsize == 0)
			return;
	}
	if (dumplo <= 0) {
		printf("\ndump to dev 0x%x not possible, not enough space\n",
		    dumpdev);
		return;
	}

	dump = bdevsw[major(dumpdev)].d_dump;
	blkno = dumplo;

	printf("\ndumping to dev 0x%x offset %ld\n", dumpdev, dumplo);

	printf("dump ");

	/* Write dump header */
	rc = cpu_dump(dump, &blkno);
	if (rc != 0)
		goto bad;

	for (seg = 0; seg < h->kcore_nsegs; seg++) {
		u_int pagesleft;

		pagesleft = atop(h->kcore_segs[seg].size);
		dumppa = (paddr_t)h->kcore_segs[seg].start;

		while (pagesleft != 0) {
			u_int npages;

#define	NPGMB	atop(1024 * 1024)
			if (page != 0 && (page % NPGMB) == 0)
				printf("%u ", page / NPGMB);

			/* do not dump more than 1MB at once */
			npages = min(pagesleft, NPGMB);
#undef NPGMB
			npages = min(npages, dumpsize);

			rc = (*dump)(dumpdev, blkno,
			    (caddr_t)SH3_PHYS_TO_P2SEG(dumppa), ptoa(npages));
			if (rc != 0)
				goto bad;

			pagesleft -= npages;
			dumppa += ptoa(npages);
			page += npages;
			dumpsize -= npages;
			if (dumpsize == 0)
				goto bad;	/* if truncated dump */
			blkno += ctod(npages);
		}
	}
bad:
	switch (rc) {
	case 0:
		printf("succeeded\n");
		break;
	case ENXIO:
		printf("device bad\n");
		break;
	case EFAULT:
		printf("device not ready\n");
		break;
	case EINVAL:
		printf("area improper\n");
		break;
	case EIO:
		printf("I/O error\n");
		break;
	case EINTR:
		printf("aborted\n");
		break;
	default:
		printf("error %d\n", rc);
		break;
	}

	/* make sure console can output our last message */
	delay(1 * 1000 * 1000);
}

/*
 * Signal frame.
 */
struct sigframe {
#if 0 /* in registers on entry to signal trampoline */
	int		sf_signum;	/* r4 - "signum" argument for handler */
	siginfo_t	*sf_sip;	/* r5 - "sip" argument for handler */
	struct sigcontext *sf_ucp;	/* r6 - "ucp" argument for handler */
#endif
	struct sigcontext sf_uc;	/* actual context */		
	siginfo_t	sf_si;
};

/*
 * Send an interrupt to process.
 */
int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct sigframe *fp, frame;
	struct trapframe *tf = p->p_md.md_regs;
	siginfo_t *sip;

	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(p->p_md.md_regs->tf_r15) &&
	    onstack)
		fp = (struct sigframe *)
		    trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size);
	else
		fp = (void *)p->p_md.md_regs->tf_r15;
	--fp;


	bzero(&frame, sizeof(frame));

	if (info) {
		frame.sf_si = *ksip;
		sip = &fp->sf_si;
	} else
		sip = NULL;

	/* Save register context. */
	memcpy(frame.sf_uc.sc_reg, &tf->tf_spc, sizeof(frame.sf_uc.sc_reg));
#ifdef SH4
	if (CPU_IS_SH4)
		fpu_save((struct fpreg *)&frame.sf_uc.sc_fpreg);
#endif

	frame.sf_uc.sc_expevt = tf->tf_expevt;
	/* frame.sf_uc.sc_err = 0; */
	frame.sf_uc.sc_mask = mask;

	frame.sf_uc.sc_cookie = (long)&fp->sf_uc ^ p->p_p->ps_sigcookie;
	if (copyout(&frame, fp, sizeof(frame)) != 0)
		return 1;

	tf->tf_r4 = sig;		/* "signum" argument for handler */
	tf->tf_r5 = (int)sip;		/* "sip" argument for handler */
	tf->tf_r6 = (int)&fp->sf_uc;	/* "ucp" argument for handler */
 	tf->tf_spc = (int)catcher;
	tf->tf_r15 = (int)fp;
	tf->tf_pr = (int)p->p_p->ps_sigcode;

	return 0;
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * psl to gain improper privileges or to cause
 * a machine fault.
 */
int
sys_sigreturn(struct proc *p, void *v, register_t *retval)
{
	struct sys_sigreturn_args /* {
		syscallarg(struct sigcontext *) sigcntxp;
	} */ *uap = v;
	struct sigcontext ksc, *scp = SCARG(uap, sigcntxp);
	struct trapframe *tf;
	int error;

	if (PROC_PC(p) != p->p_p->ps_sigcoderet) {
		sigexit(p, SIGILL);
		return (EPERM);
	}

	if ((error = copyin(scp, &ksc, sizeof(*scp))) != 0)
		return (error);

	if (ksc.sc_cookie != ((long)scp ^ p->p_p->ps_sigcookie)) {
		sigexit(p, SIGILL);
		return (EFAULT);
	}

	/* Prevent reuse of the sigcontext cookie */
	ksc.sc_cookie = 0;
	(void)copyout(&ksc.sc_cookie, (caddr_t)scp +
	    offsetof(struct sigcontext, sc_cookie), sizeof(ksc.sc_cookie));

	/* Restore signal context. */
	tf = p->p_md.md_regs;

	/* Check for security violations. */
	if (((ksc.sc_reg[1] /* ssr */ ^ tf->tf_ssr) & PSL_USERSTATIC) != 0)
		return (EINVAL);

	memcpy(&tf->tf_spc, ksc.sc_reg, sizeof(ksc.sc_reg));

#ifdef SH4
	if (CPU_IS_SH4)
		fpu_restore((struct fpreg *)&ksc.sc_fpreg);
#endif

	/* Restore signal mask. */
	p->p_sigmask = ksc.sc_mask & ~sigcantmask;

	return (EJUSTRETURN);
}

/*
 * Clear registers on exec
 */
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct trapframe *tf;
	struct pcb *pcb = p->p_md.md_pcb;

	p->p_md.md_flags &= ~MDP_USEDFPU;

	tf = p->p_md.md_regs;

	tf->tf_gbr = 0;
	tf->tf_macl = 0;
	tf->tf_mach = 0;

	tf->tf_r0 = 0;
	tf->tf_r1 = 0;
	tf->tf_r2 = 0;
	tf->tf_r3 = 0;
	tf->tf_r4 = arginfo->ps_nargvstr;
	tf->tf_r5 = (register_t)arginfo->ps_argvstr;
	tf->tf_r6 = (register_t)arginfo->ps_envstr;
	tf->tf_r7 = 0;
	tf->tf_r8 = 0;
	tf->tf_r9 = (int)p->p_p->ps_strings;
	tf->tf_r10 = 0;
	tf->tf_r11 = 0;
	tf->tf_r12 = 0;
	tf->tf_r13 = 0;
	tf->tf_r14 = 0;
	tf->tf_spc = pack->ep_entry;
	tf->tf_ssr = PSL_USERSET;
	tf->tf_r15 = stack;

#ifdef SH4
	if (CPU_IS_SH4) {
		/*
		 * Clear floating point registers.
		 */
		bzero(&pcb->pcb_fp, sizeof(pcb->pcb_fp));
		pcb->pcb_fp.fpr_fpscr = FPSCR_PR;
		fpu_restore(&pcb->pcb_fp);
	}
#endif
}

/*
 * Jump to reset vector.
 */
void
cpu_reset(void)
{
	_cpu_exception_suspend();
	_reg_write_4(SH_(EXPEVT), EXPEVT_RESET_MANUAL);

#ifndef __lint__
	goto *(void *)0xa0000000;
#endif
	/* NOTREACHED */
}
