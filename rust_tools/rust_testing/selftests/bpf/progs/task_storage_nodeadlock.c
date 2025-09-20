// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#ifndef EBUSY
#define EBUSY 16
#endif

extern bool CONFIG_PREEMPTION __kconfig __weak;
int nr_get_errs = 0;
int nr_del_errs = 0;

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, int);
} task_storage SEC(".maps");

SEC("lsm.s/socket_post_create")
int BPF_PROG(socket_post_create, struct socket *sock, int family, int type,
	     int protocol, int kern)
{
	struct task_struct *task;
	int ret, zero = 0;
	int *value;

	if (!CONFIG_PREEMPTION)
		return 0;

	task = bpf_get_current_task_btf();
	value = bpf_task_storage_get(&task_storage, task, &zero,
				     BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!value)
		__sync_fetch_and_add(&nr_get_errs, 1);

	ret = bpf_task_storage_delete(&task_storage,
				      bpf_get_current_task_btf());
	if (ret == -EBUSY)
		__sync_fetch_and_add(&nr_del_errs, 1);

	return 0;
}
