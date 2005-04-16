/* $Id: ttable.h,v 1.18 2002/02/09 19:49:32 davem Exp $ */
#ifndef _SPARC64_TTABLE_H
#define _SPARC64_TTABLE_H

#include <linux/config.h>
#include <asm/utrap.h>

#ifdef __ASSEMBLY__
#include <asm/thread_info.h>
#endif

#define BOOT_KERNEL b sparc64_boot; nop; nop; nop; nop; nop; nop; nop;

/* We need a "cleaned" instruction... */
#define CLEAN_WINDOW							\
	rdpr	%cleanwin, %l0;		add	%l0, 1, %l0;		\
	wrpr	%l0, 0x0, %cleanwin;					\
	clr	%o0;	clr	%o1;	clr	%o2;	clr	%o3;	\
	clr	%o4;	clr	%o5;	clr	%o6;	clr	%o7;	\
	clr	%l0;	clr	%l1;	clr	%l2;	clr	%l3;	\
	clr	%l4;	clr	%l5;	clr	%l6;	clr	%l7;	\
	retry;								\
	nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;nop;

#define TRAP(routine)					\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, etrap;				\
109:	 or	%g7, %lo(109b), %g7;			\
	call	routine;				\
	 add	%sp, PTREGS_OFF, %o0;			\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;					\
	nop;

#define TRAP_7INSNS(routine)				\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, etrap;				\
109:	 or	%g7, %lo(109b), %g7;			\
	call	routine;				\
	 add	%sp, PTREGS_OFF, %o0;			\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;

#define TRAP_SAVEFPU(routine)				\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, do_fptrap;			\
109:	 or	%g7, %lo(109b), %g7;			\
	call	routine;				\
	 add	%sp, PTREGS_OFF, %o0;			\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;					\
	nop;

#define TRAP_NOSAVE(routine)				\
	ba,pt	%xcc, routine;				\
	 nop;						\
	nop; nop; nop; nop; nop; nop;
	
#define TRAP_NOSAVE_7INSNS(routine)			\
	ba,pt	%xcc, routine;				\
	 nop;						\
	nop; nop; nop; nop; nop;
	
#define TRAPTL1(routine)				\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, etraptl1;				\
109:	 or	%g7, %lo(109b), %g7;			\
	call	routine;				\
	 add	%sp, PTREGS_OFF, %o0;			\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;					\
	nop;
	
#define TRAP_ARG(routine, arg)				\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, etrap;				\
109:	 or	%g7, %lo(109b), %g7;			\
	add	%sp, PTREGS_OFF, %o0;			\
	call	routine;				\
	 mov	arg, %o1;				\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;
	
#define TRAPTL1_ARG(routine, arg)			\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, etraptl1;				\
109:	 or	%g7, %lo(109b), %g7;			\
	add	%sp, PTREGS_OFF, %o0;			\
	call	routine;				\
	 mov	arg, %o1;				\
	ba,pt	%xcc, rtrap;				\
	 clr	%l6;
	
#define SYSCALL_TRAP(routine, systbl)			\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, scetrap;				\
109:	 or	%g7, %lo(109b), %g7;			\
	sethi	%hi(systbl), %l7;			\
	ba,pt	%xcc, routine;				\
	 or	%l7, %lo(systbl), %l7;			\
	nop; nop;
	
#define INDIRECT_SOLARIS_SYSCALL(num)			\
	sethi	%hi(109f), %g7;				\
	ba,pt	%xcc, etrap;				\
109:	 or	%g7, %lo(109b), %g7;			\
	ba,pt	%xcc, tl0_solaris + 0xc;		\
	 mov	num, %g1;				\
	nop;nop;nop;
	
#define TRAP_UTRAP(handler,lvl)				\
	ldx	[%g6 + TI_UTRAPS], %g1;			\
	sethi	%hi(109f), %g7;				\
	brz,pn	%g1, utrap;				\
	 or	%g7, %lo(109f), %g7;			\
	ba,pt	%xcc, utrap;				\
