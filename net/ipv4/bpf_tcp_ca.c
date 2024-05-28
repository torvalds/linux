// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook  */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <linux/filter.h>
#include <net/tcp.h>
#include <net/bpf_sk_storage.h>

/* "extern" is to avoid sparse warning.  It is only used in bpf_struct_ops.c. */
static struct bpf_struct_ops bpf_tcp_congestion_ops;

static u32 unsupported_ops[] = {
	offsetof(struct tcp_congestion_ops, get_info),
};

static const struct btf_type *tcp_sock_type;
static u32 tcp_sock_id, sock_id;
static const struct btf_type *tcp_congestion_ops_type;

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

	type_id = btf_find_by_name_kind(btf, "tcp_congestion_ops", BTF_KIND_STRUCT);
	if (type_id < 0)
		return -EINVAL;
	tcp_congestion_ops_type = btf_type_by_id(btf, type_id);

	return 0;
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

static bool bpf_tcp_ca_is_valid_access(int off, int size,
				       enum bpf_access_type type,
				       const struct bpf_prog *prog,
				       struct bpf_insn_access_aux *info)
{
	if (!bpf_tracing_btf_ctx_access(off, size, type, prog, info))
		return false;

	if (base_type(info->reg_type) == PTR_TO_BTF_ID &&
	    !bpf_type_has_unsafe_modifiers(info->reg_type) &&
	    info->btf_id == sock_id)
		/* promote it to tcp_sock */
		info->btf_id = tcp_sock_id;

	return true;
}

static int bpf_tcp_ca_btf_struct_access(struct bpf_verifier_log *log,
					const struct bpf_reg_state *reg,
					int off, int size)
{
	const struct btf_type *t;
	size_t end;

	t = btf_type_by_id(reg->btf, reg->btf_id);
	if (t != tcp_sock_type) {
		bpf_log(log, "only read is supported\n");
		return -EACCES;
	}

	switch (off) {
	case offsetof(struct sock, sk_pacing_rate):
		end = offsetofend(struct sock, sk_pacing_rate);
		break;
	case offsetof(struct sock, sk_pacing_status):
		end = offsetofend(struct sock, sk_pacing_status);
		break;
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
	case offsetof(struct tcp_sock, app_limited):
		end = offsetofend(struct tcp_sock, app_limited);
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

	return 0;
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

static u32 prog_ops_moff(const struct bpf_prog *prog)
{
	const struct btf_member *m;
	const struct btf_type *t;
	u32 midx;

	midx = prog->expected_attach_type;
	t = tcp_congestion_ops_type;
	m = &btf_type_member(t)[midx];

	return __btf_member_bit_offset(t, m) / 8;
}

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
	case BPF_FUNC_setsockopt:
		/* Does not allow release() to call setsockopt.
		 * release() is called when the current bpf-tcp-cc
		 * is retiring.  It is not allowed to call
		 * setsockopt() to make further changes which
		 * may potentially allocate new resources.
		 */
		if (prog_ops_moff(prog) !=
		    offsetof(struct tcp_congestion_ops, release))
			return &bpf_sk_setsockopt_proto;
		return NULL;
	case BPF_FUNC_getsockopt:
		/* Since get/setsockopt is usually expected to
		 * be available together, disable getsockopt for
		 * release also to avoid usage surprise.
		 * The bpf-tcp-cc already has a more powerful way
		 * to read tcp_sock from the PTR_TO_BTF_ID.
		 */
		if (prog_ops_moff(prog) !=
		    offsetof(struct tcp_congestion_ops, release))
			return &bpf_sk_getsockopt_proto;
		return NULL;
	case BPF_FUNC_ktime_get_coarse_ns:
		return &bpf_ktime_get_coarse_ns_proto;
	default:
		return bpf_base_func_proto(func_id, prog);
	}
}

BTF_KFUNCS_START(bpf_tcp_ca_check_kfunc_ids)
BTF_ID_FLAGS(func, tcp_reno_ssthresh)
BTF_ID_FLAGS(func, tcp_reno_cong_avoid)
BTF_ID_FLAGS(func, tcp_reno_undo_cwnd)
BTF_ID_FLAGS(func, tcp_slow_start)
BTF_ID_FLAGS(func, tcp_cong_avoid_ai)
BTF_KFUNCS_END(bpf_tcp_ca_check_kfunc_ids)

