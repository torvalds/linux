#ifndef __ASMi386_ELF_H
#define __ASMi386_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/user.h>
#include <asm/auxvec.h>

#include <linux/utsname.h>

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

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_i387_struct elf_fpregset_t;
typedef struct user_fxsr_struct elf_fpxregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	(((x)->e_machine == EM_386) || ((x)->e_machine == EM_486))

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB
#define ELF_ARCH	EM_386

#ifdef __KERNEL__

#include <asm/processor.h>
#include <asm/system.h>		/* for savesegment */
#include <asm/desc.h>

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

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (TASK_SIZE / 3 * 2)

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
	pr_reg[7] = regs->xds;				\
	pr_reg[8] = regs->xes;				\
	savesegment(fs,pr_reg[9]);			\
	pr_reg[10] = regs->xgs;				\
	pr_reg[11] = regs->orig_eax;			\
	pr_reg[12] = regs->eip;				\
	pr_reg[13] = regs->xcs;				\
	pr_reg[14] = regs->eflags;			\
	pr_reg[15] = regs->esp;				\
	pr_reg[16] = regs->xss;

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP	(boot_cpu_data.x86_capability[0])

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM  (utsname()->machine)

#define SET_PERSONALITY(ex, ibcs2) do { } while (0)

/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically.
 */
#define elf_read_implies_exec(ex, executable_stack)	(executable_stack != EXSTACK_DISABLE_X)

struct task_struct;

extern int dump_task_regs (struct task_struct *, elf_gregset_t *);
extern int dump_task_fpu (struct task_struct *, elf_fpregset_t *);
extern int dump_task_extended_fpu (struct task_struct *, struct user_fxsr_struct *);

#define ELF_CORE_COPY_TASK_REGS(tsk, elf_regs) dump_task_regs(tsk, elf_regs)
#define ELF_CORE_COPY_FPREGS(tsk, elf_fpregs) dump_task_fpu(tsk, elf_fpregs)
#define ELF_CORE_COPY_XFPREGS(tsk, elf_xfpregs) dump_task_extended_fpu(tsk, elf_xfpregs)

#define VDSO_HIGH_BASE		(__fix_to_virt(FIX_VDSO))
#define VDSO_BASE		((unsigned long)current->mm->context.vdso)

#ifdef CONFIG_COMPAT_VDSO
# define VDSO_COMPAT_BASE	VDSO_HIGH_BASE
# define VDSO_PRELINK		VDSO_HIGH_BASE
#else
# define VDSO_COMPAT_BASE	VDSO_BASE
# define VDSO_PRELINK		0
#endif

#define VDSO_COMPAT_SYM(x) \
		(VDSO_COMPAT_BASE + (unsigned long)(x) - VDSO_PRELINK)

#define VDSO_SYM(x) \
		(VDSO_BASE + (unsigned long)(x) - VDSO_PRELINK)

#define VDSO_HIGH_EHDR		((const struct elfhdr *) VDSO_HIGH_BASE)
#define VDSO_EHDR		((const struct elfhdr *) VDSO_COMPAT_BASE)

extern void __kernel_vsyscall;

#define VDSO_ENTRY		VDSO_SYM(&__kernel_vsyscall)

#define ARCH_HAS_SETUP_ADDITIONAL_PAGES
struct linux_binprm;
extern int arch_setup_additional_pages(struct linux_binprm *bprm,
                                       int executable_stack);

extern unsigned int vdso_enabled;

#define ARCH_DLINFO						\
do if (vdso_enabled) {						\
		NEW_AUX_ENT(AT_SYSINFO,	VDSO_ENTRY);		\
		NEW_AUX_ENT(AT_SYSINFO_EHDR, VDSO_COMPAT_BASE);	\
} while (0)

/*
 * These macros parameterize elf_core_dump in fs/binfmt_elf.c to write out
 * extra segments containing the vsyscall DSO contents.  Dumping its
 * contents makes post-mortem fully interpretable later without matching up
 * the same kernel and hardware config to see what PC values meant.
 * Dumping its extra ELF program headers includes all the other information
 * a debugger needs to easily find how the vsyscall DSO was being used.
 */
#define ELF_CORE_EXTRA_PHDRS		(VDSO_HIGH_EHDR->e_phnum)
#define ELF_CORE_WRITE_EXTRA_PHDRS					      \
do {									      \
	const struct elf_phdr *const vsyscall_phdrs =			      \
		(const struct elf_phdr *) (VDSO_HIGH_BASE		      \
					   + VDSO_HIGH_EHDR->e_phoff);    \
	int i;								      \
	Elf32_Off ofs = 0;						      \
	for (i = 0; i < VDSO_HIGH_EHDR->e_phnum; ++i) {		      \
		struct elf_phdr phdr = vsyscall_phdrs[i];		      \
		if (phdr.p_type == PT_LOAD) {				      \
			BUG_ON(ofs != 0);				      \
			ofs = phdr.p_offset = offset;			      \
			phdr.p_memsz = PAGE_ALIGN(phdr.p_memsz);	      \
			phdr.p_filesz = phdr.p_memsz;			      \
			offset += phdr.p_filesz;			      \
		}							      \
		else							      \
			phdr.p_offset += ofs;				      \
		phdr.p_paddr = 0; /* match other core phdrs */		      \
		DUMP_WRITE(&phdr, sizeof(phdr));			      \
	}								      \
} while (0)
#define ELF_CORE_WRITE_EXTRA_DATA					      \
do {									      \
	const struct elf_phdr *const vsyscall_phdrs =			      \
		(const struct elf_phdr *) (VDSO_HIGH_BASE		      \
					   + VDSO_HIGH_EHDR->e_phoff);    \
	int i;								      \
	for (i = 0; i < VDSO_HIGH_EHDR->e_phnum; ++i) {		      \
		if (vsyscall_phdrs[i].p_type == PT_LOAD)		      \
			DUMP_WRITE((void *) vsyscall_phdrs[i].p_vaddr,	      \
				   PAGE_ALIGN(vsyscall_phdrs[i].p_memsz));    \
	}								      \
} while (0)

#endif

#endif
