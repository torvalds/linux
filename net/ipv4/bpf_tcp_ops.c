// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <linux/bpf_verifier.h>
#include <linux/filter.h>
#include <net/bpf_sk_storage.h>
#include <net/tcp.h>

static int timeout_init_stub(struct sock *sk, struct request_sock *req__nullable)
{
	struct bpf_tramp_run_ctx *ctx =
		container_of(current->bpf_ctx, struct bpf_tramp_run_ctx, run_ctx);

	return ctx->retval;
}

static int rwnd_init_stub(struct sock *sk, struct request_sock *req__nullable)
{
	struct bpf_tramp_run_ctx *ctx =
		container_of(current->bpf_ctx, struct bpf_tramp_run_ctx, run_ctx);

	return ctx->retval;
}

static void active_established_stub(struct sock *sk, struct sk_buff *skb__nullable)
{
}

static void passive_established_stub(struct sock *sk, struct sk_buff *skb)
{
}

static void rto_stub(struct sock *sk)
{
}

static void rtt_stub(struct sock *sk, long mrtt, u32 srtt)
{
}

static void set_state_stub(struct sock *sk, int state)
{
}

static void retrans_stub(struct sock *sk, struct sk_buff *skb, int err)
{
}

static void connect_stub(struct sock *sk)
{
}

static void listen_stub(struct sock *sk)
{
}

static void parse_hdr_stub(struct sock *sk, struct sk_buff *skb)
{
}

static void hdr_opt_len_stub(struct sock *sk, struct sk_buff *skb__nullable,
			     struct request_sock *req__nullable,
			     struct sk_buff *syn_skb__nullable,
			     enum tcp_synack_type synack_type,
			     unsigned int *remaining)
{
}

static void write_hdr_opt_stub(struct sock *sk, struct sk_buff *skb,
			       struct request_sock *req__nullable,
			       struct sk_buff *syn_skb__nullable,
			       enum tcp_synack_type synack_type,
			       u32 opt_off)
{
}

static void enqueue_rcvq_stub(struct sock *sk, struct sk_buff *skb)
{
}

static void dequeue_rcvq_stub(struct sock *sk)
{
}

static struct bpf_tcp_ops __bpf_tcp_ops = {
	.timeout_init = timeout_init_stub,
	.rwnd_init = rwnd_init_stub,
	.active_established = active_established_stub,
	.passive_established = passive_established_stub,
	.rto = rto_stub,
	.rtt = rtt_stub,
	.set_state = set_state_stub,
	.retrans = retrans_stub,
	.connect = connect_stub,
	.listen = listen_stub,
	.parse_hdr = parse_hdr_stub,
	.hdr_opt_len = hdr_opt_len_stub,
	.write_hdr_opt = write_hdr_opt_stub,
	.enqueue_rcvq = enqueue_rcvq_stub,
	.dequeue_rcvq = dequeue_rcvq_stub,
};

BPF_CALL_4(bpf_tcp_ops_store_hdr_opt, void *, ctx, const void *, from,
	   u32, len, u64, flags)
{
	u64 *args = ctx;
	struct sk_buff *skb = (void *)(unsigned long)args[1];
	struct bpf_sock_ops_kern sock_ops = {};
	u32 opt_off = args[5];
	u8 *op, *opend;

	/*
	 * bpf_tcp_ops does not keep track of the end of the written TCP header
	 * options, so search for it every time the helper is called. The free
	 * space is NOP-filled, so a TCPOPT_NOP ends the search rather than being
	 * skipped as in a normal option walk in sockops.
	 */
	op = skb->data + opt_off;
	opend = skb->data + tcp_hdrlen(skb);
	while (op < opend && *op != TCPOPT_NOP) {
		if (*op == TCPOPT_EOL || op + 1 >= opend || op[1] < 2)
			break;
		op += op[1];
	}

	sock_ops.skb = skb;
	sock_ops.skb_data_end = op;
	sock_ops.remaining_opt_len = opend - op;

	return __bpf_sock_ops_store_hdr_opt(&sock_ops, from, len, flags);
}

