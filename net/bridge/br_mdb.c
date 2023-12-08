// SPDX-License-Identifier: GPL-2.0
#include <linux/err.h>
#include <linux/igmp.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <net/ip.h>
#include <net/netlink.h>
#include <net/switchdev.h>
#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/addrconf.h>
#endif

#include "br_private.h"

static bool
br_ip4_rports_get_timer(struct net_bridge_mcast_port *pmctx,
			unsigned long *timer)
{
	*timer = br_timer_value(&pmctx->ip4_mc_router_timer);
	return !hlist_unhashed(&pmctx->ip4_rlist);
}

static bool
br_ip6_rports_get_timer(struct net_bridge_mcast_port *pmctx,
			unsigned long *timer)
{
#if IS_ENABLED(CONFIG_IPV6)
	*timer = br_timer_value(&pmctx->ip6_mc_router_timer);
	return !hlist_unhashed(&pmctx->ip6_rlist);
#else
	*timer = 0;
	return false;
#endif
}

static size_t __br_rports_one_size(void)
{
	return nla_total_size(sizeof(u32)) + /* MDBA_ROUTER_PORT */
	       nla_total_size(sizeof(u32)) + /* MDBA_ROUTER_PATTR_TIMER */
	       nla_total_size(sizeof(u8)) +  /* MDBA_ROUTER_PATTR_TYPE */
	       nla_total_size(sizeof(u32)) + /* MDBA_ROUTER_PATTR_INET_TIMER */
	       nla_total_size(sizeof(u32)) + /* MDBA_ROUTER_PATTR_INET6_TIMER */
	       nla_total_size(sizeof(u32));  /* MDBA_ROUTER_PATTR_VID */
}

size_t br_rports_size(const struct net_bridge_mcast *brmctx)
{
	struct net_bridge_mcast_port *pmctx;
	size_t size = nla_total_size(0); /* MDBA_ROUTER */

	rcu_read_lock();
	hlist_for_each_entry_rcu(pmctx, &brmctx->ip4_mc_router_list,
				 ip4_rlist)
		size += __br_rports_one_size();

#if IS_ENABLED(CONFIG_IPV6)
	hlist_for_each_entry_rcu(pmctx, &brmctx->ip6_mc_router_list,
				 ip6_rlist)
		size += __br_rports_one_size();
#endif
	rcu_read_unlock();

	return size;
}

