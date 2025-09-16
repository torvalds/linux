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

struct folio;
struct notifier_block;

#define DECLARE_KHOSER_PTR(name, type) \
	union {                        \
		phys_addr_t phys;      \
		type ptr;              \
	} name
#define KHOSER_STORE_PTR(dest, val)               \
	({                                        \
		typeof(val) v = val;              \
		typecheck(typeof((dest).ptr), v); \
		(dest).phys = virt_to_phys(v);    \
	})
#define KHOSER_LOAD_PTR(src)                                                 \
	({                                                                   \
		typeof(src) s = src;                                         \
		(typeof((s).ptr))((s).phys ? phys_to_virt((s).phys) : NULL); \
	})

struct kho_serialization;

#ifdef CONFIG_KEXEC_HANDOVER
bool kho_is_enabled(void);

int kho_preserve_folio(struct folio *folio);
int kho_preserve_phys(phys_addr_t phys, size_t size);
struct folio *kho_restore_folio(phys_addr_t phys);
int kho_add_subtree(struct kho_serialization *ser, const char *name, void *fdt);
int kho_retrieve_subtree(const char *name, phys_addr_t *phys);

int register_kho_notifier(struct notifier_block *nb);
int unregister_kho_notifier(struct notifier_block *nb);

void kho_memory_init(void);

void kho_populate(phys_addr_t fdt_phys, u64 fdt_len, phys_addr_t scratch_phys,
		  u64 scratch_len);
#else
static inline bool kho_is_enabled(void)
{
	return false;
}

static inline int kho_preserve_folio(struct folio *folio)
{
	return -EOPNOTSUPP;
}

static inline int kho_preserve_phys(phys_addr_t phys, size_t size)
{
	return -EOPNOTSUPP;
}

static inline struct folio *kho_restore_folio(phys_addr_t phys)
{
	return NULL;
}

static inline int kho_add_subtree(struct kho_serialization *ser,
				  const char *name, void *fdt)
{
	return -EOPNOTSUPP;
}

static inline int kho_retrieve_subtree(const char *name, phys_addr_t *phys)
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

static inline void kho_populate(phys_addr_t fdt_phys, u64 fdt_len,
				phys_addr_t scratch_phys, u64 scratch_len)
{
}
#endif /* CONFIG_KEXEC_HANDOVER */

#endif /* LINUX_KEXEC_HANDOVER_H */
