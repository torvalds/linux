// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/etherdevice.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/spinlock.h>
#include <linux/netfilter/nf_conntrack_common.h>
#include <linux/netfilter/nf_tables.h>
#include <net/ip.h>
#include <net/inet_dscp.h>
#include <net/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_flow_table.h>

static enum flow_offload_xmit_type nft_xmit_type(struct dst_entry *dst)
{
	if (dst_xfrm(dst))
		return FLOW_OFFLOAD_XMIT_XFRM;

	return FLOW_OFFLOAD_XMIT_NEIGH;
}

static void nft_default_forward_path(struct nf_flow_route *route,
				     struct dst_entry *dst_cache,
				     enum ip_conntrack_dir dir)
{
	route->tuple[!dir].in.ifindex	= dst_cache->dev->ifindex;
	route->tuple[dir].dst		= dst_cache;
	route->tuple[dir].xmit_type	= nft_xmit_type(dst_cache);
}

static bool nft_is_valid_ether_device(const struct net_device *dev)
{
	if (!dev || (dev->flags & IFF_LOOPBACK) || dev->type != ARPHRD_ETHER ||
	    dev->addr_len != ETH_ALEN || !is_valid_ether_addr(dev->dev_addr))
		return false;

	return true;
}

static int nft_dev_fill_forward_path(const struct dst_entry *dst_cache,
				     const struct nf_conn *ct,
				     enum ip_conntrack_dir dir,
				     u8 *ha, __be16 ether_type,
				     struct net_device_path_stack *stack)
{
	const void *daddr = &ct->tuplehash[!dir].tuple.src.u3;
	struct net_device *dev = dst_cache->dev;
	struct net_device_path_ctx ctx = {
		.dev = dev,
		.ether_type = ether_type,
	};
	struct neighbour *n;
	u8 nud_state;

	if (!nft_is_valid_ether_device(dev)) {
		eth_zero_addr(ha);
		goto out;
	}

	n = dst_neigh_lookup(dst_cache, daddr);
	if (!n)
		return -1;

	read_lock_bh(&n->lock);
	nud_state = n->nud_state;
	ether_addr_copy(ha, n->ha);
	read_unlock_bh(&n->lock);
	neigh_release(n);

	if (!(nud_state & NUD_VALID))
		return -1;

out:
	ether_addr_copy(ctx.daddr, ha);

	return dev_fill_forward_path(&ctx, stack);
}

struct nft_forward_info {
	const struct net_device *dev;
	struct id {
		__u16	id;
		__be16	proto;
	} encap[NF_FLOW_TABLE_ENCAP_MAX];
	u8 num_encaps;
	struct flow_offload_tunnel tun;
	struct dst_entry *tun_dst;
	u8 num_tuns;
	u8 ingress_vlans;
	u8 h_source[ETH_ALEN];
	u8 h_dest[ETH_ALEN];
	bool needs_gso_segment;
	enum flow_offload_xmit_type xmit_type;
};

static bool nft_flowtable_find_dev(const struct net_device *dev,
				   struct nft_flowtable *ft);

