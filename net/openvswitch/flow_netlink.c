/*
 * Copyright (c) 2007-2014 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "flow.h"
#include "datapath.h"
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/llc_pdu.h>
#include <linux/kernel.h>
#include <linux/jhash.h>
#include <linux/jiffies.h>
#include <linux/llc.h>
#include <linux/module.h>
#include <linux/in.h>
#include <linux/rcupdate.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/sctp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/rculist.h>
#include <net/geneve.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ndisc.h>

#include "flow_netlink.h"

static void update_range__(struct sw_flow_match *match,
			   size_t offset, size_t size, bool is_mask)
{
	struct sw_flow_key_range *range = NULL;
	size_t start = rounddown(offset, sizeof(long));
	size_t end = roundup(offset + size, sizeof(long));

	if (!is_mask)
		range = &match->range;
	else if (match->mask)
		range = &match->mask->range;

	if (!range)
		return;

	if (range->start == range->end) {
		range->start = start;
		range->end = end;
		return;
	}

	if (range->start > start)
		range->start = start;

	if (range->end < end)
		range->end = end;
}

#define SW_FLOW_KEY_PUT(match, field, value, is_mask) \
	do { \
		update_range__(match, offsetof(struct sw_flow_key, field),  \
				     sizeof((match)->key->field), is_mask); \
		if (is_mask) {						    \
			if ((match)->mask)				    \
				(match)->mask->key.field = value;	    \
		} else {                                                    \
			(match)->key->field = value;		            \
		}                                                           \
	} while (0)

#define SW_FLOW_KEY_MEMCPY_OFFSET(match, offset, value_p, len, is_mask)	    \
	do {								    \
		update_range__(match, offset, len, is_mask);		    \
		if (is_mask)						    \
			memcpy((u8 *)&(match)->mask->key + offset, value_p, \
			       len);					    \
		else							    \
			memcpy((u8 *)(match)->key + offset, value_p, len);  \
	} while (0)

#define SW_FLOW_KEY_MEMCPY(match, field, value_p, len, is_mask)		      \
	SW_FLOW_KEY_MEMCPY_OFFSET(match, offsetof(struct sw_flow_key, field), \
				  value_p, len, is_mask)

#define SW_FLOW_KEY_MEMSET_FIELD(match, field, value, is_mask) \
	do { \
		update_range__(match, offsetof(struct sw_flow_key, field),  \
				     sizeof((match)->key->field), is_mask); \
		if (is_mask) {						    \
			if ((match)->mask)				    \
				memset((u8 *)&(match)->mask->key.field, value,\
				       sizeof((match)->mask->key.field));   \
		} else {                                                    \
			memset((u8 *)&(match)->key->field, value,           \
			       sizeof((match)->key->field));                \
		}                                                           \
	} while (0)

static bool match_validate(const struct sw_flow_match *match,
			   u64 key_attrs, u64 mask_attrs)
{
	u64 key_expected = 1 << OVS_KEY_ATTR_ETHERNET;
	u64 mask_allowed = key_attrs;  /* At most allow all key attributes */

	/* The following mask attributes allowed only if they
	 * pass the validation tests. */
	mask_allowed &= ~((1 << OVS_KEY_ATTR_IPV4)
			| (1 << OVS_KEY_ATTR_IPV6)
			| (1 << OVS_KEY_ATTR_TCP)
			| (1 << OVS_KEY_ATTR_TCP_FLAGS)
			| (1 << OVS_KEY_ATTR_UDP)
			| (1 << OVS_KEY_ATTR_SCTP)
			| (1 << OVS_KEY_ATTR_ICMP)
			| (1 << OVS_KEY_ATTR_ICMPV6)
			| (1 << OVS_KEY_ATTR_ARP)
			| (1 << OVS_KEY_ATTR_ND));

	/* Always allowed mask fields. */
	mask_allowed |= ((1 << OVS_KEY_ATTR_TUNNEL)
		       | (1 << OVS_KEY_ATTR_IN_PORT)
		       | (1 << OVS_KEY_ATTR_ETHERTYPE));

	/* Check key attributes. */
	if (match->key->eth.type == htons(ETH_P_ARP)
			|| match->key->eth.type == htons(ETH_P_RARP)) {
		key_expected |= 1 << OVS_KEY_ATTR_ARP;
		if (match->mask && (match->mask->key.tp.src == htons(0xff)))
			mask_allowed |= 1 << OVS_KEY_ATTR_ARP;
	}

	if (match->key->eth.type == htons(ETH_P_IP)) {
		key_expected |= 1 << OVS_KEY_ATTR_IPV4;
		if (match->mask && (match->mask->key.eth.type == htons(0xffff)))
			mask_allowed |= 1 << OVS_KEY_ATTR_IPV4;

		if (match->key->ip.frag != OVS_FRAG_TYPE_LATER) {
			if (match->key->ip.proto == IPPROTO_UDP) {
				key_expected |= 1 << OVS_KEY_ATTR_UDP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_UDP;
			}

			if (match->key->ip.proto == IPPROTO_SCTP) {
				key_expected |= 1 << OVS_KEY_ATTR_SCTP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_SCTP;
			}

			if (match->key->ip.proto == IPPROTO_TCP) {
				key_expected |= 1 << OVS_KEY_ATTR_TCP;
				key_expected |= 1 << OVS_KEY_ATTR_TCP_FLAGS;
				if (match->mask && (match->mask->key.ip.proto == 0xff)) {
					mask_allowed |= 1 << OVS_KEY_ATTR_TCP;
					mask_allowed |= 1 << OVS_KEY_ATTR_TCP_FLAGS;
				}
			}

			if (match->key->ip.proto == IPPROTO_ICMP) {
				key_expected |= 1 << OVS_KEY_ATTR_ICMP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_ICMP;
			}
		}
	}

	if (match->key->eth.type == htons(ETH_P_IPV6)) {
		key_expected |= 1 << OVS_KEY_ATTR_IPV6;
		if (match->mask && (match->mask->key.eth.type == htons(0xffff)))
			mask_allowed |= 1 << OVS_KEY_ATTR_IPV6;

		if (match->key->ip.frag != OVS_FRAG_TYPE_LATER) {
			if (match->key->ip.proto == IPPROTO_UDP) {
				key_expected |= 1 << OVS_KEY_ATTR_UDP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_UDP;
			}

			if (match->key->ip.proto == IPPROTO_SCTP) {
				key_expected |= 1 << OVS_KEY_ATTR_SCTP;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_SCTP;
			}

			if (match->key->ip.proto == IPPROTO_TCP) {
				key_expected |= 1 << OVS_KEY_ATTR_TCP;
				key_expected |= 1 << OVS_KEY_ATTR_TCP_FLAGS;
				if (match->mask && (match->mask->key.ip.proto == 0xff)) {
					mask_allowed |= 1 << OVS_KEY_ATTR_TCP;
					mask_allowed |= 1 << OVS_KEY_ATTR_TCP_FLAGS;
				}
			}

			if (match->key->ip.proto == IPPROTO_ICMPV6) {
				key_expected |= 1 << OVS_KEY_ATTR_ICMPV6;
				if (match->mask && (match->mask->key.ip.proto == 0xff))
					mask_allowed |= 1 << OVS_KEY_ATTR_ICMPV6;

				if (match->key->tp.src ==
						htons(NDISC_NEIGHBOUR_SOLICITATION) ||
				    match->key->tp.src == htons(NDISC_NEIGHBOUR_ADVERTISEMENT)) {
					key_expected |= 1 << OVS_KEY_ATTR_ND;
					if (match->mask && (match->mask->key.tp.src == htons(0xffff)))
						mask_allowed |= 1 << OVS_KEY_ATTR_ND;
				}
			}
		}
	}

	if ((key_attrs & key_expected) != key_expected) {
		/* Key attributes check failed. */
		OVS_NLERR("Missing expected key attributes (key_attrs=%llx, expected=%llx).\n",
				(unsigned long long)key_attrs, (unsigned long long)key_expected);
		return false;
	}

	if ((mask_attrs & mask_allowed) != mask_attrs) {
		/* Mask attributes check failed. */
		OVS_NLERR("Contain more than allowed mask fields (mask_attrs=%llx, mask_allowed=%llx).\n",
				(unsigned long long)mask_attrs, (unsigned long long)mask_allowed);
		return false;
	}

	return true;
}

