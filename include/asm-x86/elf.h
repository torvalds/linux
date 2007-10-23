#ifndef _ASM_X86_ELF_H
#define _ASM_X86_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/auxvec.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_i387_struct elf_fpregset_t;

#ifdef __i386__

typedef struct user_fxsr_struct elf_fpxregset_t;

#define R_386_NONE	0
#define R_386_32	1
#define R_386_PC32	2
#define R_386_GOT32	3
#define R_386_PLT32	4
#define R_386_COPY	5
#define R_386_GLOB_DAT	6
#define R_386_JMP_SLOT	7
#define R_386_RELATIVE	8
#define R_386_GOTOFF	9
#define R_386_GOTPC	10
#define R_386_NUM	11

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_386

#else

/* x86-64 relocation types */
#define R_X86_64_NONE		0	/* No reloc */
#define R_X86_64_64		1	/* Direct 64 bit  */
#define R_X86_64_PC32		2	/* PC relative 32 bit signed */
#define R_X86_64_GOT32		3	/* 32 bit GOT entry */
#define R_X86_64_PLT32		4	/* 32 bit PLT address */
#define R_X86_64_COPY		5	/* Copy symbol at runtime */
#define R_X86_64_GLOB_DAT	6	/* Create GOT entry */
#define R_X86_64_JUMP_SLOT	7	/* Create PLT entry */
#define R_X86_64_RELATIVE	8	/* Adjust by program base */
#define R_X86_64_GOTPCREL	9	/* 32 bit signed pc relative
					   offset to GOT */
#define R_X86_64_32		10	/* Direct 32 bit zero extended */
#define R_X86_64_32S		11	/* Direct 32 bit sign extended */
#define R_X86_64_16		12	/* Direct 16 bit zero extended */
#define R_X86_64_PC16		13	/* 16 bit sign extended pc relative */
#define R_X86_64_8		14	/* Direct 8 bit sign extended  */
#define R_X86_64_PC8		15	/* 8 bit sign extended pc relative */

#define R_X86_64_NUM		16

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_X86_64

#endif

#ifdef __KERNEL__

#ifdef CONFIG_X86_32
#include <asm/processor.h>
#include <asm/system.h>		/* for savesegment */
#include <asm/desc.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	(((x)->e_machine == EM_386) || ((x)->e_machine == EM_486))

/* SVR4/i386 ABI (pages 3-31, 3-32) says that when the program starts %edx
   contains a pointer to a function which might be registered using `atexit'.
   This provides a mean for the dynamic linker to call DT_FINI functions for
   shared libraries that have been loaded before the code runs.

   A value of 0 tells we have no such handler.

   We might as well make sure everything else is cleared too (except for %esp),
   just to make things more deterministic.
 */
#define ELF_PLAT_INIT(_r, load_addr)	do { \
	_r->ebx = 0; _r->ecx = 0; _r->edx = 0; \
	_r->esi = 0; _r->edi = 0; _r->ebp = 0; \
	_r->eax = 0; \
} while (0)

/* regs is struct pt_regs, pr_reg is elf_gregset_t (which is
   now struct_user_regs, they are different) */

#define ELF_CORE_COPY_REGS(pr_reg, regs)		\
	pr_reg[0] = regs->ebx;				\
	pr_reg[1] = regs->ecx;				\
	pr_reg[2] = regs->edx;				\
	pr_reg[3] = regs->esi;				\
	pr_reg[4] = regs->edi;				\
	pr_reg[5] = regs->ebp;				\
	pr_reg[6] = regs->eax;				\
	pr_reg[7] = regs->xds & 0xffff;			\
	pr_reg[8] = regs->xes & 0xffff;			\
	pr_reg[9] = regs->xfs & 0xffff;			\
	savesegment(gs,pr_reg[10]);			\
	pr_reg[11] = regs->orig_eax;			\
	pr_reg[12] = regs->eip;				\
	pr_reg[13] = regs->xcs & 0xffff;		\
	pr_reg[14] = regs->eflags;			\
	pr_reg[15] = regs->esp;				\
	pr_reg[16] = regs->xss & 0xffff;

#define ELF_PLATFORM	(utsname()->machine)
#define set_personality_64bit()	do { } while (0)
extern unsigned int vdso_enabled;

#else /* CONFIG_X86_32 */

#include <asm/processor.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	((x)->e_machine == EM_X86_64)

#define ELF_PLAT_INIT(_r, load_addr)	do {		  \
	struct task_struct *cur = current;		  \
	(_r)->rbx = 0; (_r)->rcx = 0; (_r)->rdx = 0;	  \
	(_r)->rsi = 0; (_r)->rdi = 0; (_r)->rbp = 0;	  \
	(_r)->rax = 0;					  \
	(_r)->r8 = 0;					  \
	(_r)->r9 = 0;					  \
	(_r)->r10 = 0;					  \
	(_r)->r11 = 0;					  \
	(_r)->r12 = 0;					  \
	(_r)->r13 = 0;					  \
	(_r)->r14 = 0;					  \
	(_r)->r15 = 0;					  \
	cur->thread.fs = 0; cur->thread.gs = 0;		  \
	cur->thread.fsindex = 0; cur->thread.gsindex = 0; \
	cur->thread.ds = 0; cur->thread.es = 0;		  \
	clear_thread_flag(TIF_IA32);			  \
} while (0)

