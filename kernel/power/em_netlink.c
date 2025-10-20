// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Generic netlink for energy model.
 *
 * Copyright (c) 2025 Valve Corporation.
 * Author: Changwoo Min <changwoo@igalia.com>
 */

#define pr_fmt(fmt) "energy_model: " fmt

#include <linux/energy_model.h>
#include <net/sock.h>
#include <net/genetlink.h>
#include <uapi/linux/energy_model.h>

#include "em_netlink.h"
#include "em_netlink_autogen.h"

#define EM_A_PD_CPUS_LEN		256

/*************************** Command encoding ********************************/
static int __em_nl_get_pd_size(struct em_perf_domain *pd, void *data)
{
	char cpus_buf[EM_A_PD_CPUS_LEN];
	int *tot_msg_sz = data;
	int msg_sz, cpus_sz;

	cpus_sz = snprintf(cpus_buf, sizeof(cpus_buf), "%*pb",
			   cpumask_pr_args(to_cpumask(pd->cpus)));

	msg_sz = nla_total_size(0) +			/* EM_A_PDS_PD */
		 nla_total_size(sizeof(u32)) +		/* EM_A_PD_PD_ID */
		 nla_total_size_64bit(sizeof(u64)) +	/* EM_A_PD_FLAGS */
		 nla_total_size(cpus_sz);		/* EM_A_PD_CPUS */

	*tot_msg_sz += nlmsg_total_size(genlmsg_msg_size(msg_sz));
	return 0;
}

static int __em_nl_get_pd(struct em_perf_domain *pd, void *data)
{
	char cpus_buf[EM_A_PD_CPUS_LEN];
	struct sk_buff *msg = data;
	struct nlattr *entry;

	entry = nla_nest_start(msg, EM_A_PDS_PD);
	if (!entry)
		goto out_cancel_nest;

	if (nla_put_u32(msg, EM_A_PD_PD_ID, pd->id))
		goto out_cancel_nest;

	if (nla_put_u64_64bit(msg, EM_A_PD_FLAGS, pd->flags, EM_A_PD_PAD))
		goto out_cancel_nest;

	snprintf(cpus_buf, sizeof(cpus_buf), "%*pb",
		 cpumask_pr_args(to_cpumask(pd->cpus)));
	if (nla_put_string(msg, EM_A_PD_CPUS, cpus_buf))
		goto out_cancel_nest;

	nla_nest_end(msg, entry);

	return 0;

out_cancel_nest:
	nla_nest_cancel(msg, entry);

	return -EMSGSIZE;
}

int em_nl_get_pds_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	void *hdr;
	int cmd = info->genlhdr->cmd;
	int ret = -EMSGSIZE, msg_sz = 0;

	for_each_em_perf_domain(__em_nl_get_pd_size, &msg_sz);

	msg = genlmsg_new(msg_sz, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put_reply(msg, info, &em_nl_family, 0, cmd);
	if (!hdr)
		goto out_free_msg;

	ret = for_each_em_perf_domain(__em_nl_get_pd, msg);
	if (ret)
		goto out_cancel_msg;

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

out_cancel_msg:
	genlmsg_cancel(msg, hdr);
out_free_msg:
	nlmsg_free(msg);

	return ret;
}

int em_nl_get_pd_table_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

static int __init em_netlink_init(void)
{
	return genl_register_family(&em_nl_family);
}
postcore_initcall(em_netlink_init);
