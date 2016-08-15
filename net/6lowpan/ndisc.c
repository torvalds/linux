/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Authors:
 * (C) 2016 Pengutronix, Alexander Aring <aar@pengutronix.de>
 */

#include <net/6lowpan.h>
#include <net/addrconf.h>
#include <net/ndisc.h>

#include "6lowpan_i.h"

static int lowpan_ndisc_is_useropt(u8 nd_opt_type)
{
	return nd_opt_type == ND_OPT_6CO;
}

#if IS_ENABLED(CONFIG_IEEE802154_6LOWPAN)
#define NDISC_802154_SHORT_ADDR_LENGTH	1
static int lowpan_ndisc_parse_802154_options(const struct net_device *dev,
					     struct nd_opt_hdr *nd_opt,
					     struct ndisc_options *ndopts)
{
	switch (nd_opt->nd_opt_len) {
	case NDISC_802154_SHORT_ADDR_LENGTH:
		if (ndopts->nd_802154_opt_array[nd_opt->nd_opt_type])
			ND_PRINTK(2, warn,
				  "%s: duplicated short addr ND6 option found: type=%d\n",
				  __func__, nd_opt->nd_opt_type);
		else
			ndopts->nd_802154_opt_array[nd_opt->nd_opt_type] = nd_opt;
		return 1;
	default:
		/* all others will be handled by ndisc IPv6 option parsing */
		return 0;
	}
}

static int lowpan_ndisc_parse_options(const struct net_device *dev,
				      struct nd_opt_hdr *nd_opt,
				      struct ndisc_options *ndopts)
{
	if (!lowpan_is_ll(dev, LOWPAN_LLTYPE_IEEE802154))
		return 0;

	switch (nd_opt->nd_opt_type) {
	case ND_OPT_SOURCE_LL_ADDR:
	case ND_OPT_TARGET_LL_ADDR:
		return lowpan_ndisc_parse_802154_options(dev, nd_opt, ndopts);
	default:
		return 0;
	}
}

static void lowpan_ndisc_802154_update(struct neighbour *n, u32 flags,
				       u8 icmp6_type,
				       const struct ndisc_options *ndopts)
{
	struct lowpan_802154_neigh *neigh = lowpan_802154_neigh(neighbour_priv(n));
	u8 *lladdr_short = NULL;

	switch (icmp6_type) {
	case NDISC_ROUTER_SOLICITATION:
	case NDISC_ROUTER_ADVERTISEMENT:
	case NDISC_NEIGHBOUR_SOLICITATION:
		if (ndopts->nd_802154_opts_src_lladdr) {
			lladdr_short = __ndisc_opt_addr_data(ndopts->nd_802154_opts_src_lladdr,
							     IEEE802154_SHORT_ADDR_LEN, 0);
			if (!lladdr_short) {
				ND_PRINTK(2, warn,
					  "NA: invalid short link-layer address length\n");
				return;
			}
		}
		break;
	case NDISC_REDIRECT:
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		if (ndopts->nd_802154_opts_tgt_lladdr) {
			lladdr_short = __ndisc_opt_addr_data(ndopts->nd_802154_opts_tgt_lladdr,
							     IEEE802154_SHORT_ADDR_LEN, 0);
			if (!lladdr_short) {
				ND_PRINTK(2, warn,
					  "NA: invalid short link-layer address length\n");
				return;
			}
		}
		break;
	default:
		break;
	}

	write_lock_bh(&n->lock);
	if (lladdr_short) {
		ieee802154_be16_to_le16(&neigh->short_addr, lladdr_short);
		if (!lowpan_802154_is_valid_src_short_addr(neigh->short_addr))
			neigh->short_addr = cpu_to_le16(IEEE802154_ADDR_SHORT_UNSPEC);
	} else {
		neigh->short_addr = cpu_to_le16(IEEE802154_ADDR_SHORT_UNSPEC);
	}
	write_unlock_bh(&n->lock);
}

static void lowpan_ndisc_update(const struct net_device *dev,
				struct neighbour *n, u32 flags, u8 icmp6_type,
				const struct ndisc_options *ndopts)
{
	if (!lowpan_is_ll(dev, LOWPAN_LLTYPE_IEEE802154))
		return;

	/* react on overrides only. TODO check if this is really right. */
	if (flags & NEIGH_UPDATE_F_OVERRIDE)
		lowpan_ndisc_802154_update(n, flags, icmp6_type, ndopts);
}