int br_rports_fill_info(struct sk_buff *skb,
			const struct net_bridge_mcast *brmctx)
{
	u16 vid = brmctx->vlan ? brmctx->vlan->vid : 0;
	bool have_ip4_mc_rtr, have_ip6_mc_rtr;
	unsigned long ip4_timer, ip6_timer;
	struct nlattr *nest, *port_nest;
	struct net_bridge_port *p;

	if (!brmctx->multicast_router || !br_rports_have_mc_router(brmctx))
		return 0;

	nest = nla_nest_start_noflag(skb, MDBA_ROUTER);
	if (nest == NULL)
		return -EMSGSIZE;

	list_for_each_entry_rcu(p, &brmctx->br->port_list, list) {
		struct net_bridge_mcast_port *pmctx;

		if (vid) {
			struct net_bridge_vlan *v;

			v = br_vlan_find(nbp_vlan_group(p), vid);
			if (!v)
				continue;
			pmctx = &v->port_mcast_ctx;
		} else {
			pmctx = &p->multicast_ctx;
		}

		have_ip4_mc_rtr = br_ip4_rports_get_timer(pmctx, &ip4_timer);
		have_ip6_mc_rtr = br_ip6_rports_get_timer(pmctx, &ip6_timer);

		if (!have_ip4_mc_rtr && !have_ip6_mc_rtr)
			continue;

		port_nest = nla_nest_start_noflag(skb, MDBA_ROUTER_PORT);
		if (!port_nest)
			goto fail;

		if (nla_put_nohdr(skb, sizeof(u32), &p->dev->ifindex) ||
		    nla_put_u32(skb, MDBA_ROUTER_PATTR_TIMER,
				max(ip4_timer, ip6_timer)) ||
		    nla_put_u8(skb, MDBA_ROUTER_PATTR_TYPE,
			       p->multicast_ctx.multicast_router) ||
		    (have_ip4_mc_rtr &&
		     nla_put_u32(skb, MDBA_ROUTER_PATTR_INET_TIMER,
				 ip4_timer)) ||
		    (have_ip6_mc_rtr &&
		     nla_put_u32(skb, MDBA_ROUTER_PATTR_INET6_TIMER,
				 ip6_timer)) ||
		    (vid && nla_put_u16(skb, MDBA_ROUTER_PATTR_VID, vid))) {
			nla_nest_cancel(skb, port_nest);
			goto fail;
		}
		nla_nest_end(skb, port_nest);
	}

	nla_nest_end(skb, nest);
	return 0;
fail:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static void __mdb_entry_fill_flags(struct br_mdb_entry *e, unsigned char flags)
{
	e->state = flags & MDB_PG_FLAGS_PERMANENT;
	e->flags = 0;
	if (flags & MDB_PG_FLAGS_OFFLOAD)
		e->flags |= MDB_FLAGS_OFFLOAD;
	if (flags & MDB_PG_FLAGS_FAST_LEAVE)
		e->flags |= MDB_FLAGS_FAST_LEAVE;
	if (flags & MDB_PG_FLAGS_STAR_EXCL)
		e->flags |= MDB_FLAGS_STAR_EXCL;
	if (flags & MDB_PG_FLAGS_BLOCKED)
		e->flags |= MDB_FLAGS_BLOCKED;
}

static void __mdb_entry_to_br_ip(struct br_mdb_entry *entry, struct br_ip *ip,
				 struct nlattr **mdb_attrs)
{
	memset(ip, 0, sizeof(struct br_ip));
	ip->vid = entry->vid;
	ip->proto = entry->addr.proto;
	switch (ip->proto) {
	case htons(ETH_P_IP):
		ip->dst.ip4 = entry->addr.u.ip4;
		if (mdb_attrs && mdb_attrs[MDBE_ATTR_SOURCE])
			ip->src.ip4 = nla_get_in_addr(mdb_attrs[MDBE_ATTR_SOURCE]);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		ip->dst.ip6 = entry->addr.u.ip6;
		if (mdb_attrs && mdb_attrs[MDBE_ATTR_SOURCE])
			ip->src.ip6 = nla_get_in6_addr(mdb_attrs[MDBE_ATTR_SOURCE]);
		break;
#endif
	default:
		ether_addr_copy(ip->dst.mac_addr, entry->addr.u.mac_addr);
	}

}

static int __mdb_fill_srcs(struct sk_buff *skb,
			   struct net_bridge_port_group *p)
{
	struct net_bridge_group_src *ent;
	struct nlattr *nest, *nest_ent;

	if (hlist_empty(&p->src_list))
		return 0;

	nest = nla_nest_start(skb, MDBA_MDB_EATTR_SRC_LIST);
	if (!nest)
		return -EMSGSIZE;

	hlist_for_each_entry_rcu(ent, &p->src_list, node,
				 lockdep_is_held(&p->key.port->br->multicast_lock)) {
		nest_ent = nla_nest_start(skb, MDBA_MDB_SRCLIST_ENTRY);
		if (!nest_ent)
			goto out_cancel_err;
		switch (ent->addr.proto) {
		case htons(ETH_P_IP):
			if (nla_put_in_addr(skb, MDBA_MDB_SRCATTR_ADDRESS,
					    ent->addr.src.ip4)) {
				nla_nest_cancel(skb, nest_ent);
				goto out_cancel_err;
			}
			break;
#if IS_ENABLED(CONFIG_IPV6)
		case htons(ETH_P_IPV6):
			if (nla_put_in6_addr(skb, MDBA_MDB_SRCATTR_ADDRESS,
					     &ent->addr.src.ip6)) {
				nla_nest_cancel(skb, nest_ent);
				goto out_cancel_err;
			}
			break;
#endif
		default:
			nla_nest_cancel(skb, nest_ent);
			continue;
		}
		if (nla_put_u32(skb, MDBA_MDB_SRCATTR_TIMER,
				br_timer_value(&ent->timer))) {
			nla_nest_cancel(skb, nest_ent);
			goto out_cancel_err;
		}
		nla_nest_end(skb, nest_ent);
	}

	nla_nest_end(skb, nest);

	return 0;

out_cancel_err:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int __mdb_fill_info(struct sk_buff *skb,
			   struct net_bridge_mdb_entry *mp,
			   struct net_bridge_port_group *p)
{
	bool dump_srcs_mode = false;
	struct timer_list *mtimer;
	struct nlattr *nest_ent;
	struct br_mdb_entry e;
	u8 flags = 0;
	int ifindex;

	memset(&e, 0, sizeof(e));
	if (p) {
		ifindex = p->key.port->dev->ifindex;
		mtimer = &p->timer;
		flags = p->flags;
	} else {
		ifindex = mp->br->dev->ifindex;
		mtimer = &mp->timer;
	}

	__mdb_entry_fill_flags(&e, flags);
	e.ifindex = ifindex;
	e.vid = mp->addr.vid;
	if (mp->addr.proto == htons(ETH_P_IP)) {
		e.addr.u.ip4 = mp->addr.dst.ip4;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (mp->addr.proto == htons(ETH_P_IPV6)) {
		e.addr.u.ip6 = mp->addr.dst.ip6;
#endif
	} else {
		ether_addr_copy(e.addr.u.mac_addr, mp->addr.dst.mac_addr);
		e.state = MDB_PG_FLAGS_PERMANENT;
	}
	e.addr.proto = mp->addr.proto;
	nest_ent = nla_nest_start_noflag(skb,
					 MDBA_MDB_ENTRY_INFO);
	if (!nest_ent)
		return -EMSGSIZE;

	if (nla_put_nohdr(skb, sizeof(e), &e) ||
	    nla_put_u32(skb,
			MDBA_MDB_EATTR_TIMER,
			br_timer_value(mtimer)))
		goto nest_err;

	switch (mp->addr.proto) {
	case htons(ETH_P_IP):
		dump_srcs_mode = !!(mp->br->multicast_ctx.multicast_igmp_version == 3);
		if (mp->addr.src.ip4) {
			if (nla_put_in_addr(skb, MDBA_MDB_EATTR_SOURCE,
					    mp->addr.src.ip4))
				goto nest_err;
			break;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		dump_srcs_mode = !!(mp->br->multicast_ctx.multicast_mld_version == 2);
		if (!ipv6_addr_any(&mp->addr.src.ip6)) {
			if (nla_put_in6_addr(skb, MDBA_MDB_EATTR_SOURCE,
					     &mp->addr.src.ip6))
				goto nest_err;
			break;
		}
		break;
#endif
	default:
		ether_addr_copy(e.addr.u.mac_addr, mp->addr.dst.mac_addr);
	}
	if (p) {
		if (nla_put_u8(skb, MDBA_MDB_EATTR_RTPROT, p->rt_protocol))
			goto nest_err;
		if (dump_srcs_mode &&
		    (__mdb_fill_srcs(skb, p) ||
		     nla_put_u8(skb, MDBA_MDB_EATTR_GROUP_MODE,
				p->filter_mode)))
			goto nest_err;
	}
	nla_nest_end(skb, nest_ent);

	return 0;

nest_err:
	nla_nest_cancel(skb, nest_ent);
	return -EMSGSIZE;
}

static int br_mdb_fill_info(struct sk_buff *skb, struct netlink_callback *cb,
			    struct net_device *dev)
{
	int idx = 0, s_idx = cb->args[1], err = 0, pidx = 0, s_pidx = cb->args[2];
	struct net_bridge *br = netdev_priv(dev);
	struct net_bridge_mdb_entry *mp;
	struct nlattr *nest, *nest2;

	if (!br_opt_get(br, BROPT_MULTICAST_ENABLED))
		return 0;

	nest = nla_nest_start_noflag(skb, MDBA_MDB);
	if (nest == NULL)
		return -EMSGSIZE;

	hlist_for_each_entry_rcu(mp, &br->mdb_list, mdb_node) {
		struct net_bridge_port_group *p;
		struct net_bridge_port_group __rcu **pp;

		if (idx < s_idx)
			goto skip;

		nest2 = nla_nest_start_noflag(skb, MDBA_MDB_ENTRY);
		if (!nest2) {
			err = -EMSGSIZE;
			break;
		}

		if (!s_pidx && mp->host_joined) {
			err = __mdb_fill_info(skb, mp, NULL);
			if (err) {
				nla_nest_cancel(skb, nest2);
				break;
			}
		}

		for (pp = &mp->ports; (p = rcu_dereference(*pp)) != NULL;
		      pp = &p->next) {
			if (!p->key.port)
				continue;
			if (pidx < s_pidx)
				goto skip_pg;

			err = __mdb_fill_info(skb, mp, p);
			if (err) {
				nla_nest_end(skb, nest2);
				goto out;
			}
skip_pg:
			pidx++;
		}
		pidx = 0;
		s_pidx = 0;
		nla_nest_end(skb, nest2);
skip:
		idx++;
	}

out:
	cb->args[1] = idx;
	cb->args[2] = pidx;
	nla_nest_end(skb, nest);
	return err;
}

static int br_mdb_valid_dump_req(const struct nlmsghdr *nlh,
				 struct netlink_ext_ack *extack)
{
	struct br_port_msg *bpm;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*bpm))) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid header for mdb dump request");
		return -EINVAL;
	}

	bpm = nlmsg_data(nlh);
	if (bpm->ifindex) {
		NL_SET_ERR_MSG_MOD(extack, "Filtering by device index is not supported for mdb dump request");
		return -EINVAL;
	}
	if (nlmsg_attrlen(nlh, sizeof(*bpm))) {
		NL_SET_ERR_MSG(extack, "Invalid data after header in mdb dump request");
		return -EINVAL;
	}

	return 0;
}

