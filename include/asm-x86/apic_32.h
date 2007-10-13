#ifndef __ASM_APIC_H
#define __ASM_APIC_H

#include <linux/pm.h>
#include <linux/delay.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <asm/processor.h>
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


extern void generic_apic_probe(void);

#ifdef CONFIG_X86_LOCAL_APIC

/*
 * Basic functions accessing APICs.
 */
#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define apic_write native_apic_write
#define apic_write_atomic native_apic_write_atomic
#define apic_read native_apic_read
#define setup_boot_clock setup_boot_APIC_clock
#define setup_secondary_clock setup_secondary_APIC_clock
#endif

static __inline fastcall void native_apic_write(unsigned long reg,
						unsigned long v)
{
	*((volatile unsigned long *)(APIC_BASE+reg)) = v;
}

static __inline fastcall void native_apic_write_atomic(unsigned long reg,
						       unsigned long v)
{
	xchg((volatile unsigned long *)(APIC_BASE+reg), v);
}

static __inline fastcall unsigned long native_apic_read(unsigned long reg)
{
	return *((volatile unsigned long *)(APIC_BASE+reg));
}

void apic_wait_icr_idle(void);
unsigned long safe_apic_wait_icr_idle(void);
int get_physical_broadcast(void);

#ifdef CONFIG_X86_GOOD_APIC
# define FORCE_READ_AROUND_WRITE 0
# define apic_read_around(x)
# define apic_write_around(x,y) apic_write((x),(y))
#else
# define FORCE_READ_AROUND_WRITE 1
# define apic_read_around(x) apic_read(x)
# define apic_write_around(x,y) apic_write_atomic((x),(y))
#endif

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

extern int lapic_get_maxlvt(void);
extern void clear_local_APIC(void);
extern void connect_bsp_APIC (void);
extern void disconnect_bsp_APIC (int virt_wire_setup);
extern void disable_local_APIC (void);
extern void lapic_shutdown (void);
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

extern void enable_NMI_through_LVT0 (void * dummy);

#define ARCH_APICTIMER_STOPS_ON_C3	1

extern int timer_over_8254;
extern int local_apic_timer_c2_ok;

extern int local_apic_timer_disabled;

#else /* !CONFIG_X86_LOCAL_APIC */
static inline void lapic_shutdown(void) { }

#endif /* !CONFIG_X86_LOCAL_APIC */

#endif /* __ASM_APIC_H */
