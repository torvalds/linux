#ifndef _X86_64_KDEBUG_H
#define _X86_64_KDEBUG_H 1

#include <linux/notifier.h>

struct pt_regs;

struct die_args { 
	struct pt_regs *regs;
	const char *str;
	long err; 
	int trapnr;
	int signr;
}; 

/* Note - you should never unregister because that can race with NMIs.
   If you really want to do it first unregister - then synchronize_kernel - then free. 
  */
int register_die_notifier(struct notifier_block *nb);
extern struct notifier_block *die_chain;
/* Grossly misnamed. */
enum die_val { 
	DIE_OOPS = 1,
	DIE_INT3,
	DIE_DEBUG,
	DIE_DEBUGSTEP,
	DIE_PANIC,
	DIE_NMI,
	DIE_DIE,
	DIE_NMIWATCHDOG,
	DIE_KERNELDEBUG,
	DIE_TRAP,
	DIE_GPF,
	DIE_CALL,
	DIE_NMI_IPI,
	DIE_PAGE_FAULT,
}; 
	
static inline int notify_die(enum die_val val,char *str,struct pt_regs *regs,long err,int trap, int sig)
{ 
	struct die_args args = { .regs=regs, .str=str, .err=err, .trapnr=trap,.signr=sig }; 
	return notifier_call_chain(&die_chain, val, &args); 
} 

extern int printk_address(unsigned long address);
extern void die(const char *,struct pt_regs *,long);
extern void __die(const char *,struct pt_regs *,long);
extern void show_registers(struct pt_regs *regs);
extern void dump_pagetable(unsigned long);
extern void oops_begin(void);
extern void oops_end(void);

#endif
