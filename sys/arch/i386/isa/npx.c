/*	$OpenBSD: npx.c,v 1.76 2024/05/13 01:15:50 jsg Exp $	*/
/*	$NetBSD: npx.c,v 1.57 1996/05/12 23:12:24 mycroft Exp $	*/

#if 0
#define IPRINTF(x)	printf x
#else
#define	IPRINTF(x)
#endif

/*-
 * Copyright (c) 1994, 1995 Charles M. Hannum.  All rights reserved.
 * Copyright (c) 1990 William Jolitz.
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	@(#)npx.c	7.2 (Berkeley) 5/12/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signalvar.h>
#include <sys/user.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/intr.h>
#include <machine/npx.h>
#include <machine/pio.h>
#include <machine/cpufunc.h>
#include <machine/pcb.h>
#include <machine/trap.h>
#include <machine/specialreg.h>
#include <machine/i8259.h>

#include <dev/isa/isavar.h>

/*
 * 387 and 287 Numeric Coprocessor Extension (NPX) Driver.
 *
 * We do lazy initialization and switching using the TS bit in cr0 and the
 * MDP_USEDFPU bit in mdproc.
 *
 * DNA exceptions are handled like this:
 *
 * 1) If there is no NPX, return and go to the emulator.
 * 2) If someone else has used the NPX, save its state into that process's PCB.
 * 3a) If MDP_USEDFPU is not set, set it and initialize the NPX.
 * 3b) Otherwise, reload the process's previous NPX state.
 *
 * When a process is created or exec()s, its saved cr0 image has the TS bit
 * set and the MDP_USEDFPU bit clear.  The MDP_USEDFPU bit is set when the
 * process first gets a DNA and the NPX is initialized.  The TS bit is turned
 * off when the NPX is used, and turned on again later when the process's NPX
 * state is saved.
 */

#define	fldcw(addr)		__asm("fldcw %0" : : "m" (*addr))
#define	fnclex()		__asm("fnclex")
#define	fninit()		__asm("fninit")
#define	fnsave(addr)		__asm("fnsave %0" : "=m" (*addr))
#define	fnstcw(addr)		__asm("fnstcw %0" : "=m" (*addr))
#define	fnstsw(addr)		__asm("fnstsw %0" : "=m" (*addr))
#define	fp_divide_by_0()	__asm("fldz; fld1; fdiv %st,%st(1); fwait")
#define	frstor(addr)		__asm("frstor %0" : : "m" (*addr))
#define	fwait()			__asm("fwait")
#define	clts()			__asm("clts")
#define	stts()			lcr0(rcr0() | CR0_TS)

/*
 * The mxcsr_mask for this host, taken from fxsave() on the primary CPU
 */
uint32_t	fpu_mxcsr_mask;

int npxintr(void *);
static int npxprobe1(struct isa_attach_args *);
static int x86fpflags_to_siginfo(u_int32_t);


struct npx_softc {
	struct device sc_dev;
	void *sc_ih;
};

int npxprobe(struct device *, void *, void *);
void npxattach(struct device *, struct device *, void *);

const struct cfattach npx_ca = {
	sizeof(struct npx_softc), npxprobe, npxattach
};

struct cfdriver npx_cd = {
	NULL, "npx", DV_DULL
};

enum npx_type {
	NPX_NONE = 0,
	NPX_INTERRUPT,
	NPX_EXCEPTION,
	NPX_BROKEN,
	NPX_CPUID,
};

static	enum npx_type		npx_type;
static	volatile u_int		npx_intrs_while_probing
				    __attribute__((section(".kudata")));
static	volatile u_int		npx_traps_while_probing
				    __attribute__((section(".kudata")));

#define fxsave(addr)		__asm("fxsave %0" : "=m" (*addr))
#define fxrstor(addr)		__asm("fxrstor %0" : : "m" (*addr))
#define ldmxcsr(addr)		__asm("ldmxcsr %0" : : "m" (*addr))

