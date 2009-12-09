/* Event cache for netfilter. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2003,2004 USAGI/WIDE Project <http://www.linux-ipv6.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/percpu.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>

static DEFINE_MUTEX(nf_ct_ecache_mutex);

struct nf_ct_event_notifier *nf_conntrack_event_cb __read_mostly;
EXPORT_SYMBOL_GPL(nf_conntrack_event_cb);

struct nf_exp_event_notifier *nf_expect_event_cb __read_mostly;
EXPORT_SYMBOL_GPL(nf_expect_event_cb);

/* deliver cached events and clear cache entry - must be called with locally
 * disabled softirqs */
void nf_ct_deliver_cached_events(struct nf_conn *ct)
{
	unsigned long events;
	struct nf_ct_event_notifier *notify;
	struct nf_conntrack_ecache *e;

	rcu_read_lock();
	notify = rcu_dereference(nf_conntrack_event_cb);
	if (notify == NULL)
		goto out_unlock;

	e = nf_ct_ecache_find(ct);
	if (e == NULL)
		goto out_unlock;

	events = xchg(&e->cache, 0);

	if (nf_ct_is_confirmed(ct) && !nf_ct_is_dying(ct) && events) {
		struct nf_ct_event item = {
			.ct	= ct,
			.pid	= 0,
			.report	= 0
		};
		int ret;
		/* We make a copy of the missed event cache without taking
		 * the lock, thus we may send missed events twice. However,
		 * this does not harm and it happens very rarely. */
		unsigned long missed = e->missed;

		ret = notify->fcn(events | missed, &item);
		if (unlikely(ret < 0 || missed)) {
			spin_lock_bh(&ct->lock);
			if (ret < 0)
				e->missed |= events;
			else
				e->missed &= ~missed;
			spin_unlock_bh(&ct->lock);
		} 
	}

out_unlock:
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(nf_ct_deliver_cached_events);

int nf_conntrack_register_notifier(struct nf_ct_event_notifier *new)
{
	int ret = 0;
	struct nf_ct_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_conntrack_event_cb);
	if (notify != NULL) {
		ret = -EBUSY;
		goto out_unlock;
	}
	rcu_assign_pointer(nf_conntrack_event_cb, new);
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;

out_unlock:
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_register_notifier);

void nf_conntrack_unregister_notifier(struct nf_ct_event_notifier *new)
{
	struct nf_ct_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_conntrack_event_cb);
	BUG_ON(notify != new);
	rcu_assign_pointer(nf_conntrack_event_cb, NULL);
	mutex_unlock(&nf_ct_ecache_mutex);
}
EXPORT_SYMBOL_GPL(nf_conntrack_unregister_notifier);

int nf_ct_expect_register_notifier(struct nf_exp_event_notifier *new)
{
	int ret = 0;
	struct nf_exp_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_expect_event_cb);
	if (notify != NULL) {
		ret = -EBUSY;
		goto out_unlock;
	}
	rcu_assign_pointer(nf_expect_event_cb, new);
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;

out_unlock:
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_register_notifier);

void nf_ct_expect_unregister_notifier(struct nf_exp_event_notifier *new)
{
	struct nf_exp_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference(nf_expect_event_cb);
	BUG_ON(notify != new);
	rcu_assign_pointer(nf_expect_event_cb, NULL);
	mutex_unlock(&nf_ct_ecache_mutex);
}
EXPORT_SYMBOL_GPL(nf_ct_expect_unregister_notifier);

#define NF_CT_EVENTS_DEFAULT 1
static int nf_ct_events __read_mostly = NF_CT_EVENTS_DEFAULT;
static int nf_ct_events_retry_timeout __read_mostly = 15*HZ;

#ifdef CONFIG_SYSCTL
static struct ctl_table event_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_events",
		.data		= &init_net.ct.sysctl_events,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "nf_conntrack_events_retry_timeout",
		.data		= &init_net.ct.sysctl_events_retry_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_jiffies,
	},
	{}
};
#endif /* CONFIG_SYSCTL */

static struct nf_ct_ext_type event_extend __read_mostly = {
	.len	= sizeof(struct nf_conntrack_ecache),
	.align	= __alignof__(struct nf_conntrack_ecache),
	.id	= NF_CT_EXT_ECACHE,
};

#ifdef CONFIG_SYSCTL
static int nf_conntrack_event_init_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(event_sysctl_table, sizeof(event_sysctl_table),
			GFP_KERNEL);
	if (!table)
		goto out;

	table[0].data = &net->ct.sysctl_events;
	table[1].data = &net->ct.sysctl_events_retry_timeout;

	net->ct.event_sysctl_header =
		register_net_sysctl_table(net,
					  nf_net_netfilter_sysctl_path, table);
	if (!net->ct.event_sysctl_header) {
		printk(KERN_ERR "nf_ct_event: can't register to sysctl.\n");
		goto out_register;
	}
	return 0;

out_register:
	kfree(table);
out:
	return -ENOMEM;
}

static void nf_conntrack_event_fini_sysctl(struct net *net)
{
	struct ctl_table *table;

	table = net->ct.event_sysctl_header->ctl_table_arg;
	unregister_net_sysctl_table(net->ct.event_sysctl_header);
	kfree(table);
}
#else
static int nf_conntrack_event_init_sysctl(struct net *net)
{
	return 0;
}

static void nf_conntrack_event_fini_sysctl(struct net *net)
{
}
#endif /* CONFIG_SYSCTL */

int nf_conntrack_ecache_init(struct net *net)
{
	int ret;

	net->ct.sysctl_events = nf_ct_events;
	net->ct.sysctl_events_retry_timeout = nf_ct_events_retry_timeout;

	if (net_eq(net, &init_net)) {
		ret = nf_ct_extend_register(&event_extend);
		if (ret < 0) {
			printk(KERN_ERR "nf_ct_event: Unable to register "
					"event extension.\n");
			goto out_extend_register;
		}
	}

	ret = nf_conntrack_event_init_sysctl(net);
	if (ret < 0)
		goto out_sysctl;

	return 0;

out_sysctl:
	if (net_eq(net, &init_net))
		nf_ct_extend_unregister(&event_extend);
out_extend_register:
	return ret;
}

void nf_conntrack_ecache_fini(struct net *net)
{
	nf_conntrack_event_fini_sysctl(net);
	if (net_eq(net, &init_net))
		nf_ct_extend_unregister(&event_extend);
}
