// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  SR-IPv6 implementation
 *
 *  Author:
 *  David Lebrun <david.lebrun@uclouvain.be>
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/slab.h>
#include <linux/rhashtable.h>

#include <net/ipv6.h>
#include <net/protocol.h>

#include <net/seg6.h>
#include <net/genetlink.h>
#include <linux/seg6.h>
#include <linux/seg6_genl.h>
#ifdef CONFIG_IPV6_SEG6_HMAC
#include <net/seg6_hmac.h>
#endif

bool seg6_validate_srh(struct ipv6_sr_hdr *srh, int len)
{
	unsigned int tlv_offset;
	int max_last_entry;
	int trailing;

	if (srh->type != IPV6_SRCRT_TYPE_4)
		return false;

	if (((srh->hdrlen + 1) << 3) != len)
		return false;

	max_last_entry = (srh->hdrlen / 2) - 1;

	if (srh->first_segment > max_last_entry)
		return false;

	if (srh->segments_left > srh->first_segment + 1)
		return false;

	tlv_offset = sizeof(*srh) + ((srh->first_segment + 1) << 4);

	trailing = len - tlv_offset;
	if (trailing < 0)
		return false;

	while (trailing) {
		struct sr6_tlv *tlv;
		unsigned int tlv_len;

		if (trailing < sizeof(*tlv))
			return false;

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

#ifdef CONFIG_IPV6_SEG6_HMAC

static int seg6_genl_sethmac(struct sk_buff *skb, struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct seg6_pernet_data *sdata;
	struct seg6_hmac_info *hinfo;
	u32 hmackeyid;
	char *secret;
	int err = 0;
	u8 algid;
	u8 slen;

	sdata = seg6_pernet(net);

	if (!info->attrs[SEG6_ATTR_HMACKEYID] ||
	    !info->attrs[SEG6_ATTR_SECRETLEN] ||
	    !info->attrs[SEG6_ATTR_ALGID])
		return -EINVAL;

	hmackeyid = nla_get_u32(info->attrs[SEG6_ATTR_HMACKEYID]);
	slen = nla_get_u8(info->attrs[SEG6_ATTR_SECRETLEN]);
	algid = nla_get_u8(info->attrs[SEG6_ATTR_ALGID]);

	if (hmackeyid == 0)
		return -EINVAL;

	if (slen > SEG6_HMAC_SECRET_LEN)
		return -EINVAL;

	mutex_lock(&sdata->lock);
	hinfo = seg6_hmac_info_lookup(net, hmackeyid);

	if (!slen) {
		if (!hinfo)
			err = -ENOENT;

		err = seg6_hmac_info_del(net, hmackeyid);

		goto out_unlock;
	}

	if (!info->attrs[SEG6_ATTR_SECRET]) {
		err = -EINVAL;
		goto out_unlock;
	}

	if (hinfo) {
		err = seg6_hmac_info_del(net, hmackeyid);
		if (err)
			goto out_unlock;
	}

	secret = (char *)nla_data(info->attrs[SEG6_ATTR_SECRET]);

	hinfo = kzalloc(sizeof(*hinfo), GFP_KERNEL);
	if (!hinfo) {
		err = -ENOMEM;
		goto out_unlock;
	}

	memcpy(hinfo->secret, secret, slen);
	hinfo->slen = slen;
	hinfo->alg_id = algid;
	hinfo->hmackeyid = hmackeyid;

	err = seg6_hmac_info_add(net, hmackeyid, hinfo);
	if (err)
		kfree(hinfo);

out_unlock:
	mutex_unlock(&sdata->lock);
	return err;
}

#else

static int seg6_genl_sethmac(struct sk_buff *skb, struct genl_info *info)
{
	return -ENOTSUPP;
}

#endif

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
	if (!t_new)
		return -ENOMEM;

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
	return genlmsg_reply(msg, info);

nla_put_failure:
	rcu_read_unlock();
free_msg:
	nlmsg_free(msg);
	return -ENOMEM;
}

#ifdef CONFIG_IPV6_SEG6_HMAC

static int __seg6_hmac_fill_info(struct seg6_hmac_info *hinfo,
				 struct sk_buff *msg)
{
	if (nla_put_u32(msg, SEG6_ATTR_HMACKEYID, hinfo->hmackeyid) ||
	    nla_put_u8(msg, SEG6_ATTR_SECRETLEN, hinfo->slen) ||
	    nla_put(msg, SEG6_ATTR_SECRET, hinfo->slen, hinfo->secret) ||
	    nla_put_u8(msg, SEG6_ATTR_ALGID, hinfo->alg_id))
		return -1;

	return 0;
}

static int __seg6_genl_dumphmac_element(struct seg6_hmac_info *hinfo,
					u32 portid, u32 seq, u32 flags,
					struct sk_buff *skb, u8 cmd)
{
	void *hdr;

	hdr = genlmsg_put(skb, portid, seq, &seg6_genl_family, flags, cmd);
	if (!hdr)
		return -ENOMEM;

	if (__seg6_hmac_fill_info(hinfo, skb) < 0)
		goto nla_put_failure;

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int seg6_genl_dumphmac_start(struct netlink_callback *cb)
{
	struct net *net = sock_net(cb->skb->sk);
	struct seg6_pernet_data *sdata;
	struct rhashtable_iter *iter;

	sdata = seg6_pernet(net);
	iter = (struct rhashtable_iter *)cb->args[0];

	if (!iter) {
		iter = kmalloc(sizeof(*iter), GFP_KERNEL);
		if (!iter)
			return -ENOMEM;

		cb->args[0] = (long)iter;
	}

	rhashtable_walk_enter(&sdata->hmac_infos, iter);

	return 0;
}

static int seg6_genl_dumphmac_done(struct netlink_callback *cb)
{
	struct rhashtable_iter *iter = (struct rhashtable_iter *)cb->args[0];

	rhashtable_walk_exit(iter);

	kfree(iter);

	return 0;
}

static int seg6_genl_dumphmac(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct rhashtable_iter *iter = (struct rhashtable_iter *)cb->args[0];
	struct seg6_hmac_info *hinfo;
	int ret;

	rhashtable_walk_start(iter);

	for (;;) {
		hinfo = rhashtable_walk_next(iter);

		if (IS_ERR(hinfo)) {
			if (PTR_ERR(hinfo) == -EAGAIN)
				continue;
			ret = PTR_ERR(hinfo);
			goto done;
		} else if (!hinfo) {
			break;
		}

		ret = __seg6_genl_dumphmac_element(hinfo,
						   NETLINK_CB(cb->skb).portid,
						   cb->nlh->nlmsg_seq,
						   NLM_F_MULTI,
						   skb, SEG6_CMD_DUMPHMAC);
		if (ret)
			goto done;
	}

	ret = skb->len;

done:
	rhashtable_walk_stop(iter);
	return ret;
}

#else

static int seg6_genl_dumphmac_start(struct netlink_callback *cb)
{
	return 0;
}

static int seg6_genl_dumphmac_done(struct netlink_callback *cb)
{
	return 0;
}

static int seg6_genl_dumphmac(struct sk_buff *skb, struct netlink_callback *cb)
{
	return -ENOTSUPP;
}

#endif

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

#ifdef CONFIG_IPV6_SEG6_HMAC
	seg6_hmac_net_init(net);
#endif

	return 0;
}

static void __net_exit seg6_net_exit(struct net *net)
{
	struct seg6_pernet_data *sdata = seg6_pernet(net);

#ifdef CONFIG_IPV6_SEG6_HMAC
	seg6_hmac_net_exit(net);
#endif

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
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= seg6_genl_sethmac,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= SEG6_CMD_DUMPHMAC,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.start	= seg6_genl_dumphmac_start,
		.dumpit	= seg6_genl_dumphmac,
		.done	= seg6_genl_dumphmac_done,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= SEG6_CMD_SET_TUNSRC,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= seg6_genl_set_tunsrc,
		.flags	= GENL_ADMIN_PERM,
	},
	{
		.cmd	= SEG6_CMD_GET_TUNSRC,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit	= seg6_genl_get_tunsrc,
		.flags	= GENL_ADMIN_PERM,
	},
};

