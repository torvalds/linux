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
#include <linux/bpf.h>

#include <net/rtnetlink.h>
#include <net/pkt_cls.h>
#include <net/sock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Borkmann <dborkman@redhat.com>");
MODULE_DESCRIPTION("TC BPF based classifier");

#define CLS_BPF_NAME_LEN	256

struct cls_bpf_head {
	struct list_head plist;
	u32 hgen;
	struct rcu_head rcu;
};

struct cls_bpf_prog {
	struct bpf_prog *filter;
	struct list_head link;
	struct tcf_result res;
	struct tcf_exts exts;
	u32 handle;
	union {
		u32 bpf_fd;
		u16 bpf_num_ops;
	};
	struct sock_filter *bpf_ops;
	const char *bpf_name;
	struct tcf_proto *tp;
	struct rcu_head rcu;
};

static const struct nla_policy bpf_policy[TCA_BPF_MAX + 1] = {
	[TCA_BPF_CLASSID]	= { .type = NLA_U32 },
	[TCA_BPF_FD]		= { .type = NLA_U32 },
	[TCA_BPF_NAME]		= { .type = NLA_NUL_STRING, .len = CLS_BPF_NAME_LEN },
	[TCA_BPF_OPS_LEN]	= { .type = NLA_U16 },
	[TCA_BPF_OPS]		= { .type = NLA_BINARY,
				    .len = sizeof(struct sock_filter) * BPF_MAXINSNS },
};

static int cls_bpf_classify(struct sk_buff *skb, const struct tcf_proto *tp,
			    struct tcf_result *res)
{
	struct cls_bpf_head *head = rcu_dereference_bh(tp->root);
	struct cls_bpf_prog *prog;
	int ret = -1;

	/* Needed here for accessing maps. */
	rcu_read_lock();
	list_for_each_entry_rcu(prog, &head->plist, link) {
		int filter_res = BPF_PROG_RUN(prog->filter, skb);

		if (filter_res == 0)
			continue;

		*res = prog->res;
		if (filter_res != -1)
			res->classid = filter_res;

		ret = tcf_exts_exec(skb, &prog->exts, res);
		if (ret < 0)
			continue;

		break;
	}
	rcu_read_unlock();

	return ret;
}

static bool cls_bpf_is_ebpf(const struct cls_bpf_prog *prog)
{
	return !prog->bpf_ops;
}

static int cls_bpf_init(struct tcf_proto *tp)
{
	struct cls_bpf_head *head;

	head = kzalloc(sizeof(*head), GFP_KERNEL);
	if (head == NULL)
		return -ENOBUFS;

	INIT_LIST_HEAD_RCU(&head->plist);
	rcu_assign_pointer(tp->root, head);

	return 0;
}

static void cls_bpf_delete_prog(struct tcf_proto *tp, struct cls_bpf_prog *prog)
{
	tcf_exts_destroy(&prog->exts);

	if (cls_bpf_is_ebpf(prog))
		bpf_prog_put(prog->filter);
	else
		bpf_prog_destroy(prog->filter);

	kfree(prog->bpf_name);
	kfree(prog->bpf_ops);
	kfree(prog);
}

static void __cls_bpf_delete_prog(struct rcu_head *rcu)
{
	struct cls_bpf_prog *prog = container_of(rcu, struct cls_bpf_prog, rcu);

	cls_bpf_delete_prog(prog->tp, prog);
}

static int cls_bpf_delete(struct tcf_proto *tp, unsigned long arg)
{
	struct cls_bpf_prog *prog = (struct cls_bpf_prog *) arg;

	list_del_rcu(&prog->link);
	tcf_unbind_filter(tp, &prog->res);
	call_rcu(&prog->rcu, __cls_bpf_delete_prog);

	return 0;
}

static bool cls_bpf_destroy(struct tcf_proto *tp, bool force)
{
	struct cls_bpf_head *head = rtnl_dereference(tp->root);
	struct cls_bpf_prog *prog, *tmp;

	if (!force && !list_empty(&head->plist))
		return false;

	list_for_each_entry_safe(prog, tmp, &head->plist, link) {
		list_del_rcu(&prog->link);
		tcf_unbind_filter(tp, &prog->res);
		call_rcu(&prog->rcu, __cls_bpf_delete_prog);
	}

	RCU_INIT_POINTER(tp->root, NULL);
	kfree_rcu(head, rcu);
	return true;
}

