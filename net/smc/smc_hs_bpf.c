// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Shared Memory Communications over RDMA (SMC-R) and RoCE
 *
 *  Generic hook for SMC handshake flow.
 *
 *  Copyright IBM Corp. 2016
 *  Copyright (c) 2025, Alibaba Inc.
 *
 *  Author: D. Wythe <alibuda@linux.alibaba.com>
 */

#include <linux/bpf_verifier.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/rculist.h>

#include "smc_hs_bpf.h"

static DEFINE_SPINLOCK(smc_hs_ctrl_list_lock);
static LIST_HEAD(smc_hs_ctrl_list);

static int smc_hs_ctrl_reg(struct smc_hs_ctrl *ctrl)
{
	int ret = 0;

	spin_lock(&smc_hs_ctrl_list_lock);
	/* already exist or duplicate name */
	if (smc_hs_ctrl_find_by_name(ctrl->name))
		ret = -EEXIST;
	else
		list_add_tail_rcu(&ctrl->list, &smc_hs_ctrl_list);
	spin_unlock(&smc_hs_ctrl_list_lock);
	return ret;
}

static void smc_hs_ctrl_unreg(struct smc_hs_ctrl *ctrl)
{
	spin_lock(&smc_hs_ctrl_list_lock);
	list_del_rcu(&ctrl->list);
	spin_unlock(&smc_hs_ctrl_list_lock);

	/* Ensure that all readers to complete */
	synchronize_rcu();
}

struct smc_hs_ctrl *smc_hs_ctrl_find_by_name(const char *name)
{
	struct smc_hs_ctrl *ctrl;

	list_for_each_entry_rcu(ctrl, &smc_hs_ctrl_list, list) {
		if (strcmp(ctrl->name, name) == 0)
			return ctrl;
	}
	return NULL;
}

static int __smc_bpf_stub_set_tcp_option(struct tcp_sock *tp) { return 1; }
static int __smc_bpf_stub_set_tcp_option_cond(const struct tcp_sock *tp,
					      struct inet_request_sock *ireq)
{
	return 1;
}

static struct smc_hs_ctrl __smc_bpf_hs_ctrl = {
	.syn_option	= __smc_bpf_stub_set_tcp_option,
	.synack_option	= __smc_bpf_stub_set_tcp_option_cond,
};

static int smc_bpf_hs_ctrl_init(struct btf *btf) { return 0; }

static int smc_bpf_hs_ctrl_reg(void *kdata, struct bpf_link *link)
{
	if (link)
		return -EOPNOTSUPP;

	return smc_hs_ctrl_reg(kdata);
}

static void smc_bpf_hs_ctrl_unreg(void *kdata, struct bpf_link *link)
{
	smc_hs_ctrl_unreg(kdata);
}

static int smc_bpf_hs_ctrl_init_member(const struct btf_type *t,
				       const struct btf_member *member,
				       void *kdata, const void *udata)
{
	const struct smc_hs_ctrl *u_ctrl;
	struct smc_hs_ctrl *k_ctrl;
	u32 moff;

	u_ctrl = (const struct smc_hs_ctrl *)udata;
	k_ctrl = (struct smc_hs_ctrl *)kdata;

	moff = __btf_member_bit_offset(t, member) / 8;
	switch (moff) {
	case offsetof(struct smc_hs_ctrl, name):
		if (bpf_obj_name_cpy(k_ctrl->name, u_ctrl->name,
				     sizeof(u_ctrl->name)) <= 0)
			return -EINVAL;
		return 1;
	case offsetof(struct smc_hs_ctrl, flags):
		if (u_ctrl->flags & ~SMC_HS_CTRL_ALL_FLAGS)
			return -EINVAL;
		k_ctrl->flags = u_ctrl->flags;
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct bpf_func_proto *
bpf_smc_hs_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog)
{
	return bpf_base_func_proto(func_id, prog);
}

static const struct bpf_verifier_ops smc_bpf_verifier_ops = {
	.get_func_proto		= bpf_smc_hs_func_proto,
	.is_valid_access	= bpf_tracing_btf_ctx_access,
};

static struct bpf_struct_ops bpf_smc_hs_ctrl_ops = {
	.name		= "smc_hs_ctrl",
	.init		= smc_bpf_hs_ctrl_init,
	.reg		= smc_bpf_hs_ctrl_reg,
	.unreg		= smc_bpf_hs_ctrl_unreg,
	.cfi_stubs	= &__smc_bpf_hs_ctrl,
	.verifier_ops	= &smc_bpf_verifier_ops,
	.init_member	= smc_bpf_hs_ctrl_init_member,
	.owner		= THIS_MODULE,
};

int bpf_smc_hs_ctrl_init(void)
{
	return register_bpf_struct_ops(&bpf_smc_hs_ctrl_ops, smc_hs_ctrl);
}
