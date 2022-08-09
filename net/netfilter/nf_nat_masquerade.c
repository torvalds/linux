// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/inetdevice.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_ipv6.h>

#include <net/netfilter/nf_nat_masquerade.h>

struct masq_dev_work {
	struct work_struct work;
	struct net *net;
	netns_tracker ns_tracker;
	union nf_inet_addr addr;
	int ifindex;
	int (*iter)(struct nf_conn *i, void *data);
};

#define MAX_MASQ_WORKER_COUNT	16

static DEFINE_MUTEX(masq_mutex);
static unsigned int masq_refcnt __read_mostly;
static atomic_t masq_worker_count __read_mostly;

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

static void iterate_cleanup_work(struct work_struct *work)
{
	struct masq_dev_work *w;

	w = container_of(work, struct masq_dev_work, work);

	nf_ct_iterate_cleanup_net(w->net, w->iter, (void *)w, 0, 0);

	put_net_track(w->net, &w->ns_tracker);
	kfree(w);
	atomic_dec(&masq_worker_count);
	module_put(THIS_MODULE);
}

/* Iterate conntrack table in the background and remove conntrack entries
 * that use the device/address being removed.
 *
 * In case too many work items have been queued already or memory allocation
 * fails iteration is skipped, conntrack entries will time out eventually.
 */
static void nf_nat_masq_schedule(struct net *net, union nf_inet_addr *addr,
				 int ifindex,
				 int (*iter)(struct nf_conn *i, void *data),
				 gfp_t gfp_flags)
{
	struct masq_dev_work *w;

	if (atomic_read(&masq_worker_count) > MAX_MASQ_WORKER_COUNT)
		return;

	net = maybe_get_net(net);
	if (!net)
		return;

	if (!try_module_get(THIS_MODULE))
		goto err_module;

	w = kzalloc(sizeof(*w), gfp_flags);
	if (w) {
		/* We can overshoot MAX_MASQ_WORKER_COUNT, no big deal */
		atomic_inc(&masq_worker_count);

		INIT_WORK(&w->work, iterate_cleanup_work);
		w->ifindex = ifindex;
		w->net = net;
		netns_tracker_alloc(net, &w->ns_tracker, gfp_flags);
		w->iter = iter;
		if (addr)
			w->addr = *addr;
		schedule_work(&w->work);
		return;
	}

	module_put(THIS_MODULE);
 err_module:
	put_net(net);
}

static int device_cmp(struct nf_conn *i, void *arg)
{
	const struct nf_conn_nat *nat = nfct_nat(i);
	const struct masq_dev_work *w = arg;

	if (!nat)
		return 0;
	return nat->masq_index == w->ifindex;
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

		nf_nat_masq_schedule(net, NULL, dev->ifindex,
				     device_cmp, GFP_KERNEL);
	}

	return NOTIFY_DONE;
}

static int inet_cmp(struct nf_conn *ct, void *ptr)
{
	struct nf_conntrack_tuple *tuple;
	struct masq_dev_work *w = ptr;

	if (!device_cmp(ct, ptr))
		return 0;

	tuple = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	return nf_inet_addr_cmp(&w->addr, &tuple->dst.u3);
}

static int masq_inet_event(struct notifier_block *this,
			   unsigned long event,
			   void *ptr)
{
	const struct in_ifaddr *ifa = ptr;
	const struct in_device *idev;
	const struct net_device *dev;
	union nf_inet_addr addr;

	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;

	/* The masq_dev_notifier will catch the case of the device going
	 * down.  So if the inetdev is dead and being destroyed we have
	 * no work to do.  Otherwise this is an individual address removal
	 * and we have to perform the flush.
	 */
	idev = ifa->ifa_dev;
	if (idev->dead)
		return NOTIFY_DONE;

	memset(&addr, 0, sizeof(addr));

	addr.ip = ifa->ifa_address;

	dev = idev->dev;
	nf_nat_masq_schedule(dev_net(idev->dev), &addr, dev->ifindex,
			     inet_cmp, GFP_KERNEL);

	return NOTIFY_DONE;
}

static struct notifier_block masq_dev_notifier = {
	.notifier_call	= masq_device_event,
};

static struct notifier_block masq_inet_notifier = {
	.notifier_call	= masq_inet_event,
};

#if IS_ENABLED(CONFIG_IPV6)
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
	union nf_inet_addr addr;

	if (event != NETDEV_DOWN)
		return NOTIFY_DONE;

	dev = ifa->idev->dev;

	memset(&addr, 0, sizeof(addr));

	addr.in6 = ifa->addr;

	nf_nat_masq_schedule(dev_net(dev), &addr, dev->ifindex, inet_cmp,
			     GFP_ATOMIC);
	return NOTIFY_DONE;
}

static struct notifier_block masq_inet6_notifier = {
	.notifier_call	= masq_inet6_event,
};

static int nf_nat_masquerade_ipv6_register_notifier(void)
{
	return register_inet6addr_notifier(&masq_inet6_notifier);
}
#else
static inline int nf_nat_masquerade_ipv6_register_notifier(void) { return 0; }
#endif

int nf_nat_masquerade_inet_register_notifiers(void)
{
	int ret = 0;

	mutex_lock(&masq_mutex);
	if (WARN_ON_ONCE(masq_refcnt == UINT_MAX)) {
		ret = -EOVERFLOW;
		goto out_unlock;
	}

	/* check if the notifier was already set */
	if (++masq_refcnt > 1)
		goto out_unlock;

	/* Register for device down reports */
	ret = register_netdevice_notifier(&masq_dev_notifier);
	if (ret)
		goto err_dec;
	/* Register IP address change reports */
	ret = register_inetaddr_notifier(&masq_inet_notifier);
	if (ret)
		goto err_unregister;

	ret = nf_nat_masquerade_ipv6_register_notifier();
	if (ret)
		goto err_unreg_inet;

	mutex_unlock(&masq_mutex);
	return ret;
err_unreg_inet:
	unregister_inetaddr_notifier(&masq_inet_notifier);
err_unregister:
	unregister_netdevice_notifier(&masq_dev_notifier);
err_dec:
	masq_refcnt--;
out_unlock:
	mutex_unlock(&masq_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_inet_register_notifiers);

void nf_nat_masquerade_inet_unregister_notifiers(void)
{
	mutex_lock(&masq_mutex);
	/* check if the notifiers still have clients */
	if (--masq_refcnt > 0)
		goto out_unlock;

	unregister_netdevice_notifier(&masq_dev_notifier);
	unregister_inetaddr_notifier(&masq_inet_notifier);
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&masq_inet6_notifier);
#endif
out_unlock:
	mutex_unlock(&masq_mutex);
}
EXPORT_SYMBOL_GPL(nf_nat_masquerade_inet_unregister_notifiers);
