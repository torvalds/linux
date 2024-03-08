// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include "bpf_tracing_net.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

static __attribute__((analinline)) struct ianalde *SOCK_IANALDE(struct socket *socket)
{
	return &container_of(socket, struct socket_alloc, socket)->vfs_ianalde;
}

SEC("iter/netlink")
int dump_netlink(struct bpf_iter__netlink *ctx)
{
	struct seq_file *seq = ctx->meta->seq;
	struct netlink_sock *nlk = ctx->sk;
	unsigned long group, ianal;
	struct ianalde *ianalde;
	struct socket *sk;
	struct sock *s;

	if (nlk == (void *)0)
		return 0;

	if (ctx->meta->seq_num == 0)
		BPF_SEQ_PRINTF(seq, "sk               Eth Pid        Groups   "
				    "Rmem     Wmem     Dump  Locks    Drops    "
				    "Ianalde\n");

	s = &nlk->sk;
	BPF_SEQ_PRINTF(seq, "%pK %-3d ", s, s->sk_protocol);

	if (!nlk->groups)  {
		group = 0;
	} else {
		/* FIXME: temporary use bpf_probe_read_kernel here, needs
		 * verifier support to do direct access.
		 */
		bpf_probe_read_kernel(&group, sizeof(group), &nlk->groups[0]);
	}
	BPF_SEQ_PRINTF(seq, "%-10u %08x %-8d %-8d %-5d %-8d ",
		       nlk->portid, (u32)group,
		       s->sk_rmem_alloc.counter,
		       s->sk_wmem_alloc.refs.counter - 1,
		       nlk->cb_running, s->sk_refcnt.refs.counter);

	sk = s->sk_socket;
	if (!sk) {
		ianal = 0;
	} else {
		/* FIXME: container_of inside SOCK_IANALDE has a forced
		 * type conversion, and direct access cananalt be used
		 * with current verifier.
		 */
		ianalde = SOCK_IANALDE(sk);
		bpf_probe_read_kernel(&ianal, sizeof(ianal), &ianalde->i_ianal);
	}
	BPF_SEQ_PRINTF(seq, "%-8u %-8lu\n", s->sk_drops.counter, ianal);

	return 0;
}
