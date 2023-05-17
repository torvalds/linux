// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */

#include <linux/bpf.h>
#define BPF_NO_GLOBAL_DATA
#include <bpf/bpf_helpers.h>

char LICENSE[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1);
} my_pid_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 1);
} res_map SEC(".maps");

volatile int my_pid_var = 0;
volatile int res_var = 0;

SEC("tp/raw_syscalls/sys_enter")
int handle_legacy(void *ctx)
{
	int zero = 0, *my_pid, cur_pid, *my_res;

	my_pid = bpf_map_lookup_elem(&my_pid_map, &zero);
	if (!my_pid)
		return 1;

	cur_pid = bpf_get_current_pid_tgid() >> 32;
	if (cur_pid != *my_pid)
		return 1;

	my_res = bpf_map_lookup_elem(&res_map, &zero);
	if (!my_res)
		return 1;

	if (*my_res == 0)
		/* use bpf_printk() in combination with BPF_NO_GLOBAL_DATA to
		 * force .rodata.str1.1 section that previously caused
		 * problems on old kernels due to libbpf always tried to
		 * create a global data map for it
		 */
		bpf_printk("Legacy-case bpf_printk test, pid %d\n", cur_pid);
	*my_res = 1;

	return *my_res;
}

SEC("tp/raw_syscalls/sys_enter")
int handle_modern(void *ctx)
{
	int cur_pid;

	cur_pid = bpf_get_current_pid_tgid() >> 32;
	if (cur_pid != my_pid_var)
		return 1;

	if (res_var == 0)
		/* we need bpf_printk() to validate libbpf logic around unused
		 * global maps and legacy kernels; see comment in handle_legacy()
		 */
		bpf_printk("Modern-case bpf_printk test, pid %d\n", cur_pid);
	res_var = 1;

	return res_var;
}
