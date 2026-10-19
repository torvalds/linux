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
int em_perf_domain_for_id(int id, int (*cb)(struct em_perf_domain *, void *),
			  void *data);
void em_notify_pd_created(const struct em_perf_domain *pd);
void em_notify_pd_deleted(const struct em_perf_domain *pd);
void em_notify_pd_updated(const struct em_perf_domain *pd);
#else
static inline
int for_each_em_perf_domain(int (*cb)(struct em_perf_domain*, void *),
			    void *data)
{
	return -EINVAL;
}
static inline
int em_perf_domain_for_id(int id, int (*cb)(struct em_perf_domain *, void *),
			  void *data)
{
	return -EINVAL;
}

static inline void em_notify_pd_created(const struct em_perf_domain *pd) {}

static inline void em_notify_pd_deleted(const struct em_perf_domain *pd) {}

static inline void em_notify_pd_updated(const struct em_perf_domain *pd) {}
#endif

#endif /* _EM_NETLINK_H */
