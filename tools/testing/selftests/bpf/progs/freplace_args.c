// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

/* In freplace_args_tgt.c each of them has 16 bytes. */
struct buf { __u64 v[2]; };
struct larger { __u64 v[4]; };
struct smaller { __u64 v[1]; };
struct tp_ctx { __u64 v[2]; };

SEC("?freplace")
int ext_mem(int a, struct buf *p)
{
	return 0;
}

SEC("?freplace")
int ext_mem_nonnull(int a, struct buf *p __arg_nonnull)
{
	return 0;
}

SEC("?freplace")
int ext_mem_larger(struct larger *p)
{
	return 0;
}

SEC("?freplace")
int ext_mem_smaller(struct smaller *p)
{
	return 0;
}

SEC("?freplace")
int ext_task_trusted(struct task_struct *t __arg_trusted)
{
	return 0;
}

SEC("?freplace")
int ext_skb_ctx(struct __sk_buff *skb __arg_ctx)
{
	return 0;
}

SEC("?freplace")
int ext_tp_mem(struct tp_ctx *p)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
