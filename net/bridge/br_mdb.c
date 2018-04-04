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

static int br_rports_fill_info(struct sk_buff *skb, struct netlink_callback *cb,
			       struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);
	struct net_bridge_port *p;
	struct nlattr *nest, *port_nest;

	if (!br->multicast_router || hlist_empty(&br->router_list))
		return 0;

	nest = nla_nest_start(skb, MDBA_ROUTER);
	if (nest == NULL)
		return -EMSGSIZE;

	hlist_for_each_entry_rcu(p, &br->router_list, rlist) {
		if (!p)
			continue;
		port_nest = nla_nest_start(skb, MDBA_ROUTER_PORT);
		if (!port_nest)
			goto fail;
		if (nla_put_nohdr(skb, sizeof(u32), &p->dev->ifindex) ||
		    nla_put_u32(skb, MDBA_ROUTER_PATTR_TIMER,
				br_timer_value(&p->multicast_router_timer)) ||
		    nla_put_u8(skb, MDBA_ROUTER_PATTR_TYPE,
			       p->multicast_router)) {
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
}

static void __mdb_entry_to_br_ip(struct br_mdb_entry *entry, struct br_ip *ip)
{
	memset(ip, 0, sizeof(struct br_ip));
	ip->vid = entry->vid;
	ip->proto = entry->addr.proto;
	if (ip->proto == htons(ETH_P_IP))
		ip->u.ip4 = entry->addr.u.ip4;
#if IS_ENABLED(CONFIG_IPV6)
	else
		ip->u.ip6 = entry->addr.u.ip6;
#endif
}

static int br_mdb_fill_info(struct sk_buff *skb, struct netlink_callback *cb,
			    struct net_device *dev)
{
	struct net_bridge *br = netdev_priv(dev);
	struct net_bridge_mdb_htable *mdb;
	struct nlattr *nest, *nest2;
	int i, err = 0;
	int idx = 0, s_idx = cb->args[1];

	if (br->multicast_disabled)
		return 0;

	mdb = rcu_dereference(br->mdb);
	if (!mdb)
		return 0;

	nest = nla_nest_start(skb, MDBA_MDB);
	if (nest == NULL)
		return -EMSGSIZE;

	for (i = 0; i < mdb->max; i++) {
		struct net_bridge_mdb_entry *mp;
		struct net_bridge_port_group *p;
		struct net_bridge_port_group __rcu **pp;
		struct net_bridge_port *port;

		hlist_for_each_entry_rcu(mp, &mdb->mhash[i], hlist[mdb->ver]) {
			if (idx < s_idx)
				goto skip;

			nest2 = nla_nest_start(skb, MDBA_MDB_ENTRY);
			if (nest2 == NULL) {
				err = -EMSGSIZE;
				goto out;
			}

			for (pp = &mp->ports;
			     (p = rcu_dereference(*pp)) != NULL;
			      pp = &p->next) {
				struct nlattr *nest_ent;
				struct br_mdb_entry e;

				port = p->port;
				if (!port)
					continue;

				memset(&e, 0, sizeof(e));
				e.ifindex = port->dev->ifindex;
				e.vid = p->addr.vid;
				__mdb_entry_fill_flags(&e, p->flags);
				if (p->addr.proto == htons(ETH_P_IP))
					e.addr.u.ip4 = p->addr.u.ip4;
#if IS_ENABLED(CONFIG_IPV6)
				if (p->addr.proto == htons(ETH_P_IPV6))
					e.addr.u.ip6 = p->addr.u.ip6;
#endif
				e.addr.proto = p->addr.proto;
				nest_ent = nla_nest_start(skb,
							  MDBA_MDB_ENTRY_INFO);
				if (!nest_ent) {
					nla_nest_cancel(skb, nest2);
					err = -EMSGSIZE;
					goto out;
				}
				if (nla_put_nohdr(skb, sizeof(e), &e) ||
				    nla_put_u32(skb,
						MDBA_MDB_EATTR_TIMER,
						br_timer_value(&p->timer))) {
					nla_nest_cancel(skb, nest_ent);
					nla_nest_cancel(skb, nest2);
					err = -EMSGSIZE;
					goto out;
				}
				nla_nest_end(skb, nest_ent);
			}
			nla_nest_end(skb, nest2);
		skip:
			idx++;
		}
	}

out:
	cb->args[1] = idx;
	nla_nest_end(skb, nest);
	return err;
}

static int br_mdb_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct net_device *dev;
	struct net *net = sock_net(skb->sk);
	struct nlmsghdr *nlh = NULL;
	int idx = 0, s_idx;

	s_idx = cb->args[0];

	rcu_read_lock();

	/* In theory this could be wrapped to 0... */
	cb->seq = net->dev_base_seq + br_mdb_rehash_seq;

	for_each_netdev_rcu(net, dev) {
		if (dev->priv_flags & IFF_EBRIDGE) {
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
			if (br_rports_fill_info(skb, cb, dev) < 0)
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
				   struct br_mdb_entry *entry, u32 pid,
				   u32 seq, int type, unsigned int flags)
{
	struct nlmsghdr *nlh;
	struct br_port_msg *bpm;
	struct nlattr *nest, *nest2;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*bpm), 0);
	if (!nlh)
		return -EMSGSIZE;

	bpm = nlmsg_data(nlh);
	memset(bpm, 0, sizeof(*bpm));
	bpm->family  = AF_BRIDGE;
	bpm->ifindex = dev->ifindex;
	nest = nla_nest_start(skb, MDBA_MDB);
	if (nest == NULL)
		goto cancel;
	nest2 = nla_nest_start(skb, MDBA_MDB_ENTRY);
	if (nest2 == NULL)
		goto end;

	if (nla_put(skb, MDBA_MDB_ENTRY_INFO, sizeof(*entry), entry))
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

static inline size_t rtnl_mdb_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct br_port_msg))
		+ nla_total_size(sizeof(struct br_mdb_entry));
}

