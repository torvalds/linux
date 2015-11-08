/*
 * Copyright (c) 2015 Jiri Pirko <jiri@resnulli.us>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/filter.h>
#include <linux/bpf.h>

#include <net/netlink.h>
#include <net/pkt_sched.h>

#include <linux/tc_act/tc_bpf.h>
#include <net/tc_act/tc_bpf.h>

#define BPF_TAB_MASK		15
#define ACT_BPF_NAME_LEN	256

struct tcf_bpf_cfg {
	struct bpf_prog *filter;
	struct sock_filter *bpf_ops;
	const char *bpf_name;
	u32 bpf_fd;
	u16 bpf_num_ops;
	bool is_ebpf;
};

static int tcf_bpf(struct sk_buff *skb, const struct tc_action *act,
		   struct tcf_result *res)
{
	struct tcf_bpf *prog = act->priv;
	struct bpf_prog *filter;
	int action, filter_res;
	bool at_ingress = G_TC_AT(skb->tc_verd) & AT_INGRESS;

	if (unlikely(!skb_mac_header_was_set(skb)))
		return TC_ACT_UNSPEC;

	tcf_lastuse_update(&prog->tcf_tm);
	bstats_cpu_update(this_cpu_ptr(prog->common.cpu_bstats), skb);

	rcu_read_lock();
	filter = rcu_dereference(prog->filter);
	if (at_ingress) {
		__skb_push(skb, skb->mac_len);
		filter_res = BPF_PROG_RUN(filter, skb);
		__skb_pull(skb, skb->mac_len);
	} else {
		filter_res = BPF_PROG_RUN(filter, skb);
	}
	rcu_read_unlock();

	/* A BPF program may overwrite the default action opcode.
	 * Similarly as in cls_bpf, if filter_res == -1 we use the
	 * default action specified from tc.
	 *
	 * In case a different well-known TC_ACT opcode has been
	 * returned, it will overwrite the default one.
	 *
	 * For everything else that is unkown, TC_ACT_UNSPEC is
	 * returned.
	 */
	switch (filter_res) {
	case TC_ACT_PIPE:
	case TC_ACT_RECLASSIFY:
	case TC_ACT_OK:
	case TC_ACT_REDIRECT:
		action = filter_res;
		break;
	case TC_ACT_SHOT:
		action = filter_res;
		qstats_drop_inc(this_cpu_ptr(prog->common.cpu_qstats));
		break;
	case TC_ACT_UNSPEC:
		action = prog->tcf_action;
		break;
	default:
		action = TC_ACT_UNSPEC;
		break;
	}

	return action;
}

static bool tcf_bpf_is_ebpf(const struct tcf_bpf *prog)
{
	return !prog->bpf_ops;
}

static int tcf_bpf_dump_bpf_info(const struct tcf_bpf *prog,
				 struct sk_buff *skb)
{
	struct nlattr *nla;

	if (nla_put_u16(skb, TCA_ACT_BPF_OPS_LEN, prog->bpf_num_ops))
		return -EMSGSIZE;

	nla = nla_reserve(skb, TCA_ACT_BPF_OPS, prog->bpf_num_ops *
			  sizeof(struct sock_filter));
	if (nla == NULL)
		return -EMSGSIZE;

	memcpy(nla_data(nla), prog->bpf_ops, nla_len(nla));

	return 0;
}

static int tcf_bpf_dump_ebpf_info(const struct tcf_bpf *prog,
				  struct sk_buff *skb)
{
	if (nla_put_u32(skb, TCA_ACT_BPF_FD, prog->bpf_fd))
		return -EMSGSIZE;

	if (prog->bpf_name &&
	    nla_put_string(skb, TCA_ACT_BPF_NAME, prog->bpf_name))
		return -EMSGSIZE;

	return 0;
}