static int br_mdb_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net_device *dev;
	struct net *net = sock_net(skb->sk);
	struct nlmsghdr *nlh = NULL;
	int idx = 0, s_idx;

	if (cb->strict_check) {
		int err = br_mdb_valid_dump_req(cb->nlh, cb->extack);

		if (err < 0)
			return err;
	}

	s_idx = cb->args[0];

	rcu_read_lock();

	cb->seq = net->dev_base_seq;

	for_each_netdev_rcu(net, dev) {
		if (netif_is_bridge_master(dev)) {
			struct net_bridge *br = netdev_priv(dev);
			struct br_port_msg *bpm;

			if (idx < s_idx)
				goto skip;

			nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid,
					cb->nlh->nlmsg_seq, RTM_GETMDB,
					sizeof(*bpm), NLM_F_MULTI);
			if (nlh == NULL)
				break;

			bpm = nlmsg_data(nlh);
			memset(bpm, 0, sizeof(*bpm));
			bpm->ifindex = dev->ifindex;
			if (br_mdb_fill_info(skb, cb, dev) < 0)
				goto out;
			if (br_rports_fill_info(skb, &br->multicast_ctx) < 0)
				goto out;

			cb->args[1] = 0;
			nlmsg_end(skb, nlh);
		skip:
			idx++;
		}
	}

