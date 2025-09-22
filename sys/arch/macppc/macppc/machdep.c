/*	$OpenBSD: machdep.c,v 1.200 2023/10/24 13:20:10 claudio Exp $	*/
/*	$NetBSD: machdep.c,v 1.4 1996/10/16 19:33:11 ws Exp $	*/

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

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/timeout.h>
#include <sys/exec.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mount.h>
#include <sys/msgbuf.h>
#include <sys/pool.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/reboot.h>
#include <sys/syscallargs.h>
#include <sys/syslog.h>
#include <sys/extent.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/conf.h>
#include <sys/core.h>
#include <sys/kcore.h>

#include <net/if.h>
#include <uvm/uvm_extern.h>

#include <dev/cons.h>

#include <machine/pmap.h>
#include <powerpc/powerpc.h>
#include <machine/trap.h>
#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/pio.h>
#include <machine/intr.h>

#include <dev/pci/pcivar.h>

#include <arch/macppc/macppc/ofw_machdep.h>
#include <dev/ofw/openfirm.h>

#include "adb.h"
#if NADB > 0
#include <arch/macppc/dev/adbvar.h>
#endif

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_interface.h>
#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_extern.h>
#endif

#include <powerpc/reg.h>
#include <powerpc/fpu.h>

/*
 * Global variables used here and there
 */
extern struct user *proc0paddr;
struct pool ppc_vecpl;

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

int ppc_malloc_ok = 0;

char *bootpath;
char bootpathbuf[512];

/* from autoconf.c */
extern void parseofwbp(char *);

struct firmware *fw = NULL;

#ifdef DDB
void * startsym, *endsym;
#endif

#ifdef APERTURE
int allowaperture = 0;
#endif
int lid_action = 1;
int pwr_action = 1;

void dumpsys(void);
void *ppc_intr_establish(void *lcv, pci_intr_handle_t ih, int type,
    int level, int (*func)(void *), void *arg, const char *name);


/*
 * Extent maps to manage I/O. Allocate storage for 8 regions in each.
 */
static long devio_ex_storage[EXTENT_FIXED_STORAGE_SIZE(8) / sizeof (long)];
struct extent *devio_ex;

/* XXX, called from asm */
void initppc(u_int startkernel, u_int endkernel, char *args);

