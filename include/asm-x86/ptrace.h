#ifndef ASM_X86__PTRACE_H
#define ASM_X86__PTRACE_H

#include <linux/compiler.h>	/* For __user */
#include <asm/ptrace-abi.h>
#include <asm/processor-flags.h>

#ifdef __KERNEL__
#include <asm/ds.h>		/* the DS BTS struct is used for ptrace too */
#include <asm/segment.h>
#endif

#ifndef __ASSEMBLY__

#ifdef __i386__
/* this struct defines the way the registers are stored on the
   stack during a system call. */

#ifndef __KERNEL__

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
	/* int  gs; */
	long orig_eax;
	long eip;
	int  xcs;
	long eflags;
	long esp;
	int  xss;
};

#else /* __KERNEL__ */

struct pt_regs {
	unsigned long bx;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
	unsigned long bp;
	unsigned long ax;
	unsigned long ds;
	unsigned long es;
	unsigned long fs;
	/* int  gs; */
	unsigned long orig_ax;
	unsigned long ip;
	unsigned long cs;
	unsigned long flags;
	unsigned long sp;
	unsigned long ss;
};

#endif /* __KERNEL__ */

#else /* __i386__ */

#ifndef __KERNEL__

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

#else /* __KERNEL__ */

struct pt_regs {
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
	unsigned long bp;
	unsigned long bx;
/* arguments: non interrupts/non tracing syscalls only save upto here*/
	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long ax;
	unsigned long cx;
	unsigned long dx;
	unsigned long si;
	unsigned long di;
	unsigned long orig_ax;
/* end of arguments */
/* cpu exception frame or undefined */
	unsigned long ip;
	unsigned long cs;
	unsigned long flags;
	unsigned long sp;
	unsigned long ss;
/* top of stack page */
};

#endif /* __KERNEL__ */
#endif /* !__i386__ */


#ifdef CONFIG_X86_PTRACE_BTS
/* a branch trace record entry
 *
 * In order to unify the interface between various processor versions,
 * we use the below data structure for all processors.
 */
enum bts_qualifier {
	BTS_INVALID = 0,
	BTS_BRANCH,
	BTS_TASK_ARRIVES,
	BTS_TASK_DEPARTS
};

struct bts_struct {
	__u64 qualifier;
	union {
		/* BTS_BRANCH */
		struct {
			__u64 from_ip;
			__u64 to_ip;
		} lbr;
		/* BTS_TASK_ARRIVES or
		   BTS_TASK_DEPARTS */
		__u64 jiffies;
	} variant;
};
#endif /* CONFIG_X86_PTRACE_BTS */

#ifdef __KERNEL__

#include <linux/init.h>

struct cpuinfo_x86;
struct task_struct;

#ifdef CONFIG_X86_PTRACE_BTS
extern void __cpuinit ptrace_bts_init_intel(struct cpuinfo_x86 *);
extern void ptrace_bts_take_timestamp(struct task_struct *, enum bts_qualifier);
#else
#define ptrace_bts_init_intel(config) do {} while (0)
#endif /* CONFIG_X86_PTRACE_BTS */

extern unsigned long profile_pc(struct pt_regs *regs);

extern unsigned long
convert_ip_to_linear(struct task_struct *child, struct pt_regs *regs);

#ifdef CONFIG_X86_32
extern void send_sigtrap(struct task_struct *tsk, struct pt_regs *regs,
			 int error_code);
#else
void signal_fault(struct pt_regs *regs, void __user *frame, char *where);
#endif

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->ax;
}

/*
 * user_mode_vm(regs) determines whether a register set came from user mode.
 * This is true if V8086 mode was enabled OR if the register set was from
 * protected mode with RPL-3 CS value.  This tricky test checks that with
 * one comparison.  Many places in the kernel can bypass this full check
 * if they have already ruled out V8086 mode, so user_mode(regs) can be used.
 */
static inline int user_mode(struct pt_regs *regs)
{
#ifdef CONFIG_X86_32
	return (regs->cs & SEGMENT_RPL_MASK) == USER_RPL;
#else
	return !!(regs->cs & 3);
#endif
}

static inline int user_mode_vm(struct pt_regs *regs)
{
#ifdef CONFIG_X86_32
	return ((regs->cs & SEGMENT_RPL_MASK) | (regs->flags & X86_VM_MASK)) >=
		USER_RPL;
#else
	return user_mode(regs);
#endif
}

static inline int v8086_mode(struct pt_regs *regs)
{
#ifdef CONFIG_X86_32
	return (regs->flags & X86_VM_MASK);
#else
	return 0;	/* No V86 mode support in long mode */
#endif
}

/*
 * X86_32 CPUs don't save ss and esp if the CPU is already in kernel mode
 * when it traps.  So regs will be the current sp.
 *
 * This is valid only for kernel mode traps.
 */
static inline unsigned long kernel_trap_sp(struct pt_regs *regs)
{
#ifdef CONFIG_X86_32
	return (unsigned long)regs;
#else
	return regs->sp;
#endif
}

static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->ip;
}

static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return regs->bp;
}

/*
 * These are defined as per linux/ptrace.h, which see.
 */
#define arch_has_single_step()	(1)
extern void user_enable_single_step(struct task_struct *);
extern void user_disable_single_step(struct task_struct *);

extern void user_enable_block_step(struct task_struct *);
#ifdef CONFIG_X86_DEBUGCTLMSR
#define arch_has_block_step()	(1)
#else
#define arch_has_block_step()	(boot_cpu_data.x86 >= 6)
#endif

struct user_desc;
extern int do_get_thread_area(struct task_struct *p, int idx,
			      struct user_desc __user *info);
extern int do_set_thread_area(struct task_struct *p, int idx,
			      struct user_desc __user *info, int can_allocate);

#define __ARCH_WANT_COMPAT_SYS_PTRACE

#endif /* __KERNEL__ */

#endif /* !__ASSEMBLY__ */

#endif /* ASM_X86__PTRACE_H */
