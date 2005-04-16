#ifndef _CRIS_ARCH_PTRACE_H
#define _CRIS_ARCH_PTRACE_H

/* Frame types */

#define CRIS_FRAME_NORMAL   0 /* normal frame without SBFS stacking */
#define CRIS_FRAME_BUSFAULT 1 /* frame stacked using SBFS, need RBF return
				 path */

/* Register numbers in the ptrace system call interface */

#define PT_FRAMETYPE 0
#define PT_ORIG_R10  1
#define PT_R13       2
#define PT_R12       3
#define PT_R11       4
#define PT_R10       5
#define PT_R9        6
#define PT_R8        7
#define PT_R7        8
#define PT_R6        9
#define PT_R5        10
#define PT_R4        11
#define PT_R3        12
#define PT_R2        13
#define PT_R1        14
#define PT_R0        15
#define PT_MOF       16
#define PT_DCCR      17
#define PT_SRP       18
#define PT_IRP       19    /* This is actually the debugged process' PC */
#define PT_CSRINSTR  20    /* CPU Status record remnants -
			      valid if frametype == busfault */
#define PT_CSRADDR   21
#define PT_CSRDATA   22
#define PT_USP       23    /* special case - USP is not in the pt_regs */
#define PT_MAX       23

/* Condition code bit numbers.  The same numbers apply to CCR of course,
   but we use DCCR everywhere else, so let's try and be consistent.  */
#define C_DCCR_BITNR 0
#define V_DCCR_BITNR 1
#define Z_DCCR_BITNR 2
#define N_DCCR_BITNR 3
#define X_DCCR_BITNR 4
#define I_DCCR_BITNR 5
#define B_DCCR_BITNR 6
#define M_DCCR_BITNR 7
#define U_DCCR_BITNR 8
#define P_DCCR_BITNR 9
#define F_DCCR_BITNR 10

/* pt_regs not only specifices the format in the user-struct during
 * ptrace but is also the frame format used in the kernel prologue/epilogues 
 * themselves
 */

struct pt_regs {
	unsigned long frametype;  /* type of stackframe */
	unsigned long orig_r10;
	/* pushed by movem r13, [sp] in SAVE_ALL, movem pushes backwards */
	unsigned long r13;
	unsigned long r12;
	unsigned long r11;
	unsigned long r10;
	unsigned long r9;
	unsigned long r8;
	unsigned long r7;
	unsigned long r6;
	unsigned long r5;
	unsigned long r4;
	unsigned long r3;
	unsigned long r2;
	unsigned long r1;
	unsigned long r0;
	unsigned long mof;
	unsigned long dccr;
	unsigned long srp;
	unsigned long irp; /* This is actually the debugged process' PC */
	unsigned long csrinstr;
	unsigned long csraddr;
	unsigned long csrdata;
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
	unsigned long r9;
	unsigned long r8;
	unsigned long r7;
	unsigned long r6;
	unsigned long r5;
	unsigned long r4;
	unsigned long r3;
	unsigned long r2;
	unsigned long r1;
	unsigned long r0;
	unsigned long return_ip; /* ip that _resume will return to */
};

/* bit 8 is user-mode flag */
#define user_mode(regs) (((regs)->dccr & 0x100) != 0)
#define instruction_pointer(regs) ((regs)->irp)
#define profile_pc(regs) instruction_pointer(regs)
extern void show_regs(struct pt_regs *);

#endif
