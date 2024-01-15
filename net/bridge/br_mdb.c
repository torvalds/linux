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
		e.state = MDB_PERMANENT;
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

int br_mdb_dump(struct net_device *dev, struct sk_buff *skb,
		struct netlink_callback *cb)
{
	struct net_bridge *br = netdev_priv(dev);
	struct br_port_msg *bpm;
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid,
			cb->nlh->nlmsg_seq, RTM_GETMDB, sizeof(*bpm),
			NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	bpm = nlmsg_data(nlh);
	memset(bpm, 0, sizeof(*bpm));
	bpm->ifindex = dev->ifindex;

	rcu_read_lock();

	err = br_mdb_fill_info(skb, cb, dev);
	if (err)
		goto out;
	err = br_rports_fill_info(skb, &br->multicast_ctx);
	if (err)
		goto out;

out:
	rcu_read_unlock();
	nlmsg_end(skb, nlh);
	return err;
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

static size_t rtnl_mdb_nlmsg_pg_size(const struct net_bridge_port_group *pg)
{
	struct net_bridge_group_src *ent;
	size_t nlmsg_size, addr_size = 0;

		     /* MDBA_MDB_ENTRY_INFO */
	nlmsg_size = nla_total_size(sizeof(struct br_mdb_entry)) +
		     /* MDBA_MDB_EATTR_TIMER */
		     nla_total_size(sizeof(u32));

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

static size_t rtnl_mdb_nlmsg_size(const struct net_bridge_port_group *pg)
{
	return NLMSG_ALIGN(sizeof(struct br_port_msg)) +
	       /* MDBA_MDB */
	       nla_total_size(0) +
	       /* MDBA_MDB_ENTRY */
	       nla_total_size(0) +
	       /* Port group entry */
	       rtnl_mdb_nlmsg_pg_size(pg);
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

static const struct nla_policy
br_mdbe_src_list_entry_pol[MDBE_SRCATTR_MAX + 1] = {
	[MDBE_SRCATTR_ADDRESS] = NLA_POLICY_RANGE(NLA_BINARY,
						  sizeof(struct in_addr),
						  sizeof(struct in6_addr)),
};

static const struct nla_policy
br_mdbe_src_list_pol[MDBE_SRC_LIST_MAX + 1] = {
	[MDBE_SRC_LIST_ENTRY] = NLA_POLICY_NESTED(br_mdbe_src_list_entry_pol),
};

static const struct nla_policy br_mdbe_attrs_pol[MDBE_ATTR_MAX + 1] = {
	[MDBE_ATTR_SOURCE] = NLA_POLICY_RANGE(NLA_BINARY,
					      sizeof(struct in_addr),
					      sizeof(struct in6_addr)),
	[MDBE_ATTR_GROUP_MODE] = NLA_POLICY_RANGE(NLA_U8, MCAST_EXCLUDE,
						  MCAST_INCLUDE),
	[MDBE_ATTR_SRC_LIST] = NLA_POLICY_NESTED(br_mdbe_src_list_pol),
	[MDBE_ATTR_RTPROT] = NLA_POLICY_MIN(NLA_U8, RTPROT_STATIC),
};

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

static int br_mdb_replace_group_sg(const struct br_mdb_config *cfg,
				   struct net_bridge_mdb_entry *mp,
				   struct net_bridge_port_group *pg,
				   struct net_bridge_mcast *brmctx,
				   unsigned char flags)
{
	unsigned long now = jiffies;

	pg->flags = flags;
	pg->rt_protocol = cfg->rt_protocol;
	if (!(flags & MDB_PG_FLAGS_PERMANENT) && !cfg->src_entry)
		mod_timer(&pg->timer,
			  now + brmctx->multicast_membership_interval);
	else
		del_timer(&pg->timer);

	br_mdb_notify(cfg->br->dev, mp, pg, RTM_NEWMDB);

	return 0;
}

static int br_mdb_add_group_sg(const struct br_mdb_config *cfg,
			       struct net_bridge_mdb_entry *mp,
			       struct net_bridge_mcast *brmctx,
			       unsigned char flags,
			       struct netlink_ext_ack *extack)
{
	struct net_bridge_port_group __rcu **pp;
	struct net_bridge_port_group *p;
	unsigned long now = jiffies;

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, cfg->br)) != NULL;
	     pp = &p->next) {
		if (p->key.port == cfg->p) {
			if (!(cfg->nlflags & NLM_F_REPLACE)) {
				NL_SET_ERR_MSG_MOD(extack, "(S, G) group is already joined by port");
				return -EEXIST;
			}
			return br_mdb_replace_group_sg(cfg, mp, p, brmctx,
						       flags);
		}
		if ((unsigned long)p->key.port < (unsigned long)cfg->p)
			break;
	}

