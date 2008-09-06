#ifndef ASM_X86__APIC_H
#define ASM_X86__APIC_H

#include <linux/pm.h>
#include <linux/delay.h>

#include <asm/alternative.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/cpufeature.h>
#include <asm/msr.h>

#define ARCH_APICTIMER_STOPS_ON_C3	1

/*
 * Debugging macros
 */
#define APIC_QUIET   0
#define APIC_VERBOSE 1
#define APIC_DEBUG   2

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

extern unsigned int apic_verbosity;
extern int local_apic_timer_c2_ok;

extern int ioapic_force;

extern int disable_apic;
/*
 * Basic functions accessing APICs.
 */
#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define setup_boot_clock setup_boot_APIC_clock
#define setup_secondary_clock setup_secondary_APIC_clock
#endif

extern int is_vsmp_box(void);
extern void xapic_wait_icr_idle(void);
extern u32 safe_xapic_wait_icr_idle(void);
extern u64 xapic_icr_read(void);
extern void xapic_icr_write(u32, u32);
extern int setup_profiling_timer(unsigned int);

static inline void native_apic_mem_write(u32 reg, u32 v)
{
	volatile u32 *addr = (volatile u32 *)(APIC_BASE + reg);

	alternative_io("movl %0, %1", "xchgl %0, %1", X86_FEATURE_11AP,
		       ASM_OUTPUT2("=r" (v), "=m" (*addr)),
		       ASM_OUTPUT2("0" (v), "m" (*addr)));
}

static inline u32 native_apic_mem_read(u32 reg)
{
	return *((volatile u32 *)(APIC_BASE + reg));
}

static inline void native_apic_msr_write(u32 reg, u32 v)
{
	if (reg == APIC_DFR || reg == APIC_ID || reg == APIC_LDR ||
	    reg == APIC_LVR)
		return;

	wrmsr(APIC_BASE_MSR + (reg >> 4), v, 0);
}

static inline u32 native_apic_msr_read(u32 reg)
{
	u32 low, high;

	if (reg == APIC_DFR)
		return -1;

	rdmsr(APIC_BASE_MSR + (reg >> 4), low, high);
	return low;
}

#ifndef CONFIG_X86_32
extern int x2apic, x2apic_preenabled;
extern void check_x2apic(void);
extern void enable_x2apic(void);
extern void enable_IR_x2apic(void);
extern void x2apic_icr_write(u32 low, u32 id);
#endif

struct apic_ops {
	u32 (*read)(u32 reg);
	void (*write)(u32 reg, u32 v);
	u64 (*icr_read)(void);
	void (*icr_write)(u32 low, u32 high);
	void (*wait_icr_idle)(void);
	u32 (*safe_wait_icr_idle)(void);
};

extern struct apic_ops *apic_ops;

#define apic_read (apic_ops->read)
#define apic_write (apic_ops->write)
#define apic_icr_read (apic_ops->icr_read)
#define apic_icr_write (apic_ops->icr_write)
#define apic_wait_icr_idle (apic_ops->wait_icr_idle)
#define safe_apic_wait_icr_idle (apic_ops->safe_wait_icr_idle)

extern int get_physical_broadcast(void);

#ifdef CONFIG_X86_64
static inline void ack_x2APIC_irq(void)
{
	/* Docs say use 0 for future compatibility */
	native_apic_msr_write(APIC_EOI, 0);
}
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
	apic_write(APIC_EOI, 0);
}

extern int lapic_get_maxlvt(void);
extern void clear_local_APIC(void);
extern void connect_bsp_APIC(void);
extern void disconnect_bsp_APIC(int virt_wire_setup);
extern void disable_local_APIC(void);
extern void lapic_shutdown(void);
extern int verify_local_APIC(void);
extern void cache_APIC_registers(void);
extern void sync_Arb_IDs(void);
extern void init_bsp_APIC(void);
extern void setup_local_APIC(void);
extern void end_local_APIC_setup(void);
extern void init_apic_mappings(void);
extern void setup_boot_APIC_clock(void);
extern void setup_secondary_APIC_clock(void);
extern int APIC_init_uniprocessor(void);
extern void enable_NMI_through_LVT0(void);

/*
 * On 32bit this is mach-xxx local
 */
#ifdef CONFIG_X86_64
extern void early_init_lapic_mapping(void);
extern int apic_is_clustered_box(void);
#else
static inline int apic_is_clustered_box(void)
{
	return 0;
}
#endif

extern u8 setup_APIC_eilvt_mce(u8 vector, u8 msg_type, u8 mask);
extern u8 setup_APIC_eilvt_ibs(u8 vector, u8 msg_type, u8 mask);


#else /* !CONFIG_X86_LOCAL_APIC */
static inline void lapic_shutdown(void) { }
#define local_apic_timer_c2_ok		1
static inline void init_apic_mappings(void) { }

#endif /* !CONFIG_X86_LOCAL_APIC */

#endif /* ASM_X86__APIC_H */
