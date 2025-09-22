/*	$OpenBSD: machdep.c,v 1.218 2024/05/22 05:51:49 jsg Exp $	*/
/*	$NetBSD: machdep.c,v 1.108 2001/07/24 19:30:14 eeh Exp $ */

/*-
 * Copyright (c) 1996, 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)machdep.c	8.6 (Berkeley) 1/14/94
 */

#include <sys/param.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/reboot.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/exec.h>

#include <net/if.h>

#include <sys/sysctl.h>
#include <sys/exec_elf.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/frame.h>
#include <machine/cpu.h>
#include <machine/pmap.h>
#include <machine/openfirm.h>
#include <machine/sparc64.h>

#include "pckbc.h"
#include "pckbd.h"
#if (NPCKBC > 0) && (NPCKBD == 0)
#include <dev/ic/pckbcvar.h>
#endif

int     _bus_dmamap_create(bus_dma_tag_t, bus_dma_tag_t, bus_size_t, int,
	    bus_size_t, bus_size_t, int, bus_dmamap_t *);
void    _bus_dmamap_destroy(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
int     _bus_dmamap_load(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t, void *,
            bus_size_t, struct proc *, int);
int     _bus_dmamap_load_mbuf(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
            struct mbuf *, int);
int     _bus_dmamap_load_uio(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
            struct uio *, int);
int     _bus_dmamap_load_raw(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
            bus_dma_segment_t *, int, bus_size_t, int);
int	_bus_dmamap_load_buffer(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int, bus_addr_t *, int *, int);

void    _bus_dmamap_unload(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t);
void    _bus_dmamap_sync(bus_dma_tag_t, bus_dma_tag_t, bus_dmamap_t,
	    bus_addr_t, bus_size_t, int);

int     _bus_dmamem_alloc(bus_dma_tag_t, bus_dma_tag_t tag, bus_size_t size,
            bus_size_t alignment, bus_size_t boundary,
            bus_dma_segment_t *segs, int nsegs, int *rsegs, int flags);

void    _bus_dmamem_free(bus_dma_tag_t tag, bus_dma_tag_t,
	    bus_dma_segment_t *segs, int nsegs);
int     _bus_dmamem_map(bus_dma_tag_t tag, bus_dma_tag_t,
	    bus_dma_segment_t *segs, int nsegs, size_t size, caddr_t *kvap,
	    int flags);
void    _bus_dmamem_unmap(bus_dma_tag_t tag, bus_dma_tag_t, caddr_t kva,
            size_t size);
paddr_t _bus_dmamem_mmap(bus_dma_tag_t tag, bus_dma_tag_t,
	    bus_dma_segment_t *segs, int nsegs, off_t off, int prot, int flags);

/*
 * The "bus_space_debug" flags used by macros elsewhere.
 * A good set of flags to use when first debugging something is:
 * int bus_space_debug = BSDB_ACCESS | BSDB_ASSERT | BSDB_MAP;
 */
int bus_space_debug = 0;

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

int	physmem;

int sparc_led_blink = 1;

#ifdef APERTURE
int allowaperture = 0;
#endif

extern int ceccerrs;
extern int64_t cecclast;

/*
 * Maximum number of DMA segments we'll allow in dmamem_load()
 * routines.  Can be overridden in config files, etc.
 */
#ifndef MAX_DMA_SEGS
#define MAX_DMA_SEGS	20
#endif

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = 0;

void blink_led_timeout(void *);
void	dumpsys(void);
void	stackdump(void);

/*
 * Machine-dependent startup code
 */
void
cpu_startup(void)
{
#ifdef DEBUG
	extern int pmapdebug;
	int opmapdebug = pmapdebug;
#endif
	vaddr_t minaddr, maxaddr;
	extern struct user *proc0paddr;

#ifdef DEBUG
	pmapdebug = 0;
#endif

	proc0.p_addr = proc0paddr;
	(void)pmap_extract(pmap_kernel(), (vaddr_t)proc0paddr,
	    &proc0.p_md.md_pcbpaddr);

	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	printf("%s", version);
	printf("real mem = %lu (%luMB)\n", ptoa((psize_t)physmem),
	    ptoa((psize_t)physmem)/1024/1024);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16*NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	minaddr = vm_map_min(kernel_map);
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

#ifdef DEBUG
	pmapdebug = opmapdebug;
#endif
	printf("avail mem = %lu (%luMB)\n", ptoa((psize_t)uvmexp.free),
	    ptoa((psize_t)uvmexp.free)/1024/1024);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();
}

/*
 * Set up registers on exec.
 */

#define CPOUTREG(l,v)	copyout(&(v), (l), sizeof(v))

void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct trapframe *tf = p->p_md.md_tf;
	int64_t tstate;
	int pstate = PSTATE_USER;
	Elf_Ehdr *eh = pack->ep_hdr;

	/* 
	 * Setup the process StackGhost cookie which will be XORed into
	 * the return pointer as register windows are over/underflowed.
	 */
	arc4random_buf(&p->p_addr->u_pcb.pcb_wcookie,
	    sizeof(p->p_addr->u_pcb.pcb_wcookie));

	/* The cookie needs to guarantee invalid alignment after the XOR. */
	switch (p->p_addr->u_pcb.pcb_wcookie % 3) {
	case 0: /* Set two lsbs. */
		p->p_addr->u_pcb.pcb_wcookie |= 0x3;
		break;
	case 1: /* Set the lsb. */
		p->p_addr->u_pcb.pcb_wcookie = 1 |
		    (p->p_addr->u_pcb.pcb_wcookie & ~0x3);
		break;
	case 2: /* Set the second most lsb. */
		p->p_addr->u_pcb.pcb_wcookie = 2 |
		    (p->p_addr->u_pcb.pcb_wcookie & ~0x3);
		break;
	}

	/*
	 * Set the registers to 0 except for:
	 *	%o6: stack pointer, built in exec())
	 *	%tstate: (retain icc and xcc and cwp bits)
	 *	%tpc,%tnpc: entry point of program
	 */
	/* Check what memory model is requested */
	switch ((eh->e_flags & EF_SPARCV9_MM)) {
	default:
		printf("Unknown memory model %d\n", 
		       (eh->e_flags & EF_SPARCV9_MM));
		/* FALLTHROUGH */
	case EF_SPARCV9_TSO:
		pstate = PSTATE_MM_TSO|PSTATE_IE;
		break;
	case EF_SPARCV9_PSO:
		pstate = PSTATE_MM_PSO|PSTATE_IE;
		break;
	case EF_SPARCV9_RMO:
		pstate = PSTATE_MM_RMO|PSTATE_IE;
		break;
	}

	tstate = ((u_int64_t)ASI_PRIMARY_NO_FAULT << TSTATE_ASI_SHIFT) |
	    (pstate << TSTATE_PSTATE_SHIFT) | (tf->tf_tstate & TSTATE_CWP);
	if (p->p_md.md_fpstate != NULL) {
		/*
		 * We hold an FPU state.  If we own *the* FPU chip state
		 * we must get rid of it, and the only way to do that is
		 * to save it.  In any case, get rid of our FPU state.
		 */
		fpusave_proc(p, 0);
		free(p->p_md.md_fpstate, M_SUBPROC, sizeof(struct fpstate));
		p->p_md.md_fpstate = NULL;
	}
	memset(tf, 0, sizeof *tf);
	tf->tf_tstate = tstate;
	tf->tf_pc = pack->ep_entry & ~3;
	tf->tf_npc = tf->tf_pc + 4;
	stack -= sizeof(struct rwindow);
	tf->tf_out[6] = stack - BIAS;
#ifdef NOTDEF_DEBUG
	printf("setregs: setting tf %p sp %p pc %p\n", (long)tf, 
	       (long)tf->tf_out[6], (long)tf->tf_pc);
#endif
}

