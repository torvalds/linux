/*
 *  linux/include/asm-i386/nmi.h
 */
#ifndef ASM_NMI_H
#define ASM_NMI_H

#include <linux/pm.h>
#include <asm/io.h>
 
struct pt_regs;

typedef int (*nmi_callback_t)(struct pt_regs * regs, int cpu);

/**
 * set_nmi_callback
 *
 * Set a handler for an NMI. Only one handler may be
 * set. Return 1 if the NMI was handled.
 */
void set_nmi_callback(nmi_callback_t callback);

/**
 * unset_nmi_callback
 *
 * Remove the handler previously set.
 */
void unset_nmi_callback(void);

#ifdef CONFIG_PM
 
/** Replace the PM callback routine for NMI. */
struct pm_dev * set_nmi_pm_callback(pm_callback callback);

/** Unset the PM callback routine back to the default. */
void unset_nmi_pm_callback(struct pm_dev * dev);

#else

static inline struct pm_dev * set_nmi_pm_callback(pm_callback callback)
{
	return 0;
} 
 
static inline void unset_nmi_pm_callback(struct pm_dev * dev)
{
}

#endif /* CONFIG_PM */
 
extern void default_do_nmi(struct pt_regs *);
extern void die_nmi(char *str, struct pt_regs *regs);

#define get_nmi_reason() inb(0x61)

extern int panic_on_timeout;
extern int unknown_nmi_panic;

extern int check_nmi_watchdog(void);
 
extern void setup_apic_nmi_watchdog (void);
extern int reserve_lapic_nmi(void);
extern void release_lapic_nmi(void);
extern void disable_timer_nmi_watchdog(void);
extern void enable_timer_nmi_watchdog(void);
extern void nmi_watchdog_tick (struct pt_regs * regs, unsigned reason);

extern void nmi_watchdog_default(void);
extern int setup_nmi_watchdog(char *);

extern unsigned int nmi_watchdog;
#define NMI_DEFAULT	-1
#define NMI_NONE	0
#define NMI_IO_APIC	1
#define NMI_LOCAL_APIC	2
#define NMI_INVALID	3

#endif /* ASM_NMI_H */
