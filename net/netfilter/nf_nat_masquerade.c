// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/inetdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>

#include <net/netfilter/ipv4/nf_nat_masquerade.h>
#include <net/netfilter/ipv6/nf_nat_masquerade.h>

static DEFINE_MUTEX(masq_mutex);
static unsigned int masq_refcnt4 __read_mostly;
static unsigned int masq_refcnt6 __read_mostly;

unsigned int
nf_nat_masquerade_ipv4(struct sk_buff *skb, unsigned int hooknum,
		       const struct nf_nat_range2 *range,
		       const struct net_device *out)
{
	struct nf_conn *ct;
	struct nf_conn_nat *nat;
	enum ip_conntrack_info ctinfo;
	struct nf_nat_range2 newrange;
	const struct rtable *rt;
	__be32 newsrc, nh;

	WARN_ON(hooknum != NF_INET_POST_ROUTING);

	ct = nf_ct_get(skb, &ctinfo);

	WARN_ON(!(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED ||
			 ctinfo == IP_CT_RELATED_REPLY)));

	/* Source address is 0.0.0.0 - locally generated packet that is
	 * probably not supposed to be masqueraded.
	 */
	if (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3.ip == 0)
		return NF_ACCEPT;

	rt = skb_rtable(skb);
	nh = rt_nexthop(rt, ip_hdr(skb)->daddr);
	newsrc = inet_select_addr(out, nh, RT_SCOPE_UNIVERSE);
	if (!newsrc) {
		pr_info("%s ate my IP address\n", out->name);
		return NF_DROP;
	}

	nat = nf_ct_nat_ext_add(ct);
	if (nat)
		nat->masq_index = out->ifindex;

	/* Transfer from original range. */
	memset(&newrange.min_addr, 0, sizeof(newrange.min_addr));
	memset(&newrange.max_addr, 0, sizeof(newrange.max_addr));
	newrange.flags       = range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.ip = newsrc;
	newrange.max_addr.ip = newsrc;
	newrange.min_proto   = range->min_proto;
	newrange.max_proto   = range->max_proto;

	/* Hand modified range to generic setup. */
	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_SRC);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv4);

static int device_cmp(struct nf_conn *i, void *ifindex)
{
	const struct nf_conn_nat *nat = nfct_nat(i);

	if (!nat)
		return 0;
	return nat->masq_index == (int)(long)ifindex;
}

static int masq_device_event(struct notifier_block *this,
			     unsigned long event,
			     void *ptr)
{
	const struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	if (event == NETDEV_DOWN) {
		/* Device was downed.  Search entire table for
		 * conntracks which were associated with that device,
		 * and forget them.
		 */

		nf_ct_iterate_cleanup_net(net, device_cmp,
					  (void *)(long)dev->ifindex, 0, 0);
	}

	return NOTIFY_DONE;
}

static int inet_cmp(struct nf_conn *ct, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct nf_conntrack_tuple *tuple;

	if (!device_cmp(ct, (void *)(long)dev->ifindex))
		return 0;

	tuple = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	return ifa->ifa_address == tuple->dst.u3.ip;
}

static int masq_inet_event(struct notifier_block *this,
			   unsigned long event,
			   void *ptr)
{
	struct in_device *idev = ((struct in_ifaddr *)ptr)->ifa_dev;
	struct net *net = dev_net(idev->dev);

	/* The masq_dev_notifier will catch the case of the device going
	 * down.  So if the inetdev is dead and being destroyed we have
	 * no work to do.  Otherwise this is an individual address removal
	 * and we have to perform the flush.
	 */
	if (idev->dead)
		return NOTIFY_DONE;

	if (event == NETDEV_DOWN)
		nf_ct_iterate_cleanup_net(net, inet_cmp, ptr, 0, 0);

	return NOTIFY_DONE;
}

static struct notifier_block masq_dev_notifier = {
	.notifier_call	= masq_device_event,
};

static struct notifier_block masq_inet_notifier = {
	.notifier_call	= masq_inet_event,
};

