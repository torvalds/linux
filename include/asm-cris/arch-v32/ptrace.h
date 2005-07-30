#ifndef _CRIS_ARCH_PTRACE_H
#define _CRIS_ARCH_PTRACE_H

/* Register numbers in the ptrace system call interface */

#define PT_ORIG_R10  0
#define PT_R0        1
#define PT_R1        2
#define PT_R2        3
#define PT_R3        4
#define PT_R4        5
#define PT_R5        6
#define PT_R6        7
#define PT_R7        8
#define PT_R8        9
#define PT_R9        10
#define PT_R10       11
#define PT_R11       12
#define PT_R12       13
#define PT_R13       14
#define PT_ACR       15
#define PT_SRS       16
#define PT_MOF       17
#define PT_SPC       18
#define PT_CCS       19
#define PT_SRP       20
#define PT_ERP       21    /* This is actually the debugged process' PC */
#define PT_EXS       22
#define PT_EDA       23
#define PT_USP       24    /* special case - USP is not in the pt_regs */
#define PT_PPC       25    /* special case - pseudo PC */
#define PT_BP        26    /* Base number for BP registers. */
#define PT_BP_CTRL   26    /* BP control register. */
#define PT_MAX       40

/* Condition code bit numbers. */
#define C_CCS_BITNR 0
#define V_CCS_BITNR 1
#define Z_CCS_BITNR 2
#define N_CCS_BITNR 3
#define X_CCS_BITNR 4
#define I_CCS_BITNR 5
#define U_CCS_BITNR 6
#define P_CCS_BITNR 7
#define R_CCS_BITNR 8
#define S_CCS_BITNR 9
#define M_CCS_BITNR 30
#define Q_CCS_BITNR 31
#define CCS_SHIFT   10 /* Shift count for each level in CCS */

/* pt_regs not only specifices the format in the user-struct during
 * ptrace but is also the frame format used in the kernel prologue/epilogues
 * themselves
 */

struct pt_regs {
	unsigned long orig_r10;
	/* pushed by movem r13, [sp] in SAVE_ALL. */
	unsigned long r0;
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long r10;
	unsigned long r11;
	unsigned long r12;
	unsigned long r13;
	unsigned long acr;
	unsigned long srs;
	unsigned long mof;
	unsigned long spc;
	unsigned long ccs;
	unsigned long srp;
	unsigned long erp; /* This is actually the debugged process' PC */
	/* For debugging purposes; saved only when needed. */
	unsigned long exs;
	unsigned long eda;
};

/* switch_stack is the extra stuff pushed onto the stack in _resume (entry.S)
 * when doing a context-switch. it is used (apart from in resume) when a new
 * thread is made and we need to make _resume (which is starting it for the
 * first time) realise what is going on.
 *
 * Actually, the use is very close to the thread struct (TSS) in that both the
 * switch_stack and the TSS are used to keep thread stuff when switching in
 * _resume.
 */

struct switch_stack {
	unsigned long r0;
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
	unsigned long r8;
	unsigned long r9;
	unsigned long return_ip; /* ip that _resume will return to */
};

#define user_mode(regs) (((regs)->ccs & (1 << (U_CCS_BITNR + CCS_SHIFT))) != 0)
#define instruction_pointer(regs) ((regs)->erp)
extern void show_regs(struct pt_regs *);
#define profile_pc(regs) instruction_pointer(regs)

#endif