static __inline void
fpu_save(union savefpu *addr)
{

	if (i386_use_fxsave) {
		fxsave(&addr->sv_xmm);
		/* FXSAVE doesn't FNINIT like FNSAVE does -- so do it here. */
		fninit();
	} else
		fnsave(&addr->sv_87);
}

static int
npxdna_notset(struct cpu_info *ci)
{
	panic("npxdna vector not initialized");
}

int    (*npxdna_func)(struct cpu_info *) = npxdna_notset;
int    npxdna_s87(struct cpu_info *);
int    npxdna_xmm(struct cpu_info *);

/*
 * Special interrupt handlers.  Someday intr0-intr15 will be used to count
 * interrupts.  We'll still need a special exception 16 handler.  The busy
 * latch stuff in probintr() can be moved to npxprobe().
 */
void probeintr(void);
asm (".text\n\t"
"probeintr:\n\t"
	"ss\n\t"
	"incl	npx_intrs_while_probing\n\t"
	"pushl	%eax\n\t"
	"movb	$0x20,%al	# EOI (asm in strings loses cpp features)\n\t"
	"outb	%al,$0xa0	# IO_ICU2\n\t"
	"outb	%al,$0x20	# IO_ICU1\n\t"
	"movb	$0,%al\n\t"
	"outb	%al,$0xf0	# clear BUSY# latch\n\t"
	"popl	%eax\n\t"
	"iret\n\t");

void probetrap(void);
asm (".text\n\t"
"probetrap:\n\t"
	"ss\n\t"
	"incl	npx_traps_while_probing\n\t"
	"fnclex\n\t"
	"iret\n\t");

static inline int
npxprobe1(struct isa_attach_args *ia)
{
	int control;
	int status;

	ia->ia_iosize = 16;
	ia->ia_msize = 0;

	/*
	 * Finish resetting the coprocessor, if any.  If there is an error
	 * pending, then we may get a bogus IRQ13, but probeintr() will handle
	 * it OK.  Bogus halts have never been observed, but we enabled
	 * IRQ13 and cleared the BUSY# latch early to handle them anyway.
	 */
	fninit();
	delay(1000);		/* wait for any IRQ13 (fwait might hang) */

	/*
	 * Check for a status of mostly zero.
	 */
	status = 0x5a5a;
	fnstsw(&status);
	if ((status & 0xb8ff) == 0) {
		/*
		 * Good, now check for a proper control word.
		 */
		control = 0x5a5a;	
		fnstcw(&control);
		if ((control & 0x1f3f) == 0x033f) {
			/*
			 * We have an npx, now divide by 0 to see if exception
			 * 16 works.
			 */
			control &= ~(1 << 2);	/* enable divide by 0 trap */
			fldcw(&control);
			npx_traps_while_probing = npx_intrs_while_probing = 0;
			fp_divide_by_0();
			delay(1);
			if (npx_traps_while_probing != 0) {
				/*
				 * Good, exception 16 works.
				 */
				npx_type = NPX_EXCEPTION;
				ia->ia_irq = IRQUNK;	/* zap the interrupt */
			} else if (npx_intrs_while_probing != 0) {
				/*
				 * Bad, we are stuck with IRQ13.
				 */
				npx_type = NPX_INTERRUPT;
			} else {
				/*
				 * Worse, even IRQ13 is broken.
				 */
				npx_type = NPX_BROKEN;
				ia->ia_irq = IRQUNK;
			}
			return 1;
		}
	}

	/*
	 * Probe failed.  There is no usable FPU.
	 */
	npx_type = NPX_NONE;
	return 0;
}

/*
 * Probe routine.  Initialize cr0 to give correct behaviour for [f]wait
 * whether the device exists or not (XXX should be elsewhere).  Set flags
 * to tell npxattach() what to do.  Modify device struct if npx doesn't
 * need to use interrupts.  Return 1 if device exists.
 */
