/*
 * Berkeley Packet Filter based traffic classifier
 *
 * Might be used to classify traffic through flexible, user-defined and
 * possibly JIT-ed BPF filters for traffic control as an alternative to
 * ematches.
 *
 * (C) 2013 Daniel Borkmann <dborkman@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/filter.h>
#include <net/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/sock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Borkmann <dborkman@redhat.com>");
MODULE_DESCRIPTION("TC BPF based classifier");

struct cls_bpf_head {
	struct list_head plist;
	u32 hgen;
};

struct cls_bpf_prog {
	struct sk_filter *filter;
	struct sock_filter *bpf_ops;
	struct tcf_exts exts;
	struct tcf_result res;
	struct list_head link;
	u32 handle;
	u16 bpf_len;
};

static const struct nla_policy bpf_policy[TCA_BPF_MAX + 1] = {
	[TCA_BPF_CLASSID]	= { .type = NLA_U32 },
	[TCA_BPF_OPS_LEN]	= { .type = NLA_U16 },
	[TCA_BPF_OPS]		= { .type = NLA_BINARY,
				    .len = sizeof(struct sock_filter) * BPF_MAXINSNS },
};

static const struct tcf_ext_map bpf_ext_map = {
	.action = TCA_BPF_ACT,
	.police = TCA_BPF_POLICE,
};

static int cls_bpf_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			    struct tcf_result *res)
{
	struct cls_bpf_head *head = tp->root;
	struct cls_bpf_prog *prog;
	int ret;

	list_for_each_entry(prog, &head->plist, link) {
		int filter_res = SK_RUN_FILTER(prog->filter, skb);

		if (filter_res == 0)
			continue;

		*res = prog->res;
		if (filter_res != -1)
			res->classid = filter_res;

		ret = tcf_exts_exec(skb, &prog->exts, res);
		if (ret < 0)
			continue;

		return ret;
	}

	return -1;
}

static int cls_bpf_init(struct tcf_proto *tp)
{
	struct cls_bpf_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (head == NULL)
		return -ENOBUFS;

	INIT_LIST_HEAD(&head->plist);
	tp->root = head;

	return 0;
}

static void cls_bpf_delete_prog(struct tcf_proto *tp, struct cls_bpf_prog *prog)
{
	tcf_unbind_filter(tp, &prog->res);
	tcf_exts_destroy(tp, &prog->exts);

	sk_unattached_filter_destroy(prog->filter);

	kfree(prog->bpf_ops);
	kfree(prog);
}

static int cls_bpf_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct cls_bpf_head *head = tp->root;
	struct cls_bpf_prog *prog, *todel = (struct cls_bpf_prog *) arg;

	list_for_each_entry(prog, &head->plist, link) {
		if (prog == todel) {
			tcf_tree_lock(tp);
			list_del(&prog->link);
			tcf_tree_unlock(tp);

			cls_bpf_delete_prog(tp, prog);
			return 0;
		}
	}

	return -ENOENT;
}

static void cls_bpf_destroy(struct tcf_proto *tp)
{
	struct cls_bpf_head *head = tp->root;
	struct cls_bpf_prog *prog, *tmp;

	list_for_each_entry_safe(prog, tmp, &head->plist, link) {
		list_del(&prog->link);
		cls_bpf_delete_prog(tp, prog);
	}

	kfree(head);
}

static unsigned long cls_bpf_get(struct tcf_proto *tp, u32 handle)
{
	struct cls_bpf_head *head = tp->root;
	struct cls_bpf_prog *prog;
	unsigned long ret = 0UL;

	if (head == NULL)
		return 0UL;

	list_for_each_entry(prog, &head->plist, link) {
		if (prog->handle == handle) {
			ret = (unsigned long) prog;
			break;
		}
	}

	return ret;
}

static void cls_bpf_put(struct tcf_proto *tp, unsigned long f)
{
}

static int cls_bpf_modify_existing(struct net *net, struct tcf_proto *tp,
				   struct cls_bpf_prog *prog,
				   unsigned long base, struct nlattr **tb,
				   struct nlattr *est)
{
	struct sock_filter *bpf_ops, *bpf_old;
	struct tcf_exts exts;
	struct sock_fprog tmp;
	struct sk_filter *fp, *fp_old;
	u16 bpf_size, bpf_len;
	u32 classid;
	int ret;

	if (!tb[TCA_BPF_OPS_LEN] || !tb[TCA_BPF_OPS] || !tb[TCA_BPF_CLASSID])
		return -EINVAL;

	ret = tcf_exts_validate(net, tp, tb, est, &exts, &bpf_ext_map);
	if (ret < 0)
		return ret;

	classid = nla_get_u32(tb[TCA_BPF_CLASSID]);
	bpf_len = nla_get_u16(tb[TCA_BPF_OPS_LEN]);
	if (bpf_len > BPF_MAXINSNS || bpf_len == 0) {
		ret = -EINVAL;
		goto errout;
	}

	bpf_size = bpf_len * sizeof(*bpf_ops);
	bpf_ops = kzalloc(bpf_size, GFP_KERNEL);
	if (bpf_ops == NULL) {
		ret = -ENOMEM;
		goto errout;
	}

	memcpy(bpf_ops, nla_data(tb[TCA_BPF_OPS]), bpf_size);

	tmp.len = bpf_len;
	tmp.filter = (struct sock_filter __user *) bpf_ops;

	ret = sk_unattached_filter_create(&fp, &tmp);
	if (ret)
		goto errout_free;

	tcf_tree_lock(tp);
	fp_old = prog->filter;
	bpf_old = prog->bpf_ops;

	prog->bpf_len = bpf_len;
	prog->bpf_ops = bpf_ops;
	prog->filter = fp;
	prog->res.classid = classid;
	tcf_tree_unlock(tp);

	tcf_bind_filter(tp, &prog->res, base);
	tcf_exts_change(tp, &prog->exts, &exts);

	if (fp_old)
		sk_unattached_filter_destroy(fp_old);
	if (bpf_old)
		kfree(bpf_old);

	return 0;

errout_free:
	kfree(bpf_ops);
errout:
	tcf_exts_destroy(tp, &exts);
	return ret;
}

static u32 cls_bpf_grab_new_handle(struct tcf_proto *tp,
				   struct cls_bpf_head *head)
{
	unsigned int i = 0x80000000;

	do {
		if (++head->hgen == 0x7FFFFFFF)
			head->hgen = 1;
	} while (--i > 0 && cls_bpf_get(tp, head->hgen));
	if (i == 0)
		pr_err("Insufficient number of handles\n");

	return i;
}

static int cls_bpf_change(struct net *net, struct sk_buff *in_skb,
			  struct tcf_proto *tp, unsigned long base,
			  u32 handle, struct nlattr **tca,
			  unsigned long *arg)
{
	struct cls_bpf_head *head = tp->root;
	struct cls_bpf_prog *prog = (struct cls_bpf_prog *) *arg;
	struct nlattr *tb[TCA_BPF_MAX + 1];
	int ret;

	if (tca[TCA_OPTIONS] == NULL)
		return -EINVAL;

	ret = nla_parse_nested(tb, TCA_BPF_MAX, tca[TCA_OPTIONS], bpf_policy);
	if (ret < 0)
		return ret;

	if (prog != NULL) {
		if (handle && prog->handle != handle)
			return -EINVAL;
		return cls_bpf_modify_existing(net, tp, prog, base, tb,
					       tca[TCA_RATE]);
	}

	prog = kzalloc(sizeof(*prog), GFP_KERNEL);
	if (prog == NULL)
		return -ENOBUFS;

	if (handle == 0)
		prog->handle = cls_bpf_grab_new_handle(tp, head);
	else
		prog->handle = handle;
	if (prog->handle == 0) {
		ret = -EINVAL;
		goto errout;
	}

	ret = cls_bpf_modify_existing(net, tp, prog, base, tb, tca[TCA_RATE]);
	if (ret < 0)
		goto errout;

	tcf_tree_lock(tp);
	list_add(&prog->link, &head->plist);
	tcf_tree_unlock(tp);

	*arg = (unsigned long) prog;

	return 0;
errout:
	if (*arg == 0UL && prog)
		kfree(prog);

	return ret;
}

static int cls_bpf_dump(struct tcf_proto *tp, unsigned long fh,
			struct sk_buff *skb, struct tcmsg *tm)
{
	struct cls_bpf_prog *prog = (struct cls_bpf_prog *) fh;
	struct nlattr *nest, *nla;

	if (prog == NULL)
		return skb->len;

	tm->tcm_handle = prog->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_BPF_CLASSID, prog->res.classid))
		goto nla_put_failure;
	if (nla_put_u16(skb, TCA_BPF_OPS_LEN, prog->bpf_len))
		goto nla_put_failure;

	nla = nla_reserve(skb, TCA_BPF_OPS, prog->bpf_len *
			  sizeof(struct sock_filter));
	if (nla == NULL)
		goto nla_put_failure;

        memcpy(nla_data(nla), prog->bpf_ops, nla_len(nla));

	if (tcf_exts_dump(skb, &prog->exts, &bpf_ext_map) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &prog->exts, &bpf_ext_map) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static void cls_bpf_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct cls_bpf_head *head = tp->root;
	struct cls_bpf_prog *prog;

	list_for_each_entry(prog, &head->plist, link) {
		if (arg->count < arg->skip)
			goto skip;
		if (arg->fn(tp, (unsigned long) prog, arg) < 0) {
			arg->stop = 1;
			break;
		}
skip:
		arg->count++;
	}
}

static struct tcf_proto_ops cls_bpf_ops __read_mostly = {
	.kind		=	"bpf",
	.owner		=	THIS_MODULE,
	.classify	=	cls_bpf_classify,
	.init		=	cls_bpf_init,
	.destroy	=	cls_bpf_destroy,
	.get		=	cls_bpf_get,
	.put		=	cls_bpf_put,
	.change		=	cls_bpf_change,
	.delete		=	cls_bpf_delete,
	.walk		=	cls_bpf_walk,
	.dump		=	cls_bpf_dump,
};

static int __init cls_bpf_init_mod(void)
{
	return register_tcf_proto_ops(&cls_bpf_ops);
}

static void __exit cls_bpf_exit_mod(void)
{
	unregister_tcf_proto_ops(&cls_bpf_ops);
}

module_init(cls_bpf_init_mod);
module_exit(cls_bpf_exit_mod);