109:	 ldx	[%g1 + handler*8], %g1;			\
	ba,pt	%xcc, utrap_ill;			\
	 mov	lvl, %o1;

#ifdef CONFIG_SUNOS_EMUL
#define SUNOS_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall32, sunos_sys_table)
#else
#define SUNOS_SYSCALL_TRAP TRAP(sunos_syscall)
#endif
#ifdef CONFIG_COMPAT
#define	LINUX_32BIT_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall32, sys_call_table32)
#else
#define	LINUX_32BIT_SYSCALL_TRAP BTRAP(0x110)
#endif
#define LINUX_64BIT_SYSCALL_TRAP SYSCALL_TRAP(linux_sparc_syscall, sys_call_table64)
#define GETCC_TRAP TRAP(getcc)
#define SETCC_TRAP TRAP(setcc)
#ifdef CONFIG_SOLARIS_EMUL
#define SOLARIS_SYSCALL_TRAP TRAP(solaris_sparc_syscall)
#else
#define SOLARIS_SYSCALL_TRAP TRAP(solaris_syscall)
#endif
/* FIXME: Write these actually */	
#define NETBSD_SYSCALL_TRAP TRAP(netbsd_syscall)
#define BREAKPOINT_TRAP TRAP(breakpoint_trap)

#define TRAP_IRQ(routine, level)			\
	rdpr	%pil, %g2;				\
	wrpr	%g0, 15, %pil;				\
	b,pt	%xcc, etrap_irq;			\
	 rd	%pc, %g7;				\
	mov	level, %o0;				\
	call	routine;				\
	 add	%sp, PTREGS_OFF, %o1;			\
	ba,a,pt	%xcc, rtrap_irq;
	
#define TICK_SMP_IRQ					\
	rdpr	%pil, %g2;				\
	wrpr	%g0, 15, %pil;				\
	sethi	%hi(109f), %g7;				\
	b,pt	%xcc, etrap_irq;			\
109:	 or	%g7, %lo(109b), %g7;			\
	call	smp_percpu_timer_interrupt;		\
	 add	%sp, PTREGS_OFF, %o0;			\
	ba,a,pt	%xcc, rtrap_irq;

#define TRAP_IVEC TRAP_NOSAVE(do_ivec)

#define BTRAP(lvl) TRAP_ARG(bad_trap, lvl)

#define BTRAPTL1(lvl) TRAPTL1_ARG(bad_trap_tl1, lvl)

#define FLUSH_WINDOW_TRAP						\
	ba,pt	%xcc, etrap;						\
	 rd	%pc, %g7;						\
	flushw;								\
	ldx	[%sp + PTREGS_OFF + PT_V9_TNPC], %l1;			\
	add	%l1, 4, %l2;						\
	stx	%l1, [%sp + PTREGS_OFF + PT_V9_TPC];			\
	ba,pt	%xcc, rtrap_clr_l6;					\
	 stx	%l2, [%sp + PTREGS_OFF + PT_V9_TNPC];
	        
#ifdef CONFIG_KPROBES
#define KPROBES_TRAP(lvl) TRAP_IRQ(kprobe_trap, lvl)
#else
#define KPROBES_TRAP(lvl) TRAP_ARG(bad_trap, lvl)
#endif

/* Before touching these macros, you owe it to yourself to go and
 * see how arch/sparc64/kernel/winfixup.S works... -DaveM
 *
 * For the user cases we used to use the %asi register, but
 * it turns out that the "wr xxx, %asi" costs ~5 cycles, so
 * now we use immediate ASI loads and stores instead.  Kudos
 * to Greg Onufer for pointing out this performance anomaly.
 *
 * Further note that we cannot use the g2, g4, g5, and g7 alternate
 * globals in the spill routines, check out the save instruction in
 * arch/sparc64/kernel/etrap.S to see what I mean about g2, and
 * g4/g5 are the globals which are preserved by etrap processing
 * for the caller of it.  The g7 register is the return pc for
 * etrap.  Finally, g6 is the current thread register so we cannot
 * us it in the spill handlers either.  Most of these rules do not
 * apply to fill processing, only g6 is not usable.
 */