void
initppc(u_int startkernel, u_int endkernel, char *args)
{
	extern void *trapcode; extern int trapsize;
	extern void *dsitrap; extern int dsisize;
	extern void *isitrap; extern int isisize;
	extern void *alitrap; extern int alisize;
	extern void *decrint; extern int decrsize;
	extern void *tlbimiss; extern int tlbimsize;
	extern void *tlbdlmiss; extern int tlbdlmsize;
	extern void *tlbdsmiss; extern int tlbdsmsize;
#ifdef DDB
	extern void *ddblow; extern int ddbsize;
#endif
	extern void callback(void *);
	extern void *msgbuf_addr;
	int exc;

	proc0.p_cpu = &cpu_info[0];
	proc0.p_addr = proc0paddr;
	bzero(proc0.p_addr, sizeof *proc0.p_addr);

	curpcb = &proc0paddr->u_pcb;

	curpm = curpcb->pcb_pmreal = curpcb->pcb_pm = pmap_kernel();

	cpu_bootstrap();

	/*
	 * Initialize pmap module.
	 */
	pmap_bootstrap(startkernel, endkernel);

	/*
	 * Set up trap vectors
	 */
	for (exc = EXC_RSVD; exc < EXC_END; exc += 0x100) {
		switch (exc) {
		default:
			bcopy(&trapcode, (void *)exc, (size_t)&trapsize);
			break;
		case EXC_EXI:
			/*
			 * This one is (potentially) installed during autoconf
			 */
			break;

		case EXC_DSI:
			bcopy(&dsitrap, (void *)EXC_DSI, (size_t)&dsisize);
			break;
		case EXC_ISI:
			bcopy(&isitrap, (void *)EXC_ISI, (size_t)&isisize);
			break;
		case EXC_ALI:
			bcopy(&alitrap, (void *)EXC_ALI, (size_t)&alisize);
			break;
		case EXC_DECR:
			bcopy(&decrint, (void *)EXC_DECR, (size_t)&decrsize);
			break;
		case EXC_IMISS:
			bcopy(&tlbimiss, (void *)EXC_IMISS, (size_t)&tlbimsize);
			break;
		case EXC_DLMISS:
			bcopy(&tlbdlmiss, (void *)EXC_DLMISS, (size_t)&tlbdlmsize);
			break;
		case EXC_DSMISS:
			bcopy(&tlbdsmiss, (void *)EXC_DSMISS, (size_t)&tlbdsmsize);
			break;
		case EXC_PGM:
		case EXC_TRC:
		case EXC_BPT:
#if defined(DDB)
			bcopy(&ddblow, (void *)exc, (size_t)&ddbsize);
#endif
			break;
		}
	}

	/* Grr, ALTIVEC_UNAVAIL is a vector not ~0xff aligned: 0x0f20 */
	bcopy(&trapcode, (void *)EXC_VEC, (size_t)&trapsize);

	/*
	 * since trapsize is > 0x20, we just overwrote the EXC_PERF handler
	 * since we do not use it, we will "share" it with the EXC_VEC,
	 * we dont support EXC_VEC either.
	 * should be a 'ba 0xf20 written' at address 0xf00, but we
	 * do not generate EXC_PERF exceptions...
	 */

	syncicache((void *)EXC_RST, EXC_END - EXC_RST);

	/*
	 * Now enable translation (and machine checks/recoverable interrupts).
	 */
	pmap_enable_mmu();

	/*
	 * use the memory provided by pmap_bootstrap for message buffer
	 */
	initmsgbuf(msgbuf_addr, MSGBUFSIZE);

	/*
	 * Look at arguments passed to us and compute boothowto.
	 */
	boothowto = RB_AUTOBOOT;

	/*
	 * Parse arg string.
	 */

	/* make a copy of the args! */
	strncpy(bootpathbuf, args, 512);
	bootpath= &bootpathbuf[0];
	while ( *++bootpath && *bootpath != ' ');
	if (*bootpath) {
		*bootpath++ = 0;
		while (*bootpath) {
			switch (*bootpath++) {
			case 'a':
				boothowto |= RB_ASKNAME;
				break;
			case 's':
				boothowto |= RB_SINGLE;
				break;
			case 'd':
				boothowto |= RB_KDB;
				break;
			case 'c':
				boothowto |= RB_CONFIG;
				break;
			case 'R':
				boothowto |= RB_GOODRANDOM;
				break;
			default:
				break;
			}
		}
	}
	bootpath = &bootpathbuf[0];
	parseofwbp(bootpath);

#ifdef DDB
	ddb_init();
	db_machine_init();
#endif

	/*
	 * Set up extents for pci mappings
	 * Is this too late?
	 *
	 * what are good start and end values here??
	 * 0x0 - 0x80000000 mcu bus
	 * MAP A				MAP B
	 * 0x80000000 - 0xbfffffff io		0x80000000 - 0xefffffff mem
	 * 0xc0000000 - 0xffffffff mem		0xf0000000 - 0xffffffff io
	 *
	 * of course bsd uses 0xe and 0xf
	 * So the BSD PPC memory map will look like this
	 * 0x0 - 0x80000000 memory (whatever is filled)
	 * 0x80000000 - 0xdfffffff (pci space, memory or io)
	 * 0xe0000000 - kernel vm segment
	 * 0xf0000000 - kernel map segment (user space mapped here)
	 */

	devio_ex = extent_create("devio", 0x80000000, 0xffffffff, M_DEVBUF,
		(caddr_t)devio_ex_storage, sizeof(devio_ex_storage),
		EX_NOCOALESCE|EX_NOWAIT);

	/* while using openfirmware, run userconfig */
	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}

#ifdef DDB
	if (boothowto & RB_KDB)
		db_enter();
#endif

	/*
	 * Replace with real console.
	 */
	ofwconprobe();
	consinit();

        pool_init(&ppc_vecpl, sizeof(struct vreg), 16, IPL_NONE, 0, "ppcvec",
	    NULL);

}

void
install_extint(void (*handler)(void))
{
	void extint(void);
	void extsize(void);
	extern u_long extint_call;
	long offset = (u_long)handler - (u_long)&extint_call;
	int omsr, msr;

#ifdef DIAGNOSTIC
	if (offset > 0x1ffffff || offset < -0x1ffffff)
		panic("install_extint: too far away");
#endif
	omsr = ppc_mfmsr();
	msr = omsr & ~PSL_EE;
	ppc_mtmsr(msr);
	offset &= 0x3ffffff;
	extint_call = (extint_call & 0xfc000003) | offset;
	bcopy(&extint, (void *)EXC_EXI, (size_t)&extsize);
	syncicache((void *)&extint_call, sizeof extint_call);
	syncicache((void *)EXC_EXI, (size_t)&extsize);
	ppc_mtmsr(omsr);
}

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = 0;

