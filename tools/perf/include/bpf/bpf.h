// SPDX-License-Identifier: GPL-2.0
#ifndef _PERF_BPF_H
#define _PERF_BPF_H

#include <uapi/linux/bpf.h>

/*
 * A helper structure used by eBPF C program to describe map attributes to
 * elf_bpf loader, taken from tools/testing/selftests/bpf/bpf_helpers.h:
 */
struct bpf_map {
        unsigned int type;
        unsigned int key_size;
        unsigned int value_size;
        unsigned int max_entries;
        unsigned int map_flags;
        unsigned int inner_map_idx;
        unsigned int numa_node;
};

#define bpf_map(name, _type, type_key, type_val, _max_entries)	\
struct bpf_map SEC("maps") name = {				\
	.type	     = BPF_MAP_TYPE_##_type,			\
	.key_size    = sizeof(type_key),			\
	.value_size  = sizeof(type_val),			\
	.max_entries = _max_entries,				\
};								\
struct ____btf_map_##name {					\
	type_key key;						\
	type_val value;                                 	\
};								\
struct ____btf_map_##name __attribute__((section(".maps." #name), used)) \
	____btf_map_##name = { }

/*
 * FIXME: this should receive .max_entries as a parameter, as careful
 *	  tuning of these limits is needed to avoid hitting limits that
 *	  prevents other BPF constructs, such as tracepoint handlers,
 *	  to get installed, with cryptic messages from libbpf, etc.
 *	  For the current need, 'perf trace --filter-pids', 64 should
 *	  be good enough, but this surely needs to be revisited.
 */
#define pid_map(name, value_type) bpf_map(name, HASH, pid_t, value_type, 64)

static int (*bpf_map_update_elem)(struct bpf_map *map, void *key, void *value, u64 flags) = (void *)BPF_FUNC_map_update_elem;
static void *(*bpf_map_lookup_elem)(struct bpf_map *map, void *key) = (void *)BPF_FUNC_map_lookup_elem;

static void (*bpf_tail_call)(void *ctx, void *map, int index) = (void *)BPF_FUNC_tail_call;

#define SEC(NAME) __attribute__((section(NAME),  used))

#define probe(function, vars) \
	SEC(#function "=" #function " " #vars) function

#define syscall_enter(name) \
	SEC("syscalls:sys_enter_" #name) syscall_enter_ ## name

#define syscall_exit(name) \
	SEC("syscalls:sys_exit_" #name) syscall_exit_ ## name

#define license(name) \
char _license[] SEC("license") = #name; \
int _version SEC("version") = LINUX_VERSION_CODE;

static int (*probe_read)(void *dst, int size, const void *unsafe_addr) = (void *)BPF_FUNC_probe_read;
static int (*probe_read_str)(void *dst, int size, const void *unsafe_addr) = (void *)BPF_FUNC_probe_read_str;

static int (*perf_event_output)(void *, struct bpf_map *, int, void *, unsigned long) = (void *)BPF_FUNC_perf_event_output;

#endif /* _PERF_BPF_H */