	p = br_multicast_new_port_group(cfg->p, &cfg->group, *pp, flags, NULL,
					MCAST_INCLUDE, cfg->rt_protocol, extack);
	if (unlikely(!p))
		return -ENOMEM;

	rcu_assign_pointer(*pp, p);
	if (!(flags & MDB_PG_FLAGS_PERMANENT) && !cfg->src_entry)
		mod_timer(&p->timer,
			  now + brmctx->multicast_membership_interval);
	br_mdb_notify(cfg->br->dev, mp, p, RTM_NEWMDB);

	/* All of (*, G) EXCLUDE ports need to be added to the new (S, G) for
	 * proper replication.
	 */
	if (br_multicast_should_handle_mode(brmctx, cfg->group.proto)) {
		struct net_bridge_mdb_entry *star_mp;
		struct br_ip star_group;

		star_group = p->key.addr;
		memset(&star_group.src, 0, sizeof(star_group.src));
		star_mp = br_mdb_ip_get(cfg->br, &star_group);
		if (star_mp)
			br_multicast_sg_add_exclude_ports(star_mp, p);
	}

	return 0;
}

static int br_mdb_add_group_src_fwd(const struct br_mdb_config *cfg,
				    struct br_ip *src_ip,
				    struct net_bridge_mcast *brmctx,
				    struct netlink_ext_ack *extack)
{
	struct net_bridge_mdb_entry *sgmp;
	struct br_mdb_config sg_cfg;
	struct br_ip sg_ip;
	u8 flags = 0;

	sg_ip = cfg->group;
	sg_ip.src = src_ip->src;
	sgmp = br_multicast_new_group(cfg->br, &sg_ip);
	if (IS_ERR(sgmp)) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to add (S, G) MDB entry");
		return PTR_ERR(sgmp);
	}

	if (cfg->entry->state == MDB_PERMANENT)
		flags |= MDB_PG_FLAGS_PERMANENT;
	if (cfg->filter_mode == MCAST_EXCLUDE)
		flags |= MDB_PG_FLAGS_BLOCKED;

	memset(&sg_cfg, 0, sizeof(sg_cfg));
	sg_cfg.br = cfg->br;
	sg_cfg.p = cfg->p;
	sg_cfg.entry = cfg->entry;
	sg_cfg.group = sg_ip;
	sg_cfg.src_entry = true;
	sg_cfg.filter_mode = MCAST_INCLUDE;
	sg_cfg.rt_protocol = cfg->rt_protocol;
	sg_cfg.nlflags = cfg->nlflags;
	return br_mdb_add_group_sg(&sg_cfg, sgmp, brmctx, flags, extack);
}

static int br_mdb_add_group_src(const struct br_mdb_config *cfg,
				struct net_bridge_port_group *pg,
				struct net_bridge_mcast *brmctx,
				struct br_mdb_src_entry *src,
				struct netlink_ext_ack *extack)
{
	struct net_bridge_group_src *ent;
	unsigned long now = jiffies;
	int err;