struct sigframe {
	int	sf_signo;		/* signal number */
	int	sf_code;		/* signal code (unused) */
	siginfo_t *sf_sip;		/* points to siginfo_t */
	struct	sigcontext sf_sc;	/* actual sigcontext */
	siginfo_t sf_si;
};

/*
 * machine dependent system variables.
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	int oldval, ret;

	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR);	/* overloaded */

	switch (name[0]) {
	case CPU_LED_BLINK:
		oldval = sparc_led_blink;
		ret = sysctl_int(oldp, oldlenp, newp, newlen,
		    &sparc_led_blink);
		/*
		 * If we were false and are now true, start the timer.
		 */
		if (!oldval && sparc_led_blink > oldval)
			blink_led_timeout(NULL);
		return (ret);
	case CPU_ALLOWAPERTURE:
#ifdef APERTURE
		if (securelevel > 0)
			return (sysctl_int_lower(oldp, oldlenp, newp, newlen,
			    &allowaperture));
		else
			return (sysctl_int(oldp, oldlenp, newp, newlen,
			    &allowaperture));
#else
		return (sysctl_rdint(oldp, oldlenp, newp, 0));
#endif
	case CPU_CPUTYPE:
		return (sysctl_rdint(oldp, oldlenp, newp, cputyp));
	case CPU_CECCERRORS:
		return (sysctl_rdint(oldp, oldlenp, newp, ceccerrs));
	case CPU_CECCLAST:
		return (sysctl_rdquad(oldp, oldlenp, newp, cecclast));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * Send an interrupt to process.
 */
int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct sigframe *fp;
	struct trapframe *tf;
	vaddr_t addr, oldsp, newsp;
	struct sigframe sf;

	tf = p->p_md.md_tf;
	oldsp = tf->tf_out[6] + BIAS;

	/*
	 * Compute new user stack addresses, subtract off
	 * one signal frame, and align.
	 */
	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(oldsp) && onstack)
		fp = (struct sigframe *)
		    trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size);
	else
		fp = (struct sigframe *)oldsp;
	/* Allocate an aligned sigframe */
	fp = (struct sigframe *)((long)(fp - 1) & ~0x0f);

	/*
	 * Now set up the signal frame.  We build it in kernel space
	 * and then copy it out.  We probably ought to just build it
	 * directly in user space....
	 */
	bzero(&sf, sizeof(sf));
	sf.sf_signo = sig;
	sf.sf_sip = NULL;

	/*
	 * Build the signal context to be used by sigreturn.
	 */
	sf.sf_sc.sc_mask = mask;
	/* Save register context. */
	sf.sf_sc.sc_sp = (long)tf->tf_out[6];
	sf.sf_sc.sc_pc = tf->tf_pc;
	sf.sf_sc.sc_npc = tf->tf_npc;
	sf.sf_sc.sc_tstate = tf->tf_tstate; /* XXX */
	sf.sf_sc.sc_g1 = tf->tf_global[1];
	sf.sf_sc.sc_o0 = tf->tf_out[0];

	if (info) {
		sf.sf_sip = &fp->sf_si;
		sf.sf_si = *ksip;
	}

	/*
	 * Put the stack in a consistent state before we whack away
	 * at it.  Note that write_user_windows may just dump the
	 * registers into the pcb; we need them in the process's memory.
	 * We also need to make sure that when we start the signal handler,
	 * its %i6 (%fp), which is loaded from the newly allocated stack area,
	 * joins seamlessly with the frame it was in when the signal occurred,
	 * so that the debugger and _longjmp code can back up through it.
	 */
	newsp = (vaddr_t)fp - sizeof(struct rwindow);
	write_user_windows();

	sf.sf_sc.sc_cookie = (long)&fp->sf_sc ^ p->p_p->ps_sigcookie;
	if (rwindow_save(p) || copyout((caddr_t)&sf, (caddr_t)fp, sizeof sf) || 
	    CPOUTREG(&(((struct rwindow *)newsp)->rw_in[6]), tf->tf_out[6])) {
		/*
		 * Process has trashed its stack; give it an illegal
		 * instruction to halt it in its tracks.
		 */
#ifdef DEBUG
		printf("sendsig: stack was trashed trying to send sig %d, "
		    "sending SIGILL\n", sig);
#endif
		return 1;
	}

	/*
	 * Arrange to continue execution at the code copied out in exec().
	 * It needs the function to call in %g1, and a new stack pointer.
	 */
	addr = p->p_p->ps_sigcode;
	tf->tf_global[1] = (vaddr_t)catcher;
	tf->tf_pc = addr;
	tf->tf_npc = addr + 4;
	tf->tf_out[6] = newsp - BIAS;

	return 0;
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above),
 * and return to the given trap frame (if there is one).
 * Check carefully to make sure that the user has not
 * modified the state to gain improper privileges or to cause
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
	int error = EINVAL;

	if (PROC_PC(p) != p->p_p->ps_sigcoderet) {
		sigexit(p, SIGILL);
		return (EPERM);
	}

	/* First ensure consistent stack state (see sendsig). */
	write_user_windows();

	if (rwindow_save(p)) {
#ifdef DEBUG
		printf("sigreturn: rwindow_save(%p) failed, sending SIGILL\n",
		    p);
#endif
		sigexit(p, SIGILL);
	}

	if ((vaddr_t)scp & 3)
		return (EINVAL);
	if ((error = copyin((caddr_t)scp, &ksc, sizeof ksc)))
		return (error);

	if (ksc.sc_cookie != ((long)scp ^ p->p_p->ps_sigcookie)) {
		sigexit(p, SIGILL);
		return (EFAULT);
	}

	/* Prevent reuse of the sigcontext cookie */
	ksc.sc_cookie = 0;
	(void)copyout(&ksc.sc_cookie, (caddr_t)scp +
	    offsetof(struct sigcontext, sc_cookie), sizeof (ksc.sc_cookie));

	scp = &ksc;

	tf = p->p_md.md_tf;
	/*
	 * Only the icc bits in the psr are used, so it need not be
	 * verified.  pc and npc must be multiples of 4.  This is all
	 * that is required; if it holds, just do it.
	 */
	if (((ksc.sc_pc | ksc.sc_npc) & 3) != 0 ||
	    (ksc.sc_pc == 0) || (ksc.sc_npc == 0)) {
#ifdef DEBUG
		printf("sigreturn: pc %p or npc %p invalid\n",
		   (void *)(unsigned long)ksc.sc_pc,
		   (void *)(unsigned long)ksc.sc_npc);
#endif
		return (EINVAL);
	}

	/* take only psr ICC field */
	tf->tf_tstate = (u_int64_t)(tf->tf_tstate & ~TSTATE_CCR) | (scp->sc_tstate & TSTATE_CCR);
	tf->tf_pc = (u_int64_t)scp->sc_pc;
	tf->tf_npc = (u_int64_t)scp->sc_npc;
	tf->tf_global[1] = (u_int64_t)scp->sc_g1;
	tf->tf_out[0] = (u_int64_t)scp->sc_o0;
	tf->tf_out[6] = (u_int64_t)scp->sc_sp;

	/* Restore signal mask. */
	p->p_sigmask = scp->sc_mask & ~sigcantmask;

	return (EJUSTRETURN);
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

int	waittime = -1;
struct pcb dumppcb;