out:
	if (nlh)
		nlmsg_end(skb, nlh);
	rcu_read_unlock();
	cb->args[0] = idx;
	return skb->len;
}

static int nlmsg_populate_mdb_fill(struct sk_buff *skb,
				   struct net_device *dev,
				   struct net_bridge_mdb_entry *mp,
				   struct net_bridge_port_group *pg,
				   int type)
{
	struct nlmsghdr *nlh;
	struct br_port_msg *bpm;
	struct nlattr *nest, *nest2;

	nlh = nlmsg_put(skb, 0, 0, type, sizeof(*bpm), 0);
	if (!nlh)
		return -EMSGSIZE;

	bpm = nlmsg_data(nlh);
	memset(bpm, 0, sizeof(*bpm));
	bpm->family  = AF_BRIDGE;
	bpm->ifindex = dev->ifindex;
	nest = nla_nest_start_noflag(skb, MDBA_MDB);
	if (nest == NULL)
		goto cancel;
	nest2 = nla_nest_start_noflag(skb, MDBA_MDB_ENTRY);
	if (nest2 == NULL)
		goto end;

	if (__mdb_fill_info(skb, mp, pg))
		goto end;

	nla_nest_end(skb, nest2);
	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);
	return 0;

end:
	nla_nest_end(skb, nest);
cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static size_t rtnl_mdb_nlmsg_size(struct net_bridge_port_group *pg)
{
	size_t nlmsg_size = NLMSG_ALIGN(sizeof(struct br_port_msg)) +
			    nla_total_size(sizeof(struct br_mdb_entry)) +
			    nla_total_size(sizeof(u32));
	struct net_bridge_group_src *ent;
	size_t addr_size = 0;

	if (!pg)
		goto out;

	/* MDBA_MDB_EATTR_RTPROT */
	nlmsg_size += nla_total_size(sizeof(u8));

	switch (pg->key.addr.proto) {
	case htons(ETH_P_IP):
		/* MDBA_MDB_EATTR_SOURCE */
		if (pg->key.addr.src.ip4)
			nlmsg_size += nla_total_size(sizeof(__be32));
		if (pg->key.port->br->multicast_ctx.multicast_igmp_version == 2)
			goto out;
		addr_size = sizeof(__be32);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6):
		/* MDBA_MDB_EATTR_SOURCE */
		if (!ipv6_addr_any(&pg->key.addr.src.ip6))
			nlmsg_size += nla_total_size(sizeof(struct in6_addr));
		if (pg->key.port->br->multicast_ctx.multicast_mld_version == 1)
			goto out;
		addr_size = sizeof(struct in6_addr);
		break;
#endif
	}

	/* MDBA_MDB_EATTR_GROUP_MODE */
	nlmsg_size += nla_total_size(sizeof(u8));

	/* MDBA_MDB_EATTR_SRC_LIST nested attr */
	if (!hlist_empty(&pg->src_list))
		nlmsg_size += nla_total_size(0);

	hlist_for_each_entry(ent, &pg->src_list, node) {
		/* MDBA_MDB_SRCLIST_ENTRY nested attr +
		 * MDBA_MDB_SRCATTR_ADDRESS + MDBA_MDB_SRCATTR_TIMER
		 */
		nlmsg_size += nla_total_size(0) +
			      nla_total_size(addr_size) +
			      nla_total_size(sizeof(u32));
	}
out:
	return nlmsg_size;
}

void br_mdb_notify(struct net_device *dev,
		   struct net_bridge_mdb_entry *mp,
		   struct net_bridge_port_group *pg,
		   int type)
{
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	br_switchdev_mdb_notify(dev, mp, pg, type);

	skb = nlmsg_new(rtnl_mdb_nlmsg_size(pg), GFP_ATOMIC);
	if (!skb)
		goto errout;

	err = nlmsg_populate_mdb_fill(skb, dev, mp, pg, type);
	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_MDB, NULL, GFP_ATOMIC);
	return;
errout:
	rtnl_set_sk_err(net, RTNLGRP_MDB, err);
}

