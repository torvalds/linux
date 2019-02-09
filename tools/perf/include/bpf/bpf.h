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

#define SEC(NAME) __attribute__((section(NAME),  used))

#define probe(function, vars) \
	SEC(#function "=" #function " " #vars) function

#define syscall_enter(name) \
	SEC("syscalls:sys_enter_" #name) syscall_enter_ ## name

#define license(name) \
char _license[] SEC("license") = #name; \
int _version SEC("version") = LINUX_VERSION_CODE;

static int (*probe_read)(void *dst, int size, const void *unsafe_addr) = (void *)BPF_FUNC_probe_read;
static int (*probe_read_str)(void *dst, int size, const void *unsafe_addr) = (void *)BPF_FUNC_probe_read_str;

#endif /* _PERF_BPF_H */
