/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_XEN_HYPERVISOR_H
#define _ASM_ARM_XEN_HYPERVISOR_H

#include <linux/init.h>

extern struct shared_info *HYPERVISOR_shared_info;
extern struct start_info *xen_start_info;

/* Lazy mode for batching updates / context switch */
enum paravirt_lazy_mode {
	PARAVIRT_LAZY_NONE,
	PARAVIRT_LAZY_MMU,
	PARAVIRT_LAZY_CPU,
};

static inline enum paravirt_lazy_mode paravirt_get_lazy_mode(void)
{
	return PARAVIRT_LAZY_NONE;
}

#ifdef CONFIG_XEN
void __init xen_early_init(void);
#else
static inline void xen_early_init(void) { return; }
#endif

#ifdef CONFIG_HOTPLUG_CPU
static inline void xen_arch_register_cpu(int num)
{
}

static inline void xen_arch_unregister_cpu(int num)
{
}
#endif

#endif /* _ASM_ARM_XEN_HYPERVISOR_H */
