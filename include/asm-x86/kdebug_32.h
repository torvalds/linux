#ifndef _I386_KDEBUG_H
#define _I386_KDEBUG_H 1

/*
 * Aug-05 2004 Ported by Prasanna S Panchamukhi <prasanna@in.ibm.com>
 * from x86_64 architecture.
 */

struct pt_regs;

/* Grossly misnamed. */
enum die_val {
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_PANIC,
	DIE_NMI,
	DIE_DIE,
	DIE_NMIWATCHDOG,
	DIE_KERNELDEBUG,
	DIE_TRAP,
	DIE_GPF,
	DIE_CALL,
	DIE_NMI_IPI,
};

#endif