/*
 * Machine dependent startup code.
 */
void
cpu_startup(void)
{
	vaddr_t minaddr, maxaddr;

	proc0.p_addr = proc0paddr;

	printf("%s", version);

	printf("real mem = %llu (%lluMB)\n",
	    (unsigned long long)ptoa((psize_t)physmem),
	    (unsigned long long)ptoa((psize_t)physmem)/1024U/1024U);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr, 16 * NCARGS,
	    VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);
	ppc_malloc_ok = 1;

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free) / 1024 / 1024);

	/*
	 * Set up the buffers.
	 */
	bufinit();
}

/*
 * consinit
 * Initialize system console.
 */
void
consinit(void)
{
	static int cons_initted = 0;

	if (cons_initted)
		return;
	cninit();
	cons_initted = 1;
}

/*
 * Clear registers on exec
 */
void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	u_int32_t newstack;
	u_int32_t pargs;
	struct trapframe *tf = trapframe(p);

	pargs = -roundup(-stack + 8, 16);
	newstack = (u_int32_t)(pargs - 32);

	memset(tf, 0, sizeof *tf);
	tf->fixreg[1] = newstack;
	tf->fixreg[3] = arginfo->ps_nargvstr;
	tf->fixreg[4] = (register_t)arginfo->ps_argvstr;
	tf->fixreg[5] = (register_t)arginfo->ps_envstr;
	tf->fixreg[6] = arginfo->ps_nenvstr;
	tf->srr0 = pack->ep_entry;
	tf->srr1 = PSL_MBO | PSL_USERSET | PSL_FE_DFLT;
	p->p_addr->u_pcb.pcb_flags = 0;
}

/*
 * Send a signal to process.
 */
int
sendsig(sig_t catcher, int sig, sigset_t mask, const siginfo_t *ksip,
    int info, int onstack)
{
	struct proc *p = curproc;
	struct trapframe *tf;
	struct sigframe *fp, frame;

	bzero(&frame, sizeof(frame));
	frame.sf_signum = sig;

	tf = trapframe(p);

	/*
	 * Allocate stack space for signal handler.
	 */
	if ((p->p_sigstk.ss_flags & SS_DISABLE) == 0 &&
	    !sigonstack(tf->fixreg[1]) &&
	    onstack)
		fp = (struct sigframe *)
		    trunc_page((vaddr_t)p->p_sigstk.ss_sp + p->p_sigstk.ss_size);
	else
		fp = (struct sigframe *)tf->fixreg[1];

	fp = (struct sigframe *)((int)(fp - 1) & ~0xf);

	/*
	 * Generate signal context for SYS_sigreturn.
	 */
	frame.sf_sc.sc_mask = mask;
	frame.sf_sip = NULL;
	bcopy(tf, &frame.sf_sc.sc_frame, sizeof *tf);
	if (info) {
		frame.sf_sip = &fp->sf_si;
		frame.sf_si = *ksip;
	}
	frame.sf_sc.sc_cookie = (long)&fp->sf_sc ^ p->p_p->ps_sigcookie;
	if (copyout(&frame, fp, sizeof frame) != 0)
		return 1;

	tf->fixreg[1] = (int)fp;
	tf->lr = (int)catcher;
	tf->fixreg[3] = (int)sig;
	tf->fixreg[4] = info ? (int)&fp->sf_si : 0;
	tf->fixreg[5] = (int)&fp->sf_sc;
	tf->srr0 = p->p_p->ps_sigcode;

	return 0;
}

/*
 * System call to cleanup state after a signal handler returns.
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

	if ((error = copyin(scp, &ksc, sizeof ksc)))
		return error;

	if (ksc.sc_cookie != ((long)scp ^ p->p_p->ps_sigcookie)) {
		sigexit(p, SIGILL);
		return (EFAULT);
	}

	/* Prevent reuse of the sigcontext cookie */
	ksc.sc_cookie = 0;
	(void)copyout(&ksc.sc_cookie, (caddr_t)scp +
	    offsetof(struct sigcontext, sc_cookie), sizeof (ksc.sc_cookie));

	tf = trapframe(p);
	ksc.sc_frame.srr1 &= ~PSL_VEC;
	ksc.sc_frame.srr1 |= (tf->srr1 & PSL_VEC);
	if ((ksc.sc_frame.srr1 & PSL_USERSTATIC) != (tf->srr1 & PSL_USERSTATIC))
		return EINVAL;
	bcopy(&ksc.sc_frame, tf, sizeof *tf);
	p->p_sigmask = ksc.sc_mask & ~sigcantmask;
	return EJUSTRETURN;
}

