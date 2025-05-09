/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_KEXEC_HANDOVER_H
#define LINUX_KEXEC_HANDOVER_H

#include <linux/types.h>
#include <linux/errno.h>

struct kho_scratch {
	phys_addr_t addr;
	phys_addr_t size;
};

/* KHO Notifier index */
enum kho_event {
	KEXEC_KHO_FINALIZE = 0,
	KEXEC_KHO_ABORT = 1,
};

struct notifier_block;

struct kho_serialization;

#ifdef CONFIG_KEXEC_HANDOVER
bool kho_is_enabled(void);

int kho_add_subtree(struct kho_serialization *ser, const char *name, void *fdt);

int register_kho_notifier(struct notifier_block *nb);
int unregister_kho_notifier(struct notifier_block *nb);

void kho_memory_init(void);
#else
static inline bool kho_is_enabled(void)
{
	return false;
}

static inline int kho_add_subtree(struct kho_serialization *ser,
				  const char *name, void *fdt)
{
	return -EOPNOTSUPP;
}

static inline int register_kho_notifier(struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline int unregister_kho_notifier(struct notifier_block *nb)
{
	return -EOPNOTSUPP;
}

static inline void kho_memory_init(void)
{
}
#endif /* CONFIG_KEXEC_HANDOVER */

#endif /* LINUX_KEXEC_HANDOVER_H */