static int nlmsg_populate_rtr_fill(struct sk_buff *skb,
				   struct net_device *dev,
				   int ifindex, u16 vid, u32 pid,
				   u32 seq, int type, unsigned int flags)
{
	struct nlattr *nest, *port_nest;
	struct br_port_msg *bpm;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*bpm), 0);
	if (!nlh)
		return -EMSGSIZE;

	bpm = nlmsg_data(nlh);
	memset(bpm, 0, sizeof(*bpm));
	bpm->family = AF_BRIDGE;
	bpm->ifindex = dev->ifindex;
	nest = nla_nest_start_noflag(skb, MDBA_ROUTER);
	if (!nest)
		goto cancel;

	port_nest = nla_nest_start_noflag(skb, MDBA_ROUTER_PORT);
	if (!port_nest)
		goto end;
	if (nla_put_nohdr(skb, sizeof(u32), &ifindex)) {
		nla_nest_cancel(skb, port_nest);
		goto end;
	}
	if (vid && nla_put_u16(skb, MDBA_ROUTER_PATTR_VID, vid)) {
		nla_nest_cancel(skb, port_nest);
		goto end;
	}
	nla_nest_end(skb, port_nest);

	nla_nest_end(skb, nest);
	nlmsg_end(skb, nlh);
	return 0;

end:
	nla_nest_end(skb, nest);
cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static inline size_t rtnl_rtr_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct br_port_msg))
		+ nla_total_size(sizeof(__u32))
		+ nla_total_size(sizeof(u16));
}

void br_rtr_notify(struct net_device *dev, struct net_bridge_mcast_port *pmctx,
		   int type)
{
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;
	int ifindex;
	u16 vid;

	ifindex = pmctx ? pmctx->port->dev->ifindex : 0;
	vid = pmctx && br_multicast_port_ctx_is_vlan(pmctx) ? pmctx->vlan->vid :
							      0;
	skb = nlmsg_new(rtnl_rtr_nlmsg_size(), GFP_ATOMIC);
	if (!skb)
		goto errout;

	err = nlmsg_populate_rtr_fill(skb, dev, ifindex, vid, 0, 0, type,
				      NTF_SELF);
	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_MDB, NULL, GFP_ATOMIC);
	return;

errout:
	rtnl_set_sk_err(net, RTNLGRP_MDB, err);
}

static bool is_valid_mdb_entry(struct br_mdb_entry *entry,
			       struct netlink_ext_ack *extack)
{
	if (entry->ifindex == 0) {
		NL_SET_ERR_MSG_MOD(extack, "Zero entry ifindex is not allowed");
		return false;
	}

	if (entry->addr.proto == htons(ETH_P_IP)) {
		if (!ipv4_is_multicast(entry->addr.u.ip4)) {
			NL_SET_ERR_MSG_MOD(extack, "IPv4 entry group address is not multicast");
			return false;
		}
		if (ipv4_is_local_multicast(entry->addr.u.ip4)) {
			NL_SET_ERR_MSG_MOD(extack, "IPv4 entry group address is local multicast");
			return false;
		}
#if IS_ENABLED(CONFIG_IPV6)
	} else if (entry->addr.proto == htons(ETH_P_IPV6)) {
		if (ipv6_addr_is_ll_all_nodes(&entry->addr.u.ip6)) {
			NL_SET_ERR_MSG_MOD(extack, "IPv6 entry group address is link-local all nodes");
			return false;
		}
#endif
	} else if (entry->addr.proto == 0) {
		/* L2 mdb */
		if (!is_multicast_ether_addr(entry->addr.u.mac_addr)) {
			NL_SET_ERR_MSG_MOD(extack, "L2 entry group is not multicast");
			return false;
		}
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Unknown entry protocol");
		return false;
	}

	if (entry->state != MDB_PERMANENT && entry->state != MDB_TEMPORARY) {
		NL_SET_ERR_MSG_MOD(extack, "Unknown entry state");
		return false;
	}
	if (entry->vid >= VLAN_VID_MASK) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid entry VLAN id");
		return false;
	}

	return true;
}

static bool is_valid_mdb_source(struct nlattr *attr, __be16 proto,
				struct netlink_ext_ack *extack)
{
	switch (proto) {
	case htons(ETH_P_IP):
		if (nla_len(attr) != sizeof(struct in_addr)) {
			NL_SET_ERR_MSG_MOD(extack, "IPv4 invalid source address length");
			return false;
		}
		if (ipv4_is_multicast(nla_get_in_addr(attr))) {
			NL_SET_ERR_MSG_MOD(extack, "IPv4 multicast source address is not allowed");
			return false;
		}
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case htons(ETH_P_IPV6): {
		struct in6_addr src;

		if (nla_len(attr) != sizeof(struct in6_addr)) {
			NL_SET_ERR_MSG_MOD(extack, "IPv6 invalid source address length");
			return false;
		}
		src = nla_get_in6_addr(attr);
		if (ipv6_addr_is_multicast(&src)) {
			NL_SET_ERR_MSG_MOD(extack, "IPv6 multicast source address is not allowed");
			return false;
		}
		break;
	}
#endif
	default:
		NL_SET_ERR_MSG_MOD(extack, "Invalid protocol used with source address");
		return false;
	}