static const struct bpf_func_proto bpf_tcp_ops_store_hdr_opt_proto = {
	.func		= bpf_tcp_ops_store_hdr_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM | MEM_RDONLY,
	.arg3_type	= ARG_MEM_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_4(bpf_tcp_ops_load_hdr_opt, void *, ctx, void *, search_res,
	   u32, len, u64, flags)
{
	u64 *args = ctx;
	struct sk_buff *skb = (void *)(unsigned long)args[1];
	struct bpf_sock_ops_kern sock_ops = {};

	/*
	 * No flags supported. In particular BPF_LOAD_HDR_OPT_TCP_SYN, which
	 * loads from the saved SYN, is not available because bpf_tcp_ops has no
	 * carrier to track the SYN source across the hooks.
	 */
	if (flags)
		return -EINVAL;

	sock_ops.skb = skb;
	sock_ops.skb_data_end = skb->data + tcp_hdrlen(skb);

	return __bpf_sock_ops_load_hdr_opt(&sock_ops, search_res, len, flags);
}

static const struct bpf_func_proto bpf_tcp_ops_load_hdr_opt_proto = {
	.func		= bpf_tcp_ops_load_hdr_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_PTR_TO_MEM | MEM_WRITE,
	.arg3_type	= ARG_MEM_SIZE,
	.arg4_type	= ARG_ANYTHING,
};

BPF_CALL_3(bpf_tcp_ops_reserve_hdr_opt, void *, ctx, u32, len, u64, flags)
{
	u64 *args = ctx;
	unsigned int *remaining = (void *)(unsigned long)args[5];

	if (flags || len < 2)
		return -EINVAL;

	if (len > *remaining)
		return -ENOSPC;

	*remaining -= len;
	return 0;
}

static const struct bpf_func_proto bpf_tcp_ops_reserve_hdr_opt_proto = {
	.func		= bpf_tcp_ops_reserve_hdr_opt,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
	.arg1_type	= ARG_PTR_TO_CTX,
	.arg2_type	= ARG_ANYTHING,
	.arg3_type	= ARG_ANYTHING,
};

BPF_CALL_0(bpf_tcp_ops_get_retval)
{
	struct bpf_tramp_run_ctx *ctx =
		container_of(current->bpf_ctx, struct bpf_tramp_run_ctx, run_ctx);

	/* bpf_get_retval() is only exposed to timeout_init/rwnd_init, which
	 * always run via bpf_tcp_ops_call_int(). Its run_ctx carries the int
	 * return value chained across the bpf_tcp_ops attached to the cgroup
	 * and is this program's saved_run_ctx.
	 */
	if (WARN_ON_ONCE(!ctx->saved_run_ctx))
		return 0;

	return container_of(ctx->saved_run_ctx, struct bpf_tramp_run_ctx,
			    run_ctx)->retval;
}

const struct bpf_func_proto bpf_tcp_ops_get_retval_proto = {
	.func		= bpf_tcp_ops_get_retval,
	.gpl_only	= false,
	.ret_type	= RET_INTEGER,
};

static bool is_sockopt_supported(u32 moff)
{
	switch (moff) {
	case offsetof(struct bpf_tcp_ops, active_established):
	case offsetof(struct bpf_tcp_ops, passive_established):
	case offsetof(struct bpf_tcp_ops, rto):
	case offsetof(struct bpf_tcp_ops, rtt):
	case offsetof(struct bpf_tcp_ops, set_state):
	case offsetof(struct bpf_tcp_ops, retrans):
	case offsetof(struct bpf_tcp_ops, connect):
	case offsetof(struct bpf_tcp_ops, listen):
	case offsetof(struct bpf_tcp_ops, parse_hdr):
		return true;
	}

	return false;
}

static const struct bpf_func_proto *
get_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	u32 moff = prog->aux->attach_st_ops_member_off;

	switch (func_id) {
	case BPF_FUNC_sk_storage_get:
		return &bpf_sk_storage_get_proto;
	case BPF_FUNC_sk_storage_delete:
		return &bpf_sk_storage_delete_proto;
	case BPF_FUNC_setsockopt:
		if (is_sockopt_supported(moff))
			return &bpf_sk_setsockopt_proto;
		return NULL;
	case BPF_FUNC_getsockopt:
		if (is_sockopt_supported(moff))
			return &bpf_sk_getsockopt_proto;
		return NULL;
	case BPF_FUNC_get_retval:
		if (moff == offsetof(struct bpf_tcp_ops, timeout_init) ||
		    moff == offsetof(struct bpf_tcp_ops, rwnd_init))
			return &bpf_tcp_ops_get_retval_proto;
		return NULL;
	case BPF_FUNC_reserve_hdr_opt:
		if (moff == offsetof(struct bpf_tcp_ops, hdr_opt_len))
			return &bpf_tcp_ops_reserve_hdr_opt_proto;
		return NULL;
	case BPF_FUNC_load_hdr_opt:
		if (moff == offsetof(struct bpf_tcp_ops, parse_hdr) ||
		    moff == offsetof(struct bpf_tcp_ops, write_hdr_opt))
			return &bpf_tcp_ops_load_hdr_opt_proto;
		return NULL;
	case BPF_FUNC_store_hdr_opt:
		if (moff == offsetof(struct bpf_tcp_ops, write_hdr_opt))
			return &bpf_tcp_ops_store_hdr_opt_proto;
		return NULL;
	default:
		return bpf_base_func_proto(func_id, prog);
	}
}

