// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Linux IPv6 multicast routing support for BSD pim6sd
 *	Based on net/ipv4/ipmr.c.
 *
 *	(c) 2004 Mickael Hoerdt, <hoerdt@clarinet.u-strasbg.fr>
 *		LSIIT Laboratory, Strasbourg, France
 *	(c) 2004 Jean-Philippe Andriot, <jean-philippe.andriot@6WIND.com>
 *		6WIND, Paris, France
 *	Copyright (C)2007,2008 USAGI/WIDE Project
 *		YOSHIFUJI Hideaki <yoshfuji@linux-ipv6.org>
 */

#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/compat.h>
#include <linux/rhashtable.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/raw.h>
#include <linux/notifier.h>
#include <linux/if_arp.h>
#include <net/checksum.h>
#include <net/netlink.h>
#include <net/fib_rules.h>

#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <linux/mroute6.h>
#include <linux/pim.h>
#include <net/addrconf.h>
#include <linux/netfilter_ipv6.h>
#include <linux/export.h>
#include <net/ip6_checksum.h>
#include <linux/netconf.h>
#include <net/ip_tunnels.h>

#include <linux/nospec.h>

struct ip6mr_rule {
	struct fib_rule		common;
};

struct ip6mr_result {
	struct mr_table	*mrt;
};

/* Big lock, protecting vif table, mrt cache and mroute socket state.
   Note that the changes are semaphored via rtnl_lock.
 */

static DEFINE_SPINLOCK(mrt_lock);

static struct net_device *vif_dev_read(const struct vif_device *vif)
{
	return rcu_dereference(vif->dev);
}

/* Multicast router control variables */

/* Special spinlock for queue of unresolved entries */
static DEFINE_SPINLOCK(mfc_unres_lock);

/* We return to original Alan's scheme. Hash table of resolved
   entries is changed only in process context and protected
   with weak lock mrt_lock. Queue of unresolved entries is protected
   with strong spinlock mfc_unres_lock.

   In this case data path is free of exclusive locks at all.
 */

static struct kmem_cache *mrt_cachep __read_mostly;

static struct mr_table *ip6mr_new_table(struct net *net, u32 id);
static void ip6mr_free_table(struct mr_table *mrt);

static void ip6_mr_forward(struct net *net, struct mr_table *mrt,
			   struct net_device *dev, struct sk_buff *skb,
			   struct mfc6_cache *cache);
static int ip6mr_cache_report(const struct mr_table *mrt, struct sk_buff *pkt,
			      mifi_t mifi, int assert);
static void mr6_netlink_event(struct mr_table *mrt, struct mfc6_cache *mfc,
			      int cmd);
static void mrt6msg_netlink_event(const struct mr_table *mrt, struct sk_buff *pkt);
static int ip6mr_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack);
static int ip6mr_rtm_dumproute(struct sk_buff *skb,
			       struct netlink_callback *cb);
static void mroute_clean_tables(struct mr_table *mrt, int flags);
static void ipmr_expire_process(struct timer_list *t);

#ifdef CONFIG_IPV6_MROUTE_MULTIPLE_TABLES
#define ip6mr_for_each_table(mrt, net) \
	list_for_each_entry_rcu(mrt, &net->ipv6.mr6_tables, list, \
				lockdep_rtnl_is_held() || \
				list_empty(&net->ipv6.mr6_tables))

static struct mr_table *ip6mr_mr_table_iter(struct net *net,
					    struct mr_table *mrt)
{
	struct mr_table *ret;

	if (!mrt)
		ret = list_entry_rcu(net->ipv6.mr6_tables.next,
				     struct mr_table, list);
	else
		ret = list_entry_rcu(mrt->list.next,
				     struct mr_table, list);

	if (&ret->list == &net->ipv6.mr6_tables)
		return NULL;
	return ret;
}

static struct mr_table *ip6mr_get_table(struct net *net, u32 id)
{
	struct mr_table *mrt;

	ip6mr_for_each_table(mrt, net) {
		if (mrt->id == id)
			return mrt;
	}
	return NULL;
}

static int ip6mr_fib_lookup(struct net *net, struct flowi6 *flp6,
			    struct mr_table **mrt)
{
	int err;
	struct ip6mr_result res;
	struct fib_lookup_arg arg = {
		.result = &res,
		.flags = FIB_LOOKUP_NOREF,
	};

	/* update flow if oif or iif point to device enslaved to l3mdev */
	l3mdev_update_flow(net, flowi6_to_flowi(flp6));

	err = fib_rules_lookup(net->ipv6.mr6_rules_ops,
			       flowi6_to_flowi(flp6), 0, &arg);
	if (err < 0)
		return err;
	*mrt = res.mrt;
	return 0;
}

static int ip6mr_rule_action(struct fib_rule *rule, struct flowi *flp,
			     int flags, struct fib_lookup_arg *arg)
{
	struct ip6mr_result *res = arg->result;
	struct mr_table *mrt;

	switch (rule->action) {
	case FR_ACT_TO_TBL:
		break;
	case FR_ACT_UNREACHABLE:
		return -ENETUNREACH;
	case FR_ACT_PROHIBIT:
		return -EACCES;
	case FR_ACT_BLACKHOLE:
	default:
		return -EINVAL;
	}

	arg->table = fib_rule_get_table(rule, arg);

	mrt = ip6mr_get_table(rule->fr_net, arg->table);
	if (!mrt)
		return -EAGAIN;
	res->mrt = mrt;
	return 0;
}

static int ip6mr_rule_match(struct fib_rule *rule, struct flowi *flp, int flags)
{
	return 1;
}

static int ip6mr_rule_configure(struct fib_rule *rule, struct sk_buff *skb,
				struct fib_rule_hdr *frh, struct nlattr **tb,
				struct netlink_ext_ack *extack)
{
	return 0;
}

static int ip6mr_rule_compare(struct fib_rule *rule, struct fib_rule_hdr *frh,
			      struct nlattr **tb)
{
	return 1;
}

static int ip6mr_rule_fill(struct fib_rule *rule, struct sk_buff *skb,
			   struct fib_rule_hdr *frh)
{
	frh->dst_len = 0;
	frh->src_len = 0;
	frh->tos     = 0;
	return 0;
}

static const struct fib_rules_ops __net_initconst ip6mr_rules_ops_template = {
	.family		= RTNL_FAMILY_IP6MR,
	.rule_size	= sizeof(struct ip6mr_rule),
	.addr_size	= sizeof(struct in6_addr),
	.action		= ip6mr_rule_action,
	.match		= ip6mr_rule_match,
	.configure	= ip6mr_rule_configure,
	.compare	= ip6mr_rule_compare,
	.fill		= ip6mr_rule_fill,
	.nlgroup	= RTNLGRP_IPV6_RULE,
	.owner		= THIS_MODULE,
};

static int __net_init ip6mr_rules_init(struct net *net)
{
	struct fib_rules_ops *ops;
	struct mr_table *mrt;
	int err;

	ops = fib_rules_register(&ip6mr_rules_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	INIT_LIST_HEAD(&net->ipv6.mr6_tables);

	mrt = ip6mr_new_table(net, RT6_TABLE_DFLT);
	if (IS_ERR(mrt)) {
		err = PTR_ERR(mrt);
		goto err1;
	}

	err = fib_default_rule_add(ops, 0x7fff, RT6_TABLE_DFLT, 0);
	if (err < 0)
		goto err2;

	net->ipv6.mr6_rules_ops = ops;
	return 0;

err2:
	rtnl_lock();
	ip6mr_free_table(mrt);
	rtnl_unlock();
err1:
	fib_rules_unregister(ops);
	return err;
}

static void __net_exit ip6mr_rules_exit(struct net *net)
{
	struct mr_table *mrt, *next;

	ASSERT_RTNL();
	list_for_each_entry_safe(mrt, next, &net->ipv6.mr6_tables, list) {
		list_del(&mrt->list);
		ip6mr_free_table(mrt);
	}
	fib_rules_unregister(net->ipv6.mr6_rules_ops);
}

static int ip6mr_rules_dump(struct net *net, struct notifier_block *nb,
			    struct netlink_ext_ack *extack)
{
	return fib_rules_dump(net, nb, RTNL_FAMILY_IP6MR, extack);
}

static unsigned int ip6mr_rules_seq_read(struct net *net)
{
	return fib_rules_seq_read(net, RTNL_FAMILY_IP6MR);
}

bool ip6mr_rule_default(const struct fib_rule *rule)
{
	return fib_rule_matchall(rule) && rule->action == FR_ACT_TO_TBL &&
	       rule->table == RT6_TABLE_DFLT && !rule->l3mdev;
}
EXPORT_SYMBOL(ip6mr_rule_default);
#else
#define ip6mr_for_each_table(mrt, net) \
	for (mrt = net->ipv6.mrt6; mrt; mrt = NULL)

static struct mr_table *ip6mr_mr_table_iter(struct net *net,
					    struct mr_table *mrt)
{
	if (!mrt)
		return net->ipv6.mrt6;
	return NULL;
}

static struct mr_table *ip6mr_get_table(struct net *net, u32 id)
{
	return net->ipv6.mrt6;
}

static int ip6mr_fib_lookup(struct net *net, struct flowi6 *flp6,
			    struct mr_table **mrt)
{
	*mrt = net->ipv6.mrt6;
	return 0;
}

static int __net_init ip6mr_rules_init(struct net *net)
{
	struct mr_table *mrt;

	mrt = ip6mr_new_table(net, RT6_TABLE_DFLT);
	if (IS_ERR(mrt))
		return PTR_ERR(mrt);
	net->ipv6.mrt6 = mrt;
	return 0;
}

static void __net_exit ip6mr_rules_exit(struct net *net)
{
	ASSERT_RTNL();
	ip6mr_free_table(net->ipv6.mrt6);
	net->ipv6.mrt6 = NULL;
}

static int ip6mr_rules_dump(struct net *net, struct notifier_block *nb,
			    struct netlink_ext_ack *extack)
{
	return 0;
}

static unsigned int ip6mr_rules_seq_read(struct net *net)
{
	return 0;
}
#endif

static int ip6mr_hash_cmp(struct rhashtable_compare_arg *arg,
			  const void *ptr)
{
	const struct mfc6_cache_cmp_arg *cmparg = arg->key;
	struct mfc6_cache *c = (struct mfc6_cache *)ptr;

	return !ipv6_addr_equal(&c->mf6c_mcastgrp, &cmparg->mf6c_mcastgrp) ||
	       !ipv6_addr_equal(&c->mf6c_origin, &cmparg->mf6c_origin);
}

static const struct rhashtable_params ip6mr_rht_params = {
	.head_offset = offsetof(struct mr_mfc, mnode),
	.key_offset = offsetof(struct mfc6_cache, cmparg),
	.key_len = sizeof(struct mfc6_cache_cmp_arg),
	.nelem_hint = 3,
	.obj_cmpfn = ip6mr_hash_cmp,
	.automatic_shrinking = true,
};

static void ip6mr_new_table_set(struct mr_table *mrt,
				struct net *net)
{
#ifdef CONFIG_IPV6_MROUTE_MULTIPLE_TABLES
	list_add_tail_rcu(&mrt->list, &net->ipv6.mr6_tables);
#endif
}

static struct mfc6_cache_cmp_arg ip6mr_mr_table_ops_cmparg_any = {
	.mf6c_origin = IN6ADDR_ANY_INIT,
	.mf6c_mcastgrp = IN6ADDR_ANY_INIT,
};

static struct mr_table_ops ip6mr_mr_table_ops = {
	.rht_params = &ip6mr_rht_params,
	.cmparg_any = &ip6mr_mr_table_ops_cmparg_any,
};

static struct mr_table *ip6mr_new_table(struct net *net, u32 id)
{
	struct mr_table *mrt;

	mrt = ip6mr_get_table(net, id);
	if (mrt)
		return mrt;

	return mr_table_alloc(net, id, &ip6mr_mr_table_ops,
			      ipmr_expire_process, ip6mr_new_table_set);
}

static void ip6mr_free_table(struct mr_table *mrt)
{
	del_timer_sync(&mrt->ipmr_expire_timer);
	mroute_clean_tables(mrt, MRT6_FLUSH_MIFS | MRT6_FLUSH_MIFS_STATIC |
				 MRT6_FLUSH_MFC | MRT6_FLUSH_MFC_STATIC);
	rhltable_destroy(&mrt->mfc_hash);
	kfree(mrt);
}

#ifdef CONFIG_PROC_FS
/* The /proc interfaces to multicast routing
 * /proc/ip6_mr_cache /proc/ip6_mr_vif
 */

static void *ip6mr_vif_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct mr_vif_iter *iter = seq->private;
	struct net *net = seq_file_net(seq);
	struct mr_table *mrt;

	mrt = ip6mr_get_table(net, RT6_TABLE_DFLT);
	if (!mrt)
		return ERR_PTR(-ENOENT);

	iter->mrt = mrt;

	rcu_read_lock();
	return mr_vif_seq_start(seq, pos);
}