	return true;
}

static const struct nla_policy br_mdbe_attrs_pol[MDBE_ATTR_MAX + 1] = {
	[MDBE_ATTR_SOURCE] = NLA_POLICY_RANGE(NLA_BINARY,
					      sizeof(struct in_addr),
					      sizeof(struct in6_addr)),
};

static int br_mdb_parse(struct sk_buff *skb, struct nlmsghdr *nlh,
			struct net_device **pdev, struct br_mdb_entry **pentry,
			struct nlattr **mdb_attrs, struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct br_mdb_entry *entry;
	struct br_port_msg *bpm;
	struct nlattr *tb[MDBA_SET_ENTRY_MAX+1];
	struct net_device *dev;
	int err;

	err = nlmsg_parse_deprecated(nlh, sizeof(*bpm), tb,
				     MDBA_SET_ENTRY_MAX, NULL, NULL);
	if (err < 0)
		return err;

	bpm = nlmsg_data(nlh);
	if (bpm->ifindex == 0) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid bridge ifindex");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, bpm->ifindex);
	if (dev == NULL) {
		NL_SET_ERR_MSG_MOD(extack, "Bridge device doesn't exist");
		return -ENODEV;
	}

	if (!netif_is_bridge_master(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Device is not a bridge");
		return -EOPNOTSUPP;
	}

	*pdev = dev;

	if (!tb[MDBA_SET_ENTRY]) {
		NL_SET_ERR_MSG_MOD(extack, "Missing MDBA_SET_ENTRY attribute");
		return -EINVAL;
	}
	if (nla_len(tb[MDBA_SET_ENTRY]) != sizeof(struct br_mdb_entry)) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid MDBA_SET_ENTRY attribute length");
		return -EINVAL;
	}

	entry = nla_data(tb[MDBA_SET_ENTRY]);
	if (!is_valid_mdb_entry(entry, extack))
		return -EINVAL;
	*pentry = entry;

	if (tb[MDBA_SET_ENTRY_ATTRS]) {
		err = nla_parse_nested(mdb_attrs, MDBE_ATTR_MAX,
				       tb[MDBA_SET_ENTRY_ATTRS],
				       br_mdbe_attrs_pol, extack);
		if (err)
			return err;
		if (mdb_attrs[MDBE_ATTR_SOURCE] &&
		    !is_valid_mdb_source(mdb_attrs[MDBE_ATTR_SOURCE],
					 entry->addr.proto, extack))
			return -EINVAL;
	} else {
		memset(mdb_attrs, 0,
		       sizeof(struct nlattr *) * (MDBE_ATTR_MAX + 1));
	}

	return 0;
}

static struct net_bridge_mcast *
__br_mdb_choose_context(struct net_bridge *br,
			const struct br_mdb_entry *entry,
			struct netlink_ext_ack *extack)
{
	struct net_bridge_mcast *brmctx = NULL;
	struct net_bridge_vlan *v;

	if (!br_opt_get(br, BROPT_MCAST_VLAN_SNOOPING_ENABLED)) {
		brmctx = &br->multicast_ctx;
		goto out;
	}

	if (!entry->vid) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot add an entry without a vlan when vlan snooping is enabled");
		goto out;
	}

	v = br_vlan_find(br_vlan_group(br), entry->vid);
	if (!v) {
		NL_SET_ERR_MSG_MOD(extack, "Vlan is not configured");
		goto out;
	}
	if (br_multicast_ctx_vlan_global_disabled(&v->br_mcast_ctx)) {
		NL_SET_ERR_MSG_MOD(extack, "Vlan's multicast processing is disabled");
		goto out;
	}
	brmctx = &v->br_mcast_ctx;
out:
	return brmctx;
}

static int br_mdb_add_group(struct net_bridge *br, struct net_bridge_port *port,
			    struct br_mdb_entry *entry,
			    struct nlattr **mdb_attrs,
			    struct netlink_ext_ack *extack)
{
	struct net_bridge_mdb_entry *mp, *star_mp;
	struct net_bridge_port_group __rcu **pp;
	struct net_bridge_port_group *p;
	struct net_bridge_mcast *brmctx;
	struct br_ip group, star_group;
	unsigned long now = jiffies;
	unsigned char flags = 0;
	u8 filter_mode;
	int err;

	__mdb_entry_to_br_ip(entry, &group, mdb_attrs);

	brmctx = __br_mdb_choose_context(br, entry, extack);
	if (!brmctx)
		return -EINVAL;

