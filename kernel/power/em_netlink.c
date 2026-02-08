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
#include <uapi/linux/dev_energymodel.h>

#include "em_netlink.h"
#include "em_netlink_autogen.h"

/*************************** Command encoding ********************************/
struct dump_ctx {
	int idx;
	int start;
	struct sk_buff *skb;
	struct netlink_callback *cb;
};

static int __em_nl_get_pd_size(struct em_perf_domain *pd, void *data)
{
	int nr_cpus, msg_sz, cpus_sz;
	int *tot_msg_sz = data;

	nr_cpus = cpumask_weight(to_cpumask(pd->cpus));
	cpus_sz = nla_total_size_64bit(sizeof(u64)) * nr_cpus;

	msg_sz = nla_total_size(0) +
		 /* DEV_ENERGYMODEL_A_PERF_DOMAINS_PERF_DOMAIN */
		 nla_total_size(sizeof(u32)) +
		 /* DEV_ENERGYMODEL_A_PERF_DOMAIN_PERF_DOMAIN_ID */
		 nla_total_size_64bit(sizeof(u64)) +
		 /* DEV_ENERGYMODEL_A_PERF_DOMAIN_FLAGS */
		 nla_total_size(cpus_sz);
		 /* DEV_ENERGYMODEL_A_PERF_DOMAIN_CPUS */

	*tot_msg_sz += nlmsg_total_size(genlmsg_msg_size(msg_sz));
	return 0;
}

static int __em_nl_get_pd(struct em_perf_domain *pd, void *data)
{
	struct sk_buff *msg = data;
	struct cpumask *cpumask;
	int cpu;

	if (nla_put_u32(msg, DEV_ENERGYMODEL_A_PERF_DOMAIN_PERF_DOMAIN_ID,
			pd->id))
		goto out_cancel_nest;

	if (nla_put_u64_64bit(msg, DEV_ENERGYMODEL_A_PERF_DOMAIN_FLAGS,
			      pd->flags, DEV_ENERGYMODEL_A_PERF_DOMAIN_PAD))
		goto out_cancel_nest;

	cpumask = to_cpumask(pd->cpus);
	for_each_cpu(cpu, cpumask) {
		if (nla_put_u64_64bit(msg, DEV_ENERGYMODEL_A_PERF_DOMAIN_CPUS,
				      cpu, DEV_ENERGYMODEL_A_PERF_DOMAIN_PAD))
			goto out_cancel_nest;
	}

	return 0;

out_cancel_nest:
	return -EMSGSIZE;
}

static int __em_nl_get_pd_for_dump(struct em_perf_domain *pd, void *data)
{
	const struct genl_info *info;
	struct dump_ctx *ctx = data;
	void *hdr;
	int ret;

	if (ctx->idx++ < ctx->start)
		return 0;

	info = genl_info_dump(ctx->cb);
	hdr = genlmsg_iput(ctx->skb, info);
	if (!hdr) {
		genlmsg_cancel(ctx->skb, hdr);
		return -EMSGSIZE;
	}

	ret = __em_nl_get_pd(pd, ctx->skb);
	genlmsg_end(ctx->skb, hdr);
	return ret;
}

int dev_energymodel_nl_get_perf_domains_doit(struct sk_buff *skb,
					      struct genl_info *info)
{
	int id, ret = -EMSGSIZE, msg_sz = 0;
	int cmd = info->genlhdr->cmd;
	struct em_perf_domain *pd;
	struct sk_buff *msg;
	void *hdr;

	if (!info->attrs[DEV_ENERGYMODEL_A_PERF_DOMAIN_PERF_DOMAIN_ID])
		return -EINVAL;

	id = nla_get_u32(info->attrs[DEV_ENERGYMODEL_A_PERF_DOMAIN_PERF_DOMAIN_ID]);
	pd = em_perf_domain_get_by_id(id);

