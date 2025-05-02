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

struct bpf_sched_data {
	struct qdisc_watchdog watchdog;
};

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

BTF_ID_LIST(bpf_qdisc_init_prologue_ids)
BTF_ID(func, bpf_qdisc_init_prologue)

static int bpf_qdisc_gen_prologue(struct bpf_insn *insn_buf, bool direct_write,
				  const struct bpf_prog *prog)
{
	struct bpf_insn *insn = insn_buf;

	if (prog->aux->attach_st_ops_member_off != offsetof(struct Qdisc_ops, init))
		return 0;

	/* r6 = r1; // r6 will be "u64 *ctx". r1 is "u64 *ctx".
	 * r2 = r1[16]; // r2 will be "struct netlink_ext_ack *extack"
	 * r1 = r1[0]; // r1 will be "struct Qdisc *sch"
	 * r0 = bpf_qdisc_init_prologue(r1, r2);
	 * if r0 == 0 goto pc+1;
	 * BPF_EXIT;
	 * r1 = r6; // r1 will be "u64 *ctx".
	 */
	*insn++ = BPF_MOV64_REG(BPF_REG_6, BPF_REG_1);
	*insn++ = BPF_LDX_MEM(BPF_DW, BPF_REG_2, BPF_REG_1, 16);
	*insn++ = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);
	*insn++ = BPF_CALL_KFUNC(0, bpf_qdisc_init_prologue_ids[0]);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 1);
	*insn++ = BPF_EXIT_INSN();
	*insn++ = BPF_MOV64_REG(BPF_REG_1, BPF_REG_6);
	*insn++ = prog->insnsi[0];

	return insn - insn_buf;
}

BTF_ID_LIST(bpf_qdisc_reset_destroy_epilogue_ids)
BTF_ID(func, bpf_qdisc_reset_destroy_epilogue)

static int bpf_qdisc_gen_epilogue(struct bpf_insn *insn_buf, const struct bpf_prog *prog,
				  s16 ctx_stack_off)
{
	struct bpf_insn *insn = insn_buf;

	if (prog->aux->attach_st_ops_member_off != offsetof(struct Qdisc_ops, reset) &&
	    prog->aux->attach_st_ops_member_off != offsetof(struct Qdisc_ops, destroy))
		return 0;

	/* r1 = stack[ctx_stack_off]; // r1 will be "u64 *ctx"
	 * r1 = r1[0]; // r1 will be "struct Qdisc *sch"
	 * r0 = bpf_qdisc_reset_destroy_epilogue(r1);
	 * BPF_EXIT;
	 */
	*insn++ = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_FP, ctx_stack_off);
	*insn++ = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);
	*insn++ = BPF_CALL_KFUNC(0, bpf_qdisc_reset_destroy_epilogue_ids[0]);
	*insn++ = BPF_EXIT_INSN();

	return insn - insn_buf;
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

/* bpf_qdisc_watchdog_schedule - Schedule a qdisc to a later time using a timer.
 * @sch: The qdisc to be scheduled.
 * @expire: The expiry time of the timer.
 * @delta_ns: The slack range of the timer.
 */
__bpf_kfunc void bpf_qdisc_watchdog_schedule(struct Qdisc *sch, u64 expire, u64 delta_ns)
{
	struct bpf_sched_data *q = qdisc_priv(sch);

	qdisc_watchdog_schedule_range_ns(&q->watchdog, expire, delta_ns);
}

/* bpf_qdisc_init_prologue - Hidden kfunc called in prologue of .init. */
__bpf_kfunc int bpf_qdisc_init_prologue(struct Qdisc *sch,
					struct netlink_ext_ack *extack)
{
	struct bpf_sched_data *q = qdisc_priv(sch);
	struct net_device *dev = qdisc_dev(sch);
	struct Qdisc *p;

	qdisc_watchdog_init(&q->watchdog, sch);

