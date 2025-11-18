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
struct page;

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

struct kho_vmalloc_chunk;
struct kho_vmalloc {
	DECLARE_KHOSER_PTR(first, struct kho_vmalloc_chunk *);
	unsigned int total_pages;
	unsigned short flags;
	unsigned short order;
};

#ifdef CONFIG_KEXEC_HANDOVER
bool kho_is_enabled(void);
bool is_kho_boot(void);

int kho_preserve_folio(struct folio *folio);
int kho_preserve_pages(struct page *page, unsigned int nr_pages);
int kho_preserve_vmalloc(void *ptr, struct kho_vmalloc *preservation);
struct folio *kho_restore_folio(phys_addr_t phys);
struct page *kho_restore_pages(phys_addr_t phys, unsigned int nr_pages);
void *kho_restore_vmalloc(const struct kho_vmalloc *preservation);
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

static inline bool is_kho_boot(void)
{
	return false;
}

static inline int kho_preserve_folio(struct folio *folio)
{
	return -EOPNOTSUPP;
}

static inline int kho_preserve_pages(struct page *page, unsigned int nr_pages)
{
	return -EOPNOTSUPP;
}

static inline int kho_preserve_vmalloc(void *ptr,
				       struct kho_vmalloc *preservation)
{
	return -EOPNOTSUPP;
}

static inline struct folio *kho_restore_folio(phys_addr_t phys)
{
	return NULL;
}

static inline struct page *kho_restore_pages(phys_addr_t phys,
					     unsigned int nr_pages)
{
	return NULL;
}

static inline void *kho_restore_vmalloc(const struct kho_vmalloc *preservation)
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