	ent = br_multicast_find_group_src(pg, &src->addr);
	if (!ent) {
		ent = br_multicast_new_group_src(pg, &src->addr);
		if (!ent) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to add new source entry");
			return -ENOSPC;
		}
	} else if (!(cfg->nlflags & NLM_F_REPLACE)) {
		NL_SET_ERR_MSG_MOD(extack, "Source entry already exists");
		return -EEXIST;
	}

	if (cfg->filter_mode == MCAST_INCLUDE &&
	    cfg->entry->state == MDB_TEMPORARY)
		mod_timer(&ent->timer, now + br_multicast_gmi(brmctx));
	else
		del_timer(&ent->timer);

	/* Install a (S, G) forwarding entry for the source. */
	err = br_mdb_add_group_src_fwd(cfg, &src->addr, brmctx, extack);
	if (err)
		goto err_del_sg;

	ent->flags = BR_SGRP_F_INSTALLED | BR_SGRP_F_USER_ADDED;

	return 0;

err_del_sg:
	__br_multicast_del_group_src(ent);
	return err;
}

static void br_mdb_del_group_src(struct net_bridge_port_group *pg,
				 struct br_mdb_src_entry *src)
{
	struct net_bridge_group_src *ent;

	ent = br_multicast_find_group_src(pg, &src->addr);
	if (WARN_ON_ONCE(!ent))
		return;
	br_multicast_del_group_src(ent, false);
}

static int br_mdb_add_group_srcs(const struct br_mdb_config *cfg,
				 struct net_bridge_port_group *pg,
				 struct net_bridge_mcast *brmctx,
				 struct netlink_ext_ack *extack)
{
	int i, err;

	for (i = 0; i < cfg->num_src_entries; i++) {
		err = br_mdb_add_group_src(cfg, pg, brmctx,
					   &cfg->src_entries[i], extack);
		if (err)
			goto err_del_group_srcs;
	}

	return 0;

err_del_group_srcs:
	for (i--; i >= 0; i--)
		br_mdb_del_group_src(pg, &cfg->src_entries[i]);
	return err;
}

static int br_mdb_replace_group_srcs(const struct br_mdb_config *cfg,
				     struct net_bridge_port_group *pg,
				     struct net_bridge_mcast *brmctx,
				     struct netlink_ext_ack *extack)
{
	struct net_bridge_group_src *ent;
	struct hlist_node *tmp;
	int err;

	hlist_for_each_entry(ent, &pg->src_list, node)
		ent->flags |= BR_SGRP_F_DELETE;

	err = br_mdb_add_group_srcs(cfg, pg, brmctx, extack);
	if (err)
		goto err_clear_delete;

	hlist_for_each_entry_safe(ent, tmp, &pg->src_list, node) {
		if (ent->flags & BR_SGRP_F_DELETE)
			br_multicast_del_group_src(ent, false);
	}

	return 0;

err_clear_delete:
	hlist_for_each_entry(ent, &pg->src_list, node)
		ent->flags &= ~BR_SGRP_F_DELETE;
	return err;
}

static int br_mdb_replace_group_star_g(const struct br_mdb_config *cfg,
				       struct net_bridge_mdb_entry *mp,
				       struct net_bridge_port_group *pg,
				       struct net_bridge_mcast *brmctx,
				       unsigned char flags,
				       struct netlink_ext_ack *extack)
{
	unsigned long now = jiffies;
	int err;

	err = br_mdb_replace_group_srcs(cfg, pg, brmctx, extack);
	if (err)
		return err;

	pg->flags = flags;
	pg->filter_mode = cfg->filter_mode;
	pg->rt_protocol = cfg->rt_protocol;
	if (!(flags & MDB_PG_FLAGS_PERMANENT) &&
	    cfg->filter_mode == MCAST_EXCLUDE)
		mod_timer(&pg->timer,
			  now + brmctx->multicast_membership_interval);
	else
		del_timer(&pg->timer);

	br_mdb_notify(cfg->br->dev, mp, pg, RTM_NEWMDB);

	if (br_multicast_should_handle_mode(brmctx, cfg->group.proto))
		br_multicast_star_g_handle_mode(pg, cfg->filter_mode);

