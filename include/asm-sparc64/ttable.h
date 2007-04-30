/* $Id: ttable.h,v 1.18 2002/02/09 19:49:32 davem Exp $ */
#ifndef _SPARC64_TTABLE_H
#define _SPARC64_TTABLE_H

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
	ba,pt	%xcc, etrap;				\
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
	mov	handler, %g3;				\
	ba,pt	%xcc, utrap_trap;			\
	 mov	lvl, %g4;				\
	nop;						\
	nop;						\
	nop;						\
	nop;						\
	nop;

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
#define BREAKPOINT_TRAP TRAP(breakpoint_trap)

#ifdef CONFIG_TRACE_IRQFLAGS

#define TRAP_IRQ(routine, level)			\
	rdpr	%pil, %g2;				\
	wrpr	%g0, 15, %pil;				\
	sethi	%hi(1f-4), %g7;				\
	ba,pt	%xcc, etrap_irq;			\
	 or	%g7, %lo(1f-4), %g7;			\
	nop;						\
	nop;						\
	nop;						\
	.subsection	2;				\
1:	call	trace_hardirqs_off;			\
	 nop;						\
	mov	level, %o0;				\
	call	routine;				\
	 add	%sp, PTREGS_OFF, %o1;			\
	ba,a,pt	%xcc, rtrap_irq;			\
	.previous;

#else

#define TRAP_IRQ(routine, level)			\
	rdpr	%pil, %g2;				\
	wrpr	%g0, 15, %pil;				\
	ba,pt	%xcc, etrap_irq;			\
	 rd	%pc, %g7;				\
	mov	level, %o0;				\
	call	routine;				\
	 add	%sp, PTREGS_OFF, %o1;			\
	ba,a,pt	%xcc, rtrap_irq;
	
#endif

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

#define SUN4V_ITSB_MISS					\
	ldxa	[%g0] ASI_SCRATCHPAD, %g2;		\
	ldx	[%g2 + HV_FAULT_I_ADDR_OFFSET], %g4;	\
	ldx	[%g2 + HV_FAULT_I_CTX_OFFSET], %g5;	\
	srlx	%g4, 22, %g6;				\
	ba,pt	%xcc, sun4v_itsb_miss;			\
	 nop;						\
	nop;						\
	nop;

#define SUN4V_DTSB_MISS					\
	ldxa	[%g0] ASI_SCRATCHPAD, %g2;		\
	ldx	[%g2 + HV_FAULT_D_ADDR_OFFSET], %g4;	\
	ldx	[%g2 + HV_FAULT_D_CTX_OFFSET], %g5;	\
	srlx	%g4, 22, %g6;				\
	ba,pt	%xcc, sun4v_dtsb_miss;			\
	 nop;						\
	nop;						\
	nop;

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

#define SPILL_0_NORMAL_ETRAP				\
etrap_kernel_spill:					\
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
	saved;						\
	sub	%g1, 2, %g1;				\
	ba,pt	%xcc, etrap_save;			\
	wrpr	%g1, %cwp;				\
	nop; nop; nop; nop; nop; nop; nop; nop;		\
	nop; nop; nop; nop;

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

#define SPILL_1_GENERIC_ETRAP				\
etrap_user_spill_64bit:					\
	stxa	%l0, [%sp + STACK_BIAS + 0x00] %asi;	\
	stxa	%l1, [%sp + STACK_BIAS + 0x08] %asi;	\
	stxa	%l2, [%sp + STACK_BIAS + 0x10] %asi;	\
	stxa	%l3, [%sp + STACK_BIAS + 0x18] %asi;	\
	stxa	%l4, [%sp + STACK_BIAS + 0x20] %asi;	\
	stxa	%l5, [%sp + STACK_BIAS + 0x28] %asi;	\
	stxa	%l6, [%sp + STACK_BIAS + 0x30] %asi;	\
	stxa	%l7, [%sp + STACK_BIAS + 0x38] %asi;	\
	stxa	%i0, [%sp + STACK_BIAS + 0x40] %asi;	\
	stxa	%i1, [%sp + STACK_BIAS + 0x48] %asi;	\
	stxa	%i2, [%sp + STACK_BIAS + 0x50] %asi;	\
	stxa	%i3, [%sp + STACK_BIAS + 0x58] %asi;	\
	stxa	%i4, [%sp + STACK_BIAS + 0x60] %asi;	\
	stxa	%i5, [%sp + STACK_BIAS + 0x68] %asi;	\
	stxa	%i6, [%sp + STACK_BIAS + 0x70] %asi;	\
	stxa	%i7, [%sp + STACK_BIAS + 0x78] %asi;	\
	saved;						\
	sub	%g1, 2, %g1;				\
	ba,pt	%xcc, etrap_save;			\
	 wrpr	%g1, %cwp;				\
	nop; nop; nop; nop; nop;			\
	nop; nop; nop; nop;				\
	ba,a,pt	%xcc, etrap_spill_fixup_64bit;		\
	ba,a,pt	%xcc, etrap_spill_fixup_64bit;		\
	ba,a,pt	%xcc, etrap_spill_fixup_64bit;