__dead void
boot(int howto)
{
	int i;
	static char str[128];

	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	fb_unblank();
	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0 && waittime < 0) {
		waittime = 0;
		vfs_shutdown(curproc);

		if ((howto & RB_TIMEBAD) == 0) {
			resettodr();
		} else {
			printf("WARNING: not updating battery clock\n");
		}
	}
	if_downall();

	uvm_shutdown();
	splhigh();
	cold = 1;

	if ((howto & RB_DUMP) != 0)
		dumpsys();

haltsys:
	config_suspend_all(DVACT_POWERDOWN);

	/* If powerdown was requested, do it. */
	if ((howto & RB_POWERDOWN) != 0) {
		/* Let the OBP do the work. */
		OF_poweroff();
		printf("WARNING: powerdown failed!\n");
		/*
		 * RB_POWERDOWN implies RB_HALT... fall into it...
		 */
	}

	if ((howto & RB_HALT) != 0) {
		printf("halted\n\n");
		OF_exit();
		panic("PROM exit failed");
	}

doreset:
	printf("rebooting\n\n");
#if 0
	if (user_boot_string && *user_boot_string) {
		i = strlen(user_boot_string);
		if (i > sizeof(str))
			OF_boot(user_boot_string);	/* XXX */
		bcopy(user_boot_string, str, i);
	} else
#endif
	{
		i = 1;
		str[0] = '\0';
	}

	if ((howto & RB_SINGLE) != 0)
		str[i++] = 's';
	if ((howto & RB_KDB) != 0)
		str[i++] = 'd';
	if (i > 1) {
		if (str[0] == '\0')
			str[0] = '-';
		str[i] = 0;
	} else
		str[0] = 0;
	OF_boot(str);
	panic("cpu_reboot -- failed");
	for (;;)
		continue;
	/* NOTREACHED */
}

u_long	dumpmag = 0x8fca0101;	/* magic number for savecore */
int	dumpsize = 0;		/* also for savecore */
long	dumplo = 0;

void
dumpconf(void)
{
	int nblks, dumpblks;

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpblks = ctod(physmem) + pmap_dumpsize();
	if (dumpblks > (nblks - ctod(1)))
		/*
		 * dump size is too big for the partition.
		 * Note, we safeguard a click at the front for a
		 * possible disk label.
		 */
		return;

	/* Put the dump at the end of the partition */
	dumplo = nblks - dumpblks;

	/*
	 * savecore(8) expects dumpsize to be the number of pages
	 * of actual core dumped (i.e. excluding the MMU stuff).
	 */
	dumpsize = physmem;
}

#define	BYTES_PER_DUMP	(NBPG)	/* must be a multiple of pagesize */
static vaddr_t dumpspace;

caddr_t
reserve_dumppages(caddr_t p)
{

	dumpspace = (vaddr_t)p;
	return (p + BYTES_PER_DUMP);
}

/*
 * Write a crash dump.
 */
void
dumpsys(void)
{
	int psize;
	daddr_t blkno;
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int error = 0;
	struct mem_region *mp;
	extern struct mem_region *mem;

	/* copy registers to memory */
	snapshot(&dumppcb);
	stackdump();

	if (dumpdev == NODEV)
		return;

	/*
	 * For dumps during autoconfiguration,
	 * if dump device has already configured...
	 */
	if (dumpsize == 0)
		dumpconf();
	if (!dumpspace) {
		printf("\nno address space available, dump not possible\n");
		return;
	}
	if (dumplo <= 0) {
		printf("\ndump to dev %u,%u not possible\n", major(dumpdev),
		    minor(dumpdev));
		return;
	}
	printf("\ndumping to dev %u,%u offset %ld\n", major(dumpdev),
	    minor(dumpdev), dumplo);

	psize = (*bdevsw[major(dumpdev)].d_psize)(dumpdev);
	printf("dump ");
	if (psize == -1) {
		printf("area unavailable\n");
		return;
	}
	blkno = dumplo;
	dump = bdevsw[major(dumpdev)].d_dump;

	error = pmap_dumpmmu(dump, blkno);
	blkno += pmap_dumpsize();
printf("starting dump, blkno %lld\n", (long long)blkno);
	for (mp = mem; mp->size; mp++) {
		u_int64_t i = 0, n;
		paddr_t maddr = mp->start;

#if 0
		/* Remind me: why don't we dump page 0 ? */
		if (maddr == 0) {
			/* Skip first page at physical address 0 */
			maddr += NBPG;
			i += NBPG;
			blkno += btodb(NBPG);
		}
#endif
		for (; i < mp->size; i += n) {
			n = mp->size - i;
			if (n > BYTES_PER_DUMP)
				n = BYTES_PER_DUMP;

			/* print out how many MBs we have dumped */
			if (i && (i % (1024*1024)) == 0)
				printf("%lld ", i / (1024*1024));
			(void) pmap_enter(pmap_kernel(), dumpspace, maddr,
			    PROT_READ, PROT_READ | PMAP_WIRED);
			pmap_update(pmap_kernel());
			error = (*dump)(dumpdev, blkno,
					(caddr_t)dumpspace, (int)n);
			pmap_remove(pmap_kernel(), dumpspace, dumpspace + n);
			pmap_update(pmap_kernel());
			if (error)
				break;
			maddr += n;
			blkno += btodb(n);
		}
	}

	switch (error) {

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
		printf("i/o error\n");
		break;

	case 0:
		printf("succeeded\n");
		break;

	default:
		printf("error %d\n", error);
		break;
	}
}

/*
 * get the fp and dump the stack as best we can.  don't leave the
 * current stack page
 */
void
stackdump(void)
{
	struct frame *sfp = getfp(), *fp64;

	fp64 = sfp = v9next_frame(sfp);
	printf("Frame pointer is at %p\n", fp64);
	printf("Call traceback:\n");
	while (fp64 && ((u_long)fp64 >> PGSHIFT) == ((u_long)sfp >> PGSHIFT)) {
		printf("%llx(%llx, %llx, %llx, %llx, %llx, %llx) "
		    "fp = %llx\n",
		       (unsigned long long)fp64->fr_pc,
		       (unsigned long long)fp64->fr_arg[0],
		       (unsigned long long)fp64->fr_arg[1],
		       (unsigned long long)fp64->fr_arg[2],
		       (unsigned long long)fp64->fr_arg[3],
		       (unsigned long long)fp64->fr_arg[4],
		       (unsigned long long)fp64->fr_arg[5],	
		       (unsigned long long)fp64->fr_fp);
		fp64 = v9next_frame(fp64);
	}
}


/*
 * Common function for DMA map creation.  May be called by bus-specific
 * DMA map creation functions.
 */
int
_bus_dmamap_create(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    int nsegments, bus_size_t maxsegsz, bus_size_t boundary, int flags,
    bus_dmamap_t *dmamp)
{
	struct sparc_bus_dmamap *map;
	void *mapstore;
	size_t mapsize;

	/*
	 * Allocate and initialize the DMA map.  The end of the map
	 * is a variable-sized array of segments, so we allocate enough
	 * room for them in one shot.
	 *
	 * Note we don't preserve the WAITOK or NOWAIT flags.  Preservation
	 * of ALLOCNOW notifies others that we've reserved these resources,
	 * and they are not to be freed.
	 *
	 * The bus_dmamap_t includes one bus_dma_segment_t, hence
	 * the (nsegments - 1).
	 */
	mapsize = sizeof(struct sparc_bus_dmamap) +
	    (sizeof(bus_dma_segment_t) * (nsegments - 1));
	if ((mapstore = malloc(mapsize, M_DEVBUF, (flags & BUS_DMA_NOWAIT) ?
	    (M_NOWAIT | M_ZERO) : (M_WAITOK | M_ZERO))) == NULL)
		return (ENOMEM);

	map = (struct sparc_bus_dmamap *)mapstore;
	map->_dm_size = size;
	map->_dm_segcnt = nsegments;
	map->_dm_maxsegsz = maxsegsz;
	map->_dm_boundary = boundary;
	map->_dm_flags = flags & ~(BUS_DMA_WAITOK | BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_NOWRITE | BUS_DMA_NOCACHE);
	map->dm_mapsize = 0;		/* no valid mappings */
	map->dm_nsegs = 0;

	*dmamp = map;
	return (0);
}

