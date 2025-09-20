// SPDX-License-Identifier: GPL-2.0
#include <linux/zalloc.h>
#include "mem-info.h"

struct mem_info *mem_info__get(struct mem_info *mi)
{
	struct mem_info *result;

	if (RC_CHK_GET(result, mi))
		refcount_inc(mem_info__refcnt(mi));

	return result;
}

void mem_info__put(struct mem_info *mi)
{
	if (mi && refcount_dec_and_test(mem_info__refcnt(mi))) {
		addr_map_symbol__exit(mem_info__iaddr(mi));
		addr_map_symbol__exit(mem_info__daddr(mi));
		RC_CHK_FREE(mi);
	} else {
		RC_CHK_PUT(mi);
	}
}

struct mem_info *mem_info__new(void)
{
	struct mem_info *result = NULL;
	RC_STRUCT(mem_info) *mi = zalloc(sizeof(*mi));

	if (ADD_RC_CHK(result, mi))
		refcount_set(mem_info__refcnt(result), 1);

	return result;
}

struct mem_info *mem_info__clone(struct mem_info *mi)
{
	struct mem_info *result = mem_info__new();

	if (result) {
		addr_map_symbol__copy(mem_info__iaddr(result), mem_info__iaddr(mi));
		addr_map_symbol__copy(mem_info__daddr(result), mem_info__daddr(mi));
		mem_info__data_src(result)->val = mem_info__data_src(mi)->val;
	}

	return result;
}