static bool is_valid_access(int off, int size, enum bpf_access_type type,
			    const struct bpf_prog *prog, struct bpf_insn_access_aux *info)
{
	if (!bpf_tracing_btf_ctx_access(off, size, type, prog, info))
		return false;

	if (base_type(info->reg_type) == PTR_TO_BTF_ID &&
	    !bpf_type_has_unsafe_modifiers(info->reg_type) &&
	    info->btf_id == btf_sock_ids[BTF_SOCK_TYPE_SOCK])
		/* promote it to tcp_sock */
		info->btf_id = btf_sock_ids[BTF_SOCK_TYPE_TCP];

	return true;
}

static int bpf_tcp_ops_init_member(const struct btf_type *t,
				   const struct btf_member *member,
				   void *kdata, const void *udata)
{
	return 0;
}

static int bpf_tcp_ops_check_member(const struct btf_type *t,
				    const struct btf_member *member,
				    const struct bpf_prog *prog)
{
	if (prog->sleepable)
		return -EINVAL;

	return 0;
}

static int bpf_tcp_ops_init(struct btf *btf)
{
	return 0;
}

static int bpf_tcp_ops_validate(void *kdata)
{
	return 0;
}

static const struct bpf_verifier_ops bpf_tcp_ops_verifier = {
	.get_func_proto		= get_func_proto,
	.is_valid_access	= is_valid_access,
};

static struct bpf_struct_ops bpf_tcp_ops = {
	.verifier_ops = &bpf_tcp_ops_verifier,
	.init_member = bpf_tcp_ops_init_member,
	.check_member = bpf_tcp_ops_check_member,
	.init = bpf_tcp_ops_init,
	.validate = bpf_tcp_ops_validate,
	.name = "bpf_tcp_ops",
	.cgroup_atype = CGROUP_TCP_SOCK_OPS,
	.cfi_stubs = &__bpf_tcp_ops,
	.owner = THIS_MODULE,
};

__bpf_kfunc_start_defs();

__bpf_kfunc int bpf_tcp_ops_set_flags(struct tcp_sock *tp, u32 enable, u32 disable)
{
	u32 old, new;

	if ((enable & disable) || (enable | disable) & ~BPF_TCP_OPS_FLAG_ALL)
		return -EINVAL;

	if (sk_is_mptcp((struct sock *)tp) && (enable & BPF_TCP_OPS_FLAG_RCVQ))
		return -EOPNOTSUPP;

	old = READ_ONCE(tp->bpf_tcp_ops_flags);

	do {
		new = (old | enable) & ~disable;
		if (new == old)
			break;
	} while (!try_cmpxchg(&tp->bpf_tcp_ops_flags, &old, new));

	return 0;
}

