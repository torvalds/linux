/* SPDX-License-Identifier: GPL-2.0 */
/*
 *
 * Generic netlink for energy model.
 *
 * Copyright (c) 2025 Valve Corporation.
 * Author: Changwoo Min <changwoo@igalia.com>
 */
#ifndef _EM_NETLINK_H
#define _EM_NETLINK_H

#if defined(CONFIG_ENERGY_MODEL) && defined(CONFIG_NET)
int for_each_em_perf_domain(int (*cb)(struct em_perf_domain*, void *),
			    void *data);
struct em_perf_domain *em_perf_domain_get_by_id(int id);
#else
static inline
int for_each_em_perf_domain(int (*cb)(struct em_perf_domain*, void *),
			    void *data)
{
	return -EINVAL;
}
static inline
struct em_perf_domain *em_perf_domain_get_by_id(int id)
{
	return NULL;
}
#endif

#endif /* _EM_NETLINK_H */
