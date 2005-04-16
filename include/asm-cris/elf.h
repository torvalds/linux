#ifndef __ASMCRIS_ELF_H
#define __ASMCRIS_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/arch/elf.h>
#include <asm/user.h>

typedef unsigned long elf_greg_t;

/* Note that NGREG is defined to ELF_NGREG in include/linux/elfcore.h, and is
   thus exposed to user-space. */
#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/* A placeholder; CRIS does not have any fp regs.  */
typedef unsigned long elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ( (x)->e_machine == EM_CRIS )

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB;
#define ELF_ARCH	EM_CRIS

#define USE_ELF_CORE_DUMP

#define ELF_EXEC_PAGESIZE	8192

/* This is the location that an ET_DYN program is loaded if exec'ed.  Typical
   use of this is to invoke "./ld.so someprog" to test out a new version of
   the loader.  We need to make sure that it is out of the way of the program
   that it will "exec", and that there is sufficient room for the brk.  */

#define ELF_ET_DYN_BASE         (2 * TASK_SIZE / 3)

/* This yields a mask that user programs can use to figure out what
   instruction set this CPU supports.  This could be done in user space,
   but it's not easy, and we've already done it here.  */

#define ELF_HWCAP       (0)

/* This yields a string that ld.so will use to load implementation
   specific libraries for optimization.  This is more specific in
   intent than poking at uname or /proc/cpuinfo.
*/

#define ELF_PLATFORM  (NULL)

#ifdef __KERNEL__
#define SET_PERSONALITY(ex, ibcs2) set_personality((ibcs2)?PER_SVR4:PER_LINUX)
#endif

#endif
