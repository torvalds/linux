// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/filter.h>
#include <net/pkt_sched.h>
#include <net/pkt_cls.h>

#define QDISC_OP_IDX(op)	(offsetof(struct Qdisc_ops, op) / sizeof(void (*)(void)))
#define QDISC_MOFF_IDX(moff)	(moff / sizeof(void (*)(void)))

static struct bpf_struct_ops bpf_Qdisc_ops;

struct bpf_sk_buff_ptr {
	struct sk_buff *skb;
};

static int bpf_qdisc_init(struct btf *btf)
{
	return 0;
}

BTF_ID_LIST_SINGLE(bpf_qdisc_ids, struct, Qdisc)
BTF_ID_LIST_SINGLE(bpf_sk_buff_ids, struct, sk_buff)
BTF_ID_LIST_SINGLE(bpf_sk_buff_ptr_ids, struct, bpf_sk_buff_ptr)

static bool bpf_qdisc_is_valid_access(int off, int size,
				      enum bpf_access_type type,
				      const struct bpf_prog *prog,
				      struct bpf_insn_access_aux *info)
{
	struct btf *btf = prog->aux->attach_btf;
	u32 arg;

	arg = btf_ctx_arg_idx(btf, prog->aux->attach_func_proto, off);
	if (prog->aux->attach_st_ops_member_off == offsetof(struct Qdisc_ops, enqueue)) {
		if (arg == 2 && type == BPF_READ) {
			info->reg_type = PTR_TO_BTF_ID | PTR_TRUSTED;
			info->btf = btf;
			info->btf_id = bpf_sk_buff_ptr_ids[0];
			return true;
		}
	}

	return bpf_tracing_btf_ctx_access(off, size, type, prog, info);
}

static int bpf_qdisc_qdisc_access(struct bpf_verifier_log *log,
				  const struct bpf_reg_state *reg,
				  int off, size_t *end)
{
	switch (off) {
	case offsetof(struct Qdisc, limit):
		*end = offsetofend(struct Qdisc, limit);
		break;
	case offsetof(struct Qdisc, q) + offsetof(struct qdisc_skb_head, qlen):
		*end = offsetof(struct Qdisc, q) + offsetofend(struct qdisc_skb_head, qlen);
		break;
	case offsetof(struct Qdisc, qstats) ... offsetofend(struct Qdisc, qstats) - 1:
		*end = offsetofend(struct Qdisc, qstats);
		break;
	default:
		return -EACCES;
	}

	return 0;
}

static int bpf_qdisc_sk_buff_access(struct bpf_verifier_log *log,
				    const struct bpf_reg_state *reg,
				    int off, size_t *end)
{
	switch (off) {
	case offsetof(struct sk_buff, tstamp):
		*end = offsetofend(struct sk_buff, tstamp);
		break;
	case offsetof(struct sk_buff, cb) + offsetof(struct qdisc_skb_cb, data[0]) ...
	     offsetof(struct sk_buff, cb) + offsetof(struct qdisc_skb_cb,
						     data[QDISC_CB_PRIV_LEN - 1]):
		*end = offsetof(struct sk_buff, cb) +
		       offsetofend(struct qdisc_skb_cb, data[QDISC_CB_PRIV_LEN - 1]);
		break;
	default:
		return -EACCES;
	}

	return 0;
}

static int bpf_qdisc_btf_struct_access(struct bpf_verifier_log *log,
				       const struct bpf_reg_state *reg,
				       int off, int size)
{
	const struct btf_type *t, *skbt, *qdisct;
	size_t end;
	int err;

	skbt = btf_type_by_id(reg->btf, bpf_sk_buff_ids[0]);
	qdisct = btf_type_by_id(reg->btf, bpf_qdisc_ids[0]);
	t = btf_type_by_id(reg->btf, reg->btf_id);

	if (t == skbt) {
		err = bpf_qdisc_sk_buff_access(log, reg, off, &end);
	} else if (t == qdisct) {
		err = bpf_qdisc_qdisc_access(log, reg, off, &end);
	} else {
		bpf_log(log, "only read is supported\n");
		return -EACCES;
	}

	if (err) {
		bpf_log(log, "no write support to %s at off %d\n",
			btf_name_by_offset(reg->btf, t->name_off), off);
		return -EACCES;
	}

	if (off + size > end) {
		bpf_log(log,
			"write access at off %d with size %d beyond the member of %s ended at %zu\n",
			off, size, btf_name_by_offset(reg->btf, t->name_off), end);
		return -EACCES;
	}

	return 0;
}

