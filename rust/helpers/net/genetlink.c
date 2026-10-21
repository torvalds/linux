// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2026 Google LLC.
 */

#include <net/genetlink.h>

#ifdef CONFIG_NET

__rust_helper struct sk_buff *rust_helper_genlmsg_new(size_t payload, gfp_t flags)
{
	return genlmsg_new(payload, flags);
}

__rust_helper
int rust_helper_genlmsg_multicast(const struct genl_family *family,
				  struct sk_buff *skb, u32 portid,
				  unsigned int group, gfp_t flags)
{
	return genlmsg_multicast(family, skb, portid, group, flags);
}

__rust_helper void rust_helper_genlmsg_cancel(struct sk_buff *skb, void *hdr)
{
	genlmsg_cancel(skb, hdr);
}

__rust_helper void rust_helper_genlmsg_end(struct sk_buff *skb, void *hdr)
{
	genlmsg_end(skb, hdr);
}

__rust_helper void rust_helper_nlmsg_free(struct sk_buff *skb)
{
	nlmsg_free(skb);
}

__rust_helper
int rust_helper_genl_has_listeners(const struct genl_family *family,
				   struct net *net, unsigned int group)
{
	return genl_has_listeners(family, net, group);
}

#endif
