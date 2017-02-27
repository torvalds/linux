/* Copyright (c) 2016 Thomas Graf <tgraf@tgraf.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/bpf.h>
#include <net/lwtunnel.h>

struct bpf_lwt_prog {
	struct bpf_prog *prog;
	char *name;
};

struct bpf_lwt {
	struct bpf_lwt_prog in;
	struct bpf_lwt_prog out;
	struct bpf_lwt_prog xmit;
	int family;
};

#define MAX_PROG_NAME 256

static inline struct bpf_lwt *bpf_lwt_lwtunnel(struct lwtunnel_state *lwt)
{
	return (struct bpf_lwt *)lwt->data;
}

#define NO_REDIRECT false
#define CAN_REDIRECT true

static int run_lwt_bpf(struct sk_buff *skb, struct bpf_lwt_prog *lwt,
		       struct dst_entry *dst, bool can_redirect)
{
	int ret;

	/* Preempt disable is needed to protect per-cpu redirect_info between
	 * BPF prog and skb_do_redirect(). The call_rcu in bpf_prog_put() and
	 * access to maps strictly require a rcu_read_lock() for protection,
	 * mixing with BH RCU lock doesn't work.
	 */
	preempt_disable();
	rcu_read_lock();
	bpf_compute_data_end(skb);
	ret = bpf_prog_run_save_cb(lwt->prog, skb);
	rcu_read_unlock();

	switch (ret) {
	case BPF_OK:
		break;

	case BPF_REDIRECT:
		if (unlikely(!can_redirect)) {
			pr_warn_once("Illegal redirect return code in prog %s\n",
				     lwt->name ? : "<unknown>");
			ret = BPF_OK;
		} else {
			ret = skb_do_redirect(skb);
			if (ret == 0)
				ret = BPF_REDIRECT;
		}
		break;

	case BPF_DROP:
		kfree_skb(skb);
		ret = -EPERM;
		break;

	default:
		pr_warn_once("bpf-lwt: Illegal return value %u, expect packet loss\n", ret);
		kfree_skb(skb);
		ret = -EINVAL;
		break;
	}

	preempt_enable();

	return ret;
}

static int bpf_input(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct bpf_lwt *bpf;
	int ret;

	bpf = bpf_lwt_lwtunnel(dst->lwtstate);
	if (bpf->in.prog) {
		ret = run_lwt_bpf(skb, &bpf->in, dst, NO_REDIRECT);
		if (ret < 0)
			return ret;
	}

	if (unlikely(!dst->lwtstate->orig_input)) {
		pr_warn_once("orig_input not set on dst for prog %s\n",
			     bpf->out.name);
		kfree_skb(skb);
		return -EINVAL;
	}

	return dst->lwtstate->orig_input(skb);
}

static int bpf_output(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct bpf_lwt *bpf;
	int ret;

	bpf = bpf_lwt_lwtunnel(dst->lwtstate);
	if (bpf->out.prog) {
		ret = run_lwt_bpf(skb, &bpf->out, dst, NO_REDIRECT);
		if (ret < 0)
			return ret;
	}

	if (unlikely(!dst->lwtstate->orig_output)) {
		pr_warn_once("orig_output not set on dst for prog %s\n",
			     bpf->out.name);
		kfree_skb(skb);
		return -EINVAL;
	}

	return dst->lwtstate->orig_output(net, sk, skb);
}

static int xmit_check_hhlen(struct sk_buff *skb)
{
	int hh_len = skb_dst(skb)->dev->hard_header_len;

	if (skb_headroom(skb) < hh_len) {
		int nhead = HH_DATA_ALIGN(hh_len - skb_headroom(skb));

		if (pskb_expand_head(skb, nhead, 0, GFP_ATOMIC))
			return -ENOMEM;
	}

	return 0;
}

static int bpf_xmit(struct sk_buff *skb)
{
	struct dst_entry *dst = skb_dst(skb);
	struct bpf_lwt *bpf;

	bpf = bpf_lwt_lwtunnel(dst->lwtstate);
	if (bpf->xmit.prog) {
		int ret;

		ret = run_lwt_bpf(skb, &bpf->xmit, dst, CAN_REDIRECT);
		switch (ret) {
		case BPF_OK:
			/* If the header was expanded, headroom might be too
			 * small for L2 header to come, expand as needed.
			 */
			ret = xmit_check_hhlen(skb);
			if (unlikely(ret))
				return ret;

			return LWTUNNEL_XMIT_CONTINUE;
		case BPF_REDIRECT:
			return LWTUNNEL_XMIT_DONE;
		default:
			return ret;
		}
	}

	return LWTUNNEL_XMIT_CONTINUE;
}