static int tcf_bpf_dump(struct sk_buff *skb, struct tc_action *act,
			int bind, int ref)
{
	unsigned char *tp = skb_tail_pointer(skb);
	struct tcf_bpf *prog = act->priv;
	struct tc_act_bpf opt = {
		.index   = prog->tcf_index,
		.refcnt  = prog->tcf_refcnt - ref,
		.bindcnt = prog->tcf_bindcnt - bind,
		.action  = prog->tcf_action,
	};
	struct tcf_t tm;
	int ret;

	if (nla_put(skb, TCA_ACT_BPF_PARMS, sizeof(opt), &opt))
		goto nla_put_failure;

	if (tcf_bpf_is_ebpf(prog))
		ret = tcf_bpf_dump_ebpf_info(prog, skb);
	else
		ret = tcf_bpf_dump_bpf_info(prog, skb);
	if (ret)
		goto nla_put_failure;

	tm.install = jiffies_to_clock_t(jiffies - prog->tcf_tm.install);
	tm.lastuse = jiffies_to_clock_t(jiffies - prog->tcf_tm.lastuse);
	tm.expires = jiffies_to_clock_t(prog->tcf_tm.expires);

	if (nla_put(skb, TCA_ACT_BPF_TM, sizeof(tm), &tm))
		goto nla_put_failure;

	return skb->len;

nla_put_failure:
	nlmsg_trim(skb, tp);
	return -1;
}

static const struct nla_policy act_bpf_policy[TCA_ACT_BPF_MAX + 1] = {
	[TCA_ACT_BPF_PARMS]	= { .len = sizeof(struct tc_act_bpf) },
	[TCA_ACT_BPF_FD]	= { .type = NLA_U32 },
	[TCA_ACT_BPF_NAME]	= { .type = NLA_NUL_STRING, .len = ACT_BPF_NAME_LEN },
	[TCA_ACT_BPF_OPS_LEN]	= { .type = NLA_U16 },
	[TCA_ACT_BPF_OPS]	= { .type = NLA_BINARY,
				    .len = sizeof(struct sock_filter) * BPF_MAXINSNS },
};

static int tcf_bpf_init_from_ops(struct nlattr **tb, struct tcf_bpf_cfg *cfg)
{
	struct sock_filter *bpf_ops;
	struct sock_fprog_kern fprog_tmp;
	struct bpf_prog *fp;
	u16 bpf_size, bpf_num_ops;
	int ret;

	bpf_num_ops = nla_get_u16(tb[TCA_ACT_BPF_OPS_LEN]);
	if (bpf_num_ops	> BPF_MAXINSNS || bpf_num_ops == 0)
		return -EINVAL;

	bpf_size = bpf_num_ops * sizeof(*bpf_ops);
	if (bpf_size != nla_len(tb[TCA_ACT_BPF_OPS]))
		return -EINVAL;

	bpf_ops = kzalloc(bpf_size, GFP_KERNEL);
	if (bpf_ops == NULL)
		return -ENOMEM;

	memcpy(bpf_ops, nla_data(tb[TCA_ACT_BPF_OPS]), bpf_size);

	fprog_tmp.len = bpf_num_ops;
	fprog_tmp.filter = bpf_ops;

	ret = bpf_prog_create(&fp, &fprog_tmp);
	if (ret < 0) {
		kfree(bpf_ops);
		return ret;
	}

	cfg->bpf_ops = bpf_ops;
	cfg->bpf_num_ops = bpf_num_ops;
	cfg->filter = fp;
	cfg->is_ebpf = false;

	return 0;
}

static int tcf_bpf_init_from_efd(struct nlattr **tb, struct tcf_bpf_cfg *cfg)
{
	struct bpf_prog *fp;
	char *name = NULL;
	u32 bpf_fd;

	bpf_fd = nla_get_u32(tb[TCA_ACT_BPF_FD]);

	fp = bpf_prog_get(bpf_fd);
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	if (fp->type != BPF_PROG_TYPE_SCHED_ACT) {
		bpf_prog_put(fp);
		return -EINVAL;
	}

	if (tb[TCA_ACT_BPF_NAME]) {
		name = kmemdup(nla_data(tb[TCA_ACT_BPF_NAME]),
			       nla_len(tb[TCA_ACT_BPF_NAME]),
			       GFP_KERNEL);
		if (!name) {
			bpf_prog_put(fp);
			return -ENOMEM;
		}
	}

	cfg->bpf_fd = bpf_fd;
	cfg->bpf_name = name;
	cfg->filter = fp;
	cfg->is_ebpf = true;

	return 0;
}

static void tcf_bpf_cfg_cleanup(const struct tcf_bpf_cfg *cfg)
{
	if (cfg->is_ebpf)
		bpf_prog_put(cfg->filter);
	else
		bpf_prog_destroy(cfg->filter);

	kfree(cfg->bpf_ops);
	kfree(cfg->bpf_name);
}

