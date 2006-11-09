#ifndef __ASMARM_ELF_H
#define __ASMARM_ELF_H

#ifndef __ASSEMBLY__
/*
 * ELF register definitions..
 */
#include <asm/ptrace.h>
#include <asm/user.h>

typedef unsigned long elf_greg_t;
typedef unsigned long elf_freg_t[3];

#define ELF_NGREG (sizeof (struct pt_regs) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_fp elf_fpregset_t;
#endif

#define EM_ARM	40
#define EF_ARM_APCS26 0x08
#define EF_ARM_SOFT_FLOAT 0x200
#define EF_ARM_EABI_MASK 0xFF000000

#define R_ARM_NONE	0
#define R_ARM_PC24	1
#define R_ARM_ABS32	2
#define R_ARM_CALL	28
#define R_ARM_JUMP24	29

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#ifdef __ARMEB__
#define ELF_DATA	ELFDATA2MSB
#else
#define ELF_DATA	ELFDATA2LSB
#endif
#define ELF_ARCH	EM_ARM

/*
 * HWCAP flags - for elf_hwcap (in kernel) and AT_HWCAP
 */
#define HWCAP_SWP	1
#define HWCAP_HALF	2
#define HWCAP_THUMB	4
#define HWCAP_26BIT	8	/* Play it safe */
#define HWCAP_FAST_MULT	16
#define HWCAP_FPA	32
#define HWCAP_VFP	64
#define HWCAP_EDSP	128
#define HWCAP_JAVA	256
#define HWCAP_IWMMXT	512

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP	(elf_hwcap)
extern unsigned int elf_hwcap;

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 *
 * For now we just provide a fairly general string that describes the
 * processor family.  This could be made more specific later if someone
 * implemented optimisations that require it.  26-bit CPUs give you
 * "v1l" for ARM2 (no SWP) and "v2l" for anything else (ARM1 isn't
 * supported).  32-bit CPUs give you "v3[lb]" for anything based on an
 * ARM6 or ARM7 core and "armv4[lb]" for anything based on a StrongARM-1
 * core.
 */
#define ELF_PLATFORM_SIZE 8
#define ELF_PLATFORM	(elf_platform)

extern char elf_platform[];
#endif

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ((x)->e_machine == EM_ARM && ELF_PROC_OK(x))

/*
 * 32-bit code is always OK.  Some cpus can do 26-bit, some can't.
 */
#define ELF_PROC_OK(x)	(ELF_THUMB_OK(x) && ELF_26BIT_OK(x))

#define ELF_THUMB_OK(x) \
	((elf_hwcap & HWCAP_THUMB && ((x)->e_entry & 1) == 1) || \
	 ((x)->e_entry & 3) == 0)

#define ELF_26BIT_OK(x) \
	((elf_hwcap & HWCAP_26BIT && (x)->e_flags & EF_ARM_APCS26) || \
	  ((x)->e_flags & EF_ARM_APCS26) == 0)

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE	(2 * TASK_SIZE / 3)

/* When the program starts, a1 contains a pointer to a function to be 
   registered with atexit, as per the SVR4 ABI.  A value of 0 means we 
   have no such handler.  */
#define ELF_PLAT_INIT(_r, load_addr)	(_r)->ARM_r0 = 0

#ifndef CONFIG_IWMMXT

/* Old NetWinder binaries were compiled in such a way that the iBCS
   heuristic always trips on them.  Until these binaries become uncommon
   enough not to care, don't trust the `ibcs' flag here.  In any case
   there is no other ELF system currently supported by iBCS.
   @@ Could print a warning message to encourage users to upgrade.  */
#define SET_PERSONALITY(ex,ibcs2) \
	set_personality(((ex).e_flags & EF_ARM_APCS26 ? PER_LINUX : PER_LINUX_32BIT))

#else

/*
 * All iWMMXt capable CPUs don't support 26-bit mode.  Yet they can run
 * legacy binaries which used to contain FPA11 floating point instructions
 * that have always been emulated by the kernel.  PFA11 and iWMMXt overlap
 * on coprocessor 1 space though.  We therefore must decide if given task
 * is allowed to use CP 0 and 1 for iWMMXt, or if they should be blocked
 * at all times for the prefetch exception handler to catch FPA11 opcodes
 * and emulate them.  The best indication to discriminate those two cases
 * is the SOFT_FLOAT flag in the ELF header.
 */

#define SET_PERSONALITY(ex,ibcs2) \
do { \
	set_personality(PER_LINUX_32BIT); \
	if (((ex).e_flags & EF_ARM_EABI_MASK) || \
	    ((ex).e_flags & EF_ARM_SOFT_FLOAT)) \
		set_thread_flag(TIF_USING_IWMMXT); \
	else \
		clear_thread_flag(TIF_USING_IWMMXT); \
} while (0)

#endif

#endif

#endif
