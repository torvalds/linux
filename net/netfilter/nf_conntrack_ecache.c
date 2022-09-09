// SPDX-License-Identifier: GPL-2.0-only
/* Event cache for netfilter. */

/*
 * (C) 2005 Harald Welte <laforge@gnumonks.org>
 * (C) 2005 Patrick McHardy <kaber@trash.net>
 * (C) 2005-2006 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2005 USAGI/WIDE Project <http://www.linux-ipv6.org>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/vmalloc.h>
#include <linux/stddef.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_ecache.h>
#include <net/netfilter/nf_conntrack_extend.h>

static DEFINE_MUTEX(nf_ct_ecache_mutex);

#define DYING_NULLS_VAL			((1 << 30) + 1)
#define ECACHE_MAX_JIFFIES		msecs_to_jiffies(10)
#define ECACHE_RETRY_JIFFIES		msecs_to_jiffies(10)

enum retry_state {
	STATE_CONGESTED,
	STATE_RESTART,
	STATE_DONE,
};

struct nf_conntrack_net_ecache *nf_conn_pernet_ecache(const struct net *net)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);

	return &cnet->ecache;
}
#if IS_MODULE(CONFIG_NF_CT_NETLINK)
EXPORT_SYMBOL_GPL(nf_conn_pernet_ecache);
#endif

static enum retry_state ecache_work_evict_list(struct nf_conntrack_net *cnet)
{
	unsigned long stop = jiffies + ECACHE_MAX_JIFFIES;
	struct hlist_nulls_head evicted_list;
	enum retry_state ret = STATE_DONE;
	struct nf_conntrack_tuple_hash *h;
	struct hlist_nulls_node *n;
	unsigned int sent;

	INIT_HLIST_NULLS_HEAD(&evicted_list, DYING_NULLS_VAL);

next:
	sent = 0;
	spin_lock_bh(&cnet->ecache.dying_lock);

	hlist_nulls_for_each_entry_safe(h, n, &cnet->ecache.dying_list, hnnode) {
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		/* The worker owns all entries, ct remains valid until nf_ct_put
		 * in the loop below.
		 */
		if (nf_conntrack_event(IPCT_DESTROY, ct)) {
			ret = STATE_CONGESTED;
			break;
		}

		hlist_nulls_del_rcu(&ct->tuplehash[IP_CT_DIR_ORIGINAL].hnnode);
		hlist_nulls_add_head(&ct->tuplehash[IP_CT_DIR_REPLY].hnnode, &evicted_list);

		if (time_after(stop, jiffies)) {
			ret = STATE_RESTART;
			break;
		}

		if (sent++ > 16) {
			spin_unlock_bh(&cnet->ecache.dying_lock);
			cond_resched();
			goto next;
		}
	}

	spin_unlock_bh(&cnet->ecache.dying_lock);

	hlist_nulls_for_each_entry_safe(h, n, &evicted_list, hnnode) {
		struct nf_conn *ct = nf_ct_tuplehash_to_ctrack(h);

		hlist_nulls_del_rcu(&ct->tuplehash[IP_CT_DIR_REPLY].hnnode);
		nf_ct_put(ct);

		cond_resched();
	}

	return ret;
}

static void ecache_work(struct work_struct *work)
{
	struct nf_conntrack_net *cnet = container_of(work, struct nf_conntrack_net, ecache.dwork.work);
	int ret, delay = -1;

	ret = ecache_work_evict_list(cnet);
	switch (ret) {
	case STATE_CONGESTED:
		delay = ECACHE_RETRY_JIFFIES;
		break;
	case STATE_RESTART:
		delay = 0;
		break;
	case STATE_DONE:
		break;
	}

	if (delay >= 0)
		schedule_delayed_work(&cnet->ecache.dwork, delay);
}

static int __nf_conntrack_eventmask_report(struct nf_conntrack_ecache *e,
					   const u32 events,
					   const u32 missed,
					   const struct nf_ct_event *item)
{
	struct net *net = nf_ct_net(item->ct);
	struct nf_ct_event_notifier *notify;
	u32 old, want;
	int ret;

	if (!((events | missed) & e->ctmask))
		return 0;

	rcu_read_lock();

	notify = rcu_dereference(net->ct.nf_conntrack_event_cb);
	if (!notify) {
		rcu_read_unlock();
		return 0;
	}

	ret = notify->ct_event(events | missed, item);
	rcu_read_unlock();

	if (likely(ret >= 0 && missed == 0))
		return 0;

	do {
		old = READ_ONCE(e->missed);
		if (ret < 0)
			want = old | events;
		else
			want = old & ~missed;
	} while (cmpxchg(&e->missed, old, want) != old);

	return ret;
}

