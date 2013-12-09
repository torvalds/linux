/*
 * IrDA netlink layer, for stack configuration.
 *
 * Copyright (c) 2007 Samuel Ortiz <samuel@sortiz.org>
 *
 * Partly based on the 802.11 nelink implementation
 * (see net/wireless/nl80211.c) which is:
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/socket.h>
#include <linux/irda.h>
#include <linux/gfp.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/genetlink.h>



static struct genl_family irda_nl_family = {
	.id = GENL_ID_GENERATE,
	.name = IRDA_NL_NAME,
	.hdrsize = 0,
	.version = IRDA_NL_VERSION,
	.maxattr = IRDA_NL_CMD_MAX,
};

static struct net_device * ifname_to_netdev(struct net *net, struct genl_info *info)
{
	char * ifname;

	if (!info->attrs[IRDA_NL_ATTR_IFNAME])
		return NULL;

	ifname = nla_data(info->attrs[IRDA_NL_ATTR_IFNAME]);

	IRDA_DEBUG(5, "%s(): Looking for %s\n", __func__, ifname);

	return dev_get_by_name(net, ifname);
}

static int irda_nl_set_mode(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device * dev;
	struct irlap_cb * irlap;
	u32 mode;

	if (!info->attrs[IRDA_NL_ATTR_MODE])
		return -EINVAL;

	mode = nla_get_u32(info->attrs[IRDA_NL_ATTR_MODE]);

	IRDA_DEBUG(5, "%s(): Switching to mode: %d\n", __func__, mode);

	dev = ifname_to_netdev(&init_net, info);
	if (!dev)
		return -ENODEV;

	irlap = (struct irlap_cb *)dev->atalk_ptr;
	if (!irlap) {
		dev_put(dev);
		return -ENODEV;
	}

	irlap->mode = mode;

	dev_put(dev);

	return 0;
}

static int irda_nl_get_mode(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device * dev;
	struct irlap_cb * irlap;
	struct sk_buff *msg;
	void *hdr;
	int ret = -ENOBUFS;

	dev = ifname_to_netdev(&init_net, info);
	if (!dev)
		return -ENODEV;

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		dev_put(dev);
		return -ENOMEM;
	}

	irlap = (struct irlap_cb *)dev->atalk_ptr;
	if (!irlap) {
		ret = -ENODEV;
		goto err_out;
	}

	hdr = genlmsg_put(msg, info->snd_portid, info->snd_seq,
			  &irda_nl_family, 0,  IRDA_NL_CMD_GET_MODE);
	if (hdr == NULL) {
		ret = -EMSGSIZE;
		goto err_out;
	}

	if(nla_put_string(msg, IRDA_NL_ATTR_IFNAME,
			  dev->name))
		goto err_out;

	if(nla_put_u32(msg, IRDA_NL_ATTR_MODE, irlap->mode))
		goto err_out;

	genlmsg_end(msg, hdr);

	return genlmsg_reply(msg, info);

 err_out:
	nlmsg_free(msg);
	dev_put(dev);

	return ret;
}

static const struct nla_policy irda_nl_policy[IRDA_NL_ATTR_MAX + 1] = {
	[IRDA_NL_ATTR_IFNAME] = { .type = NLA_NUL_STRING,
				  .len = IFNAMSIZ-1 },
	[IRDA_NL_ATTR_MODE] = { .type = NLA_U32 },
};

static const struct genl_ops irda_nl_ops[] = {
	{
		.cmd = IRDA_NL_CMD_SET_MODE,
		.doit = irda_nl_set_mode,
		.policy = irda_nl_policy,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = IRDA_NL_CMD_GET_MODE,
		.doit = irda_nl_get_mode,
		.policy = irda_nl_policy,
		/* can be retrieved by unprivileged users */
	},

};

int irda_nl_register(void)
{
	return genl_register_family_with_ops(&irda_nl_family, irda_nl_ops);
}

void irda_nl_unregister(void)
{
	genl_unregister_family(&irda_nl_family);
}
