/* Copyright (c) 2016 Facebook
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/version.h>
#include <uapi/linux/bpf.h>
#include "bpf_helpers.h"

#define MAX_ENTRIES 1000

struct bpf_map_def SEC("maps") hash_map = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = MAX_ENTRIES,
};

struct bpf_map_def SEC("maps") lru_hash_map = {
	.type = BPF_MAP_TYPE_LRU_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 10000,
};

struct bpf_map_def SEC("maps") percpu_lru_hash_map = {
	.type = BPF_MAP_TYPE_LRU_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 10000,
	.map_flags = BPF_F_NO_COMMON_LRU,
};

struct bpf_map_def SEC("maps") percpu_hash_map = {
	.type = BPF_MAP_TYPE_PERCPU_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = MAX_ENTRIES,
};

struct bpf_map_def SEC("maps") hash_map_alloc = {
	.type = BPF_MAP_TYPE_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = MAX_ENTRIES,
	.map_flags = BPF_F_NO_PREALLOC,
};

struct bpf_map_def SEC("maps") percpu_hash_map_alloc = {
	.type = BPF_MAP_TYPE_PERCPU_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = MAX_ENTRIES,
	.map_flags = BPF_F_NO_PREALLOC,
};

struct bpf_map_def SEC("maps") lpm_trie_map_alloc = {
	.type = BPF_MAP_TYPE_LPM_TRIE,
	.key_size = 8,
	.value_size = sizeof(long),
	.max_entries = 10000,
	.map_flags = BPF_F_NO_PREALLOC,
};

SEC("kprobe/sys_getuid")
int stress_hmap(struct pt_regs *ctx)
{
	u32 key = bpf_get_current_pid_tgid();
	long init_val = 1;
	long *value;

	bpf_map_update_elem(&hash_map, &key, &init_val, BPF_ANY);
	value = bpf_map_lookup_elem(&hash_map, &key);
	if (value)
		bpf_map_delete_elem(&hash_map, &key);

	return 0;
}

SEC("kprobe/sys_geteuid")
int stress_percpu_hmap(struct pt_regs *ctx)
{
	u32 key = bpf_get_current_pid_tgid();
	long init_val = 1;
	long *value;

	bpf_map_update_elem(&percpu_hash_map, &key, &init_val, BPF_ANY);
	value = bpf_map_lookup_elem(&percpu_hash_map, &key);
	if (value)
		bpf_map_delete_elem(&percpu_hash_map, &key);
	return 0;
}
SEC("kprobe/sys_getgid")
int stress_hmap_alloc(struct pt_regs *ctx)
{
	u32 key = bpf_get_current_pid_tgid();
	long init_val = 1;
	long *value;

	bpf_map_update_elem(&hash_map_alloc, &key, &init_val, BPF_ANY);
	value = bpf_map_lookup_elem(&hash_map_alloc, &key);
	if (value)
		bpf_map_delete_elem(&hash_map_alloc, &key);
	return 0;
}

SEC("kprobe/sys_getegid")
int stress_percpu_hmap_alloc(struct pt_regs *ctx)
{
	u32 key = bpf_get_current_pid_tgid();
	long init_val = 1;
	long *value;

	bpf_map_update_elem(&percpu_hash_map_alloc, &key, &init_val, BPF_ANY);
	value = bpf_map_lookup_elem(&percpu_hash_map_alloc, &key);
	if (value)
		bpf_map_delete_elem(&percpu_hash_map_alloc, &key);
	return 0;
}

SEC("kprobe/sys_getpid")
int stress_lru_hmap_alloc(struct pt_regs *ctx)
{
	u32 key = bpf_get_prandom_u32();
	long val = 1;

	bpf_map_update_elem(&lru_hash_map, &key, &val, BPF_ANY);

	return 0;
}

SEC("kprobe/sys_getppid")
int stress_percpu_lru_hmap_alloc(struct pt_regs *ctx)
{
	u32 key = bpf_get_prandom_u32();
	long val = 1;

	bpf_map_update_elem(&percpu_lru_hash_map, &key, &val, BPF_ANY);

	return 0;
}

SEC("kprobe/sys_gettid")
int stress_lpm_trie_map_alloc(struct pt_regs *ctx)
{
	union {
		u32 b32[2];
		u8 b8[8];
	} key;
	unsigned int i;

	key.b32[0] = 32;
	key.b8[4] = 192;
	key.b8[5] = 168;
	key.b8[6] = 0;
	key.b8[7] = 1;

#pragma clang loop unroll(full)
	for (i = 0; i < 32; ++i)
		bpf_map_lookup_elem(&lpm_trie_map_alloc, &key);

	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