	/* host join errors which can happen before creating the group */
	if (!port && !br_group_is_l2(&group)) {
		/* don't allow any flags for host-joined IP groups */
		if (entry->state) {
			NL_SET_ERR_MSG_MOD(extack, "Flags are not allowed for host groups");
			return -EINVAL;
		}
		if (!br_multicast_is_star_g(&group)) {
			NL_SET_ERR_MSG_MOD(extack, "Groups with sources cannot be manually host joined");
			return -EINVAL;
		}
	}

	if (br_group_is_l2(&group) && entry->state != MDB_PERMANENT) {
		NL_SET_ERR_MSG_MOD(extack, "Only permanent L2 entries allowed");
		return -EINVAL;
	}

	mp = br_mdb_ip_get(br, &group);
	if (!mp) {
		mp = br_multicast_new_group(br, &group);
		err = PTR_ERR_OR_ZERO(mp);
		if (err)
			return err;
	}

	/* host join */
	if (!port) {
		if (mp->host_joined) {
			NL_SET_ERR_MSG_MOD(extack, "Group is already joined by host");
			return -EEXIST;
		}

		br_multicast_host_join(brmctx, mp, false);
		br_mdb_notify(br->dev, mp, NULL, RTM_NEWMDB);

		return 0;
	}

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (p->key.port == port) {
			NL_SET_ERR_MSG_MOD(extack, "Group is already joined by port");
			return -EEXIST;
		}
		if ((unsigned long)p->key.port < (unsigned long)port)
			break;
	}

	filter_mode = br_multicast_is_star_g(&group) ? MCAST_EXCLUDE :
						       MCAST_INCLUDE;

	if (entry->state == MDB_PERMANENT)
		flags |= MDB_PG_FLAGS_PERMANENT;

	p = br_multicast_new_port_group(port, &group, *pp, flags, NULL,
					filter_mode, RTPROT_STATIC);
	if (unlikely(!p)) {
		NL_SET_ERR_MSG_MOD(extack, "Couldn't allocate new port group");
		return -ENOMEM;
	}
	rcu_assign_pointer(*pp, p);
	if (entry->state == MDB_TEMPORARY)
		mod_timer(&p->timer,
			  now + brmctx->multicast_membership_interval);
	br_mdb_notify(br->dev, mp, p, RTM_NEWMDB);
	/* if we are adding a new EXCLUDE port group (*,G) it needs to be also
	 * added to all S,G entries for proper replication, if we are adding
	 * a new INCLUDE port (S,G) then all of *,G EXCLUDE ports need to be
	 * added to it for proper replication
	 */
	if (br_multicast_should_handle_mode(brmctx, group.proto)) {
		switch (filter_mode) {
		case MCAST_EXCLUDE:
			br_multicast_star_g_handle_mode(p, MCAST_EXCLUDE);
			break;
		case MCAST_INCLUDE:
			star_group = p->key.addr;
			memset(&star_group.src, 0, sizeof(star_group.src));
			star_mp = br_mdb_ip_get(br, &star_group);
			if (star_mp)
				br_multicast_sg_add_exclude_ports(star_mp, p);
			break;
		}
	}

	return 0;
}

static int __br_mdb_add(struct net *net, struct net_bridge *br,
			struct net_bridge_port *p,
			struct br_mdb_entry *entry,
			struct nlattr **mdb_attrs,
			struct netlink_ext_ack *extack)
{
	int ret;

	spin_lock_bh(&br->multicast_lock);
	ret = br_mdb_add_group(br, p, entry, mdb_attrs, extack);
	spin_unlock_bh(&br->multicast_lock);

	return ret;
}

static int br_mdb_add(struct sk_buff *skb, struct nlmsghdr *nlh,
		      struct netlink_ext_ack *extack)
{
	struct nlattr *mdb_attrs[MDBE_ATTR_MAX + 1];
	struct net *net = sock_net(skb->sk);
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p = NULL;
	struct net_device *dev, *pdev;
	struct br_mdb_entry *entry;
	struct net_bridge_vlan *v;
	struct net_bridge *br;
	int err;

	err = br_mdb_parse(skb, nlh, &dev, &entry, mdb_attrs, extack);
	if (err < 0)
		return err;

	br = netdev_priv(dev);