/* The size of the argument for each %OVS_KEY_ATTR_* Netlink attribute.  */
static const int ovs_key_lens[OVS_KEY_ATTR_MAX + 1] = {
	[OVS_KEY_ATTR_ENCAP] = -1,
	[OVS_KEY_ATTR_PRIORITY] = sizeof(u32),
	[OVS_KEY_ATTR_IN_PORT] = sizeof(u32),
	[OVS_KEY_ATTR_SKB_MARK] = sizeof(u32),
	[OVS_KEY_ATTR_ETHERNET] = sizeof(struct ovs_key_ethernet),
	[OVS_KEY_ATTR_VLAN] = sizeof(__be16),
	[OVS_KEY_ATTR_ETHERTYPE] = sizeof(__be16),
	[OVS_KEY_ATTR_IPV4] = sizeof(struct ovs_key_ipv4),
	[OVS_KEY_ATTR_IPV6] = sizeof(struct ovs_key_ipv6),
	[OVS_KEY_ATTR_TCP] = sizeof(struct ovs_key_tcp),
	[OVS_KEY_ATTR_TCP_FLAGS] = sizeof(__be16),
	[OVS_KEY_ATTR_UDP] = sizeof(struct ovs_key_udp),
	[OVS_KEY_ATTR_SCTP] = sizeof(struct ovs_key_sctp),
	[OVS_KEY_ATTR_ICMP] = sizeof(struct ovs_key_icmp),
	[OVS_KEY_ATTR_ICMPV6] = sizeof(struct ovs_key_icmpv6),
	[OVS_KEY_ATTR_ARP] = sizeof(struct ovs_key_arp),
	[OVS_KEY_ATTR_ND] = sizeof(struct ovs_key_nd),
	[OVS_KEY_ATTR_RECIRC_ID] = sizeof(u32),
	[OVS_KEY_ATTR_DP_HASH] = sizeof(u32),
	[OVS_KEY_ATTR_TUNNEL] = -1,
};

static bool is_all_zero(const u8 *fp, size_t size)
{
	int i;

	if (!fp)
		return false;

	for (i = 0; i < size; i++)
		if (fp[i])
			return false;

	return true;
}

static int __parse_flow_nlattrs(const struct nlattr *attr,
				const struct nlattr *a[],
				u64 *attrsp, bool nz)
{
	const struct nlattr *nla;
	u64 attrs;
	int rem;

	attrs = *attrsp;
	nla_for_each_nested(nla, attr, rem) {
		u16 type = nla_type(nla);
		int expected_len;

		if (type > OVS_KEY_ATTR_MAX) {
			OVS_NLERR("Unknown key attribute (type=%d, max=%d).\n",
				  type, OVS_KEY_ATTR_MAX);
			return -EINVAL;
		}

		if (attrs & (1 << type)) {
			OVS_NLERR("Duplicate key attribute (type %d).\n", type);
			return -EINVAL;
		}

		expected_len = ovs_key_lens[type];
		if (nla_len(nla) != expected_len && expected_len != -1) {
			OVS_NLERR("Key attribute has unexpected length (type=%d"
				  ", length=%d, expected=%d).\n", type,
				  nla_len(nla), expected_len);
			return -EINVAL;
		}

		if (!nz || !is_all_zero(nla_data(nla), expected_len)) {
			attrs |= 1 << type;
			a[type] = nla;
		}
	}
	if (rem) {
		OVS_NLERR("Message has %d unknown bytes.\n", rem);
		return -EINVAL;
	}

	*attrsp = attrs;
	return 0;
}

static int parse_flow_mask_nlattrs(const struct nlattr *attr,
				   const struct nlattr *a[], u64 *attrsp)
{
	return __parse_flow_nlattrs(attr, a, attrsp, true);
}

static int parse_flow_nlattrs(const struct nlattr *attr,
			      const struct nlattr *a[], u64 *attrsp)
{
	return __parse_flow_nlattrs(attr, a, attrsp, false);
}

static int ipv4_tun_from_nlattr(const struct nlattr *attr,
				struct sw_flow_match *match, bool is_mask)
{
	struct nlattr *a;
	int rem;
	bool ttl = false;
	__be16 tun_flags = 0;
	unsigned long opt_key_offset;

