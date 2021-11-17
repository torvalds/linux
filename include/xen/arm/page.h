/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_XEN_PAGE_H
#define _ASM_ARM_XEN_PAGE_H

#include <asm/page.h>

#include <linux/pfn.h>
#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/pgtable.h>

#include <xen/xen.h>
#include <xen/interface/grant_table.h>

#define phys_to_machine_mapping_valid(pfn) (1)

/* Xen machine address */
typedef struct xmaddr {
	phys_addr_t maddr;
} xmaddr_t;

/* Xen pseudo-physical address */
typedef struct xpaddr {
	phys_addr_t paddr;
} xpaddr_t;

#define XMADDR(x)	((xmaddr_t) { .maddr = (x) })
#define XPADDR(x)	((xpaddr_t) { .paddr = (x) })

#define INVALID_P2M_ENTRY      (~0UL)

/*
 * The pseudo-physical frame (pfn) used in all the helpers is always based
 * on Xen page granularity (i.e 4KB).
 *
 * A Linux page may be split across multiple non-contiguous Xen page so we
 * have to keep track with frame based on 4KB page granularity.
 *
 * PV drivers should never make a direct usage of those helpers (particularly
 * pfn_to_gfn and gfn_to_pfn).
 */

unsigned long __pfn_to_mfn(unsigned long pfn);
extern struct rb_root phys_to_mach;

/* Pseudo-physical <-> Guest conversion */
static inline unsigned long pfn_to_gfn(unsigned long pfn)
{
	return pfn;
}

static inline unsigned long gfn_to_pfn(unsigned long gfn)
{
	return gfn;
}

/* Pseudo-physical <-> BUS conversion */
static inline unsigned long pfn_to_bfn(unsigned long pfn)
{
	unsigned long mfn;

	if (phys_to_mach.rb_node != NULL) {
		mfn = __pfn_to_mfn(pfn);
		if (mfn != INVALID_P2M_ENTRY)
			return mfn;
	}

	return pfn;
}

static inline unsigned long bfn_to_pfn(unsigned long bfn)
{
	return bfn;
}

#define bfn_to_local_pfn(bfn)	bfn_to_pfn(bfn)

/* VIRT <-> GUEST conversion */
#define virt_to_gfn(v)                                                         \
	({                                                                     \
		WARN_ON_ONCE(!virt_addr_valid(v));                              \
		pfn_to_gfn(virt_to_phys(v) >> XEN_PAGE_SHIFT);                 \
	})
#define gfn_to_virt(m)		(__va(gfn_to_pfn(m) << XEN_PAGE_SHIFT))

#define percpu_to_gfn(v)	\
	(pfn_to_gfn(per_cpu_ptr_to_phys(v) >> XEN_PAGE_SHIFT))

/* Only used in PV code. But ARM guests are always HVM. */
static inline xmaddr_t arbitrary_virt_to_machine(void *vaddr)
{
	BUG();
}

extern int set_foreign_p2m_mapping(struct gnttab_map_grant_ref *map_ops,
				   struct gnttab_map_grant_ref *kmap_ops,
				   struct page **pages, unsigned int count);

extern int clear_foreign_p2m_mapping(struct gnttab_unmap_grant_ref *unmap_ops,
				     struct gnttab_unmap_grant_ref *kunmap_ops,
				     struct page **pages, unsigned int count);

bool __set_phys_to_machine(unsigned long pfn, unsigned long mfn);
bool __set_phys_to_machine_multi(unsigned long pfn, unsigned long mfn,
		unsigned long nr_pages);

static inline bool set_phys_to_machine(unsigned long pfn, unsigned long mfn)
{
	return __set_phys_to_machine(pfn, mfn);
}

#define xen_remap(cookie, size) ioremap_cache((cookie), (size))
#define xen_unmap(cookie) iounmap((cookie))

bool xen_arch_need_swiotlb(struct device *dev,
			   phys_addr_t phys,
			   dma_addr_t dev_addr);
unsigned long xen_get_swiotlb_free_pages(unsigned int order);

#endif /* _ASM_ARM_XEN_PAGE_H */