	return 0;
}

static int br_mdb_add_group_star_g(const struct br_mdb_config *cfg,
				   struct net_bridge_mdb_entry *mp,
				   struct net_bridge_mcast *brmctx,
				   unsigned char flags,
				   struct netlink_ext_ack *extack)
{
	struct net_bridge_port_group __rcu **pp;
	struct net_bridge_port_group *p;
	unsigned long now = jiffies;
	int err;

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, cfg->br)) != NULL;
	     pp = &p->next) {
		if (p->key.port == cfg->p) {
			if (!(cfg->nlflags & NLM_F_REPLACE)) {
				NL_SET_ERR_MSG_MOD(extack, "(*, G) group is already joined by port");
				return -EEXIST;
			}
			return br_mdb_replace_group_star_g(cfg, mp, p, brmctx,
							   flags, extack);
		}
		if ((unsigned long)p->key.port < (unsigned long)cfg->p)
			break;
	}

	p = br_multicast_new_port_group(cfg->p, &cfg->group, *pp, flags, NULL,
					cfg->filter_mode, cfg->rt_protocol,
					extack);
	if (unlikely(!p))
		return -ENOMEM;

	err = br_mdb_add_group_srcs(cfg, p, brmctx, extack);
	if (err)
		goto err_del_port_group;

	rcu_assign_pointer(*pp, p);
	if (!(flags & MDB_PG_FLAGS_PERMANENT) &&
	    cfg->filter_mode == MCAST_EXCLUDE)
		mod_timer(&p->timer,
			  now + brmctx->multicast_membership_interval);
	br_mdb_notify(cfg->br->dev, mp, p, RTM_NEWMDB);
	/* If we are adding a new EXCLUDE port group (*, G), it needs to be
	 * also added to all (S, G) entries for proper replication.
	 */
	if (br_multicast_should_handle_mode(brmctx, cfg->group.proto) &&
	    cfg->filter_mode == MCAST_EXCLUDE)
		br_multicast_star_g_handle_mode(p, MCAST_EXCLUDE);

	return 0;

err_del_port_group:
	br_multicast_del_port_group(p);
	return err;
}

static int br_mdb_add_group(const struct br_mdb_config *cfg,
			    struct netlink_ext_ack *extack)
{
	struct br_mdb_entry *entry = cfg->entry;
	struct net_bridge_port *port = cfg->p;
	struct net_bridge_mdb_entry *mp;
	struct net_bridge *br = cfg->br;
	struct net_bridge_mcast *brmctx;
	struct br_ip group = cfg->group;
	unsigned char flags = 0;

	brmctx = __br_mdb_choose_context(br, entry, extack);
	if (!brmctx)
		return -EINVAL;

	mp = br_multicast_new_group(br, &group);
	if (IS_ERR(mp))
		return PTR_ERR(mp);

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

	if (entry->state == MDB_PERMANENT)
		flags |= MDB_PG_FLAGS_PERMANENT;

	if (br_multicast_is_star_g(&group))
		return br_mdb_add_group_star_g(cfg, mp, brmctx, flags, extack);
	else
		return br_mdb_add_group_sg(cfg, mp, brmctx, flags, extack);
}

static int __br_mdb_add(const struct br_mdb_config *cfg,
			struct netlink_ext_ack *extack)
{
	int ret;

	spin_lock_bh(&cfg->br->multicast_lock);
	ret = br_mdb_add_group(cfg, extack);
	spin_unlock_bh(&cfg->br->multicast_lock);

	return ret;
}

static int br_mdb_config_src_entry_init(struct nlattr *src_entry,
					struct br_mdb_src_entry *src,
					__be16 proto,
					struct netlink_ext_ack *extack)
{
	struct nlattr *tb[MDBE_SRCATTR_MAX + 1];
	int err;

	err = nla_parse_nested(tb, MDBE_SRCATTR_MAX, src_entry,
			       br_mdbe_src_list_entry_pol, extack);
	if (err)
		return err;