	nla_for_each_nested(a, attr, rem) {
		int type = nla_type(a);
		static const u32 ovs_tunnel_key_lens[OVS_TUNNEL_KEY_ATTR_MAX + 1] = {
			[OVS_TUNNEL_KEY_ATTR_ID] = sizeof(u64),
			[OVS_TUNNEL_KEY_ATTR_IPV4_SRC] = sizeof(u32),
			[OVS_TUNNEL_KEY_ATTR_IPV4_DST] = sizeof(u32),
			[OVS_TUNNEL_KEY_ATTR_TOS] = 1,
			[OVS_TUNNEL_KEY_ATTR_TTL] = 1,
			[OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT] = 0,
			[OVS_TUNNEL_KEY_ATTR_CSUM] = 0,
			[OVS_TUNNEL_KEY_ATTR_OAM] = 0,
			[OVS_TUNNEL_KEY_ATTR_GENEVE_OPTS] = -1,
		};

		if (type > OVS_TUNNEL_KEY_ATTR_MAX) {
			OVS_NLERR("Unknown IPv4 tunnel attribute (type=%d, max=%d).\n",
			type, OVS_TUNNEL_KEY_ATTR_MAX);
			return -EINVAL;
		}

		if (ovs_tunnel_key_lens[type] != nla_len(a) &&
		    ovs_tunnel_key_lens[type] != -1) {
			OVS_NLERR("IPv4 tunnel attribute type has unexpected "
				  " length (type=%d, length=%d, expected=%d).\n",
				  type, nla_len(a), ovs_tunnel_key_lens[type]);
			return -EINVAL;
		}

		switch (type) {
		case OVS_TUNNEL_KEY_ATTR_ID:
			SW_FLOW_KEY_PUT(match, tun_key.tun_id,
					nla_get_be64(a), is_mask);
			tun_flags |= TUNNEL_KEY;
			break;
		case OVS_TUNNEL_KEY_ATTR_IPV4_SRC:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_src,
					nla_get_be32(a), is_mask);
			break;
		case OVS_TUNNEL_KEY_ATTR_IPV4_DST:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_dst,
					nla_get_be32(a), is_mask);
			break;
		case OVS_TUNNEL_KEY_ATTR_TOS:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_tos,
					nla_get_u8(a), is_mask);
			break;
		case OVS_TUNNEL_KEY_ATTR_TTL:
			SW_FLOW_KEY_PUT(match, tun_key.ipv4_ttl,
					nla_get_u8(a), is_mask);
			ttl = true;
			break;
		case OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT:
			tun_flags |= TUNNEL_DONT_FRAGMENT;
			break;
		case OVS_TUNNEL_KEY_ATTR_CSUM:
			tun_flags |= TUNNEL_CSUM;
			break;
		case OVS_TUNNEL_KEY_ATTR_OAM:
			tun_flags |= TUNNEL_OAM;
			break;
		case OVS_TUNNEL_KEY_ATTR_GENEVE_OPTS:
			tun_flags |= TUNNEL_OPTIONS_PRESENT;
			if (nla_len(a) > sizeof(match->key->tun_opts)) {
				OVS_NLERR("Geneve option length exceeds maximum size (len %d, max %zu).\n",
					  nla_len(a),
					  sizeof(match->key->tun_opts));
				return -EINVAL;
			}

			if (nla_len(a) % 4 != 0) {
				OVS_NLERR("Geneve option length is not a multiple of 4 (len %d).\n",
					  nla_len(a));
				return -EINVAL;
			}

			/* We need to record the length of the options passed
			 * down, otherwise packets with the same format but
			 * additional options will be silently matched.
			 */
			if (!is_mask) {
				SW_FLOW_KEY_PUT(match, tun_opts_len, nla_len(a),
						false);
			} else {
				/* This is somewhat unusual because it looks at
				 * both the key and mask while parsing the
				 * attributes (and by extension assumes the key
				 * is parsed first). Normally, we would verify
				 * that each is the correct length and that the
				 * attributes line up in the validate function.
				 * However, that is difficult because this is
				 * variable length and we won't have the
				 * information later.
				 */
				if (match->key->tun_opts_len != nla_len(a)) {
					OVS_NLERR("Geneve option key length (%d) is different from mask length (%d).",
						  match->key->tun_opts_len,
						  nla_len(a));
					return -EINVAL;
				}

				SW_FLOW_KEY_PUT(match, tun_opts_len, 0xff,
						true);
			}

			opt_key_offset = (unsigned long)GENEVE_OPTS(
					  (struct sw_flow_key *)0,
					  nla_len(a));
			SW_FLOW_KEY_MEMCPY_OFFSET(match, opt_key_offset,
						  nla_data(a), nla_len(a),
						  is_mask);
			break;
		default:
			OVS_NLERR("Unknown IPv4 tunnel attribute (%d).\n",
				  type);
			return -EINVAL;
		}
	}

	SW_FLOW_KEY_PUT(match, tun_key.tun_flags, tun_flags, is_mask);

	if (rem > 0) {
		OVS_NLERR("IPv4 tunnel attribute has %d unknown bytes.\n", rem);
		return -EINVAL;
	}

	if (!is_mask) {
		if (!match->key->tun_key.ipv4_dst) {
			OVS_NLERR("IPv4 tunnel destination address is zero.\n");
			return -EINVAL;
		}

		if (!ttl) {
			OVS_NLERR("IPv4 tunnel TTL not specified.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int __ipv4_tun_to_nlattr(struct sk_buff *skb,
				const struct ovs_key_ipv4_tunnel *output,
				const struct geneve_opt *tun_opts,
				int swkey_tun_opts_len)
{
	if (output->tun_flags & TUNNEL_KEY &&
	    nla_put_be64(skb, OVS_TUNNEL_KEY_ATTR_ID, output->tun_id))
		return -EMSGSIZE;
	if (output->ipv4_src &&
	    nla_put_be32(skb, OVS_TUNNEL_KEY_ATTR_IPV4_SRC, output->ipv4_src))
		return -EMSGSIZE;
	if (output->ipv4_dst &&
	    nla_put_be32(skb, OVS_TUNNEL_KEY_ATTR_IPV4_DST, output->ipv4_dst))
		return -EMSGSIZE;
	if (output->ipv4_tos &&
	    nla_put_u8(skb, OVS_TUNNEL_KEY_ATTR_TOS, output->ipv4_tos))
		return -EMSGSIZE;
	if (nla_put_u8(skb, OVS_TUNNEL_KEY_ATTR_TTL, output->ipv4_ttl))
		return -EMSGSIZE;
	if ((output->tun_flags & TUNNEL_DONT_FRAGMENT) &&
	    nla_put_flag(skb, OVS_TUNNEL_KEY_ATTR_DONT_FRAGMENT))
		return -EMSGSIZE;
	if ((output->tun_flags & TUNNEL_CSUM) &&
	    nla_put_flag(skb, OVS_TUNNEL_KEY_ATTR_CSUM))
		return -EMSGSIZE;
	if ((output->tun_flags & TUNNEL_OAM) &&
	    nla_put_flag(skb, OVS_TUNNEL_KEY_ATTR_OAM))
		return -EMSGSIZE;
	if (tun_opts &&
	    nla_put(skb, OVS_TUNNEL_KEY_ATTR_GENEVE_OPTS,
		    swkey_tun_opts_len, tun_opts))
		return -EMSGSIZE;

	return 0;
}


static int ipv4_tun_to_nlattr(struct sk_buff *skb,
			      const struct ovs_key_ipv4_tunnel *output,
			      const struct geneve_opt *tun_opts,
			      int swkey_tun_opts_len)
{
	struct nlattr *nla;
	int err;

	nla = nla_nest_start(skb, OVS_KEY_ATTR_TUNNEL);
	if (!nla)
		return -EMSGSIZE;

	err = __ipv4_tun_to_nlattr(skb, output, tun_opts, swkey_tun_opts_len);
	if (err)
		return err;

	nla_nest_end(skb, nla);
	return 0;
}

static int metadata_from_nlattrs(struct sw_flow_match *match,  u64 *attrs,
				 const struct nlattr **a, bool is_mask)
{
	if (*attrs & (1 << OVS_KEY_ATTR_DP_HASH)) {
		u32 hash_val = nla_get_u32(a[OVS_KEY_ATTR_DP_HASH]);

		SW_FLOW_KEY_PUT(match, ovs_flow_hash, hash_val, is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_DP_HASH);
	}

	if (*attrs & (1 << OVS_KEY_ATTR_RECIRC_ID)) {
		u32 recirc_id = nla_get_u32(a[OVS_KEY_ATTR_RECIRC_ID]);

		SW_FLOW_KEY_PUT(match, recirc_id, recirc_id, is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_RECIRC_ID);
	}

	if (*attrs & (1 << OVS_KEY_ATTR_PRIORITY)) {
		SW_FLOW_KEY_PUT(match, phy.priority,
			  nla_get_u32(a[OVS_KEY_ATTR_PRIORITY]), is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_PRIORITY);
	}

	if (*attrs & (1 << OVS_KEY_ATTR_IN_PORT)) {
		u32 in_port = nla_get_u32(a[OVS_KEY_ATTR_IN_PORT]);

		if (is_mask)
			in_port = 0xffffffff; /* Always exact match in_port. */
		else if (in_port >= DP_MAX_PORTS)
			return -EINVAL;

		SW_FLOW_KEY_PUT(match, phy.in_port, in_port, is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_IN_PORT);
	} else if (!is_mask) {
		SW_FLOW_KEY_PUT(match, phy.in_port, DP_MAX_PORTS, is_mask);
	}

	if (*attrs & (1 << OVS_KEY_ATTR_SKB_MARK)) {
		uint32_t mark = nla_get_u32(a[OVS_KEY_ATTR_SKB_MARK]);

		SW_FLOW_KEY_PUT(match, phy.skb_mark, mark, is_mask);
		*attrs &= ~(1 << OVS_KEY_ATTR_SKB_MARK);
	}
	if (*attrs & (1 << OVS_KEY_ATTR_TUNNEL)) {
		if (ipv4_tun_from_nlattr(a[OVS_KEY_ATTR_TUNNEL], match,
					 is_mask))
			return -EINVAL;
		*attrs &= ~(1 << OVS_KEY_ATTR_TUNNEL);
	}
	return 0;
}

static int ovs_key_from_nlattrs(struct sw_flow_match *match, u64 attrs,
				const struct nlattr **a, bool is_mask)
{
	int err;
	u64 orig_attrs = attrs;

	err = metadata_from_nlattrs(match, &attrs, a, is_mask);
	if (err)
		return err;

	if (attrs & (1 << OVS_KEY_ATTR_ETHERNET)) {
		const struct ovs_key_ethernet *eth_key;

		eth_key = nla_data(a[OVS_KEY_ATTR_ETHERNET]);
		SW_FLOW_KEY_MEMCPY(match, eth.src,
				eth_key->eth_src, ETH_ALEN, is_mask);
		SW_FLOW_KEY_MEMCPY(match, eth.dst,
				eth_key->eth_dst, ETH_ALEN, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ETHERNET);
	}

	if (attrs & (1 << OVS_KEY_ATTR_VLAN)) {
		__be16 tci;

		tci = nla_get_be16(a[OVS_KEY_ATTR_VLAN]);
		if (!(tci & htons(VLAN_TAG_PRESENT))) {
			if (is_mask)
				OVS_NLERR("VLAN TCI mask does not have exact match for VLAN_TAG_PRESENT bit.\n");
			else
				OVS_NLERR("VLAN TCI does not have VLAN_TAG_PRESENT bit set.\n");

			return -EINVAL;
		}

		SW_FLOW_KEY_PUT(match, eth.tci, tci, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_VLAN);
	} else if (!is_mask)
		SW_FLOW_KEY_PUT(match, eth.tci, htons(0xffff), true);

	if (attrs & (1 << OVS_KEY_ATTR_ETHERTYPE)) {
		__be16 eth_type;

		eth_type = nla_get_be16(a[OVS_KEY_ATTR_ETHERTYPE]);
		if (is_mask) {
			/* Always exact match EtherType. */
			eth_type = htons(0xffff);
		} else if (ntohs(eth_type) < ETH_P_802_3_MIN) {
			OVS_NLERR("EtherType is less than minimum (type=%x, min=%x).\n",
					ntohs(eth_type), ETH_P_802_3_MIN);
			return -EINVAL;
		}

		SW_FLOW_KEY_PUT(match, eth.type, eth_type, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ETHERTYPE);
	} else if (!is_mask) {
		SW_FLOW_KEY_PUT(match, eth.type, htons(ETH_P_802_2), is_mask);
	}

	if (attrs & (1 << OVS_KEY_ATTR_IPV4)) {
		const struct ovs_key_ipv4 *ipv4_key;

		ipv4_key = nla_data(a[OVS_KEY_ATTR_IPV4]);
		if (!is_mask && ipv4_key->ipv4_frag > OVS_FRAG_TYPE_MAX) {
			OVS_NLERR("Unknown IPv4 fragment type (value=%d, max=%d).\n",
				ipv4_key->ipv4_frag, OVS_FRAG_TYPE_MAX);
			return -EINVAL;
		}
		SW_FLOW_KEY_PUT(match, ip.proto,
				ipv4_key->ipv4_proto, is_mask);
		SW_FLOW_KEY_PUT(match, ip.tos,
				ipv4_key->ipv4_tos, is_mask);
		SW_FLOW_KEY_PUT(match, ip.ttl,
				ipv4_key->ipv4_ttl, is_mask);
		SW_FLOW_KEY_PUT(match, ip.frag,
				ipv4_key->ipv4_frag, is_mask);
		SW_FLOW_KEY_PUT(match, ipv4.addr.src,
				ipv4_key->ipv4_src, is_mask);
		SW_FLOW_KEY_PUT(match, ipv4.addr.dst,
				ipv4_key->ipv4_dst, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_IPV4);
	}

	if (attrs & (1 << OVS_KEY_ATTR_IPV6)) {
		const struct ovs_key_ipv6 *ipv6_key;

		ipv6_key = nla_data(a[OVS_KEY_ATTR_IPV6]);
		if (!is_mask && ipv6_key->ipv6_frag > OVS_FRAG_TYPE_MAX) {
			OVS_NLERR("Unknown IPv6 fragment type (value=%d, max=%d).\n",
				ipv6_key->ipv6_frag, OVS_FRAG_TYPE_MAX);
			return -EINVAL;
		}

		if (!is_mask && ipv6_key->ipv6_label & htonl(0xFFF00000)) {
			OVS_NLERR("IPv6 flow label %x is out of range (max=%x).\n",
				  ntohl(ipv6_key->ipv6_label), (1 << 20) - 1);
			return -EINVAL;
		}

		SW_FLOW_KEY_PUT(match, ipv6.label,
				ipv6_key->ipv6_label, is_mask);
		SW_FLOW_KEY_PUT(match, ip.proto,
				ipv6_key->ipv6_proto, is_mask);
		SW_FLOW_KEY_PUT(match, ip.tos,
				ipv6_key->ipv6_tclass, is_mask);
		SW_FLOW_KEY_PUT(match, ip.ttl,
				ipv6_key->ipv6_hlimit, is_mask);
		SW_FLOW_KEY_PUT(match, ip.frag,
				ipv6_key->ipv6_frag, is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.addr.src,
				ipv6_key->ipv6_src,
				sizeof(match->key->ipv6.addr.src),
				is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.addr.dst,
				ipv6_key->ipv6_dst,
				sizeof(match->key->ipv6.addr.dst),
				is_mask);

		attrs &= ~(1 << OVS_KEY_ATTR_IPV6);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ARP)) {
		const struct ovs_key_arp *arp_key;

		arp_key = nla_data(a[OVS_KEY_ATTR_ARP]);
		if (!is_mask && (arp_key->arp_op & htons(0xff00))) {
			OVS_NLERR("Unknown ARP opcode (opcode=%d).\n",
				  arp_key->arp_op);
			return -EINVAL;
		}

		SW_FLOW_KEY_PUT(match, ipv4.addr.src,
				arp_key->arp_sip, is_mask);
		SW_FLOW_KEY_PUT(match, ipv4.addr.dst,
			arp_key->arp_tip, is_mask);
		SW_FLOW_KEY_PUT(match, ip.proto,
				ntohs(arp_key->arp_op), is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv4.arp.sha,
				arp_key->arp_sha, ETH_ALEN, is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv4.arp.tha,
				arp_key->arp_tha, ETH_ALEN, is_mask);

		attrs &= ~(1 << OVS_KEY_ATTR_ARP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_TCP)) {
		const struct ovs_key_tcp *tcp_key;

		tcp_key = nla_data(a[OVS_KEY_ATTR_TCP]);
		SW_FLOW_KEY_PUT(match, tp.src, tcp_key->tcp_src, is_mask);
		SW_FLOW_KEY_PUT(match, tp.dst, tcp_key->tcp_dst, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_TCP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_TCP_FLAGS)) {
		if (orig_attrs & (1 << OVS_KEY_ATTR_IPV4)) {
			SW_FLOW_KEY_PUT(match, tp.flags,
					nla_get_be16(a[OVS_KEY_ATTR_TCP_FLAGS]),
					is_mask);
		} else {
			SW_FLOW_KEY_PUT(match, tp.flags,
					nla_get_be16(a[OVS_KEY_ATTR_TCP_FLAGS]),
					is_mask);
		}
		attrs &= ~(1 << OVS_KEY_ATTR_TCP_FLAGS);
	}

	if (attrs & (1 << OVS_KEY_ATTR_UDP)) {
		const struct ovs_key_udp *udp_key;

		udp_key = nla_data(a[OVS_KEY_ATTR_UDP]);
		SW_FLOW_KEY_PUT(match, tp.src, udp_key->udp_src, is_mask);
		SW_FLOW_KEY_PUT(match, tp.dst, udp_key->udp_dst, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_UDP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_SCTP)) {
		const struct ovs_key_sctp *sctp_key;

		sctp_key = nla_data(a[OVS_KEY_ATTR_SCTP]);
		SW_FLOW_KEY_PUT(match, tp.src, sctp_key->sctp_src, is_mask);
		SW_FLOW_KEY_PUT(match, tp.dst, sctp_key->sctp_dst, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_SCTP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ICMP)) {
		const struct ovs_key_icmp *icmp_key;

		icmp_key = nla_data(a[OVS_KEY_ATTR_ICMP]);
		SW_FLOW_KEY_PUT(match, tp.src,
				htons(icmp_key->icmp_type), is_mask);
		SW_FLOW_KEY_PUT(match, tp.dst,
				htons(icmp_key->icmp_code), is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ICMP);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ICMPV6)) {
		const struct ovs_key_icmpv6 *icmpv6_key;

		icmpv6_key = nla_data(a[OVS_KEY_ATTR_ICMPV6]);
		SW_FLOW_KEY_PUT(match, tp.src,
				htons(icmpv6_key->icmpv6_type), is_mask);
		SW_FLOW_KEY_PUT(match, tp.dst,
				htons(icmpv6_key->icmpv6_code), is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ICMPV6);
	}

	if (attrs & (1 << OVS_KEY_ATTR_ND)) {
		const struct ovs_key_nd *nd_key;

		nd_key = nla_data(a[OVS_KEY_ATTR_ND]);
		SW_FLOW_KEY_MEMCPY(match, ipv6.nd.target,
			nd_key->nd_target,
			sizeof(match->key->ipv6.nd.target),
			is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.nd.sll,
			nd_key->nd_sll, ETH_ALEN, is_mask);
		SW_FLOW_KEY_MEMCPY(match, ipv6.nd.tll,
				nd_key->nd_tll, ETH_ALEN, is_mask);
		attrs &= ~(1 << OVS_KEY_ATTR_ND);
	}

	if (attrs != 0)
		return -EINVAL;

	return 0;
}