/* regs is struct pt_regs, pr_reg is elf_gregset_t (which is
   now struct_user_regs, they are different). Assumes current is the process
   getting dumped. */

#define ELF_CORE_COPY_REGS(pr_reg, regs)  do {			\
	unsigned v;						\
	(pr_reg)[0] = (regs)->r15;				\
	(pr_reg)[1] = (regs)->r14;				\
	(pr_reg)[2] = (regs)->r13;				\
	(pr_reg)[3] = (regs)->r12;				\
	(pr_reg)[4] = (regs)->rbp;				\
	(pr_reg)[5] = (regs)->rbx;				\
	(pr_reg)[6] = (regs)->r11;				\
	(pr_reg)[7] = (regs)->r10;				\
	(pr_reg)[8] = (regs)->r9;				\
	(pr_reg)[9] = (regs)->r8;				\
	(pr_reg)[10] = (regs)->rax;				\
	(pr_reg)[11] = (regs)->rcx;				\
	(pr_reg)[12] = (regs)->rdx;				\
	(pr_reg)[13] = (regs)->rsi;				\
	(pr_reg)[14] = (regs)->rdi;				\
	(pr_reg)[15] = (regs)->orig_rax;			\
	(pr_reg)[16] = (regs)->rip;				\
	(pr_reg)[17] = (regs)->cs;				\
	(pr_reg)[18] = (regs)->eflags;				\
	(pr_reg)[19] = (regs)->rsp;				\
	(pr_reg)[20] = (regs)->ss;				\
	(pr_reg)[21] = current->thread.fs;			\
	(pr_reg)[22] = current->thread.gs;			\
	asm("movl %%ds,%0" : "=r" (v)); (pr_reg)[23] = v;	\
	asm("movl %%es,%0" : "=r" (v)); (pr_reg)[24] = v;	\
	asm("movl %%fs,%0" : "=r" (v)); (pr_reg)[25] = v;	\
	asm("movl %%gs,%0" : "=r" (v)); (pr_reg)[26] = v;	\
} while(0);

/* I'm not sure if we can use '-' here */
#define ELF_PLATFORM       ("x86_64")
extern void set_personality_64bit(void);
extern int vdso_enabled;

#endif /* !CONFIG_X86_32 */

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE		(TASK_SIZE / 3 * 2)

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP		(boot_cpu_data.x86_capability[0])

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define SET_PERSONALITY(ex, ibcs2) set_personality_64bit()

/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically.
 */
#define elf_read_implies_exec(ex, executable_stack)	\
	(executable_stack != EXSTACK_DISABLE_X)

struct task_struct;

extern int dump_task_regs (struct task_struct *, elf_gregset_t *);
extern int dump_task_fpu (struct task_struct *, elf_fpregset_t *);

#define ELF_CORE_COPY_TASK_REGS(tsk, elf_regs) dump_task_regs(tsk, elf_regs)
#define ELF_CORE_COPY_FPREGS(tsk, elf_fpregs) dump_task_fpu(tsk, elf_fpregs)

#ifdef CONFIG_X86_32
extern int dump_task_extended_fpu (struct task_struct *,
				   struct user_fxsr_struct *);
#define ELF_CORE_COPY_XFPREGS(tsk, elf_xfpregs) \
	dump_task_extended_fpu(tsk, elf_xfpregs)
#define ELF_CORE_XFPREG_TYPE NT_PRXFPREG

#define VDSO_HIGH_BASE		(__fix_to_virt(FIX_VDSO))
#define VDSO_CURRENT_BASE	((unsigned long)current->mm->context.vdso)
#define VDSO_PRELINK		0

#define VDSO_SYM(x) \
		(VDSO_CURRENT_BASE + (unsigned long)(x) - VDSO_PRELINK)

#define VDSO_HIGH_EHDR		((const struct elfhdr *) VDSO_HIGH_BASE)
#define VDSO_EHDR		((const struct elfhdr *) VDSO_CURRENT_BASE)

extern void __kernel_vsyscall;

#define VDSO_ENTRY		VDSO_SYM(&__kernel_vsyscall)

/* update AT_VECTOR_SIZE_ARCH if the number of NEW_AUX_ENT entries changes */

#define ARCH_DLINFO \
do if (vdso_enabled) {							\
		NEW_AUX_ENT(AT_SYSINFO,	VDSO_ENTRY);			\
		NEW_AUX_ENT(AT_SYSINFO_EHDR, VDSO_CURRENT_BASE);	\
} while (0)

#else /* CONFIG_X86_32 */

/* 1GB for 64bit, 8MB for 32bit */
#define STACK_RND_MASK (test_thread_flag(TIF_IA32) ? 0x7ff : 0x3fffff)

#define ARCH_DLINFO						\
do if (vdso_enabled) {						\
	NEW_AUX_ENT(AT_SYSINFO_EHDR,(unsigned long)current->mm->context.vdso);\
} while (0)

#endif /* !CONFIG_X86_32 */

struct linux_binprm;

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
				       int executable_stack);

#endif /* __KERNEL__ */

#endif