static void bpf_lwt_prog_destroy(struct bpf_lwt_prog *prog)
{
	if (prog->prog)
		bpf_prog_put(prog->prog);

	kfree(prog->name);
}

static void bpf_destroy_state(struct lwtunnel_state *lwt)
{
	struct bpf_lwt *bpf = bpf_lwt_lwtunnel(lwt);

	bpf_lwt_prog_destroy(&bpf->in);
	bpf_lwt_prog_destroy(&bpf->out);
	bpf_lwt_prog_destroy(&bpf->xmit);
}

static const struct nla_policy bpf_prog_policy[LWT_BPF_PROG_MAX + 1] = {
	[LWT_BPF_PROG_FD]   = { .type = NLA_U32, },
	[LWT_BPF_PROG_NAME] = { .type = NLA_NUL_STRING,
				.len = MAX_PROG_NAME },
};

static int bpf_parse_prog(struct nlattr *attr, struct bpf_lwt_prog *prog,
			  enum bpf_prog_type type)
{
	struct nlattr *tb[LWT_BPF_PROG_MAX + 1];
	struct bpf_prog *p;
	int ret;
	u32 fd;

	ret = nla_parse_nested(tb, LWT_BPF_PROG_MAX, attr, bpf_prog_policy);
	if (ret < 0)
		return ret;

	if (!tb[LWT_BPF_PROG_FD] || !tb[LWT_BPF_PROG_NAME])
		return -EINVAL;

	prog->name = nla_memdup(tb[LWT_BPF_PROG_NAME], GFP_KERNEL);
	if (!prog->name)
		return -ENOMEM;

	fd = nla_get_u32(tb[LWT_BPF_PROG_FD]);
	p = bpf_prog_get_type(fd, type);
	if (IS_ERR(p))
		return PTR_ERR(p);

	prog->prog = p;

	return 0;
}

static const struct nla_policy bpf_nl_policy[LWT_BPF_MAX + 1] = {
	[LWT_BPF_IN]		= { .type = NLA_NESTED, },
	[LWT_BPF_OUT]		= { .type = NLA_NESTED, },
	[LWT_BPF_XMIT]		= { .type = NLA_NESTED, },
	[LWT_BPF_XMIT_HEADROOM]	= { .type = NLA_U32 },
};

static int bpf_build_state(struct net_device *dev, struct nlattr *nla,
			   unsigned int family, const void *cfg,
			   struct lwtunnel_state **ts)
{
	struct nlattr *tb[LWT_BPF_MAX + 1];
	struct lwtunnel_state *newts;
	struct bpf_lwt *bpf;
	int ret;

	if (family != AF_INET && family != AF_INET6)
		return -EAFNOSUPPORT;

	ret = nla_parse_nested(tb, LWT_BPF_MAX, nla, bpf_nl_policy);
	if (ret < 0)
		return ret;

	if (!tb[LWT_BPF_IN] && !tb[LWT_BPF_OUT] && !tb[LWT_BPF_XMIT])
		return -EINVAL;

	newts = lwtunnel_state_alloc(sizeof(*bpf));
	if (!newts)
		return -ENOMEM;

	newts->type = LWTUNNEL_ENCAP_BPF;
	bpf = bpf_lwt_lwtunnel(newts);

	if (tb[LWT_BPF_IN]) {
		newts->flags |= LWTUNNEL_STATE_INPUT_REDIRECT;
		ret = bpf_parse_prog(tb[LWT_BPF_IN], &bpf->in,
				     BPF_PROG_TYPE_LWT_IN);
		if (ret  < 0)
			goto errout;
	}

	if (tb[LWT_BPF_OUT]) {
		newts->flags |= LWTUNNEL_STATE_OUTPUT_REDIRECT;
		ret = bpf_parse_prog(tb[LWT_BPF_OUT], &bpf->out,
				     BPF_PROG_TYPE_LWT_OUT);
		if (ret < 0)
			goto errout;
	}