static void nlattr_set(struct nlattr *attr, u8 val, bool is_attr_mask_key)
{
	struct nlattr *nla;
	int rem;

	/* The nlattr stream should already have been validated */
	nla_for_each_nested(nla, attr, rem) {
		/* We assume that ovs_key_lens[type] == -1 means that type is a
		 * nested attribute
		 */
		if (is_attr_mask_key && ovs_key_lens[nla_type(nla)] == -1)
			nlattr_set(nla, val, false);
		else
			memset(nla_data(nla), val, nla_len(nla));
	}
}

static void mask_set_nlattr(struct nlattr *attr, u8 val)
{
	nlattr_set(attr, val, true);
}

/**
 * ovs_nla_get_match - parses Netlink attributes into a flow key and
 * mask. In case the 'mask' is NULL, the flow is treated as exact match
 * flow. Otherwise, it is treated as a wildcarded flow, except the mask
 * does not include any don't care bit.
 * @match: receives the extracted flow match information.
 * @key: Netlink attribute holding nested %OVS_KEY_ATTR_* Netlink attribute
 * sequence. The fields should of the packet that triggered the creation
 * of this flow.
 * @mask: Optional. Netlink attribute holding nested %OVS_KEY_ATTR_* Netlink
 * attribute specifies the mask field of the wildcarded flow.
 */
