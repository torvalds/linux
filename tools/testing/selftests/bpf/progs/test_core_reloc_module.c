// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct bpf_testmod_test_read_ctx {
	/* field order is mixed up */
	size_t len;
	char *buf;
	loff_t off;
} __attribute__((preserve_access_index));

struct {
	char in[256];
	char out[256];
	bool skip;
	uint64_t my_pid_tgid;
} data = {};

struct core_reloc_module_output {
	long long len;
	long long off;
	int read_ctx_sz;
	bool read_ctx_exists;
	bool buf_exists;
	bool len_exists;
	bool off_exists;
	/* we have test_progs[-flavor], so cut flavor part */
	char comm[sizeof("test_progs")];
	int comm_len;
};

SEC("raw_tp/bpf_testmod_test_read")
int BPF_PROG(test_core_module_probed,
	     struct task_struct *task,
	     struct bpf_testmod_test_read_ctx *read_ctx)
{
#if __has_builtin(__builtin_preserve_enum_value)
	struct core_reloc_module_output *out = (void *)&data.out;
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	__u32 real_tgid = (__u32)(pid_tgid >> 32);
	__u32 real_pid = (__u32)pid_tgid;

	if (data.my_pid_tgid != pid_tgid)
		return 0;

	if (BPF_CORE_READ(task, pid) != real_pid || BPF_CORE_READ(task, tgid) != real_tgid)
		return 0;

	out->len = BPF_CORE_READ(read_ctx, len);
	out->off = BPF_CORE_READ(read_ctx, off);

	out->read_ctx_sz = bpf_core_type_size(struct bpf_testmod_test_read_ctx);
	out->read_ctx_exists = bpf_core_type_exists(struct bpf_testmod_test_read_ctx);
	out->buf_exists = bpf_core_field_exists(read_ctx->buf);
	out->off_exists = bpf_core_field_exists(read_ctx->off);
	out->len_exists = bpf_core_field_exists(read_ctx->len);

	out->comm_len = BPF_CORE_READ_STR_INTO(&out->comm, task, comm);
#else
	data.skip = true;
#endif

	return 0;
}

SEC("tp_btf/bpf_testmod_test_read")
int BPF_PROG(test_core_module_direct,
	     struct task_struct *task,
	     struct bpf_testmod_test_read_ctx *read_ctx)
{
#if __has_builtin(__builtin_preserve_enum_value)
	struct core_reloc_module_output *out = (void *)&data.out;
	__u64 pid_tgid = bpf_get_current_pid_tgid();
	__u32 real_tgid = (__u32)(pid_tgid >> 32);
	__u32 real_pid = (__u32)pid_tgid;

	if (data.my_pid_tgid != pid_tgid)
		return 0;

	if (task->pid != real_pid || task->tgid != real_tgid)
		return 0;

	out->len = read_ctx->len;
	out->off = read_ctx->off;

	out->read_ctx_sz = bpf_core_type_size(struct bpf_testmod_test_read_ctx);
	out->read_ctx_exists = bpf_core_type_exists(struct bpf_testmod_test_read_ctx);
	out->buf_exists = bpf_core_field_exists(read_ctx->buf);
	out->off_exists = bpf_core_field_exists(read_ctx->off);
	out->len_exists = bpf_core_field_exists(read_ctx->len);

	out->comm_len = BPF_CORE_READ_STR_INTO(&out->comm, task, comm);
#else
	data.skip = true;
#endif

	return 0;
}
