/* Event cache for netfilter. */

/*
 * (C) 2005 Harald Welte <laforge@gnumonks.org>
 * (C) 2005 Patrick McHardy <kaber@trash.net>
 * (C) 2005-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2005 USAGI/WIDE Project <http://www.linux-ipv6.org>
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
#include <linux/slab.h>
#include <linux/export.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>

static DEFINE_MUTEX(nf_ct_ecache_mutex);

#define ECACHE_RETRY_WAIT (HZ/10)

enum retry_state {
	STATE_CONGESTED,
	STATE_RESTART,
	STATE_DONE,
};

static enum retry_state ecache_work_evict_list(struct ct_pcpu *pcpu)
{
	struct nf_conn *refs[16];
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	unsigned int evicted = 0;
	enum retry_state ret = STATE_DONE;

	spin_lock(&pcpu->lock);

	hlist_nulls_for_each_entry(h, n, &pcpu->dying, hnnode) {
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		if (nf_ct_is_dying(ct))
			continue;

		if (nf_conntrack_event(IPCT_DESTROY, ct)) {
			ret = STATE_CONGESTED;
			break;
		}

		/* we've got the event delivered, now it's dying */
		set_bit(IPS_DYING_BIT, &ct->status);
		refs[evicted] = ct;

		if (++evicted >= ARRAY_SIZE(refs)) {
			ret = STATE_RESTART;
			break;
		}
	}

	spin_unlock(&pcpu->lock);

	/* can't _put while holding lock */
	while (evicted)
		nf_ct_put(refs[--evicted]);

	return ret;
}

static void ecache_work(struct work_struct *work)
{
	struct netns_ct *ctnet =
		container_of(work, struct netns_ct, ecache_dwork.work);
	int cpu, delay = -1;
	struct ct_pcpu *pcpu;

	local_bh_disable();

	for_each_possible_cpu(cpu) {
		enum retry_state ret;

		pcpu = per_cpu_ptr(ctnet->pcpu_lists, cpu);

		ret = ecache_work_evict_list(pcpu);

		switch (ret) {
		case STATE_CONGESTED:
			delay = ECACHE_RETRY_WAIT;
			goto out;
		case STATE_RESTART:
			delay = 0;
			break;
		case STATE_DONE:
			break;
		}
	}

 out:
	local_bh_enable();

	ctnet->ecache_dwork_pending = delay > 0;
	if (delay >= 0)
		schedule_delayed_work(&ctnet->ecache_dwork, delay);
}

/* deliver cached events and clear cache entry - must be called with locally
 * disabled softirqs */
void nf_ct_deliver_cached_events(struct nf_conn *ct)
{
	struct net *net = nf_ct_net(ct);
	unsigned long events, missed;
	struct nf_ct_event_notifier *notify;
	struct nf_conntrack_ecache *e;
	struct nf_ct_event item;
	int ret;

	rcu_read_lock();
	notify = rcu_dereference(net->ct.nf_conntrack_event_cb);
	if (notify == NULL)
		goto out_unlock;

	e = nf_ct_ecache_find(ct);
	if (e == NULL)
		goto out_unlock;

	events = xchg(&e->cache, 0);

	if (!nf_ct_is_confirmed(ct) || nf_ct_is_dying(ct) || !events)
		goto out_unlock;

	/* We make a copy of the missed event cache without taking
	 * the lock, thus we may send missed events twice. However,
	 * this does not harm and it happens very rarely. */
	missed = e->missed;

	if (!((events | missed) & e->ctmask))
		goto out_unlock;

	item.ct = ct;
	item.portid = 0;
	item.report = 0;

	ret = notify->fcn(events | missed, &item);

	if (likely(ret >= 0 && !missed))
		goto out_unlock;

	spin_lock_bh(&ct->lock);
	if (ret < 0)
		e->missed |= events;
	else
		e->missed &= ~missed;
	spin_unlock_bh(&ct->lock);

out_unlock:
	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(nf_ct_deliver_cached_events);

int nf_conntrack_register_notifier(struct net *net,
				   struct nf_ct_event_notifier *new)
{
	int ret;
	struct nf_ct_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference_protected(net->ct.nf_conntrack_event_cb,
					   lockdep_is_held(&nf_ct_ecache_mutex));
	if (notify != NULL) {
		ret = -EBUSY;
		goto out_unlock;
	}
	rcu_assign_pointer(net->ct.nf_conntrack_event_cb, new);
	ret = 0;

out_unlock:
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_register_notifier);

void nf_conntrack_unregister_notifier(struct net *net,
				      struct nf_ct_event_notifier *new)
{
	struct nf_ct_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference_protected(net->ct.nf_conntrack_event_cb,
					   lockdep_is_held(&nf_ct_ecache_mutex));
	BUG_ON(notify != new);
	RCU_INIT_POINTER(net->ct.nf_conntrack_event_cb, NULL);
	mutex_unlock(&nf_ct_ecache_mutex);
	/* synchronize_rcu() is called from ctnetlink_exit. */
}
EXPORT_SYMBOL_GPL(nf_conntrack_unregister_notifier);

int nf_ct_expect_register_notifier(struct net *net,
				   struct nf_exp_event_notifier *new)
{
	int ret;
	struct nf_exp_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference_protected(net->ct.nf_expect_event_cb,
					   lockdep_is_held(&nf_ct_ecache_mutex));
	if (notify != NULL) {
		ret = -EBUSY;
		goto out_unlock;
	}
	rcu_assign_pointer(net->ct.nf_expect_event_cb, new);
	ret = 0;

out_unlock:
	mutex_unlock(&nf_ct_ecache_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(nf_ct_expect_register_notifier);

void nf_ct_expect_unregister_notifier(struct net *net,
				      struct nf_exp_event_notifier *new)
{
	struct nf_exp_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference_protected(net->ct.nf_expect_event_cb,
					   lockdep_is_held(&nf_ct_ecache_mutex));
	BUG_ON(notify != new);
	RCU_INIT_POINTER(net->ct.nf_expect_event_cb, NULL);
	mutex_unlock(&nf_ct_ecache_mutex);
	/* synchronize_rcu() is called from ctnetlink_exit. */
}
EXPORT_SYMBOL_GPL(nf_ct_expect_unregister_notifier);

#define NF_CT_EVENTS_DEFAULT 1
static int nf_ct_events __read_mostly = NF_CT_EVENTS_DEFAULT;

#ifdef CONFIG_SYSCTL
static struct ctl_table event_sysctl_table[] = {
	{
		.procname	= "nf_conntrack_events",
		.data		= &init_net.ct.sysctl_events,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
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

	/* Don't export sysctls to unprivileged users */
	if (net->user_ns != &init_user_ns)
		table[0].procname = NULL;

	net->ct.event_sysctl_header =
		register_net_sysctl(net, "net/netfilter", table);
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

int nf_conntrack_ecache_pernet_init(struct net *net)
{
	net->ct.sysctl_events = nf_ct_events;
	INIT_DELAYED_WORK(&net->ct.ecache_dwork, ecache_work);
	return nf_conntrack_event_init_sysctl(net);
}

void nf_conntrack_ecache_pernet_fini(struct net *net)
{
	cancel_delayed_work_sync(&net->ct.ecache_dwork);
	nf_conntrack_event_fini_sysctl(net);
}

int nf_conntrack_ecache_init(void)
{
	int ret = nf_ct_extend_register(&event_extend);
	if (ret < 0)
		pr_err("nf_ct_event: Unable to register event extension.\n");
	return ret;
}

void nf_conntrack_ecache_fini(void)
{
	nf_ct_extend_unregister(&event_extend);
}
