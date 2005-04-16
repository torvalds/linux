#ifndef __PPC_ELF_H
#define __PPC_ELF_H

/*
 * ELF register definitions..
 */
#include <asm/types.h>
#include <asm/ptrace.h>
#include <asm/cputable.h>

/* PowerPC relocations defined by the ABIs */
#define R_PPC_NONE		0
#define R_PPC_ADDR32		1	/* 32bit absolute address */
#define R_PPC_ADDR24		2	/* 26bit address, 2 bits ignored.  */
#define R_PPC_ADDR16		3	/* 16bit absolute address */
#define R_PPC_ADDR16_LO		4	/* lower 16bit of absolute address */
#define R_PPC_ADDR16_HI		5	/* high 16bit of absolute address */
#define R_PPC_ADDR16_HA		6	/* adjusted high 16bit */
#define R_PPC_ADDR14		7	/* 16bit address, 2 bits ignored */
#define R_PPC_ADDR14_BRTAKEN	8
#define R_PPC_ADDR14_BRNTAKEN	9
#define R_PPC_REL24		10	/* PC relative 26 bit */
#define R_PPC_REL14		11	/* PC relative 16 bit */
#define R_PPC_REL14_BRTAKEN	12
#define R_PPC_REL14_BRNTAKEN	13
#define R_PPC_GOT16		14
#define R_PPC_GOT16_LO		15
#define R_PPC_GOT16_HI		16
#define R_PPC_GOT16_HA		17
#define R_PPC_PLTREL24		18
#define R_PPC_COPY		19
#define R_PPC_GLOB_DAT		20
#define R_PPC_JMP_SLOT		21
#define R_PPC_RELATIVE		22
#define R_PPC_LOCAL24PC		23
#define R_PPC_UADDR32		24
#define R_PPC_UADDR16		25
#define R_PPC_REL32		26
#define R_PPC_PLT32		27
#define R_PPC_PLTREL32		28
#define R_PPC_PLT16_LO		29
#define R_PPC_PLT16_HI		30
#define R_PPC_PLT16_HA		31
#define R_PPC_SDAREL16		32
#define R_PPC_SECTOFF		33
#define R_PPC_SECTOFF_LO	34
#define R_PPC_SECTOFF_HI	35
#define R_PPC_SECTOFF_HA	36
/* Keep this the last entry.  */
#define R_PPC_NUM		37

#define ELF_NGREG	48	/* includes nip, msr, lr, etc. */
#define ELF_NFPREG	33	/* includes fpscr */
#define ELF_NVRREG	33	/* includes vscr */
#define ELF_NEVRREG	34	/* includes acc (as 2) */

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_ARCH	EM_PPC
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2MSB

/* General registers */
typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* Floating point registers */
typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

/* Altivec registers */
typedef __vector128 elf_vrreg_t;
typedef elf_vrreg_t elf_vrregset_t[ELF_NVRREG];

#ifdef __KERNEL__

struct task_struct;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */

#define elf_check_arch(x) ((x)->e_machine == EM_PPC)

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (0x08000000)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

#define ELF_CORE_COPY_REGS(gregs, regs)				\
	memcpy((gregs), (regs), sizeof(struct pt_regs));	\
	memset((char *)(gregs) + sizeof(struct pt_regs), 0,	\
	       sizeof(elf_gregset_t) - sizeof(struct pt_regs));

#define ELF_CORE_COPY_TASK_REGS(t, elfregs)			\
	((t)->thread.regs?					\
	 ({ ELF_CORE_COPY_REGS((elfregs), (t)->thread.regs); 1; }): 0)

extern int dump_task_fpu(struct task_struct *t, elf_fpregset_t *fpu);
#define ELF_CORE_COPY_FPREGS(t, fpu)	dump_task_fpu((t), (fpu))

/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  This could be done in userspace,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP	(cur_cpu_spec[0]->cpu_user_features)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.

   For the moment, we have only optimizations for the Intel generations,
   but that could change... */

#define ELF_PLATFORM	(NULL)

#define SET_PERSONALITY(ex, ibcs2) set_personality((ibcs2)?PER_SVR4:PER_LINUX)

/*
 * We need to put in some extra aux table entries to tell glibc what
 * the cache block size is, so it can use the dcbz instruction safely.
 */
#define AT_DCACHEBSIZE		19
#define AT_ICACHEBSIZE		20
#define AT_UCACHEBSIZE		21
/* A special ignored type value for PPC, for glibc compatibility.  */
#define AT_IGNOREPPC		22

extern int dcache_bsize;
extern int icache_bsize;
extern int ucache_bsize;

/*
 * The requirements here are:
 * - keep the final alignment of sp (sp & 0xf)
 * - make sure the 32-bit value at the first 16 byte aligned position of
 *   AUXV is greater than 16 for glibc compatibility.
 *   AT_IGNOREPPC is used for that.
 * - for compatibility with glibc ARCH_DLINFO must always be defined on PPC,
 *   even if DLINFO_ARCH_ITEMS goes to zero or is undefined.
 */
#define ARCH_DLINFO							\
do {									\
	/* Handle glibc compatibility. */				\
	NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);			\
	NEW_AUX_ENT(AT_IGNOREPPC, AT_IGNOREPPC);			\
	/* Cache size items */						\
	NEW_AUX_ENT(AT_DCACHEBSIZE, dcache_bsize);			\
	NEW_AUX_ENT(AT_ICACHEBSIZE, icache_bsize);			\
	NEW_AUX_ENT(AT_UCACHEBSIZE, ucache_bsize);			\
 } while (0)

#endif /* __KERNEL__ */
#endif
