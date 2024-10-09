// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/skbuff.h>

#include "shaper_nl_gen.h"

int net_shaper_nl_pre_doit(const struct genl_split_ops *ops,
			   struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

void net_shaper_nl_post_doit(const struct genl_split_ops *ops,
			     struct sk_buff *skb, struct genl_info *info)
{
}

int net_shaper_nl_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int net_shaper_nl_get_dumpit(struct sk_buff *skb,
			     struct netlink_callback *cb)
{
	return -EOPNOTSUPP;
}

int net_shaper_nl_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int net_shaper_nl_delete_doit(struct sk_buff *skb, struct genl_info *info)
{
	return -EOPNOTSUPP;
}

int net_shaper_nl_pre_dumpit(struct netlink_callback *cb)
{
	return -EOPNOTSUPP;
}

int net_shaper_nl_post_dumpit(struct netlink_callback *cb)
{
	return -EOPNOTSUPP;
}

static int __init shaper_init(void)
{
	return genl_register_family(&net_shaper_nl_family);
}

subsys_initcall(shaper_init);