int ovs_nla_get_match(struct sw_flow_match *match,
		      const struct nlattr *key,
		      const struct nlattr *mask)
{
	const struct nlattr *a[OVS_KEY_ATTR_MAX + 1];
	const struct nlattr *encap;
	struct nlattr *newmask = NULL;
	u64 key_attrs = 0;
	u64 mask_attrs = 0;
	bool encap_valid = false;
	int err;

	err = parse_flow_nlattrs(key, a, &key_attrs);
	if (err)
		return err;

	if ((key_attrs & (1 << OVS_KEY_ATTR_ETHERNET)) &&
	    (key_attrs & (1 << OVS_KEY_ATTR_ETHERTYPE)) &&
	    (nla_get_be16(a[OVS_KEY_ATTR_ETHERTYPE]) == htons(ETH_P_8021Q))) {
		__be16 tci;

		if (!((key_attrs & (1 << OVS_KEY_ATTR_VLAN)) &&
		      (key_attrs & (1 << OVS_KEY_ATTR_ENCAP)))) {
			OVS_NLERR("Invalid Vlan frame.\n");
			return -EINVAL;
		}

		key_attrs &= ~(1 << OVS_KEY_ATTR_ETHERTYPE);
		tci = nla_get_be16(a[OVS_KEY_ATTR_VLAN]);
		encap = a[OVS_KEY_ATTR_ENCAP];
		key_attrs &= ~(1 << OVS_KEY_ATTR_ENCAP);
		encap_valid = true;

		if (tci & htons(VLAN_TAG_PRESENT)) {
			err = parse_flow_nlattrs(encap, a, &key_attrs);
			if (err)
				return err;
		} else if (!tci) {
			/* Corner case for truncated 802.1Q header. */
			if (nla_len(encap)) {
				OVS_NLERR("Truncated 802.1Q header has non-zero encap attribute.\n");
				return -EINVAL;
			}
		} else {
			OVS_NLERR("Encap attribute is set for a non-VLAN frame.\n");
			return  -EINVAL;
		}
	}

	err = ovs_key_from_nlattrs(match, key_attrs, a, false);
	if (err)
		return err;

	if (match->mask && !mask) {
		/* Create an exact match mask. We need to set to 0xff all the
		 * 'match->mask' fields that have been touched in 'match->key'.
		 * We cannot simply memset 'match->mask', because padding bytes
		 * and fields not specified in 'match->key' should be left to 0.
		 * Instead, we use a stream of netlink attributes, copied from
		 * 'key' and set to 0xff: ovs_key_from_nlattrs() will take care
		 * of filling 'match->mask' appropriately.
		 */
		newmask = kmemdup(key, nla_total_size(nla_len(key)),
				  GFP_KERNEL);
		if (!newmask)
			return -ENOMEM;

		mask_set_nlattr(newmask, 0xff);

		/* The userspace does not send tunnel attributes that are 0,
		 * but we should not wildcard them nonetheless.
		 */
		if (match->key->tun_key.ipv4_dst)
			SW_FLOW_KEY_MEMSET_FIELD(match, tun_key, 0xff, true);

		mask = newmask;
	}

	if (mask) {
		err = parse_flow_mask_nlattrs(mask, a, &mask_attrs);
		if (err)
			goto free_newmask;

		if (mask_attrs & 1 << OVS_KEY_ATTR_ENCAP) {
			__be16 eth_type = 0;
			__be16 tci = 0;

			if (!encap_valid) {
				OVS_NLERR("Encap mask attribute is set for non-VLAN frame.\n");
				err = -EINVAL;
				goto free_newmask;
			}

			mask_attrs &= ~(1 << OVS_KEY_ATTR_ENCAP);
			if (a[OVS_KEY_ATTR_ETHERTYPE])
				eth_type = nla_get_be16(a[OVS_KEY_ATTR_ETHERTYPE]);

			if (eth_type == htons(0xffff)) {
				mask_attrs &= ~(1 << OVS_KEY_ATTR_ETHERTYPE);
				encap = a[OVS_KEY_ATTR_ENCAP];
				err = parse_flow_mask_nlattrs(encap, a, &mask_attrs);
				if (err)
					goto free_newmask;
			} else {
				OVS_NLERR("VLAN frames must have an exact match on the TPID (mask=%x).\n",
						ntohs(eth_type));
				err = -EINVAL;
				goto free_newmask;
			}

			if (a[OVS_KEY_ATTR_VLAN])
				tci = nla_get_be16(a[OVS_KEY_ATTR_VLAN]);

			if (!(tci & htons(VLAN_TAG_PRESENT))) {
				OVS_NLERR("VLAN tag present bit must have an exact match (tci_mask=%x).\n", ntohs(tci));
				err = -EINVAL;
				goto free_newmask;
			}
		}

		err = ovs_key_from_nlattrs(match, mask_attrs, a, true);
		if (err)
			goto free_newmask;
	}

	if (!match_validate(match, key_attrs, mask_attrs))
		err = -EINVAL;

free_newmask:
	kfree(newmask);
	return err;
}

/**
 * ovs_nla_get_flow_metadata - parses Netlink attributes into a flow key.
 * @key: Receives extracted in_port, priority, tun_key and skb_mark.
 * @attr: Netlink attribute holding nested %OVS_KEY_ATTR_* Netlink attribute
 * sequence.
 *
 * This parses a series of Netlink attributes that form a flow key, which must
 * take the same form accepted by flow_from_nlattrs(), but only enough of it to
 * get the metadata, that is, the parts of the flow key that cannot be
 * extracted from the packet itself.
 */

int ovs_nla_get_flow_metadata(const struct nlattr *attr,
			      struct sw_flow_key *key)
{
	const struct nlattr *a[OVS_KEY_ATTR_MAX + 1];
	struct sw_flow_match match;
	u64 attrs = 0;
	int err;

	err = parse_flow_nlattrs(attr, a, &attrs);
	if (err)
		return -EINVAL;

	memset(&match, 0, sizeof(match));
	match.key = key;

	key->phy.in_port = DP_MAX_PORTS;

	return metadata_from_nlattrs(&match, &attrs, a, false);
}