	if (NL_REQ_ATTR_CHECK(extack, src_entry, tb, MDBE_SRCATTR_ADDRESS))
		return -EINVAL;

	if (!is_valid_mdb_source(tb[MDBE_SRCATTR_ADDRESS], proto, extack))
		return -EINVAL;

	src->addr.proto = proto;
	nla_memcpy(&src->addr.src, tb[MDBE_SRCATTR_ADDRESS],
		   nla_len(tb[MDBE_SRCATTR_ADDRESS]));

	return 0;
}

static int br_mdb_config_src_list_init(struct nlattr *src_list,
				       struct br_mdb_config *cfg,
				       struct netlink_ext_ack *extack)
{
	struct nlattr *src_entry;
	int rem, err;
	int i = 0;

	nla_for_each_nested(src_entry, src_list, rem)
		cfg->num_src_entries++;

	if (cfg->num_src_entries >= PG_SRC_ENT_LIMIT) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "Exceeded maximum number of source entries (%u)",
				       PG_SRC_ENT_LIMIT - 1);
		return -EINVAL;
	}

	cfg->src_entries = kcalloc(cfg->num_src_entries,
				   sizeof(struct br_mdb_src_entry), GFP_KERNEL);
	if (!cfg->src_entries)
		return -ENOMEM;

	nla_for_each_nested(src_entry, src_list, rem) {
		err = br_mdb_config_src_entry_init(src_entry,
						   &cfg->src_entries[i],
						   cfg->entry->addr.proto,
						   extack);
		if (err)
			goto err_src_entry_init;
		i++;
	}

	return 0;

err_src_entry_init:
	kfree(cfg->src_entries);
	return err;
}

static void br_mdb_config_src_list_fini(struct br_mdb_config *cfg)
{
	kfree(cfg->src_entries);
}

static int br_mdb_config_attrs_init(struct nlattr *set_attrs,
				    struct br_mdb_config *cfg,
				    struct netlink_ext_ack *extack)
{
	struct nlattr *mdb_attrs[MDBE_ATTR_MAX + 1];
	int err;

	err = nla_parse_nested(mdb_attrs, MDBE_ATTR_MAX, set_attrs,
			       br_mdbe_attrs_pol, extack);
	if (err)
		return err;

	if (mdb_attrs[MDBE_ATTR_SOURCE] &&
	    !is_valid_mdb_source(mdb_attrs[MDBE_ATTR_SOURCE],
				 cfg->entry->addr.proto, extack))
		return -EINVAL;

	__mdb_entry_to_br_ip(cfg->entry, &cfg->group, mdb_attrs);

	if (mdb_attrs[MDBE_ATTR_GROUP_MODE]) {
		if (!cfg->p) {
			NL_SET_ERR_MSG_MOD(extack, "Filter mode cannot be set for host groups");
			return -EINVAL;
		}
		if (!br_multicast_is_star_g(&cfg->group)) {
			NL_SET_ERR_MSG_MOD(extack, "Filter mode can only be set for (*, G) entries");
			return -EINVAL;
		}
		cfg->filter_mode = nla_get_u8(mdb_attrs[MDBE_ATTR_GROUP_MODE]);
	} else {
		cfg->filter_mode = MCAST_EXCLUDE;
	}

	if (mdb_attrs[MDBE_ATTR_SRC_LIST]) {
		if (!cfg->p) {
			NL_SET_ERR_MSG_MOD(extack, "Source list cannot be set for host groups");
			return -EINVAL;
		}
		if (!br_multicast_is_star_g(&cfg->group)) {
			NL_SET_ERR_MSG_MOD(extack, "Source list can only be set for (*, G) entries");
			return -EINVAL;
		}
		if (!mdb_attrs[MDBE_ATTR_GROUP_MODE]) {
			NL_SET_ERR_MSG_MOD(extack, "Source list cannot be set without filter mode");
			return -EINVAL;
		}
		err = br_mdb_config_src_list_init(mdb_attrs[MDBE_ATTR_SRC_LIST],
						  cfg, extack);
		if (err)
			return err;
	}

