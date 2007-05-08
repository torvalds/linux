#ifndef __BFIN_ENTRY_H
#define __BFIN_ENTRY_H

#include <asm/setup.h>
#include <asm/page.h>

#ifdef __ASSEMBLY__

#define	LFLUSH_I_AND_D	0x00000808
#define	LSIGTRAP	5

/* process bits for task_struct.flags */
#define	PF_TRACESYS_OFF	3
#define	PF_TRACESYS_BIT	5
#define	PF_PTRACED_OFF	3
#define	PF_PTRACED_BIT	4
#define	PF_DTRACE_OFF	1
#define	PF_DTRACE_BIT	5

/* This one is used for exceptions, emulation, and NMI.  It doesn't push
   RETI and doesn't do cli.  */
#define SAVE_ALL_SYS		save_context_no_interrupts
/* This is used for all normal interrupts.  It saves a minimum of registers
   to the stack, loads the IRQ number, and jumps to common code.  */
#define INTERRUPT_ENTRY(N)						\
    [--sp] = SYSCFG;							\
									\
    [--sp] = P0;	/*orig_p0*/					\
    [--sp] = R0;	/*orig_r0*/					\
    [--sp] = (R7:0,P5:0);						\
    R0 = (N);								\
    jump __common_int_entry;

/* For timer interrupts, we need to save IPEND, since the user_mode
	   macro accesses it to determine where to account time.  */
#define TIMER_INTERRUPT_ENTRY(N)					\
    [--sp] = SYSCFG;							\
									\
    [--sp] = P0;	/*orig_p0*/					\
    [--sp] = R0;	/*orig_r0*/					\
    [--sp] = (R7:0,P5:0);						\
    p0.l = lo(IPEND);							\
    p0.h = hi(IPEND);							\
    r1 = [p0];								\
    R0 = (N);								\
    jump __common_int_entry;

/* This one pushes RETI without using CLI.  Interrupts are enabled.  */
#define SAVE_CONTEXT_SYSCALL	save_context_syscall
#define SAVE_CONTEXT		save_context_with_interrupts

#define RESTORE_ALL_SYS		restore_context_no_interrupts
#define RESTORE_CONTEXT		restore_context_with_interrupts

#endif				/* __ASSEMBLY__ */
#endif				/* __BFIN_ENTRY_H */
