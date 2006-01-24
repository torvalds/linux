/* $Id: elf.h,v 1.32 2002/02/09 19:49:31 davem Exp $ */
#ifndef __ASM_SPARC64_ELF_H
#define __ASM_SPARC64_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#ifdef __KERNEL__
#include <asm/processor.h>
#include <asm/uaccess.h>
#endif

/*
 * Sparc section types
 */
#define STT_REGISTER		13

/*
 * Sparc ELF relocation types
 */
#define	R_SPARC_NONE		0
#define	R_SPARC_8		1
#define	R_SPARC_16		2
#define	R_SPARC_32		3
#define	R_SPARC_DISP8		4
#define	R_SPARC_DISP16		5
#define	R_SPARC_DISP32		6
#define	R_SPARC_WDISP30		7
#define	R_SPARC_WDISP22		8
#define	R_SPARC_HI22		9
#define	R_SPARC_22		10
#define	R_SPARC_13		11
#define	R_SPARC_LO10		12
#define	R_SPARC_GOT10		13
#define	R_SPARC_GOT13		14
#define	R_SPARC_GOT22		15
#define	R_SPARC_PC10		16
#define	R_SPARC_PC22		17
#define	R_SPARC_WPLT30		18
#define	R_SPARC_COPY		19
#define	R_SPARC_GLOB_DAT	20
#define	R_SPARC_JMP_SLOT	21
#define	R_SPARC_RELATIVE	22
#define	R_SPARC_UA32		23
#define R_SPARC_PLT32		24
#define R_SPARC_HIPLT22		25
#define R_SPARC_LOPLT10		26
#define R_SPARC_PCPLT32		27
#define R_SPARC_PCPLT22		28
#define R_SPARC_PCPLT10		29
#define R_SPARC_10		30
#define R_SPARC_11		31
#define R_SPARC_64		32
#define R_SPARC_OLO10		33
#define R_SPARC_WDISP16		40
#define R_SPARC_WDISP19		41
#define R_SPARC_7		43
#define R_SPARC_5		44
#define R_SPARC_6		45

/* Bits present in AT_HWCAP, primarily for Sparc32.  */

#define HWCAP_SPARC_FLUSH       1    /* CPU supports flush instruction. */
#define HWCAP_SPARC_STBAR       2
#define HWCAP_SPARC_SWAP        4
#define HWCAP_SPARC_MULDIV      8
#define HWCAP_SPARC_V9		16
#define HWCAP_SPARC_ULTRA3	32

/*
 * These are used to set parameters in the core dumps.
 */
#ifndef ELF_ARCH
#define ELF_ARCH		EM_SPARCV9
#define ELF_CLASS		ELFCLASS64
#define ELF_DATA		ELFDATA2MSB

typedef unsigned long elf_greg_t;

#define ELF_NGREG 36
typedef elf_greg_t elf_gregset_t[ELF_NGREG];
/* Format of 64-bit elf_gregset_t is:
 * 	G0 --> G7
 * 	O0 --> O7
 * 	L0 --> L7
 * 	I0 --> I7
 *	TSTATE
 *	TPC
 *	TNPC
 *	Y
 */
#define ELF_CORE_COPY_REGS(__elf_regs, __pt_regs)	\
do {	unsigned long *dest = &(__elf_regs[0]);		\
	struct pt_regs *src = (__pt_regs);		\
	unsigned long __user *sp;			\
	int i;						\
	for(i = 0; i < 16; i++)				\
		dest[i] = src->u_regs[i];		\
	/* Don't try this at home kids... */		\
	sp = (unsigned long __user *)			\
	 ((src->u_regs[14] + STACK_BIAS)		\
	  & 0xfffffffffffffff8UL);			\
	for(i = 0; i < 16; i++)				\
		__get_user(dest[i+16], &sp[i]);		\
	dest[32] = src->tstate;				\
	dest[33] = src->tpc;				\
	dest[34] = src->tnpc;				\
	dest[35] = src->y;				\
} while (0);

typedef struct {
	unsigned long	pr_regs[32];
	unsigned long	pr_fsr;
	unsigned long	pr_gsr;
	unsigned long	pr_fprs;
} elf_fpregset_t;
#endif

#define ELF_CORE_COPY_TASK_REGS(__tsk, __elf_regs)	\
	({ ELF_CORE_COPY_REGS((*(__elf_regs)), task_pt_regs(__tsk)); 1; })

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#ifndef elf_check_arch
#define elf_check_arch(x) ((x)->e_machine == ELF_ARCH)	/* Might be EM_SPARCV9 or EM_SPARC */
#endif

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#ifndef ELF_ET_DYN_BASE
#define ELF_ET_DYN_BASE         0x0000010000000000UL
#endif


/* This yields a mask that user programs can use to figure out what
   instruction set this cpu supports.  */

/* On Ultra, we support all of the v8 capabilities. */
#define ELF_HWCAP	((HWCAP_SPARC_FLUSH | HWCAP_SPARC_STBAR | \
			  HWCAP_SPARC_SWAP | HWCAP_SPARC_MULDIV | \
			  HWCAP_SPARC_V9) | \
			 ((tlb_type == cheetah || tlb_type == cheetah_plus) ? \
			  HWCAP_SPARC_ULTRA3 : 0))

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.  */

#define ELF_PLATFORM	(NULL)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2)			\
do {	unsigned long new_flags = current_thread_info()->flags; \
	new_flags &= _TIF_32BIT;			\
	if ((ex).e_ident[EI_CLASS] == ELFCLASS32)	\
		new_flags |= _TIF_32BIT;		\
	else						\
		new_flags &= ~_TIF_32BIT;		\
	if ((current_thread_info()->flags & _TIF_32BIT) \
	    != new_flags)				\
		set_thread_flag(TIF_ABI_PENDING);	\
	else						\
		clear_thread_flag(TIF_ABI_PENDING);	\
	/* flush_thread will update pgd cache */	\
	if (ibcs2)					\
		set_personality(PER_SVR4);		\
	else if (current->personality != PER_LINUX32)	\
		set_personality(PER_LINUX);		\
} while (0)
#endif

#endif /* !(__ASM_SPARC64_ELF_H) */
