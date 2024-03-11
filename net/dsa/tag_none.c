// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * net/dsa/tag_none.c - Traffic handling for switches with no tag
 * Copyright (c) 2008-2009 Marvell Semiconductor
 * Copyright (c) 2013 Florian Fainelli <florian@openwrt.org>
 *
 * WARNING: do not use this for new switches. In case of no hardware
 * tagging support, look at tag_8021q.c instead.
 */

#include "tag.h"

#define NONE_NAME	"none"

static struct sk_buff *dsa_user_notag_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	/* Just return the original SKB */
	return skb;
}

static const struct dsa_device_ops none_ops = {
	.name	= NONE_NAME,
	.proto	= DSA_TAG_PROTO_NONE,
	.xmit	= dsa_user_notag_xmit,
};

module_dsa_tag_driver(none_ops);
MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_NONE, NONE_NAME);
MODULE_DESCRIPTION("DSA no-op tag driver");
MODULE_LICENSE("GPL");
