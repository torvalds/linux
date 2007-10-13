/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */
#ifndef __UM_ELF_X86_64_H
#define __UM_ELF_X86_64_H

#include <linux/sched.h>
#include <asm/user.h>
#include "skas.h"

/* x86-64 relocation types, taken from asm-x86_64/elf.h */
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

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct { } elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) \
	((x)->e_machine == EM_X86_64)

#define ELF_CLASS	ELFCLASS64
#define ELF_DATA        ELFDATA2LSB
#define ELF_ARCH        EM_X86_64

#define ELF_PLAT_INIT(regs, load_addr)    do { \
	PT_REGS_RBX(regs) = 0; \
	PT_REGS_RCX(regs) = 0; \
	PT_REGS_RDX(regs) = 0; \
	PT_REGS_RSI(regs) = 0; \
	PT_REGS_RDI(regs) = 0; \
	PT_REGS_RBP(regs) = 0; \
	PT_REGS_RAX(regs) = 0; \
	PT_REGS_R8(regs) = 0; \
	PT_REGS_R9(regs) = 0; \
	PT_REGS_R10(regs) = 0; \
	PT_REGS_R11(regs) = 0; \
	PT_REGS_R12(regs) = 0; \
	PT_REGS_R13(regs) = 0; \
	PT_REGS_R14(regs) = 0; \
	PT_REGS_R15(regs) = 0; \
} while (0)

#define ELF_CORE_COPY_REGS(pr_reg, regs)		\
	(pr_reg)[0] = (regs)->regs.skas.regs[0];			\
	(pr_reg)[1] = (regs)->regs.skas.regs[1];			\
	(pr_reg)[2] = (regs)->regs.skas.regs[2];			\
	(pr_reg)[3] = (regs)->regs.skas.regs[3];			\
	(pr_reg)[4] = (regs)->regs.skas.regs[4];			\
	(pr_reg)[5] = (regs)->regs.skas.regs[5];			\
	(pr_reg)[6] = (regs)->regs.skas.regs[6];			\
	(pr_reg)[7] = (regs)->regs.skas.regs[7];			\
	(pr_reg)[8] = (regs)->regs.skas.regs[8];			\
	(pr_reg)[9] = (regs)->regs.skas.regs[9];			\
	(pr_reg)[10] = (regs)->regs.skas.regs[10];			\
	(pr_reg)[11] = (regs)->regs.skas.regs[11];			\
	(pr_reg)[12] = (regs)->regs.skas.regs[12];			\
	(pr_reg)[13] = (regs)->regs.skas.regs[13];			\
	(pr_reg)[14] = (regs)->regs.skas.regs[14];			\
	(pr_reg)[15] = (regs)->regs.skas.regs[15];			\
	(pr_reg)[16] = (regs)->regs.skas.regs[16];			\
	(pr_reg)[17] = (regs)->regs.skas.regs[17];			\
	(pr_reg)[18] = (regs)->regs.skas.regs[18];			\
	(pr_reg)[19] = (regs)->regs.skas.regs[19];			\
	(pr_reg)[20] = (regs)->regs.skas.regs[20];			\
	(pr_reg)[21] = current->thread.arch.fs;			\
	(pr_reg)[22] = 0;					\
	(pr_reg)[23] = 0;					\
	(pr_reg)[24] = 0;					\
	(pr_reg)[25] = 0;					\
	(pr_reg)[26] = 0;

static inline int elf_core_copy_fpregs(struct task_struct *t,
				       elf_fpregset_t *fpu)
{
	int cpu = current_thread->cpu;
	return save_fp_registers(userspace_pid[cpu], (unsigned long *) fpu);
}

#define ELF_CORE_COPY_FPREGS(t, fpu) elf_core_copy_fpregs(t, fpu)

#ifdef TIF_IA32 /* XXX */
#error XXX, indeed
        clear_thread_flag(TIF_IA32);
#endif

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE 4096

#define ELF_ET_DYN_BASE (2 * TASK_SIZE / 3)

extern long elf_aux_hwcap;
#define ELF_HWCAP (elf_aux_hwcap)

#define ELF_PLATFORM "x86_64"

#define SET_PERSONALITY(ex, ibcs2) do ; while(0)

#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