int nf_conntrack_eventmask_report(unsigned int events, struct nf_conn *ct,
				  u32 portid, int report)
{
	struct nf_conntrack_ecache *e;
	struct nf_ct_event item;
	unsigned int missed;
	int ret;

	if (!nf_ct_is_confirmed(ct))
		return 0;

	e = nf_ct_ecache_find(ct);
	if (!e)
		return 0;

	memset(&item, 0, sizeof(item));

	item.ct = ct;
	item.portid = e->portid ? e->portid : portid;
	item.report = report;

	/* This is a resent of a destroy event? If so, skip missed */
	missed = e->portid ? 0 : e->missed;

	ret = __nf_conntrack_eventmask_report(e, events, missed, &item);
	if (unlikely(ret < 0 && (events & (1 << IPCT_DESTROY)))) {
		/* This is a destroy event that has been triggered by a process,
		 * we store the PORTID to include it in the retransmission.
		 */
		if (e->portid == 0 && portid != 0)
			e->portid = portid;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(nf_conntrack_eventmask_report);

/* deliver cached events and clear cache entry - must be called with locally
 * disabled softirqs */
void nf_ct_deliver_cached_events(struct nf_conn *ct)
{
	struct nf_conntrack_ecache *e;
	struct nf_ct_event item;
	unsigned int events;

	if (!nf_ct_is_confirmed(ct) || nf_ct_is_dying(ct))
		return;

	e = nf_ct_ecache_find(ct);
	if (e == NULL)
		return;

	events = xchg(&e->cache, 0);

	item.ct = ct;
	item.portid = 0;
	item.report = 0;

	/* We make a copy of the missed event cache without taking
	 * the lock, thus we may send missed events twice. However,
	 * this does not harm and it happens very rarely.
	 */
	__nf_conntrack_eventmask_report(e, events, e->missed, &item);
}
EXPORT_SYMBOL_GPL(nf_ct_deliver_cached_events);

void nf_ct_expect_event_report(enum ip_conntrack_expect_events event,
			       struct nf_conntrack_expect *exp,
			       u32 portid, int report)

{
	struct net *net = nf_ct_exp_net(exp);
	struct nf_ct_event_notifier *notify;
	struct nf_conntrack_ecache *e;

	rcu_read_lock();
	notify = rcu_dereference(net->ct.nf_conntrack_event_cb);
	if (!notify)
		goto out_unlock;

	e = nf_ct_ecache_find(exp->master);
	if (!e)
		goto out_unlock;

	if (e->expmask & (1 << event)) {
		struct nf_exp_event item = {
			.exp	= exp,
			.portid	= portid,
			.report = report
		};
		notify->exp_event(1 << event, &item);
	}
out_unlock:
	rcu_read_unlock();
}

void nf_conntrack_register_notifier(struct net *net,
				    const struct nf_ct_event_notifier *new)
{
	struct nf_ct_event_notifier *notify;

	mutex_lock(&nf_ct_ecache_mutex);
	notify = rcu_dereference_protected(net->ct.nf_conntrack_event_cb,
					   lockdep_is_held(&nf_ct_ecache_mutex));
	WARN_ON_ONCE(notify);
	rcu_assign_pointer(net->ct.nf_conntrack_event_cb, new);
	mutex_unlock(&nf_ct_ecache_mutex);
}
EXPORT_SYMBOL_GPL(nf_conntrack_register_notifier);

void nf_conntrack_unregister_notifier(struct net *net)
{
	mutex_lock(&nf_ct_ecache_mutex);
	RCU_INIT_POINTER(net->ct.nf_conntrack_event_cb, NULL);
	mutex_unlock(&nf_ct_ecache_mutex);
	/* synchronize_rcu() is called after netns pre_exit */
}
EXPORT_SYMBOL_GPL(nf_conntrack_unregister_notifier);

void nf_conntrack_ecache_work(struct net *net, enum nf_ct_ecache_state state)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);

	if (state == NFCT_ECACHE_DESTROY_FAIL &&
	    !delayed_work_pending(&cnet->ecache.dwork)) {
		schedule_delayed_work(&cnet->ecache.dwork, HZ);
		net->ct.ecache_dwork_pending = true;
	} else if (state == NFCT_ECACHE_DESTROY_SENT) {
		if (!hlist_nulls_empty(&cnet->ecache.dying_list))
			mod_delayed_work(system_wq, &cnet->ecache.dwork, 0);
		else
			net->ct.ecache_dwork_pending = false;
	}
}

bool nf_ct_ecache_ext_add(struct nf_conn *ct, u16 ctmask, u16 expmask, gfp_t gfp)
{
	struct net *net = nf_ct_net(ct);
	struct nf_conntrack_ecache *e;

	switch (net->ct.sysctl_events) {
	case 0:
		 /* assignment via template / ruleset? ignore sysctl. */
		if (ctmask || expmask)
			break;
		return true;
	case 2: /* autodetect: no event listener, don't allocate extension. */
		if (!READ_ONCE(net->ct.ctnetlink_has_listener))
			return true;
		fallthrough;
	case 1:
		/* always allocate an extension. */
		if (!ctmask && !expmask) {
			ctmask = ~0;
			expmask = ~0;
		}
		break;
	default:
		WARN_ON_ONCE(1);
		return true;
	}

	e = nf_ct_ext_add(ct, NF_CT_EXT_ECACHE, gfp);
	if (e) {
		e->ctmask  = ctmask;
		e->expmask = expmask;
	}

	return e != NULL;
}
EXPORT_SYMBOL_GPL(nf_ct_ecache_ext_add);

#define NF_CT_EVENTS_DEFAULT 2
static int nf_ct_events __read_mostly = NF_CT_EVENTS_DEFAULT;

void nf_conntrack_ecache_pernet_init(struct net *net)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);

	net->ct.sysctl_events = nf_ct_events;

	INIT_DELAYED_WORK(&cnet->ecache.dwork, ecache_work);
	INIT_HLIST_NULLS_HEAD(&cnet->ecache.dying_list, DYING_NULLS_VAL);
	spin_lock_init(&cnet->ecache.dying_lock);

	BUILD_BUG_ON(__IPCT_MAX >= 16);	/* e->ctmask is u16 */
}

void nf_conntrack_ecache_pernet_fini(struct net *net)
{
	struct nf_conntrack_net *cnet = nf_ct_pernet(net);

	cancel_delayed_work_sync(&cnet->ecache.dwork);
}
