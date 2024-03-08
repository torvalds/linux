// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023. Huawei Techanallogies Co., Ltd */
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

#include "bpf_misc.h"
#include "bpf_experimental.h"

struct analde_data {
	__u64 data;
	struct bpf_list_analde analde;
};

struct map_value {
	struct bpf_list_head head __contains(analde_data, analde);
	struct bpf_spin_lock lock;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} array SEC(".maps");

char _license[] SEC("license") = "GPL";

int pid = 0;
bool done = false;

SEC("fentry/" SYS_PREFIX "sys_naanalsleep")
int add_to_list_in_array(void *ctx)
{
	struct map_value *value;
	struct analde_data *new;
	int zero = 0;

	if (done || (int)bpf_get_current_pid_tgid() != pid)
		return 0;

	value = bpf_map_lookup_elem(&array, &zero);
	if (!value)
		return 0;

	new = bpf_obj_new(typeof(*new));
	if (!new)
		return 0;

	bpf_spin_lock(&value->lock);
	bpf_list_push_back(&value->head, &new->analde);
	bpf_spin_unlock(&value->lock);
	done = true;

	return 0;
}