	if (!cfg->num_src_entries && cfg->filter_mode == MCAST_INCLUDE) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot add (*, G) INCLUDE with an empty source list");
		return -EINVAL;
	}

	if (mdb_attrs[MDBE_ATTR_RTPROT]) {
		if (!cfg->p) {
			NL_SET_ERR_MSG_MOD(extack, "Protocol cannot be set for host groups");
			return -EINVAL;
		}
		cfg->rt_protocol = nla_get_u8(mdb_attrs[MDBE_ATTR_RTPROT]);
	}

	return 0;
}

static int br_mdb_config_init(struct br_mdb_config *cfg, struct net_device *dev,
			      struct nlattr *tb[], u16 nlmsg_flags,
			      struct netlink_ext_ack *extack)
{
	struct net *net = dev_net(dev);

	memset(cfg, 0, sizeof(*cfg));
	cfg->filter_mode = MCAST_EXCLUDE;
	cfg->rt_protocol = RTPROT_STATIC;
	cfg->nlflags = nlmsg_flags;

	cfg->br = netdev_priv(dev);

	if (!netif_running(cfg->br->dev)) {
		NL_SET_ERR_MSG_MOD(extack, "Bridge device is not running");
		return -EINVAL;
	}

	if (!br_opt_get(cfg->br, BROPT_MULTICAST_ENABLED)) {
		NL_SET_ERR_MSG_MOD(extack, "Bridge's multicast processing is disabled");
		return -EINVAL;
	}

	cfg->entry = nla_data(tb[MDBA_SET_ENTRY]);

	if (cfg->entry->ifindex != cfg->br->dev->ifindex) {
		struct net_device *pdev;

		pdev = __dev_get_by_index(net, cfg->entry->ifindex);
		if (!pdev) {
			NL_SET_ERR_MSG_MOD(extack, "Port net device doesn't exist");
			return -ENODEV;
		}

		cfg->p = br_port_get_rtnl(pdev);
		if (!cfg->p) {
			NL_SET_ERR_MSG_MOD(extack, "Net device is not a bridge port");
			return -EINVAL;
		}

		if (cfg->p->br != cfg->br) {
			NL_SET_ERR_MSG_MOD(extack, "Port belongs to a different bridge device");
			return -EINVAL;
		}
	}

	if (cfg->entry->addr.proto == htons(ETH_P_IP) &&
	    ipv4_is_zeronet(cfg->entry->addr.u.ip4)) {
		NL_SET_ERR_MSG_MOD(extack, "IPv4 entry group address 0.0.0.0 is not allowed");
		return -EINVAL;
	}

	if (tb[MDBA_SET_ENTRY_ATTRS])
		return br_mdb_config_attrs_init(tb[MDBA_SET_ENTRY_ATTRS], cfg,
						extack);
	else
		__mdb_entry_to_br_ip(cfg->entry, &cfg->group, NULL);

	return 0;
}

static void br_mdb_config_fini(struct br_mdb_config *cfg)
{
	br_mdb_config_src_list_fini(cfg);
}