static struct genl_family seg6_genl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= SEG6_GENL_NAME,
	.version	= SEG6_GENL_VERSION,
	.maxattr	= SEG6_ATTR_MAX,
	.policy = seg6_genl_policy,
	.netnsok	= true,
	.parallel_ops	= true,
	.ops		= seg6_genl_ops,
	.n_ops		= ARRAY_SIZE(seg6_genl_ops),
	.module		= THIS_MODULE,
};

int __init seg6_init(void)
{
	int err;

	err = genl_register_family(&seg6_genl_family);
	if (err)
		goto out;

	err = register_pernet_subsys(&ip6_segments_ops);
	if (err)
		goto out_unregister_genl;

#ifdef CONFIG_IPV6_SEG6_LWTUNNEL
	err = seg6_iptunnel_init();
	if (err)
		goto out_unregister_pernet;

	err = seg6_local_init();
	if (err)
		goto out_unregister_pernet;
#endif

#ifdef CONFIG_IPV6_SEG6_HMAC
	err = seg6_hmac_init();
	if (err)
		goto out_unregister_iptun;
#endif

	pr_info("Segment Routing with IPv6\n");

out:
	return err;
#ifdef CONFIG_IPV6_SEG6_HMAC
out_unregister_iptun:
#ifdef CONFIG_IPV6_SEG6_LWTUNNEL
	seg6_local_exit();
	seg6_iptunnel_exit();
#endif
#endif
#ifdef CONFIG_IPV6_SEG6_LWTUNNEL
out_unregister_pernet:
	unregister_pernet_subsys(&ip6_segments_ops);
#endif
out_unregister_genl:
	genl_unregister_family(&seg6_genl_family);
	goto out;
}

void seg6_exit(void)
{
#ifdef CONFIG_IPV6_SEG6_HMAC
	seg6_hmac_exit();
#endif
#ifdef CONFIG_IPV6_SEG6_LWTUNNEL
	seg6_iptunnel_exit();
#endif
	unregister_pernet_subsys(&ip6_segments_ops);
	genl_unregister_family(&seg6_genl_family);
}
