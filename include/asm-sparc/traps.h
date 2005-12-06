/* $Id: traps.h,v 1.9 1998/03/09 14:04:53 jj Exp $
 * traps.h:  Format of entries for the Sparc trap table.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_TRAPS_H
#define _SPARC_TRAPS_H

#define NUM_SPARC_TRAPS  255

#ifndef __ASSEMBLY__

/* This is for V8 compliant Sparc CPUS */
struct tt_entry {
	unsigned long inst_one;
	unsigned long inst_two;
	unsigned long inst_three;
	unsigned long inst_four;
};

/* We set this to _start in system setup. */
extern struct tt_entry *sparc_ttable;

static inline unsigned long get_tbr(void)
{
	unsigned long tbr;

	__asm__ __volatile__("rd %%tbr, %0\n\t" : "=r" (tbr));
	return tbr;
}

#endif /* !(__ASSEMBLY__) */

/* For patching the trap table at boot time, we need to know how to
 * form various common Sparc instructions.  Thus these macros...
 */

#define SPARC_MOV_CONST_L3(const) (0xa6102000 | (const&0xfff))

/* The following assumes that the branch lies before the place we
 * are branching to.  This is the case for a trap vector...
 * You have been warned.
 */
#define SPARC_BRANCH(dest_addr, inst_addr) \
          (0x10800000 | (((dest_addr-inst_addr)>>2)&0x3fffff))

#define SPARC_RD_PSR_L0  (0xa1480000)
#define SPARC_RD_WIM_L3  (0xa7500000)
#define SPARC_NOP (0x01000000)

/* Various interesting trap levels. */
/* First, hardware traps. */
#define SP_TRAP_TFLT    0x1          /* Text fault */
#define SP_TRAP_II      0x2          /* Illegal Instruction */
#define SP_TRAP_PI      0x3          /* Privileged Instruction */
#define SP_TRAP_FPD     0x4          /* Floating Point Disabled */
#define SP_TRAP_WOVF    0x5          /* Window Overflow */
#define SP_TRAP_WUNF    0x6          /* Window Underflow */
#define SP_TRAP_MNA     0x7          /* Memory Address Unaligned */
#define SP_TRAP_FPE     0x8          /* Floating Point Exception */
#define SP_TRAP_DFLT    0x9          /* Data Fault */
#define SP_TRAP_TOF     0xa          /* Tag Overflow */
#define SP_TRAP_WDOG    0xb          /* Watchpoint Detected */
#define SP_TRAP_IRQ1    0x11         /* IRQ level 1 */
#define SP_TRAP_IRQ2    0x12         /* IRQ level 2 */
#define SP_TRAP_IRQ3    0x13         /* IRQ level 3 */
#define SP_TRAP_IRQ4    0x14         /* IRQ level 4 */
#define SP_TRAP_IRQ5    0x15         /* IRQ level 5 */
#define SP_TRAP_IRQ6    0x16         /* IRQ level 6 */
#define SP_TRAP_IRQ7    0x17         /* IRQ level 7 */
#define SP_TRAP_IRQ8    0x18         /* IRQ level 8 */
#define SP_TRAP_IRQ9    0x19         /* IRQ level 9 */
#define SP_TRAP_IRQ10   0x1a         /* IRQ level 10 */
#define SP_TRAP_IRQ11   0x1b         /* IRQ level 11 */
#define SP_TRAP_IRQ12   0x1c         /* IRQ level 12 */
#define SP_TRAP_IRQ13   0x1d         /* IRQ level 13 */
#define SP_TRAP_IRQ14   0x1e         /* IRQ level 14 */
#define SP_TRAP_IRQ15   0x1f         /* IRQ level 15 Non-maskable */
#define SP_TRAP_RACC    0x20         /* Register Access Error ??? */
#define SP_TRAP_IACC    0x21         /* Instruction Access Error */
#define SP_TRAP_CPDIS   0x24         /* Co-Processor Disabled */
#define SP_TRAP_BADFL   0x25         /* Unimplemented Flush Instruction */
#define SP_TRAP_CPEXP   0x28         /* Co-Processor Exception */
#define SP_TRAP_DACC    0x29         /* Data Access Error */
#define SP_TRAP_DIVZ    0x2a         /* Divide By Zero */
#define SP_TRAP_DSTORE  0x2b         /* Data Store Error ??? */
#define SP_TRAP_DMM     0x2c         /* Data Access MMU Miss ??? */
#define SP_TRAP_IMM     0x3c         /* Instruction Access MMU Miss ??? */

/* Now the Software Traps... */
#define SP_TRAP_SUNOS   0x80         /* SunOS System Call */
#define SP_TRAP_SBPT    0x81         /* Software Breakpoint */
#define SP_TRAP_SDIVZ   0x82         /* Software Divide-by-Zero trap */
#define SP_TRAP_FWIN    0x83         /* Flush Windows */
#define SP_TRAP_CWIN    0x84         /* Clean Windows */
#define SP_TRAP_RCHK    0x85         /* Range Check */
#define SP_TRAP_FUNA    0x86         /* Fix Unaligned Access */
#define SP_TRAP_IOWFL   0x87         /* Integer Overflow */
#define SP_TRAP_SOLARIS 0x88         /* Solaris System Call */
#define SP_TRAP_NETBSD  0x89         /* NetBSD System Call */
#define SP_TRAP_LINUX   0x90         /* Linux System Call */

/* Names used for compatibility with SunOS */
#define ST_SYSCALL              0x00
#define ST_BREAKPOINT           0x01
#define ST_DIV0                 0x02
#define ST_FLUSH_WINDOWS        0x03
#define ST_CLEAN_WINDOWS        0x04
#define ST_RANGE_CHECK          0x05
#define ST_FIX_ALIGN            0x06
#define ST_INT_OVERFLOW         0x07

/* Special traps... */
#define SP_TRAP_KBPT1   0xfe         /* KADB/PROM Breakpoint one */
#define SP_TRAP_KBPT2   0xff         /* KADB/PROM Breakpoint two */

/* Handy Macros */
/* Is this a trap we never expect to get? */
#define BAD_TRAP_P(level) \
        ((level > SP_TRAP_WDOG && level < SP_TRAP_IRQ1) || \
	 (level > SP_TRAP_IACC && level < SP_TRAP_CPDIS) || \
	 (level > SP_TRAP_BADFL && level < SP_TRAP_CPEXP) || \
	 (level > SP_TRAP_DMM && level < SP_TRAP_IMM) || \
	 (level > SP_TRAP_IMM && level < SP_TRAP_SUNOS) || \
	 (level > SP_TRAP_LINUX && level < SP_TRAP_KBPT1))

/* Is this a Hardware trap? */
#define HW_TRAP_P(level) ((level > 0) && (level < SP_TRAP_SUNOS))

/* Is this a Software trap? */
#define SW_TRAP_P(level) ((level >= SP_TRAP_SUNOS) && (level <= SP_TRAP_KBPT2))

/* Is this a system call for some OS we know about? */
#define SCALL_TRAP_P(level) ((level == SP_TRAP_SUNOS) || \
			     (level == SP_TRAP_SOLARIS) || \
			     (level == SP_TRAP_NETBSD) || \
			     (level == SP_TRAP_LINUX))

#endif /* !(_SPARC_TRAPS_H) */
