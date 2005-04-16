#ifndef __ASM_ARM_PTRACE_H
#define __ASM_ARM_PTRACE_H

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15
#define PTRACE_OLDSETOPTIONS    21

/* options set using PTRACE_SETOPTIONS */
#define PTRACE_O_TRACESYSGOOD	0x00000001

#define MODE_USR26      0x00000000
#define MODE_FIQ26      0x00000001
#define MODE_IRQ26      0x00000002
#define MODE_SVC26      0x00000003
#define MODE_MASK       0x00000003

#define PSR_F_BIT       0x04000000
#define PSR_I_BIT       0x08000000
#define PSR_V_BIT       0x10000000
#define PSR_C_BIT       0x20000000
#define PSR_Z_BIT       0x40000000
#define PSR_N_BIT       0x80000000

#define PCMASK          0xfc000003


#ifndef __ASSEMBLY__

#define pc_pointer(v) ((v) & ~PCMASK)   /* convert v to pc type address */
#define instruction_pointer(regs) (pc_pointer((regs)->ARM_pc)) /* get pc */
#define profile_pc(regs) instruction_pointer(regs)

/* this struct defines the way the registers are stored on the
   stack during a system call. */

struct pt_regs {
	long uregs[17];
};

#define ARM_pc		uregs[15]
#define ARM_lr		uregs[14]
#define ARM_sp		uregs[13]
#define ARM_ip		uregs[12]
#define ARM_fp		uregs[11]
#define ARM_r10		uregs[10]
#define ARM_r9		uregs[9]
#define ARM_r8		uregs[8]
#define ARM_r7		uregs[7]
#define ARM_r6		uregs[6]
#define ARM_r5		uregs[5]
#define ARM_r4		uregs[4]
#define ARM_r3		uregs[3]
#define ARM_r2		uregs[2]
#define ARM_r1		uregs[1]
#define ARM_r0		uregs[0]
#define ARM_ORIG_r0	uregs[16]

#ifdef __KERNEL__

#define processor_mode(regs) \
	((regs)->ARM_pc & MODE_MASK)

#define user_mode(regs) \
	(processor_mode(regs) == MODE_USR26)

#define interrupts_enabled(regs) \
	(!((regs)->ARM_pc & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->ARM_pc & PSR_F_BIT))

#define condition_codes(regs) \
	((regs)->ARM_pc & (PSR_V_BIT|PSR_C_BIT|PSR_Z_BIT|PSR_N_BIT))

/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	if (user_mode(regs) &&
	    (regs->ARM_pc & (PSR_F_BIT | PSR_I_BIT)) == 0)
		return 1;

	/*
	 * force it to be something sensible
	 */
	regs->ARM_pc &= ~(MODE_MASK | PSR_F_BIT | PSR_I_BIT);

	return 0;
}

extern void show_regs(struct pt_regs *);

#define predicate(x)    (x & 0xf0000000)
#define PREDICATE_ALWAYS        0xe0000000

#endif	/* __KERNEL__ */

#endif	/* __ASSEMBLY__ */

#endif

