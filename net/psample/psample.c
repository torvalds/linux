// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/psample/psample.c - Netlink channel for packet sampling
 * Copyright (c) 2017 Yotam Gigi <yotamg@mellanox.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/timekeeping.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <net/psample.h>
#include <linux/spinlock.h>
#include <net/ip_tunnels.h>
#include <net/dst_metadata.h>

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
	[PSAMPLE_NL_MCGRP_SAMPLE] = { .name = PSAMPLE_NL_MCGRP_SAMPLE_NAME,
				      .flags = GENL_MCAST_CAP_NET_ADMIN, },
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

	spin_lock_bh(&psample_groups_lock);
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

	spin_unlock_bh(&psample_groups_lock);
	cb->args[0] = idx;
	return msg->len;
}

static const struct genl_small_ops psample_nl_ops[] = {
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
	.small_ops	= psample_nl_ops,
	.n_small_ops	= ARRAY_SIZE(psample_nl_ops),
	.resv_start_op	= PSAMPLE_CMD_GET_GROUP + 1,
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
	kfree_rcu(group, rcu);
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

	spin_lock_bh(&psample_groups_lock);

	group = psample_group_lookup(net, group_num);
	if (!group) {
		group = psample_group_create(net, group_num);
		if (!group)
			goto out;
	}
	group->refcount++;

out:
	spin_unlock_bh(&psample_groups_lock);
	return group;
}
EXPORT_SYMBOL_GPL(psample_group_get);

void psample_group_take(struct psample_group *group)
{
	spin_lock_bh(&psample_groups_lock);
	group->refcount++;
	spin_unlock_bh(&psample_groups_lock);
}
EXPORT_SYMBOL_GPL(psample_group_take);

void psample_group_put(struct psample_group *group)
{
	spin_lock_bh(&psample_groups_lock);

	if (--group->refcount == 0)
		psample_group_destroy(group);

	spin_unlock_bh(&psample_groups_lock);
}
EXPORT_SYMBOL_GPL(psample_group_put);

#ifdef CONFIG_INET
static int __psample_ip_tun_to_nlattr(struct sk_buff *skb,
			      struct ip_tunnel_info *tun_info)
{
	unsigned short tun_proto = ip_tunnel_info_af(tun_info);
	const void *tun_opts = ip_tunnel_info_opts(tun_info);
	const struct ip_tunnel_key *tun_key = &tun_info->key;
	int tun_opts_len = tun_info->options_len;

	if (test_bit(IP_TUNNEL_KEY_BIT, tun_key->tun_flags) &&
	    nla_put_be64(skb, PSAMPLE_TUNNEL_KEY_ATTR_ID, tun_key->tun_id,
			 PSAMPLE_TUNNEL_KEY_ATTR_PAD))
		return -EMSGSIZE;

	if (tun_info->mode & IP_TUNNEL_INFO_BRIDGE &&
	    nla_put_flag(skb, PSAMPLE_TUNNEL_KEY_ATTR_IPV4_INFO_BRIDGE))
		return -EMSGSIZE;