static int nft_dev_path_info(struct net_device_path_stack *stack,
			     struct nft_forward_info *info,
			     unsigned char *ha, struct nft_flowtable *ft)
{
	const struct net_device_path *path;
	int i;

	memcpy(info->h_dest, ha, ETH_ALEN);

	for (i = 0; i < stack->num_paths; i++) {
		path = &stack->path[i];
		switch (path->type) {
		case DEV_PATH_ETHERNET:
		case DEV_PATH_DSA:
		case DEV_PATH_VLAN:
		case DEV_PATH_PPPOE:
		case DEV_PATH_TUN:
			info->dev = path->dev;
			if (is_zero_ether_addr(info->h_source))
				memcpy(info->h_source, path->dev->dev_addr, ETH_ALEN);

			if (path->type == DEV_PATH_ETHERNET ||
			    path->type == DEV_PATH_DSA)
				break;

			/* DEV_PATH_VLAN, DEV_PATH_PPPOE and DEV_PATH_TUN */
			if (path->type == DEV_PATH_TUN) {
				if (info->num_tuns)
					goto err_out;

				info->tun.src_v6 = path->tun.src_v6;
				info->tun.dst_v6 = path->tun.dst_v6;
				info->tun.inner_proto = path->tun.inner_proto;
				info->tun_dst = path->tun.dst;
				info->num_tuns++;
			} else {
				if (info->num_encaps >= NF_FLOW_TABLE_ENCAP_MAX)
					goto err_out;

				info->encap[info->num_encaps].id =
					path->encap.id;
				info->encap[info->num_encaps].proto =
					path->encap.proto;
				info->num_encaps++;
			}
			if (path->type == DEV_PATH_PPPOE) {
				memcpy(info->h_dest, path->encap.h_dest, ETH_ALEN);
				info->xmit_type = FLOW_OFFLOAD_XMIT_DIRECT;
				info->needs_gso_segment = 1;
			}
			break;
		case DEV_PATH_BRIDGE:
			if (is_zero_ether_addr(info->h_source))
				memcpy(info->h_source, path->dev->dev_addr, ETH_ALEN);

			switch (path->bridge.vlan_mode) {
			case DEV_PATH_BR_VLAN_UNTAG_HW:
				if (info->num_encaps == 0)
					goto err_out;

				info->ingress_vlans |= BIT(info->num_encaps - 1);
				break;
			case DEV_PATH_BR_VLAN_TAG:
				if (info->num_encaps >= NF_FLOW_TABLE_ENCAP_MAX)
					goto err_out;

				info->encap[info->num_encaps].id = path->bridge.vlan_id;
				info->encap[info->num_encaps].proto = path->bridge.vlan_proto;
				info->num_encaps++;
				break;
			case DEV_PATH_BR_VLAN_UNTAG:
				if (info->num_encaps == 0)
					goto err_out;

				info->num_encaps--;
				break;
			case DEV_PATH_BR_VLAN_KEEP:
				break;
			}
			info->xmit_type = FLOW_OFFLOAD_XMIT_DIRECT;
			break;
		default:
			goto err_out;
		}
	}

	if (nf_flowtable_hw_offload(&ft->data) &&
	    nft_is_valid_ether_device(info->dev))
		info->xmit_type = FLOW_OFFLOAD_XMIT_DIRECT;

	if (!nft_flowtable_find_dev(info->dev, ft))
		goto err_out;

	return 0;
err_out:
	dev_fill_forward_path_release(stack);

	return -1;
}

static bool nft_flowtable_find_dev(const struct net_device *dev,
				   struct nft_flowtable *ft)
{
	struct nft_hook *hook;
	bool found = false;

	list_for_each_entry_rcu(hook, &ft->hook_list, list) {
		if (!nft_hook_find_ops_rcu(hook, dev))
			continue;

		found = true;
		break;
	}

	return found;
}

static int nft_dev_forward_path(const struct nft_pktinfo *pkt,
				struct nf_flow_route *route,
				const struct nf_conn *ct,
				enum ip_conntrack_dir dir,
				struct nft_flowtable *ft)
{
	const struct dst_entry *dst = route->tuple[dir].dst;
	struct net_device_path_stack stack;
	struct nft_forward_info info = {};
	unsigned char ha[ETH_ALEN];
	int i;

	if (nft_dev_fill_forward_path(dst, ct, dir, ha, pkt->ethertype, &stack) < 0 ||
	    nft_dev_path_info(&stack, &info, ha, ft) < 0)
		return -ENOENT;

	route->tuple[!dir].in.ifindex = info.dev->ifindex;
	route->tuple[dir].out.ifindex = info.dev->ifindex;

	for (i = 0; i < info.num_encaps; i++) {
		route->tuple[!dir].in.encap[i].id = info.encap[i].id;
		route->tuple[!dir].in.encap[i].proto = info.encap[i].proto;
	}

