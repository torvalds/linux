/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2025 Meta Platforms, Inc. and affiliates. */
#ifndef __CGROUP_ITER_MEMCG_H
#define __CGROUP_ITER_MEMCG_H

struct memcg_query {
	/* some node_stat_item's */
	unsigned long nr_anon_mapped;
	unsigned long nr_shmem;
	unsigned long nr_file_pages;
	unsigned long nr_file_mapped;
	/* some memcg_stat_item */
	unsigned long memcg_kmem;
	/* some vm_event_item */
	unsigned long pgfault;
};

#endif /* __CGROUP_ITER_MEMCG_H */