int
npxprobe(struct device *parent, void *match, void *aux)
{
	struct	isa_attach_args *ia = aux;
	int	irq;
	int	result;
	u_long	s;
	unsigned save_imen;
	struct	gate_descriptor save_idt_npxintr;
	struct	gate_descriptor save_idt_npxtrap;

	if (cpu_feature & CPUID_FPU) {
		npx_type = NPX_CPUID;
		ia->ia_irq = IRQUNK;	/* Don't want the interrupt vector */
		ia->ia_iosize = 16;
		ia->ia_msize = 0;
		return 1;
	}

	/*
	 * This routine is now just a wrapper for npxprobe1(), to install
	 * special npx interrupt and trap handlers, to enable npx interrupts
	 * and to disable other interrupts.  Someday isa_configure() will
	 * install suitable handlers and run with interrupts enabled so we
	 * won't need to do so much here.
	 */
	irq = NRSVIDT + ia->ia_irq;
	s = intr_disable();
	save_idt_npxintr = idt[irq];
	save_idt_npxtrap = idt[16];
	setgate(&idt[irq], probeintr, 0, SDT_SYS386IGT, SEL_KPL, GICODE_SEL);
	setgate(&idt[16], probetrap, 0, SDT_SYS386TGT, SEL_KPL, GCODE_SEL);
	save_imen = imen;
	imen = ~((1 << IRQ_SLAVE) | (1 << ia->ia_irq));
	SET_ICUS();

	/*
	 * Partially reset the coprocessor, if any.  Some BIOS's don't reset
	 * it after a warm boot.
	 */
	outb(0xf1, 0);		/* full reset on some systems, NOP on others */
	delay(1000);
	outb(0xf0, 0);		/* clear BUSY# latch */

	/*
	 * We set CR0 in locore to trap all ESC and WAIT instructions.
	 * We have to turn off the CR0_EM bit temporarily while probing.
	 */
	lcr0(rcr0() & ~(CR0_EM|CR0_TS));
	intr_restore(s);
	result = npxprobe1(ia);
	s = intr_disable();
	lcr0(rcr0() | (CR0_EM|CR0_TS));

	imen = save_imen;
	SET_ICUS();
	idt[irq] = save_idt_npxintr;
	idt[16] = save_idt_npxtrap;
	intr_restore(s);
	return (result);
}

int npx586bug1(int, int);
asm (".text\n\t"
"npx586bug1:\n\t"
	"fildl	4(%esp)		# x\n\t"
	"fildl	8(%esp)		# y\n\t"
	"fld	%st(1)\n\t"
	"fdiv	%st(1),%st	# x/y\n\t"
	"fmulp	%st,%st(1)	# (x/y)*y\n\t"
	"fsubrp	%st,%st(1)	# x-(x/y)*y\n\t"
	"pushl	$0\n\t"
	"fistpl	(%esp)\n\t"
	"popl	%eax\n\t"
	"ret\n\t");

void
npxinit(struct cpu_info *ci)
{
	lcr0(rcr0() & ~(CR0_EM|CR0_TS));
	fninit();
	if (npx586bug1(4195835, 3145727) != 0) {
		printf("%s: WARNING: Pentium FDIV bug detected!\n",
		    ci->ci_dev->dv_xname);
	}
	if (fpu_mxcsr_mask == 0 && i386_use_fxsave) {
		struct savexmm xm __attribute__((aligned(16)));

		bzero(&xm, sizeof(xm));
		fxsave(&xm);
		if (xm.sv_env.en_mxcsr_mask)
			fpu_mxcsr_mask = xm.sv_env.en_mxcsr_mask;
		else
			fpu_mxcsr_mask = __INITIAL_MXCSR_MASK__;
	}
	lcr0(rcr0() | (CR0_TS));
}

