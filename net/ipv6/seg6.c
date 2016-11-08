/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 *
 *
 *  This program is free software; you can redistribute it and/or
 *	  modify it under the terms of the GNU General Public License
 *	  as published by the Free Software Foundation; either version
 *	  2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/slab.h>

#include <net/ipv6.h>
#include <net/protocol.h>

#include <net/seg6.h>
#include <net/genetlink.h>
#include <linux/seg6.h>
#include <linux/seg6_genl.h>

bool seg6_validate_srh(struct ipv6_sr_hdr *srh, int len)
{
	int trailing;
	unsigned int tlv_offset;

	if (srh->type != IPV6_SRCRT_TYPE_4)
		return false;

	if (((srh->hdrlen + 1) << 3) != len)
		return false;

	if (srh->segments_left != srh->first_segment)
		return false;

	tlv_offset = sizeof(*srh) + ((srh->first_segment + 1) << 4);

	trailing = len - tlv_offset;
	if (trailing < 0)
		return false;

	while (trailing) {
		struct sr6_tlv *tlv;
		unsigned int tlv_len;

		tlv = (struct sr6_tlv *)((unsigned char *)srh + tlv_offset);
		tlv_len = sizeof(*tlv) + tlv->len;

		trailing -= tlv_len;
		if (trailing < 0)
			return false;

		tlv_offset += tlv_len;
	}

	return true;
}

static struct genl_family seg6_genl_family;

static const struct nla_policy seg6_genl_policy[SEG6_ATTR_MAX + 1] = {
	[SEG6_ATTR_DST]				= { .type = NLA_BINARY,
		.len = sizeof(struct in6_addr) },
	[SEG6_ATTR_DSTLEN]			= { .type = NLA_S32, },
	[SEG6_ATTR_HMACKEYID]		= { .type = NLA_U32, },
	[SEG6_ATTR_SECRET]			= { .type = NLA_BINARY, },
	[SEG6_ATTR_SECRETLEN]		= { .type = NLA_U8, },
	[SEG6_ATTR_ALGID]			= { .type = NLA_U8, },
	[SEG6_ATTR_HMACINFO]		= { .type = NLA_NESTED, },
};

static int seg6_genl_sethmac(struct sk_buff *skb, struct genl_info *info)
{
	return -ENOTSUPP;
}

static int seg6_genl_set_tunsrc(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct in6_addr *val, *t_old, *t_new;
	struct seg6_pernet_data *sdata;

	sdata = seg6_pernet(net);

	if (!info->attrs[SEG6_ATTR_DST])
		return -EINVAL;

	val = nla_data(info->attrs[SEG6_ATTR_DST]);
	t_new = kmemdup(val, sizeof(*val), GFP_KERNEL);

	mutex_lock(&sdata->lock);

	t_old = sdata->tun_src;
	rcu_assign_pointer(sdata->tun_src, t_new);

	mutex_unlock(&sdata->lock);

	synchronize_net();
	kfree(t_old);

	return 0;
}

static int seg6_genl_get_tunsrc(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct in6_addr *tun_src;
	struct sk_buff *msg;
	void *hdr;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			  &seg6_genl_family, 0, SEG6_CMD_GET_TUNSRC);
	if (!hdr)
		goto free_msg;

	rcu_read_lock();
	tun_src = rcu_dereference(seg6_pernet(net)->tun_src);

	if (nla_put(msg, SEG6_ATTR_DST, sizeof(struct in6_addr), tun_src))
		goto nla_put_failure;

	rcu_read_unlock();

	genlmsg_end(msg, hdr);
	genlmsg_reply(msg, info);

	return 0;

nla_put_failure:
	rcu_read_unlock();
	genlmsg_cancel(msg, hdr);
free_msg:
	nlmsg_free(msg);
	return -ENOMEM;
}

static int seg6_genl_dumphmac(struct sk_buff *skb, struct netlink_callback *cb)
{
	return -ENOTSUPP;
}

static int __net_init seg6_net_init(struct net *net)
{
	struct seg6_pernet_data *sdata;

	sdata = kzalloc(sizeof(*sdata), GFP_KERNEL);
	if (!sdata)
		return -ENOMEM;

	mutex_init(&sdata->lock);

	sdata->tun_src = kzalloc(sizeof(*sdata->tun_src), GFP_KERNEL);
	if (!sdata->tun_src) {
		kfree(sdata);
		return -ENOMEM;
	}

	net->ipv6.seg6_data = sdata;

	return 0;
}

static void __net_exit seg6_net_exit(struct net *net)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);

	kfree(sdata->tun_src);
	kfree(sdata);
}

static struct pernet_operations ip6_segments_ops = {
	.init = seg6_net_init,
	.exit = seg6_net_exit,
};

static const struct genl_ops seg6_genl_ops[] = {
	{
		.cmd	= SEG6_CMD_SETHMAC,
		.doit	= seg6_genl_sethmac,
		.policy	= seg6_genl_policy,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= SEG6_CMD_DUMPHMAC,
		.dumpit	= seg6_genl_dumphmac,
		.policy	= seg6_genl_policy,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= SEG6_CMD_SET_TUNSRC,
		.doit	= seg6_genl_set_tunsrc,
		.policy	= seg6_genl_policy,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= SEG6_CMD_GET_TUNSRC,
		.doit	= seg6_genl_get_tunsrc,
		.policy = seg6_genl_policy,
		.flags	= GENL_ADMIN_PERM,
	},
};

static struct genl_family seg6_genl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= SEG6_GENL_NAME,
	.version	= SEG6_GENL_VERSION,
	.maxattr	= SEG6_ATTR_MAX,
	.netnsok	= true,
	.parallel_ops	= true,
	.ops		= seg6_genl_ops,
	.n_ops		= ARRAY_SIZE(seg6_genl_ops),
	.module		= THIS_MODULE,
};

int __init seg6_init(void)
{
	int err = -ENOMEM;

	err = genl_register_family(&seg6_genl_family);
	if (err)
		goto out;

	err = register_pernet_subsys(&ip6_segments_ops);
	if (err)
		goto out_unregister_genl;

	err = seg6_iptunnel_init();
	if (err)
		goto out_unregister_pernet;

	pr_info("Segment Routing with IPv6\n");

out:
	return err;
out_unregister_pernet:
	unregister_pernet_subsys(&ip6_segments_ops);
out_unregister_genl:
	genl_unregister_family(&seg6_genl_family);
	goto out;
}

void seg6_exit(void)
{
	seg6_iptunnel_exit();
	unregister_pernet_subsys(&ip6_segments_ops);
	genl_unregister_family(&seg6_genl_family);
}