/* Normal kernel spill */
#define SPILL_0_NORMAL					\
	stx	%l0, [%sp + STACK_BIAS + 0x00];		\
	stx	%l1, [%sp + STACK_BIAS + 0x08];		\
	stx	%l2, [%sp + STACK_BIAS + 0x10];		\
	stx	%l3, [%sp + STACK_BIAS + 0x18];		\
	stx	%l4, [%sp + STACK_BIAS + 0x20];		\
	stx	%l5, [%sp + STACK_BIAS + 0x28];		\
	stx	%l6, [%sp + STACK_BIAS + 0x30];		\
	stx	%l7, [%sp + STACK_BIAS + 0x38];		\
	stx	%i0, [%sp + STACK_BIAS + 0x40];		\
	stx	%i1, [%sp + STACK_BIAS + 0x48];		\
	stx	%i2, [%sp + STACK_BIAS + 0x50];		\
	stx	%i3, [%sp + STACK_BIAS + 0x58];		\
	stx	%i4, [%sp + STACK_BIAS + 0x60];		\
	stx	%i5, [%sp + STACK_BIAS + 0x68];		\
	stx	%i6, [%sp + STACK_BIAS + 0x70];		\
	stx	%i7, [%sp + STACK_BIAS + 0x78];		\
	saved; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop; nop; nop; nop;

/* Normal 64bit spill */
#define SPILL_1_GENERIC(ASI)				\
	add	%sp, STACK_BIAS + 0x00, %g1;		\
	stxa	%l0, [%g1 + %g0] ASI;			\
	mov	0x08, %g3;				\
	stxa	%l1, [%g1 + %g3] ASI;			\
	add	%g1, 0x10, %g1;				\
	stxa	%l2, [%g1 + %g0] ASI;			\
	stxa	%l3, [%g1 + %g3] ASI;			\
	add	%g1, 0x10, %g1;				\
	stxa	%l4, [%g1 + %g0] ASI;			\
	stxa	%l5, [%g1 + %g3] ASI;			\
	add	%g1, 0x10, %g1;				\
	stxa	%l6, [%g1 + %g0] ASI;			\
	stxa	%l7, [%g1 + %g3] ASI;			\
	add	%g1, 0x10, %g1;				\
	stxa	%i0, [%g1 + %g0] ASI;			\
	stxa	%i1, [%g1 + %g3] ASI;			\
	add	%g1, 0x10, %g1;				\
	stxa	%i2, [%g1 + %g0] ASI;			\
	stxa	%i3, [%g1 + %g3] ASI;			\
	add	%g1, 0x10, %g1;				\
	stxa	%i4, [%g1 + %g0] ASI;			\
	stxa	%i5, [%g1 + %g3] ASI;			\
	add	%g1, 0x10, %g1;				\
	stxa	%i6, [%g1 + %g0] ASI;			\
	stxa	%i7, [%g1 + %g3] ASI;			\
	saved;						\
	retry; nop; nop;				\
	b,a,pt	%xcc, spill_fixup_dax;			\
	b,a,pt	%xcc, spill_fixup_mna;			\
	b,a,pt	%xcc, spill_fixup;

