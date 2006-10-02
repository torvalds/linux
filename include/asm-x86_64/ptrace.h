#ifndef _X86_64_PTRACE_H
#define _X86_64_PTRACE_H

#include <asm/ptrace-abi.h>

#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long rbp;
	unsigned long rbx;
/* arguments: non interrupts/non tracing syscalls only save upto here*/
 	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long rax;
	unsigned long rcx;
	unsigned long rdx;
	unsigned long rsi;
	unsigned long rdi;
	unsigned long orig_rax;
/* end of arguments */
/* cpu exception frame or undefined */
	unsigned long rip;
	unsigned long cs;
	unsigned long eflags;
	unsigned long rsp;
	unsigned long ss;
/* top of stack page */
};

#endif

#if defined(__KERNEL__) && !defined(__ASSEMBLY__) 
#define user_mode(regs) (!!((regs)->cs & 3))
#define user_mode_vm(regs) user_mode(regs)
#define instruction_pointer(regs) ((regs)->rip)
#define regs_return_value(regs) ((regs)->rax)

extern unsigned long profile_pc(struct pt_regs *regs);
void signal_fault(struct pt_regs *regs, void __user *frame, char *where);

struct task_struct;

extern unsigned long
convert_rip_to_linear(struct task_struct *child, struct pt_regs *regs);

enum {
        EF_CF   = 0x00000001,
        EF_PF   = 0x00000004,
        EF_AF   = 0x00000010,
        EF_ZF   = 0x00000040,
        EF_SF   = 0x00000080,
        EF_TF   = 0x00000100,
        EF_IE   = 0x00000200,
        EF_DF   = 0x00000400,
        EF_OF   = 0x00000800,
        EF_IOPL = 0x00003000,
        EF_IOPL_RING0 = 0x00000000,
        EF_IOPL_RING1 = 0x00001000,
        EF_IOPL_RING2 = 0x00002000,
        EF_NT   = 0x00004000,   /* nested task */
        EF_RF   = 0x00010000,   /* resume */
        EF_VM   = 0x00020000,   /* virtual mode */
        EF_AC   = 0x00040000,   /* alignment */
        EF_VIF  = 0x00080000,   /* virtual interrupt */
        EF_VIP  = 0x00100000,   /* virtual interrupt pending */
        EF_ID   = 0x00200000,   /* id */
};

#endif

#endif