int ovs_nla_put_flow(const struct sw_flow_key *swkey,
		     const struct sw_flow_key *output, struct sk_buff *skb)
{
	struct ovs_key_ethernet *eth_key;
	struct nlattr *nla, *encap;
	bool is_mask = (swkey != output);

	if (nla_put_u32(skb, OVS_KEY_ATTR_RECIRC_ID, output->recirc_id))
		goto nla_put_failure;

	if (nla_put_u32(skb, OVS_KEY_ATTR_DP_HASH, output->ovs_flow_hash))
		goto nla_put_failure;

	if (nla_put_u32(skb, OVS_KEY_ATTR_PRIORITY, output->phy.priority))
		goto nla_put_failure;

	if ((swkey->tun_key.ipv4_dst || is_mask)) {
		const struct geneve_opt *opts = NULL;

		if (output->tun_key.tun_flags & TUNNEL_OPTIONS_PRESENT)
			opts = GENEVE_OPTS(output, swkey->tun_opts_len);

		if (ipv4_tun_to_nlattr(skb, &output->tun_key, opts,
				       swkey->tun_opts_len))
			goto nla_put_failure;
	}

	if (swkey->phy.in_port == DP_MAX_PORTS) {
		if (is_mask && (output->phy.in_port == 0xffff))
			if (nla_put_u32(skb, OVS_KEY_ATTR_IN_PORT, 0xffffffff))
				goto nla_put_failure;
	} else {
		u16 upper_u16;
		upper_u16 = !is_mask ? 0 : 0xffff;

		if (nla_put_u32(skb, OVS_KEY_ATTR_IN_PORT,
				(upper_u16 << 16) | output->phy.in_port))
			goto nla_put_failure;
	}

	if (nla_put_u32(skb, OVS_KEY_ATTR_SKB_MARK, output->phy.skb_mark))
		goto nla_put_failure;

	nla = nla_reserve(skb, OVS_KEY_ATTR_ETHERNET, sizeof(*eth_key));
	if (!nla)
		goto nla_put_failure;

	eth_key = nla_data(nla);
	ether_addr_copy(eth_key->eth_src, output->eth.src);
	ether_addr_copy(eth_key->eth_dst, output->eth.dst);

	if (swkey->eth.tci || swkey->eth.type == htons(ETH_P_8021Q)) {
		__be16 eth_type;
		eth_type = !is_mask ? htons(ETH_P_8021Q) : htons(0xffff);
		if (nla_put_be16(skb, OVS_KEY_ATTR_ETHERTYPE, eth_type) ||
		    nla_put_be16(skb, OVS_KEY_ATTR_VLAN, output->eth.tci))
			goto nla_put_failure;
		encap = nla_nest_start(skb, OVS_KEY_ATTR_ENCAP);
		if (!swkey->eth.tci)
			goto unencap;
	} else
		encap = NULL;

	if (swkey->eth.type == htons(ETH_P_802_2)) {
		/*
		 * Ethertype 802.2 is represented in the netlink with omitted
		 * OVS_KEY_ATTR_ETHERTYPE in the flow key attribute, and
		 * 0xffff in the mask attribute.  Ethertype can also
		 * be wildcarded.
		 */
		if (is_mask && output->eth.type)
			if (nla_put_be16(skb, OVS_KEY_ATTR_ETHERTYPE,
						output->eth.type))
				goto nla_put_failure;
		goto unencap;
	}

	if (nla_put_be16(skb, OVS_KEY_ATTR_ETHERTYPE, output->eth.type))
		goto nla_put_failure;

	if (swkey->eth.type == htons(ETH_P_IP)) {
		struct ovs_key_ipv4 *ipv4_key;

		nla = nla_reserve(skb, OVS_KEY_ATTR_IPV4, sizeof(*ipv4_key));
		if (!nla)
			goto nla_put_failure;
		ipv4_key = nla_data(nla);
		ipv4_key->ipv4_src = output->ipv4.addr.src;
		ipv4_key->ipv4_dst = output->ipv4.addr.dst;
		ipv4_key->ipv4_proto = output->ip.proto;
		ipv4_key->ipv4_tos = output->ip.tos;
		ipv4_key->ipv4_ttl = output->ip.ttl;
		ipv4_key->ipv4_frag = output->ip.frag;
	} else if (swkey->eth.type == htons(ETH_P_IPV6)) {
		struct ovs_key_ipv6 *ipv6_key;

		nla = nla_reserve(skb, OVS_KEY_ATTR_IPV6, sizeof(*ipv6_key));
		if (!nla)
			goto nla_put_failure;
		ipv6_key = nla_data(nla);
		memcpy(ipv6_key->ipv6_src, &output->ipv6.addr.src,
				sizeof(ipv6_key->ipv6_src));
		memcpy(ipv6_key->ipv6_dst, &output->ipv6.addr.dst,
				sizeof(ipv6_key->ipv6_dst));
		ipv6_key->ipv6_label = output->ipv6.label;
		ipv6_key->ipv6_proto = output->ip.proto;
		ipv6_key->ipv6_tclass = output->ip.tos;
		ipv6_key->ipv6_hlimit = output->ip.ttl;
		ipv6_key->ipv6_frag = output->ip.frag;
	} else if (swkey->eth.type == htons(ETH_P_ARP) ||
		   swkey->eth.type == htons(ETH_P_RARP)) {
		struct ovs_key_arp *arp_key;

		nla = nla_reserve(skb, OVS_KEY_ATTR_ARP, sizeof(*arp_key));
		if (!nla)
			goto nla_put_failure;
		arp_key = nla_data(nla);
		memset(arp_key, 0, sizeof(struct ovs_key_arp));
		arp_key->arp_sip = output->ipv4.addr.src;
		arp_key->arp_tip = output->ipv4.addr.dst;
		arp_key->arp_op = htons(output->ip.proto);
		ether_addr_copy(arp_key->arp_sha, output->ipv4.arp.sha);
		ether_addr_copy(arp_key->arp_tha, output->ipv4.arp.tha);
	}

	if ((swkey->eth.type == htons(ETH_P_IP) ||
	     swkey->eth.type == htons(ETH_P_IPV6)) &&
	     swkey->ip.frag != OVS_FRAG_TYPE_LATER) {

		if (swkey->ip.proto == IPPROTO_TCP) {
			struct ovs_key_tcp *tcp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_TCP, sizeof(*tcp_key));
			if (!nla)
				goto nla_put_failure;
			tcp_key = nla_data(nla);
			tcp_key->tcp_src = output->tp.src;
			tcp_key->tcp_dst = output->tp.dst;
			if (nla_put_be16(skb, OVS_KEY_ATTR_TCP_FLAGS,
					 output->tp.flags))
				goto nla_put_failure;
		} else if (swkey->ip.proto == IPPROTO_UDP) {
			struct ovs_key_udp *udp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_UDP, sizeof(*udp_key));
			if (!nla)
				goto nla_put_failure;
			udp_key = nla_data(nla);
			udp_key->udp_src = output->tp.src;
			udp_key->udp_dst = output->tp.dst;
		} else if (swkey->ip.proto == IPPROTO_SCTP) {
			struct ovs_key_sctp *sctp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_SCTP, sizeof(*sctp_key));
			if (!nla)
				goto nla_put_failure;
			sctp_key = nla_data(nla);
			sctp_key->sctp_src = output->tp.src;
			sctp_key->sctp_dst = output->tp.dst;
		} else if (swkey->eth.type == htons(ETH_P_IP) &&
			   swkey->ip.proto == IPPROTO_ICMP) {
			struct ovs_key_icmp *icmp_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_ICMP, sizeof(*icmp_key));
			if (!nla)
				goto nla_put_failure;
			icmp_key = nla_data(nla);
			icmp_key->icmp_type = ntohs(output->tp.src);
			icmp_key->icmp_code = ntohs(output->tp.dst);
		} else if (swkey->eth.type == htons(ETH_P_IPV6) &&
			   swkey->ip.proto == IPPROTO_ICMPV6) {
			struct ovs_key_icmpv6 *icmpv6_key;

			nla = nla_reserve(skb, OVS_KEY_ATTR_ICMPV6,
						sizeof(*icmpv6_key));
			if (!nla)
				goto nla_put_failure;
			icmpv6_key = nla_data(nla);
			icmpv6_key->icmpv6_type = ntohs(output->tp.src);
			icmpv6_key->icmpv6_code = ntohs(output->tp.dst);

			if (icmpv6_key->icmpv6_type == NDISC_NEIGHBOUR_SOLICITATION ||
			    icmpv6_key->icmpv6_type == NDISC_NEIGHBOUR_ADVERTISEMENT) {
				struct ovs_key_nd *nd_key;

				nla = nla_reserve(skb, OVS_KEY_ATTR_ND, sizeof(*nd_key));
				if (!nla)
					goto nla_put_failure;
				nd_key = nla_data(nla);
				memcpy(nd_key->nd_target, &output->ipv6.nd.target,
							sizeof(nd_key->nd_target));
				ether_addr_copy(nd_key->nd_sll, output->ipv6.nd.sll);
				ether_addr_copy(nd_key->nd_tll, output->ipv6.nd.tll);
			}
		}
	}

unencap:
	if (encap)
		nla_nest_end(skb, encap);

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

#define MAX_ACTIONS_BUFSIZE	(32 * 1024)

struct sw_flow_actions *ovs_nla_alloc_flow_actions(int size)
{
	struct sw_flow_actions *sfa;

	if (size > MAX_ACTIONS_BUFSIZE)
		return ERR_PTR(-EINVAL);

	sfa = kmalloc(sizeof(*sfa) + size, GFP_KERNEL);
	if (!sfa)
		return ERR_PTR(-ENOMEM);

	sfa->actions_len = 0;
	return sfa;
}

/* Schedules 'sf_acts' to be freed after the next RCU grace period.
 * The caller must hold rcu_read_lock for this to be sensible. */
void ovs_nla_free_flow_actions(struct sw_flow_actions *sf_acts)
{
	kfree_rcu(sf_acts, rcu);
}