	if (tb[LWT_BPF_XMIT]) {
		newts->flags |= LWTUNNEL_STATE_XMIT_REDIRECT;
		ret = bpf_parse_prog(tb[LWT_BPF_XMIT], &bpf->xmit,
				     BPF_PROG_TYPE_LWT_XMIT);
		if (ret < 0)
			goto errout;
	}

	if (tb[LWT_BPF_XMIT_HEADROOM]) {
		u32 headroom = nla_get_u32(tb[LWT_BPF_XMIT_HEADROOM]);

		if (headroom > LWT_BPF_MAX_HEADROOM) {
			ret = -ERANGE;
			goto errout;
		}

		newts->headroom = headroom;
	}

	bpf->family = family;
	*ts = newts;

	return 0;

errout:
	bpf_destroy_state(newts);
	kfree(newts);
	return ret;
}

static int bpf_fill_lwt_prog(struct sk_buff *skb, int attr,
			     struct bpf_lwt_prog *prog)
{
	struct nlattr *nest;

	if (!prog->prog)
		return 0;

	nest = nla_nest_start(skb, attr);
	if (!nest)
		return -EMSGSIZE;

	if (prog->name &&
	    nla_put_string(skb, LWT_BPF_PROG_NAME, prog->name))
		return -EMSGSIZE;

	return nla_nest_end(skb, nest);
}

static int bpf_fill_encap_info(struct sk_buff *skb, struct lwtunnel_state *lwt)
{
	struct bpf_lwt *bpf = bpf_lwt_lwtunnel(lwt);

	if (bpf_fill_lwt_prog(skb, LWT_BPF_IN, &bpf->in) < 0 ||
	    bpf_fill_lwt_prog(skb, LWT_BPF_OUT, &bpf->out) < 0 ||
	    bpf_fill_lwt_prog(skb, LWT_BPF_XMIT, &bpf->xmit) < 0)
		return -EMSGSIZE;

	return 0;
}

static int bpf_encap_nlsize(struct lwtunnel_state *lwtstate)
{
	int nest_len = nla_total_size(sizeof(struct nlattr)) +
		       nla_total_size(MAX_PROG_NAME) + /* LWT_BPF_PROG_NAME */
		       0;

	return nest_len + /* LWT_BPF_IN */
	       nest_len + /* LWT_BPF_OUT */
	       nest_len + /* LWT_BPF_XMIT */
	       0;
}

int bpf_lwt_prog_cmp(struct bpf_lwt_prog *a, struct bpf_lwt_prog *b)
{
	/* FIXME:
	 * The LWT state is currently rebuilt for delete requests which
	 * results in a new bpf_prog instance. Comparing names for now.
	 */
	if (!a->name && !b->name)
		return 0;

	if (!a->name || !b->name)
		return 1;

	return strcmp(a->name, b->name);
}

static int bpf_encap_cmp(struct lwtunnel_state *a, struct lwtunnel_state *b)
{
	struct bpf_lwt *a_bpf = bpf_lwt_lwtunnel(a);
	struct bpf_lwt *b_bpf = bpf_lwt_lwtunnel(b);

	return bpf_lwt_prog_cmp(&a_bpf->in, &b_bpf->in) ||
	       bpf_lwt_prog_cmp(&a_bpf->out, &b_bpf->out) ||
	       bpf_lwt_prog_cmp(&a_bpf->xmit, &b_bpf->xmit);
}

static const struct lwtunnel_encap_ops bpf_encap_ops = {
	.build_state	= bpf_build_state,
	.destroy_state	= bpf_destroy_state,
	.input		= bpf_input,
	.output		= bpf_output,
	.xmit		= bpf_xmit,
	.fill_encap	= bpf_fill_encap_info,
	.get_encap_size = bpf_encap_nlsize,
	.cmp_encap	= bpf_encap_cmp,
	.owner		= THIS_MODULE,
};

static int __init bpf_lwt_init(void)
{
	return lwtunnel_encap_add_ops(&bpf_encap_ops, LWTUNNEL_ENCAP_BPF);
}

subsys_initcall(bpf_lwt_init)