/*
 * Common function for DMA map destruction.  May be called by bus-specific
 * DMA map destruction functions.
 */
void
_bus_dmamap_destroy(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map)
{
	/*
	 * Unload the map if it is still loaded.  This is required
	 * by the specification (well, the manpage).  Higher level
	 * drivers, if any, should do this too.  By the time the
	 * system gets here, the higher level "destroy" functions
	 * would probably already have clobbered the data needed
	 * to do a proper unload.
	 */
	if (map->dm_nsegs)
		bus_dmamap_unload(t0, map);

	free(map, M_DEVBUF, 0);
}

/*
 * Common function for loading a DMA map with a linear buffer.  May
 * be called by bus-specific DMA map load functions.
 *
 * Most SPARCs have IOMMUs in the bus controllers.  In those cases
 * they only need one segment and will use virtual addresses for DVMA.
 * Those bus controllers should intercept these vectors and should
 * *NEVER* call _bus_dmamap_load() which is used only by devices that
 * bypass DVMA.
 */
int
_bus_dmamap_load(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags)
{
	bus_addr_t lastaddr;
	int seg, error;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

	if (buflen > map->_dm_size)
		return (EINVAL);

	seg = 0;
	error = _bus_dmamap_load_buffer(t, map, buf, buflen, p, flags,
	    &lastaddr, &seg, 1);
	if (error == 0) {
		map->dm_mapsize = buflen;
		map->dm_nsegs = seg + 1;
	}
	return (error);
}

/*
 * Like _bus_dmamap_load(), but for mbufs.
 */
int
_bus_dmamap_load_mbuf(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    struct mbuf *m, int flags)
{
	bus_dma_segment_t segs[MAX_DMA_SEGS];
	int i;
	size_t len;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
 	map->dm_mapsize = 0;
 	map->dm_nsegs = 0;

	if (m->m_pkthdr.len > map->_dm_size)
		return (EINVAL);

	/* Record mbuf for *_unload */
	map->_dm_type = _DM_TYPE_MBUF;
	map->_dm_source = m;

	i = 0;
	len = 0;
	while (m) {
		vaddr_t vaddr = mtod(m, vaddr_t);
		long buflen = (long)m->m_len;

		len += buflen;
		while (buflen > 0 && i < MAX_DMA_SEGS) {
			paddr_t pa;
			long incr;

			incr = min(buflen,
			    PAGE_SIZE - ((u_long)vaddr & PGOFSET));

			if (pmap_extract(pmap_kernel(), vaddr, &pa) == FALSE) {
#ifdef DIAGNOSTIC
				printf("_bus_dmamap_load_mbuf: pmap_extract failed %lx\n",
					vaddr);
				map->_dm_type = 0;
				map->_dm_source = NULL;
#endif
				return EINVAL;
			}

			buflen -= incr;
			vaddr += incr;

			if (i > 0 && pa == (segs[i - 1].ds_addr +
			    segs[i - 1].ds_len) && ((segs[i - 1].ds_len + incr)
			    < map->_dm_maxsegsz)) {
				/* Hey, waddyaknow, they're contiguous */
				segs[i - 1].ds_len += incr;
				continue;
			}
			segs[i].ds_addr = pa;
			segs[i].ds_len = incr;
			segs[i]._ds_boundary = 0;
			segs[i]._ds_align = 0;
			segs[i]._ds_mlist = NULL;
			i++;
		}
		m = m->m_next;
		if (m && i >= MAX_DMA_SEGS) {
			/* Exceeded the size of our dmamap */
			map->_dm_type = 0;
			map->_dm_source = NULL;
			return (EFBIG);
		}
	}

	return (bus_dmamap_load_raw(t0, map, segs, i,
			    (bus_size_t)len, flags));
}

/*
 * Like _bus_dmamap_load(), but for uios.
 */
int
_bus_dmamap_load_uio(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    struct uio *uio, int flags)
{
	/*
	 * XXXXXXX The problem with this routine is that it needs to 
	 * lock the user address space that is being loaded, but there
	 * is no real way for us to unlock it during the unload process.
	 * As a result, only UIO_SYSSPACE uio's are allowed for now.
	 */
	bus_dma_segment_t segs[MAX_DMA_SEGS];
	int i, j;
	size_t len;

	/*
	 * Make sure that on error condition we return "no valid mappings".
	 */
 	map->dm_mapsize = 0;
 	map->dm_nsegs = 0;

	if (uio->uio_resid > map->_dm_size)
		return (EINVAL);

	if (uio->uio_segflg != UIO_SYSSPACE)
		return (EOPNOTSUPP);

	/* Record for *_unload */
	map->_dm_type = _DM_TYPE_UIO;
	map->_dm_source = (void *)uio;

	i = j = 0;
	len = 0;
	while (j < uio->uio_iovcnt) {
		vaddr_t vaddr = (vaddr_t)uio->uio_iov[j].iov_base;
		long buflen = (long)uio->uio_iov[j].iov_len;

		len += buflen;
		while (buflen > 0 && i < MAX_DMA_SEGS) {
			paddr_t pa;
			long incr;

			incr = min(buflen,
			    PAGE_SIZE - ((u_long)vaddr & PGOFSET));

			(void) pmap_extract(pmap_kernel(), vaddr, &pa);
			buflen -= incr;
			vaddr += incr;

			if (i > 0 && pa == (segs[i - 1].ds_addr +
			    segs[i - 1].ds_len) && ((segs[i - 1].ds_len + incr)
			    < map->_dm_maxsegsz)) {
				/* Hey, waddyaknow, they're contiguous */
				segs[i - 1].ds_len += incr;
				continue;
			}
			segs[i].ds_addr = pa;
			segs[i].ds_len = incr;
			segs[i]._ds_boundary = 0;
			segs[i]._ds_align = 0;
			segs[i]._ds_mlist = NULL;
			i++;
		}
		j++;
		if ((uio->uio_iovcnt - j) && i >= MAX_DMA_SEGS) {
			/* Exceeded the size of our dmamap */
			map->_dm_type = 0;
			map->_dm_source = NULL;
			return (EFBIG);
		}
	}

	return (bus_dmamap_load_raw(t0, map, segs, i, (bus_size_t)len, flags));
}

/*
 * Like _bus_dmamap_load(), but for raw memory allocated with
 * bus_dmamem_alloc().
 */