/*
 * Attach routine - announce which it is, and wire into system
 */
void
npxattach(struct device *parent, struct device *self, void *aux)
{
	struct npx_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;

	switch (npx_type) {
	case NPX_INTERRUPT:
		printf("\n");
		lcr0(rcr0() & ~CR0_NE);
		sc->sc_ih = isa_intr_establish(ia->ia_ic, ia->ia_irq,
		    IST_EDGE, IPL_NONE, npxintr, 0, sc->sc_dev.dv_xname);
		break;
	case NPX_EXCEPTION:
		printf(": using exception 16\n");
		break;
	case NPX_CPUID:
		printf(": reported by CPUID; using exception 16\n");
		npx_type = NPX_EXCEPTION;
		break;
	case NPX_BROKEN:
		printf(": error reporting broken; not using\n");
		npx_type = NPX_NONE;
		return;
	case NPX_NONE:
		return;
	}

	npxinit(&cpu_info_primary);

	if (i386_use_fxsave)
		npxdna_func = npxdna_xmm;
	else
		npxdna_func = npxdna_s87;
}

/*
 * Record the FPU state and reinitialize it all except for the control word.
 * Then generate a SIGFPE.
 *
 * Reinitializing the state allows naive SIGFPE handlers to longjmp without
 * doing any fixups.
 *
 * XXX there is currently no way to pass the full error state to signal
 * handlers, and if this is a nested interrupt there is no way to pass even
 * a status code!  So there is no way to have a non-naive SIGFPE handler.  At
 * best a handler could do an fninit followed by an fldcw of a static value.
 * fnclex would be of little use because it would leave junk on the FPU stack.
 * Returning from the handler would be even less safe than usual because
 * IRQ13 exception handling makes exceptions even less precise than usual.
 */
