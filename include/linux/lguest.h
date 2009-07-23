/* Things the lguest guest needs to know.  Note: like all lguest interfaces,
 * this is subject to wild and random change between versions. */
#ifndef _LINUX_LGUEST_H
#define _LINUX_LGUEST_H

#ifndef __ASSEMBLY__
#include <linux/time.h>
#include <asm/irq.h>
#include <asm/lguest_hcall.h>

#define LG_CLOCK_MIN_DELTA	100UL
#define LG_CLOCK_MAX_DELTA	ULONG_MAX

/*G:031 The second method of communicating with the Host is to via "struct
 * lguest_data".  Once the Guest's initialization hypercall tells the Host where
 * this is, the Guest and Host both publish information in it. :*/
struct lguest_data
{
	/* 512 == enabled (same as eflags in normal hardware).  The Guest
	 * changes interrupts so often that a hypercall is too slow. */
	unsigned int irq_enabled;
	/* Fine-grained interrupt disabling by the Guest */
	DECLARE_BITMAP(blocked_interrupts, LGUEST_IRQS);

	/* The Host writes the virtual address of the last page fault here,
	 * which saves the Guest a hypercall.  CR2 is the native register where
	 * this address would normally be found. */
	unsigned long cr2;

	/* Wallclock time set by the Host. */
	struct timespec time;

	/* Interrupt pending set by the Host.  The Guest should do a hypercall
	 * if it re-enables interrupts and sees this set (to X86_EFLAGS_IF). */
	int irq_pending;

	/* Async hypercall ring.  Instead of directly making hypercalls, we can
	 * place them in here for processing the next time the Host wants.
	 * This batching can be quite efficient. */

	/* 0xFF == done (set by Host), 0 == pending (set by Guest). */
	u8 hcall_status[LHCALL_RING_SIZE];
	/* The actual registers for the hypercalls. */
	struct hcall_args hcalls[LHCALL_RING_SIZE];

/* Fields initialized by the Host at boot: */
	/* Memory not to try to access */
	unsigned long reserve_mem;
	/* KHz for the TSC clock. */
	u32 tsc_khz;
	/* Page where the top-level pagetable is */
	unsigned long pgdir;

/* Fields initialized by the Guest at boot: */
	/* Instruction range to suppress interrupts even if enabled */
	unsigned long noirq_start, noirq_end;
	/* Address above which page tables are all identical. */
	unsigned long kernel_address;
	/* The vector to try to use for system calls (0x40 or 0x80). */
	unsigned int syscall_vec;
};
extern struct lguest_data lguest_data;
#endif /* __ASSEMBLY__ */
#endif	/* _LINUX_LGUEST_H */