struct br_mdb_complete_info {
	struct net_bridge_port *port;
	struct br_ip ip;
};

static void br_mdb_complete(struct net_device *dev, int err, void *priv)
{
	struct br_mdb_complete_info *data = priv;
	struct net_bridge_port_group __rcu **pp;
	struct net_bridge_port_group *p;
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_mdb_entry *mp;
	struct net_bridge_port *port = data->port;
	struct net_bridge *br = port->br;

	if (err)
		goto err;

	spin_lock_bh(&br->multicast_lock);
	mdb = mlock_dereference(br->mdb, br);
	mp = br_mdb_ip_get(mdb, &data->ip);
	if (!mp)
		goto out;
	for (pp = &mp->ports; (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (p->port != port)
			continue;
		p->flags |= MDB_PG_FLAGS_OFFLOAD;
	}
out:
	spin_unlock_bh(&br->multicast_lock);
err:
	kfree(priv);
}

static void br_mdb_switchdev_host_port(struct net_device *dev,
				       struct net_device *lower_dev,
				       struct br_mdb_entry *entry, int type)
{
	struct switchdev_obj_port_mdb mdb = {
		.obj = {
			.id = SWITCHDEV_OBJ_ID_HOST_MDB,
			.flags = SWITCHDEV_F_DEFER,
		},
		.vid = entry->vid,
	};

	if (entry->addr.proto == htons(ETH_P_IP))
		ip_eth_mc_map(entry->addr.u.ip4, mdb.addr);
#if IS_ENABLED(CONFIG_IPV6)
	else
		ipv6_eth_mc_map(&entry->addr.u.ip6, mdb.addr);
#endif

	mdb.obj.orig_dev = dev;
	switch (type) {
	case RTM_NEWMDB:
		switchdev_port_obj_add(lower_dev, &mdb.obj);
		break;
	case RTM_DELMDB:
		switchdev_port_obj_del(lower_dev, &mdb.obj);
		break;
	}
}

static void br_mdb_switchdev_host(struct net_device *dev,
				  struct br_mdb_entry *entry, int type)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	netdev_for_each_lower_dev(dev, lower_dev, iter)
		br_mdb_switchdev_host_port(dev, lower_dev, entry, type);
}