__bpf_kfunc_start_defs();

/* bpf_skb_get_hash - Get the flow hash of an skb.
 * @skb: The skb to get the flow hash from.
 */
__bpf_kfunc u32 bpf_skb_get_hash(struct sk_buff *skb)
{
	return skb_get_hash(skb);
}

/* bpf_kfree_skb - Release an skb's reference and drop it immediately.
 * @skb: The skb whose reference to be released and dropped.
 */
__bpf_kfunc void bpf_kfree_skb(struct sk_buff *skb)
{
	kfree_skb(skb);
}

/* bpf_qdisc_skb_drop - Drop an skb by adding it to a deferred free list.
 * @skb: The skb whose reference to be released and dropped.
 * @to_free_list: The list of skbs to be dropped.
 */
__bpf_kfunc void bpf_qdisc_skb_drop(struct sk_buff *skb,
				    struct bpf_sk_buff_ptr *to_free_list)
{
	__qdisc_drop(skb, (struct sk_buff **)to_free_list);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(qdisc_kfunc_ids)
BTF_ID_FLAGS(func, bpf_skb_get_hash, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_kfree_skb, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_qdisc_skb_drop, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_dynptr_from_skb, KF_TRUSTED_ARGS)
BTF_KFUNCS_END(qdisc_kfunc_ids)

BTF_SET_START(qdisc_common_kfunc_set)
BTF_ID(func, bpf_skb_get_hash)
BTF_ID(func, bpf_kfree_skb)
BTF_ID(func, bpf_dynptr_from_skb)
BTF_SET_END(qdisc_common_kfunc_set)

BTF_SET_START(qdisc_enqueue_kfunc_set)
BTF_ID(func, bpf_qdisc_skb_drop)
BTF_SET_END(qdisc_enqueue_kfunc_set)

enum qdisc_ops_kf_flags {
	QDISC_OPS_KF_COMMON		= 0,
	QDISC_OPS_KF_ENQUEUE		= 1 << 0,
};

static const u32 qdisc_ops_context_flags[] = {
	[QDISC_OP_IDX(enqueue)]		= QDISC_OPS_KF_ENQUEUE,
	[QDISC_OP_IDX(dequeue)]		= QDISC_OPS_KF_COMMON,
	[QDISC_OP_IDX(init)]		= QDISC_OPS_KF_COMMON,
	[QDISC_OP_IDX(reset)]		= QDISC_OPS_KF_COMMON,
	[QDISC_OP_IDX(destroy)]		= QDISC_OPS_KF_COMMON,
};

static int bpf_qdisc_kfunc_filter(const struct bpf_prog *prog, u32 kfunc_id)
{
	u32 moff, flags;

	if (!btf_id_set8_contains(&qdisc_kfunc_ids, kfunc_id))
		return 0;

	if (prog->aux->st_ops != &bpf_Qdisc_ops)
		return -EACCES;

	moff = prog->aux->attach_st_ops_member_off;
	flags = qdisc_ops_context_flags[QDISC_MOFF_IDX(moff)];

	if ((flags & QDISC_OPS_KF_ENQUEUE) &&
	    btf_id_set_contains(&qdisc_enqueue_kfunc_set, kfunc_id))
		return 0;

	if (btf_id_set_contains(&qdisc_common_kfunc_set, kfunc_id))
		return 0;

	return -EACCES;
}

static const struct btf_kfunc_id_set bpf_qdisc_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &qdisc_kfunc_ids,
	.filter = bpf_qdisc_kfunc_filter,
};