int
_bus_dmamap_load_raw(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{

	panic("_bus_dmamap_load_raw: not implemented");
}

int
_bus_dmamap_load_buffer(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags, bus_addr_t *lastaddrp,
    int *segp, int first)
{
	bus_size_t sgsize;
	bus_addr_t curaddr, lastaddr, baddr, bmask;
	vaddr_t vaddr = (vaddr_t)buf;
	int seg;
	pmap_t pmap;

	if (p != NULL)
		pmap = p->p_vmspace->vm_map.pmap;
	else
		pmap = pmap_kernel();

	lastaddr = *lastaddrp;
	bmask  = ~(map->_dm_boundary - 1);

	for (seg = *segp; buflen > 0 ; ) {
		/*
		 * Get the physical address for this segment.
		 */
		pmap_extract(pmap, vaddr, (paddr_t *)&curaddr);

		/*
		 * Compute the segment size, and adjust counts.
		 */
		sgsize = PAGE_SIZE - ((u_long)vaddr & PGOFSET);
		if (buflen < sgsize)
			sgsize = buflen;

		/*
		 * Make sure we don't cross any boundaries.
		 */
		if (map->_dm_boundary > 0) {
			baddr = (curaddr + map->_dm_boundary) & bmask;
			if (sgsize > (baddr - curaddr))
				sgsize = (baddr - curaddr);
		}

		/*
		 * Insert chunk into a segment, coalescing with
		 * previous segment if possible.
		 */
		if (first) {
			map->dm_segs[seg].ds_addr = curaddr;
			map->dm_segs[seg].ds_len = sgsize;
			first = 0;
		} else {
			if (curaddr == lastaddr &&
			    (map->dm_segs[seg].ds_len + sgsize) <=
			     map->_dm_maxsegsz &&
			    (map->_dm_boundary == 0 ||
			     (map->dm_segs[seg].ds_addr & bmask) ==
			     (curaddr & bmask)))
				map->dm_segs[seg].ds_len += sgsize;
			else {
				if (++seg >= map->_dm_segcnt)
					break;
				map->dm_segs[seg].ds_addr = curaddr;
				map->dm_segs[seg].ds_len = sgsize;
			}
		}

		lastaddr = curaddr + sgsize;
		vaddr += sgsize;
		buflen -= sgsize;
	}

	*segp = seg;
	*lastaddrp = lastaddr;

	/*
	 * Did we fit?
	 */
	if (buflen != 0)
		return (EFBIG);		/* XXX better return value here? */
	return (0);
}

/*
 * Common function for unloading a DMA map.  May be called by
 * bus-specific DMA map unload functions.
 */
void
_bus_dmamap_unload(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map)
{
	/* Mark the mappings as invalid. */
	map->dm_mapsize = 0;
	map->dm_nsegs = 0;

}

/*
 * Common function for DMA map synchronization.  May be called
 * by bus-specific DMA map synchronization functions.
 */
void
_bus_dmamap_sync(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dmamap_t map,
    bus_addr_t offset, bus_size_t len, int ops)
{
	if (ops & (BUS_DMASYNC_PREWRITE | BUS_DMASYNC_POSTREAD))
		__membar("#MemIssue");
}

/*
 * Common function for DMA-safe memory allocation.  May be called
 * by bus-specific DMA memory allocation functions.
 */
int
_bus_dmamem_alloc(bus_dma_tag_t t, bus_dma_tag_t t0, bus_size_t size,
    bus_size_t alignment, bus_size_t boundary, bus_dma_segment_t *segs,
    int nsegs, int *rsegs, int flags)
{
	struct pglist *mlist;
	int error, plaflag;

	/* Always round the size. */
	size = round_page(size);

	if ((mlist = malloc(sizeof(*mlist), M_DEVBUF,
	    (flags & BUS_DMA_NOWAIT) ? M_NOWAIT : M_WAITOK)) == NULL)
		return (ENOMEM);

	/*
	 * If the bus uses DVMA then ignore boundary and alignment.
	 */
	segs[0]._ds_boundary = boundary;
	segs[0]._ds_align = alignment;
	if (flags & BUS_DMA_DVMA) {
		boundary = 0;
		alignment = 0;
	}

	/*
	 * Allocate pages from the VM system.
	 */
	plaflag = flags & BUS_DMA_NOWAIT ? UVM_PLA_NOWAIT : UVM_PLA_WAITOK;
	if (flags & BUS_DMA_ZERO)
		plaflag |= UVM_PLA_ZERO;

	TAILQ_INIT(mlist);
	error = uvm_pglistalloc(size, (paddr_t)0, (paddr_t)-1,
	    alignment, boundary, mlist, nsegs, plaflag);
	if (error)
		return (error);

	/*
	 * Compute the location, size, and number of segments actually
	 * returned by the VM code.
	 */
	segs[0].ds_addr = 0UL; /* UPA does not map things */
	segs[0].ds_len = size;
	*rsegs = 1;

	/*
	 * Simply keep a pointer around to the linked list, so
	 * bus_dmamap_free() can return it.
	 *
	 * NOBODY SHOULD TOUCH THE pageq FIELDS WHILE THESE PAGES
	 * ARE IN OUR CUSTODY.
	 */
	segs[0]._ds_mlist = mlist;

	/* The bus driver should do the actual mapping */
	return (0);
}

/*
 * Common function for freeing DMA-safe memory.  May be called by
 * bus-specific DMA memory free functions.
 */
void
_bus_dmamem_free(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dma_segment_t *segs,
    int nsegs)
{

#ifdef DIAGNOSTIC
	if (nsegs != 1)
		panic("bus_dmamem_free: nsegs = %d", nsegs);
#endif

	/*
	 * Return the list of pages back to the VM system.
	 */
	uvm_pglistfree(segs[0]._ds_mlist);
	free(segs[0]._ds_mlist, M_DEVBUF, 0);
}

/*
 * Common function for mapping DMA-safe memory.  May be called by
 * bus-specific DMA memory map functions.
 */
int
_bus_dmamem_map(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dma_segment_t *segs,
    int nsegs, size_t size, caddr_t *kvap, int flags)
{
	const struct kmem_dyn_mode *kd;
	struct vm_page *m;
	vaddr_t va, sva;
	size_t ssize;
	bus_addr_t addr, cbit;
	struct pglist *mlist;
	int error;

#ifdef DIAGNOSTIC
	if (nsegs != 1)
		panic("_bus_dmamem_map: nsegs = %d", nsegs);
#endif

	size = round_page(size);
	kd = flags & BUS_DMA_NOWAIT ? &kd_trylock : &kd_waitok;
	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, kd);
	if (va == 0)
		return (ENOMEM);

	*kvap = (caddr_t)va;

	cbit = 0;
	if (flags & BUS_DMA_NOCACHE)
		cbit |= PMAP_NC;

	sva = va;
	ssize = size;
	mlist = segs[0]._ds_mlist;
	TAILQ_FOREACH(m, mlist, pageq) {
#ifdef DIAGNOSTIC
		if (size == 0)
			panic("_bus_dmamem_map: size botch");
#endif
		addr = VM_PAGE_TO_PHYS(m);
		error = pmap_enter(pmap_kernel(), va, addr | cbit,
		    PROT_READ | PROT_WRITE,
		    PROT_READ | PROT_WRITE | PMAP_WIRED | PMAP_CANFAIL);
		if (error) {
			pmap_update(pmap_kernel());
			km_free((void *)sva, ssize, &kv_any, &kp_none);
			return (error);
		}
		va += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	pmap_update(pmap_kernel());

	return (0);
}

/*
 * Common function for unmapping DMA-safe memory.  May be called by
 * bus-specific DMA memory unmapping functions.
 */
void
_bus_dmamem_unmap(bus_dma_tag_t t, bus_dma_tag_t t0, caddr_t kva, size_t size)
{

#ifdef DIAGNOSTIC
	if ((u_long)kva & PAGE_MASK)
		panic("_bus_dmamem_unmap");
#endif

	km_free(kva, round_page(size), &kv_any, &kp_none);
}

/*
 * Common function for mmap(2)'ing DMA-safe memory.  May be called by
 * bus-specific DMA mmap(2)'ing functions.
 */
paddr_t
_bus_dmamem_mmap(bus_dma_tag_t t, bus_dma_tag_t t0, bus_dma_segment_t *segs,
    int nsegs, off_t off, int prot, int flags)
{
	int i;

	for (i = 0; i < nsegs; i++) {
#ifdef DIAGNOSTIC
		if (off & PGOFSET)
			panic("_bus_dmamem_mmap: offset unaligned");
		if (segs[i].ds_addr & PGOFSET)
			panic("_bus_dmamem_mmap: segment unaligned");
		if (segs[i].ds_len & PGOFSET)
			panic("_bus_dmamem_mmap: segment size not multiple"
					" of page size");
#endif
		if (off >= segs[i].ds_len) {
			off -= segs[i].ds_len;
			continue;
		}

		return (segs[i].ds_addr + off);
	}

	/* Page not found. */
	return (-1);
}

struct sparc_bus_dma_tag mainbus_dma_tag = {
	NULL,
	NULL,
	_bus_dmamap_create,
	_bus_dmamap_destroy,
	_bus_dmamap_load,
	_bus_dmamap_load_mbuf,
	_bus_dmamap_load_uio,
	_bus_dmamap_load_raw,
	_bus_dmamap_unload,
	_bus_dmamap_sync,

	_bus_dmamem_alloc,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	_bus_dmamem_mmap
};


/*
 * Base bus space handlers.
 */
int sparc_bus_map(bus_space_tag_t, bus_space_tag_t, bus_addr_t, bus_size_t,
    int, bus_space_handle_t *);
int sparc_bus_protect(bus_space_tag_t, bus_space_tag_t, bus_space_handle_t,
    bus_size_t, int);
int sparc_bus_unmap(bus_space_tag_t, bus_space_tag_t, bus_space_handle_t,
    bus_size_t);
bus_addr_t sparc_bus_addr(bus_space_tag_t, bus_space_tag_t,
    bus_space_handle_t);
int sparc_bus_subregion(bus_space_tag_t, bus_space_tag_t,  bus_space_handle_t,
    bus_size_t, bus_size_t, bus_space_handle_t *);
paddr_t sparc_bus_mmap(bus_space_tag_t, bus_space_tag_t, bus_addr_t, off_t,
    int, int);
void *sparc_mainbus_intr_establish(bus_space_tag_t, bus_space_tag_t, int, int,
    int, int (*)(void *), void *, const char *);
int sparc_bus_alloc(bus_space_tag_t, bus_space_tag_t, bus_addr_t, bus_addr_t,
    bus_size_t, bus_size_t, bus_size_t, int, bus_addr_t *,
    bus_space_handle_t *);
void sparc_bus_free(bus_space_tag_t, bus_space_tag_t, bus_space_handle_t,
    bus_size_t);

int
sparc_bus_map(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t	addr,
    bus_size_t size, int flags, bus_space_handle_t *hp)
{
	vaddr_t va;
	u_int64_t pa;
	paddr_t	pm_flags = 0;
	vm_prot_t pm_prot = PROT_READ;

	if (flags & BUS_SPACE_MAP_PROMADDRESS) {
		hp->bh_ptr = addr;
		return (0);
	}

	if (size == 0) {
		char buf[80];
		bus_space_render_tag(t0, buf, sizeof buf);
		printf("\nsparc_bus_map: zero size on %s", buf);
		return (EINVAL);
	}

	if ( (LITTLE_ASI(t0->asi) && LITTLE_ASI(t0->sasi)) ||
	    (PHYS_ASI(t0->asi) != PHYS_ASI(t0->sasi)) ) {
		char buf[80];
		bus_space_render_tag(t0, buf, sizeof buf);
		printf("\nsparc_bus_map: mismatched ASIs on %s: asi=%x sasi=%x",
		    buf, t0->asi, t0->sasi);
	}

	if (PHYS_ASI(t0->asi)) {
#ifdef BUS_SPACE_DEBUG
		char buf[80];
		bus_space_render_tag(t0, buf, sizeof buf);
		BUS_SPACE_PRINTF(BSDB_MAP,
		    ("\nsparc_bus_map: physical tag %s asi %x sasi %x flags %x "
		    "paddr %016llx size %016llx",
		    buf,
		    (int)t0->asi, (int)t0->sasi, (int)flags,
		    (unsigned long long)addr, (unsigned long long)size));
#endif /* BUS_SPACE_DEBUG */
		if (flags & BUS_SPACE_MAP_LINEAR) {
			char buf[80];
			bus_space_render_tag(t0, buf, sizeof buf);
			printf("\nsparc_bus_map: linear mapping requested on physical bus %s", buf);
			return (EINVAL);
		}

		hp->bh_ptr = addr;
		return (0);
	}

	size = round_page(size);

	if (LITTLE_ASI(t0->sasi) && !LITTLE_ASI(t0->asi))
		pm_flags |= PMAP_LITTLE;

	if ((flags & BUS_SPACE_MAP_CACHEABLE) == 0)
		pm_flags |= PMAP_NC;

	va = (vaddr_t)km_alloc(size, &kv_any, &kp_none, &kd_nowait);
	if (va == 0)
		return (ENOMEM);

	/* note: preserve page offset */
	hp->bh_ptr = va | (addr & PGOFSET);

	pa = trunc_page(addr);
	if ((flags & BUS_SPACE_MAP_READONLY) == 0)
		pm_prot |= PROT_WRITE;

#ifdef BUS_SPACE_DEBUG
	{ /* scope */
		char buf[80];
		bus_space_render_tag(t0, buf, sizeof buf);
		BUS_SPACE_PRINTF(BSDB_MAP, ("\nsparc_bus_map: tag %s type %x "
		    "flags %x addr %016llx size %016llx virt %llx paddr "
		    "%016llx", buf, (int)t->default_type, (int) flags,
		    (unsigned long long)addr, (unsigned long long)size,
		    (unsigned long long)hp->bh_ptr, (unsigned long long)pa));
	}
#endif /* BUS_SPACE_DEBUG */

	do {
		BUS_SPACE_PRINTF(BSDB_MAPDETAIL, ("\nsparc_bus_map: phys %llx "
		    "virt %p hp->bh_ptr %llx", (unsigned long long)pa,
		    (char *)v, (unsigned long long)hp->bh_ptr));
		pmap_enter(pmap_kernel(), va, pa | pm_flags, pm_prot,
			pm_prot|PMAP_WIRED);
		va += PAGE_SIZE;
		pa += PAGE_SIZE;
	} while ((size -= PAGE_SIZE) > 0);
	pmap_update(pmap_kernel());
	return (0);
}

int
sparc_bus_subregion(bus_space_tag_t tag, bus_space_tag_t tag0,
    bus_space_handle_t handle, bus_size_t offset, bus_size_t size,
    bus_space_handle_t *nhandlep)
{
	*nhandlep = handle;
	nhandlep->bh_ptr += offset;
	return (0);
}

/* stolen from uvm_chgkprot() */
/*
 * Change protections on kernel pages from addr to addr+len
 * (presumably so debugger can plant a breakpoint).
 *
 * We force the protection change at the pmap level.  If we were
 * to use vm_map_protect a change to allow writing would be lazily-
 * applied meaning we would still take a protection fault, something
 * we really don't want to do.  It would also fragment the kernel
 * map unnecessarily.  We cannot use pmap_protect since it also won't
 * enforce a write-enable request.  Using pmap_enter is the only way
 * we can ensure the change takes place properly.
 */
int
sparc_bus_protect(bus_space_tag_t t, bus_space_tag_t t0, bus_space_handle_t h,
    bus_size_t size, int flags)
{
        vm_prot_t prot;
	paddr_t	pm_flags = 0;
        paddr_t pa;
        vaddr_t sva, eva;
	void* addr = bus_space_vaddr(t0, h);

	if (addr == 0) {
		printf("\nsparc_bus_protect: null address");
		return (EINVAL);
	}

	if (PHYS_ASI(t0->asi)) {
		printf("\nsparc_bus_protect: physical ASI");
		return (EINVAL);
	}

        prot = (flags & BUS_SPACE_MAP_READONLY) ?
	    PROT_READ : PROT_READ | PROT_WRITE;
	if ((flags & BUS_SPACE_MAP_CACHEABLE) == 0)
	    pm_flags |= PMAP_NC;

        eva = round_page((vaddr_t)addr + size);
        for (sva = trunc_page((vaddr_t)addr); sva < eva; sva += PAGE_SIZE) {
                /*
                 * Extract physical address for the page.
                 */
                if (pmap_extract(pmap_kernel(), sva, &pa) == FALSE)
                        panic("bus_space_protect(): invalid page");
                pmap_enter(pmap_kernel(), sva, pa | pm_flags, prot, prot | PMAP_WIRED);
        }
	pmap_update(pmap_kernel());

	return (0);
}

int
sparc_bus_unmap(bus_space_tag_t t, bus_space_tag_t t0, bus_space_handle_t bh,
    bus_size_t size)
{
	vaddr_t va = trunc_page((vaddr_t)bh.bh_ptr);
	vaddr_t endva = va + round_page(size);

	if (PHYS_ASI(t0->asi))
		return (0);

	pmap_remove(pmap_kernel(), va, endva);
	pmap_update(pmap_kernel());
	km_free((void *)va, endva - va, &kv_any, &kp_none);

	return (0);
}

paddr_t
sparc_bus_mmap(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t paddr,
    off_t off, int prot, int flags)
{
	if (PHYS_ASI(t0->asi)) {
		printf("\nsparc_bus_mmap: physical ASI");
		return (0);
	}

	/* Devices are un-cached... although the driver should do that */
	return ((paddr + off) | PMAP_NC);
}

bus_addr_t
sparc_bus_addr(bus_space_tag_t t, bus_space_tag_t t0, bus_space_handle_t h)
{
	paddr_t addr;

	if (PHYS_ASI(t0->asi))
		return h.bh_ptr;

	if (!pmap_extract(pmap_kernel(), h.bh_ptr, &addr))
		return (-1);
	return addr;
}

void *
bus_intr_allocate(bus_space_tag_t t, int (*handler)(void *), void *arg,
    int number, int pil,
    volatile u_int64_t *mapper, volatile u_int64_t *clearer,
    const char *what)
{
	struct intrhand *ih;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (ih == NULL)
		return (NULL);

	ih->ih_fun = handler;
	ih->ih_arg = arg;
	ih->ih_number = number;
	ih->ih_pil = pil;
	ih->ih_map = mapper;
	ih->ih_clr = clearer;
	ih->ih_bus = t;
	strlcpy(ih->ih_name, what, sizeof(ih->ih_name));

	return (ih);
}

#ifdef notyet
void
bus_intr_free(void *arg)
{
	free(arg, M_DEVBUF, 0);
}
#endif

void *
sparc_mainbus_intr_establish(bus_space_tag_t t, bus_space_tag_t t0, int number,
    int pil, int flags, int (*handler)(void *), void *arg, const char *what)
{
	struct intrhand *ih;

	ih = bus_intr_allocate(t0, handler, arg, number, pil, NULL, NULL, what);
	if (ih == NULL)
		return (NULL);

	intr_establish(ih);

	return (ih);
}

int
sparc_bus_alloc(bus_space_tag_t t, bus_space_tag_t t0, bus_addr_t rs,
    bus_addr_t re, bus_size_t s, bus_size_t a, bus_size_t b, int f,
    bus_addr_t *ap, bus_space_handle_t *hp)
{
	return (ENOTTY);
}

void
sparc_bus_free(bus_space_tag_t t, bus_space_tag_t t0, bus_space_handle_t h,
    bus_size_t s)
{
	return;
}

static const struct sparc_bus_space_tag _mainbus_space_tag = {
	.cookie =			NULL,
	.parent =			NULL,
	.default_type =			UPA_BUS_SPACE,
	.asi =				ASI_PRIMARY,
	.sasi =				ASI_PRIMARY,
	.name =				"mainbus",
	.sparc_bus_alloc =		sparc_bus_alloc,
	.sparc_bus_free =		sparc_bus_free,
	.sparc_bus_map =		sparc_bus_map,
	.sparc_bus_protect =		sparc_bus_protect,
	.sparc_bus_unmap =		sparc_bus_unmap,
	.sparc_bus_subregion =		sparc_bus_subregion,
	.sparc_bus_mmap =		sparc_bus_mmap,	
	.sparc_intr_establish =		sparc_mainbus_intr_establish,
	/*.sparc_intr_establish_cpu*/
	.sparc_bus_addr =		sparc_bus_addr
};
const bus_space_tag_t mainbus_space_tag = &_mainbus_space_tag;

struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL
};