static void __br_mdb_notify(struct net_device *dev, struct net_bridge_port *p,
			    struct br_mdb_entry *entry, int type)
{
	struct br_mdb_complete_info *complete_info;
	struct switchdev_obj_port_mdb mdb = {
		.obj = {
			.id = SWITCHDEV_OBJ_ID_PORT_MDB,
			.flags = SWITCHDEV_F_DEFER,
		},
		.vid = entry->vid,
	};
	struct net_device *port_dev;
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	port_dev = __dev_get_by_index(net, entry->ifindex);
	if (entry->addr.proto == htons(ETH_P_IP))
		ip_eth_mc_map(entry->addr.u.ip4, mdb.addr);
#if IS_ENABLED(CONFIG_IPV6)
	else
		ipv6_eth_mc_map(&entry->addr.u.ip6, mdb.addr);
#endif

	mdb.obj.orig_dev = port_dev;
	if (p && port_dev && type == RTM_NEWMDB) {
		complete_info = kmalloc(sizeof(*complete_info), GFP_ATOMIC);
		if (complete_info) {
			complete_info->port = p;
			__mdb_entry_to_br_ip(entry, &complete_info->ip);
			mdb.obj.complete_priv = complete_info;
			mdb.obj.complete = br_mdb_complete;
			if (switchdev_port_obj_add(port_dev, &mdb.obj))
				kfree(complete_info);
		}
	} else if (p && port_dev && type == RTM_DELMDB) {
		switchdev_port_obj_del(port_dev, &mdb.obj);
	}

	if (!p)
		br_mdb_switchdev_host(dev, entry, type);

	skb = nlmsg_new(rtnl_mdb_nlmsg_size(), GFP_ATOMIC);
	if (!skb)
		goto errout;

	err = nlmsg_populate_mdb_fill(skb, dev, entry, 0, 0, type, NTF_SELF);
	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_MDB, NULL, GFP_ATOMIC);
	return;
errout:
	rtnl_set_sk_err(net, RTNLGRP_MDB, err);
}

void br_mdb_notify(struct net_device *dev, struct net_bridge_port *port,
		   struct br_ip *group, int type, u8 flags)
{
	struct br_mdb_entry entry;

	memset(&entry, 0, sizeof(entry));
	if (port)
		entry.ifindex = port->dev->ifindex;
	else
		entry.ifindex = dev->ifindex;
	entry.addr.proto = group->proto;
	entry.addr.u.ip4 = group->u.ip4;
#if IS_ENABLED(CONFIG_IPV6)
	entry.addr.u.ip6 = group->u.ip6;
#endif
	entry.vid = group->vid;
	__mdb_entry_fill_flags(&entry, flags);
	__br_mdb_notify(dev, port, &entry, type);
}

static int nlmsg_populate_rtr_fill(struct sk_buff *skb,
				   struct net_device *dev,
				   int ifindex, u32 pid,
				   u32 seq, int type, unsigned int flags)
{
	struct br_port_msg *bpm;
	struct nlmsghdr *nlh;
	struct nlattr *nest;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*bpm), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	bpm = nlmsg_data(nlh);
	memset(bpm, 0, sizeof(*bpm));
	bpm->family = AF_BRIDGE;
	bpm->ifindex = dev->ifindex;
	nest = nla_nest_start(skb, MDBA_ROUTER);
	if (!nest)
		goto cancel;

	if (nla_put_u32(skb, MDBA_ROUTER_PORT, ifindex))
		goto end;

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
		+ nla_total_size(sizeof(__u32));
}

void br_rtr_notify(struct net_device *dev, struct net_bridge_port *port,
		   int type)
{
	struct net *net = dev_net(dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;
	int ifindex;

	ifindex = port ? port->dev->ifindex : 0;
	skb = nlmsg_new(rtnl_rtr_nlmsg_size(), GFP_ATOMIC);
	if (!skb)
		goto errout;

	err = nlmsg_populate_rtr_fill(skb, dev, ifindex, 0, 0, type, NTF_SELF);
	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_MDB, NULL, GFP_ATOMIC);
	return;

errout:
	rtnl_set_sk_err(net, RTNLGRP_MDB, err);
}