static void ip6mr_vif_seq_stop(struct seq_file *seq, void *v)
	__releases(RCU)
{
	rcu_read_unlock();
}

static int ip6mr_vif_seq_show(struct seq_file *seq, void *v)
{
	struct mr_vif_iter *iter = seq->private;
	struct mr_table *mrt = iter->mrt;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq,
			 "Interface      BytesIn  PktsIn  BytesOut PktsOut Flags\n");
	} else {
		const struct vif_device *vif = v;
		const struct net_device *vif_dev;
		const char *name;

		vif_dev = vif_dev_read(vif);
		name = vif_dev ? vif_dev->name : "none";

		seq_printf(seq,
			   "%2td %-10s %8ld %7ld  %8ld %7ld %05X\n",
			   vif - mrt->vif_table,
			   name, vif->bytes_in, vif->pkt_in,
			   vif->bytes_out, vif->pkt_out,
			   vif->flags);
	}
	return 0;
}

static const struct seq_operations ip6mr_vif_seq_ops = {
	.start = ip6mr_vif_seq_start,
	.next  = mr_vif_seq_next,
	.stop  = ip6mr_vif_seq_stop,
	.show  = ip6mr_vif_seq_show,
};

static void *ipmr_mfc_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	struct mr_table *mrt;

	mrt = ip6mr_get_table(net, RT6_TABLE_DFLT);
	if (!mrt)
		return ERR_PTR(-ENOENT);

	return mr_mfc_seq_start(seq, pos, mrt, &mfc_unres_lock);
}

static int ipmr_mfc_seq_show(struct seq_file *seq, void *v)
{
	int n;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq,
			 "Group                            "
			 "Origin                           "
			 "Iif      Pkts  Bytes     Wrong  Oifs\n");
	} else {
		const struct mfc6_cache *mfc = v;
		const struct mr_mfc_iter *it = seq->private;
		struct mr_table *mrt = it->mrt;

		seq_printf(seq, "%pI6 %pI6 %-3hd",
			   &mfc->mf6c_mcastgrp, &mfc->mf6c_origin,
			   mfc->_c.mfc_parent);

		if (it->cache != &mrt->mfc_unres_queue) {
			seq_printf(seq, " %8lu %8lu %8lu",
				   mfc->_c.mfc_un.res.pkt,
				   mfc->_c.mfc_un.res.bytes,
				   mfc->_c.mfc_un.res.wrong_if);
			for (n = mfc->_c.mfc_un.res.minvif;
			     n < mfc->_c.mfc_un.res.maxvif; n++) {
				if (VIF_EXISTS(mrt, n) &&
				    mfc->_c.mfc_un.res.ttls[n] < 255)
					seq_printf(seq,
						   " %2d:%-3d", n,
						   mfc->_c.mfc_un.res.ttls[n]);
			}
		} else {
			/* unresolved mfc_caches don't contain
			 * pkt, bytes and wrong_if values
			 */
			seq_printf(seq, " %8lu %8lu %8lu", 0ul, 0ul, 0ul);
		}
		seq_putc(seq, '\n');
	}
	return 0;
}

static const struct seq_operations ipmr_mfc_seq_ops = {
	.start = ipmr_mfc_seq_start,
	.next  = mr_mfc_seq_next,
	.stop  = mr_mfc_seq_stop,
	.show  = ipmr_mfc_seq_show,
};
#endif

#ifdef CONFIG_IPV6_PIMSM_V2

static int pim6_rcv(struct sk_buff *skb)
{
	struct pimreghdr *pim;
	struct ipv6hdr   *encap;
	struct net_device  *reg_dev = NULL;
	struct net *net = dev_net(skb->dev);
	struct mr_table *mrt;
	struct flowi6 fl6 = {
		.flowi6_iif	= skb->dev->ifindex,
		.flowi6_mark	= skb->mark,
	};
	int reg_vif_num;

	if (!pskb_may_pull(skb, sizeof(*pim) + sizeof(*encap)))
		goto drop;

	pim = (struct pimreghdr *)skb_transport_header(skb);
	if (pim->type != ((PIM_VERSION << 4) | PIM_TYPE_REGISTER) ||
	    (pim->flags & PIM_NULL_REGISTER) ||
	    (csum_ipv6_magic(&ipv6_hdr(skb)->saddr, &ipv6_hdr(skb)->daddr,
			     sizeof(*pim), IPPROTO_PIM,
			     csum_partial((void *)pim, sizeof(*pim), 0)) &&
	     csum_fold(skb_checksum(skb, 0, skb->len, 0))))
		goto drop;

	/* check if the inner packet is destined to mcast group */
	encap = (struct ipv6hdr *)(skb_transport_header(skb) +
				   sizeof(*pim));

	if (!ipv6_addr_is_multicast(&encap->daddr) ||
	    encap->payload_len == 0 ||
	    ntohs(encap->payload_len) + sizeof(*pim) > skb->len)
		goto drop;

	if (ip6mr_fib_lookup(net, &fl6, &mrt) < 0)
		goto drop;

	/* Pairs with WRITE_ONCE() in mif6_add()/mif6_delete() */
	reg_vif_num = READ_ONCE(mrt->mroute_reg_vif_num);
	if (reg_vif_num >= 0)
		reg_dev = vif_dev_read(&mrt->vif_table[reg_vif_num]);

	if (!reg_dev)
		goto drop;

	skb->mac_header = skb->network_header;
	skb_pull(skb, (u8 *)encap - skb->data);
	skb_reset_network_header(skb);
	skb->protocol = htons(ETH_P_IPV6);
	skb->ip_summed = CHECKSUM_NONE;

	skb_tunnel_rx(skb, reg_dev, dev_net(reg_dev));

	netif_rx(skb);

	return 0;
 drop:
	kfree_skb(skb);
	return 0;
}

static const struct inet6_protocol pim6_protocol = {
	.handler	=	pim6_rcv,
};

/* Service routines creating virtual interfaces: PIMREG */

static netdev_tx_t reg_vif_xmit(struct sk_buff *skb,
				      struct net_device *dev)
{
	struct net *net = dev_net(dev);
	struct mr_table *mrt;
	struct flowi6 fl6 = {
		.flowi6_oif	= dev->ifindex,
		.flowi6_iif	= skb->skb_iif ? : LOOPBACK_IFINDEX,
		.flowi6_mark	= skb->mark,
	};

	if (!pskb_inet_may_pull(skb))
		goto tx_err;

	if (ip6mr_fib_lookup(net, &fl6, &mrt) < 0)
		goto tx_err;

	dev->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;
	rcu_read_lock();
	ip6mr_cache_report(mrt, skb, READ_ONCE(mrt->mroute_reg_vif_num),
			   MRT6MSG_WHOLEPKT);
	rcu_read_unlock();
	kfree_skb(skb);
	return NETDEV_TX_OK;

tx_err:
	dev->stats.tx_errors++;
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int reg_vif_get_iflink(const struct net_device *dev)
{
	return 0;
}

static const struct net_device_ops reg_vif_netdev_ops = {
	.ndo_start_xmit	= reg_vif_xmit,
	.ndo_get_iflink = reg_vif_get_iflink,
};

static void reg_vif_setup(struct net_device *dev)
{
	dev->type		= ARPHRD_PIMREG;
	dev->mtu		= 1500 - sizeof(struct ipv6hdr) - 8;
	dev->flags		= IFF_NOARP;
	dev->netdev_ops		= &reg_vif_netdev_ops;
	dev->needs_free_netdev	= true;
	dev->features		|= NETIF_F_NETNS_LOCAL;
}

static struct net_device *ip6mr_reg_vif(struct net *net, struct mr_table *mrt)
{
	struct net_device *dev;
	char name[IFNAMSIZ];

	if (mrt->id == RT6_TABLE_DFLT)
		sprintf(name, "pim6reg");
	else
		sprintf(name, "pim6reg%u", mrt->id);

	dev = alloc_netdev(0, name, NET_NAME_UNKNOWN, reg_vif_setup);
	if (!dev)
		return NULL;

	dev_net_set(dev, net);

	if (register_netdevice(dev)) {
		free_netdev(dev);
		return NULL;
	}

	if (dev_open(dev, NULL))
		goto failure;

	dev_hold(dev);
	return dev;

failure:
	unregister_netdevice(dev);
	return NULL;
}
#endif

static int call_ip6mr_vif_entry_notifiers(struct net *net,
					  enum fib_event_type event_type,
					  struct vif_device *vif,
					  struct net_device *vif_dev,
					  mifi_t vif_index, u32 tb_id)
{
	return mr_call_vif_notifiers(net, RTNL_FAMILY_IP6MR, event_type,
				     vif, vif_dev, vif_index, tb_id,
				     &net->ipv6.ipmr_seq);
}

static int call_ip6mr_mfc_entry_notifiers(struct net *net,
					  enum fib_event_type event_type,
					  struct mfc6_cache *mfc, u32 tb_id)
{
	return mr_call_mfc_notifiers(net, RTNL_FAMILY_IP6MR, event_type,
				     &mfc->_c, tb_id, &net->ipv6.ipmr_seq);
}

/* Delete a VIF entry */
static int mif6_delete(struct mr_table *mrt, int vifi, int notify,
		       struct list_head *head)
{
	struct vif_device *v;
	struct net_device *dev;
	struct inet6_dev *in6_dev;

	if (vifi < 0 || vifi >= mrt->maxvif)
		return -EADDRNOTAVAIL;

