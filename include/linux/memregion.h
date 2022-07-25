/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MEMREGION_H_
#define _MEMREGION_H_
#include <linux/types.h>
#include <linux/errno.h>

struct memregion_info {
	int target_node;
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
#endif /* _MEMREGION_H_ */