static bool is_valid_mdb_entry(struct br_mdb_entry *entry)
{
	if (entry->ifindex == 0)
		return false;

	if (entry->addr.proto == htons(ETH_P_IP)) {
		if (!ipv4_is_multicast(entry->addr.u.ip4))
			return false;
		if (ipv4_is_local_multicast(entry->addr.u.ip4))
			return false;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (entry->addr.proto == htons(ETH_P_IPV6)) {
		if (ipv6_addr_is_ll_all_nodes(&entry->addr.u.ip6))
			return false;
#endif
	} else
		return false;
	if (entry->state != MDB_PERMANENT && entry->state != MDB_TEMPORARY)
		return false;
	if (entry->vid >= VLAN_VID_MASK)
		return false;

	return true;
}

static int br_mdb_parse(struct sk_buff *skb, struct nlmsghdr *nlh,
			struct net_device **pdev, struct br_mdb_entry **pentry)
{
	struct net *net = sock_net(skb->sk);
	struct br_mdb_entry *entry;
	struct br_port_msg *bpm;
	struct nlattr *tb[MDBA_SET_ENTRY_MAX+1];
	struct net_device *dev;
	int err;

	err = nlmsg_parse(nlh, sizeof(*bpm), tb, MDBA_SET_ENTRY_MAX, NULL,
			  NULL);
	if (err < 0)
		return err;

	bpm = nlmsg_data(nlh);
	if (bpm->ifindex == 0) {
		pr_info("PF_BRIDGE: br_mdb_parse() with invalid ifindex\n");
		return -EINVAL;
	}

	dev = __dev_get_by_index(net, bpm->ifindex);
	if (dev == NULL) {
		pr_info("PF_BRIDGE: br_mdb_parse() with unknown ifindex\n");
		return -ENODEV;
	}

	if (!(dev->priv_flags & IFF_EBRIDGE)) {
		pr_info("PF_BRIDGE: br_mdb_parse() with non-bridge\n");
		return -EOPNOTSUPP;
	}

	*pdev = dev;

	if (!tb[MDBA_SET_ENTRY] ||
	    nla_len(tb[MDBA_SET_ENTRY]) != sizeof(struct br_mdb_entry)) {
		pr_info("PF_BRIDGE: br_mdb_parse() with invalid attr\n");
		return -EINVAL;
	}

	entry = nla_data(tb[MDBA_SET_ENTRY]);
	if (!is_valid_mdb_entry(entry)) {
		pr_info("PF_BRIDGE: br_mdb_parse() with invalid entry\n");
		return -EINVAL;
	}

	*pentry = entry;
	return 0;
}

static int br_mdb_add_group(struct net_bridge *br, struct net_bridge_port *port,
			    struct br_ip *group, unsigned char state)
{
	struct net_bridge_mdb_entry *mp;
	struct net_bridge_port_group *p;
	struct net_bridge_port_group __rcu **pp;
	struct net_bridge_mdb_htable *mdb;
	unsigned long now = jiffies;
	int err;

	mdb = mlock_dereference(br->mdb, br);
	mp = br_mdb_ip_get(mdb, group);
	if (!mp) {
		mp = br_multicast_new_group(br, port, group);
		err = PTR_ERR_OR_ZERO(mp);
		if (err)
			return err;
	}

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (p->port == port)
			return -EEXIST;
		if ((unsigned long)p->port < (unsigned long)port)
			break;
	}

	p = br_multicast_new_port_group(port, group, *pp, state, NULL);
	if (unlikely(!p))
		return -ENOMEM;
	rcu_assign_pointer(*pp, p);
	if (state == MDB_TEMPORARY)
		mod_timer(&p->timer, now + br->multicast_membership_interval);

	return 0;
}

static int __br_mdb_add(struct net *net, struct net_bridge *br,
			struct br_mdb_entry *entry)
{
	struct br_ip ip;
	struct net_device *dev;
	struct net_bridge_port *p;
	int ret;

	if (!netif_running(br->dev) || br->multicast_disabled)
		return -EINVAL;

	dev = __dev_get_by_index(net, entry->ifindex);
	if (!dev)
		return -ENODEV;

	p = br_port_get_rtnl(dev);
	if (!p || p->br != br || p->state == BR_STATE_DISABLED)
		return -EINVAL;

	__mdb_entry_to_br_ip(entry, &ip);

	spin_lock_bh(&br->multicast_lock);
	ret = br_mdb_add_group(br, p, &ip, entry->state);
	spin_unlock_bh(&br->multicast_lock);
	return ret;
}