int nf_nat_masquerade_ipv4_register_notifier(void)
{
	int ret = 0;

	mutex_lock(&masq_mutex);
	if (WARN_ON_ONCE(masq_refcnt4 == UINT_MAX)) {
		ret = -EOVERFLOW;
		goto out_unlock;
	}

	/* check if the notifier was already set */
	if (++masq_refcnt4 > 1)
		goto out_unlock;

	/* Register for device down reports */
	ret = register_netdevice_notifier(&masq_dev_notifier);
	if (ret)
		goto err_dec;
	/* Register IP address change reports */
	ret = register_inetaddr_notifier(&masq_inet_notifier);
	if (ret)
		goto err_unregister;

	mutex_unlock(&masq_mutex);
	return ret;

err_unregister:
	unregister_netdevice_notifier(&masq_dev_notifier);
err_dec:
	masq_refcnt4--;
out_unlock:
	mutex_unlock(&masq_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv4_register_notifier);

void nf_nat_masquerade_ipv4_unregister_notifier(void)
{
	mutex_lock(&masq_mutex);
	/* check if the notifier still has clients */
	if (--masq_refcnt4 > 0)
		goto out_unlock;

	unregister_netdevice_notifier(&masq_dev_notifier);
	unregister_inetaddr_notifier(&masq_inet_notifier);
out_unlock:
	mutex_unlock(&masq_mutex);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv4_unregister_notifier);

#if IS_ENABLED(CONFIG_IPV6)
static atomic_t v6_worker_count __read_mostly;

static int
nat_ipv6_dev_get_saddr(struct net *net, const struct net_device *dev,
		       const struct in6_addr *daddr, unsigned int srcprefs,
		       struct in6_addr *saddr)
{
#ifdef CONFIG_IPV6_MODULE
	const struct nf_ipv6_ops *v6_ops = nf_get_ipv6_ops();

	if (!v6_ops)
		return -EHOSTUNREACH;

	return v6_ops->dev_get_saddr(net, dev, daddr, srcprefs, saddr);
#else
	return ipv6_dev_get_saddr(net, dev, daddr, srcprefs, saddr);
#endif
}

unsigned int
nf_nat_masquerade_ipv6(struct sk_buff *skb, const struct nf_nat_range2 *range,
		       const struct net_device *out)
{
	enum ip_conntrack_info ctinfo;
	struct nf_conn_nat *nat;
	struct in6_addr src;
	struct nf_conn *ct;
	struct nf_nat_range2 newrange;

	ct = nf_ct_get(skb, &ctinfo);
	WARN_ON(!(ct && (ctinfo == IP_CT_NEW || ctinfo == IP_CT_RELATED ||
			 ctinfo == IP_CT_RELATED_REPLY)));

	if (nat_ipv6_dev_get_saddr(nf_ct_net(ct), out,
				   &ipv6_hdr(skb)->daddr, 0, &src) < 0)
		return NF_DROP;

	nat = nf_ct_nat_ext_add(ct);
	if (nat)
		nat->masq_index = out->ifindex;

	newrange.flags		= range->flags | NF_NAT_RANGE_MAP_IPS;
	newrange.min_addr.in6	= src;
	newrange.max_addr.in6	= src;
	newrange.min_proto	= range->min_proto;
	newrange.max_proto	= range->max_proto;

	return nf_nat_setup_info(ct, &newrange, NF_NAT_MANIP_SRC);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6);

struct masq_dev_work {
	struct work_struct work;
	struct net *net;
	struct in6_addr addr;
	int ifindex;
};

static int inet6_cmp(struct nf_conn *ct, void *work)
{
	struct masq_dev_work *w = (struct masq_dev_work *)work;
	struct nf_conntrack_tuple *tuple;

	if (!device_cmp(ct, (void *)(long)w->ifindex))
		return 0;

	tuple = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	return ipv6_addr_equal(&w->addr, &tuple->dst.u3.in6);
}

static void iterate_cleanup_work(struct work_struct *work)
{
	struct masq_dev_work *w;

	w = container_of(work, struct masq_dev_work, work);

	nf_ct_iterate_cleanup_net(w->net, inet6_cmp, (void *)w, 0, 0);

	put_net(w->net);
	kfree(w);
	atomic_dec(&v6_worker_count);
	module_put(THIS_MODULE);
}

/* atomic notifier; can't call nf_ct_iterate_cleanup_net (it can sleep).
 *
 * Defer it to the system workqueue.
 *
 * As we can have 'a lot' of inet_events (depending on amount of ipv6
 * addresses being deleted), we also need to limit work item queue.
 */
static int masq_inet6_event(struct notifier_block *this,
			    unsigned long event, void *ptr)
{
	struct inet6_ifaddr *ifa = ptr;
	const struct net_device *dev;
	struct masq_dev_work *w;
	struct net *net;

	if (event != NETDEV_DOWN || atomic_read(&v6_worker_count) >= 16)
		return NOTIFY_DONE;

	dev = ifa->idev->dev;
	net = maybe_get_net(dev_net(dev));
	if (!net)
		return NOTIFY_DONE;

	if (!try_module_get(THIS_MODULE))
		goto err_module;

	w = kmalloc(sizeof(*w), GFP_ATOMIC);
	if (w) {
		atomic_inc(&v6_worker_count);

		INIT_WORK(&w->work, iterate_cleanup_work);
		w->ifindex = dev->ifindex;
		w->net = net;
		w->addr = ifa->addr;
		schedule_work(&w->work);

		return NOTIFY_DONE;
	}

	module_put(THIS_MODULE);
 err_module:
	put_net(net);
	return NOTIFY_DONE;
}

static struct notifier_block masq_inet6_notifier = {
	.notifier_call	= masq_inet6_event,
};

int nf_nat_masquerade_ipv6_register_notifier(void)
{
	int ret = 0;

	mutex_lock(&masq_mutex);
	if (WARN_ON_ONCE(masq_refcnt6 == UINT_MAX)) {
		ret = -EOVERFLOW;
		goto out_unlock;
	}

	/* check if the notifier is already set */
	if (++masq_refcnt6 > 1)
		goto out_unlock;

	ret = register_inet6addr_notifier(&masq_inet6_notifier);
	if (ret)
		goto err_dec;

	mutex_unlock(&masq_mutex);
	return ret;
err_dec:
	masq_refcnt6--;
out_unlock:
	mutex_unlock(&masq_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6_register_notifier);

void nf_nat_masquerade_ipv6_unregister_notifier(void)
{
	mutex_lock(&masq_mutex);
	/* check if the notifier still has clients */
	if (--masq_refcnt6 > 0)
		goto out_unlock;

	unregister_inet6addr_notifier(&masq_inet6_notifier);
out_unlock:
	mutex_unlock(&masq_mutex);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_ipv6_unregister_notifier);
#endif