int
npxintr(void *arg)
{
	struct cpu_info *ci = curcpu();
	struct proc *p = ci->ci_fpcurproc;
	union savefpu *addr;
	struct intrframe *frame = arg;
	int code;
	union sigval sv;

	uvmexp.traps++;
	IPRINTF(("%s: fp intr\n", ci->ci_dev->dv_xname));

	if (p == NULL || npx_type == NPX_NONE) {
		/* XXX no %p in stand/printf.c.  Cast to quiet gcc -Wall. */
		printf("npxintr: p = %lx, curproc = %lx, npx_type = %d\n",
		       (u_long) p, (u_long) curproc, npx_type);
		panic("npxintr from nowhere");
	}
	/*
	 * Clear the interrupt latch.
	 */
	outb(0xf0, 0);
	/*
	 * If we're saving, ignore the interrupt.  The FPU will happily
	 * generate another one when we restore the state later.
	 */
	if (ci->ci_fpsaving)
		return (1);

#ifdef DIAGNOSTIC
	/*
	 * At this point, fpcurproc should be curproc.  If it wasn't, the TS
	 * bit should be set, and we should have gotten a DNA exception.
	 */
	if (p != curproc)
		panic("npxintr: wrong process");
#endif

	/*
	 * Find the address of fpcurproc's saved FPU state.  (Given the
	 * invariant above, this is always the one in curpcb.)
	 */
	addr = &p->p_addr->u_pcb.pcb_savefpu;
	/*
	 * Save state.  This does an implied fninit.  It had better not halt
	 * the cpu or we'll hang.
	 */
	fpu_save(addr);
	fwait();
	/*
	 * Restore control word (was clobbered by fpu_save).
	 */
	if (i386_use_fxsave) {
		fldcw(&addr->sv_xmm.sv_env.en_cw);
		/*
		 * FNINIT doesn't affect MXCSR or the XMM registers;
		 * no need to re-load MXCSR here.
		 */
	} else
		fldcw(&addr->sv_87.sv_env.en_cw);
	fwait();
	/*
	 * Remember the exception status word and tag word.  The current
	 * (almost fninit'ed) fpu state is in the fpu and the exception
	 * state just saved will soon be junk.  However, the implied fninit
	 * doesn't change the error pointers or register contents, and we
	 * preserved the control word and will copy the status and tag
	 * words, so the complete exception state can be recovered.
	 */
	if (i386_use_fxsave) {
	        addr->sv_xmm.sv_ex_sw = addr->sv_xmm.sv_env.en_sw;
	        addr->sv_xmm.sv_ex_tw = addr->sv_xmm.sv_env.en_tw;
	} else {
	        addr->sv_87.sv_ex_sw = addr->sv_87.sv_env.en_sw;
	        addr->sv_87.sv_ex_tw = addr->sv_87.sv_env.en_tw;
	}

	/*
	 * Pass exception to process.  If it's the current process, try to do
	 * it immediately.
	 */
	if (p == curproc && USERMODE(frame->if_cs, frame->if_eflags)) {
		/*
		 * Interrupt is essentially a trap, so we can afford to call
		 * the SIGFPE handler (if any) as soon as the interrupt
		 * returns.
		 *
		 * XXX little or nothing is gained from this, and plenty is
		 * lost - the interrupt frame has to contain the trap frame
		 * (this is otherwise only necessary for the rescheduling trap
		 * in doreti, and the frame for that could easily be set up
		 * just before it is used).
		 */
		p->p_md.md_regs = (struct trapframe *)&frame->if_fs;

		/*
		 * Encode the appropriate code for detailed information on
		 * this exception.
		 */
		if (i386_use_fxsave)
			code = x86fpflags_to_siginfo(addr->sv_xmm.sv_ex_sw);
		else
			code = x86fpflags_to_siginfo(addr->sv_87.sv_ex_sw);
		sv.sival_int = frame->if_eip;
		trapsignal(p, SIGFPE, T_ARITHTRAP, code, sv);
	} else {
		/*
		 * Nested interrupt.  These losers occur when:
		 *	o an IRQ13 is bogusly generated at a bogus time, e.g.:
		 *		o immediately after an fnsave or frstor of an
		 *		  error state.
		 *		o a couple of 386 instructions after
		 *		  "fstpl _memvar" causes a stack overflow.
		 *	  These are especially nasty when combined with a
		 *	  trace trap.
		 *	o an IRQ13 occurs at the same time as another higher-
		 *	  priority interrupt.
		 *
		 * Treat them like a true async interrupt.
		 */
		KERNEL_LOCK();
		psignal(p, SIGFPE);
		KERNEL_UNLOCK();
	}

	return (1);
}

void
npxtrap(struct trapframe *frame)
{
	struct proc *p = curcpu()->ci_fpcurproc;
	union savefpu *addr = &p->p_addr->u_pcb.pcb_savefpu;
	u_int32_t mxcsr, statbits;
	int code;
	union sigval sv;

#ifdef DIAGNOSTIC
	/*
	 * At this point, fpcurproc should be curproc.  If it wasn't, the TS
	 * bit should be set, and we should have gotten a DNA exception.
	 */
	if (p != curproc)
		panic("npxtrap: wrong process");
#endif

	fxsave(&addr->sv_xmm);
	mxcsr = addr->sv_xmm.sv_env.en_mxcsr;
	statbits = mxcsr;
	mxcsr &= ~0x3f;
	ldmxcsr(&mxcsr);
	addr->sv_xmm.sv_ex_sw = addr->sv_xmm.sv_env.en_sw;
	addr->sv_xmm.sv_ex_tw = addr->sv_xmm.sv_env.en_tw;
	code = x86fpflags_to_siginfo(statbits);
	sv.sival_int = frame->tf_eip;
	trapsignal(p, SIGFPE, frame->tf_err, code, sv);
}