	__em_nl_get_pd_size(pd, &msg_sz);
	msg = genlmsg_new(msg_sz, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put_reply(msg, info, &dev_energymodel_nl_family, 0, cmd);
	if (!hdr)
		goto out_free_msg;

	ret = __em_nl_get_pd(pd, msg);
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

int dev_energymodel_nl_get_perf_domains_dumpit(struct sk_buff *skb,
						struct netlink_callback *cb)
{
	struct dump_ctx ctx = {
		.idx = 0,
		.start = cb->args[0],
		.skb = skb,
		.cb = cb,
	};

	return for_each_em_perf_domain(__em_nl_get_pd_for_dump, &ctx);
}

static struct em_perf_domain *__em_nl_get_pd_table_id(struct nlattr **attrs)
{
	struct em_perf_domain *pd;
	int id;

	if (!attrs[DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID])
		return NULL;

	id = nla_get_u32(attrs[DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID]);
	pd = em_perf_domain_get_by_id(id);
	return pd;
}

static int __em_nl_get_pd_table_size(const struct em_perf_domain *pd)
{
	int id_sz, ps_sz;

	id_sz = nla_total_size(sizeof(u32));
		/* DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID */
	ps_sz = nla_total_size(0) +
		/* DEV_ENERGYMODEL_A_PERF_TABLE_PERF_STATE */
		nla_total_size_64bit(sizeof(u64)) +
		/* DEV_ENERGYMODEL_A_PERF_STATE_PERFORMANCE */
		nla_total_size_64bit(sizeof(u64)) +
		/* DEV_ENERGYMODEL_A_PERF_STATE_FREQUENCY */
		nla_total_size_64bit(sizeof(u64)) +
		/* DEV_ENERGYMODEL_A_PERF_STATE_POWER */
		nla_total_size_64bit(sizeof(u64)) +
		/* DEV_ENERGYMODEL_A_PERF_STATE_COST */
		nla_total_size_64bit(sizeof(u64));
		/* DEV_ENERGYMODEL_A_PERF_STATE_FLAGS */
	ps_sz *= pd->nr_perf_states;

	return nlmsg_total_size(genlmsg_msg_size(id_sz + ps_sz));
}

static
int __em_nl_get_pd_table(struct sk_buff *msg, const struct em_perf_domain *pd)
{
	struct em_perf_state *table, *ps;
	struct nlattr *entry;
	int i;

	if (nla_put_u32(msg, DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID,
			pd->id))
		goto out_err;

	rcu_read_lock();
	table = em_perf_state_from_pd((struct em_perf_domain *)pd);

	for (i = 0; i < pd->nr_perf_states; i++) {
		ps = &table[i];

		entry = nla_nest_start(msg,
				       DEV_ENERGYMODEL_A_PERF_TABLE_PERF_STATE);
		if (!entry)
			goto out_unlock_ps;

		if (nla_put_u64_64bit(msg,
				      DEV_ENERGYMODEL_A_PERF_STATE_PERFORMANCE,
				      ps->performance,
				      DEV_ENERGYMODEL_A_PERF_STATE_PAD))
			goto out_cancel_ps_nest;
		if (nla_put_u64_64bit(msg,
				      DEV_ENERGYMODEL_A_PERF_STATE_FREQUENCY,
				      ps->frequency,
				      DEV_ENERGYMODEL_A_PERF_STATE_PAD))
			goto out_cancel_ps_nest;
		if (nla_put_u64_64bit(msg,
				      DEV_ENERGYMODEL_A_PERF_STATE_POWER,
				      ps->power,
				      DEV_ENERGYMODEL_A_PERF_STATE_PAD))
			goto out_cancel_ps_nest;
		if (nla_put_u64_64bit(msg,
				      DEV_ENERGYMODEL_A_PERF_STATE_COST,
				      ps->cost,
				      DEV_ENERGYMODEL_A_PERF_STATE_PAD))
			goto out_cancel_ps_nest;
		if (nla_put_u64_64bit(msg,
				      DEV_ENERGYMODEL_A_PERF_STATE_FLAGS,
				      ps->flags,
				      DEV_ENERGYMODEL_A_PERF_STATE_PAD))
			goto out_cancel_ps_nest;

		nla_nest_end(msg, entry);
	}
	rcu_read_unlock();
	return 0;

out_cancel_ps_nest:
	nla_nest_cancel(msg, entry);
out_unlock_ps:
	rcu_read_unlock();
out_err:
	return -EMSGSIZE;
}

int dev_energymodel_nl_get_perf_table_doit(struct sk_buff *skb,
					    struct genl_info *info)
{
	int cmd = info->genlhdr->cmd;
	int msg_sz, ret = -EMSGSIZE;
	struct em_perf_domain *pd;
	struct sk_buff *msg;
	void *hdr;

	pd = __em_nl_get_pd_table_id(info->attrs);
	if (!pd)
		return -EINVAL;

	msg_sz = __em_nl_get_pd_table_size(pd);

	msg = genlmsg_new(msg_sz, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put_reply(msg, info, &dev_energymodel_nl_family, 0, cmd);
	if (!hdr)
		goto out_free_msg;

	ret = __em_nl_get_pd_table(msg, pd);
	if (ret)
		goto out_free_msg;

	genlmsg_end(msg, hdr);
	return genlmsg_reply(msg, info);

out_free_msg:
	nlmsg_free(msg);
	return ret;
}


/**************************** Event encoding *********************************/
static void __em_notify_pd_table(const struct em_perf_domain *pd, int ntf_type)
{
	struct sk_buff *msg;
	int msg_sz, ret = -EMSGSIZE;
	void *hdr;

	if (!genl_has_listeners(&dev_energymodel_nl_family, &init_net, DEV_ENERGYMODEL_NLGRP_EVENT))
		return;

	msg_sz = __em_nl_get_pd_table_size(pd);

	msg = genlmsg_new(msg_sz, GFP_KERNEL);
	if (!msg)
		return;

	hdr = genlmsg_put(msg, 0, 0, &dev_energymodel_nl_family, 0, ntf_type);
	if (!hdr)
		goto out_free_msg;

	ret = __em_nl_get_pd_table(msg, pd);
	if (ret)
		goto out_free_msg;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&dev_energymodel_nl_family, msg, 0,
			  DEV_ENERGYMODEL_NLGRP_EVENT, GFP_KERNEL);

	return;

out_free_msg:
	nlmsg_free(msg);
}

void em_notify_pd_created(const struct em_perf_domain *pd)
{
	__em_notify_pd_table(pd, DEV_ENERGYMODEL_CMD_PERF_DOMAIN_CREATED);
}

void em_notify_pd_updated(const struct em_perf_domain *pd)
{
	__em_notify_pd_table(pd, DEV_ENERGYMODEL_CMD_PERF_DOMAIN_UPDATED);
}

static int __em_notify_pd_deleted_size(const struct em_perf_domain *pd)
{
	int id_sz = nla_total_size(sizeof(u32)); /* DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID */

	return nlmsg_total_size(genlmsg_msg_size(id_sz));
}

void em_notify_pd_deleted(const struct em_perf_domain *pd)
{
	struct sk_buff *msg;
	void *hdr;
	int msg_sz;

	if (!genl_has_listeners(&dev_energymodel_nl_family, &init_net,
				DEV_ENERGYMODEL_NLGRP_EVENT))
		return;

	msg_sz = __em_notify_pd_deleted_size(pd);

	msg = genlmsg_new(msg_sz, GFP_KERNEL);
	if (!msg)
		return;

	hdr = genlmsg_put(msg, 0, 0, &dev_energymodel_nl_family, 0,
			  DEV_ENERGYMODEL_CMD_PERF_DOMAIN_DELETED);
	if (!hdr)
		goto out_free_msg;

	if (nla_put_u32(msg, DEV_ENERGYMODEL_A_PERF_TABLE_PERF_DOMAIN_ID,
			pd->id))
		goto out_free_msg;

	genlmsg_end(msg, hdr);

	genlmsg_multicast(&dev_energymodel_nl_family, msg, 0,
			  DEV_ENERGYMODEL_NLGRP_EVENT, GFP_KERNEL);

	return;

out_free_msg:
	nlmsg_free(msg);
}

/**************************** Initialization *********************************/
static int __init em_netlink_init(void)
{
	return genl_register_family(&dev_energymodel_nl_family);
}
postcore_initcall(em_netlink_init);
