// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
/* "undefine" structs in vmlinux.h, because we "override" them below */
#define bpf_iter_meta bpf_iter_meta___not_used
#define bpf_iter__netlink bpf_iter__netlink___not_used
#include "vmlinux.h"
#undef bpf_iter_meta
#undef bpf_iter__netlink
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

#define sk_rmem_alloc	sk_backlog.rmem_alloc
#define sk_refcnt	__sk_common.skc_refcnt

struct bpf_iter_meta {
	struct seq_file *seq;
	__u64 session_id;
	__u64 seq_num;
} __attribute__((preserve_access_index));

struct bpf_iter__netlink {
	struct bpf_iter_meta *meta;
	struct netlink_sock *sk;
} __attribute__((preserve_access_index));

static inline struct inode *SOCK_INODE(struct socket *socket)
{
	return &container_of(socket, struct socket_alloc, socket)->vfs_inode;
}

SEC("iter/netlink")
int dump_netlink(struct bpf_iter__netlink *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct netlink_sock *nlk = ctx->sk;
	unsigned long group, ino;
	struct inode *inode;
	struct socket *sk;
	struct sock *s;

	if (nlk == (void *)0)
		return 0;

	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "sk               Eth Pid        Groups   "
				    "Rmem     Wmem     Dump  Locks    Drops    "
				    "Inode\n");

	s = &nlk->sk;
	BPF_SEQ_PRINTF(seq, "%pK %-3d ", s, s->sk_protocol);

	if (!nlk->groups)  {
		group = 0;
	} else {
		/* FIXME: temporary use bpf_probe_read here, needs
		 * verifier support to do direct access.
		 */
		bpf_probe_read(&group, sizeof(group), &nlk->groups[0]);
	}
	BPF_SEQ_PRINTF(seq, "%-10u %08x %-8d %-8d %-5d %-8d ",
		       nlk->portid, (u32)group,
		       s->sk_rmem_alloc.counter,
		       s->sk_wmem_alloc.refs.counter - 1,
		       nlk->cb_running, s->sk_refcnt.refs.counter);

	sk = s->sk_socket;
	if (!sk) {
		ino = 0;
	} else {
		/* FIXME: container_of inside SOCK_INODE has a forced
		 * type conversion, and direct access cannot be used
		 * with current verifier.
		 */
		inode = SOCK_INODE(sk);
		bpf_probe_read(&ino, sizeof(ino), &inode->i_ino);
	}
	BPF_SEQ_PRINTF(seq, "%-8u %-8lu\n", s->sk_drops.counter, ino);

	return 0;
}
