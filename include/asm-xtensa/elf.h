/*
 * include/asm-xtensa/elf.h
 *
 * ELF register definitions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_ELF_H
#define _XTENSA_ELF_H

#include <asm/ptrace.h>
#include <asm/coprocessor.h>
#include <xtensa/config/core.h>

/* Xtensa processor ELF architecture-magic number */

#define EM_XTENSA	94
#define EM_XTENSA_OLD	0xABC7

/* ELF register definitions. This is needed for core dump support.  */

/*
 * elf_gregset_t contains the application-level state in the following order:
 * Processor info: 	config_version, cpuxy
 * Processor state:	pc, ps, exccause, excvaddr, wb, ws,
 *			lbeg, lend, lcount, sar
 * GP regs:		ar0 - arXX
 */

typedef unsigned long elf_greg_t;

typedef struct {
	elf_greg_t xchal_config_id0;
	elf_greg_t xchal_config_id1;
	elf_greg_t cpux;
	elf_greg_t cpuy;
	elf_greg_t pc;
	elf_greg_t ps;
	elf_greg_t exccause;
	elf_greg_t excvaddr;
	elf_greg_t windowbase;
	elf_greg_t windowstart;
	elf_greg_t lbeg;
	elf_greg_t lend;
	elf_greg_t lcount;
	elf_greg_t sar;
	elf_greg_t syscall;
	elf_greg_t ar[XCHAL_NUM_AREGS];
} xtensa_gregset_t;

#define ELF_NGREG	(sizeof(xtensa_gregset_t) / sizeof(elf_greg_t))

typedef elf_greg_t elf_gregset_t[ELF_NGREG];

/*
 *  Compute the size of the coprocessor and extra state layout (register info)
 *  table (in bytes).
 *  This is actually the maximum size of the table, as opposed to the size,
 *  which is available from the _xtensa_reginfo_table_size global variable.
 *
 *  (See also arch/xtensa/kernel/coprocessor.S)
 *
 */

#ifndef XCHAL_EXTRA_SA_CONTENTS_LIBDB_NUM
# define XTENSA_CPE_LTABLE_SIZE		0
#else
# define XTENSA_CPE_SEGMENT(num)	(num ? (1+num) : 0)
# define XTENSA_CPE_LTABLE_ENTRIES	\
		( XTENSA_CPE_SEGMENT(XCHAL_EXTRA_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP0_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP1_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP2_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP3_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP4_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP5_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP6_SA_CONTENTS_LIBDB_NUM)	\
		+ XTENSA_CPE_SEGMENT(XCHAL_CP7_SA_CONTENTS_LIBDB_NUM)	\
		+ 1		/* final entry */			\
		)
# define XTENSA_CPE_LTABLE_SIZE		(XTENSA_CPE_LTABLE_ENTRIES * 8)
#endif


/*
 * Instantiations of the elf_fpregset_t type contain, in most
 * architectures, the floating point (FPU) register set.
 * For Xtensa, this type is extended to contain all custom state,
 * ie. coprocessor and "extra" (non-coprocessor) state (including,
 * for example, TIE-defined states and register files; as well
 * as other optional processor state).
 * This includes FPU state if a floating-point coprocessor happens
 * to have been configured within the Xtensa processor.
 *
 * TOTAL_FPREGS_SIZE is the required size (without rounding)
 * of elf_fpregset_t.  It provides space for the following:
 *
 *  a)	32-bit mask of active coprocessors for this task (similar
 *	to CPENABLE in single-threaded Xtensa processor systems)
 *
 *  b)	table describing the layout of custom states (ie. of
 *      individual registers, etc) within the save areas
 *
 *  c)  save areas for each coprocessor and for non-coprocessor
 *      ("extra") state
 *
 * Note that save areas may require up to 16-byte alignment when
 * accessed by save/restore sequences.  We do not need to ensure
 * such alignment in an elf_fpregset_t structure because custom
 * state is not directly loaded/stored into it; rather, save area
 * contents are copied to elf_fpregset_t from the active save areas
 * (see 'struct task_struct' definition in processor.h for that)
 * using memcpy().  But we do allow space for such alignment,
 * to allow optimizations of layout and copying.
 */

