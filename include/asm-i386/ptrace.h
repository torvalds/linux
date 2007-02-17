#ifndef _I386_PTRACE_H
#define _I386_PTRACE_H

#include <asm/ptrace-abi.h>

/* this struct defines the way the registers are stored on the 
   stack during a system call. */

struct pt_regs {
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	int  xds;
	int  xes;
	int  xfs;
	/* int  xgs; */
	long orig_eax;
	long eip;
	int  xcs;
	long eflags;
	long esp;
	int  xss;
};

#ifdef __KERNEL__

#include <asm/vm86.h>
#include <asm/segment.h>

struct task_struct;
extern void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs, int error_code);

/*
 * user_mode_vm(regs) determines whether a register set came from user mode.
 * This is true if V8086 mode was enabled OR if the register set was from
 * protected mode with RPL-3 CS value.  This tricky test checks that with
 * one comparison.  Many places in the kernel can bypass this full check
 * if they have already ruled out V8086 mode, so user_mode(regs) can be used.
 */
static inline int user_mode(struct pt_regs *regs)
{
	return (regs->xcs & SEGMENT_RPL_MASK) == USER_RPL;
}
static inline int user_mode_vm(struct pt_regs *regs)
{
	return ((regs->xcs & SEGMENT_RPL_MASK) | (regs->eflags & VM_MASK)) >= USER_RPL;
}
static inline int v8086_mode(struct pt_regs *regs)
{
	return (regs->eflags & VM_MASK);
}

#define instruction_pointer(regs) ((regs)->eip)
#define regs_return_value(regs) ((regs)->eax)

extern unsigned long profile_pc(struct pt_regs *regs);
#endif /* __KERNEL__ */

#endif