	switch (tun_proto) {
	case AF_INET:
		if (tun_key->u.ipv4.src &&
		    nla_put_in_addr(skb, PSAMPLE_TUNNEL_KEY_ATTR_IPV4_SRC,
				    tun_key->u.ipv4.src))
			return -EMSGSIZE;
		if (tun_key->u.ipv4.dst &&
		    nla_put_in_addr(skb, PSAMPLE_TUNNEL_KEY_ATTR_IPV4_DST,
				    tun_key->u.ipv4.dst))
			return -EMSGSIZE;
		break;
	case AF_INET6:
		if (!ipv6_addr_any(&tun_key->u.ipv6.src) &&
		    nla_put_in6_addr(skb, PSAMPLE_TUNNEL_KEY_ATTR_IPV6_SRC,
				     &tun_key->u.ipv6.src))
			return -EMSGSIZE;
		if (!ipv6_addr_any(&tun_key->u.ipv6.dst) &&
		    nla_put_in6_addr(skb, PSAMPLE_TUNNEL_KEY_ATTR_IPV6_DST,
				     &tun_key->u.ipv6.dst))
			return -EMSGSIZE;
		break;
	}
	if (tun_key->tos &&
	    nla_put_u8(skb, PSAMPLE_TUNNEL_KEY_ATTR_TOS, tun_key->tos))
		return -EMSGSIZE;
	if (nla_put_u8(skb, PSAMPLE_TUNNEL_KEY_ATTR_TTL, tun_key->ttl))
		return -EMSGSIZE;
	if (test_bit(IP_TUNNEL_DONT_FRAGMENT_BIT, tun_key->tun_flags) &&
	    nla_put_flag(skb, PSAMPLE_TUNNEL_KEY_ATTR_DONT_FRAGMENT))
		return -EMSGSIZE;
	if (test_bit(IP_TUNNEL_CSUM_BIT, tun_key->tun_flags) &&
	    nla_put_flag(skb, PSAMPLE_TUNNEL_KEY_ATTR_CSUM))
		return -EMSGSIZE;
	if (tun_key->tp_src &&
	    nla_put_be16(skb, PSAMPLE_TUNNEL_KEY_ATTR_TP_SRC, tun_key->tp_src))
		return -EMSGSIZE;
	if (tun_key->tp_dst &&
	    nla_put_be16(skb, PSAMPLE_TUNNEL_KEY_ATTR_TP_DST, tun_key->tp_dst))
		return -EMSGSIZE;
	if (test_bit(IP_TUNNEL_OAM_BIT, tun_key->tun_flags) &&
	    nla_put_flag(skb, PSAMPLE_TUNNEL_KEY_ATTR_OAM))
		return -EMSGSIZE;
	if (tun_opts_len) {
		if (test_bit(IP_TUNNEL_GENEVE_OPT_BIT, tun_key->tun_flags) &&
		    nla_put(skb, PSAMPLE_TUNNEL_KEY_ATTR_GENEVE_OPTS,
			    tun_opts_len, tun_opts))
			return -EMSGSIZE;
		else if (test_bit(IP_TUNNEL_ERSPAN_OPT_BIT,
				  tun_key->tun_flags) &&
			 nla_put(skb, PSAMPLE_TUNNEL_KEY_ATTR_ERSPAN_OPTS,
				 tun_opts_len, tun_opts))
			return -EMSGSIZE;
	}

	return 0;
}

static int psample_ip_tun_to_nlattr(struct sk_buff *skb,
			    struct ip_tunnel_info *tun_info)
{
	struct nlattr *nla;
	int err;

	nla = nla_nest_start_noflag(skb, PSAMPLE_ATTR_TUNNEL);
	if (!nla)
		return -EMSGSIZE;

	err = __psample_ip_tun_to_nlattr(skb, tun_info);
	if (err) {
		nla_nest_cancel(skb, nla);
		return err;
	}

	nla_nest_end(skb, nla);

	return 0;
}

static int psample_tunnel_meta_len(struct ip_tunnel_info *tun_info)
{
	unsigned short tun_proto = ip_tunnel_info_af(tun_info);
	const struct ip_tunnel_key *tun_key = &tun_info->key;
	int tun_opts_len = tun_info->options_len;
	int sum = nla_total_size(0);	/* PSAMPLE_ATTR_TUNNEL */

	if (test_bit(IP_TUNNEL_KEY_BIT, tun_key->tun_flags))
		sum += nla_total_size_64bit(sizeof(u64));

	if (tun_info->mode & IP_TUNNEL_INFO_BRIDGE)
		sum += nla_total_size(0);

	switch (tun_proto) {
	case AF_INET:
		if (tun_key->u.ipv4.src)
			sum += nla_total_size(sizeof(u32));
		if (tun_key->u.ipv4.dst)
			sum += nla_total_size(sizeof(u32));
		break;
	case AF_INET6:
		if (!ipv6_addr_any(&tun_key->u.ipv6.src))
			sum += nla_total_size(sizeof(struct in6_addr));
		if (!ipv6_addr_any(&tun_key->u.ipv6.dst))
			sum += nla_total_size(sizeof(struct in6_addr));
		break;
	}
	if (tun_key->tos)
		sum += nla_total_size(sizeof(u8));
	sum += nla_total_size(sizeof(u8));	/* TTL */
	if (test_bit(IP_TUNNEL_DONT_FRAGMENT_BIT, tun_key->tun_flags))
		sum += nla_total_size(0);
	if (test_bit(IP_TUNNEL_CSUM_BIT, tun_key->tun_flags))
		sum += nla_total_size(0);
	if (tun_key->tp_src)
		sum += nla_total_size(sizeof(u16));
	if (tun_key->tp_dst)
		sum += nla_total_size(sizeof(u16));
	if (test_bit(IP_TUNNEL_OAM_BIT, tun_key->tun_flags))
		sum += nla_total_size(0);
	if (tun_opts_len) {
		if (test_bit(IP_TUNNEL_GENEVE_OPT_BIT, tun_key->tun_flags))
			sum += nla_total_size(tun_opts_len);
		else if (test_bit(IP_TUNNEL_ERSPAN_OPT_BIT,
				  tun_key->tun_flags))
			sum += nla_total_size(tun_opts_len);
	}

	return sum;
}
#endif