static unsigned long cls_bpf_get(struct tcf_proto *tp, u32 handle)
{
	struct cls_bpf_head *head = rtnl_dereference(tp->root);
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

static int cls_bpf_prog_from_ops(struct nlattr **tb,
				 struct cls_bpf_prog *prog, u32 classid)
{
	struct sock_filter *bpf_ops;
	struct sock_fprog_kern fprog_tmp;
	struct bpf_prog *fp;
	u16 bpf_size, bpf_num_ops;
	int ret;

	bpf_num_ops = nla_get_u16(tb[TCA_BPF_OPS_LEN]);
	if (bpf_num_ops > BPF_MAXINSNS || bpf_num_ops == 0)
		return -EINVAL;

	bpf_size = bpf_num_ops * sizeof(*bpf_ops);
	if (bpf_size != nla_len(tb[TCA_BPF_OPS]))
		return -EINVAL;

	bpf_ops = kzalloc(bpf_size, GFP_KERNEL);
	if (bpf_ops == NULL)
		return -ENOMEM;

	memcpy(bpf_ops, nla_data(tb[TCA_BPF_OPS]), bpf_size);

	fprog_tmp.len = bpf_num_ops;
	fprog_tmp.filter = bpf_ops;

	ret = bpf_prog_create(&fp, &fprog_tmp);
	if (ret < 0) {
		kfree(bpf_ops);
		return ret;
	}

	prog->bpf_ops = bpf_ops;
	prog->bpf_num_ops = bpf_num_ops;
	prog->bpf_name = NULL;

	prog->filter = fp;
	prog->res.classid = classid;

	return 0;
}

static int cls_bpf_prog_from_efd(struct nlattr **tb,
				 struct cls_bpf_prog *prog, u32 classid)
{
	struct bpf_prog *fp;
	char *name = NULL;
	u32 bpf_fd;

	bpf_fd = nla_get_u32(tb[TCA_BPF_FD]);

	fp = bpf_prog_get(bpf_fd);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	if (fp->type != BPF_PROG_TYPE_SCHED_CLS) {
		bpf_prog_put(fp);
		return -EINVAL;
	}

	if (tb[TCA_BPF_NAME]) {
		name = kmemdup(nla_data(tb[TCA_BPF_NAME]),
			       nla_len(tb[TCA_BPF_NAME]),
			       GFP_KERNEL);
		if (!name) {
			bpf_prog_put(fp);
			return -ENOMEM;
		}
	}

	prog->bpf_ops = NULL;
	prog->bpf_fd = bpf_fd;
	prog->bpf_name = name;

	prog->filter = fp;
	prog->res.classid = classid;

	return 0;
}

static int cls_bpf_modify_existing(struct net *net, struct tcf_proto *tp,
				   struct cls_bpf_prog *prog,
				   unsigned long base, struct nlattr **tb,
				   struct nlattr *est, bool ovr)
{
	struct tcf_exts exts;
	bool is_bpf, is_ebpf;
	u32 classid;
	int ret;

	is_bpf = tb[TCA_BPF_OPS_LEN] && tb[TCA_BPF_OPS];
	is_ebpf = tb[TCA_BPF_FD];

	if ((!is_bpf && !is_ebpf) || (is_bpf && is_ebpf) ||
	    !tb[TCA_BPF_CLASSID])
		return -EINVAL;

	tcf_exts_init(&exts, TCA_BPF_ACT, TCA_BPF_POLICE);
	ret = tcf_exts_validate(net, tp, tb, est, &exts, ovr);
	if (ret < 0)
		return ret;

	classid = nla_get_u32(tb[TCA_BPF_CLASSID]);

	ret = is_bpf ? cls_bpf_prog_from_ops(tb, prog, classid) :
		       cls_bpf_prog_from_efd(tb, prog, classid);
	if (ret < 0) {
		tcf_exts_destroy(&exts);
		return ret;
	}

	tcf_bind_filter(tp, &prog->res, base);
	tcf_exts_change(tp, &prog->exts, &exts);

	return 0;
}

static u32 cls_bpf_grab_new_handle(struct tcf_proto *tp,
				   struct cls_bpf_head *head)
{
	unsigned int i = 0x80000000;
	u32 handle;

	do {
		if (++head->hgen == 0x7FFFFFFF)
			head->hgen = 1;
	} while (--i > 0 && cls_bpf_get(tp, head->hgen));

	if (unlikely(i == 0)) {
		pr_err("Insufficient number of handles\n");
		handle = 0;
	} else {
		handle = head->hgen;
	}

	return handle;
}

static int cls_bpf_change(struct net *net, struct sk_buff *in_skb,
			  struct tcf_proto *tp, unsigned long base,
			  u32 handle, struct nlattr **tca,
			  unsigned long *arg, bool ovr)
{
	struct cls_bpf_head *head = rtnl_dereference(tp->root);
	struct cls_bpf_prog *oldprog = (struct cls_bpf_prog *) *arg;
	struct nlattr *tb[TCA_BPF_MAX + 1];
	struct cls_bpf_prog *prog;
	int ret;

	if (tca[TCA_OPTIONS] == NULL)
		return -EINVAL;

	ret = nla_parse_nested(tb, TCA_BPF_MAX, tca[TCA_OPTIONS], bpf_policy);
	if (ret < 0)
		return ret;

	prog = kzalloc(sizeof(*prog), GFP_KERNEL);
	if (!prog)
		return -ENOBUFS;

	tcf_exts_init(&prog->exts, TCA_BPF_ACT, TCA_BPF_POLICE);

	if (oldprog) {
		if (handle && oldprog->handle != handle) {
			ret = -EINVAL;
			goto errout;
		}
	}

	if (handle == 0)
		prog->handle = cls_bpf_grab_new_handle(tp, head);
	else
		prog->handle = handle;
	if (prog->handle == 0) {
		ret = -EINVAL;
		goto errout;
	}

	ret = cls_bpf_modify_existing(net, tp, prog, base, tb, tca[TCA_RATE], ovr);
	if (ret < 0)
		goto errout;

	if (oldprog) {
		list_replace_rcu(&prog->link, &oldprog->link);
		tcf_unbind_filter(tp, &oldprog->res);
		call_rcu(&oldprog->rcu, __cls_bpf_delete_prog);
	} else {
		list_add_rcu(&prog->link, &head->plist);
	}

	*arg = (unsigned long) prog;
	return 0;
errout:
	kfree(prog);

	return ret;
}

static int cls_bpf_dump_bpf_info(const struct cls_bpf_prog *prog,
				 struct sk_buff *skb)
{
	struct nlattr *nla;

	if (nla_put_u16(skb, TCA_BPF_OPS_LEN, prog->bpf_num_ops))
		return -EMSGSIZE;

	nla = nla_reserve(skb, TCA_BPF_OPS, prog->bpf_num_ops *
			  sizeof(struct sock_filter));
	if (nla == NULL)
		return -EMSGSIZE;

	memcpy(nla_data(nla), prog->bpf_ops, nla_len(nla));

	return 0;
}

static int cls_bpf_dump_ebpf_info(const struct cls_bpf_prog *prog,
				  struct sk_buff *skb)
{
	if (nla_put_u32(skb, TCA_BPF_FD, prog->bpf_fd))
		return -EMSGSIZE;

	if (prog->bpf_name &&
	    nla_put_string(skb, TCA_BPF_NAME, prog->bpf_name))
		return -EMSGSIZE;

	return 0;
}

static int cls_bpf_dump(struct net *net, struct tcf_proto *tp, unsigned long fh,
			struct sk_buff *skb, struct tcmsg *tm)
{
	struct cls_bpf_prog *prog = (struct cls_bpf_prog *) fh;
	struct nlattr *nest;
	int ret;

	if (prog == NULL)
		return skb->len;

	tm->tcm_handle = prog->handle;

	nest = nla_nest_start(skb, TCA_OPTIONS);
	if (nest == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_BPF_CLASSID, prog->res.classid))
		goto nla_put_failure;

	if (cls_bpf_is_ebpf(prog))
		ret = cls_bpf_dump_ebpf_info(prog, skb);
	else
		ret = cls_bpf_dump_bpf_info(prog, skb);
	if (ret)
		goto nla_put_failure;

	if (tcf_exts_dump(skb, &prog->exts) < 0)
		goto nla_put_failure;

	nla_nest_end(skb, nest);

	if (tcf_exts_dump_stats(skb, &prog->exts) < 0)
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -1;
}

static void cls_bpf_walk(struct tcf_proto *tp, struct tcf_walker *arg)
{
	struct cls_bpf_head *head = rtnl_dereference(tp->root);
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
