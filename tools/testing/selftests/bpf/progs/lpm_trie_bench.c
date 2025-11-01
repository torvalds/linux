// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025 Cloudflare */

#include <vmlinux.h>
#include <errno.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"
#include "bpf_atomic.h"
#include "progs/lpm_trie.h"

#define BPF_OBJ_NAME_LEN 16U
#define MAX_ENTRIES 100000000
#define NR_LOOPS 10000

char _license[] SEC("license") = "GPL";

/* Filled by userspace. See fill_map() in bench_lpm_trie_map.c */
struct {
	__uint(type, BPF_MAP_TYPE_LPM_TRIE);
	__type(key, struct trie_key);
	__type(value, __u32);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, MAX_ENTRIES);
} trie_map SEC(".maps");

long hits;
long duration_ns;

/* Configured from userspace */
__u32 nr_entries;
__u32 prefixlen;
bool random;
__u8 op;

static __u64 latency_free_start;

SEC("fentry/bpf_map_free_deferred")
int BPF_PROG(trie_free_entry, struct work_struct *work)
{
	struct bpf_map *map = container_of(work, struct bpf_map, work);
	char name[BPF_OBJ_NAME_LEN];
	u32 map_type;

	map_type = BPF_CORE_READ(map, map_type);
	if (map_type != BPF_MAP_TYPE_LPM_TRIE)
		return 0;

	/*
	 * Ideally we'd have access to the map ID but that's already
	 * freed before we enter trie_free().
	 */
	BPF_CORE_READ_STR_INTO(&name, map, name);
	if (bpf_strncmp(name, BPF_OBJ_NAME_LEN, "trie_free_map"))
		return 0;

	latency_free_start = bpf_ktime_get_ns();

	return 0;
}

SEC("fexit/bpf_map_free_deferred")
int BPF_PROG(trie_free_exit, struct work_struct *work)
{
	__u64 val;

	if (!latency_free_start)
		return 0;

	val = bpf_ktime_get_ns() - latency_free_start;
	latency_free_start = 0;

	__sync_add_and_fetch(&duration_ns, val);
	__sync_add_and_fetch(&hits, 1);

	return 0;
}

static __u32 cur_key;

static __always_inline void generate_key(struct trie_key *key)
{
	key->prefixlen = prefixlen;

	if (random)
		key->data = bpf_get_prandom_u32() % nr_entries;
	else
		key->data = cur_key++ % nr_entries;
}

static int noop(__u32 index, __u32 *unused)
{
	return 0;
}

static int baseline(__u32 index, __u32 *unused)
{
	struct trie_key key;
	__u32 blackbox = 0;

	generate_key(&key);
	/* Avoid compiler optimizing out the modulo */
	barrier_var(blackbox);
	blackbox = READ_ONCE(key.data);

	return 0;
}

static int lookup(__u32 index, int *retval)
{
	struct trie_key key;

	generate_key(&key);
	if (!bpf_map_lookup_elem(&trie_map, &key)) {
		*retval = -ENOENT;
		return 1;
	}

	return 0;
}

static int insert(__u32 index, int *retval)
{
	struct trie_key key;
	u32 val = 1;
	int err;

	generate_key(&key);
	err = bpf_map_update_elem(&trie_map, &key, &val, BPF_NOEXIST);
	if (err) {
		*retval = err;
		return 1;
	}

	/* Is this the last entry? */
	if (key.data == nr_entries - 1) {
		/* For atomicity concerns, see the comment in delete() */
		*retval = LPM_BENCH_REINIT_MAP;
		return 1;
	}

	return 0;
}

static int update(__u32 index, int *retval)
{
	struct trie_key key;
	u32 val = 1;
	int err;

	generate_key(&key);
	err = bpf_map_update_elem(&trie_map, &key, &val, BPF_EXIST);
	if (err) {
		*retval = err;
		return 1;
	}

	return 0;
}

static int delete(__u32 index, int *retval)
{
	struct trie_key key;
	int err;

	generate_key(&key);
	err = bpf_map_delete_elem(&trie_map, &key);
	if (err) {
		*retval = err;
		return 1;
	}

	/* Do we need to refill the map? */
	if (key.data == nr_entries - 1) {
		/*
		 * Atomicity isn't required because DELETE only supports
		 * one producer running concurrently. What we need is a
		 * way to track how many entries have been deleted from
		 * the trie between consecutive invocations of the BPF
		 * prog because a single bpf_loop() call might not
		 * delete all entries, e.g. when NR_LOOPS < nr_entries.
		 */
		*retval = LPM_BENCH_REINIT_MAP;
		return 1;
	}

	return 0;
}

SEC("xdp")
int BPF_PROG(run_bench)
{
	int err = LPM_BENCH_SUCCESS;
	u64 start, delta;
	int loops;

	start = bpf_ktime_get_ns();

	switch (op) {
	case LPM_OP_NOOP:
		loops = bpf_loop(NR_LOOPS, noop, NULL, 0);
		break;
	case LPM_OP_BASELINE:
		loops = bpf_loop(NR_LOOPS, baseline, NULL, 0);
		break;
	case LPM_OP_LOOKUP:
		loops = bpf_loop(NR_LOOPS, lookup, &err, 0);
		break;
	case LPM_OP_INSERT:
		loops = bpf_loop(NR_LOOPS, insert, &err, 0);
		break;
	case LPM_OP_UPDATE:
		loops = bpf_loop(NR_LOOPS, update, &err, 0);
		break;
	case LPM_OP_DELETE:
		loops = bpf_loop(NR_LOOPS, delete, &err, 0);
		break;
	default:
		bpf_printk("invalid benchmark operation\n");
		return -1;
	}

	delta = bpf_ktime_get_ns() - start;

	__sync_add_and_fetch(&duration_ns, delta);
	__sync_add_and_fetch(&hits, loops);

	return err;
}