static int
x86fpflags_to_siginfo(u_int32_t flags)
{
        int i;
        static int x86fp_siginfo_table[] = {
                FPE_FLTINV, /* bit 0 - invalid operation */
                FPE_FLTRES, /* bit 1 - denormal operand */
                FPE_FLTDIV, /* bit 2 - divide by zero   */
                FPE_FLTOVF, /* bit 3 - fp overflow      */
                FPE_FLTUND, /* bit 4 - fp underflow     */
                FPE_FLTRES, /* bit 5 - fp precision     */
                FPE_FLTINV, /* bit 6 - stack fault      */
        };

        for (i=0;i < sizeof(x86fp_siginfo_table)/sizeof(int); i++) {
                if (flags & (1 << i))
                        return (x86fp_siginfo_table[i]);
        }
        /* punt if flags not set */
        return (FPE_FLTINV);
}

/*
 * Implement device not available (DNA) exception
 *
 * If we were the last process to use the FPU, we can simply return.
 * Otherwise, we save the previous state, if necessary, and restore our last
 * saved state.
 */
int
npxdna_xmm(struct cpu_info *ci)
{
	union savefpu *sfp;
	struct proc *p;
	int s;

	if (ci->ci_fpsaving) {
		printf("recursive npx trap; cr0=%x\n", rcr0());
		return (0);
	}

	s = splipi();		/* lock out IPI's while we clean house.. */

#ifdef MULTIPROCESSOR
	p = ci->ci_curproc;
#else
	p = curproc;
#endif

	IPRINTF(("%s: dna for %lx%s\n", ci->ci_dev->dv_xname, (u_long)p,
	    (p->p_md.md_flags & MDP_USEDFPU) ? " (used fpu)" : ""));

	/*
	 * XXX should have a fast-path here when no save/restore is necessary
	 */
	/*
	 * Initialize the FPU state to clear any exceptions.  If someone else
	 * was using the FPU, save their state (which does an implicit
	 * initialization).
	 */
	if (ci->ci_fpcurproc != NULL) {
		IPRINTF(("%s: fp save %lx\n", ci->ci_dev->dv_xname,
		    (u_long)ci->ci_fpcurproc));
		npxsave_cpu(ci, ci->ci_fpcurproc != &proc0);
	} else {
		clts();
		IPRINTF(("%s: fp init\n", ci->ci_dev->dv_xname));
		fninit();
		fwait();
		stts();
	}
	splx(s);

	IPRINTF(("%s: done saving\n", ci->ci_dev->dv_xname));
	KDASSERT(ci->ci_fpcurproc == NULL);
#ifndef MULTIPROCESSOR
	KDASSERT(p->p_addr->u_pcb.pcb_fpcpu == NULL);
#else
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		npxsave_proc(p, 1);
#endif
	p->p_addr->u_pcb.pcb_cr0 &= ~CR0_TS;
	clts();
	s = splipi();
	ci->ci_fpcurproc = p;
	p->p_addr->u_pcb.pcb_fpcpu = ci;
	splx(s);
	uvmexp.fpswtch++;

	sfp = &p->p_addr->u_pcb.pcb_savefpu;

	if ((p->p_md.md_flags & MDP_USEDFPU) == 0) {
		bzero(&sfp->sv_xmm, sizeof(sfp->sv_xmm));
		sfp->sv_xmm.sv_env.en_cw = __INITIAL_NPXCW__;
		sfp->sv_xmm.sv_env.en_mxcsr = __INITIAL_MXCSR__;
		fxrstor(&sfp->sv_xmm);
		p->p_md.md_flags |= MDP_USEDFPU;
	} else {
		static double	zero = 0.0;

		/*
		 * amd fpu does not restore fip, fdp, fop on fxrstor
		 * thus leaking other process's execution history.
		 */
		fnclex();
		__asm volatile("ffree %%st(7)\n\tfldl %0" : : "m" (zero));
		fxrstor(&sfp->sv_xmm);
	}

	return (1);
}

