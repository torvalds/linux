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

/* Xtensa processor ELF architecture-magic number */

#define EM_XTENSA	94
#define EM_XTENSA_OLD	0xABC7

/* Xtensa relocations defined by the ABIs */

#define R_XTENSA_NONE           0
#define R_XTENSA_32             1
#define R_XTENSA_RTLD           2
#define R_XTENSA_GLOB_DAT       3
#define R_XTENSA_JMP_SLOT       4
#define R_XTENSA_RELATIVE       5
#define R_XTENSA_PLT            6
#define R_XTENSA_OP0            8
#define R_XTENSA_OP1            9
#define R_XTENSA_OP2            10
#define R_XTENSA_ASM_EXPAND	11
#define R_XTENSA_ASM_SIMPLIFY	12
#define R_XTENSA_GNU_VTINHERIT	15
#define R_XTENSA_GNU_VTENTRY	16
#define R_XTENSA_DIFF8		17
#define R_XTENSA_DIFF16		18
#define R_XTENSA_DIFF32		19
#define R_XTENSA_SLOT0_OP	20
#define R_XTENSA_SLOT1_OP	21
#define R_XTENSA_SLOT2_OP	22
#define R_XTENSA_SLOT3_OP	23
#define R_XTENSA_SLOT4_OP	24
#define R_XTENSA_SLOT5_OP	25
#define R_XTENSA_SLOT6_OP	26
#define R_XTENSA_SLOT7_OP	27
#define R_XTENSA_SLOT8_OP	28
#define R_XTENSA_SLOT9_OP	29
#define R_XTENSA_SLOT10_OP	30
#define R_XTENSA_SLOT11_OP	31
#define R_XTENSA_SLOT12_OP	32
#define R_XTENSA_SLOT13_OP	33
#define R_XTENSA_SLOT14_OP	34
#define R_XTENSA_SLOT0_ALT	35
#define R_XTENSA_SLOT1_ALT	36
#define R_XTENSA_SLOT2_ALT	37
#define R_XTENSA_SLOT3_ALT	38
#define R_XTENSA_SLOT4_ALT	39
#define R_XTENSA_SLOT5_ALT	40
#define R_XTENSA_SLOT6_ALT	41
#define R_XTENSA_SLOT7_ALT	42
#define R_XTENSA_SLOT8_ALT	43
#define R_XTENSA_SLOT9_ALT	44
#define R_XTENSA_SLOT10_ALT	45
#define R_XTENSA_SLOT11_ALT	46
#define R_XTENSA_SLOT12_ALT	47
#define R_XTENSA_SLOT13_ALT	48
#define R_XTENSA_SLOT14_ALT	49

/* ELF register definitions. This is needed for core dump support.  */

typedef unsigned long elf_greg_t;

typedef struct {
	elf_greg_t pc;
	elf_greg_t ps;
	elf_greg_t lbeg;
	elf_greg_t lend;
	elf_greg_t lcount;
	elf_greg_t sar;
	elf_greg_t windowstart;
	elf_greg_t windowbase;
	elf_greg_t reserved[8+48];
	elf_greg_t a[64];
} xtensa_gregset_t;

#define ELF_NGREG	(sizeof(xtensa_gregset_t) / sizeof(elf_greg_t))

typedef elf_greg_t elf_gregset_t[ELF_NGREG];

#define ELF_NFPREG	18

typedef unsigned int elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#define ELF_CORE_COPY_REGS(_eregs, _pregs) 				\
	xtensa_elf_core_copy_regs ((xtensa_gregset_t*)&(_eregs), _pregs);

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

typedef struct {
	xtregs_opt_t	opt;
	xtregs_user_t	user;
#if XTENSA_HAVE_COPROCESSORS
	xtregs_cp0_t	cp0;
	xtregs_cp1_t	cp1;
	xtregs_cp2_t	cp2;
	xtregs_cp3_t	cp3;
	xtregs_cp4_t	cp4;
	xtregs_cp5_t	cp5;
	xtregs_cp6_t	cp6;
	xtregs_cp7_t	cp7;
#endif
} elf_xtregs_t;

#define SET_PERSONALITY(ex, ibcs2) set_personality(PER_LINUX_32BIT)

struct task_struct;

extern void do_copy_regs (xtensa_gregset_t*, struct pt_regs*,
			  struct task_struct*);
extern void do_restore_regs (xtensa_gregset_t*, struct pt_regs*,
			     struct task_struct*);
extern void do_save_fpregs (elf_fpregset_t*, struct pt_regs*,
			    struct task_struct*);
extern int do_restore_fpregs (elf_fpregset_t*, struct pt_regs*,
			      struct task_struct*);

#endif	/* _XTENSA_ELF_H */