#define SPILL_1_GENERIC_ETRAP_FIXUP			\
etrap_spill_fixup_64bit:				\
	ldub	[%g6 + TI_WSAVED], %g1;			\
	sll	%g1, 3, %g3;				\
	add	%g6, %g3, %g3;				\
	stx	%sp, [%g3 + TI_RWIN_SPTRS];		\
	sll	%g1, 7, %g3;				\
	add	%g6, %g3, %g3;				\
	stx	%l0, [%g3 + TI_REG_WINDOW + 0x00];	\
	stx	%l1, [%g3 + TI_REG_WINDOW + 0x08];	\
	stx	%l2, [%g3 + TI_REG_WINDOW + 0x10];	\
	stx	%l3, [%g3 + TI_REG_WINDOW + 0x18];	\
	stx	%l4, [%g3 + TI_REG_WINDOW + 0x20];	\
	stx	%l5, [%g3 + TI_REG_WINDOW + 0x28];	\
	stx	%l6, [%g3 + TI_REG_WINDOW + 0x30];	\
	stx	%l7, [%g3 + TI_REG_WINDOW + 0x38];	\
	stx	%i0, [%g3 + TI_REG_WINDOW + 0x40];	\
	stx	%i1, [%g3 + TI_REG_WINDOW + 0x48];	\
	stx	%i2, [%g3 + TI_REG_WINDOW + 0x50];	\
	stx	%i3, [%g3 + TI_REG_WINDOW + 0x58];	\
	stx	%i4, [%g3 + TI_REG_WINDOW + 0x60];	\
	stx	%i5, [%g3 + TI_REG_WINDOW + 0x68];	\
	stx	%i6, [%g3 + TI_REG_WINDOW + 0x70];	\
	stx	%i7, [%g3 + TI_REG_WINDOW + 0x78];	\
	add	%g1, 1, %g1;				\
	stb	%g1, [%g6 + TI_WSAVED];			\
	saved;						\
	rdpr	%cwp, %g1;				\
	sub	%g1, 2, %g1;				\
	ba,pt	%xcc, etrap_save;			\
	 wrpr	%g1, %cwp;				\
	nop; nop; nop

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

#define SPILL_2_GENERIC_ETRAP		\
etrap_user_spill_32bit:			\
	srl	%sp, 0, %sp;		\
	stwa	%l0, [%sp + 0x00] %asi;	\
	stwa	%l1, [%sp + 0x04] %asi;	\
	stwa	%l2, [%sp + 0x08] %asi;	\
	stwa	%l3, [%sp + 0x0c] %asi;	\
	stwa	%l4, [%sp + 0x10] %asi;	\
	stwa	%l5, [%sp + 0x14] %asi;	\
	stwa	%l6, [%sp + 0x18] %asi;	\
	stwa	%l7, [%sp + 0x1c] %asi;	\
	stwa	%i0, [%sp + 0x20] %asi;	\
	stwa	%i1, [%sp + 0x24] %asi;	\
	stwa	%i2, [%sp + 0x28] %asi;	\
	stwa	%i3, [%sp + 0x2c] %asi;	\
	stwa	%i4, [%sp + 0x30] %asi;	\
	stwa	%i5, [%sp + 0x34] %asi;	\
	stwa	%i6, [%sp + 0x38] %asi;	\
	stwa	%i7, [%sp + 0x3c] %asi;	\
	saved;				\
	sub	%g1, 2, %g1;		\
	ba,pt	%xcc, etrap_save;	\
	 wrpr	%g1, %cwp;		\
	nop; nop; nop; nop;		\
	nop; nop; nop; nop;		\
	ba,a,pt	%xcc, etrap_spill_fixup_32bit; \
	ba,a,pt	%xcc, etrap_spill_fixup_32bit; \
	ba,a,pt	%xcc, etrap_spill_fixup_32bit;

#define SPILL_2_GENERIC_ETRAP_FIXUP			\
etrap_spill_fixup_32bit:				\
	ldub	[%g6 + TI_WSAVED], %g1;			\
	sll	%g1, 3, %g3;				\
	add	%g6, %g3, %g3;				\
	stx	%sp, [%g3 + TI_RWIN_SPTRS];		\
	sll	%g1, 7, %g3;				\
	add	%g6, %g3, %g3;				\
	stw	%l0, [%g3 + TI_REG_WINDOW + 0x00];	\
	stw	%l1, [%g3 + TI_REG_WINDOW + 0x04];	\
	stw	%l2, [%g3 + TI_REG_WINDOW + 0x08];	\
	stw	%l3, [%g3 + TI_REG_WINDOW + 0x0c];	\
	stw	%l4, [%g3 + TI_REG_WINDOW + 0x10];	\
	stw	%l5, [%g3 + TI_REG_WINDOW + 0x14];	\
	stw	%l6, [%g3 + TI_REG_WINDOW + 0x18];	\
	stw	%l7, [%g3 + TI_REG_WINDOW + 0x1c];	\
	stw	%i0, [%g3 + TI_REG_WINDOW + 0x20];	\
	stw	%i1, [%g3 + TI_REG_WINDOW + 0x24];	\
	stw	%i2, [%g3 + TI_REG_WINDOW + 0x28];	\
	stw	%i3, [%g3 + TI_REG_WINDOW + 0x2c];	\
	stw	%i4, [%g3 + TI_REG_WINDOW + 0x30];	\
	stw	%i5, [%g3 + TI_REG_WINDOW + 0x34];	\
	stw	%i6, [%g3 + TI_REG_WINDOW + 0x38];	\
	stw	%i7, [%g3 + TI_REG_WINDOW + 0x3c];	\
	add	%g1, 1, %g1;				\
	stb	%g1, [%g6 + TI_WSAVED];			\
	saved;						\
	rdpr	%cwp, %g1;				\
	sub	%g1, 2, %g1;				\
	ba,pt	%xcc, etrap_save;			\
	 wrpr	%g1, %cwp;				\
	nop; nop; nop

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

