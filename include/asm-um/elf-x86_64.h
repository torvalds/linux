/*
 * Copyright 2003 PathScale, Inc.
 *
 * Licensed under the GPL
 */
#ifndef __UM_ELF_X86_64_H
#define __UM_ELF_X86_64_H

#include <asm/user.h>

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

#ifdef TIF_IA32 /* XXX */
        clear_thread_flag(TIF_IA32); \
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