/* Normal 32bit spill */
#define SPILL_2_GENERIC(ASI)				\
	srl	%sp, 0, %sp;				\
	stwa	%l0, [%sp + %g0] ASI;			\
	mov	0x04, %g3;				\
	stwa	%l1, [%sp + %g3] ASI;			\
	add	%sp, 0x08, %g1;				\
	stwa	%l2, [%g1 + %g0] ASI;			\
	stwa	%l3, [%g1 + %g3] ASI;			\
	add	%g1, 0x08, %g1;				\
	stwa	%l4, [%g1 + %g0] ASI;			\
	stwa	%l5, [%g1 + %g3] ASI;			\
	add	%g1, 0x08, %g1;				\
	stwa	%l6, [%g1 + %g0] ASI;			\
	stwa	%l7, [%g1 + %g3] ASI;			\
	add	%g1, 0x08, %g1;				\
	stwa	%i0, [%g1 + %g0] ASI;			\
	stwa	%i1, [%g1 + %g3] ASI;			\
	add	%g1, 0x08, %g1;				\
	stwa	%i2, [%g1 + %g0] ASI;			\
	stwa	%i3, [%g1 + %g3] ASI;			\
	add	%g1, 0x08, %g1;				\
	stwa	%i4, [%g1 + %g0] ASI;			\
	stwa	%i5, [%g1 + %g3] ASI;			\
	add	%g1, 0x08, %g1;				\
	stwa	%i6, [%g1 + %g0] ASI;			\
	stwa	%i7, [%g1 + %g3] ASI;			\
	saved;						\
        retry; nop; nop;				\
	b,a,pt	%xcc, spill_fixup_dax;			\
	b,a,pt	%xcc, spill_fixup_mna;			\
	b,a,pt	%xcc, spill_fixup;

#define SPILL_1_NORMAL SPILL_1_GENERIC(ASI_AIUP)
#define SPILL_2_NORMAL SPILL_2_GENERIC(ASI_AIUP)
#define SPILL_3_NORMAL SPILL_0_NORMAL
#define SPILL_4_NORMAL SPILL_0_NORMAL
#define SPILL_5_NORMAL SPILL_0_NORMAL
#define SPILL_6_NORMAL SPILL_0_NORMAL
#define SPILL_7_NORMAL SPILL_0_NORMAL

#define SPILL_0_OTHER SPILL_0_NORMAL
#define SPILL_1_OTHER SPILL_1_GENERIC(ASI_AIUS)
#define SPILL_2_OTHER SPILL_2_GENERIC(ASI_AIUS)
#define SPILL_3_OTHER SPILL_3_NORMAL
#define SPILL_4_OTHER SPILL_4_NORMAL
#define SPILL_5_OTHER SPILL_5_NORMAL
#define SPILL_6_OTHER SPILL_6_NORMAL
#define SPILL_7_OTHER SPILL_7_NORMAL

/* Normal kernel fill */
#define FILL_0_NORMAL					\
	ldx	[%sp + STACK_BIAS + 0x00], %l0;		\
	ldx	[%sp + STACK_BIAS + 0x08], %l1;		\
	ldx	[%sp + STACK_BIAS + 0x10], %l2;		\
	ldx	[%sp + STACK_BIAS + 0x18], %l3;		\
	ldx	[%sp + STACK_BIAS + 0x20], %l4;		\
	ldx	[%sp + STACK_BIAS + 0x28], %l5;		\
	ldx	[%sp + STACK_BIAS + 0x30], %l6;		\
	ldx	[%sp + STACK_BIAS + 0x38], %l7;		\
	ldx	[%sp + STACK_BIAS + 0x40], %i0;		\
	ldx	[%sp + STACK_BIAS + 0x48], %i1;		\
	ldx	[%sp + STACK_BIAS + 0x50], %i2;		\
	ldx	[%sp + STACK_BIAS + 0x58], %i3;		\
	ldx	[%sp + STACK_BIAS + 0x60], %i4;		\
	ldx	[%sp + STACK_BIAS + 0x68], %i5;		\
	ldx	[%sp + STACK_BIAS + 0x70], %i6;		\
	ldx	[%sp + STACK_BIAS + 0x78], %i7;		\
	restored; retry; nop; nop; nop; nop; nop; nop;	\
	nop; nop; nop; nop; nop; nop; nop; nop;