int br_mdb_add(struct net_device *dev, struct nlattr *tb[], u16 nlmsg_flags,
	       struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;
	struct br_mdb_config cfg;
	int err;

	err = br_mdb_config_init(&cfg, dev, tb, nlmsg_flags, extack);
	if (err)
		return err;

	err = -EINVAL;
	/* host join errors which can happen before creating the group */
	if (!cfg.p && !br_group_is_l2(&cfg.group)) {
		/* don't allow any flags for host-joined IP groups */
		if (cfg.entry->state) {
			NL_SET_ERR_MSG_MOD(extack, "Flags are not allowed for host groups");
			goto out;
		}
		if (!br_multicast_is_star_g(&cfg.group)) {
			NL_SET_ERR_MSG_MOD(extack, "Groups with sources cannot be manually host joined");
			goto out;
		}
	}

	if (br_group_is_l2(&cfg.group) && cfg.entry->state != MDB_PERMANENT) {
		NL_SET_ERR_MSG_MOD(extack, "Only permanent L2 entries allowed");
		goto out;
	}

	if (cfg.p) {
		if (cfg.p->state == BR_STATE_DISABLED && cfg.entry->state != MDB_PERMANENT) {
			NL_SET_ERR_MSG_MOD(extack, "Port is in disabled state and entry is not permanent");
			goto out;
		}
		vg = nbp_vlan_group(cfg.p);
	} else {
		vg = br_vlan_group(cfg.br);
	}

	/* If vlan filtering is enabled and VLAN is not specified
	 * install mdb entry on all vlans configured on the port.
	 */
	if (br_vlan_enabled(cfg.br->dev) && vg && cfg.entry->vid == 0) {
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			cfg.entry->vid = v->vid;
			cfg.group.vid = v->vid;
			err = __br_mdb_add(&cfg, extack);
			if (err)
				break;
		}
	} else {
		err = __br_mdb_add(&cfg, extack);
	}

out:
	br_mdb_config_fini(&cfg);
	return err;
}

static int __br_mdb_del(const struct br_mdb_config *cfg)
{
	struct br_mdb_entry *entry = cfg->entry;
	struct net_bridge *br = cfg->br;
	struct net_bridge_mdb_entry *mp;
	struct net_bridge_port_group *p;
	struct net_bridge_port_group __rcu **pp;
	struct br_ip ip = cfg->group;
	int err = -EINVAL;

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

int br_mdb_del(struct net_device *dev, struct nlattr *tb[],
	       struct netlink_ext_ack *extack)
{
	struct net_bridge_vlan_group *vg;
	struct net_bridge_vlan *v;
	struct br_mdb_config cfg;
	int err;

	err = br_mdb_config_init(&cfg, dev, tb, 0, extack);
	if (err)
		return err;

	if (cfg.p)
		vg = nbp_vlan_group(cfg.p);
	else
		vg = br_vlan_group(cfg.br);

	/* If vlan filtering is enabled and VLAN is not specified
	 * delete mdb entry on all vlans configured on the port.
	 */
	if (br_vlan_enabled(cfg.br->dev) && vg && cfg.entry->vid == 0) {
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			cfg.entry->vid = v->vid;
			cfg.group.vid = v->vid;
			err = __br_mdb_del(&cfg);
		}
	} else {
		err = __br_mdb_del(&cfg);
	}

	br_mdb_config_fini(&cfg);
	return err;
}

static const struct nla_policy br_mdbe_attrs_get_pol[MDBE_ATTR_MAX + 1] = {
	[MDBE_ATTR_SOURCE] = NLA_POLICY_RANGE(NLA_BINARY,
					      sizeof(struct in_addr),
					      sizeof(struct in6_addr)),
};

static int br_mdb_get_parse(struct net_device *dev, struct nlattr *tb[],
			    struct br_ip *group, struct netlink_ext_ack *extack)
{
	struct br_mdb_entry *entry = nla_data(tb[MDBA_GET_ENTRY]);
	struct nlattr *mdbe_attrs[MDBE_ATTR_MAX + 1];
	int err;

	if (!tb[MDBA_GET_ENTRY_ATTRS]) {
		__mdb_entry_to_br_ip(entry, group, NULL);
		return 0;
	}

	err = nla_parse_nested(mdbe_attrs, MDBE_ATTR_MAX,
			       tb[MDBA_GET_ENTRY_ATTRS], br_mdbe_attrs_get_pol,
			       extack);
	if (err)
		return err;

	if (mdbe_attrs[MDBE_ATTR_SOURCE] &&
	    !is_valid_mdb_source(mdbe_attrs[MDBE_ATTR_SOURCE],
				 entry->addr.proto, extack))
		return -EINVAL;

	__mdb_entry_to_br_ip(entry, group, mdbe_attrs);

