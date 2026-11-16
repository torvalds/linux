// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

/* In freplace_args.c 'larger' has 32 bytes and 'smaller' has 8 bytes. */
struct buf { __u64 v[2]; };
struct larger { __u64 v[2]; };
struct smaller { __u64 v[2]; };
struct tp_ctx { __u64 v[2]; };

__weak int mem(int a, struct buf *p)
{
	return 0;
}

__weak int mem_nonnull(int a, struct buf *p __arg_nonnull)
{
	return 0;
}

__weak int mem_larger(struct larger *p)
{
	return 0;
}

__weak int mem_smaller(struct smaller *p)
{
	return 0;
}

/* No tag: memory of the size of the struct. */
__weak int task_mem(struct task_struct *t)
{
	return 0;
}

__weak int task_trusted(struct task_struct *t __arg_trusted)
{
	return 0;
}

__weak int skb_ctx(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int tc_prog(struct __sk_buff *skb)
{
	struct smaller smaller = {};
	struct larger larger = {};
	struct buf buf = {};

	return mem(0, &buf) + mem_nonnull(0, &buf) + mem_larger(&larger) + mem_smaller(&smaller) +
	       task_mem(NULL) + task_trusted(bpf_get_current_task_btf()) + skb_ctx(skb);
}

/* In a tracepoint prog the tag makes a context of any struct. */
__weak int tagged_ctx(struct tp_ctx *ctx __arg_ctx)
{
	return 0;
}

SEC("tracepoint")
int tp_prog(void *ctx)
{
	return tagged_ctx(ctx);
}

char _license[] SEC("license") = "GPL";