#define _BS_PRECALL(t,f)		\
        while (t->f == NULL)		\
                t = t->parent;
#define _BS_POSTCALL

#define _BS_CALL(t,f)			\
        (*(t)->f)

int
bus_space_alloc(bus_space_tag_t t, bus_addr_t rs, bus_addr_t re, bus_size_t s,
    bus_size_t a, bus_size_t b, int f, bus_addr_t *ap, bus_space_handle_t *hp)
{
        const bus_space_tag_t t0 = t;
        int ret;

        _BS_PRECALL(t, sparc_bus_alloc);
        ret = _BS_CALL(t, sparc_bus_alloc)(t, t0, rs, re, s, a, b, f, ap, hp);
        _BS_POSTCALL;
        return ret;
}

void
bus_space_free(bus_space_tag_t t, bus_space_handle_t h, bus_size_t s)
{
	const bus_space_tag_t t0 = t;

	_BS_PRECALL(t, sparc_bus_free);
	_BS_CALL(t, sparc_bus_free)(t, t0, h, s);
	_BS_POSTCALL;
}

int
bus_space_map(bus_space_tag_t t, bus_addr_t a, bus_size_t s, int f,
    bus_space_handle_t *hp)
{
	const bus_space_tag_t t0 = t;
	int ret;

	_BS_PRECALL(t, sparc_bus_map);
	ret = _BS_CALL(t, sparc_bus_map)(t, t0, a, s, f, hp);
	_BS_POSTCALL;
#ifdef BUS_SPACE_DEBUG
	if(s == 0) {
		char buf[128];
		bus_space_render_tag(t, buf, sizeof buf);
		printf("\n********** bus_space_map: requesting "
		    "zero-length mapping on bus %p:%s",
		    t, buf);
	}
	hp->bh_flags = 0;
	if (ret == 0) {
		hp->bh_size = s;
		hp->bh_tag = t0;
	} else {
		hp->bh_size = 0;
		hp->bh_tag = NULL;
	}
#endif /* BUS_SPACE_DEBUG */
	return (ret);
}