const struct sysctl_bounded_args cpuctl_vars[] = {
	{ CPU_ALTIVEC, &ppc_altivec, SYSCTL_INT_READONLY },
	{ CPU_LIDACTION, &lid_action, 0, 2 },
	{ CPU_PWRACTION, &pwr_action, 0, 2 },
};

/*
 * Machine dependent system variables.
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return ENOTDIR;
	switch (name[0]) {
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
	default:
		return (sysctl_bounded_arr(cpuctl_vars, nitems(cpuctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen));
	}
}


u_long dumpmag = 0x04959fca;			/* magic number */
int dumpsize = 0;			/* size of dump in pages */
long dumplo = -1;			/* blocks */

/*
 * This is called by configure to set dumplo and dumpsize.
 * Dumps always skip the first CLBYTES of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void dumpconf(void);

void
dumpconf(void)
{
	int nblks;	/* size of dump area */
	int i;

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	/* Always skip the first block, in case there is a label there. */
	if (dumplo < ctod(1))
		dumplo = ctod(1);

        for (i = 0; i < ndumpmem; i++)
		dumpsize = max(dumpsize, dumpmem[i].end);

	/* Put dump at end of partition, and make it fit. */
	if (dumpsize > dtoc(nblks - dumplo - 1))
		dumpsize = dtoc(nblks - dumplo - 1);
	if (dumplo < nblks - ctod(dumpsize) - 1)
		dumplo = nblks - ctod(dumpsize) - 1;

}

#define BYTES_PER_DUMP  (PAGE_SIZE)  /* must be a multiple of pagesize */
static vaddr_t dumpspace;

int
reserve_dumppages(caddr_t p)
{
	dumpspace = (vaddr_t)p;
	return BYTES_PER_DUMP;
}

/*
 * cpu_dump: dump machine-dependent kernel core dump headers.
 */
