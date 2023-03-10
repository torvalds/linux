/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MEMREGION_H_
#define _MEMREGION_H_
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/range.h>
#include <linux/bug.h>

struct memregion_info {
	int target_node;
	struct range range;
};

#ifdef CONFIG_MEMREGION
int memregion_alloc(gfp_t gfp);
void memregion_free(int id);
#else
static inline int memregion_alloc(gfp_t gfp)
{
	return -ENOMEM;
}
static inline void memregion_free(int id)
{
}
#endif

/**
 * cpu_cache_invalidate_memregion - drop any CPU cached data for
 *     memregions described by @res_desc
 * @res_desc: one of the IORES_DESC_* types
 *
 * Perform cache maintenance after a memory event / operation that
 * changes the contents of physical memory in a cache-incoherent manner.
 * For example, device memory technologies like NVDIMM and CXL have
 * device secure erase, and dynamic region provision that can replace
 * the memory mapped to a given physical address.
 *
 * Limit the functionality to architectures that have an efficient way
 * to writeback and invalidate potentially terabytes of address space at
 * once.  Note that this routine may or may not write back any dirty
 * contents while performing the invalidation. It is only exported for
 * the explicit usage of the NVDIMM and CXL modules in the 'DEVMEM'
 * symbol namespace on bare platforms.
 *
 * Returns 0 on success or negative error code on a failure to perform
 * the cache maintenance.
 */
#ifdef CONFIG_ARCH_HAS_CPU_CACHE_INVALIDATE_MEMREGION
int cpu_cache_invalidate_memregion(int res_desc);
bool cpu_cache_has_invalidate_memregion(void);
#else
static inline bool cpu_cache_has_invalidate_memregion(void)
{
	return false;
}

static inline int cpu_cache_invalidate_memregion(int res_desc)
{
	WARN_ON_ONCE("CPU cache invalidation required");
	return -ENXIO;
}
#endif
#endif /* _MEMREGION_H_ */
