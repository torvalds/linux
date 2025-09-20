/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tools for integrating with lru_gen, like parsing the lru_gen debugfs output.
 *
 * Copyright (C) 2025, Google LLC.
 */
#ifndef SELFTEST_KVM_LRU_GEN_UTIL_H
#define SELFTEST_KVM_LRU_GEN_UTIL_H

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

#include "test_util.h"

#define MAX_NR_GENS 16 /* MAX_NR_GENS in include/linux/mmzone.h */
#define MAX_NR_NODES 4 /* Maximum number of nodes supported by the test */

#define LRU_GEN_DEBUGFS "/sys/kernel/debug/lru_gen"
#define LRU_GEN_ENABLED_PATH "/sys/kernel/mm/lru_gen/enabled"
#define LRU_GEN_ENABLED 1
#define LRU_GEN_MM_WALK 2

struct generation_stats {
	int gen;
	long age_ms;
	long nr_anon;
	long nr_file;
};

struct node_stats {
	int node;
	int nr_gens; /* Number of populated gens entries. */
	struct generation_stats gens[MAX_NR_GENS];
};

struct memcg_stats {
	unsigned long memcg_id;
	int nr_nodes; /* Number of populated nodes entries. */
	struct node_stats nodes[MAX_NR_NODES];
};

void lru_gen_read_memcg_stats(struct memcg_stats *stats, const char *memcg);
long lru_gen_sum_memcg_stats(const struct memcg_stats *stats);
long lru_gen_sum_memcg_stats_for_gen(int gen, const struct memcg_stats *stats);
void lru_gen_do_aging(struct memcg_stats *stats, const char *memcg);
int lru_gen_find_generation(const struct memcg_stats *stats,
			    unsigned long total_pages);
bool lru_gen_usable(void);

#endif /* SELFTEST_KVM_LRU_GEN_UTIL_H */
