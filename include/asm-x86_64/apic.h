#ifndef __ASM_APIC_H
#define __ASM_APIC_H

#include <linux/config.h>
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

#ifdef CONFIG_X86_LOCAL_APIC

struct pt_regs;

/*
 * Basic functions accessing APICs.
 */

static __inline void apic_write(unsigned long reg, unsigned int v)
{
	*((volatile unsigned int *)(APIC_BASE+reg)) = v;
}

static __inline void apic_write_atomic(unsigned long reg, unsigned int v)
{
	xchg((volatile unsigned int *)(APIC_BASE+reg), v);
}

static __inline unsigned int apic_read(unsigned long reg)
{
	return *((volatile unsigned int *)(APIC_BASE+reg));
}

static __inline__ void apic_wait_icr_idle(void)
{
	while ( apic_read( APIC_ICR ) & APIC_ICR_BUSY );
}

#define FORCE_READ_AROUND_WRITE 0
#define apic_read_around(x)
#define apic_write_around(x,y) apic_write((x),(y))

static inline void ack_APIC_irq(void)
{
	/*
	 * ack_APIC_irq() actually gets compiled as a single instruction:
	 * - a single rmw on Pentium/82489DX
	 * - a single write on P6+ cores (CONFIG_X86_GOOD_APIC)
	 * ... yummie.
	 */

	/* Docs say use 0 for future compatibility */
	apic_write_around(APIC_EOI, 0);
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
extern void smp_local_timer_interrupt (struct pt_regs * regs);
extern void setup_boot_APIC_clock (void);
extern void setup_secondary_APIC_clock (void);
extern void setup_apic_nmi_watchdog (void);
extern int reserve_lapic_nmi(void);
extern void release_lapic_nmi(void);
extern void disable_timer_nmi_watchdog(void);
extern void enable_timer_nmi_watchdog(void);
extern void nmi_watchdog_tick (struct pt_regs * regs, unsigned reason);
extern int APIC_init_uniprocessor (void);
extern void disable_APIC_timer(void);
extern void enable_APIC_timer(void);
extern void clustered_apic_check(void);

extern void nmi_watchdog_default(void);
extern int setup_nmi_watchdog(char *);

extern unsigned int nmi_watchdog;
#define NMI_DEFAULT	-1
#define NMI_NONE	0
#define NMI_IO_APIC	1
#define NMI_LOCAL_APIC	2
#define NMI_INVALID	3

extern int disable_timer_pin_1;

extern void setup_threshold_lvt(unsigned long lvt_off);

#endif /* CONFIG_X86_LOCAL_APIC */

extern unsigned boot_cpu_id;

#endif /* __ASM_APIC_H */