	if (sch->parent != TC_H_ROOT) {
		/* If qdisc_lookup() returns NULL, it means .init is called by
		 * qdisc_create_dflt() in mq/mqprio_init and the parent qdisc
		 * has not been added to qdisc_hash yet.
		 */
		p = qdisc_lookup(dev, TC_H_MAJ(sch->parent));
		if (p && !(p->flags & TCQ_F_MQROOT)) {
			NL_SET_ERR_MSG(extack, "BPF qdisc only supported on root or mq");
			return -EINVAL;
		}
	}

	return 0;
}

/* bpf_qdisc_reset_destroy_epilogue - Hidden kfunc called in epilogue of .reset
 * and .destroy
 */
__bpf_kfunc void bpf_qdisc_reset_destroy_epilogue(struct Qdisc *sch)
{
	struct bpf_sched_data *q = qdisc_priv(sch);

	qdisc_watchdog_cancel(&q->watchdog);
}

/* bpf_qdisc_bstats_update - Update Qdisc basic statistics
 * @sch: The qdisc from which an skb is dequeued.
 * @skb: The skb to be dequeued.
 */
__bpf_kfunc void bpf_qdisc_bstats_update(struct Qdisc *sch, const struct sk_buff *skb)
{
	bstats_update(&sch->bstats, skb);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(qdisc_kfunc_ids)
BTF_ID_FLAGS(func, bpf_skb_get_hash, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_kfree_skb, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_qdisc_skb_drop, KF_RELEASE)
BTF_ID_FLAGS(func, bpf_dynptr_from_skb, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_qdisc_watchdog_schedule, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_qdisc_init_prologue, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_qdisc_reset_destroy_epilogue, KF_TRUSTED_ARGS)
BTF_ID_FLAGS(func, bpf_qdisc_bstats_update, KF_TRUSTED_ARGS)
BTF_KFUNCS_END(qdisc_kfunc_ids)

BTF_SET_START(qdisc_common_kfunc_set)
BTF_ID(func, bpf_skb_get_hash)
BTF_ID(func, bpf_kfree_skb)
BTF_ID(func, bpf_dynptr_from_skb)
BTF_SET_END(qdisc_common_kfunc_set)

BTF_SET_START(qdisc_enqueue_kfunc_set)
BTF_ID(func, bpf_qdisc_skb_drop)
BTF_ID(func, bpf_qdisc_watchdog_schedule)
BTF_SET_END(qdisc_enqueue_kfunc_set)

BTF_SET_START(qdisc_dequeue_kfunc_set)
BTF_ID(func, bpf_qdisc_watchdog_schedule)
BTF_ID(func, bpf_qdisc_bstats_update)
BTF_SET_END(qdisc_dequeue_kfunc_set)

enum qdisc_ops_kf_flags {
	QDISC_OPS_KF_COMMON		= 0,
	QDISC_OPS_KF_ENQUEUE		= 1 << 0,
	QDISC_OPS_KF_DEQUEUE		= 1 << 1,
};

static const u32 qdisc_ops_context_flags[] = {
	[QDISC_OP_IDX(enqueue)]		= QDISC_OPS_KF_ENQUEUE,
	[QDISC_OP_IDX(dequeue)]		= QDISC_OPS_KF_DEQUEUE,
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

	if ((flags & QDISC_OPS_KF_DEQUEUE) &&
	    btf_id_set_contains(&qdisc_dequeue_kfunc_set, kfunc_id))
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
	.gen_prologue		= bpf_qdisc_gen_prologue,
	.gen_epilogue		= bpf_qdisc_gen_epilogue,
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
	case offsetof(struct Qdisc_ops, priv_size):
		if (uqdisc_ops->priv_size)
			return -EINVAL;
		qdisc_ops->priv_size = sizeof(struct bpf_sched_data);
		return 1;
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

static int bpf_qdisc_validate(void *kdata)
{
	struct Qdisc_ops *ops = (struct Qdisc_ops *)kdata;

	if (!ops->enqueue || !ops->dequeue || !ops->init ||
	    !ops->reset || !ops->destroy)
		return -EINVAL;

	return 0;
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
	.validate = bpf_qdisc_validate,
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
