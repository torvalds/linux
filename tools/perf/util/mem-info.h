/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_MEM_INFO_H
#define __PERF_MEM_INFO_H

#include <linux/refcount.h>
#include <linux/perf_event.h>
#include <internal/rc_check.h>
#include "map_symbol.h"

DECLARE_RC_STRUCT(mem_info) {
	struct addr_map_symbol	iaddr;
	struct addr_map_symbol	daddr;
	union perf_mem_data_src	data_src;
	refcount_t		refcnt;
};

struct mem_info *mem_info__new(void);
struct mem_info *mem_info__get(struct mem_info *mi);
void   mem_info__put(struct mem_info *mi);

static inline void __mem_info__zput(struct mem_info **mi)
{
	mem_info__put(*mi);
	*mi = NULL;
}

#define mem_info__zput(mi) __mem_info__zput(&mi)

static inline struct addr_map_symbol *mem_info__iaddr(struct mem_info *mi)
{
	return &RC_CHK_ACCESS(mi)->iaddr;
}

static inline struct addr_map_symbol *mem_info__daddr(struct mem_info *mi)
{
	return &RC_CHK_ACCESS(mi)->daddr;
}

static inline union perf_mem_data_src *mem_info__data_src(struct mem_info *mi)
{
	return &RC_CHK_ACCESS(mi)->data_src;
}

static inline const union perf_mem_data_src *mem_info__const_data_src(const struct mem_info *mi)
{
	return &RC_CHK_ACCESS(mi)->data_src;
}

static inline refcount_t *mem_info__refcnt(struct mem_info *mi)
{
	return &RC_CHK_ACCESS(mi)->refcnt;
}

#endif /* __PERF_MEM_INFO_H */
