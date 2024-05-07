// SPDX-License-Identifier: GPL-2.0
#include <linux/zalloc.h>
#include "mem-info.h"

struct mem_info *mem_info__get(struct mem_info *mi)
{
	if (mi)
		refcount_inc(&mi->refcnt);
	return mi;
}

void mem_info__put(struct mem_info *mi)
{
	if (mi && refcount_dec_and_test(&mi->refcnt)) {
		addr_map_symbol__exit(&mi->iaddr);
		addr_map_symbol__exit(&mi->daddr);
		free(mi);
	}
}

struct mem_info *mem_info__new(void)
{
	struct mem_info *mi = zalloc(sizeof(*mi));

	if (mi)
		refcount_set(&mi->refcnt, 1);
	return mi;
}