#define TOTAL_FPREGS_SIZE						\
  	(4 + XTENSA_CPE_LTABLE_SIZE + XTENSA_CP_EXTRA_SIZE)
#define ELF_NFPREG							\
	((TOTAL_FPREGS_SIZE + sizeof(elf_fpreg_t) - 1) / sizeof(elf_fpreg_t))

typedef unsigned int elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#define ELF_CORE_COPY_REGS(_eregs, _pregs) 				\
	xtensa_elf_core_copy_regs (&_eregs, _pregs);

extern void xtensa_elf_core_copy_regs (xtensa_gregset_t *, struct pt_regs *);

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */

#define elf_check_arch(x) ( ( (x)->e_machine == EM_XTENSA )  || \
			    ( (x)->e_machine == EM_XTENSA_OLD ) )

/*
 * These are used to set parameters in the core dumps.
 */

#ifdef __XTENSA_EL__
# define ELF_DATA	ELFDATA2LSB
#elif defined(__XTENSA_EB__)
# define ELF_DATA	ELFDATA2MSB
#else
# error processor byte order undefined!
#endif

#define ELF_CLASS	ELFCLASS32
#define ELF_ARCH	EM_XTENSA

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	PAGE_SIZE

/*
 * This is the location that an ET_DYN program is loaded if exec'ed.  Typical
 * use of this is to invoke "./ld.so someprog" to test out a new version of
 * the loader.  We need to make sure that it is out of the way of the program
 * that it will "exec", and that there is sufficient room for the brk.
 */

#define ELF_ET_DYN_BASE         (2 * TASK_SIZE / 3)

/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this CPU supports.  This could be done in user space,
 * but it's not easy, and we've already done it here.
 */

#define ELF_HWCAP	(0)

/*
 * This yields a string that ld.so will use to load implementation
 * specific libraries for optimization.  This is more specific in
 * intent than poking at uname or /proc/cpuinfo.
 * For the moment, we have only optimizations for the Intel generations,
 * but that could change...
 */

#define ELF_PLATFORM  (NULL)

/*
 * The Xtensa processor ABI says that when the program starts, a2
 * contains a pointer to a function which might be registered using
 * `atexit'.  This provides a mean for the dynamic linker to call
 * DT_FINI functions for shared libraries that have been loaded before
 * the code runs.
 *
 * A value of 0 tells we have no such handler.
 *
 * We might as well make sure everything else is cleared too (except
 * for the stack pointer in a1), just to make things more
 * deterministic.  Also, clearing a0 terminates debugger backtraces.
 */

#define ELF_PLAT_INIT(_r, load_addr) \
  do { _r->areg[0]=0; /*_r->areg[1]=0;*/ _r->areg[2]=0;  _r->areg[3]=0;  \
       _r->areg[4]=0;  _r->areg[5]=0;    _r->areg[6]=0;  _r->areg[7]=0;  \
       _r->areg[8]=0;  _r->areg[9]=0;    _r->areg[10]=0; _r->areg[11]=0; \
       _r->areg[12]=0; _r->areg[13]=0;   _r->areg[14]=0; _r->areg[15]=0; \
  } while (0)

#ifdef __KERNEL__

#define SET_PERSONALITY(ex, ibcs2) set_personality(PER_LINUX_32BIT)

extern void do_copy_regs (xtensa_gregset_t*, struct pt_regs*,
			  struct task_struct*);
extern void do_restore_regs (xtensa_gregset_t*, struct pt_regs*,
			     struct task_struct*);
extern void do_save_fpregs (elf_fpregset_t*, struct pt_regs*,
			    struct task_struct*);
extern int do_restore_fpregs (elf_fpregset_t*, struct pt_regs*,
			      struct task_struct*);

#endif	/* __KERNEL__ */
#endif	/* _XTENSA_ELF_H */