static struct nlattr *reserve_sfa_size(struct sw_flow_actions **sfa,
				       int attr_len)
{

	struct sw_flow_actions *acts;
	int new_acts_size;
	int req_size = NLA_ALIGN(attr_len);
	int next_offset = offsetof(struct sw_flow_actions, actions) +
					(*sfa)->actions_len;

	if (req_size <= (ksize(*sfa) - next_offset))
		goto out;

	new_acts_size = ksize(*sfa) * 2;

	if (new_acts_size > MAX_ACTIONS_BUFSIZE) {
		if ((MAX_ACTIONS_BUFSIZE - next_offset) < req_size)
			return ERR_PTR(-EMSGSIZE);
		new_acts_size = MAX_ACTIONS_BUFSIZE;
	}

	acts = ovs_nla_alloc_flow_actions(new_acts_size);
	if (IS_ERR(acts))
		return (void *)acts;

	memcpy(acts->actions, (*sfa)->actions, (*sfa)->actions_len);
	acts->actions_len = (*sfa)->actions_len;
	kfree(*sfa);
	*sfa = acts;

out:
	(*sfa)->actions_len += req_size;
	return  (struct nlattr *) ((unsigned char *)(*sfa) + next_offset);
}

static struct nlattr *__add_action(struct sw_flow_actions **sfa,
				   int attrtype, void *data, int len)
{
	struct nlattr *a;

	a = reserve_sfa_size(sfa, nla_attr_size(len));
	if (IS_ERR(a))
		return a;

	a->nla_type = attrtype;
	a->nla_len = nla_attr_size(len);

	if (data)
		memcpy(nla_data(a), data, len);
	memset((unsigned char *) a + a->nla_len, 0, nla_padlen(len));

	return a;
}

static int add_action(struct sw_flow_actions **sfa, int attrtype,
		      void *data, int len)
{
	struct nlattr *a;

	a = __add_action(sfa, attrtype, data, len);
	if (IS_ERR(a))
		return PTR_ERR(a);

	return 0;
}

static inline int add_nested_action_start(struct sw_flow_actions **sfa,
					  int attrtype)
{
	int used = (*sfa)->actions_len;
	int err;

	err = add_action(sfa, attrtype, NULL, 0);
	if (err)
		return err;

	return used;
}

static inline void add_nested_action_end(struct sw_flow_actions *sfa,
					 int st_offset)
{
	struct nlattr *a = (struct nlattr *) ((unsigned char *)sfa->actions +
							       st_offset);

	a->nla_len = sfa->actions_len - st_offset;
}

static int validate_and_copy_sample(const struct nlattr *attr,
				    const struct sw_flow_key *key, int depth,
				    struct sw_flow_actions **sfa)
{
	const struct nlattr *attrs[OVS_SAMPLE_ATTR_MAX + 1];
	const struct nlattr *probability, *actions;
	const struct nlattr *a;
	int rem, start, err, st_acts;

	memset(attrs, 0, sizeof(attrs));
	nla_for_each_nested(a, attr, rem) {
		int type = nla_type(a);
		if (!type || type > OVS_SAMPLE_ATTR_MAX || attrs[type])
			return -EINVAL;
		attrs[type] = a;
	}
	if (rem)
		return -EINVAL;

	probability = attrs[OVS_SAMPLE_ATTR_PROBABILITY];
	if (!probability || nla_len(probability) != sizeof(u32))
		return -EINVAL;

	actions = attrs[OVS_SAMPLE_ATTR_ACTIONS];
	if (!actions || (nla_len(actions) && nla_len(actions) < NLA_HDRLEN))
		return -EINVAL;

	/* validation done, copy sample action. */
	start = add_nested_action_start(sfa, OVS_ACTION_ATTR_SAMPLE);
	if (start < 0)
		return start;
	err = add_action(sfa, OVS_SAMPLE_ATTR_PROBABILITY,
			 nla_data(probability), sizeof(u32));
	if (err)
		return err;
	st_acts = add_nested_action_start(sfa, OVS_SAMPLE_ATTR_ACTIONS);
	if (st_acts < 0)
		return st_acts;

	err = ovs_nla_copy_actions(actions, key, depth + 1, sfa);
	if (err)
		return err;

	add_nested_action_end(*sfa, st_acts);
	add_nested_action_end(*sfa, start);

	return 0;
}

static int validate_tp_port(const struct sw_flow_key *flow_key)
{
	if ((flow_key->eth.type == htons(ETH_P_IP) ||
	     flow_key->eth.type == htons(ETH_P_IPV6)) &&
	    (flow_key->tp.src || flow_key->tp.dst))
		return 0;

	return -EINVAL;
}

void ovs_match_init(struct sw_flow_match *match,
		    struct sw_flow_key *key,
		    struct sw_flow_mask *mask)
{
	memset(match, 0, sizeof(*match));
	match->key = key;
	match->mask = mask;

	memset(key, 0, sizeof(*key));

	if (mask) {
		memset(&mask->key, 0, sizeof(mask->key));
		mask->range.start = mask->range.end = 0;
	}
}

static int validate_and_copy_set_tun(const struct nlattr *attr,
				     struct sw_flow_actions **sfa)
{
	struct sw_flow_match match;
	struct sw_flow_key key;
	struct ovs_tunnel_info *tun_info;
	struct nlattr *a;
	int err, start;

	ovs_match_init(&match, &key, NULL);
	err = ipv4_tun_from_nlattr(nla_data(attr), &match, false);
	if (err)
		return err;

	if (key.tun_opts_len) {
		struct geneve_opt *option = GENEVE_OPTS(&key,
							key.tun_opts_len);
		int opts_len = key.tun_opts_len;
		bool crit_opt = false;

		while (opts_len > 0) {
			int len;

			if (opts_len < sizeof(*option))
				return -EINVAL;

			len = sizeof(*option) + option->length * 4;
			if (len > opts_len)
				return -EINVAL;

			crit_opt |= !!(option->type & GENEVE_CRIT_OPT_TYPE);

			option = (struct geneve_opt *)((u8 *)option + len);
			opts_len -= len;
		};

		key.tun_key.tun_flags |= crit_opt ? TUNNEL_CRIT_OPT : 0;
	};

	start = add_nested_action_start(sfa, OVS_ACTION_ATTR_SET);
	if (start < 0)
		return start;

	a = __add_action(sfa, OVS_KEY_ATTR_TUNNEL_INFO, NULL,
			 sizeof(*tun_info) + key.tun_opts_len);
	if (IS_ERR(a))
		return PTR_ERR(a);

	tun_info = nla_data(a);
	tun_info->tunnel = key.tun_key;
	tun_info->options_len = key.tun_opts_len;

	if (tun_info->options_len) {
		/* We need to store the options in the action itself since
		 * everything else will go away after flow setup. We can append
		 * it to tun_info and then point there.
		 */
		memcpy((tun_info + 1), GENEVE_OPTS(&key, key.tun_opts_len),
		       key.tun_opts_len);
		tun_info->options = (struct geneve_opt *)(tun_info + 1);
	} else {
		tun_info->options = NULL;
	}

	add_nested_action_end(*sfa, start);

	return err;
}

static int validate_set(const struct nlattr *a,
			const struct sw_flow_key *flow_key,
			struct sw_flow_actions **sfa,
			bool *set_tun)
{
	const struct nlattr *ovs_key = nla_data(a);
	int key_type = nla_type(ovs_key);

	/* There can be only one key in a action */
	if (nla_total_size(nla_len(ovs_key)) != nla_len(a))
		return -EINVAL;

	if (key_type > OVS_KEY_ATTR_MAX ||
	    (ovs_key_lens[key_type] != nla_len(ovs_key) &&
	     ovs_key_lens[key_type] != -1))
		return -EINVAL;

	switch (key_type) {
	const struct ovs_key_ipv4 *ipv4_key;
	const struct ovs_key_ipv6 *ipv6_key;
	int err;

	case OVS_KEY_ATTR_PRIORITY:
	case OVS_KEY_ATTR_SKB_MARK:
	case OVS_KEY_ATTR_ETHERNET:
		break;

	case OVS_KEY_ATTR_TUNNEL:
		*set_tun = true;
		err = validate_and_copy_set_tun(a, sfa);
		if (err)
			return err;
		break;

	case OVS_KEY_ATTR_IPV4:
		if (flow_key->eth.type != htons(ETH_P_IP))
			return -EINVAL;

		if (!flow_key->ip.proto)
			return -EINVAL;

		ipv4_key = nla_data(ovs_key);
		if (ipv4_key->ipv4_proto != flow_key->ip.proto)
			return -EINVAL;

		if (ipv4_key->ipv4_frag != flow_key->ip.frag)
			return -EINVAL;

		break;

	case OVS_KEY_ATTR_IPV6:
		if (flow_key->eth.type != htons(ETH_P_IPV6))
			return -EINVAL;

		if (!flow_key->ip.proto)
			return -EINVAL;

		ipv6_key = nla_data(ovs_key);
		if (ipv6_key->ipv6_proto != flow_key->ip.proto)
			return -EINVAL;

		if (ipv6_key->ipv6_frag != flow_key->ip.frag)
			return -EINVAL;

		if (ntohl(ipv6_key->ipv6_label) & 0xFFF00000)
			return -EINVAL;

		break;

	case OVS_KEY_ATTR_TCP:
		if (flow_key->ip.proto != IPPROTO_TCP)
			return -EINVAL;

		return validate_tp_port(flow_key);

	case OVS_KEY_ATTR_UDP:
		if (flow_key->ip.proto != IPPROTO_UDP)
			return -EINVAL;

		return validate_tp_port(flow_key);

	case OVS_KEY_ATTR_SCTP:
		if (flow_key->ip.proto != IPPROTO_SCTP)
			return -EINVAL;

		return validate_tp_port(flow_key);

	default:
		return -EINVAL;
	}

	return 0;
}

