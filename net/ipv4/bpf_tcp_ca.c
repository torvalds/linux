// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */

#include <linux/types.h>
#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <net/tcp.h>
#include <net/bpf_sk_storage.h>

static u32 optional_ops[] = {
	offsetof(struct tcp_congestion_ops, init),
	offsetof(struct tcp_congestion_ops, release),
	offsetof(struct tcp_congestion_ops, set_state),
	offsetof(struct tcp_congestion_ops, cwnd_event),
	offsetof(struct tcp_congestion_ops, in_ack_event),
	offsetof(struct tcp_congestion_ops, pkts_acked),
	offsetof(struct tcp_congestion_ops, min_tso_segs),
	offsetof(struct tcp_congestion_ops, sndbuf_expand),
	offsetof(struct tcp_congestion_ops, cong_control),
};

static u32 unsupported_ops[] = {
	offsetof(struct tcp_congestion_ops, get_info),
};

static const struct btf_type *tcp_sock_type;
static u32 tcp_sock_id, sock_id;

static int bpf_tcp_ca_init(struct btf *btf)
{
	s32 type_id;

	type_id = btf_find_by_name_kind(btf, "sock", BTF_KIND_STRUCT);
	if (type_id < 0)
		return -EINVAL;
	sock_id = type_id;

	type_id = btf_find_by_name_kind(btf, "tcp_sock", BTF_KIND_STRUCT);
	if (type_id < 0)
		return -EINVAL;
	tcp_sock_id = type_id;
	tcp_sock_type = btf_type_by_id(btf, tcp_sock_id);

	return 0;
}

static bool is_optional(u32 member_offset)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(optional_ops); i++) {
		if (member_offset == optional_ops[i])
			return true;
	}

	return false;
}

static bool is_unsupported(u32 member_offset)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(unsupported_ops); i++) {
		if (member_offset == unsupported_ops[i])
			return true;
	}

	return false;
}

extern struct btf *btf_vmlinux;

static bool bpf_tcp_ca_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	if (off < 0 || off >= sizeof(__u64) * MAX_BPF_FUNC_ARGS)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;

	if (!btf_ctx_access(off, size, type, prog, info))
		return false;

	if (info->reg_type == PTR_TO_BTF_ID && info->btf_id == sock_id)
		/* promote it to tcp_sock */
		info->btf_id = tcp_sock_id;

	return true;
}

static int bpf_tcp_ca_btf_struct_access(struct bpf_verifier_log *log,
					const struct btf_type *t, int off,
					int size, enum bpf_access_type atype,
					u32 *next_btf_id)
{
	size_t end;

	if (atype == BPF_READ)
		return btf_struct_access(log, t, off, size, atype, next_btf_id);

	if (t != tcp_sock_type) {
		bpf_log(log, "only read is supported\n");
		return -EACCES;
	}

	switch (off) {
	case bpf_ctx_range(struct inet_connection_sock, icsk_ca_priv):
		end = offsetofend(struct inet_connection_sock, icsk_ca_priv);
		break;
	case offsetof(struct inet_connection_sock, icsk_ack.pending):
		end = offsetofend(struct inet_connection_sock,
				  icsk_ack.pending);
		break;
	case offsetof(struct tcp_sock, snd_cwnd):
		end = offsetofend(struct tcp_sock, snd_cwnd);
		break;
	case offsetof(struct tcp_sock, snd_cwnd_cnt):
		end = offsetofend(struct tcp_sock, snd_cwnd_cnt);
		break;
	case offsetof(struct tcp_sock, snd_ssthresh):
		end = offsetofend(struct tcp_sock, snd_ssthresh);
		break;
	case offsetof(struct tcp_sock, ecn_flags):
		end = offsetofend(struct tcp_sock, ecn_flags);
		break;
	default:
		bpf_log(log, "no write support to tcp_sock at off %d\n", off);
		return -EACCES;
	}

	if (off + size > end) {
		bpf_log(log,
			"write access at off %d with size %d beyond the member of tcp_sock ended at %zu\n",
			off, size, end);
		return -EACCES;
	}

	return NOT_INIT;
}

