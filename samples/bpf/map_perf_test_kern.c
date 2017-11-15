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
#define MAX_NR_CPUS 1024

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

struct bpf_map_def SEC("maps") nocommon_lru_hash_map = {
	.type = BPF_MAP_TYPE_LRU_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = 10000,
	.map_flags = BPF_F_NO_COMMON_LRU,
};

struct bpf_map_def SEC("maps") inner_lru_hash_map = {
	.type = BPF_MAP_TYPE_LRU_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = MAX_ENTRIES,
	.map_flags = BPF_F_NUMA_NODE,
	.numa_node = 0,
};

struct bpf_map_def SEC("maps") array_of_lru_hashs = {
	.type = BPF_MAP_TYPE_ARRAY_OF_MAPS,
	.key_size = sizeof(u32),
	.max_entries = MAX_NR_CPUS,
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

struct bpf_map_def SEC("maps") array_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = MAX_ENTRIES,
};

struct bpf_map_def SEC("maps") lru_hash_lookup_map = {
	.type = BPF_MAP_TYPE_LRU_HASH,
	.key_size = sizeof(u32),
	.value_size = sizeof(long),
	.max_entries = MAX_ENTRIES,
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

SEC("kprobe/sys_connect")
int stress_lru_hmap_alloc(struct pt_regs *ctx)
{
	char fmt[] = "Failed at stress_lru_hmap_alloc. ret:%dn";
	union {
		u16 dst6[8];
		struct {
			u16 magic0;
			u16 magic1;
			u16 tcase;
			u16 unused16;
			u32 unused32;
			u32 key;
		};
	} test_params;
	struct sockaddr_in6 *in6;
	u16 test_case;
	int addrlen, ret;
	long val = 1;
	u32 key = 0;

	in6 = (struct sockaddr_in6 *)PT_REGS_PARM2(ctx);
	addrlen = (int)PT_REGS_PARM3(ctx);

	if (addrlen != sizeof(*in6))
		return 0;

	ret = bpf_probe_read(test_params.dst6, sizeof(test_params.dst6),
			     &in6->sin6_addr);
	if (ret)
		goto done;

	if (test_params.magic0 != 0xdead ||
	    test_params.magic1 != 0xbeef)
		return 0;

	test_case = test_params.tcase;
	if (test_case != 3)
		key = bpf_get_prandom_u32();

	if (test_case == 0) {
		ret = bpf_map_update_elem(&lru_hash_map, &key, &val, BPF_ANY);
	} else if (test_case == 1) {
		ret = bpf_map_update_elem(&nocommon_lru_hash_map, &key, &val,
					  BPF_ANY);
	} else if (test_case == 2) {
		void *nolocal_lru_map;
		int cpu = bpf_get_smp_processor_id();

		nolocal_lru_map = bpf_map_lookup_elem(&array_of_lru_hashs,
						      &cpu);
		if (!nolocal_lru_map) {
			ret = -ENOENT;
			goto done;
		}

		ret = bpf_map_update_elem(nolocal_lru_map, &key, &val,
					  BPF_ANY);
	} else if (test_case == 3) {
		u32 i;

		key = test_params.key;

#pragma clang loop unroll(full)
		for (i = 0; i < 32; i++) {
			bpf_map_lookup_elem(&lru_hash_lookup_map, &key);
			key++;
		}
	} else {
		ret = -EINVAL;
	}

done:
	if (ret)
		bpf_trace_printk(fmt, sizeof(fmt), ret);

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

SEC("kprobe/sys_getpgid")
int stress_hash_map_lookup(struct pt_regs *ctx)
{
	u32 key = 1, i;
	long *value;

#pragma clang loop unroll(full)
	for (i = 0; i < 64; ++i)
		value = bpf_map_lookup_elem(&hash_map, &key);

	return 0;
}

SEC("kprobe/sys_getppid")
int stress_array_map_lookup(struct pt_regs *ctx)
{
	u32 key = 1, i;
	long *value;

#pragma clang loop unroll(full)
	for (i = 0; i < 64; ++i)
		value = bpf_map_lookup_elem(&array_map, &key);

	return 0;
}

char _license[] SEC("license") = "GPL";
u32 _version SEC("version") = LINUX_VERSION_CODE;