	v = &mrt->vif_table[vifi];

	dev = rtnl_dereference(v->dev);
	if (!dev)
		return -EADDRNOTAVAIL;

	call_ip6mr_vif_entry_notifiers(read_pnet(&mrt->net),
				       FIB_EVENT_VIF_DEL, v, dev,
				       vifi, mrt->id);
	spin_lock(&mrt_lock);
	RCU_INIT_POINTER(v->dev, NULL);

#ifdef CONFIG_IPV6_PIMSM_V2
	if (vifi == mrt->mroute_reg_vif_num) {
		/* Pairs with READ_ONCE() in ip6mr_cache_report() and reg_vif_xmit() */
		WRITE_ONCE(mrt->mroute_reg_vif_num, -1);
	}
#endif

	if (vifi + 1 == mrt->maxvif) {
		int tmp;
		for (tmp = vifi - 1; tmp >= 0; tmp--) {
			if (VIF_EXISTS(mrt, tmp))
				break;
		}
		WRITE_ONCE(mrt->maxvif, tmp + 1);
	}

	spin_unlock(&mrt_lock);

	dev_set_allmulti(dev, -1);

	in6_dev = __in6_dev_get(dev);
	if (in6_dev) {
		atomic_dec(&in6_dev->cnf.mc_forwarding);
		inet6_netconf_notify_devconf(dev_net(dev), RTM_NEWNETCONF,
					     NETCONFA_MC_FORWARDING,
					     dev->ifindex, &in6_dev->cnf);
	}

	if ((v->flags & MIFF_REGISTER) && !notify)
		unregister_netdevice_queue(dev, head);

	netdev_put(dev, &v->dev_tracker);
	return 0;
}

static inline void ip6mr_cache_free_rcu(struct rcu_head *head)
{
	struct mr_mfc *c = container_of(head, struct mr_mfc, rcu);

	kmem_cache_free(mrt_cachep, (struct mfc6_cache *)c);
}

static inline void ip6mr_cache_free(struct mfc6_cache *c)
{
	call_rcu(&c->_c.rcu, ip6mr_cache_free_rcu);
}

/* Destroy an unresolved cache entry, killing queued skbs
   and reporting error to netlink readers.
 */

static void ip6mr_destroy_unres(struct mr_table *mrt, struct mfc6_cache *c)
{
	struct net *net = read_pnet(&mrt->net);
	struct sk_buff *skb;

	atomic_dec(&mrt->cache_resolve_queue_len);

	while ((skb = skb_dequeue(&c->_c.mfc_un.unres.unresolved)) != NULL) {
		if (ipv6_hdr(skb)->version == 0) {
			struct nlmsghdr *nlh = skb_pull(skb,
							sizeof(struct ipv6hdr));
			nlh->nlmsg_type = NLMSG_ERROR;
			nlh->nlmsg_len = nlmsg_msg_size(sizeof(struct nlmsgerr));
			skb_trim(skb, nlh->nlmsg_len);
			((struct nlmsgerr *)nlmsg_data(nlh))->error = -ETIMEDOUT;
			rtnl_unicast(skb, net, NETLINK_CB(skb).portid);
		} else
			kfree_skb(skb);
	}

	ip6mr_cache_free(c);
}


/* Timer process for all the unresolved queue. */

static void ipmr_do_expire_process(struct mr_table *mrt)
{
	unsigned long now = jiffies;
	unsigned long expires = 10 * HZ;
	struct mr_mfc *c, *next;

	list_for_each_entry_safe(c, next, &mrt->mfc_unres_queue, list) {
		if (time_after(c->mfc_un.unres.expires, now)) {
			/* not yet... */
			unsigned long interval = c->mfc_un.unres.expires - now;
			if (interval < expires)
				expires = interval;
			continue;
		}

		list_del(&c->list);
		mr6_netlink_event(mrt, (struct mfc6_cache *)c, RTM_DELROUTE);
		ip6mr_destroy_unres(mrt, (struct mfc6_cache *)c);
	}

	if (!list_empty(&mrt->mfc_unres_queue))
		mod_timer(&mrt->ipmr_expire_timer, jiffies + expires);
}

static void ipmr_expire_process(struct timer_list *t)
{
	struct mr_table *mrt = from_timer(mrt, t, ipmr_expire_timer);

	if (!spin_trylock(&mfc_unres_lock)) {
		mod_timer(&mrt->ipmr_expire_timer, jiffies + 1);
		return;
	}

	if (!list_empty(&mrt->mfc_unres_queue))
		ipmr_do_expire_process(mrt);

	spin_unlock(&mfc_unres_lock);
}

/* Fill oifs list. It is called under locked mrt_lock. */

static void ip6mr_update_thresholds(struct mr_table *mrt,
				    struct mr_mfc *cache,
				    unsigned char *ttls)
{
	int vifi;

	cache->mfc_un.res.minvif = MAXMIFS;
	cache->mfc_un.res.maxvif = 0;
	memset(cache->mfc_un.res.ttls, 255, MAXMIFS);

	for (vifi = 0; vifi < mrt->maxvif; vifi++) {
		if (VIF_EXISTS(mrt, vifi) &&
		    ttls[vifi] && ttls[vifi] < 255) {
			cache->mfc_un.res.ttls[vifi] = ttls[vifi];
			if (cache->mfc_un.res.minvif > vifi)
				cache->mfc_un.res.minvif = vifi;
			if (cache->mfc_un.res.maxvif <= vifi)
				cache->mfc_un.res.maxvif = vifi + 1;
		}
	}
	cache->mfc_un.res.lastuse = jiffies;
}

static int mif6_add(struct net *net, struct mr_table *mrt,
		    struct mif6ctl *vifc, int mrtsock)
{
	int vifi = vifc->mif6c_mifi;
	struct vif_device *v = &mrt->vif_table[vifi];
	struct net_device *dev;
	struct inet6_dev *in6_dev;
	int err;

	/* Is vif busy ? */
	if (VIF_EXISTS(mrt, vifi))
		return -EADDRINUSE;

	switch (vifc->mif6c_flags) {
#ifdef CONFIG_IPV6_PIMSM_V2
	case MIFF_REGISTER:
		/*
		 * Special Purpose VIF in PIM
		 * All the packets will be sent to the daemon
		 */
		if (mrt->mroute_reg_vif_num >= 0)
			return -EADDRINUSE;
		dev = ip6mr_reg_vif(net, mrt);
		if (!dev)
			return -ENOBUFS;
		err = dev_set_allmulti(dev, 1);
		if (err) {
			unregister_netdevice(dev);
			dev_put(dev);
			return err;
		}
		break;
#endif
	case 0:
		dev = dev_get_by_index(net, vifc->mif6c_pifi);
		if (!dev)
			return -EADDRNOTAVAIL;
		err = dev_set_allmulti(dev, 1);
		if (err) {
			dev_put(dev);
			return err;
		}
		break;
	default:
		return -EINVAL;
	}

	in6_dev = __in6_dev_get(dev);
	if (in6_dev) {
		atomic_inc(&in6_dev->cnf.mc_forwarding);
		inet6_netconf_notify_devconf(dev_net(dev), RTM_NEWNETCONF,
					     NETCONFA_MC_FORWARDING,
					     dev->ifindex, &in6_dev->cnf);
	}

	/* Fill in the VIF structures */
	vif_device_init(v, dev, vifc->vifc_rate_limit, vifc->vifc_threshold,
			vifc->mif6c_flags | (!mrtsock ? VIFF_STATIC : 0),
			MIFF_REGISTER);

	/* And finish update writing critical data */
	spin_lock(&mrt_lock);
	rcu_assign_pointer(v->dev, dev);
	netdev_tracker_alloc(dev, &v->dev_tracker, GFP_ATOMIC);
#ifdef CONFIG_IPV6_PIMSM_V2
	if (v->flags & MIFF_REGISTER)
		WRITE_ONCE(mrt->mroute_reg_vif_num, vifi);
#endif
	if (vifi + 1 > mrt->maxvif)
		WRITE_ONCE(mrt->maxvif, vifi + 1);
	spin_unlock(&mrt_lock);
	call_ip6mr_vif_entry_notifiers(net, FIB_EVENT_VIF_ADD,
				       v, dev, vifi, mrt->id);
	return 0;
}

static struct mfc6_cache *ip6mr_cache_find(struct mr_table *mrt,
					   const struct in6_addr *origin,
					   const struct in6_addr *mcastgrp)
{
	struct mfc6_cache_cmp_arg arg = {
		.mf6c_origin = *origin,
		.mf6c_mcastgrp = *mcastgrp,
	};

	return mr_mfc_find(mrt, &arg);
}

/* Look for a (*,G) entry */
static struct mfc6_cache *ip6mr_cache_find_any(struct mr_table *mrt,
					       struct in6_addr *mcastgrp,
					       mifi_t mifi)
{
	struct mfc6_cache_cmp_arg arg = {
		.mf6c_origin = in6addr_any,
		.mf6c_mcastgrp = *mcastgrp,
	};

	if (ipv6_addr_any(mcastgrp))
		return mr_mfc_find_any_parent(mrt, mifi);
	return mr_mfc_find_any(mrt, mifi, &arg);
}

/* Look for a (S,G,iif) entry if parent != -1 */
static struct mfc6_cache *
ip6mr_cache_find_parent(struct mr_table *mrt,
			const struct in6_addr *origin,
			const struct in6_addr *mcastgrp,
			int parent)
{
	struct mfc6_cache_cmp_arg arg = {
		.mf6c_origin = *origin,
		.mf6c_mcastgrp = *mcastgrp,
	};

	return mr_mfc_find_parent(mrt, &arg, parent);
}

/* Allocate a multicast cache entry */
static struct mfc6_cache *ip6mr_cache_alloc(void)
{
	struct mfc6_cache *c = kmem_cache_zalloc(mrt_cachep, GFP_KERNEL);
	if (!c)
		return NULL;
	c->_c.mfc_un.res.last_assert = jiffies - MFC_ASSERT_THRESH - 1;
	c->_c.mfc_un.res.minvif = MAXMIFS;
	c->_c.free = ip6mr_cache_free_rcu;
	refcount_set(&c->_c.mfc_un.res.refcount, 1);
	return c;
}

static struct mfc6_cache *ip6mr_cache_alloc_unres(void)
{
	struct mfc6_cache *c = kmem_cache_zalloc(mrt_cachep, GFP_ATOMIC);
	if (!c)
		return NULL;
	skb_queue_head_init(&c->_c.mfc_un.unres.unresolved);
	c->_c.mfc_un.unres.expires = jiffies + 10 * HZ;
	return c;
}

/*
 *	A cache entry has gone into a resolved state from queued
 */

static void ip6mr_cache_resolve(struct net *net, struct mr_table *mrt,
				struct mfc6_cache *uc, struct mfc6_cache *c)
{
	struct sk_buff *skb;

	/*
	 *	Play the pending entries through our router
	 */

