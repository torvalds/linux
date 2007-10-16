#ifndef _X86_64_KDEBUG_H
#define _X86_64_KDEBUG_H 1

#include <linux/compiler.h>

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

extern void printk_address(unsigned long address);
extern void die(const char *,struct pt_regs *,long);
extern void __die(const char *,struct pt_regs *,long);
extern void show_registers(struct pt_regs *regs);
extern void dump_pagetable(unsigned long);
extern unsigned long oops_begin(void);
extern void oops_end(unsigned long);

#endif
