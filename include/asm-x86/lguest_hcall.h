/* Architecture specific portion of the lguest hypercalls */
#ifndef _X86_LGUEST_HCALL_H
#define _X86_LGUEST_HCALL_H

#define LHCALL_FLUSH_ASYNC	0
#define LHCALL_LGUEST_INIT	1
#define LHCALL_CRASH		2
#define LHCALL_LOAD_GDT		3
#define LHCALL_NEW_PGTABLE	4
#define LHCALL_FLUSH_TLB	5
#define LHCALL_LOAD_IDT_ENTRY	6
#define LHCALL_SET_STACK	7
#define LHCALL_TS		8
#define LHCALL_SET_CLOCKEVENT	9
#define LHCALL_HALT		10
#define LHCALL_SET_PTE		14
#define LHCALL_SET_PMD		15
#define LHCALL_LOAD_TLS		16
#define LHCALL_NOTIFY		17

/*G:031 First, how does our Guest contact the Host to ask for privileged
 * operations?  There are two ways: the direct way is to make a "hypercall",
 * to make requests of the Host Itself.
 *
 * Our hypercall mechanism uses the highest unused trap code (traps 32 and
 * above are used by real hardware interrupts).  Seventeen hypercalls are
 * available: the hypercall number is put in the %eax register, and the
 * arguments (when required) are placed in %edx, %ebx and %ecx.  If a return
 * value makes sense, it's returned in %eax.
 *
 * Grossly invalid calls result in Sudden Death at the hands of the vengeful
 * Host, rather than returning failure.  This reflects Winston Churchill's
 * definition of a gentleman: "someone who is only rude intentionally". */
#define LGUEST_TRAP_ENTRY 0x1F

#ifndef __ASSEMBLY__
#include <asm/hw_irq.h>

static inline unsigned long
hcall(unsigned long call,
      unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	/* "int" is the Intel instruction to trigger a trap. */
	asm volatile("int $" __stringify(LGUEST_TRAP_ENTRY)
		       /* The call is in %eax (aka "a"), and can be replaced */
		     : "=a"(call)
		       /* The other arguments are in %eax, %edx, %ebx & %ecx */
		     : "a"(call), "d"(arg1), "b"(arg2), "c"(arg3)
		       /* "memory" means this might write somewhere in memory.
			* This isn't true for all calls, but it's safe to tell
			* gcc that it might happen so it doesn't get clever. */
		     : "memory");
	return call;
}
/*:*/

void async_hcall(unsigned long call,
		 unsigned long arg1, unsigned long arg2, unsigned long arg3);

/* Can't use our min() macro here: needs to be a constant */
#define LGUEST_IRQS (NR_IRQS < 32 ? NR_IRQS: 32)

#define LHCALL_RING_SIZE 64
struct hcall_args
{
	/* These map directly onto eax, ebx, ecx, edx in struct lguest_regs */
	unsigned long arg0, arg2, arg3, arg1;
};

#endif /* !__ASSEMBLY__ */
#endif	/* _I386_LGUEST_HCALL_H */