	while ((skb = __skb_dequeue(&uc->_c.mfc_un.unres.unresolved))) {
		if (ipv6_hdr(skb)->version == 0) {
			struct nlmsghdr *nlh = skb_pull(skb,
							sizeof(struct ipv6hdr));

			if (mr_fill_mroute(mrt, skb, &c->_c,
					   nlmsg_data(nlh)) > 0) {
				nlh->nlmsg_len = skb_tail_pointer(skb) - (u8 *)nlh;
			} else {
				nlh->nlmsg_type = NLMSG_ERROR;
				nlh->nlmsg_len = nlmsg_msg_size(sizeof(struct nlmsgerr));
				skb_trim(skb, nlh->nlmsg_len);
				((struct nlmsgerr *)nlmsg_data(nlh))->error = -EMSGSIZE;
			}
			rtnl_unicast(skb, net, NETLINK_CB(skb).portid);
		} else {
			rcu_read_lock();
			ip6_mr_forward(net, mrt, skb->dev, skb, c);
			rcu_read_unlock();
		}
	}
}

/*
 *	Bounce a cache query up to pim6sd and netlink.
 *
 *	Called under rcu_read_lock()
 */

static int ip6mr_cache_report(const struct mr_table *mrt, struct sk_buff *pkt,
			      mifi_t mifi, int assert)
{
	struct sock *mroute6_sk;
	struct sk_buff *skb;
	struct mrt6msg *msg;
	int ret;

#ifdef CONFIG_IPV6_PIMSM_V2
	if (assert == MRT6MSG_WHOLEPKT || assert == MRT6MSG_WRMIFWHOLE)
		skb = skb_realloc_headroom(pkt, -skb_network_offset(pkt)
						+sizeof(*msg));
	else
#endif
		skb = alloc_skb(sizeof(struct ipv6hdr) + sizeof(*msg), GFP_ATOMIC);

	if (!skb)
		return -ENOBUFS;

	/* I suppose that internal messages
	 * do not require checksums */

	skb->ip_summed = CHECKSUM_UNNECESSARY;

#ifdef CONFIG_IPV6_PIMSM_V2
	if (assert == MRT6MSG_WHOLEPKT || assert == MRT6MSG_WRMIFWHOLE) {
		/* Ugly, but we have no choice with this interface.
		   Duplicate old header, fix length etc.
		   And all this only to mangle msg->im6_msgtype and
		   to set msg->im6_mbz to "mbz" :-)
		 */
		skb_push(skb, -skb_network_offset(pkt));

		skb_push(skb, sizeof(*msg));
		skb_reset_transport_header(skb);
		msg = (struct mrt6msg *)skb_transport_header(skb);
		msg->im6_mbz = 0;
		msg->im6_msgtype = assert;
		if (assert == MRT6MSG_WRMIFWHOLE)
			msg->im6_mif = mifi;
		else
			msg->im6_mif = READ_ONCE(mrt->mroute_reg_vif_num);
		msg->im6_pad = 0;
		msg->im6_src = ipv6_hdr(pkt)->saddr;
		msg->im6_dst = ipv6_hdr(pkt)->daddr;

		skb->ip_summed = CHECKSUM_UNNECESSARY;
	} else
#endif
	{
	/*
	 *	Copy the IP header
	 */

	skb_put(skb, sizeof(struct ipv6hdr));
	skb_reset_network_header(skb);
	skb_copy_to_linear_data(skb, ipv6_hdr(pkt), sizeof(struct ipv6hdr));

	/*
	 *	Add our header
	 */
	skb_put(skb, sizeof(*msg));
	skb_reset_transport_header(skb);
	msg = (struct mrt6msg *)skb_transport_header(skb);

	msg->im6_mbz = 0;
	msg->im6_msgtype = assert;
	msg->im6_mif = mifi;
	msg->im6_pad = 0;
	msg->im6_src = ipv6_hdr(pkt)->saddr;
	msg->im6_dst = ipv6_hdr(pkt)->daddr;

	skb_dst_set(skb, dst_clone(skb_dst(pkt)));
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	}

	mroute6_sk = rcu_dereference(mrt->mroute_sk);
	if (!mroute6_sk) {
		kfree_skb(skb);
		return -EINVAL;
	}

	mrt6msg_netlink_event(mrt, skb);

	/* Deliver to user space multicast routing algorithms */
	ret = sock_queue_rcv_skb(mroute6_sk, skb);

	if (ret < 0) {
		net_warn_ratelimited("mroute6: pending queue full, dropping entries\n");
		kfree_skb(skb);
	}

	return ret;
}

/* Queue a packet for resolution. It gets locked cache entry! */
static int ip6mr_cache_unresolved(struct mr_table *mrt, mifi_t mifi,
				  struct sk_buff *skb, struct net_device *dev)
{
	struct mfc6_cache *c;
	bool found = false;
	int err;

	spin_lock_bh(&mfc_unres_lock);
	list_for_each_entry(c, &mrt->mfc_unres_queue, _c.list) {
		if (ipv6_addr_equal(&c->mf6c_mcastgrp, &ipv6_hdr(skb)->daddr) &&
		    ipv6_addr_equal(&c->mf6c_origin, &ipv6_hdr(skb)->saddr)) {
			found = true;
			break;
		}
	}

	if (!found) {
		/*
		 *	Create a new entry if allowable
		 */

		c = ip6mr_cache_alloc_unres();
		if (!c) {
			spin_unlock_bh(&mfc_unres_lock);

			kfree_skb(skb);
			return -ENOBUFS;
		}

		/* Fill in the new cache entry */
		c->_c.mfc_parent = -1;
		c->mf6c_origin = ipv6_hdr(skb)->saddr;
		c->mf6c_mcastgrp = ipv6_hdr(skb)->daddr;

		/*
		 *	Reflect first query at pim6sd
		 */
		err = ip6mr_cache_report(mrt, skb, mifi, MRT6MSG_NOCACHE);
		if (err < 0) {
			/* If the report failed throw the cache entry
			   out - Brad Parker
			 */
			spin_unlock_bh(&mfc_unres_lock);

			ip6mr_cache_free(c);
			kfree_skb(skb);
			return err;
		}

		atomic_inc(&mrt->cache_resolve_queue_len);
		list_add(&c->_c.list, &mrt->mfc_unres_queue);
		mr6_netlink_event(mrt, c, RTM_NEWROUTE);

		ipmr_do_expire_process(mrt);
	}

	/* See if we can append the packet */
	if (c->_c.mfc_un.unres.unresolved.qlen > 3) {
		kfree_skb(skb);
		err = -ENOBUFS;
	} else {
		if (dev) {
			skb->dev = dev;
			skb->skb_iif = dev->ifindex;
		}
		skb_queue_tail(&c->_c.mfc_un.unres.unresolved, skb);
		err = 0;
	}

	spin_unlock_bh(&mfc_unres_lock);
	return err;
}

/*
 *	MFC6 cache manipulation by user space
 */

static int ip6mr_mfc_delete(struct mr_table *mrt, struct mf6cctl *mfc,
			    int parent)
{
	struct mfc6_cache *c;

	/* The entries are added/deleted only under RTNL */
	rcu_read_lock();
	c = ip6mr_cache_find_parent(mrt, &mfc->mf6cc_origin.sin6_addr,
				    &mfc->mf6cc_mcastgrp.sin6_addr, parent);
	rcu_read_unlock();
	if (!c)
		return -ENOENT;
	rhltable_remove(&mrt->mfc_hash, &c->_c.mnode, ip6mr_rht_params);
	list_del_rcu(&c->_c.list);

	call_ip6mr_mfc_entry_notifiers(read_pnet(&mrt->net),
				       FIB_EVENT_ENTRY_DEL, c, mrt->id);
	mr6_netlink_event(mrt, c, RTM_DELROUTE);
	mr_cache_put(&c->_c);
	return 0;
}

static int ip6mr_device_event(struct notifier_block *this,
			      unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);
	struct mr_table *mrt;
	struct vif_device *v;
	int ct;

	if (event != NETDEV_UNREGISTER)
		return NOTIFY_DONE;

	ip6mr_for_each_table(mrt, net) {
		v = &mrt->vif_table[0];
		for (ct = 0; ct < mrt->maxvif; ct++, v++) {
			if (rcu_access_pointer(v->dev) == dev)
				mif6_delete(mrt, ct, 1, NULL);
		}
	}

	return NOTIFY_DONE;
}

static unsigned int ip6mr_seq_read(struct net *net)
{
	ASSERT_RTNL();

	return net->ipv6.ipmr_seq + ip6mr_rules_seq_read(net);
}

static int ip6mr_dump(struct net *net, struct notifier_block *nb,
		      struct netlink_ext_ack *extack)
{
	return mr_dump(net, nb, RTNL_FAMILY_IP6MR, ip6mr_rules_dump,
		       ip6mr_mr_table_iter, extack);
}

static struct notifier_block ip6_mr_notifier = {
	.notifier_call = ip6mr_device_event
};

static const struct fib_notifier_ops ip6mr_notifier_ops_template = {
	.family		= RTNL_FAMILY_IP6MR,
	.fib_seq_read	= ip6mr_seq_read,
	.fib_dump	= ip6mr_dump,
	.owner		= THIS_MODULE,
};

static int __net_init ip6mr_notifier_init(struct net *net)
{
	struct fib_notifier_ops *ops;

	net->ipv6.ipmr_seq = 0;

	ops = fib_notifier_ops_register(&ip6mr_notifier_ops_template, net);
	if (IS_ERR(ops))
		return PTR_ERR(ops);

	net->ipv6.ip6mr_notifier_ops = ops;

	return 0;
}

static void __net_exit ip6mr_notifier_exit(struct net *net)
{
	fib_notifier_ops_unregister(net->ipv6.ip6mr_notifier_ops);
	net->ipv6.ip6mr_notifier_ops = NULL;
}

/* Setup for IP multicast routing */
static int __net_init ip6mr_net_init(struct net *net)
{
	int err;

	err = ip6mr_notifier_init(net);
	if (err)
		return err;

	err = ip6mr_rules_init(net);
	if (err < 0)
		goto ip6mr_rules_fail;

#ifdef CONFIG_PROC_FS
	err = -ENOMEM;
	if (!proc_create_net("ip6_mr_vif", 0, net->proc_net, &ip6mr_vif_seq_ops,
			sizeof(struct mr_vif_iter)))
		goto proc_vif_fail;
	if (!proc_create_net("ip6_mr_cache", 0, net->proc_net, &ipmr_mfc_seq_ops,
			sizeof(struct mr_mfc_iter)))
		goto proc_cache_fail;
#endif

	return 0;

#ifdef CONFIG_PROC_FS
proc_cache_fail:
	remove_proc_entry("ip6_mr_vif", net->proc_net);
proc_vif_fail:
	rtnl_lock();
	ip6mr_rules_exit(net);
	rtnl_unlock();
#endif
ip6mr_rules_fail:
	ip6mr_notifier_exit(net);
	return err;
}

static void __net_exit ip6mr_net_exit(struct net *net)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("ip6_mr_cache", net->proc_net);
	remove_proc_entry("ip6_mr_vif", net->proc_net);
#endif
	ip6mr_notifier_exit(net);
}

static void __net_exit ip6mr_net_exit_batch(struct list_head *net_list)
{
	struct net *net;

	rtnl_lock();
	list_for_each_entry(net, net_list, exit_list)
		ip6mr_rules_exit(net);
	rtnl_unlock();
}