/* Normal 64bit fill */
#define FILL_1_GENERIC(ASI)				\
	add	%sp, STACK_BIAS + 0x00, %g1;		\
	ldxa	[%g1 + %g0] ASI, %l0;			\
	mov	0x08, %g2;				\
	mov	0x10, %g3;				\
	ldxa	[%g1 + %g2] ASI, %l1;			\
	mov	0x18, %g5;				\
	ldxa	[%g1 + %g3] ASI, %l2;			\
	ldxa	[%g1 + %g5] ASI, %l3;			\
	add	%g1, 0x20, %g1;				\
	ldxa	[%g1 + %g0] ASI, %l4;			\
	ldxa	[%g1 + %g2] ASI, %l5;			\
	ldxa	[%g1 + %g3] ASI, %l6;			\
	ldxa	[%g1 + %g5] ASI, %l7;			\
	add	%g1, 0x20, %g1;				\
	ldxa	[%g1 + %g0] ASI, %i0;			\
	ldxa	[%g1 + %g2] ASI, %i1;			\
	ldxa	[%g1 + %g3] ASI, %i2;			\
	ldxa	[%g1 + %g5] ASI, %i3;			\
	add	%g1, 0x20, %g1;				\
	ldxa	[%g1 + %g0] ASI, %i4;			\
	ldxa	[%g1 + %g2] ASI, %i5;			\
	ldxa	[%g1 + %g3] ASI, %i6;			\
	ldxa	[%g1 + %g5] ASI, %i7;			\
	restored;					\
	retry; nop; nop; nop; nop;			\
	b,a,pt	%xcc, fill_fixup_dax;			\
	b,a,pt	%xcc, fill_fixup_mna;			\
	b,a,pt	%xcc, fill_fixup;

/* Normal 32bit fill */
#define FILL_2_GENERIC(ASI)				\
	srl	%sp, 0, %sp;				\
	lduwa	[%sp + %g0] ASI, %l0;			\
	mov	0x04, %g2;				\
	mov	0x08, %g3;				\
	lduwa	[%sp + %g2] ASI, %l1;			\
	mov	0x0c, %g5;				\
	lduwa	[%sp + %g3] ASI, %l2;			\
	lduwa	[%sp + %g5] ASI, %l3;			\
	add	%sp, 0x10, %g1;				\
	lduwa	[%g1 + %g0] ASI, %l4;			\
	lduwa	[%g1 + %g2] ASI, %l5;			\
	lduwa	[%g1 + %g3] ASI, %l6;			\
	lduwa	[%g1 + %g5] ASI, %l7;			\
	add	%g1, 0x10, %g1;				\
	lduwa	[%g1 + %g0] ASI, %i0;			\
	lduwa	[%g1 + %g2] ASI, %i1;			\
	lduwa	[%g1 + %g3] ASI, %i2;			\
	lduwa	[%g1 + %g5] ASI, %i3;			\
	add	%g1, 0x10, %g1;				\
	lduwa	[%g1 + %g0] ASI, %i4;			\
	lduwa	[%g1 + %g2] ASI, %i5;			\
	lduwa	[%g1 + %g3] ASI, %i6;			\
	lduwa	[%g1 + %g5] ASI, %i7;			\
	restored;					\
	retry; nop; nop; nop; nop;			\
	b,a,pt	%xcc, fill_fixup_dax;			\
	b,a,pt	%xcc, fill_fixup_mna;			\
	b,a,pt	%xcc, fill_fixup;

#define FILL_1_NORMAL FILL_1_GENERIC(ASI_AIUP)
#define FILL_2_NORMAL FILL_2_GENERIC(ASI_AIUP)
#define FILL_3_NORMAL FILL_0_NORMAL
#define FILL_4_NORMAL FILL_0_NORMAL
#define FILL_5_NORMAL FILL_0_NORMAL
#define FILL_6_NORMAL FILL_0_NORMAL
#define FILL_7_NORMAL FILL_0_NORMAL

#define FILL_0_OTHER FILL_0_NORMAL
#define FILL_1_OTHER FILL_1_GENERIC(ASI_AIUS)
#define FILL_2_OTHER FILL_2_GENERIC(ASI_AIUS)
#define FILL_3_OTHER FILL_3_NORMAL
#define FILL_4_OTHER FILL_4_NORMAL
#define FILL_5_OTHER FILL_5_NORMAL
#define FILL_6_OTHER FILL_6_NORMAL
#define FILL_7_OTHER FILL_7_NORMAL

#endif /* !(_SPARC64_TTABLE_H) */