int
bus_space_protect(bus_space_tag_t t, bus_space_handle_t h, bus_size_t s, int f)
{
	const bus_space_tag_t t0 = t;
	int ret;

	_BS_PRECALL(t, sparc_bus_protect);
	ret = _BS_CALL(t, sparc_bus_protect)(t, t0, h, s, f);
	_BS_POSTCALL;

	return (ret);
}

int
bus_space_unmap(bus_space_tag_t t, bus_space_handle_t h, bus_size_t s)
{
	const bus_space_tag_t t0 = t;
	int ret;

	_BS_PRECALL(t, sparc_bus_unmap);
	BUS_SPACE_ASSERT(t0, h, 0, 1);
#ifdef BUS_SPACE_DEBUG
	if(h.bh_size != s) {
		char buf[128];
		bus_space_render_tag(t0, buf, sizeof buf);
		printf("\n********* bus_space_unmap: %p:%s, map/unmap "
		    "size mismatch (%llx != %llx)",
		    t, buf, h.bh_size, s);
	}
#endif /* BUS_SPACE_DEBUG */
	ret = _BS_CALL(t, sparc_bus_unmap)(t, t0, h, s);
	_BS_POSTCALL;
	return (ret);
}

int
bus_space_subregion(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    bus_size_t s, bus_space_handle_t *hp)
{
	const bus_space_tag_t t0 = t;
	int ret;

	_BS_PRECALL(t, sparc_bus_subregion);
	BUS_SPACE_ASSERT(t0, h, o, 1);
#ifdef BUS_SPACE_DEBUG
	if(h.bh_size < o + s) {
		char buf[128];
		bus_space_render_tag(t0, buf, sizeof buf);
		printf("\n********** bus_space_subregion: "
		    "%p:%s, %llx < %llx + %llx", 
		    t0, buf, h.bh_size, o, s);
		hp->bh_size = 0;
		hp->bh_tag = NULL;
		return (EINVAL);
	}
#endif /* BUS_SPACE_DEBUG */
	ret = _BS_CALL(t, sparc_bus_subregion)(t, t0, h, o, s, hp);
	_BS_POSTCALL;
#ifdef BUS_SPACE_DEBUG
	if (ret == 0) {
		hp->bh_size = s;
		hp->bh_tag = t0;
	} else {
		hp->bh_size = 0;
		hp->bh_tag = NULL;
	}
#endif /* BUS_SPACE_DEBUG */
	return (ret);
}