#define FILL_0_NORMAL_RTRAP				\
kern_rtt_fill:						\
	rdpr	%cwp, %g1;				\
	sub	%g1, 1, %g1;				\
	wrpr	%g1, %cwp;				\
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
	restored;					\
	add	%g1, 1, %g1;				\
	ba,pt	%xcc, kern_rtt_restore;			\
	 wrpr	%g1, %cwp;				\
	nop; nop; nop; nop; nop;			\
	nop; nop; nop; nop;


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

#define FILL_1_GENERIC_RTRAP				\
user_rtt_fill_64bit:					\
	ldxa	[%sp + STACK_BIAS + 0x00] %asi, %l0;	\
	ldxa	[%sp + STACK_BIAS + 0x08] %asi, %l1;	\
	ldxa	[%sp + STACK_BIAS + 0x10] %asi, %l2;	\
	ldxa	[%sp + STACK_BIAS + 0x18] %asi, %l3;	\
	ldxa	[%sp + STACK_BIAS + 0x20] %asi, %l4;	\
	ldxa	[%sp + STACK_BIAS + 0x28] %asi, %l5;	\
	ldxa	[%sp + STACK_BIAS + 0x30] %asi, %l6;	\
	ldxa	[%sp + STACK_BIAS + 0x38] %asi, %l7;	\
	ldxa	[%sp + STACK_BIAS + 0x40] %asi, %i0;	\
	ldxa	[%sp + STACK_BIAS + 0x48] %asi, %i1;	\
	ldxa	[%sp + STACK_BIAS + 0x50] %asi, %i2;	\
	ldxa	[%sp + STACK_BIAS + 0x58] %asi, %i3;	\
	ldxa	[%sp + STACK_BIAS + 0x60] %asi, %i4;	\
	ldxa	[%sp + STACK_BIAS + 0x68] %asi, %i5;	\
	ldxa	[%sp + STACK_BIAS + 0x70] %asi, %i6;	\
	ldxa	[%sp + STACK_BIAS + 0x78] %asi, %i7;	\
	ba,pt	%xcc, user_rtt_pre_restore;		\
	 restored;					\
	nop; nop; nop; nop; nop; nop;			\
	nop; nop; nop; nop; nop;			\
	ba,a,pt	%xcc, user_rtt_fill_fixup;		\
	ba,a,pt	%xcc, user_rtt_fill_fixup;		\
	ba,a,pt	%xcc, user_rtt_fill_fixup;


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

#define FILL_2_GENERIC_RTRAP				\
user_rtt_fill_32bit:					\
	srl	%sp, 0, %sp;				\
	lduwa	[%sp + 0x00] %asi, %l0;			\
	lduwa	[%sp + 0x04] %asi, %l1;			\
	lduwa	[%sp + 0x08] %asi, %l2;			\
	lduwa	[%sp + 0x0c] %asi, %l3;			\
	lduwa	[%sp + 0x10] %asi, %l4;			\
	lduwa	[%sp + 0x14] %asi, %l5;			\
	lduwa	[%sp + 0x18] %asi, %l6;			\
	lduwa	[%sp + 0x1c] %asi, %l7;			\
	lduwa	[%sp + 0x20] %asi, %i0;			\
	lduwa	[%sp + 0x24] %asi, %i1;			\
	lduwa	[%sp + 0x28] %asi, %i2;			\
	lduwa	[%sp + 0x2c] %asi, %i3;			\
	lduwa	[%sp + 0x30] %asi, %i4;			\
	lduwa	[%sp + 0x34] %asi, %i5;			\
	lduwa	[%sp + 0x38] %asi, %i6;			\
	lduwa	[%sp + 0x3c] %asi, %i7;			\
	ba,pt	%xcc, user_rtt_pre_restore;		\
	 restored;					\
	nop; nop; nop; nop; nop;			\
	nop; nop; nop; nop; nop;			\
	ba,a,pt	%xcc, user_rtt_fill_fixup;		\
	ba,a,pt	%xcc, user_rtt_fill_fixup;		\
	ba,a,pt	%xcc, user_rtt_fill_fixup;
		

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
