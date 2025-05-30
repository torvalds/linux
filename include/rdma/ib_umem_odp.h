/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2014 Mellanox Technologies. All rights reserved.
 */

#ifndef IB_UMEM_ODP_H
#define IB_UMEM_ODP_H

#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>
#include <linux/hmm-dma.h>

struct ib_umem_odp {
	struct ib_umem umem;
	struct mmu_interval_notifier notifier;
	struct pid *tgid;

	struct hmm_dma_map map;

	/*
	 * The umem_mutex protects the page_list field of an ODP
	 * umem, allowing only a single thread to map/unmap pages. The mutex
	 * also protects access to the mmu notifier counters.
	 */
	struct mutex		umem_mutex;
	void			*private; /* for the HW driver to use. */

	int npages;

	/*
	 * An implicit odp umem cannot be DMA mapped, has 0 length, and serves
	 * only as an anchor for the driver to hold onto the per_mm. FIXME:
	 * This should be removed and drivers should work with the per_mm
	 * directly.
	 */
	bool is_implicit_odp;

	unsigned int		page_shift;
};

static inline struct ib_umem_odp *to_ib_umem_odp(struct ib_umem *umem)
{
	return container_of(umem, struct ib_umem_odp, umem);
}

/* Returns the first page of an ODP umem. */
static inline unsigned long ib_umem_start(struct ib_umem_odp *umem_odp)
{
	return umem_odp->notifier.interval_tree.start;
}

/* Returns the address of the page after the last one of an ODP umem. */
static inline unsigned long ib_umem_end(struct ib_umem_odp *umem_odp)
{
	return umem_odp->notifier.interval_tree.last + 1;
}

static inline size_t ib_umem_odp_num_pages(struct ib_umem_odp *umem_odp)
{
	return (ib_umem_end(umem_odp) - ib_umem_start(umem_odp)) >>
	       umem_odp->page_shift;
}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING

struct ib_umem_odp *
ib_umem_odp_get(struct ib_device *device, unsigned long addr, size_t size,
		int access, const struct mmu_interval_notifier_ops *ops);
struct ib_umem_odp *ib_umem_odp_alloc_implicit(struct ib_device *device,
					       int access);
struct ib_umem_odp *
ib_umem_odp_alloc_child(struct ib_umem_odp *root_umem, unsigned long addr,
			size_t size,
			const struct mmu_interval_notifier_ops *ops);
void ib_umem_odp_release(struct ib_umem_odp *umem_odp);

int ib_umem_odp_map_dma_and_lock(struct ib_umem_odp *umem_odp, u64 start_offset,
				 u64 bcnt, u64 access_mask, bool fault);

void ib_umem_odp_unmap_dma_pages(struct ib_umem_odp *umem_odp, u64 start_offset,
				 u64 bound);

#else /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */

static inline struct ib_umem_odp *
ib_umem_odp_get(struct ib_device *device, unsigned long addr, size_t size,
		int access, const struct mmu_interval_notifier_ops *ops)
{
	return ERR_PTR(-EINVAL);
}

static inline void ib_umem_odp_release(struct ib_umem_odp *umem_odp) {}

#endif /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */

#endif /* IB_UMEM_ODP_H */