int cpu_dump(void);
int
cpu_dump(void)
{
	int (*dump) (dev_t, daddr_t, caddr_t, size_t);
	long buf[dbtob(1) / sizeof (long)];
	kcore_seg_t	*segp;

	dump = bdevsw[major(dumpdev)].d_dump;

	segp = (kcore_seg_t *)buf;

	/*
	 * Generate a segment header.
	 */
	CORE_SETMAGIC(*segp, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	segp->c_size = dbtob(1) - ALIGN(sizeof(*segp));

	return (dump(dumpdev, dumplo, (caddr_t)buf, dbtob(1)));
}

void
dumpsys(void)
{
#if 0
	u_int npg;
	u_int i, j;
	daddr_t blkno;
	int (*dump) (dev_t, daddr_t, caddr_t, size_t);
	char *str;
	int maddr;
	extern int msgbufmapped;
	int error;

	/* save registers */

	msgbufmapped = 0;	/* don't record dump msgs in msgbuf */
	if (dumpdev == NODEV)
		return;
	/*
	 * For dumps during autoconfiguration,
	 * if dump device has already configured...
	 */
	if (dumpsize == 0)
		dumpconf();
	if (dumplo < 0)
		return;
	printf("dumping to dev %x, offset %ld\n", dumpdev, dumplo);

	error = (*bdevsw[major(dumpdev)].d_psize)(dumpdev);
	if (error == -1) {
		printf("area unavailable\n");
		delay (10000000);
		return;
	}

	dump = bdevsw[major(dumpdev)].d_dump;
	error = cpu_dump();
	for (i = 0; !error && i < ndumpmem; i++) {
		npg = dumpmem[i].end - dumpmem[i].start;
		maddr = ptoa(dumpmem[i].start);
		blkno = dumplo + btodb(maddr) + 1;

		for (j = npg; j;
			j--, maddr += PAGE_SIZE, blkno+= btodb(PAGE_SIZE))
		{
			/* Print out how many MBs we have to go. */
                        if (dbtob(blkno - dumplo) % (1024 * 1024) < NBPG)
                                printf("%d ",
                                    (ptoa(dumpsize) - maddr) / (1024 * 1024));

			pmap_enter(pmap_kernel(), dumpspace, maddr,
				PROT_READ, PMAP_WIRED);
			if ((error = (*dump)(dumpdev, blkno,
			    (caddr_t)dumpspace, PAGE_SIZE)) != 0)
				break;
		}
	}

	switch (error) {

	case 0:         str = "succeeded\n\n";                  break;
	case ENXIO:     str = "device bad\n\n";                 break;
	case EFAULT:    str = "device not ready\n\n";           break;
	case EINVAL:    str = "area improper\n\n";              break;
	case EIO:       str = "i/o error\n\n";                  break;
	case EINTR:     str = "aborted from console\n\n";       break;
	default:        str = "error %d\n\n";                   break;
	}
	printf(str, error);

#else
	printf("dumpsys() - no yet supported\n");
	
#endif
	delay(5000000);         /* 5 seconds */

}

/*
 * Halt or reboot the machine after syncing/dumping according to howto.
 */
__dead void
boot(int howto)
{
	static int syncing;

	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0 && !syncing) {
		syncing = 1;
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

	if ((howto & RB_HALT) != 0) {
		if ((howto & RB_POWERDOWN) != 0) {
#if NADB > 0
			delay(1000000);
			adb_poweroff();
			printf("WARNING: adb powerdown failed!\n");
#endif
			OF_interpret("shut-down", 0);
		}

		printf("halted\n\n");
		OF_exit();
	}
doreset:
	printf("rebooting\n\n");

#if NADB > 0
	adb_restart();  /* not return */
#endif

	OF_interpret("reset-all", 0);
	OF_exit();
	printf("boot failed, spinning\n");
	for (;;)
		continue;
	/* NOTREACHED */
}

typedef void  (void_f) (void);
void_f *pending_int_f = NULL;

/* call the bus/interrupt controller specific pending interrupt handler
 * would be nice if the offlevel interrupt code was handled here
 * instead of being in each of the specific handler code
 */
void
do_pending_int(void)
{
	if (pending_int_f != NULL) {
		(*pending_int_f)();
	}
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
		ppc_send_ipi(ci, PPC_IPI_NOP);
}
#endif

int ppc_configed_intr_cnt = 0;
struct intrhand ppc_configed_intr[MAX_PRECONF_INTR];

/*
 * True if the system has any non-level interrupts which are shared
 * on the same pin.
 */
int	intr_shared_edge;

void *
ppc_intr_establish(void *lcv, pci_intr_handle_t ih, int type, int level,
    int (*func)(void *), void *arg, const char *name)
{
	if (ppc_configed_intr_cnt < MAX_PRECONF_INTR) {
		ppc_configed_intr[ppc_configed_intr_cnt].ih_fun = func;
		ppc_configed_intr[ppc_configed_intr_cnt].ih_arg = arg;
		ppc_configed_intr[ppc_configed_intr_cnt].ih_type = type;
		ppc_configed_intr[ppc_configed_intr_cnt].ih_level = level;
		ppc_configed_intr[ppc_configed_intr_cnt].ih_irq = ih;
		ppc_configed_intr[ppc_configed_intr_cnt].ih_what = name;
		ppc_configed_intr_cnt++;
	} else {
		panic("ppc_intr_establish called before interrupt controller"
			" configured: driver %s too many interrupts", name);
	}
	/* disestablish is going to be tricky to supported for these :-) */
	return (void *)ppc_configed_intr_cnt;
}

intr_establish_t *intr_establish_func = (intr_establish_t *)ppc_intr_establish;
intr_disestablish_t *intr_disestablish_func;

intr_send_ipi_t ppc_no_send_ipi;
intr_send_ipi_t *intr_send_ipi_func = ppc_no_send_ipi;

void
ppc_no_send_ipi(struct cpu_info *ci, int id)
{
	panic("ppc_send_ipi called: no ipi function");
}

void
ppc_send_ipi(struct cpu_info *ci, int id)
{
	(*intr_send_ipi_func)(ci, id);
}

/* bcopy(), error on fault */
int
kcopy(const void *from, void *to, size_t size)
{
	faultbuf env;
	void *oldh = curproc->p_addr->u_pcb.pcb_onfault;

	if (setfault(&env)) {
		curproc->p_addr->u_pcb.pcb_onfault = oldh;
		return EFAULT;
	}
	bcopy(from, to, size);
	curproc->p_addr->u_pcb.pcb_onfault = oldh;

	return 0;
}

/* prototype for locore function */
void cpu_switchto_asm(struct proc *oldproc, struct proc *newproc);

void
cpu_switchto(struct proc *oldproc, struct proc *newproc)
{
	/*
	 * if this CPU is running a new process, flush the
	 * FPU/Altivec context to avoid an IPI.
	 */
#ifdef MULTIPROCESSOR
	struct cpu_info *ci = curcpu();
	if (ci->ci_fpuproc)
		save_fpu();
	if (ci->ci_vecproc)
		save_vec(ci->ci_vecproc);
#endif

	cpu_switchto_asm(oldproc, newproc);
}