	if (!netif_running(br->dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Bridge device is not running");
		return -EINVAL;
	}

	if (!br_opt_get(br, BROPT_MULTICAST_ENABLED)) {
		NL_SET_ERR_MSG_MOD(extack, "Bridge's multicast processing is disabled");
		return -EINVAL;
	}

	if (entry->ifindex != br->dev->ifindex) {
		pdev = __dev_get_by_index(net, entry->ifindex);
		if (!pdev) {
			NL_SET_ERR_MSG_MOD(extack, "Port net device doesn't exist");
			return -ENODEV;
		}

		p = br_port_get_rtnl(pdev);
		if (!p) {
			NL_SET_ERR_MSG_MOD(extack, "Net device is not a bridge port");
			return -EINVAL;
		}

		if (p->br != br) {
			NL_SET_ERR_MSG_MOD(extack, "Port belongs to a different bridge device");
			return -EINVAL;
		}
		if (p->state == BR_STATE_DISABLED && entry->state != MDB_PERMANENT) {
			NL_SET_ERR_MSG_MOD(extack, "Port is in disabled state and entry is not permanent");
			return -EINVAL;
		}
		vg = nbp_vlan_group(p);
	} else {
		vg = br_vlan_group(br);
	}

	/* If vlan filtering is enabled and VLAN is not specified
	 * install mdb entry on all vlans configured on the port.
	 */
	if (br_vlan_enabled(br->dev) && vg && entry->vid == 0) {
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			entry->vid = v->vid;
			err = __br_mdb_add(net, br, p, entry, mdb_attrs, extack);
			if (err)
				break;
		}
	} else {
		err = __br_mdb_add(net, br, p, entry, mdb_attrs, extack);
	}

	return err;
}

static int __br_mdb_del(struct net_bridge *br, struct br_mdb_entry *entry,
			struct nlattr **mdb_attrs)
{
	struct net_bridge_mdb_entry *mp;
	struct net_bridge_port_group *p;
	struct net_bridge_port_group __rcu **pp;
	struct br_ip ip;
	int err = -EINVAL;

	if (!netif_running(br->dev) || !br_opt_get(br, BROPT_MULTICAST_ENABLED))
		return -EINVAL;

	__mdb_entry_to_br_ip(entry, &ip, mdb_attrs);

	spin_lock_bh(&br->multicast_lock);
	mp = br_mdb_ip_get(br, &ip);
	if (!mp)
		goto unlock;

	/* host leave */
	if (entry->ifindex == mp->br->dev->ifindex && mp->host_joined) {
		br_multicast_host_leave(mp, false);
		err = 0;
		br_mdb_notify(br->dev, mp, NULL, RTM_DELMDB);
		if (!mp->ports && netif_running(br->dev))
			mod_timer(&mp->timer, jiffies);
		goto unlock;
	}

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (!p->key.port || p->key.port->dev->ifindex != entry->ifindex)
			continue;

		br_multicast_del_pg(mp, p, pp);
		err = 0;
		break;
	}

unlock:
	spin_unlock_bh(&br->multicast_lock);
	return err;
}

static int br_mdb_del(struct sk_buff *skb, struct nlmsghdr *nlh,
		      struct netlink_ext_ack *extack)
{
	struct nlattr *mdb_attrs[MDBE_ATTR_MAX + 1];
	struct net *net = sock_net(skb->sk);
	struct net_bridge_vlan_group *vg;
	struct net_bridge_port *p = NULL;
	struct net_device *dev, *pdev;
	struct br_mdb_entry *entry;
	struct net_bridge_vlan *v;
	struct net_bridge *br;
	int err;

	err = br_mdb_parse(skb, nlh, &dev, &entry, mdb_attrs, extack);
	if (err < 0)
		return err;

	br = netdev_priv(dev);

	if (entry->ifindex != br->dev->ifindex) {
		pdev = __dev_get_by_index(net, entry->ifindex);
		if (!pdev)
			return -ENODEV;

		p = br_port_get_rtnl(pdev);
		if (!p) {
			NL_SET_ERR_MSG_MOD(extack, "Net device is not a bridge port");
			return -EINVAL;
		}
		if (p->br != br) {
			NL_SET_ERR_MSG_MOD(extack, "Port belongs to a different bridge device");
			return -EINVAL;
		}
		vg = nbp_vlan_group(p);
	} else {
		vg = br_vlan_group(br);
	}

	/* If vlan filtering is enabled and VLAN is not specified
	 * delete mdb entry on all vlans configured on the port.
	 */
	if (br_vlan_enabled(br->dev) && vg && entry->vid == 0) {
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			entry->vid = v->vid;
			err = __br_mdb_del(br, entry, mdb_attrs);
		}
	} else {
		err = __br_mdb_del(br, entry, mdb_attrs);
	}

	return err;
}

void br_mdb_init(void)
{
	rtnl_register_module(THIS_MODULE, PF_BRIDGE, RTM_GETMDB, NULL, br_mdb_dump, 0);
	rtnl_register_module(THIS_MODULE, PF_BRIDGE, RTM_NEWMDB, br_mdb_add, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_BRIDGE, RTM_DELMDB, br_mdb_del, NULL, 0);
}

void br_mdb_uninit(void)
{
	rtnl_unregister(PF_BRIDGE, RTM_GETMDB);
	rtnl_unregister(PF_BRIDGE, RTM_NEWMDB);
	rtnl_unregister(PF_BRIDGE, RTM_DELMDB);
}