static struct pernet_operations ip6mr_net_ops = {
	.init = ip6mr_net_init,
	.exit = ip6mr_net_exit,
	.exit_batch = ip6mr_net_exit_batch,
};

int __init ip6_mr_init(void)
{
	int err;

	mrt_cachep = kmem_cache_create("ip6_mrt_cache",
				       sizeof(struct mfc6_cache),
				       0, SLAB_HWCACHE_ALIGN,
				       NULL);
	if (!mrt_cachep)
		return -ENOMEM;

	err = register_pernet_subsys(&ip6mr_net_ops);
	if (err)
		goto reg_pernet_fail;

	err = register_netdevice_notifier(&ip6_mr_notifier);
	if (err)
		goto reg_notif_fail;
#ifdef CONFIG_IPV6_PIMSM_V2
	if (inet6_add_protocol(&pim6_protocol, IPPROTO_PIM) < 0) {
		pr_err("%s: can't add PIM protocol\n", __func__);
		err = -EAGAIN;
		goto add_proto_fail;
	}
#endif
	err = rtnl_register_module(THIS_MODULE, RTNL_FAMILY_IP6MR, RTM_GETROUTE,
				   ip6mr_rtm_getroute, ip6mr_rtm_dumproute, 0);
	if (err == 0)
		return 0;

#ifdef CONFIG_IPV6_PIMSM_V2
	inet6_del_protocol(&pim6_protocol, IPPROTO_PIM);
add_proto_fail:
	unregister_netdevice_notifier(&ip6_mr_notifier);
#endif
reg_notif_fail:
	unregister_pernet_subsys(&ip6mr_net_ops);
reg_pernet_fail:
	kmem_cache_destroy(mrt_cachep);
	return err;
}

void ip6_mr_cleanup(void)
{
	rtnl_unregister(RTNL_FAMILY_IP6MR, RTM_GETROUTE);
#ifdef CONFIG_IPV6_PIMSM_V2
	inet6_del_protocol(&pim6_protocol, IPPROTO_PIM);
#endif
	unregister_netdevice_notifier(&ip6_mr_notifier);
	unregister_pernet_subsys(&ip6mr_net_ops);
	kmem_cache_destroy(mrt_cachep);
}

static int ip6mr_mfc_add(struct net *net, struct mr_table *mrt,
			 struct mf6cctl *mfc, int mrtsock, int parent)
{
	unsigned char ttls[MAXMIFS];
	struct mfc6_cache *uc, *c;
	struct mr_mfc *_uc;
	bool found;
	int i, err;

	if (mfc->mf6cc_parent >= MAXMIFS)
		return -ENFILE;

	memset(ttls, 255, MAXMIFS);
	for (i = 0; i < MAXMIFS; i++) {
		if (IF_ISSET(i, &mfc->mf6cc_ifset))
			ttls[i] = 1;
	}

	/* The entries are added/deleted only under RTNL */
	rcu_read_lock();
	c = ip6mr_cache_find_parent(mrt, &mfc->mf6cc_origin.sin6_addr,
				    &mfc->mf6cc_mcastgrp.sin6_addr, parent);
	rcu_read_unlock();
	if (c) {
		spin_lock(&mrt_lock);
		c->_c.mfc_parent = mfc->mf6cc_parent;
		ip6mr_update_thresholds(mrt, &c->_c, ttls);
		if (!mrtsock)
			c->_c.mfc_flags |= MFC_STATIC;
		spin_unlock(&mrt_lock);
		call_ip6mr_mfc_entry_notifiers(net, FIB_EVENT_ENTRY_REPLACE,
					       c, mrt->id);
		mr6_netlink_event(mrt, c, RTM_NEWROUTE);
		return 0;
	}

	if (!ipv6_addr_any(&mfc->mf6cc_mcastgrp.sin6_addr) &&
	    !ipv6_addr_is_multicast(&mfc->mf6cc_mcastgrp.sin6_addr))
		return -EINVAL;

	c = ip6mr_cache_alloc();
	if (!c)
		return -ENOMEM;

	c->mf6c_origin = mfc->mf6cc_origin.sin6_addr;
	c->mf6c_mcastgrp = mfc->mf6cc_mcastgrp.sin6_addr;
	c->_c.mfc_parent = mfc->mf6cc_parent;
	ip6mr_update_thresholds(mrt, &c->_c, ttls);
	if (!mrtsock)
		c->_c.mfc_flags |= MFC_STATIC;

	err = rhltable_insert_key(&mrt->mfc_hash, &c->cmparg, &c->_c.mnode,
				  ip6mr_rht_params);
	if (err) {
		pr_err("ip6mr: rhtable insert error %d\n", err);
		ip6mr_cache_free(c);
		return err;
	}
	list_add_tail_rcu(&c->_c.list, &mrt->mfc_cache_list);

	/* Check to see if we resolved a queued list. If so we
	 * need to send on the frames and tidy up.
	 */
	found = false;
	spin_lock_bh(&mfc_unres_lock);
	list_for_each_entry(_uc, &mrt->mfc_unres_queue, list) {
		uc = (struct mfc6_cache *)_uc;
		if (ipv6_addr_equal(&uc->mf6c_origin, &c->mf6c_origin) &&
		    ipv6_addr_equal(&uc->mf6c_mcastgrp, &c->mf6c_mcastgrp)) {
			list_del(&_uc->list);
			atomic_dec(&mrt->cache_resolve_queue_len);
			found = true;
			break;
		}
	}
	if (list_empty(&mrt->mfc_unres_queue))
		del_timer(&mrt->ipmr_expire_timer);
	spin_unlock_bh(&mfc_unres_lock);

	if (found) {
		ip6mr_cache_resolve(net, mrt, uc, c);
		ip6mr_cache_free(uc);
	}
	call_ip6mr_mfc_entry_notifiers(net, FIB_EVENT_ENTRY_ADD,
				       c, mrt->id);
	mr6_netlink_event(mrt, c, RTM_NEWROUTE);
	return 0;
}

/*
 *	Close the multicast socket, and clear the vif tables etc
 */

static void mroute_clean_tables(struct mr_table *mrt, int flags)
{
	struct mr_mfc *c, *tmp;
	LIST_HEAD(list);
	int i;

	/* Shut down all active vif entries */
	if (flags & (MRT6_FLUSH_MIFS | MRT6_FLUSH_MIFS_STATIC)) {
		for (i = 0; i < mrt->maxvif; i++) {
			if (((mrt->vif_table[i].flags & VIFF_STATIC) &&
			     !(flags & MRT6_FLUSH_MIFS_STATIC)) ||
			    (!(mrt->vif_table[i].flags & VIFF_STATIC) && !(flags & MRT6_FLUSH_MIFS)))
				continue;
			mif6_delete(mrt, i, 0, &list);
		}
		unregister_netdevice_many(&list);
	}

	/* Wipe the cache */
	if (flags & (MRT6_FLUSH_MFC | MRT6_FLUSH_MFC_STATIC)) {
		list_for_each_entry_safe(c, tmp, &mrt->mfc_cache_list, list) {
			if (((c->mfc_flags & MFC_STATIC) && !(flags & MRT6_FLUSH_MFC_STATIC)) ||
			    (!(c->mfc_flags & MFC_STATIC) && !(flags & MRT6_FLUSH_MFC)))
				continue;
			rhltable_remove(&mrt->mfc_hash, &c->mnode, ip6mr_rht_params);
			list_del_rcu(&c->list);
			call_ip6mr_mfc_entry_notifiers(read_pnet(&mrt->net),
						       FIB_EVENT_ENTRY_DEL,
						       (struct mfc6_cache *)c, mrt->id);
			mr6_netlink_event(mrt, (struct mfc6_cache *)c, RTM_DELROUTE);
			mr_cache_put(c);
		}
	}

	if (flags & MRT6_FLUSH_MFC) {
		if (atomic_read(&mrt->cache_resolve_queue_len) != 0) {
			spin_lock_bh(&mfc_unres_lock);
			list_for_each_entry_safe(c, tmp, &mrt->mfc_unres_queue, list) {
				list_del(&c->list);
				mr6_netlink_event(mrt, (struct mfc6_cache *)c,
						  RTM_DELROUTE);
				ip6mr_destroy_unres(mrt, (struct mfc6_cache *)c);
			}
			spin_unlock_bh(&mfc_unres_lock);
		}
	}
}

static int ip6mr_sk_init(struct mr_table *mrt, struct sock *sk)
{
	int err = 0;
	struct net *net = sock_net(sk);

	rtnl_lock();
	spin_lock(&mrt_lock);
	if (rtnl_dereference(mrt->mroute_sk)) {
		err = -EADDRINUSE;
	} else {
		rcu_assign_pointer(mrt->mroute_sk, sk);
		sock_set_flag(sk, SOCK_RCU_FREE);
		atomic_inc(&net->ipv6.devconf_all->mc_forwarding);
	}
	spin_unlock(&mrt_lock);

	if (!err)
		inet6_netconf_notify_devconf(net, RTM_NEWNETCONF,
					     NETCONFA_MC_FORWARDING,
					     NETCONFA_IFINDEX_ALL,
					     net->ipv6.devconf_all);
	rtnl_unlock();

	return err;
}

int ip6mr_sk_done(struct sock *sk)
{
	struct net *net = sock_net(sk);
	struct ipv6_devconf *devconf;
	struct mr_table *mrt;
	int err = -EACCES;

	if (sk->sk_type != SOCK_RAW ||
	    inet_sk(sk)->inet_num != IPPROTO_ICMPV6)
		return err;

	devconf = net->ipv6.devconf_all;
	if (!devconf || !atomic_read(&devconf->mc_forwarding))
		return err;

	rtnl_lock();
	ip6mr_for_each_table(mrt, net) {
		if (sk == rtnl_dereference(mrt->mroute_sk)) {
			spin_lock(&mrt_lock);
			RCU_INIT_POINTER(mrt->mroute_sk, NULL);
			/* Note that mroute_sk had SOCK_RCU_FREE set,
			 * so the RCU grace period before sk freeing
			 * is guaranteed by sk_destruct()
			 */
			atomic_dec(&devconf->mc_forwarding);
			spin_unlock(&mrt_lock);
			inet6_netconf_notify_devconf(net, RTM_NEWNETCONF,
						     NETCONFA_MC_FORWARDING,
						     NETCONFA_IFINDEX_ALL,
						     net->ipv6.devconf_all);

			mroute_clean_tables(mrt, MRT6_FLUSH_MIFS | MRT6_FLUSH_MFC);
			err = 0;
			break;
		}
	}
	rtnl_unlock();

	return err;
}

bool mroute6_is_socket(struct net *net, struct sk_buff *skb)
{
	struct mr_table *mrt;
	struct flowi6 fl6 = {
		.flowi6_iif	= skb->skb_iif ? : LOOPBACK_IFINDEX,
		.flowi6_oif	= skb->dev->ifindex,
		.flowi6_mark	= skb->mark,
	};

	if (ip6mr_fib_lookup(net, &fl6, &mrt) < 0)
		return NULL;

	return rcu_access_pointer(mrt->mroute_sk);
}
EXPORT_SYMBOL(mroute6_is_socket);