__bpf_kfunc int bpf_tcp_ops_set_rcvlowat(struct sock *sk, int rcvlowat,
					 const struct bpf_prog_aux *aux)
{
	u32 moff = aux->attach_st_ops_member_off;
	bool wakeup = false;

	if (moff == offsetof(struct bpf_tcp_ops, dequeue_rcvq))
		wakeup = true;

	if (rcvlowat < 0)
		rcvlowat = INT_MAX;

	return __tcp_set_rcvlowat(sk, rcvlowat, wakeup);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(bpf_tcp_ops_set_flags_kfunc_set)
BTF_ID_FLAGS(func, bpf_tcp_ops_set_flags)
BTF_KFUNCS_END(bpf_tcp_ops_set_flags_kfunc_set)

BTF_KFUNCS_START(bpf_tcp_ops_set_rcvlowat_kfunc_set)
BTF_ID_FLAGS(func, bpf_tcp_ops_set_rcvlowat, KF_IMPLICIT_ARGS)
BTF_KFUNCS_END(bpf_tcp_ops_set_rcvlowat_kfunc_set)

static int bpf_tcp_ops_set_flags_kfunc_filter(const struct bpf_prog *prog,
					      u32 kfunc_id)
{
	if (!btf_id_set8_contains(&bpf_tcp_ops_set_flags_kfunc_set, kfunc_id))
		return 0;

	if (prog->type == BPF_PROG_TYPE_STRUCT_OPS &&
	    prog->aux->st_ops == &bpf_tcp_ops)
		return 0;

	if (prog->type == BPF_PROG_TYPE_CGROUP_SOCKOPT ||
	    prog->type == BPF_PROG_TYPE_CGROUP_SKB)
		return 0;

	return -EACCES;
}

static const struct btf_kfunc_id_set bpf_tcp_ops_set_flags_kfunc_id_set = {
	.owner = THIS_MODULE,
	.set = &bpf_tcp_ops_set_flags_kfunc_set,
	.filter = bpf_tcp_ops_set_flags_kfunc_filter,
};

static int bpf_tcp_ops_set_rcvlowat_kfunc_filter(const struct bpf_prog *prog,
						 u32 kfunc_id)
{
	u32 moff;

	if (!btf_id_set8_contains(&bpf_tcp_ops_set_rcvlowat_kfunc_set, kfunc_id))
		return 0;

	if (prog->aux->st_ops != &bpf_tcp_ops)
		return -EACCES;

	moff = prog->aux->attach_st_ops_member_off;
	if (moff != offsetof(struct bpf_tcp_ops, enqueue_rcvq) &&
	    moff != offsetof(struct bpf_tcp_ops, dequeue_rcvq))
		return -EACCES;

	return 0;
}

static const struct btf_kfunc_id_set bpf_tcp_ops_set_rcvlowat_kfunc_id_set = {
	.owner = THIS_MODULE,
	.set = &bpf_tcp_ops_set_rcvlowat_kfunc_set,
	.filter = bpf_tcp_ops_set_rcvlowat_kfunc_filter,
};

static int __init __bpf_tcp_ops_init(void)
{
	int ret;

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					&bpf_tcp_ops_set_flags_kfunc_id_set);
	/* BPF_PROG_TYPE_CGROUP_{SOCKOPT,SKB} share BTF_KFUNC_HOOK_CGROUP. */
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_CGROUP_SOCKOPT,
					       &bpf_tcp_ops_set_flags_kfunc_id_set);
	ret = ret ?: register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS,
					       &bpf_tcp_ops_set_rcvlowat_kfunc_id_set);
	ret = ret ?: register_bpf_struct_ops(&bpf_tcp_ops, bpf_tcp_ops);

	return ret;
}

late_initcall(__bpf_tcp_ops_init);
