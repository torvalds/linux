/* Things the lguest guest needs to know.  Note: like all lguest interfaces,
 * this is subject to wild and random change between versions. */
#ifndef _ASM_LGUEST_H
#define _ASM_LGUEST_H

#ifndef __ASSEMBLY__
#include <asm/irq.h>

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
#define LHCALL_GET_WALLCLOCK	11
#define LHCALL_BIND_DMA		12
#define LHCALL_SEND_DMA		13
#define LHCALL_SET_PTE		14
#define LHCALL_SET_PMD		15
#define LHCALL_LOAD_TLS		16

#define LG_CLOCK_MIN_DELTA	100UL
#define LG_CLOCK_MAX_DELTA	ULONG_MAX

#define LGUEST_TRAP_ENTRY 0x1F

static inline unsigned long
hcall(unsigned long call,
      unsigned long arg1, unsigned long arg2, unsigned long arg3)
{
	asm volatile("int $" __stringify(LGUEST_TRAP_ENTRY)
		     : "=a"(call)
		     : "a"(call), "d"(arg1), "b"(arg2), "c"(arg3)
		     : "memory");
	return call;
}

void async_hcall(unsigned long call,
		 unsigned long arg1, unsigned long arg2, unsigned long arg3);

/* Can't use our min() macro here: needs to be a constant */
#define LGUEST_IRQS (NR_IRQS < 32 ? NR_IRQS: 32)

#define LHCALL_RING_SIZE 64
struct hcall_ring
{
	u32 eax, edx, ebx, ecx;
};

/* All the good stuff happens here: guest registers it with LGUEST_INIT */
struct lguest_data
{
/* Fields which change during running: */
	/* 512 == enabled (same as eflags) */
	unsigned int irq_enabled;
	/* Interrupts blocked by guest. */
	DECLARE_BITMAP(blocked_interrupts, LGUEST_IRQS);

	/* Virtual address of page fault. */
	unsigned long cr2;

	/* Async hypercall ring.  0xFF == done, 0 == pending. */
	u8 hcall_status[LHCALL_RING_SIZE];
	struct hcall_ring hcalls[LHCALL_RING_SIZE];

/* Fields initialized by the hypervisor at boot: */
	/* Memory not to try to access */
	unsigned long reserve_mem;
	/* ID of this guest (used by network driver to set ethernet address) */
	u16 guestid;
	/* KHz for the TSC clock. */
	u32 tsc_khz;

/* Fields initialized by the guest at boot: */
	/* Instruction range to suppress interrupts even if enabled */
	unsigned long noirq_start, noirq_end;
};
extern struct lguest_data lguest_data;
#endif /* __ASSEMBLY__ */
#endif	/* _ASM_LGUEST_H */