BPF_CALL_2(bpf_tcp_send_ack, struct tcp_sock *, tp, u32, rcv_nxt)
{
	/* bpf_tcp_ca prog cannot have NULL tp */
	__tcp_send_ack((struct sock *)tp, rcv_nxt);
	return 0;
}

static const struct bpf_func_proto bpf_tcp_send_ack_proto = {
	.func		= bpf_tcp_send_ack,
	.gpl_only	= false,
	/* In case we want to report error later */
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_BTF_ID,
	.arg1_btf_id	= &tcp_sock_id,
	.arg2_type	= ARG_ANYTHING,
};

static const struct bpf_func_proto *
bpf_tcp_ca_get_func_proto(enum bpf_func_id func_id,
			  const struct bpf_prog *prog)
{
	switch (func_id) {
	case BPF_FUNC_tcp_send_ack:
		return &bpf_tcp_send_ack_proto;
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
	default:
		return bpf_base_func_proto(func_id);
	}
}

static const struct bpf_verifier_ops bpf_tcp_ca_verifier_ops = {
	.get_func_proto		= bpf_tcp_ca_get_func_proto,
	.is_valid_access	= bpf_tcp_ca_is_valid_access,
	.btf_struct_access	= bpf_tcp_ca_btf_struct_access,
};

static int bpf_tcp_ca_init_member(const struct btf_type *t,
				  const struct btf_member *member,
				  void *kdata, const void *udata)
{
	const struct tcp_congestion_ops *utcp_ca;
	struct tcp_congestion_ops *tcp_ca;
	int prog_fd;
	u32 moff;

	utcp_ca = (const struct tcp_congestion_ops *)udata;
	tcp_ca = (struct tcp_congestion_ops *)kdata;

	moff = btf_member_bit_offset(t, member) / 8;
	switch (moff) {
	case offsetof(struct tcp_congestion_ops, flags):
		if (utcp_ca->flags & ~TCP_CONG_MASK)
			return -EINVAL;
		tcp_ca->flags = utcp_ca->flags;
		return 1;
	case offsetof(struct tcp_congestion_ops, name):
		if (bpf_obj_name_cpy(tcp_ca->name, utcp_ca->name,
				     sizeof(tcp_ca->name)) <= 0)
			return -EINVAL;
		if (tcp_ca_find(utcp_ca->name))
			return -EEXIST;
		return 1;
	}

	if (!btf_type_resolve_func_ptr(btf_vmlinux, member->type, NULL))
		return 0;

	/* Ensure bpf_prog is provided for compulsory func ptr */
	prog_fd = (int)(*(unsigned long *)(udata + moff));
	if (!prog_fd && !is_optional(moff) && !is_unsupported(moff))
		return -EINVAL;

	return 0;
}

static int bpf_tcp_ca_check_member(const struct btf_type *t,
				   const struct btf_member *member)
{
	if (is_unsupported(btf_member_bit_offset(t, member) / 8))
		return -ENOTSUPP;
	return 0;
}

static int bpf_tcp_ca_reg(void *kdata)
{
	return tcp_register_congestion_control(kdata);
}

static void bpf_tcp_ca_unreg(void *kdata)
{
	tcp_unregister_congestion_control(kdata);
}

/* Avoid sparse warning.  It is only used in bpf_struct_ops.c. */
extern struct bpf_struct_ops bpf_tcp_congestion_ops;

struct bpf_struct_ops bpf_tcp_congestion_ops = {
	.verifier_ops = &bpf_tcp_ca_verifier_ops,
	.reg = bpf_tcp_ca_reg,
	.unreg = bpf_tcp_ca_unreg,
	.check_member = bpf_tcp_ca_check_member,
	.init_member = bpf_tcp_ca_init_member,
	.init = bpf_tcp_ca_init,
	.name = "tcp_congestion_ops",
};
