#ifndef __ASM_APIC_H
#define __ASM_APIC_H

#include <linux/pm.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <asm/system.h>

#define Dprintk(x...)

/*
 * Debugging macros
 */
#define APIC_QUIET   0
#define APIC_VERBOSE 1
#define APIC_DEBUG   2

extern int apic_verbosity;
extern int apic_runs_main_timer;
extern int ioapic_force;
extern int apic_mapped;

/*
 * Define the default level of output to be very little
 * This can be turned up by using apic=verbose for more
 * information and apic=debug for _lots_ of information.
 * apic_verbosity is defined in apic.c
 */
#define apic_printk(v, s, a...) do {       \
		if ((v) <= apic_verbosity) \
			printk(s, ##a);    \
	} while (0)

struct pt_regs;

/*
 * Basic functions accessing APICs.
 */

static __inline void apic_write(unsigned long reg, unsigned int v)
{
	*((volatile unsigned int *)(APIC_BASE+reg)) = v;
}

static __inline unsigned int apic_read(unsigned long reg)
{
	return *((volatile unsigned int *)(APIC_BASE+reg));
}

static __inline__ void apic_wait_icr_idle(void)
{
	while (apic_read( APIC_ICR ) & APIC_ICR_BUSY)
		cpu_relax();
}

static inline void ack_APIC_irq(void)
{
	/*
	 * ack_APIC_irq() actually gets compiled as a single instruction:
	 * - a single rmw on Pentium/82489DX
	 * - a single write on P6+ cores (CONFIG_X86_GOOD_APIC)
	 * ... yummie.
	 */

	/* Docs say use 0 for future compatibility */
	apic_write(APIC_EOI, 0);
}

extern int get_maxlvt (void);
extern void clear_local_APIC (void);
extern void connect_bsp_APIC (void);
extern void disconnect_bsp_APIC (int virt_wire_setup);
extern void disable_local_APIC (void);
extern int verify_local_APIC (void);
extern void cache_APIC_registers (void);
extern void sync_Arb_IDs (void);
extern void init_bsp_APIC (void);
extern void setup_local_APIC (void);
extern void init_apic_mappings (void);
extern void smp_local_timer_interrupt (void);
extern void setup_boot_APIC_clock (void);
extern void setup_secondary_APIC_clock (void);
extern int APIC_init_uniprocessor (void);
extern void disable_APIC_timer(void);
extern void enable_APIC_timer(void);
extern void clustered_apic_check(void);

extern void setup_APIC_extened_lvt(unsigned char lvt_off, unsigned char vector,
				   unsigned char msg_type, unsigned char mask);

#define K8_APIC_EXT_LVT_BASE    0x500
#define K8_APIC_EXT_INT_MSG_FIX 0x0
#define K8_APIC_EXT_INT_MSG_SMI 0x2
#define K8_APIC_EXT_INT_MSG_NMI 0x4
#define K8_APIC_EXT_INT_MSG_EXT 0x7
#define K8_APIC_EXT_LVT_ENTRY_THRESHOLD    0

void smp_send_timer_broadcast_ipi(void);
void switch_APIC_timer_to_ipi(void *cpumask);
void switch_ipi_to_APIC_timer(void *cpumask);

#define ARCH_APICTIMER_STOPS_ON_C3	1

extern unsigned boot_cpu_id;
extern int local_apic_timer_c2_ok;

#endif /* __ASM_APIC_H */
