/*
 * net/psample/psample.c - Netlink channel for packet sampling
 * Copyright (c) 2017 Yotam Gigi <yotamg@mellanox.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/psample.h>
#include <linux/spinlock.h>

#define PSAMPLE_MAX_PACKET_SIZE 0xffff

static LIST_HEAD(psample_groups_list);
static DEFINE_SPINLOCK(psample_groups_lock);

/* multicast groups */
enum psample_nl_multicast_groups {
	PSAMPLE_NL_MCGRP_CONFIG,
	PSAMPLE_NL_MCGRP_SAMPLE,
};

static const struct genl_multicast_group psample_nl_mcgrps[] = {
	[PSAMPLE_NL_MCGRP_CONFIG] = { .name = PSAMPLE_NL_MCGRP_CONFIG_NAME },
	[PSAMPLE_NL_MCGRP_SAMPLE] = { .name = PSAMPLE_NL_MCGRP_SAMPLE_NAME },
};

static struct genl_family psample_nl_family __ro_after_init;

static int psample_group_nl_fill(struct sk_buff *msg,
				 struct psample_group *group,
				 enum psample_command cmd, u32 portid, u32 seq,
				 int flags)
{
	void *hdr;
	int ret;

	hdr = genlmsg_put(msg, portid, seq, &psample_nl_family, flags, cmd);
	if (!hdr)
		return -EMSGSIZE;

	ret = nla_put_u32(msg, PSAMPLE_ATTR_SAMPLE_GROUP, group->group_num);
	if (ret < 0)
		goto error;

	ret = nla_put_u32(msg, PSAMPLE_ATTR_GROUP_REFCOUNT, group->refcount);
	if (ret < 0)
		goto error;

	ret = nla_put_u32(msg, PSAMPLE_ATTR_GROUP_SEQ, group->seq);
	if (ret < 0)
		goto error;

	genlmsg_end(msg, hdr);
	return 0;

error:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

static int psample_nl_cmd_get_group_dumpit(struct sk_buff *msg,
					   struct netlink_callback *cb)
{
	struct psample_group *group;
	int start = cb->args[0];
	int idx = 0;
	int err;

	spin_lock(&psample_groups_lock);
	list_for_each_entry(group, &psample_groups_list, list) {
		if (!net_eq(group->net, sock_net(msg->sk)))
			continue;
		if (idx < start) {
			idx++;
			continue;
		}
		err = psample_group_nl_fill(msg, group, PSAMPLE_CMD_NEW_GROUP,
					    NETLINK_CB(cb->skb).portid,
					    cb->nlh->nlmsg_seq, NLM_F_MULTI);
		if (err)
			break;
		idx++;
	}

	spin_unlock(&psample_groups_lock);
	cb->args[0] = idx;
	return msg->len;
}

static const struct genl_ops psample_nl_ops[] = {
	{
		.cmd = PSAMPLE_CMD_GET_GROUP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.dumpit = psample_nl_cmd_get_group_dumpit,
		/* can be retrieved by unprivileged users */
	}
};

static struct genl_family psample_nl_family __ro_after_init = {
	.name		= PSAMPLE_GENL_NAME,
	.version	= PSAMPLE_GENL_VERSION,
	.maxattr	= PSAMPLE_ATTR_MAX,
	.netnsok	= true,
	.module		= THIS_MODULE,
	.mcgrps		= psample_nl_mcgrps,
	.ops		= psample_nl_ops,
	.n_ops		= ARRAY_SIZE(psample_nl_ops),
	.n_mcgrps	= ARRAY_SIZE(psample_nl_mcgrps),
};

static void psample_group_notify(struct psample_group *group,
				 enum psample_command cmd)
{
	struct sk_buff *msg;
	int err;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return;

	err = psample_group_nl_fill(msg, group, cmd, 0, 0, NLM_F_MULTI);
	if (!err)
		genlmsg_multicast_netns(&psample_nl_family, group->net, msg, 0,
					PSAMPLE_NL_MCGRP_CONFIG, GFP_ATOMIC);
	else
		nlmsg_free(msg);
}

static struct psample_group *psample_group_create(struct net *net,
						  u32 group_num)
{
	struct psample_group *group;

	group = kzalloc(sizeof(*group), GFP_ATOMIC);
	if (!group)
		return NULL;

	group->net = net;
	group->group_num = group_num;
	list_add_tail(&group->list, &psample_groups_list);

	psample_group_notify(group, PSAMPLE_CMD_NEW_GROUP);
	return group;
}

static void psample_group_destroy(struct psample_group *group)
{
	psample_group_notify(group, PSAMPLE_CMD_DEL_GROUP);
	list_del(&group->list);
	kfree(group);
}

static struct psample_group *
psample_group_lookup(struct net *net, u32 group_num)
{
	struct psample_group *group;

	list_for_each_entry(group, &psample_groups_list, list)
		if ((group->group_num == group_num) && (group->net == net))
			return group;
	return NULL;
}

struct psample_group *psample_group_get(struct net *net, u32 group_num)
{
	struct psample_group *group;

	spin_lock(&psample_groups_lock);

	group = psample_group_lookup(net, group_num);
	if (!group) {
		group = psample_group_create(net, group_num);
		if (!group)
			goto out;
	}
	group->refcount++;

out:
	spin_unlock(&psample_groups_lock);
	return group;
}
EXPORT_SYMBOL_GPL(psample_group_get);

void psample_group_put(struct psample_group *group)
{
	spin_lock(&psample_groups_lock);

	if (--group->refcount == 0)
		psample_group_destroy(group);

	spin_unlock(&psample_groups_lock);
}
EXPORT_SYMBOL_GPL(psample_group_put);

void psample_sample_packet(struct psample_group *group, struct sk_buff *skb,
			   u32 trunc_size, int in_ifindex, int out_ifindex,
			   u32 sample_rate)
{
	struct sk_buff *nl_skb;
	int data_len;
	int meta_len;
	void *data;
	int ret;

	meta_len = (in_ifindex ? nla_total_size(sizeof(u16)) : 0) +
		   (out_ifindex ? nla_total_size(sizeof(u16)) : 0) +
		   nla_total_size(sizeof(u32)) +	/* sample_rate */
		   nla_total_size(sizeof(u32)) +	/* orig_size */
		   nla_total_size(sizeof(u32)) +	/* group_num */
		   nla_total_size(sizeof(u32));		/* seq */

	data_len = min(skb->len, trunc_size);
	if (meta_len + nla_total_size(data_len) > PSAMPLE_MAX_PACKET_SIZE)
		data_len = PSAMPLE_MAX_PACKET_SIZE - meta_len - NLA_HDRLEN
			    - NLA_ALIGNTO;

	nl_skb = genlmsg_new(meta_len + data_len, GFP_ATOMIC);
	if (unlikely(!nl_skb))
		return;

	data = genlmsg_put(nl_skb, 0, 0, &psample_nl_family, 0,
			   PSAMPLE_CMD_SAMPLE);
	if (unlikely(!data))
		goto error;

	if (in_ifindex) {
		ret = nla_put_u16(nl_skb, PSAMPLE_ATTR_IIFINDEX, in_ifindex);
		if (unlikely(ret < 0))
			goto error;
	}

	if (out_ifindex) {
		ret = nla_put_u16(nl_skb, PSAMPLE_ATTR_OIFINDEX, out_ifindex);
		if (unlikely(ret < 0))
			goto error;
	}

	ret = nla_put_u32(nl_skb, PSAMPLE_ATTR_SAMPLE_RATE, sample_rate);
	if (unlikely(ret < 0))
		goto error;

	ret = nla_put_u32(nl_skb, PSAMPLE_ATTR_ORIGSIZE, skb->len);
	if (unlikely(ret < 0))
		goto error;

	ret = nla_put_u32(nl_skb, PSAMPLE_ATTR_SAMPLE_GROUP, group->group_num);
	if (unlikely(ret < 0))
		goto error;

	ret = nla_put_u32(nl_skb, PSAMPLE_ATTR_GROUP_SEQ, group->seq++);
	if (unlikely(ret < 0))
		goto error;

	if (data_len) {
		int nla_len = nla_total_size(data_len);
		struct nlattr *nla;

		nla = skb_put(nl_skb, nla_len);
		nla->nla_type = PSAMPLE_ATTR_DATA;
		nla->nla_len = nla_attr_size(data_len);

		if (skb_copy_bits(skb, 0, nla_data(nla), data_len))
			goto error;
	}

	genlmsg_end(nl_skb, data);
	genlmsg_multicast_netns(&psample_nl_family, group->net, nl_skb, 0,
				PSAMPLE_NL_MCGRP_SAMPLE, GFP_ATOMIC);

	return;
error:
	pr_err_ratelimited("Could not create psample log message\n");
	nlmsg_free(nl_skb);
}
EXPORT_SYMBOL_GPL(psample_sample_packet);

static int __init psample_module_init(void)
{
	return genl_register_family(&psample_nl_family);
}

static void __exit psample_module_exit(void)
{
	genl_unregister_family(&psample_nl_family);
}

module_init(psample_module_init);
module_exit(psample_module_exit);

MODULE_AUTHOR("Yotam Gigi <yotam.gi@gmail.com>");
MODULE_DESCRIPTION("netlink channel for packet sampling");
MODULE_LICENSE("GPL v2");