int
npxdna_s87(struct cpu_info *ci)
{
	union savefpu *sfp;
	struct proc *p;
	int s;

	KDASSERT(i386_use_fxsave == 0);

	if (ci->ci_fpsaving) {
		printf("recursive npx trap; cr0=%x\n", rcr0());
		return (0);
	}

	s = splipi();		/* lock out IPI's while we clean house.. */
#ifdef MULTIPROCESSOR
	p = ci->ci_curproc;
#else
	p = curproc;
#endif

	IPRINTF(("%s: dna for %lx%s\n", ci->ci_dev->dv_xname, (u_long)p,
	    (p->p_md.md_flags & MDP_USEDFPU) ? " (used fpu)" : ""));

	/*
	 * If someone else was using our FPU, save their state (which does an
	 * implicit initialization); otherwise, initialize the FPU state to
	 * clear any exceptions.
	 */
	if (ci->ci_fpcurproc != NULL) {
		IPRINTF(("%s: fp save %lx\n", ci->ci_dev->dv_xname,
		    (u_long)ci->ci_fpcurproc));
		npxsave_cpu(ci, ci->ci_fpcurproc != &proc0);
	} else {
		clts();
		IPRINTF(("%s: fp init\n", ci->ci_dev->dv_xname));
		fninit();
		fwait();
		stts();
	}
	splx(s);

	IPRINTF(("%s: done saving\n", ci->ci_dev->dv_xname));
	KDASSERT(ci->ci_fpcurproc == NULL);
#ifndef MULTIPROCESSOR
	KDASSERT(p->p_addr->u_pcb.pcb_fpcpu == NULL);
#else
	if (p->p_addr->u_pcb.pcb_fpcpu != NULL)
		npxsave_proc(p, 1);
#endif
	p->p_addr->u_pcb.pcb_cr0 &= ~CR0_TS;
	clts();
	s = splipi();
	ci->ci_fpcurproc = p;
	p->p_addr->u_pcb.pcb_fpcpu = ci;
	splx(s);
	uvmexp.fpswtch++;

	sfp = &p->p_addr->u_pcb.pcb_savefpu;

	if ((p->p_md.md_flags & MDP_USEDFPU) == 0) {
		bzero(&sfp->sv_87, sizeof(sfp->sv_87));
		sfp->sv_87.sv_env.en_cw = __INITIAL_NPXCW__;
		sfp->sv_87.sv_env.en_tw = 0xffff;
		frstor(&sfp->sv_87);
		p->p_md.md_flags |= MDP_USEDFPU;
	} else {
		/*
		 * The following frstor may cause an IRQ13 when the state being
		 * restored has a pending error.  The error will appear to have
		 * been triggered by the current (npx) user instruction even
		 * when that instruction is a no-wait instruction that should
		 * not trigger an error (e.g., fnclex).  On at least one 486
		 * system all of the no-wait instructions are broken the same
		 * as frstor, so our treatment does not amplify the breakage.
		 * On at least one 386/Cyrix 387 system, fnclex works correctly
		 * while frstor and fnsave are broken, so our treatment breaks
		 * fnclex if it is the first FPU instruction after a context
		 * switch.
		 */
		frstor(&sfp->sv_87);
	}

	return (1);
}

/*
 * The FNSAVE instruction clears the FPU state.  Rather than reloading the FPU
 * immediately, we clear fpcurproc and turn on CR0_TS to force a DNA and a
 * reload of the FPU state the next time we try to use it.  This routine
 * is only called when forking, core dumping, or debugging, or swapping,
 * so the lazy reload at worst forces us to trap once per fork(), and at best
 * saves us a reload once per fork().
 */