	if (info.num_tuns) {
		route->tuple[!dir].in.tun.src_v6 = info.tun.dst_v6;
		route->tuple[!dir].in.tun.dst_v6 = info.tun.src_v6;
		route->tuple[!dir].in.tun.inner_proto = info.tun.inner_proto;
		route->tuple[!dir].in.num_tuns = info.num_tuns;
		dst_release(route->tuple[dir].dst);
		route->tuple[dir].dst = info.tun_dst;
	}

	route->tuple[!dir].in.num_encaps = info.num_encaps;
	route->tuple[!dir].in.ingress_vlans = info.ingress_vlans;

	if (info.xmit_type == FLOW_OFFLOAD_XMIT_DIRECT) {
		memcpy(route->tuple[dir].out.h_source, info.h_source, ETH_ALEN);
		memcpy(route->tuple[dir].out.h_dest, info.h_dest, ETH_ALEN);
		route->tuple[dir].xmit_type = info.xmit_type;
	}
	route->tuple[dir].out.needs_gso_segment = info.needs_gso_segment;

	return 0;
}

int nft_flow_route(const struct nft_pktinfo *pkt, const struct nf_conn *ct,
		   struct nf_flow_route *route, enum ip_conntrack_dir dir,
		   struct nft_flowtable *ft)
{
	struct dst_entry *this_dst = skb_dst(pkt->skb);
	struct dst_entry *other_dst = NULL;
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));
	switch (nft_pf(pkt)) {
	case NFPROTO_IPV4:
		fl.u.ip4.daddr = ct->tuplehash[dir].tuple.src.u3.ip;
		fl.u.ip4.saddr = ct->tuplehash[!dir].tuple.src.u3.ip;
		fl.u.ip4.flowi4_oif = nft_in(pkt)->ifindex;
		fl.u.ip4.flowi4_iif = this_dst->dev->ifindex;
		fl.u.ip4.flowi4_dscp = ip4h_dscp(ip_hdr(pkt->skb));
		fl.u.ip4.flowi4_mark = pkt->skb->mark;
		fl.u.ip4.flowi4_flags = FLOWI_FLAG_ANYSRC;
		break;
	case NFPROTO_IPV6:
		fl.u.ip6.daddr = ct->tuplehash[dir].tuple.src.u3.in6;
		fl.u.ip6.saddr = ct->tuplehash[!dir].tuple.src.u3.in6;
		fl.u.ip6.flowi6_oif = nft_in(pkt)->ifindex;
		fl.u.ip6.flowi6_iif = this_dst->dev->ifindex;
		fl.u.ip6.flowlabel = ip6_flowinfo(ipv6_hdr(pkt->skb));
		fl.u.ip6.flowi6_mark = pkt->skb->mark;
		fl.u.ip6.flowi6_flags = FLOWI_FLAG_ANYSRC;
		break;
	}

	if (!dst_hold_safe(this_dst))
		return -ENOENT;

	nf_route(nft_net(pkt), &other_dst, &fl, false, nft_pf(pkt));
	if (!other_dst) {
		dst_release(this_dst);
		return -ENOENT;
	}

	nft_default_forward_path(route, this_dst, dir);
	nft_default_forward_path(route, other_dst, !dir);

	if (route->tuple[dir].xmit_type	== FLOW_OFFLOAD_XMIT_NEIGH &&
	    nft_dev_forward_path(pkt, route, ct, dir, ft) < 0)
		goto err_dst_release;

	if (route->tuple[!dir].xmit_type == FLOW_OFFLOAD_XMIT_NEIGH &&
	    nft_dev_forward_path(pkt, route, ct, !dir, ft) < 0)
		goto err_dst_release;

	return 0;

err_dst_release:
	dst_release(route->tuple[dir].dst);
	dst_release(route->tuple[!dir].dst);
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(nft_flow_route);
