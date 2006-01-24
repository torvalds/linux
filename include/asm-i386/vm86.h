#ifndef _LINUX_VM86_H
#define _LINUX_VM86_H

/*
 * I'm guessing at the VIF/VIP flag usage, but hope that this is how
 * the Pentium uses them. Linux will return from vm86 mode when both
 * VIF and VIP is set.
 *
 * On a Pentium, we could probably optimize the virtual flags directly
 * in the eflags register instead of doing it "by hand" in vflags...
 *
 * Linus
 */

#define TF_MASK		0x00000100
#define IF_MASK		0x00000200
#define IOPL_MASK	0x00003000
#define NT_MASK		0x00004000
#ifdef CONFIG_VM86
#define VM_MASK		0x00020000
#else
#define VM_MASK		0 /* ignored */
#endif
#define AC_MASK		0x00040000
#define VIF_MASK	0x00080000	/* virtual interrupt flag */
#define VIP_MASK	0x00100000	/* virtual interrupt pending */
#define ID_MASK		0x00200000

#define BIOSSEG		0x0f000

#define CPU_086		0
#define CPU_186		1
#define CPU_286		2
#define CPU_386		3
#define CPU_486		4
#define CPU_586		5

/*
 * Return values for the 'vm86()' system call
 */
#define VM86_TYPE(retval)	((retval) & 0xff)
#define VM86_ARG(retval)	((retval) >> 8)

#define VM86_SIGNAL	0	/* return due to signal */
#define VM86_UNKNOWN	1	/* unhandled GP fault - IO-instruction or similar */
#define VM86_INTx	2	/* int3/int x instruction (ARG = x) */
#define VM86_STI	3	/* sti/popf/iret instruction enabled virtual interrupts */

/*
 * Additional return values when invoking new vm86()
 */
#define VM86_PICRETURN	4	/* return due to pending PIC request */
#define VM86_TRAP	6	/* return due to DOS-debugger request */

/*
 * function codes when invoking new vm86()
 */
#define VM86_PLUS_INSTALL_CHECK	0
#define VM86_ENTER		1
#define VM86_ENTER_NO_BYPASS	2
#define	VM86_REQUEST_IRQ	3
#define VM86_FREE_IRQ		4
#define VM86_GET_IRQ_BITS	5
#define VM86_GET_AND_RESET_IRQ	6

/*
 * This is the stack-layout seen by the user space program when we have
 * done a translation of "SAVE_ALL" from vm86 mode. The real kernel layout
 * is 'kernel_vm86_regs' (see below).
 */

struct vm86_regs {
/*
 * normal regs, with special meaning for the segment descriptors..
 */
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	long __null_ds;
	long __null_es;
	long __null_fs;
	long __null_gs;
	long orig_eax;
	long eip;
	unsigned short cs, __csh;
	long eflags;
	long esp;
	unsigned short ss, __ssh;
/*
 * these are specific to v86 mode:
 */
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

struct revectored_struct {
	unsigned long __map[8];			/* 256 bits */
};

struct vm86_struct {
	struct vm86_regs regs;
	unsigned long flags;
	unsigned long screen_bitmap;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
};

/*
 * flags masks
 */
#define VM86_SCREEN_BITMAP	0x0001

struct vm86plus_info_struct {
	unsigned long force_return_for_pic:1;
	unsigned long vm86dbg_active:1;       /* for debugger */
	unsigned long vm86dbg_TFpendig:1;     /* for debugger */
	unsigned long unused:28;
	unsigned long is_vm86pus:1;	      /* for vm86 internal use */
	unsigned char vm86dbg_intxxtab[32];   /* for debugger */
};

struct vm86plus_struct {
	struct vm86_regs regs;
	unsigned long flags;
	unsigned long screen_bitmap;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
	struct vm86plus_info_struct vm86plus;
};

#ifdef __KERNEL__
/*
 * This is the (kernel) stack-layout when we have done a "SAVE_ALL" from vm86
 * mode - the main change is that the old segment descriptors aren't
 * useful any more and are forced to be zero by the kernel (and the
 * hardware when a trap occurs), and the real segment descriptors are
 * at the end of the structure. Look at ptrace.h to see the "normal"
 * setup. For user space layout see 'struct vm86_regs' above.
 */

struct kernel_vm86_regs {
/*
 * normal regs, with special meaning for the segment descriptors..
 */
	long ebx;
	long ecx;
	long edx;
	long esi;
	long edi;
	long ebp;
	long eax;
	long __null_ds;
	long __null_es;
	long orig_eax;
	long eip;
	unsigned short cs, __csh;
	long eflags;
	long esp;
	unsigned short ss, __ssh;
/*
 * these are specific to v86 mode:
 */
	unsigned short es, __esh;
	unsigned short ds, __dsh;
	unsigned short fs, __fsh;
	unsigned short gs, __gsh;
};

struct kernel_vm86_struct {
	struct kernel_vm86_regs regs;
/*
 * the below part remains on the kernel stack while we are in VM86 mode.
 * 'tss.esp0' then contains the address of VM86_TSS_ESP0 below, and when we
 * get forced back from VM86, the CPU and "SAVE_ALL" will restore the above
 * 'struct kernel_vm86_regs' with the then actual values.
 * Therefore, pt_regs in fact points to a complete 'kernel_vm86_struct'
 * in kernelspace, hence we need not reget the data from userspace.
 */
#define VM86_TSS_ESP0 flags
	unsigned long flags;
	unsigned long screen_bitmap;
	unsigned long cpu_type;
	struct revectored_struct int_revectored;
	struct revectored_struct int21_revectored;
	struct vm86plus_info_struct vm86plus;
	struct pt_regs *regs32;   /* here we save the pointer to the old regs */
/*
 * The below is not part of the structure, but the stack layout continues
 * this way. In front of 'return-eip' may be some data, depending on
 * compilation, so we don't rely on this and save the pointer to 'oldregs'
 * in 'regs32' above.
 * However, with GCC-2.7.2 and the current CFLAGS you see exactly this:

	long return-eip;        from call to vm86()
	struct pt_regs oldregs;  user space registers as saved by syscall
 */
};

#ifdef CONFIG_VM86

void handle_vm86_fault(struct kernel_vm86_regs *, long);
int handle_vm86_trap(struct kernel_vm86_regs *, long, int);

struct task_struct;
void release_vm86_irqs(struct task_struct *);

#else

#define handle_vm86_fault(a, b)
#define release_vm86_irqs(a)

static inline int handle_vm86_trap(struct kernel_vm86_regs *a, long b, int c) {
	return 0;
}

#endif /* CONFIG_VM86 */

#endif /* __KERNEL__ */

#endif