static const struct bpf_verifier_ops bpf_qdisc_verifier_ops = {
	.get_func_proto		= bpf_base_func_proto,
	.is_valid_access	= bpf_qdisc_is_valid_access,
	.btf_struct_access	= bpf_qdisc_btf_struct_access,
};

static int bpf_qdisc_init_member(const struct btf_type *t,
				 const struct btf_member *member,
				 void *kdata, const void *udata)
{
	const struct Qdisc_ops *uqdisc_ops;
	struct Qdisc_ops *qdisc_ops;
	u32 moff;

	uqdisc_ops = (const struct Qdisc_ops *)udata;
	qdisc_ops = (struct Qdisc_ops *)kdata;

	moff = __btf_member_bit_offset(t, member) / 8;
	switch (moff) {
	case offsetof(struct Qdisc_ops, peek):
		qdisc_ops->peek = qdisc_peek_dequeued;
		return 0;
	case offsetof(struct Qdisc_ops, id):
		if (bpf_obj_name_cpy(qdisc_ops->id, uqdisc_ops->id,
				     sizeof(qdisc_ops->id)) <= 0)
			return -EINVAL;
		return 1;
	}

	return 0;
}

static int bpf_qdisc_reg(void *kdata, struct bpf_link *link)
{
	return register_qdisc(kdata);
}

static void bpf_qdisc_unreg(void *kdata, struct bpf_link *link)
{
	return unregister_qdisc(kdata);
}

static int Qdisc_ops__enqueue(struct sk_buff *skb__ref, struct Qdisc *sch,
			      struct sk_buff **to_free)
{
	return 0;
}

static struct sk_buff *Qdisc_ops__dequeue(struct Qdisc *sch)
{
	return NULL;
}

static int Qdisc_ops__init(struct Qdisc *sch, struct nlattr *arg,
			   struct netlink_ext_ack *extack)
{
	return 0;
}

static void Qdisc_ops__reset(struct Qdisc *sch)
{
}

static void Qdisc_ops__destroy(struct Qdisc *sch)
{
}

static struct Qdisc_ops __bpf_ops_qdisc_ops = {
	.enqueue = Qdisc_ops__enqueue,
	.dequeue = Qdisc_ops__dequeue,
	.init = Qdisc_ops__init,
	.reset = Qdisc_ops__reset,
	.destroy = Qdisc_ops__destroy,
};

static struct bpf_struct_ops bpf_Qdisc_ops = {
	.verifier_ops = &bpf_qdisc_verifier_ops,
	.reg = bpf_qdisc_reg,
	.unreg = bpf_qdisc_unreg,
	.init_member = bpf_qdisc_init_member,
	.init = bpf_qdisc_init,
	.name = "Qdisc_ops",
	.cfi_stubs = &__bpf_ops_qdisc_ops,
	.owner = THIS_MODULE,
};

BTF_ID_LIST(bpf_sk_buff_dtor_ids)
BTF_ID(func, bpf_kfree_skb)

static int __init bpf_qdisc_kfunc_init(void)
{
	int ret;
	const struct btf_id_dtor_kfunc skb_kfunc_dtors[] = {
		{
			.btf_id       = bpf_sk_buff_ids[0],
			.kfunc_btf_id = bpf_sk_buff_dtor_ids[0]
		},
	};

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_STRUCT_OPS, &bpf_qdisc_kfunc_set);
	ret = ret ?: register_btf_id_dtor_kfuncs(skb_kfunc_dtors,
						 ARRAY_SIZE(skb_kfunc_dtors),
						 THIS_MODULE);
	ret = ret ?: register_bpf_struct_ops(&bpf_Qdisc_ops, Qdisc_ops);

	return ret;
}
late_initcall(bpf_qdisc_kfunc_init);