paddr_t
bus_space_mmap(bus_space_tag_t t, bus_addr_t a, off_t o, int p, int f)
{
	const bus_space_tag_t t0 = t;
	paddr_t ret;

	_BS_PRECALL(t, sparc_bus_mmap);
	ret = _BS_CALL(t, sparc_bus_mmap)(t, t0, a, o, p, f);
	_BS_POSTCALL;
	return (ret);
}

void *
bus_intr_establish(bus_space_tag_t t, int p, int l, int f, int (*h)(void *),
    void *a, const char *w)
{
	const bus_space_tag_t t0 = t;
	void *ret;

	_BS_PRECALL(t, sparc_intr_establish);
	ret = _BS_CALL(t, sparc_intr_establish)(t, t0, p, l, f, h, a, w);
	_BS_POSTCALL;
	return (ret);
}

void *
bus_intr_establish_cpu(bus_space_tag_t t, int p, int l, int f,
    struct cpu_info *ci, int (*h)(void *), void *a, const char *w)
{
	const bus_space_tag_t t0 = t;
	void *ret;

	if (t->sparc_intr_establish_cpu == NULL)
		return (bus_intr_establish(t, p, l, f, h, a, w));

	_BS_PRECALL(t, sparc_intr_establish_cpu);
	ret = _BS_CALL(t, sparc_intr_establish_cpu)(t, t0, p, l, f, ci,
	    h, a, w);
	_BS_POSTCALL;
	return (ret);
}

/* XXXX Things get complicated if we use unmapped register accesses. */
void *
bus_space_vaddr(bus_space_tag_t t, bus_space_handle_t h)
{
	BUS_SPACE_ASSERT(t, h, 0, 1);
        if(t->asi == ASI_PRIMARY || t->asi == ASI_PRIMARY_LITTLE) 
		return 	((void *)(vaddr_t)(h.bh_ptr));

#ifdef BUS_SPACE_DEBUG
	{ /* Scope */
		char buf[64];
		bus_space_render_tag(t, buf, sizeof buf);
		printf("\nbus_space_vaddr: no vaddr for %p:%s (asi=%x)",
			t, buf, t->asi);
	}
#endif

	return (NULL);
}

void
bus_space_render_tag(bus_space_tag_t t, char* buf, size_t len)
{
	if (t == NULL) {
		strlcat(buf, "<NULL>", len);
		return;
	}
	buf[0] = '\0';
	if (t->parent)
		bus_space_render_tag(t->parent, buf, len);

	strlcat(buf, "/", len);
	strlcat(buf, t->name, len);
}

#ifdef BUS_SPACE_DEBUG

void
bus_space_assert(bus_space_tag_t t, const bus_space_handle_t *h, bus_size_t o,
    int n)
{
        if (h->bh_tag != t) {
		char buf1[128];
		char buf2[128];
		bus_space_render_tag(t, buf1, sizeof buf1);
		bus_space_render_tag(h->bh_tag, buf2, sizeof buf2);
                printf("\n********** bus_space_assert: wrong tag (%p:%s, "
		    "expecting %p:%s) ", t, buf1, h->bh_tag, buf2);
	}

        if (o >= h->bh_size) {
		char buf[128];
		bus_space_render_tag(t, buf, sizeof buf);
                printf("\n********** bus_space_assert: bus %p:%s, offset "
		    "(%llx) out of mapping range (%llx) ", t, buf, o,
		    h->bh_size);
	}

	if (o & (n - 1)) {
		char buf[128];
		bus_space_render_tag(t, buf, sizeof buf);
                printf("\n********** bus_space_assert: bus %p:%s, offset "
		    "(%llx) incorrect alignment (%d) ", t, buf, o, n);
	}
}

#endif /* BUS_SPACE_DEBUG */

struct blink_led_softc {
	SLIST_HEAD(, blink_led) bls_head;
	int bls_on;
	struct timeout bls_to;
} blink_sc = { SLIST_HEAD_INITIALIZER(blink_sc.bls_head), 0 };

void
blink_led_register(struct blink_led *l)
{
	if (SLIST_EMPTY(&blink_sc.bls_head)) {
		timeout_set(&blink_sc.bls_to, blink_led_timeout, &blink_sc);
		blink_sc.bls_on = 0;
		if (sparc_led_blink)
			timeout_add(&blink_sc.bls_to, 1);
	}
	SLIST_INSERT_HEAD(&blink_sc.bls_head, l, bl_next);
}

void
blink_led_timeout(void *vsc)
{
	struct blink_led_softc *sc = &blink_sc;
	struct blink_led *l;
	int t;

	if (SLIST_EMPTY(&sc->bls_head))
		return;

	SLIST_FOREACH(l, &sc->bls_head, bl_next) {
		(*l->bl_func)(l->bl_arg, sc->bls_on);
	}
	sc->bls_on = !sc->bls_on;

	if (!sparc_led_blink)
		return;

	/*
	 * Blink rate is:
	 *      full cycle every second if completely idle (loadav = 0)
	 *      full cycle every 2 seconds if loadav = 1
	 *      full cycle every 3 seconds if loadav = 2
	 * etc.
	 */
	t = (((averunnable.ldavg[0] + FSCALE) * hz) >> (FSHIFT + 1));
	timeout_add(&sc->bls_to, t);
}