void
npxsave_cpu(struct cpu_info *ci, int save)
{
	struct proc *p;
	int s;

	KDASSERT(ci == curcpu());

	p = ci->ci_fpcurproc;
	if (p == NULL)
		return;

	IPRINTF(("%s: fp cpu %s %lx\n", ci->ci_dev->dv_xname,
	    save ? "save" : "flush", (u_long)p));

	if (save) {
#ifdef DIAGNOSTIC
		if (ci->ci_fpsaving != 0)
			panic("npxsave_cpu: recursive save!");
#endif
		 /*
		  * Set ci->ci_fpsaving, so that any pending exception will be
		  * thrown away.  (It will be caught again if/when the FPU
		  * state is restored.)
		  *
		  * XXX on i386 and earlier, this routine should always be
		  * called at spl0; if it might called with the NPX interrupt
		  * masked, it would be necessary to forcibly unmask the NPX
		  * interrupt so that it could succeed.
		  * XXX this is irrelevant on 486 and above (systems
		  * which report FP failures via traps rather than irq13).
		  * XXX punting for now..
		  */
		clts();
		ci->ci_fpsaving = 1;
		fpu_save(&p->p_addr->u_pcb.pcb_savefpu);
		ci->ci_fpsaving = 0;
		/* It is unclear if this is needed. */
		fwait();
	}

	/*
	 * We set the TS bit in the saved CR0 for this process, so that it
	 * will get a DNA exception on any FPU instruction and force a reload.
	 */
	stts();
	p->p_addr->u_pcb.pcb_cr0 |= CR0_TS;

	s = splipi();
	p->p_addr->u_pcb.pcb_fpcpu = NULL;
	ci->ci_fpcurproc = NULL;
	splx(s);
}

/*
 * Save p's FPU state, which may be on this processor or another processor.
 */
void
npxsave_proc(struct proc *p, int save)
{
	struct cpu_info *ci = curcpu();
	struct cpu_info *oci;

	KDASSERT(p->p_addr != NULL);

	oci = p->p_addr->u_pcb.pcb_fpcpu;
	if (oci == NULL)
		return;

	IPRINTF(("%s: fp proc %s %lx\n", ci->ci_dev->dv_xname,
	    save ? "save" : "flush", (u_long)p));

#if defined(MULTIPROCESSOR)
	if (oci == ci) {
		int s = splipi();
		npxsave_cpu(ci, save);
		splx(s);
	} else {
		IPRINTF(("%s: fp ipi to %s %s %lx\n", ci->ci_dev->dv_xname,
		    oci->ci_dev->dv_xname, save ? "save" : "flush", (u_long)p));

		oci->ci_fpsaveproc = p;
		i386_send_ipi(oci,
		    save ? I386_IPI_SYNCH_FPU : I386_IPI_FLUSH_FPU);
		while (p->p_addr->u_pcb.pcb_fpcpu != NULL)
			CPU_BUSY_CYCLE();
	}
#else
	KASSERT(ci->ci_fpcurproc == p);
	npxsave_cpu(ci, save);
#endif
}

void
fpu_kernel_enter(void)
{
	struct cpu_info	*ci = curcpu();
	uint32_t	 cw;
	int		 s;

	/*
	 * Fast path.  If the kernel was using the FPU before, there
	 * is no work to do besides clearing TS.
	 */
	if (ci->ci_fpcurproc == &proc0) {
		clts();
		return;
	}

	s = splipi();

	if (ci->ci_fpcurproc != NULL) {
		npxsave_cpu(ci, 1);
		uvmexp.fpswtch++;
	}

	/* Claim the FPU */
	ci->ci_fpcurproc = &proc0;

	splx(s);

	/* Disable DNA exceptions */
	clts();

	/* Initialize the FPU */
	fninit();
	cw = __INITIAL_NPXCW__;
	fldcw(&cw);
	if (i386_has_sse || i386_has_sse2) {
		cw = __INITIAL_MXCSR__;
		ldmxcsr(&cw);
	}
}

void
fpu_kernel_exit(void)
{
	/* Enable DNA exceptions */
	stts();
}