/*
 *	Socket options and virtual interface manipulation. The whole
 *	virtual interface system is a complete heap, but unfortunately
 *	that's how BSD mrouted happens to think. Maybe one day with a proper
 *	MOSPF/PIM router set up we can clean this up.
 */

int ip6_mroute_setsockopt(struct sock *sk, int optname, sockptr_t optval,
			  unsigned int optlen)
{
	int ret, parent = 0;
	struct mif6ctl vif;
	struct mf6cctl mfc;
	mifi_t mifi;
	struct net *net = sock_net(sk);
	struct mr_table *mrt;

	if (sk->sk_type != SOCK_RAW ||
	    inet_sk(sk)->inet_num != IPPROTO_ICMPV6)
		return -EOPNOTSUPP;

	mrt = ip6mr_get_table(net, raw6_sk(sk)->ip6mr_table ? : RT6_TABLE_DFLT);
	if (!mrt)
		return -ENOENT;

	if (optname != MRT6_INIT) {
		if (sk != rcu_access_pointer(mrt->mroute_sk) &&
		    !ns_capable(net->user_ns, CAP_NET_ADMIN))
			return -EACCES;
	}

	switch (optname) {
	case MRT6_INIT:
		if (optlen < sizeof(int))
			return -EINVAL;

		return ip6mr_sk_init(mrt, sk);

	case MRT6_DONE:
		return ip6mr_sk_done(sk);

	case MRT6_ADD_MIF:
		if (optlen < sizeof(vif))
			return -EINVAL;
		if (copy_from_sockptr(&vif, optval, sizeof(vif)))
			return -EFAULT;
		if (vif.mif6c_mifi >= MAXMIFS)
			return -ENFILE;
		rtnl_lock();
		ret = mif6_add(net, mrt, &vif,
			       sk == rtnl_dereference(mrt->mroute_sk));
		rtnl_unlock();
		return ret;

	case MRT6_DEL_MIF:
		if (optlen < sizeof(mifi_t))
			return -EINVAL;
		if (copy_from_sockptr(&mifi, optval, sizeof(mifi_t)))
			return -EFAULT;
		rtnl_lock();
		ret = mif6_delete(mrt, mifi, 0, NULL);
		rtnl_unlock();
		return ret;

	/*
	 *	Manipulate the forwarding caches. These live
	 *	in a sort of kernel/user symbiosis.
	 */
	case MRT6_ADD_MFC:
	case MRT6_DEL_MFC:
		parent = -1;
		fallthrough;
	case MRT6_ADD_MFC_PROXY:
	case MRT6_DEL_MFC_PROXY:
		if (optlen < sizeof(mfc))
			return -EINVAL;
		if (copy_from_sockptr(&mfc, optval, sizeof(mfc)))
			return -EFAULT;
		if (parent == 0)
			parent = mfc.mf6cc_parent;
		rtnl_lock();
		if (optname == MRT6_DEL_MFC || optname == MRT6_DEL_MFC_PROXY)
			ret = ip6mr_mfc_delete(mrt, &mfc, parent);
		else
			ret = ip6mr_mfc_add(net, mrt, &mfc,
					    sk ==
					    rtnl_dereference(mrt->mroute_sk),
					    parent);
		rtnl_unlock();
		return ret;

	case MRT6_FLUSH:
	{
		int flags;

		if (optlen != sizeof(flags))
			return -EINVAL;
		if (copy_from_sockptr(&flags, optval, sizeof(flags)))
			return -EFAULT;
		rtnl_lock();
		mroute_clean_tables(mrt, flags);
		rtnl_unlock();
		return 0;
	}

	/*
	 *	Control PIM assert (to activate pim will activate assert)
	 */
	case MRT6_ASSERT:
	{
		int v;

		if (optlen != sizeof(v))
			return -EINVAL;
		if (copy_from_sockptr(&v, optval, sizeof(v)))
			return -EFAULT;
		mrt->mroute_do_assert = v;
		return 0;
	}

#ifdef CONFIG_IPV6_PIMSM_V2
	case MRT6_PIM:
	{
		bool do_wrmifwhole;
		int v;

		if (optlen != sizeof(v))
			return -EINVAL;
		if (copy_from_sockptr(&v, optval, sizeof(v)))
			return -EFAULT;

		do_wrmifwhole = (v == MRT6MSG_WRMIFWHOLE);
		v = !!v;
		rtnl_lock();
		ret = 0;
		if (v != mrt->mroute_do_pim) {
			mrt->mroute_do_pim = v;
			mrt->mroute_do_assert = v;
			mrt->mroute_do_wrvifwhole = do_wrmifwhole;
		}
		rtnl_unlock();
		return ret;
	}

#endif
#ifdef CONFIG_IPV6_MROUTE_MULTIPLE_TABLES
	case MRT6_TABLE:
	{
		u32 v;

		if (optlen != sizeof(u32))
			return -EINVAL;
		if (copy_from_sockptr(&v, optval, sizeof(v)))
			return -EFAULT;
		/* "pim6reg%u" should not exceed 16 bytes (IFNAMSIZ) */
		if (v != RT_TABLE_DEFAULT && v >= 100000000)
			return -EINVAL;
		if (sk == rcu_access_pointer(mrt->mroute_sk))
			return -EBUSY;

		rtnl_lock();
		ret = 0;
		mrt = ip6mr_new_table(net, v);
		if (IS_ERR(mrt))
			ret = PTR_ERR(mrt);
		else
			raw6_sk(sk)->ip6mr_table = v;
		rtnl_unlock();
		return ret;
	}
#endif
	/*
	 *	Spurious command, or MRT6_VERSION which you cannot
	 *	set.
	 */
	default:
		return -ENOPROTOOPT;
	}
}

/*
 *	Getsock opt support for the multicast routing system.
 */