static void tcf_bpf_prog_fill_cfg(const struct tcf_bpf *prog,
				  struct tcf_bpf_cfg *cfg)
{
	cfg->is_ebpf = tcf_bpf_is_ebpf(prog);
	/* updates to prog->filter are prevented, since it's called either
	 * with rtnl lock or during final cleanup in rcu callback
	 */
	cfg->filter = rcu_dereference_protected(prog->filter, 1);

	cfg->bpf_ops = prog->bpf_ops;
	cfg->bpf_name = prog->bpf_name;
}

static int tcf_bpf_init(struct net *net, struct nlattr *nla,
			struct nlattr *est, struct tc_action *act,
			int replace, int bind)
{
	struct nlattr *tb[TCA_ACT_BPF_MAX + 1];
	struct tcf_bpf_cfg cfg, old;
	struct tc_act_bpf *parm;
	struct tcf_bpf *prog;
	bool is_bpf, is_ebpf;
	int ret, res = 0;

	if (!nla)
		return -EINVAL;

	ret = nla_parse_nested(tb, TCA_ACT_BPF_MAX, nla, act_bpf_policy);
	if (ret < 0)
		return ret;

	if (!tb[TCA_ACT_BPF_PARMS])
		return -EINVAL;

	parm = nla_data(tb[TCA_ACT_BPF_PARMS]);

	if (!tcf_hash_check(parm->index, act, bind)) {
		ret = tcf_hash_create(parm->index, est, act,
				      sizeof(*prog), bind, true);
		if (ret < 0)
			return ret;

		res = ACT_P_CREATED;
	} else {
		/* Don't override defaults. */
		if (bind)
			return 0;

		tcf_hash_release(act, bind);
		if (!replace)
			return -EEXIST;
	}

	is_bpf = tb[TCA_ACT_BPF_OPS_LEN] && tb[TCA_ACT_BPF_OPS];
	is_ebpf = tb[TCA_ACT_BPF_FD];

	if ((!is_bpf && !is_ebpf) || (is_bpf && is_ebpf)) {
		ret = -EINVAL;
		goto out;
	}

	memset(&cfg, 0, sizeof(cfg));

	ret = is_bpf ? tcf_bpf_init_from_ops(tb, &cfg) :
		       tcf_bpf_init_from_efd(tb, &cfg);
	if (ret < 0)
		goto out;

	prog = to_bpf(act);
	ASSERT_RTNL();

	if (res != ACT_P_CREATED)
		tcf_bpf_prog_fill_cfg(prog, &old);

	prog->bpf_ops = cfg.bpf_ops;
	prog->bpf_name = cfg.bpf_name;

	if (cfg.bpf_num_ops)
		prog->bpf_num_ops = cfg.bpf_num_ops;
	if (cfg.bpf_fd)
		prog->bpf_fd = cfg.bpf_fd;

	prog->tcf_action = parm->action;
	rcu_assign_pointer(prog->filter, cfg.filter);

	if (res == ACT_P_CREATED) {
		tcf_hash_insert(act);
	} else {
		/* make sure the program being replaced is no longer executing */
		synchronize_rcu();
		tcf_bpf_cfg_cleanup(&old);
	}

	return res;
out:
	if (res == ACT_P_CREATED)
		tcf_hash_cleanup(act, est);

	return ret;
}

static void tcf_bpf_cleanup(struct tc_action *act, int bind)
{
	struct tcf_bpf_cfg tmp;

	tcf_bpf_prog_fill_cfg(act->priv, &tmp);
	tcf_bpf_cfg_cleanup(&tmp);
}

static struct tc_action_ops act_bpf_ops __read_mostly = {
	.kind		=	"bpf",
	.type		=	TCA_ACT_BPF,
	.owner		=	THIS_MODULE,
	.act		=	tcf_bpf,
	.dump		=	tcf_bpf_dump,
	.cleanup	=	tcf_bpf_cleanup,
	.init		=	tcf_bpf_init,
};

static int __init bpf_init_module(void)
{
	return tcf_register_action(&act_bpf_ops, BPF_TAB_MASK);
}

static void __exit bpf_cleanup_module(void)
{
	tcf_unregister_action(&act_bpf_ops);
}

module_init(bpf_init_module);
module_exit(bpf_cleanup_module);

MODULE_AUTHOR("Jiri Pirko <jiri@resnulli.us>");
MODULE_DESCRIPTION("TC BPF based action");
MODULE_LICENSE("GPL v2");
