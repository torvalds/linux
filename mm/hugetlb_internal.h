/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal HugeTLB definitions.
 * (C) Nadia Yvette Chambers, April 2004
 */

#ifndef _LINUX_HUGETLB_INTERNAL_H
#define _LINUX_HUGETLB_INTERNAL_H

#include <linux/hugetlb.h>
#include <linux/hugetlb_cgroup.h>

/*
 * Check if the hstate represents gigantic pages but gigantic page
 * runtime support is not available. This is a common condition used to
 * skip operations that cannot be performed on gigantic pages when runtime
 * support is disabled.
 */
static inline bool hstate_is_gigantic_no_runtime(struct hstate *h)
{
	return hstate_is_gigantic(h) && !gigantic_page_runtime_supported();
}

/*
 * common helper functions for hstate_next_node_to_{alloc|free}.
 * We may have allocated or freed a huge page based on a different
 * nodes_allowed previously, so h->next_node_to_{alloc|free} might
 * be outside of *nodes_allowed.  Ensure that we use an allowed
 * node for alloc or free.
 */
static inline int next_node_allowed(int nid, nodemask_t *nodes_allowed)
{
	nid = next_node_in(nid, *nodes_allowed);
	VM_BUG_ON(nid >= MAX_NUMNODES);

	return nid;
}

static inline int get_valid_node_allowed(int nid, nodemask_t *nodes_allowed)
{
	if (!node_isset(nid, *nodes_allowed))
		nid = next_node_allowed(nid, nodes_allowed);
	return nid;
}

/*
 * returns the previously saved node ["this node"] from which to
 * allocate a persistent huge page for the pool and advance the
 * next node from which to allocate, handling wrap at end of node
 * mask.
 */
static inline int hstate_next_node_to_alloc(int *next_node,
					    nodemask_t *nodes_allowed)
{
	int nid;

	VM_BUG_ON(!nodes_allowed);

	nid = get_valid_node_allowed(*next_node, nodes_allowed);
	*next_node = next_node_allowed(nid, nodes_allowed);

	return nid;
}

/*
 * helper for remove_pool_hugetlb_folio() - return the previously saved
 * node ["this node"] from which to free a huge page.  Advance the
 * next node id whether or not we find a free huge page to free so
 * that the next attempt to free addresses the next node.
 */
static inline int hstate_next_node_to_free(struct hstate *h, nodemask_t *nodes_allowed)
{
	int nid;

	VM_BUG_ON(!nodes_allowed);

	nid = get_valid_node_allowed(h->next_nid_to_free, nodes_allowed);
	h->next_nid_to_free = next_node_allowed(nid, nodes_allowed);

	return nid;
}

#define for_each_node_mask_to_alloc(next_node, nr_nodes, node, mask)		\
	for (nr_nodes = nodes_weight(*mask);				\
		nr_nodes > 0 &&						\
		((node = hstate_next_node_to_alloc(next_node, mask)) || 1);	\
		nr_nodes--)

#define for_each_node_mask_to_free(hs, nr_nodes, node, mask)		\
	for (nr_nodes = nodes_weight(*mask);				\
		nr_nodes > 0 &&						\
		((node = hstate_next_node_to_free(hs, mask)) || 1);	\
		nr_nodes--)

extern void remove_hugetlb_folio(struct hstate *h, struct folio *folio,
				 bool adjust_surplus);
extern void add_hugetlb_folio(struct hstate *h, struct folio *folio,
			      bool adjust_surplus);
extern void init_new_hugetlb_folio(struct folio *folio);
extern void prep_and_add_allocated_folios(struct hstate *h,
					  struct list_head *folio_list);
extern long demote_pool_huge_page(struct hstate *src,
				  nodemask_t *nodes_allowed,
				  unsigned long nr_to_demote);
extern ssize_t __nr_hugepages_store_common(bool obey_mempolicy,
					   struct hstate *h, int nid,
					   unsigned long count, size_t len);

extern void hugetlb_sysfs_init(void) __init;

#ifdef CONFIG_SYSCTL
extern void hugetlb_sysctl_init(void);
#else
static inline void hugetlb_sysctl_init(void) { }
#endif

#endif /* _LINUX_HUGETLB_INTERNAL_H */