int ip6_mroute_getsockopt(struct sock *sk, int optname, sockptr_t optval,
			  sockptr_t optlen)
{
	int olr;
	int val;
	struct net *net = sock_net(sk);
	struct mr_table *mrt;

	if (sk->sk_type != SOCK_RAW ||
	    inet_sk(sk)->inet_num != IPPROTO_ICMPV6)
		return -EOPNOTSUPP;

	mrt = ip6mr_get_table(net, raw6_sk(sk)->ip6mr_table ? : RT6_TABLE_DFLT);
	if (!mrt)
		return -ENOENT;

	switch (optname) {
	case MRT6_VERSION:
		val = 0x0305;
		break;
#ifdef CONFIG_IPV6_PIMSM_V2
	case MRT6_PIM:
		val = mrt->mroute_do_pim;
		break;
#endif
	case MRT6_ASSERT:
		val = mrt->mroute_do_assert;
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (copy_from_sockptr(&olr, optlen, sizeof(int)))
		return -EFAULT;

	olr = min_t(int, olr, sizeof(int));
	if (olr < 0)
		return -EINVAL;

	if (copy_to_sockptr(optlen, &olr, sizeof(int)))
		return -EFAULT;
	if (copy_to_sockptr(optval, &val, olr))
		return -EFAULT;
	return 0;
}

/*
 *	The IP multicast ioctl support routines.
 */

int ip6mr_ioctl(struct sock *sk, int cmd, void __user *arg)
{
	struct sioc_sg_req6 sr;
	struct sioc_mif_req6 vr;
	struct vif_device *vif;
	struct mfc6_cache *c;
	struct net *net = sock_net(sk);
	struct mr_table *mrt;

	mrt = ip6mr_get_table(net, raw6_sk(sk)->ip6mr_table ? : RT6_TABLE_DFLT);
	if (!mrt)
		return -ENOENT;

	switch (cmd) {
	case SIOCGETMIFCNT_IN6:
		if (copy_from_user(&vr, arg, sizeof(vr)))
			return -EFAULT;
		if (vr.mifi >= mrt->maxvif)
			return -EINVAL;
		vr.mifi = array_index_nospec(vr.mifi, mrt->maxvif);
		rcu_read_lock();
		vif = &mrt->vif_table[vr.mifi];
		if (VIF_EXISTS(mrt, vr.mifi)) {
			vr.icount = READ_ONCE(vif->pkt_in);
			vr.ocount = READ_ONCE(vif->pkt_out);
			vr.ibytes = READ_ONCE(vif->bytes_in);
			vr.obytes = READ_ONCE(vif->bytes_out);
			rcu_read_unlock();

			if (copy_to_user(arg, &vr, sizeof(vr)))
				return -EFAULT;
			return 0;
		}
		rcu_read_unlock();
		return -EADDRNOTAVAIL;
	case SIOCGETSGCNT_IN6:
		if (copy_from_user(&sr, arg, sizeof(sr)))
			return -EFAULT;

		rcu_read_lock();
		c = ip6mr_cache_find(mrt, &sr.src.sin6_addr, &sr.grp.sin6_addr);
		if (c) {
			sr.pktcnt = c->_c.mfc_un.res.pkt;
			sr.bytecnt = c->_c.mfc_un.res.bytes;
			sr.wrong_if = c->_c.mfc_un.res.wrong_if;
			rcu_read_unlock();

			if (copy_to_user(arg, &sr, sizeof(sr)))
				return -EFAULT;
			return 0;
		}
		rcu_read_unlock();
		return -EADDRNOTAVAIL;
	default:
		return -ENOIOCTLCMD;
	}
}

#ifdef CONFIG_COMPAT
struct compat_sioc_sg_req6 {
	struct sockaddr_in6 src;
	struct sockaddr_in6 grp;
	compat_ulong_t pktcnt;
	compat_ulong_t bytecnt;
	compat_ulong_t wrong_if;
};

struct compat_sioc_mif_req6 {
	mifi_t	mifi;
	compat_ulong_t icount;
	compat_ulong_t ocount;
	compat_ulong_t ibytes;
	compat_ulong_t obytes;
};

int ip6mr_compat_ioctl(struct sock *sk, unsigned int cmd, void __user *arg)
{
	struct compat_sioc_sg_req6 sr;
	struct compat_sioc_mif_req6 vr;
	struct vif_device *vif;
	struct mfc6_cache *c;
	struct net *net = sock_net(sk);
	struct mr_table *mrt;

	mrt = ip6mr_get_table(net, raw6_sk(sk)->ip6mr_table ? : RT6_TABLE_DFLT);
	if (!mrt)
		return -ENOENT;

	switch (cmd) {
	case SIOCGETMIFCNT_IN6:
		if (copy_from_user(&vr, arg, sizeof(vr)))
			return -EFAULT;
		if (vr.mifi >= mrt->maxvif)
			return -EINVAL;
		vr.mifi = array_index_nospec(vr.mifi, mrt->maxvif);
		rcu_read_lock();
		vif = &mrt->vif_table[vr.mifi];
		if (VIF_EXISTS(mrt, vr.mifi)) {
			vr.icount = READ_ONCE(vif->pkt_in);
			vr.ocount = READ_ONCE(vif->pkt_out);
			vr.ibytes = READ_ONCE(vif->bytes_in);
			vr.obytes = READ_ONCE(vif->bytes_out);
			rcu_read_unlock();

			if (copy_to_user(arg, &vr, sizeof(vr)))
				return -EFAULT;
			return 0;
		}
		rcu_read_unlock();
		return -EADDRNOTAVAIL;
	case SIOCGETSGCNT_IN6:
		if (copy_from_user(&sr, arg, sizeof(sr)))
			return -EFAULT;

		rcu_read_lock();
		c = ip6mr_cache_find(mrt, &sr.src.sin6_addr, &sr.grp.sin6_addr);
		if (c) {
			sr.pktcnt = c->_c.mfc_un.res.pkt;
			sr.bytecnt = c->_c.mfc_un.res.bytes;
			sr.wrong_if = c->_c.mfc_un.res.wrong_if;
			rcu_read_unlock();

			if (copy_to_user(arg, &sr, sizeof(sr)))
				return -EFAULT;
			return 0;
		}
		rcu_read_unlock();
		return -EADDRNOTAVAIL;
	default:
		return -ENOIOCTLCMD;
	}
}
#endif

static inline int ip6mr_forward2_finish(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	IP6_INC_STATS(net, ip6_dst_idev(skb_dst(skb)),
		      IPSTATS_MIB_OUTFORWDATAGRAMS);
	IP6_ADD_STATS(net, ip6_dst_idev(skb_dst(skb)),
		      IPSTATS_MIB_OUTOCTETS, skb->len);
	return dst_output(net, sk, skb);
}

/*
 *	Processing handlers for ip6mr_forward
 */

static int ip6mr_forward2(struct net *net, struct mr_table *mrt,
			  struct sk_buff *skb, int vifi)
{
	struct vif_device *vif = &mrt->vif_table[vifi];
	struct net_device *vif_dev;
	struct ipv6hdr *ipv6h;
	struct dst_entry *dst;
	struct flowi6 fl6;

	vif_dev = vif_dev_read(vif);
	if (!vif_dev)
		goto out_free;

#ifdef CONFIG_IPV6_PIMSM_V2
	if (vif->flags & MIFF_REGISTER) {
		WRITE_ONCE(vif->pkt_out, vif->pkt_out + 1);
		WRITE_ONCE(vif->bytes_out, vif->bytes_out + skb->len);
		vif_dev->stats.tx_bytes += skb->len;
		vif_dev->stats.tx_packets++;
		ip6mr_cache_report(mrt, skb, vifi, MRT6MSG_WHOLEPKT);
		goto out_free;
	}
#endif

	ipv6h = ipv6_hdr(skb);

	fl6 = (struct flowi6) {
		.flowi6_oif = vif->link,
		.daddr = ipv6h->daddr,
	};

	dst = ip6_route_output(net, NULL, &fl6);
	if (dst->error) {
		dst_release(dst);
		goto out_free;
	}

	skb_dst_drop(skb);
	skb_dst_set(skb, dst);

	/*
	 * RFC1584 teaches, that DVMRP/PIM router must deliver packets locally
	 * not only before forwarding, but after forwarding on all output
	 * interfaces. It is clear, if mrouter runs a multicasting
	 * program, it should receive packets not depending to what interface
	 * program is joined.
	 * If we will not make it, the program will have to join on all
	 * interfaces. On the other hand, multihoming host (or router, but
	 * not mrouter) cannot join to more than one interface - it will
	 * result in receiving multiple packets.
	 */
	skb->dev = vif_dev;
	WRITE_ONCE(vif->pkt_out, vif->pkt_out + 1);
	WRITE_ONCE(vif->bytes_out, vif->bytes_out + skb->len);

	/* We are about to write */
	/* XXX: extension headers? */
	if (skb_cow(skb, sizeof(*ipv6h) + LL_RESERVED_SPACE(vif_dev)))
		goto out_free;

	ipv6h = ipv6_hdr(skb);
	ipv6h->hop_limit--;

	IP6CB(skb)->flags |= IP6SKB_FORWARDED;

	return NF_HOOK(NFPROTO_IPV6, NF_INET_FORWARD,
		       net, NULL, skb, skb->dev, vif_dev,
		       ip6mr_forward2_finish);

out_free:
	kfree_skb(skb);
	return 0;
}

/* Called with rcu_read_lock() */
static int ip6mr_find_vif(struct mr_table *mrt, struct net_device *dev)
{
	int ct;

	/* Pairs with WRITE_ONCE() in mif6_delete()/mif6_add() */
	for (ct = READ_ONCE(mrt->maxvif) - 1; ct >= 0; ct--) {
		if (rcu_access_pointer(mrt->vif_table[ct].dev) == dev)
			break;
	}
	return ct;
}

/* Called under rcu_read_lock() */
static void ip6_mr_forward(struct net *net, struct mr_table *mrt,
			   struct net_device *dev, struct sk_buff *skb,
			   struct mfc6_cache *c)
{
	int psend = -1;
	int vif, ct;
	int true_vifi = ip6mr_find_vif(mrt, dev);

	vif = c->_c.mfc_parent;
	c->_c.mfc_un.res.pkt++;
	c->_c.mfc_un.res.bytes += skb->len;
	c->_c.mfc_un.res.lastuse = jiffies;

	if (ipv6_addr_any(&c->mf6c_origin) && true_vifi >= 0) {
		struct mfc6_cache *cache_proxy;

		/* For an (*,G) entry, we only check that the incoming
		 * interface is part of the static tree.
		 */
		cache_proxy = mr_mfc_find_any_parent(mrt, vif);
		if (cache_proxy &&
		    cache_proxy->_c.mfc_un.res.ttls[true_vifi] < 255)
			goto forward;
	}

	/*
	 * Wrong interface: drop packet and (maybe) send PIM assert.
	 */
	if (rcu_access_pointer(mrt->vif_table[vif].dev) != dev) {
		c->_c.mfc_un.res.wrong_if++;

		if (true_vifi >= 0 && mrt->mroute_do_assert &&
		    /* pimsm uses asserts, when switching from RPT to SPT,
		       so that we cannot check that packet arrived on an oif.
		       It is bad, but otherwise we would need to move pretty
		       large chunk of pimd to kernel. Ough... --ANK
		     */
		    (mrt->mroute_do_pim ||
		     c->_c.mfc_un.res.ttls[true_vifi] < 255) &&
		    time_after(jiffies,
			       c->_c.mfc_un.res.last_assert +
			       MFC_ASSERT_THRESH)) {
			c->_c.mfc_un.res.last_assert = jiffies;
			ip6mr_cache_report(mrt, skb, true_vifi, MRT6MSG_WRONGMIF);
			if (mrt->mroute_do_wrvifwhole)
				ip6mr_cache_report(mrt, skb, true_vifi,
						   MRT6MSG_WRMIFWHOLE);
		}
		goto dont_forward;
	}

forward:
	WRITE_ONCE(mrt->vif_table[vif].pkt_in,
		   mrt->vif_table[vif].pkt_in + 1);
	WRITE_ONCE(mrt->vif_table[vif].bytes_in,
		   mrt->vif_table[vif].bytes_in + skb->len);

	/*
	 *	Forward the frame
	 */
	if (ipv6_addr_any(&c->mf6c_origin) &&
	    ipv6_addr_any(&c->mf6c_mcastgrp)) {
		if (true_vifi >= 0 &&
		    true_vifi != c->_c.mfc_parent &&
		    ipv6_hdr(skb)->hop_limit >
				c->_c.mfc_un.res.ttls[c->_c.mfc_parent]) {
			/* It's an (*,*) entry and the packet is not coming from
			 * the upstream: forward the packet to the upstream
			 * only.
			 */
			psend = c->_c.mfc_parent;
			goto last_forward;
		}
		goto dont_forward;
	}
	for (ct = c->_c.mfc_un.res.maxvif - 1;
	     ct >= c->_c.mfc_un.res.minvif; ct--) {
		/* For (*,G) entry, don't forward to the incoming interface */
		if ((!ipv6_addr_any(&c->mf6c_origin) || ct != true_vifi) &&
		    ipv6_hdr(skb)->hop_limit > c->_c.mfc_un.res.ttls[ct]) {
			if (psend != -1) {
				struct sk_buff *skb2 = skb_clone(skb, GFP_ATOMIC);
				if (skb2)
					ip6mr_forward2(net, mrt, skb2, psend);
			}
			psend = ct;
		}
	}
last_forward:
	if (psend != -1) {
		ip6mr_forward2(net, mrt, skb, psend);
		return;
	}

dont_forward:
	kfree_skb(skb);
}


/*
 *	Multicast packets for forwarding arrive here
 */

int ip6_mr_input(struct sk_buff *skb)
{
	struct mfc6_cache *cache;
	struct net *net = dev_net(skb->dev);
	struct mr_table *mrt;
	struct flowi6 fl6 = {
		.flowi6_iif	= skb->dev->ifindex,
		.flowi6_mark	= skb->mark,
	};
	int err;
	struct net_device *dev;

	/* skb->dev passed in is the master dev for vrfs.
	 * Get the proper interface that does have a vif associated with it.
	 */
	dev = skb->dev;
	if (netif_is_l3_master(skb->dev)) {
		dev = dev_get_by_index_rcu(net, IPCB(skb)->iif);
		if (!dev) {
			kfree_skb(skb);
			return -ENODEV;
		}
	}

	err = ip6mr_fib_lookup(net, &fl6, &mrt);
	if (err < 0) {
		kfree_skb(skb);
		return err;
	}

	cache = ip6mr_cache_find(mrt,
				 &ipv6_hdr(skb)->saddr, &ipv6_hdr(skb)->daddr);
	if (!cache) {
		int vif = ip6mr_find_vif(mrt, dev);

		if (vif >= 0)
			cache = ip6mr_cache_find_any(mrt,
						     &ipv6_hdr(skb)->daddr,
						     vif);
	}

	/*
	 *	No usable cache entry
	 */
	if (!cache) {
		int vif;

		vif = ip6mr_find_vif(mrt, dev);
		if (vif >= 0) {
			int err = ip6mr_cache_unresolved(mrt, vif, skb, dev);

			return err;
		}
		kfree_skb(skb);
		return -ENODEV;
	}

	ip6_mr_forward(net, mrt, dev, skb, cache);

	return 0;
}

int ip6mr_get_route(struct net *net, struct sk_buff *skb, struct rtmsg *rtm,
		    u32 portid)
{
	int err;
	struct mr_table *mrt;
	struct mfc6_cache *cache;
	struct rt6_info *rt = (struct rt6_info *)skb_dst(skb);

	mrt = ip6mr_get_table(net, RT6_TABLE_DFLT);
	if (!mrt)
		return -ENOENT;

	rcu_read_lock();
	cache = ip6mr_cache_find(mrt, &rt->rt6i_src.addr, &rt->rt6i_dst.addr);
	if (!cache && skb->dev) {
		int vif = ip6mr_find_vif(mrt, skb->dev);

		if (vif >= 0)
			cache = ip6mr_cache_find_any(mrt, &rt->rt6i_dst.addr,
						     vif);
	}

	if (!cache) {
		struct sk_buff *skb2;
		struct ipv6hdr *iph;
		struct net_device *dev;
		int vif;

		dev = skb->dev;
		if (!dev || (vif = ip6mr_find_vif(mrt, dev)) < 0) {
			rcu_read_unlock();
			return -ENODEV;
		}

		/* really correct? */
		skb2 = alloc_skb(sizeof(struct ipv6hdr), GFP_ATOMIC);
		if (!skb2) {
			rcu_read_unlock();
			return -ENOMEM;
		}

		NETLINK_CB(skb2).portid = portid;
		skb_reset_transport_header(skb2);

		skb_put(skb2, sizeof(struct ipv6hdr));
		skb_reset_network_header(skb2);

		iph = ipv6_hdr(skb2);
		iph->version = 0;
		iph->priority = 0;
		iph->flow_lbl[0] = 0;
		iph->flow_lbl[1] = 0;
		iph->flow_lbl[2] = 0;
		iph->payload_len = 0;
		iph->nexthdr = IPPROTO_NONE;
		iph->hop_limit = 0;
		iph->saddr = rt->rt6i_src.addr;
		iph->daddr = rt->rt6i_dst.addr;

		err = ip6mr_cache_unresolved(mrt, vif, skb2, dev);
		rcu_read_unlock();

		return err;
	}

	err = mr_fill_mroute(mrt, skb, &cache->_c, rtm);
	rcu_read_unlock();
	return err;
}

static int ip6mr_fill_mroute(struct mr_table *mrt, struct sk_buff *skb,
			     u32 portid, u32 seq, struct mfc6_cache *c, int cmd,
			     int flags)
{
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;
	int err;

	nlh = nlmsg_put(skb, portid, seq, cmd, sizeof(*rtm), flags);
	if (!nlh)
		return -EMSGSIZE;

	rtm = nlmsg_data(nlh);
	rtm->rtm_family   = RTNL_FAMILY_IP6MR;
	rtm->rtm_dst_len  = 128;
	rtm->rtm_src_len  = 128;
	rtm->rtm_tos      = 0;
	rtm->rtm_table    = mrt->id;
	if (nla_put_u32(skb, RTA_TABLE, mrt->id))
		goto nla_put_failure;
	rtm->rtm_type = RTN_MULTICAST;
	rtm->rtm_scope    = RT_SCOPE_UNIVERSE;
	if (c->_c.mfc_flags & MFC_STATIC)
		rtm->rtm_protocol = RTPROT_STATIC;
	else
		rtm->rtm_protocol = RTPROT_MROUTED;
	rtm->rtm_flags    = 0;

	if (nla_put_in6_addr(skb, RTA_SRC, &c->mf6c_origin) ||
	    nla_put_in6_addr(skb, RTA_DST, &c->mf6c_mcastgrp))
		goto nla_put_failure;
	err = mr_fill_mroute(mrt, skb, &c->_c, rtm);
	/* do not break the dump if cache is unresolved */
	if (err < 0 && err != -ENOENT)
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int _ip6mr_fill_mroute(struct mr_table *mrt, struct sk_buff *skb,
			      u32 portid, u32 seq, struct mr_mfc *c,
			      int cmd, int flags)
{
	return ip6mr_fill_mroute(mrt, skb, portid, seq, (struct mfc6_cache *)c,
				 cmd, flags);
}

static int mr6_msgsize(bool unresolved, int maxvif)
{
	size_t len =
		NLMSG_ALIGN(sizeof(struct rtmsg))
		+ nla_total_size(4)	/* RTA_TABLE */
		+ nla_total_size(sizeof(struct in6_addr))	/* RTA_SRC */
		+ nla_total_size(sizeof(struct in6_addr))	/* RTA_DST */
		;

	if (!unresolved)
		len = len
		      + nla_total_size(4)	/* RTA_IIF */
		      + nla_total_size(0)	/* RTA_MULTIPATH */
		      + maxvif * NLA_ALIGN(sizeof(struct rtnexthop))
						/* RTA_MFC_STATS */
		      + nla_total_size_64bit(sizeof(struct rta_mfc_stats))
		;

	return len;
}

static void mr6_netlink_event(struct mr_table *mrt, struct mfc6_cache *mfc,
			      int cmd)
{
	struct net *net = read_pnet(&mrt->net);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(mr6_msgsize(mfc->_c.mfc_parent >= MAXMIFS, mrt->maxvif),
			GFP_ATOMIC);
	if (!skb)
		goto errout;

	err = ip6mr_fill_mroute(mrt, skb, 0, 0, mfc, cmd, 0);
	if (err < 0)
		goto errout;

	rtnl_notify(skb, net, 0, RTNLGRP_IPV6_MROUTE, NULL, GFP_ATOMIC);
	return;

errout:
	kfree_skb(skb);
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_IPV6_MROUTE, err);
}

static size_t mrt6msg_netlink_msgsize(size_t payloadlen)
{
	size_t len =
		NLMSG_ALIGN(sizeof(struct rtgenmsg))
		+ nla_total_size(1)	/* IP6MRA_CREPORT_MSGTYPE */
		+ nla_total_size(4)	/* IP6MRA_CREPORT_MIF_ID */
					/* IP6MRA_CREPORT_SRC_ADDR */
		+ nla_total_size(sizeof(struct in6_addr))
					/* IP6MRA_CREPORT_DST_ADDR */
		+ nla_total_size(sizeof(struct in6_addr))
					/* IP6MRA_CREPORT_PKT */
		+ nla_total_size(payloadlen)
		;

	return len;
}

static void mrt6msg_netlink_event(const struct mr_table *mrt, struct sk_buff *pkt)
{
	struct net *net = read_pnet(&mrt->net);
	struct nlmsghdr *nlh;
	struct rtgenmsg *rtgenm;
	struct mrt6msg *msg;
	struct sk_buff *skb;
	struct nlattr *nla;
	int payloadlen;

	payloadlen = pkt->len - sizeof(struct mrt6msg);
	msg = (struct mrt6msg *)skb_transport_header(pkt);

	skb = nlmsg_new(mrt6msg_netlink_msgsize(payloadlen), GFP_ATOMIC);
	if (!skb)
		goto errout;

	nlh = nlmsg_put(skb, 0, 0, RTM_NEWCACHEREPORT,
			sizeof(struct rtgenmsg), 0);
	if (!nlh)
		goto errout;
	rtgenm = nlmsg_data(nlh);
	rtgenm->rtgen_family = RTNL_FAMILY_IP6MR;
	if (nla_put_u8(skb, IP6MRA_CREPORT_MSGTYPE, msg->im6_msgtype) ||
	    nla_put_u32(skb, IP6MRA_CREPORT_MIF_ID, msg->im6_mif) ||
	    nla_put_in6_addr(skb, IP6MRA_CREPORT_SRC_ADDR,
			     &msg->im6_src) ||
	    nla_put_in6_addr(skb, IP6MRA_CREPORT_DST_ADDR,
			     &msg->im6_dst))
		goto nla_put_failure;

	nla = nla_reserve(skb, IP6MRA_CREPORT_PKT, payloadlen);
	if (!nla || skb_copy_bits(pkt, sizeof(struct mrt6msg),
				  nla_data(nla), payloadlen))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);

	rtnl_notify(skb, net, 0, RTNLGRP_IPV6_MROUTE_R, NULL, GFP_ATOMIC);
	return;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
errout:
	kfree_skb(skb);
	rtnl_set_sk_err(net, RTNLGRP_IPV6_MROUTE_R, -ENOBUFS);
}

