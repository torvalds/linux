/*
 *  linux/include/asm-i386/nmi.h
 */
#ifndef ASM_NMI_H
#define ASM_NMI_H

#include <linux/pm.h>
#include <asm/irq.h>

#ifdef ARCH_HAS_NMI_WATCHDOG

/**
 * do_nmi_callback
 *
 * Check to see if a callback exists and execute it.  Return 1
 * if the handler exists and was handled successfully.
 */
int do_nmi_callback(struct pt_regs *regs, int cpu);

extern int nmi_watchdog_enabled;
extern int avail_to_resrv_perfctr_nmi_bit(unsigned int);
extern int avail_to_resrv_perfctr_nmi(unsigned int);
extern int reserve_perfctr_nmi(unsigned int);
extern void release_perfctr_nmi(unsigned int);
extern int reserve_evntsel_nmi(unsigned int);
extern void release_evntsel_nmi(unsigned int);

extern void setup_apic_nmi_watchdog (void *);
extern void stop_apic_nmi_watchdog (void *);
extern void disable_timer_nmi_watchdog(void);
extern void enable_timer_nmi_watchdog(void);
extern int nmi_watchdog_tick (struct pt_regs * regs, unsigned reason);

extern atomic_t nmi_active;
extern unsigned int nmi_watchdog;
#define NMI_DISABLED    -1
#define NMI_NONE	0
#define NMI_IO_APIC	1
#define NMI_LOCAL_APIC	2
#define NMI_INVALID	3
#define NMI_DEFAULT	NMI_DISABLED

struct ctl_table;
struct file;
extern int proc_nmi_enabled(struct ctl_table *, int , struct file *,
			void __user *, size_t *, loff_t *);
extern int unknown_nmi_panic;

void __trigger_all_cpu_backtrace(void);
#define trigger_all_cpu_backtrace() __trigger_all_cpu_backtrace()

#endif

void lapic_watchdog_stop(void);
int lapic_watchdog_init(unsigned nmi_hz);
int lapic_wd_event(unsigned nmi_hz);
unsigned lapic_adjust_nmi_hz(unsigned hz);
int lapic_watchdog_ok(void);
void disable_lapic_nmi_watchdog(void);
void enable_lapic_nmi_watchdog(void);
void stop_nmi(void);
void restart_nmi(void);

#endif /* ASM_NMI_H */