static int validate_userspace(const struct nlattr *attr)
{
	static const struct nla_policy userspace_policy[OVS_USERSPACE_ATTR_MAX + 1] = {
		[OVS_USERSPACE_ATTR_PID] = {.type = NLA_U32 },
		[OVS_USERSPACE_ATTR_USERDATA] = {.type = NLA_UNSPEC },
	};
	struct nlattr *a[OVS_USERSPACE_ATTR_MAX + 1];
	int error;

	error = nla_parse_nested(a, OVS_USERSPACE_ATTR_MAX,
				 attr, userspace_policy);
	if (error)
		return error;

	if (!a[OVS_USERSPACE_ATTR_PID] ||
	    !nla_get_u32(a[OVS_USERSPACE_ATTR_PID]))
		return -EINVAL;

	return 0;
}

static int copy_action(const struct nlattr *from,
		       struct sw_flow_actions **sfa)
{
	int totlen = NLA_ALIGN(from->nla_len);
	struct nlattr *to;

	to = reserve_sfa_size(sfa, from->nla_len);
	if (IS_ERR(to))
		return PTR_ERR(to);

	memcpy(to, from, totlen);
	return 0;
}

int ovs_nla_copy_actions(const struct nlattr *attr,
			 const struct sw_flow_key *key,
			 int depth,
			 struct sw_flow_actions **sfa)
{
	const struct nlattr *a;
	int rem, err;

	if (depth >= SAMPLE_ACTION_DEPTH)
		return -EOVERFLOW;

	nla_for_each_nested(a, attr, rem) {
		/* Expected argument lengths, (u32)-1 for variable length. */
		static const u32 action_lens[OVS_ACTION_ATTR_MAX + 1] = {
			[OVS_ACTION_ATTR_OUTPUT] = sizeof(u32),
			[OVS_ACTION_ATTR_RECIRC] = sizeof(u32),
			[OVS_ACTION_ATTR_USERSPACE] = (u32)-1,
			[OVS_ACTION_ATTR_PUSH_VLAN] = sizeof(struct ovs_action_push_vlan),
			[OVS_ACTION_ATTR_POP_VLAN] = 0,
			[OVS_ACTION_ATTR_SET] = (u32)-1,
			[OVS_ACTION_ATTR_SAMPLE] = (u32)-1,
			[OVS_ACTION_ATTR_HASH] = sizeof(struct ovs_action_hash)
		};
		const struct ovs_action_push_vlan *vlan;
		int type = nla_type(a);
		bool skip_copy;

		if (type > OVS_ACTION_ATTR_MAX ||
		    (action_lens[type] != nla_len(a) &&
		     action_lens[type] != (u32)-1))
			return -EINVAL;

		skip_copy = false;
		switch (type) {
		case OVS_ACTION_ATTR_UNSPEC:
			return -EINVAL;

		case OVS_ACTION_ATTR_USERSPACE:
			err = validate_userspace(a);
			if (err)
				return err;
			break;

		case OVS_ACTION_ATTR_OUTPUT:
			if (nla_get_u32(a) >= DP_MAX_PORTS)
				return -EINVAL;
			break;

		case OVS_ACTION_ATTR_HASH: {
			const struct ovs_action_hash *act_hash = nla_data(a);

			switch (act_hash->hash_alg) {
			case OVS_HASH_ALG_L4:
				break;
			default:
				return  -EINVAL;
			}

			break;
		}

		case OVS_ACTION_ATTR_POP_VLAN:
			break;

		case OVS_ACTION_ATTR_PUSH_VLAN:
			vlan = nla_data(a);
			if (vlan->vlan_tpid != htons(ETH_P_8021Q))
				return -EINVAL;
			if (!(vlan->vlan_tci & htons(VLAN_TAG_PRESENT)))
				return -EINVAL;
			break;

		case OVS_ACTION_ATTR_RECIRC:
			break;

		case OVS_ACTION_ATTR_SET:
			err = validate_set(a, key, sfa, &skip_copy);
			if (err)
				return err;
			break;

		case OVS_ACTION_ATTR_SAMPLE:
			err = validate_and_copy_sample(a, key, depth, sfa);
			if (err)
				return err;
			skip_copy = true;
			break;

		default:
			return -EINVAL;
		}
		if (!skip_copy) {
			err = copy_action(a, sfa);
			if (err)
				return err;
		}
	}

	if (rem > 0)
		return -EINVAL;

	return 0;
}

static int sample_action_to_attr(const struct nlattr *attr, struct sk_buff *skb)
{
	const struct nlattr *a;
	struct nlattr *start;
	int err = 0, rem;

	start = nla_nest_start(skb, OVS_ACTION_ATTR_SAMPLE);
	if (!start)
		return -EMSGSIZE;

	nla_for_each_nested(a, attr, rem) {
		int type = nla_type(a);
		struct nlattr *st_sample;

		switch (type) {
		case OVS_SAMPLE_ATTR_PROBABILITY:
			if (nla_put(skb, OVS_SAMPLE_ATTR_PROBABILITY,
				    sizeof(u32), nla_data(a)))
				return -EMSGSIZE;
			break;
		case OVS_SAMPLE_ATTR_ACTIONS:
			st_sample = nla_nest_start(skb, OVS_SAMPLE_ATTR_ACTIONS);
			if (!st_sample)
				return -EMSGSIZE;
			err = ovs_nla_put_actions(nla_data(a), nla_len(a), skb);
			if (err)
				return err;
			nla_nest_end(skb, st_sample);
			break;
		}
	}

	nla_nest_end(skb, start);
	return err;
}

static int set_action_to_attr(const struct nlattr *a, struct sk_buff *skb)
{
	const struct nlattr *ovs_key = nla_data(a);
	int key_type = nla_type(ovs_key);
	struct nlattr *start;
	int err;

	switch (key_type) {
	case OVS_KEY_ATTR_TUNNEL_INFO: {
		struct ovs_tunnel_info *tun_info = nla_data(ovs_key);

		start = nla_nest_start(skb, OVS_ACTION_ATTR_SET);
		if (!start)
			return -EMSGSIZE;

		err = ipv4_tun_to_nlattr(skb, &tun_info->tunnel,
					 tun_info->options_len ?
						tun_info->options : NULL,
					 tun_info->options_len);
		if (err)
			return err;
		nla_nest_end(skb, start);
		break;
	}
	default:
		if (nla_put(skb, OVS_ACTION_ATTR_SET, nla_len(a), ovs_key))
			return -EMSGSIZE;
		break;
	}

	return 0;
}

int ovs_nla_put_actions(const struct nlattr *attr, int len, struct sk_buff *skb)
{
	const struct nlattr *a;
	int rem, err;

	nla_for_each_attr(a, attr, len, rem) {
		int type = nla_type(a);

		switch (type) {
		case OVS_ACTION_ATTR_SET:
			err = set_action_to_attr(a, skb);
			if (err)
				return err;
			break;

		case OVS_ACTION_ATTR_SAMPLE:
			err = sample_action_to_attr(a, skb);
			if (err)
				return err;
			break;
		default:
			if (nla_put(skb, type, nla_len(a), nla_data(a)))
				return -EMSGSIZE;
			break;
		}
	}

	return 0;
}
