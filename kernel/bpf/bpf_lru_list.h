/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016 Facebook
 */
#ifndef __BPF_LRU_LIST_H_
#define __BPF_LRU_LIST_H_

#include <linux/cache.h>
#include <linux/list.h>
#include <linux/spinlock_types.h>

#define NR_BPF_LRU_LIST_T	(3)
#define NR_BPF_LRU_LIST_COUNT	(2)
#define NR_BPF_LRU_LOCAL_LIST_T (2)
#define BPF_LOCAL_LIST_T_OFFSET NR_BPF_LRU_LIST_T

enum bpf_lru_list_type {
	BPF_LRU_LIST_T_ACTIVE,
	BPF_LRU_LIST_T_INACTIVE,
	BPF_LRU_LIST_T_FREE,
	BPF_LRU_LOCAL_LIST_T_FREE,
	BPF_LRU_LOCAL_LIST_T_PENDING,
};

struct bpf_lru_node {
	struct list_head list;
	u16 cpu;
	u8 type;
	u8 ref;
};

struct bpf_lru_list {
	struct list_head lists[NR_BPF_LRU_LIST_T];
	unsigned int counts[NR_BPF_LRU_LIST_COUNT];
	/* The next inactive list rotation starts from here */
	struct list_head *next_inactive_rotation;

	raw_spinlock_t lock ____cacheline_aligned_in_smp;
};

struct bpf_lru_locallist {
	struct list_head lists[NR_BPF_LRU_LOCAL_LIST_T];
	u16 next_steal;
	raw_spinlock_t lock;
};

struct bpf_common_lru {
	struct bpf_lru_list lru_list;
	struct bpf_lru_locallist __percpu *local_list;
};

typedef bool (*del_from_htab_func)(void *arg, struct bpf_lru_node *node);

struct bpf_lru {
	union {
		struct bpf_common_lru common_lru;
		struct bpf_lru_list __percpu *percpu_lru;
	};
	del_from_htab_func del_from_htab;
	void *del_arg;
	unsigned int hash_offset;
	unsigned int target_free;
	unsigned int nr_scans;
	bool percpu;
};

static inline void bpf_lru_node_set_ref(struct bpf_lru_node *node)
{
	if (!READ_ONCE(node->ref))
		WRITE_ONCE(node->ref, 1);
}

int bpf_lru_init(struct bpf_lru *lru, bool percpu, u32 hash_offset,
		 del_from_htab_func del_from_htab, void *delete_arg);
void bpf_lru_populate(struct bpf_lru *lru, void *buf, u32 node_offset,
		      u32 elem_size, u32 nr_elems);
void bpf_lru_destroy(struct bpf_lru *lru);
struct bpf_lru_node *bpf_lru_pop_free(struct bpf_lru *lru, u32 hash);
void bpf_lru_push_free(struct bpf_lru *lru, struct bpf_lru_node *node);

#endif
