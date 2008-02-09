/*
 *  include/asm-s390/elf.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/elf.h"
 */

#ifndef __ASMS390_ELF_H
#define __ASMS390_ELF_H

/* s390 relocations defined by the ABIs */
#define R_390_NONE		0	/* No reloc.  */
#define R_390_8			1	/* Direct 8 bit.  */
#define R_390_12		2	/* Direct 12 bit.  */
#define R_390_16		3	/* Direct 16 bit.  */
#define R_390_32		4	/* Direct 32 bit.  */
#define R_390_PC32		5	/* PC relative 32 bit.	*/
#define R_390_GOT12		6	/* 12 bit GOT offset.  */
#define R_390_GOT32		7	/* 32 bit GOT offset.  */
#define R_390_PLT32		8	/* 32 bit PC relative PLT address.  */
#define R_390_COPY		9	/* Copy symbol at runtime.  */
#define R_390_GLOB_DAT		10	/* Create GOT entry.  */
#define R_390_JMP_SLOT		11	/* Create PLT entry.  */
#define R_390_RELATIVE		12	/* Adjust by program base.  */
#define R_390_GOTOFF32		13	/* 32 bit offset to GOT.	 */
#define R_390_GOTPC		14	/* 32 bit PC rel. offset to GOT.  */
#define R_390_GOT16		15	/* 16 bit GOT offset.  */
#define R_390_PC16		16	/* PC relative 16 bit.	*/
#define R_390_PC16DBL		17	/* PC relative 16 bit shifted by 1.  */
#define R_390_PLT16DBL		18	/* 16 bit PC rel. PLT shifted by 1.  */
#define R_390_PC32DBL		19	/* PC relative 32 bit shifted by 1.  */
#define R_390_PLT32DBL		20	/* 32 bit PC rel. PLT shifted by 1.  */
#define R_390_GOTPCDBL		21	/* 32 bit PC rel. GOT shifted by 1.  */
#define R_390_64		22	/* Direct 64 bit.  */
#define R_390_PC64		23	/* PC relative 64 bit.	*/
#define R_390_GOT64		24	/* 64 bit GOT offset.  */
#define R_390_PLT64		25	/* 64 bit PC relative PLT address.  */
#define R_390_GOTENT		26	/* 32 bit PC rel. to GOT entry >> 1. */
#define R_390_GOTOFF16		27	/* 16 bit offset to GOT. */
#define R_390_GOTOFF64		28	/* 64 bit offset to GOT. */
#define R_390_GOTPLT12		29	/* 12 bit offset to jump slot.	*/
#define R_390_GOTPLT16		30	/* 16 bit offset to jump slot.	*/
#define R_390_GOTPLT32		31	/* 32 bit offset to jump slot.	*/
#define R_390_GOTPLT64		32	/* 64 bit offset to jump slot.	*/
#define R_390_GOTPLTENT		33	/* 32 bit rel. offset to jump slot.  */
#define R_390_PLTOFF16		34	/* 16 bit offset from GOT to PLT. */
#define R_390_PLTOFF32		35	/* 32 bit offset from GOT to PLT. */
#define R_390_PLTOFF64		36	/* 16 bit offset from GOT to PLT. */
#define R_390_TLS_LOAD		37	/* Tag for load insn in TLS code. */
#define R_390_TLS_GDCALL	38	/* Tag for function call in general
                                           dynamic TLS code.  */
#define R_390_TLS_LDCALL	39	/* Tag for function call in local
                                           dynamic TLS code.  */
#define R_390_TLS_GD32		40	/* Direct 32 bit for general dynamic
                                           thread local data.  */
#define R_390_TLS_GD64		41	/* Direct 64 bit for general dynamic
                                           thread local data.  */
#define R_390_TLS_GOTIE12	42	/* 12 bit GOT offset for static TLS
                                           block offset.  */
#define R_390_TLS_GOTIE32	43	/* 32 bit GOT offset for static TLS
                                           block offset.  */
#define R_390_TLS_GOTIE64	44	/* 64 bit GOT offset for static TLS
                                           block offset.  */
#define R_390_TLS_LDM32		45	/* Direct 32 bit for local dynamic
                                           thread local data in LD code.  */
#define R_390_TLS_LDM64		46	/* Direct 64 bit for local dynamic
                                           thread local data in LD code.  */
#define R_390_TLS_IE32		47	/* 32 bit address of GOT entry for
                                           negated static TLS block offset.  */
#define R_390_TLS_IE64		48	/* 64 bit address of GOT entry for
                                           negated static TLS block offset.  */
#define R_390_TLS_IEENT		49	/* 32 bit rel. offset to GOT entry for
                                           negated static TLS block offset.  */
#define R_390_TLS_LE32		50	/* 32 bit negated offset relative to
                                           static TLS block.  */
