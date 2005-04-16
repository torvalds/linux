/*
 *  linux/include/asm-i386/nmi.h
 */
#ifndef ASM_NMI_H
#define ASM_NMI_H

#include <linux/pm.h>
 
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
 
#endif /* ASM_NMI_H */