	return 0;
}

static struct sk_buff *
br_mdb_get_reply_alloc(const struct net_bridge_mdb_entry *mp)
{
	struct net_bridge_port_group *pg;
	size_t nlmsg_size;

	nlmsg_size = NLMSG_ALIGN(sizeof(struct br_port_msg)) +
		     /* MDBA_MDB */
		     nla_total_size(0) +
		     /* MDBA_MDB_ENTRY */
		     nla_total_size(0);

	if (mp->host_joined)
		nlmsg_size += rtnl_mdb_nlmsg_pg_size(NULL);

	for (pg = mlock_dereference(mp->ports, mp->br); pg;
	     pg = mlock_dereference(pg->next, mp->br))
		nlmsg_size += rtnl_mdb_nlmsg_pg_size(pg);

	return nlmsg_new(nlmsg_size, GFP_ATOMIC);
}

static int br_mdb_get_reply_fill(struct sk_buff *skb,
				 struct net_bridge_mdb_entry *mp, u32 portid,
				 u32 seq)
{
	struct nlattr *mdb_nest, *mdb_entry_nest;
	struct net_bridge_port_group *pg;
	struct br_port_msg *bpm;
	struct nlmsghdr *nlh;
	int err;

	nlh = nlmsg_put(skb, portid, seq, RTM_NEWMDB, sizeof(*bpm), 0);
	if (!nlh)
		return -EMSGSIZE;

	bpm = nlmsg_data(nlh);
	memset(bpm, 0, sizeof(*bpm));
	bpm->family  = AF_BRIDGE;
	bpm->ifindex = mp->br->dev->ifindex;
	mdb_nest = nla_nest_start_noflag(skb, MDBA_MDB);
	if (!mdb_nest) {
		err = -EMSGSIZE;
		goto cancel;
	}
	mdb_entry_nest = nla_nest_start_noflag(skb, MDBA_MDB_ENTRY);
	if (!mdb_entry_nest) {
		err = -EMSGSIZE;
		goto cancel;
	}

	if (mp->host_joined) {
		err = __mdb_fill_info(skb, mp, NULL);
		if (err)
			goto cancel;
	}

	for (pg = mlock_dereference(mp->ports, mp->br); pg;
	     pg = mlock_dereference(pg->next, mp->br)) {
		err = __mdb_fill_info(skb, mp, pg);
		if (err)
			goto cancel;
	}

	nla_nest_end(skb, mdb_entry_nest);
	nla_nest_end(skb, mdb_nest);
	nlmsg_end(skb, nlh);

	return 0;

cancel:
	nlmsg_cancel(skb, nlh);
	return err;
}

int br_mdb_get(struct net_device *dev, struct nlattr *tb[], u32 portid, u32 seq,
	       struct netlink_ext_ack *extack)
{
	struct net_bridge *br = netdev_priv(dev);
	struct net_bridge_mdb_entry *mp;
	struct sk_buff *skb;
	struct br_ip group;
	int err;

	err = br_mdb_get_parse(dev, tb, &group, extack);
	if (err)
		return err;

	/* Hold the multicast lock to ensure that the MDB entry does not change
	 * between the time the reply size is determined and when the reply is
	 * filled in.
	 */
	spin_lock_bh(&br->multicast_lock);

	mp = br_mdb_ip_get(br, &group);
	if (!mp) {
		NL_SET_ERR_MSG_MOD(extack, "MDB entry not found");
		err = -ENOENT;
		goto unlock;
	}

	skb = br_mdb_get_reply_alloc(mp);
	if (!skb) {
		err = -ENOMEM;
		goto unlock;
	}

	err = br_mdb_get_reply_fill(skb, mp, portid, seq);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to fill MDB get reply");
		goto free;
	}

	spin_unlock_bh(&br->multicast_lock);

	return rtnl_unicast(skb, dev_net(dev), portid);

free:
	kfree_skb(skb);
unlock:
	spin_unlock_bh(&br->multicast_lock);
	return err;
}