#define R_390_TLS_LE64		51	/* 64 bit negated offset relative to
                                           static TLS block.  */
#define R_390_TLS_LDO32		52	/* 32 bit offset relative to TLS
                                           block.  */
#define R_390_TLS_LDO64		53	/* 64 bit offset relative to TLS
                                           block.  */
#define R_390_TLS_DTPMOD	54	/* ID of module containing symbol.  */
#define R_390_TLS_DTPOFF	55	/* Offset in TLS block.  */
#define R_390_TLS_TPOFF		56	/* Negate offset in static TLS
                                           block.  */
#define R_390_20		57	/* Direct 20 bit.  */
#define R_390_GOT20		58	/* 20 bit GOT offset.  */
#define R_390_GOTPLT20		59	/* 20 bit offset to jump slot.  */
#define R_390_TLS_GOTIE20	60	/* 20 bit GOT offset for static TLS
					   block offset.  */
/* Keep this the last entry.  */
#define R_390_NUM	61

/*
 * These are used to set parameters in the core dumps.
 */
#ifndef __s390x__
#define ELF_CLASS	ELFCLASS32
#else /* __s390x__ */
#define ELF_CLASS	ELFCLASS64
#endif /* __s390x__ */
#define ELF_DATA	ELFDATA2MSB
#define ELF_ARCH	EM_S390

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/user.h>

typedef s390_fp_regs elf_fpregset_t;
typedef s390_regs elf_gregset_t;

#include <linux/sched.h>	/* for task_struct */
#include <asm/system.h>		/* for save_access_regs */
#include <asm/mmu_context.h>

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	(((x)->e_machine == EM_S390 || (x)->e_machine == EM_S390_OLD) \
         && (x)->e_ident[EI_CLASS] == ELF_CLASS) 

/* For SVR4/S390 the function pointer to be registered with `atexit` is
   passed in R14. */
#define ELF_PLAT_INIT(_r, load_addr) \
	do { \
		_r->gprs[14] = 0; \
	} while (0)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */
#define ELF_ET_DYN_BASE		(STACK_TOP / 3 * 2)

/* Wow, the "main" arch needs arch dependent functions too.. :) */

/* regs is struct pt_regs, pr_reg is elf_gregset_t (which is
   now struct_user_regs, they are different) */

static inline int dump_regs(struct pt_regs *ptregs, elf_gregset_t *regs)
{
	memcpy(&regs->psw, &ptregs->psw, sizeof(regs->psw)+sizeof(regs->gprs));
	save_access_regs(regs->acrs);
	regs->orig_gpr2 = ptregs->orig_gpr2;
	return 1;
}

#define ELF_CORE_COPY_REGS(pr_reg, regs) dump_regs(regs, &pr_reg);

static inline int dump_task_regs(struct task_struct *tsk, elf_gregset_t *regs)
{
	struct pt_regs *ptregs = task_pt_regs(tsk);
	memcpy(&regs->psw, &ptregs->psw, sizeof(regs->psw)+sizeof(regs->gprs));
	memcpy(regs->acrs, tsk->thread.acrs, sizeof(regs->acrs));
	regs->orig_gpr2 = ptregs->orig_gpr2;
	return 1;
}

#define ELF_CORE_COPY_TASK_REGS(tsk, regs) dump_task_regs(tsk, regs)

static inline int dump_task_fpu(struct task_struct *tsk, elf_fpregset_t *fpregs)
{
	if (tsk == current)
		save_fp_regs(fpregs);
	else
		memcpy(fpregs, &tsk->thread.fp_regs, sizeof(elf_fpregset_t));
	return 1;
}

#define ELF_CORE_COPY_FPREGS(tsk, fpregs) dump_task_fpu(tsk, fpregs)


/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports. */

extern unsigned long elf_hwcap;
#define ELF_HWCAP (elf_hwcap)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM_SIZE 8
extern char elf_platform[];
#define ELF_PLATFORM (elf_platform)

#ifndef __s390x__
#define SET_PERSONALITY(ex, ibcs2) set_personality((ibcs2)?PER_SVR4:PER_LINUX)
#else /* __s390x__ */
#define SET_PERSONALITY(ex, ibcs2)			\
do {							\
	if (ibcs2)					\
		set_personality(PER_SVR4);		\
	else if (current->personality != PER_LINUX32)	\
		set_personality(PER_LINUX);		\
	clear_thread_flag(TIF_31BIT);			\
} while (0)
#endif /* __s390x__ */

/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically.
 */
#define elf_read_implies_exec(ex, executable_stack)	\
({							\
	if (current->mm->context.noexec &&		\
	    executable_stack != EXSTACK_DISABLE_X)	\
		disable_noexec(current->mm, current);	\
	current->mm->context.noexec == 0;		\
})

#endif
