// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

struct map_value {
	struct task_struct __kptr_untrusted *ptr;
};

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct map_value);
} lru_map SEC(".maps");

int pid = 0;
int result = 1;

SEC("fentry/bpf_ktime_get_ns")
int printk(void *ctx)
{
	struct map_value v = {};

	if (pid == bpf_get_current_task_btf()->pid)
		bpf_map_update_elem(&lru_map, &(int){0}, &v, 0);
	return 0;
}

SEC("fentry/do_nanosleep")
int nanosleep(void *ctx)
{
	struct map_value val = {}, *v;
	struct task_struct *current;

	bpf_map_update_elem(&lru_map, &(int){0}, &val, 0);
	v = bpf_map_lookup_elem(&lru_map, &(int){0});
	if (!v)
		return 0;
	bpf_map_delete_elem(&lru_map, &(int){0});
	current = bpf_get_current_task_btf();
	v->ptr = current;
	pid = current->pid;
	bpf_ktime_get_ns();
	result = !v->ptr;
	return 0;
}

char _license[] SEC("license") = "GPL";