static int br_mdb_add(struct sk_buff *skb, struct nlmsghdr *nlh,
		      struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct net_bridge_vlan_group *vg;
	struct net_device *dev, *pdev;
	struct br_mdb_entry *entry;
	struct net_bridge_port *p;
	struct net_bridge_vlan *v;
	struct net_bridge *br;
	int err;

	err = br_mdb_parse(skb, nlh, &dev, &entry);
	if (err < 0)
		return err;

	br = netdev_priv(dev);

	/* If vlan filtering is enabled and VLAN is not specified
	 * install mdb entry on all vlans configured on the port.
	 */
	pdev = __dev_get_by_index(net, entry->ifindex);
	if (!pdev)
		return -ENODEV;

	p = br_port_get_rtnl(pdev);
	if (!p || p->br != br || p->state == BR_STATE_DISABLED)
		return -EINVAL;

	vg = nbp_vlan_group(p);
	if (br_vlan_enabled(br->dev) && vg && entry->vid == 0) {
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			entry->vid = v->vid;
			err = __br_mdb_add(net, br, entry);
			if (err)
				break;
			__br_mdb_notify(dev, p, entry, RTM_NEWMDB);
		}
	} else {
		err = __br_mdb_add(net, br, entry);
		if (!err)
			__br_mdb_notify(dev, p, entry, RTM_NEWMDB);
	}

	return err;
}

static int __br_mdb_del(struct net_bridge *br, struct br_mdb_entry *entry)
{
	struct net_bridge_mdb_htable *mdb;
	struct net_bridge_mdb_entry *mp;
	struct net_bridge_port_group *p;
	struct net_bridge_port_group __rcu **pp;
	struct br_ip ip;
	int err = -EINVAL;

	if (!netif_running(br->dev) || br->multicast_disabled)
		return -EINVAL;

	__mdb_entry_to_br_ip(entry, &ip);

	spin_lock_bh(&br->multicast_lock);
	mdb = mlock_dereference(br->mdb, br);

	mp = br_mdb_ip_get(mdb, &ip);
	if (!mp)
		goto unlock;

	for (pp = &mp->ports;
	     (p = mlock_dereference(*pp, br)) != NULL;
	     pp = &p->next) {
		if (!p->port || p->port->dev->ifindex != entry->ifindex)
			continue;

		if (p->port->state == BR_STATE_DISABLED)
			goto unlock;

		__mdb_entry_fill_flags(entry, p->flags);
		rcu_assign_pointer(*pp, p->next);
		hlist_del_init(&p->mglist);
		del_timer(&p->timer);
		call_rcu_bh(&p->rcu, br_multicast_free_pg);
		err = 0;

		if (!mp->ports && !mp->host_joined &&
		    netif_running(br->dev))
			mod_timer(&mp->timer, jiffies);
		break;
	}

unlock:
	spin_unlock_bh(&br->multicast_lock);
	return err;
}

static int br_mdb_del(struct sk_buff *skb, struct nlmsghdr *nlh,
		      struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct net_bridge_vlan_group *vg;
	struct net_device *dev, *pdev;
	struct br_mdb_entry *entry;
	struct net_bridge_port *p;
	struct net_bridge_vlan *v;
	struct net_bridge *br;
	int err;

	err = br_mdb_parse(skb, nlh, &dev, &entry);
	if (err < 0)
		return err;

	br = netdev_priv(dev);

	/* If vlan filtering is enabled and VLAN is not specified
	 * delete mdb entry on all vlans configured on the port.
	 */
	pdev = __dev_get_by_index(net, entry->ifindex);
	if (!pdev)
		return -ENODEV;

	p = br_port_get_rtnl(pdev);
	if (!p || p->br != br || p->state == BR_STATE_DISABLED)
		return -EINVAL;

	vg = nbp_vlan_group(p);
	if (br_vlan_enabled(br->dev) && vg && entry->vid == 0) {
		list_for_each_entry(v, &vg->vlan_list, vlist) {
			entry->vid = v->vid;
			err = __br_mdb_del(br, entry);
			if (!err)
				__br_mdb_notify(dev, p, entry, RTM_DELMDB);
		}
	} else {
		err = __br_mdb_del(br, entry);
		if (!err)
			__br_mdb_notify(dev, p, entry, RTM_DELMDB);
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