static const struct nla_policy ip6mr_getroute_policy[RTA_MAX + 1] = {
	[RTA_SRC]		= NLA_POLICY_EXACT_LEN(sizeof(struct in6_addr)),
	[RTA_DST]		= NLA_POLICY_EXACT_LEN(sizeof(struct in6_addr)),
	[RTA_TABLE]		= { .type = NLA_U32 },
};

static int ip6mr_rtm_valid_getroute_req(struct sk_buff *skb,
					const struct nlmsghdr *nlh,
					struct nlattr **tb,
					struct netlink_ext_ack *extack)
{
	struct rtmsg *rtm;
	int err;

	err = nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX, ip6mr_getroute_policy,
			  extack);
	if (err)
		return err;

	rtm = nlmsg_data(nlh);
	if ((rtm->rtm_src_len && rtm->rtm_src_len != 128) ||
	    (rtm->rtm_dst_len && rtm->rtm_dst_len != 128) ||
	    rtm->rtm_tos || rtm->rtm_table || rtm->rtm_protocol ||
	    rtm->rtm_scope || rtm->rtm_type || rtm->rtm_flags) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid values in header for multicast route get request");
		return -EINVAL;
	}

	if ((tb[RTA_SRC] && !rtm->rtm_src_len) ||
	    (tb[RTA_DST] && !rtm->rtm_dst_len)) {
		NL_SET_ERR_MSG_MOD(extack, "rtm_src_len and rtm_dst_len must be 128 for IPv6");
		return -EINVAL;
	}

	return 0;
}

static int ip6mr_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct in6_addr src = {}, grp = {};
	struct nlattr *tb[RTA_MAX + 1];
	struct mfc6_cache *cache;
	struct mr_table *mrt;
	struct sk_buff *skb;
	u32 tableid;
	int err;

	err = ip6mr_rtm_valid_getroute_req(in_skb, nlh, tb, extack);
	if (err < 0)
		return err;

	if (tb[RTA_SRC])
		src = nla_get_in6_addr(tb[RTA_SRC]);
	if (tb[RTA_DST])
		grp = nla_get_in6_addr(tb[RTA_DST]);
	tableid = tb[RTA_TABLE] ? nla_get_u32(tb[RTA_TABLE]) : 0;

	mrt = ip6mr_get_table(net, tableid ?: RT_TABLE_DEFAULT);
	if (!mrt) {
		NL_SET_ERR_MSG_MOD(extack, "MR table does not exist");
		return -ENOENT;
	}

	/* entries are added/deleted only under RTNL */
	rcu_read_lock();
	cache = ip6mr_cache_find(mrt, &src, &grp);
	rcu_read_unlock();
	if (!cache) {
		NL_SET_ERR_MSG_MOD(extack, "MR cache entry not found");
		return -ENOENT;
	}

	skb = nlmsg_new(mr6_msgsize(false, mrt->maxvif), GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	err = ip6mr_fill_mroute(mrt, skb, NETLINK_CB(in_skb).portid,
				nlh->nlmsg_seq, cache, RTM_NEWROUTE, 0);
	if (err < 0) {
		kfree_skb(skb);
		return err;
	}

	return rtnl_unicast(skb, net, NETLINK_CB(in_skb).portid);
}

static int ip6mr_rtm_dumproute(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = cb->nlh;
	struct fib_dump_filter filter = {};
	int err;

	if (cb->strict_check) {
		err = ip_valid_fib_dump_req(sock_net(skb->sk), nlh,
					    &filter, cb);
		if (err < 0)
			return err;
	}

	if (filter.table_id) {
		struct mr_table *mrt;

		mrt = ip6mr_get_table(sock_net(skb->sk), filter.table_id);
		if (!mrt) {
			if (rtnl_msg_family(cb->nlh) != RTNL_FAMILY_IP6MR)
				return skb->len;

			NL_SET_ERR_MSG_MOD(cb->extack, "MR table does not exist");
			return -ENOENT;
		}
		err = mr_table_dump(mrt, skb, cb, _ip6mr_fill_mroute,
				    &mfc_unres_lock, &filter);
		return skb->len ? : err;
	}

	return mr_rtm_dumproute(skb, cb, ip6mr_mr_table_iter,
				_ip6mr_fill_mroute, &mfc_unres_lock, &filter);
}