static const struct btf_kfunc_id_set bpf_tcp_ca_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &bpf_tcp_ca_check_kfunc_ids,
};

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
	u32 moff;

	utcp_ca = (const struct tcp_congestion_ops *)udata;
	tcp_ca = (struct tcp_congestion_ops *)kdata;

	moff = __btf_member_bit_offset(t, member) / 8;
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
		return 1;
	}

	return 0;
}

static int bpf_tcp_ca_check_member(const struct btf_type *t,
				   const struct btf_member *member,
				   const struct bpf_prog *prog)
{
	if (is_unsupported(__btf_member_bit_offset(t, member) / 8))
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

static int bpf_tcp_ca_update(void *kdata, void *old_kdata)
{
	return tcp_update_congestion_control(kdata, old_kdata);
}

static int bpf_tcp_ca_validate(void *kdata)
{
	return tcp_validate_congestion_control(kdata);
}

static u32 bpf_tcp_ca_ssthresh(struct sock *sk)
{
	return 0;
}

static void bpf_tcp_ca_cong_avoid(struct sock *sk, u32 ack, u32 acked)
{
}

static void bpf_tcp_ca_set_state(struct sock *sk, u8 new_state)
{
}

static void bpf_tcp_ca_cwnd_event(struct sock *sk, enum tcp_ca_event ev)
{
}

static void bpf_tcp_ca_in_ack_event(struct sock *sk, u32 flags)
{
}

static void bpf_tcp_ca_pkts_acked(struct sock *sk, const struct ack_sample *sample)
{
}

static u32 bpf_tcp_ca_min_tso_segs(struct sock *sk)
{
	return 0;
}

static void bpf_tcp_ca_cong_control(struct sock *sk, const struct rate_sample *rs)
{
}

static u32 bpf_tcp_ca_undo_cwnd(struct sock *sk)
{
	return 0;
}

static u32 bpf_tcp_ca_sndbuf_expand(struct sock *sk)
{
	return 0;
}

static void __bpf_tcp_ca_init(struct sock *sk)
{
}

static void __bpf_tcp_ca_release(struct sock *sk)
{
}

static struct tcp_congestion_ops __bpf_ops_tcp_congestion_ops = {
	.ssthresh = bpf_tcp_ca_ssthresh,
	.cong_avoid = bpf_tcp_ca_cong_avoid,
	.set_state = bpf_tcp_ca_set_state,
	.cwnd_event = bpf_tcp_ca_cwnd_event,
	.in_ack_event = bpf_tcp_ca_in_ack_event,
	.pkts_acked = bpf_tcp_ca_pkts_acked,
	.min_tso_segs = bpf_tcp_ca_min_tso_segs,
	.cong_control = bpf_tcp_ca_cong_control,
	.undo_cwnd = bpf_tcp_ca_undo_cwnd,
	.sndbuf_expand = bpf_tcp_ca_sndbuf_expand,

	.init = __bpf_tcp_ca_init,
	.release = __bpf_tcp_ca_release,
};

static struct bpf_struct_ops bpf_tcp_congestion_ops = {
	.verifier_ops = &bpf_tcp_ca_verifier_ops,
	.reg = bpf_tcp_ca_reg,
	.unreg = bpf_tcp_ca_unreg,
	.update = bpf_tcp_ca_update,
	.check_member = bpf_tcp_ca_check_member,
	.init_member = bpf_tcp_ca_init_member,
	.init = bpf_tcp_ca_init,
	.validate = bpf_tcp_ca_validate,
	.name = "tcp_congestion_ops",
	.cfi_stubs = &__bpf_ops_tcp_congestion_ops,
	.owner = THIS_MODULE,
};

static int __init bpf_tcp_ca_kfunc_init(void)
{
	int ret;

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &bpf_tcp_ca_kfunc_set);
	ret = ret ?: register_bpf_struct_ops(&bpf_tcp_congestion_ops, tcp_congestion_ops);

	return ret;
}
late_initcall(bpf_tcp_ca_kfunc_init);