void psample_sample_packet(struct psample_group *group,
			   const struct sk_buff *skb, u32 sample_rate,
			   const struct psample_metadata *md)
{
	ktime_t tstamp = ktime_get_real();
	int out_ifindex = md->out_ifindex;
	int in_ifindex = md->in_ifindex;
	u32 trunc_size = md->trunc_size;
#ifdef CONFIG_INET
	struct ip_tunnel_info *tun_info;
#endif
	struct sk_buff *nl_skb;
	int data_len;
	int meta_len;
	void *data;
	int ret;

	if (!genl_has_listeners(&psample_nl_family, group->net,
				PSAMPLE_NL_MCGRP_SAMPLE))
		return;

	meta_len = (in_ifindex ? nla_total_size(sizeof(u16)) : 0) +
		   (out_ifindex ? nla_total_size(sizeof(u16)) : 0) +
		   (md->out_tc_valid ? nla_total_size(sizeof(u16)) : 0) +
		   (md->out_tc_occ_valid ? nla_total_size_64bit(sizeof(u64)) : 0) +
		   (md->latency_valid ? nla_total_size_64bit(sizeof(u64)) : 0) +
		   nla_total_size(sizeof(u32)) +	/* sample_rate */
		   nla_total_size(sizeof(u32)) +	/* orig_size */
		   nla_total_size(sizeof(u32)) +	/* group_num */
		   nla_total_size(sizeof(u32)) +	/* seq */
		   nla_total_size_64bit(sizeof(u64)) +	/* timestamp */
		   nla_total_size(sizeof(u16)) +	/* protocol */
		   (md->user_cookie_len ?
		    nla_total_size(md->user_cookie_len) : 0); /* user cookie */

#ifdef CONFIG_INET
	tun_info = skb_tunnel_info(skb);
	if (tun_info)
		meta_len += psample_tunnel_meta_len(tun_info);
#endif

	data_len = min(skb->len, trunc_size);
	if (meta_len + nla_total_size(data_len) > PSAMPLE_MAX_PACKET_SIZE)
		data_len = PSAMPLE_MAX_PACKET_SIZE - meta_len - NLA_HDRLEN
			    - NLA_ALIGNTO;

	nl_skb = genlmsg_new(meta_len + nla_total_size(data_len), GFP_ATOMIC);
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

	if (md->out_tc_valid) {
		ret = nla_put_u16(nl_skb, PSAMPLE_ATTR_OUT_TC, md->out_tc);
		if (unlikely(ret < 0))
			goto error;
	}

	if (md->out_tc_occ_valid) {
		ret = nla_put_u64_64bit(nl_skb, PSAMPLE_ATTR_OUT_TC_OCC,
					md->out_tc_occ, PSAMPLE_ATTR_PAD);
		if (unlikely(ret < 0))
			goto error;
	}

	if (md->latency_valid) {
		ret = nla_put_u64_64bit(nl_skb, PSAMPLE_ATTR_LATENCY,
					md->latency, PSAMPLE_ATTR_PAD);
		if (unlikely(ret < 0))
			goto error;
	}

	ret = nla_put_u64_64bit(nl_skb, PSAMPLE_ATTR_TIMESTAMP,
				ktime_to_ns(tstamp), PSAMPLE_ATTR_PAD);
	if (unlikely(ret < 0))
		goto error;

	ret = nla_put_u16(nl_skb, PSAMPLE_ATTR_PROTO,
			  be16_to_cpu(skb->protocol));
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

#ifdef CONFIG_INET
	if (tun_info) {
		ret = psample_ip_tun_to_nlattr(nl_skb, tun_info);
		if (unlikely(ret < 0))
			goto error;
	}
#endif

	if (md->user_cookie && md->user_cookie_len &&
	    nla_put(nl_skb, PSAMPLE_ATTR_USER_COOKIE, md->user_cookie_len,
		    md->user_cookie))
		goto error;

	if (md->rate_as_probability)
		nla_put_flag(nl_skb, PSAMPLE_ATTR_SAMPLE_PROBABILITY);

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