static int lowpan_ndisc_opt_addr_space(const struct net_device *dev,
				       u8 icmp6_type, struct neighbour *neigh,
				       u8 *ha_buf, u8 **ha)
{
	struct lowpan_802154_neigh *n;
	struct wpan_dev *wpan_dev;
	int addr_space = 0;

	if (!lowpan_is_ll(dev, LOWPAN_LLTYPE_IEEE802154))
		return 0;

	switch (icmp6_type) {
	case NDISC_REDIRECT:
		n = lowpan_802154_neigh(neighbour_priv(neigh));

		read_lock_bh(&neigh->lock);
		if (lowpan_802154_is_valid_src_short_addr(n->short_addr)) {
			memcpy(ha_buf, &n->short_addr,
			       IEEE802154_SHORT_ADDR_LEN);
			read_unlock_bh(&neigh->lock);
			addr_space += __ndisc_opt_addr_space(IEEE802154_SHORT_ADDR_LEN, 0);
			*ha = ha_buf;
		} else {
			read_unlock_bh(&neigh->lock);
		}
		break;
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
	case NDISC_NEIGHBOUR_SOLICITATION:
	case NDISC_ROUTER_SOLICITATION:
		wpan_dev = lowpan_802154_dev(dev)->wdev->ieee802154_ptr;

		if (lowpan_802154_is_valid_src_short_addr(wpan_dev->short_addr))
			addr_space = __ndisc_opt_addr_space(IEEE802154_SHORT_ADDR_LEN, 0);
		break;
	default:
		break;
	}

	return addr_space;
}

static void lowpan_ndisc_fill_addr_option(const struct net_device *dev,
					  struct sk_buff *skb, u8 icmp6_type,
					  const u8 *ha)
{
	struct wpan_dev *wpan_dev;
	__be16 short_addr;
	u8 opt_type;

	if (!lowpan_is_ll(dev, LOWPAN_LLTYPE_IEEE802154))
		return;

	switch (icmp6_type) {
	case NDISC_REDIRECT:
		if (ha) {
			ieee802154_le16_to_be16(&short_addr, ha);
			__ndisc_fill_addr_option(skb, ND_OPT_TARGET_LL_ADDR,
						 &short_addr,
						 IEEE802154_SHORT_ADDR_LEN, 0);
		}
		return;
	case NDISC_NEIGHBOUR_ADVERTISEMENT:
		opt_type = ND_OPT_TARGET_LL_ADDR;
		break;
	case NDISC_ROUTER_SOLICITATION:
	case NDISC_NEIGHBOUR_SOLICITATION:
		opt_type = ND_OPT_SOURCE_LL_ADDR;
		break;
	default:
		return;
	}

	wpan_dev = lowpan_802154_dev(dev)->wdev->ieee802154_ptr;

	if (lowpan_802154_is_valid_src_short_addr(wpan_dev->short_addr)) {
		ieee802154_le16_to_be16(&short_addr,
					&wpan_dev->short_addr);
		__ndisc_fill_addr_option(skb, opt_type, &short_addr,
					 IEEE802154_SHORT_ADDR_LEN, 0);
	}
}

static void lowpan_ndisc_prefix_rcv_add_addr(struct net *net,
					     struct net_device *dev,
					     const struct prefix_info *pinfo,
					     struct inet6_dev *in6_dev,
					     struct in6_addr *addr,
					     int addr_type, u32 addr_flags,
					     bool sllao, bool tokenized,
					     __u32 valid_lft,
					     u32 prefered_lft,
					     bool dev_addr_generated)
{
	int err;

	/* generates short based address for RA PIO's */
	if (lowpan_is_ll(dev, LOWPAN_LLTYPE_IEEE802154) && dev_addr_generated &&
	    !addrconf_ifid_802154_6lowpan(addr->s6_addr + 8, dev)) {
		err = addrconf_prefix_rcv_add_addr(net, dev, pinfo, in6_dev,
						   addr, addr_type, addr_flags,
						   sllao, tokenized, valid_lft,
						   prefered_lft);
		if (err)
			ND_PRINTK(2, warn,
				  "RA: could not add a short address based address for prefix: %pI6c\n",
				  &pinfo->prefix);
	}
}
#endif

const struct ndisc_ops lowpan_ndisc_ops = {
	.is_useropt		= lowpan_ndisc_is_useropt,
#if IS_ENABLED(CONFIG_IEEE802154_6LOWPAN)
	.parse_options		= lowpan_ndisc_parse_options,
	.update			= lowpan_ndisc_update,
	.opt_addr_space		= lowpan_ndisc_opt_addr_space,
	.fill_addr_option	= lowpan_ndisc_fill_addr_option,
	.prefix_rcv_add_addr	= lowpan_ndisc_prefix_rcv_add_addr,
#endif
};
