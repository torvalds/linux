/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_KEXEC_HANDOVER_H
#define LINUX_KEXEC_HANDOVER_H

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/types.h>

struct kho_scratch {
	phys_addr_t addr;
	phys_addr_t size;
};

struct kho_vmalloc;

struct folio;
struct page;

#ifdef CONFIG_KEXEC_HANDOVER
bool kho_is_enabled(void);
bool is_kho_boot(void);

int kho_preserve_folio(struct folio *folio);
void kho_unpreserve_folio(struct folio *folio);
int kho_preserve_pages(struct page *page, unsigned long nr_pages);
void kho_unpreserve_pages(struct page *page, unsigned long nr_pages);
int kho_preserve_vmalloc(void *ptr, struct kho_vmalloc *preservation);
void kho_unpreserve_vmalloc(struct kho_vmalloc *preservation);
void *kho_alloc_preserve(size_t size);
void kho_unpreserve_free(void *mem);
void kho_restore_free(void *mem);
struct folio *kho_restore_folio(phys_addr_t phys);
struct page *kho_restore_pages(phys_addr_t phys, unsigned long nr_pages);
void *kho_restore_vmalloc(const struct kho_vmalloc *preservation);
int kho_add_subtree(const char *name, void *fdt);
void kho_remove_subtree(void *fdt);
int kho_retrieve_subtree(const char *name, phys_addr_t *phys);

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

static inline void kho_unpreserve_folio(struct folio *folio) { }

static inline int kho_preserve_pages(struct page *page, unsigned int nr_pages)
{
	return -EOPNOTSUPP;
}

static inline void kho_unpreserve_pages(struct page *page, unsigned int nr_pages) { }

static inline int kho_preserve_vmalloc(void *ptr,
				       struct kho_vmalloc *preservation)
{
	return -EOPNOTSUPP;
}

static inline void kho_unpreserve_vmalloc(struct kho_vmalloc *preservation) { }

static inline void *kho_alloc_preserve(size_t size)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void kho_unpreserve_free(void *mem) { }
static inline void kho_restore_free(void *mem) { }

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

static inline int kho_add_subtree(const char *name, void *fdt)
{
	return -EOPNOTSUPP;
}

static inline void kho_remove_subtree(void *fdt) { }

static inline int kho_retrieve_subtree(const char *name, phys_addr_t *phys)
{
	return -EOPNOTSUPP;
}

static inline void kho_memory_init(void) { }

static inline void kho_populate(phys_addr_t fdt_phys, u64 fdt_len,
				phys_addr_t scratch_phys, u64 scratch_len)
{
}
#endif /* CONFIG_KEXEC_HANDOVER */

#endif /* LINUX_KEXEC_HANDOVER_H */
