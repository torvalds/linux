/* netfilter.c: look after the filters for various protocols. 
 * Heavily influenced by the old firewall.c by David Bonn and Alan Cox.
 *
 * Thanks to Rob `CmdrTaco' Malda for not influencing this code in any
 * way.
 *
 * Rusty Russell (C)2000 -- This code is GPL.
 *
 * February 2000: Modified by James Morris to have 1 queue per protocol.
 * 15-Mar-2000:   Added NF_REPEAT --RR.
 * 08-May-2003:	  Internal logging interface added by Jozsef Kadlecsik.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/netfilter.h>
#include <net/protocol.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/if.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <net/sock.h>
#include <net/route.h>
#include <linux/ip.h>

/* In this code, we can be waiting indefinitely for userspace to
 * service a packet if a hook returns NF_QUEUE.  We could keep a count
 * of skbuffs queued for userspace, and not deregister a hook unless
 * this is zero, but that sucks.  Now, we simply check when the
 * packets come back: if the hook is gone, the packet is discarded. */
#ifdef CONFIG_NETFILTER_DEBUG
#define NFDEBUG(format, args...)  printk(format , ## args)
#else
#define NFDEBUG(format, args...)
#endif

/* Sockopts only registered and called from user context, so
   net locking would be overkill.  Also, [gs]etsockopt calls may
   sleep. */
static DECLARE_MUTEX(nf_sockopt_mutex);

struct list_head nf_hooks[NPROTO][NF_MAX_HOOKS];
static LIST_HEAD(nf_sockopts);
static DEFINE_SPINLOCK(nf_hook_lock);

/* 
 * A queue handler may be registered for each protocol.  Each is protected by
 * long term mutex.  The handler must provide an an outfn() to accept packets
 * for queueing and must reinject all packets it receives, no matter what.
 */
static struct nf_queue_handler_t {
	nf_queue_outfn_t outfn;
	void *data;
} queue_handler[NPROTO];
static DEFINE_RWLOCK(queue_handler_lock);

int nf_register_hook(struct nf_hook_ops *reg)
{
	struct list_head *i;

	spin_lock_bh(&nf_hook_lock);
	list_for_each(i, &nf_hooks[reg->pf][reg->hooknum]) {
		if (reg->priority < ((struct nf_hook_ops *)i)->priority)
			break;
	}
	list_add_rcu(&reg->list, i->prev);
	spin_unlock_bh(&nf_hook_lock);

	synchronize_net();
	return 0;
}

void nf_unregister_hook(struct nf_hook_ops *reg)
{
	spin_lock_bh(&nf_hook_lock);
	list_del_rcu(&reg->list);
	spin_unlock_bh(&nf_hook_lock);

	synchronize_net();
}

/* Do exclusive ranges overlap? */
static inline int overlap(int min1, int max1, int min2, int max2)
{
	return max1 > min2 && min1 < max2;
}

/* Functions to register sockopt ranges (exclusive). */
int nf_register_sockopt(struct nf_sockopt_ops *reg)
{
	struct list_head *i;
	int ret = 0;

	if (down_interruptible(&nf_sockopt_mutex) != 0)
		return -EINTR;

	list_for_each(i, &nf_sockopts) {
		struct nf_sockopt_ops *ops = (struct nf_sockopt_ops *)i;
		if (ops->pf == reg->pf
		    && (overlap(ops->set_optmin, ops->set_optmax, 
				reg->set_optmin, reg->set_optmax)
			|| overlap(ops->get_optmin, ops->get_optmax, 
				   reg->get_optmin, reg->get_optmax))) {
			NFDEBUG("nf_sock overlap: %u-%u/%u-%u v %u-%u/%u-%u\n",
				ops->set_optmin, ops->set_optmax, 
				ops->get_optmin, ops->get_optmax, 
				reg->set_optmin, reg->set_optmax,
				reg->get_optmin, reg->get_optmax);
			ret = -EBUSY;
			goto out;
		}
	}

	list_add(&reg->list, &nf_sockopts);
out:
	up(&nf_sockopt_mutex);
	return ret;
}

void nf_unregister_sockopt(struct nf_sockopt_ops *reg)
{
	/* No point being interruptible: we're probably in cleanup_module() */
 restart:
	down(&nf_sockopt_mutex);
	if (reg->use != 0) {
		/* To be woken by nf_sockopt call... */
		/* FIXME: Stuart Young's name appears gratuitously. */
		set_current_state(TASK_UNINTERRUPTIBLE);
		reg->cleanup_task = current;
		up(&nf_sockopt_mutex);
		schedule();
		goto restart;
	}
	list_del(&reg->list);
	up(&nf_sockopt_mutex);
}

/* Call get/setsockopt() */
static int nf_sockopt(struct sock *sk, int pf, int val, 
		      char __user *opt, int *len, int get)
{
	struct list_head *i;
	struct nf_sockopt_ops *ops;
	int ret;

	if (down_interruptible(&nf_sockopt_mutex) != 0)
		return -EINTR;

	list_for_each(i, &nf_sockopts) {
		ops = (struct nf_sockopt_ops *)i;
		if (ops->pf == pf) {
			if (get) {
				if (val >= ops->get_optmin
				    && val < ops->get_optmax) {
					ops->use++;
					up(&nf_sockopt_mutex);
					ret = ops->get(sk, val, opt, len);
					goto out;
				}
			} else {
				if (val >= ops->set_optmin
				    && val < ops->set_optmax) {
					ops->use++;
					up(&nf_sockopt_mutex);
					ret = ops->set(sk, val, opt, *len);
					goto out;
				}
			}
		}
	}
	up(&nf_sockopt_mutex);
	return -ENOPROTOOPT;
	
 out:
	down(&nf_sockopt_mutex);
	ops->use--;
	if (ops->cleanup_task)
		wake_up_process(ops->cleanup_task);
	up(&nf_sockopt_mutex);
	return ret;
}

int nf_setsockopt(struct sock *sk, int pf, int val, char __user *opt,
		  int len)
{
	return nf_sockopt(sk, pf, val, opt, &len, 0);
}

int nf_getsockopt(struct sock *sk, int pf, int val, char __user *opt, int *len)
{
	return nf_sockopt(sk, pf, val, opt, len, 1);
}

static unsigned int nf_iterate(struct list_head *head,
			       struct sk_buff **skb,
			       int hook,
			       const struct net_device *indev,
			       const struct net_device *outdev,
			       struct list_head **i,
			       int (*okfn)(struct sk_buff *),
			       int hook_thresh)
{
	unsigned int verdict;

	/*
	 * The caller must not block between calls to this
	 * function because of risk of continuing from deleted element.
	 */
	list_for_each_continue_rcu(*i, head) {
		struct nf_hook_ops *elem = (struct nf_hook_ops *)*i;

		if (hook_thresh > elem->priority)
			continue;

		/* Optimization: we don't need to hold module
                   reference here, since function can't sleep. --RR */
		verdict = elem->hook(hook, skb, indev, outdev, okfn);
		if (verdict != NF_ACCEPT) {
#ifdef CONFIG_NETFILTER_DEBUG
			if (unlikely(verdict > NF_MAX_VERDICT)) {
				NFDEBUG("Evil return from %p(%u).\n",
				        elem->hook, hook);
				continue;
			}
#endif
			if (verdict != NF_REPEAT)
				return verdict;
			*i = (*i)->prev;
		}
	}
	return NF_ACCEPT;
}

int nf_register_queue_handler(int pf, nf_queue_outfn_t outfn, void *data)
{      
	int ret;

	write_lock_bh(&queue_handler_lock);
	if (queue_handler[pf].outfn)
		ret = -EBUSY;
	else {
		queue_handler[pf].outfn = outfn;
		queue_handler[pf].data = data;
		ret = 0;
	}
	write_unlock_bh(&queue_handler_lock);

	return ret;
}

/* The caller must flush their queue before this */
int nf_unregister_queue_handler(int pf)
{
	write_lock_bh(&queue_handler_lock);
	queue_handler[pf].outfn = NULL;
	queue_handler[pf].data = NULL;
	write_unlock_bh(&queue_handler_lock);
	
	return 0;
}

/* 
 * Any packet that leaves via this function must come back 
 * through nf_reinject().
 */
static int nf_queue(struct sk_buff *skb, 
		    struct list_head *elem, 
		    int pf, unsigned int hook,
		    struct net_device *indev,
		    struct net_device *outdev,
		    int (*okfn)(struct sk_buff *))
{
	int status;
	struct nf_info *info;
#ifdef CONFIG_BRIDGE_NETFILTER
	struct net_device *physindev = NULL;
	struct net_device *physoutdev = NULL;
#endif

	/* QUEUE == DROP if noone is waiting, to be safe. */
	read_lock(&queue_handler_lock);
	if (!queue_handler[pf].outfn) {
		read_unlock(&queue_handler_lock);
		kfree_skb(skb);
		return 1;
	}

	info = kmalloc(sizeof(*info), GFP_ATOMIC);
	if (!info) {
		if (net_ratelimit())
			printk(KERN_ERR "OOM queueing packet %p\n",
			       skb);
		read_unlock(&queue_handler_lock);
		kfree_skb(skb);
		return 1;
	}

	*info = (struct nf_info) { 
		(struct nf_hook_ops *)elem, pf, hook, indev, outdev, okfn };

	/* If it's going away, ignore hook. */
	if (!try_module_get(info->elem->owner)) {
		read_unlock(&queue_handler_lock);
		kfree(info);
		return 0;
	}

	/* Bump dev refs so they don't vanish while packet is out */
	if (indev) dev_hold(indev);
	if (outdev) dev_hold(outdev);

#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge) {
		physindev = skb->nf_bridge->physindev;
		if (physindev) dev_hold(physindev);
		physoutdev = skb->nf_bridge->physoutdev;
		if (physoutdev) dev_hold(physoutdev);
	}
#endif

	status = queue_handler[pf].outfn(skb, info, queue_handler[pf].data);
	read_unlock(&queue_handler_lock);

	if (status < 0) {
		/* James M doesn't say fuck enough. */
		if (indev) dev_put(indev);
		if (outdev) dev_put(outdev);
#ifdef CONFIG_BRIDGE_NETFILTER
		if (physindev) dev_put(physindev);
		if (physoutdev) dev_put(physoutdev);
#endif
		module_put(info->elem->owner);
		kfree(info);
		kfree_skb(skb);
		return 1;
	}
	return 1;
}

/* Returns 1 if okfn() needs to be executed by the caller,
 * -EPERM for NF_DROP, 0 otherwise. */
int nf_hook_slow(int pf, unsigned int hook, struct sk_buff **pskb,
		 struct net_device *indev,
		 struct net_device *outdev,
		 int (*okfn)(struct sk_buff *),
		 int hook_thresh)
{
	struct list_head *elem;
	unsigned int verdict;
	int ret = 0;

	/* We may already have this, but read-locks nest anyway */
	rcu_read_lock();

	elem = &nf_hooks[pf][hook];
next_hook:
	verdict = nf_iterate(&nf_hooks[pf][hook], pskb, hook, indev,
			     outdev, &elem, okfn, hook_thresh);
	if (verdict == NF_ACCEPT || verdict == NF_STOP) {
		ret = 1;
		goto unlock;
	} else if (verdict == NF_DROP) {
		kfree_skb(*pskb);
		ret = -EPERM;
	} else if (verdict == NF_QUEUE) {
		NFDEBUG("nf_hook: Verdict = QUEUE.\n");
		if (!nf_queue(*pskb, elem, pf, hook, indev, outdev, okfn))
			goto next_hook;
	}
unlock:
	rcu_read_unlock();
	return ret;
}

void nf_reinject(struct sk_buff *skb, struct nf_info *info,
		 unsigned int verdict)
{
	struct list_head *elem = &info->elem->list;
	struct list_head *i;

	rcu_read_lock();

	/* Release those devices we held, or Alexey will kill me. */
	if (info->indev) dev_put(info->indev);
	if (info->outdev) dev_put(info->outdev);
#ifdef CONFIG_BRIDGE_NETFILTER
	if (skb->nf_bridge) {
		if (skb->nf_bridge->physindev)
			dev_put(skb->nf_bridge->physindev);
		if (skb->nf_bridge->physoutdev)
			dev_put(skb->nf_bridge->physoutdev);
	}
#endif

	/* Drop reference to owner of hook which queued us. */
	module_put(info->elem->owner);

	list_for_each_rcu(i, &nf_hooks[info->pf][info->hook]) {
		if (i == elem) 
  			break;
  	}
  
	if (elem == &nf_hooks[info->pf][info->hook]) {
		/* The module which sent it to userspace is gone. */
		NFDEBUG("%s: module disappeared, dropping packet.\n",
			__FUNCTION__);
		verdict = NF_DROP;
	}

	/* Continue traversal iff userspace said ok... */
	if (verdict == NF_REPEAT) {
		elem = elem->prev;
		verdict = NF_ACCEPT;
	}

	if (verdict == NF_ACCEPT) {
	next_hook:
		verdict = nf_iterate(&nf_hooks[info->pf][info->hook],
				     &skb, info->hook, 
				     info->indev, info->outdev, &elem,
				     info->okfn, INT_MIN);
	}

	switch (verdict) {
	case NF_ACCEPT:
		info->okfn(skb);
		break;

	case NF_QUEUE:
		if (!nf_queue(skb, elem, info->pf, info->hook, 
			      info->indev, info->outdev, info->okfn))
			goto next_hook;
		break;
	}
	rcu_read_unlock();

	if (verdict == NF_DROP)
		kfree_skb(skb);

	kfree(info);
	return;
}

#ifdef CONFIG_INET
/* route_me_harder function, used by iptable_nat, iptable_mangle + ip_queue */
int ip_route_me_harder(struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct rtable *rt;
	struct flowi fl = {};
	struct dst_entry *odst;
	unsigned int hh_len;

	/* some non-standard hacks like ipt_REJECT.c:send_reset() can cause
	 * packets with foreign saddr to appear on the NF_IP_LOCAL_OUT hook.
	 */
	if (inet_addr_type(iph->saddr) == RTN_LOCAL) {
		fl.nl_u.ip4_u.daddr = iph->daddr;
		fl.nl_u.ip4_u.saddr = iph->saddr;
		fl.nl_u.ip4_u.tos = RT_TOS(iph->tos);
		fl.oif = (*pskb)->sk ? (*pskb)->sk->sk_bound_dev_if : 0;
#ifdef CONFIG_IP_ROUTE_FWMARK
		fl.nl_u.ip4_u.fwmark = (*pskb)->nfmark;
#endif
		fl.proto = iph->protocol;
		if (ip_route_output_key(&rt, &fl) != 0)
			return -1;

		/* Drop old route. */
		dst_release((*pskb)->dst);
		(*pskb)->dst = &rt->u.dst;
	} else {
		/* non-local src, find valid iif to satisfy
		 * rp-filter when calling ip_route_input. */
		fl.nl_u.ip4_u.daddr = iph->saddr;
		if (ip_route_output_key(&rt, &fl) != 0)
			return -1;

		odst = (*pskb)->dst;
		if (ip_route_input(*pskb, iph->daddr, iph->saddr,
				   RT_TOS(iph->tos), rt->u.dst.dev) != 0) {
			dst_release(&rt->u.dst);
			return -1;
		}
		dst_release(&rt->u.dst);
		dst_release(odst);
	}
	
	if ((*pskb)->dst->error)
		return -1;

	/* Change in oif may mean change in hh_len. */
	hh_len = (*pskb)->dst->dev->hard_header_len;
	if (skb_headroom(*pskb) < hh_len) {
		struct sk_buff *nskb;

		nskb = skb_realloc_headroom(*pskb, hh_len);
		if (!nskb) 
			return -1;
		if ((*pskb)->sk)
			skb_set_owner_w(nskb, (*pskb)->sk);
		kfree_skb(*pskb);
		*pskb = nskb;
	}

	return 0;
}
EXPORT_SYMBOL(ip_route_me_harder);

int skb_ip_make_writable(struct sk_buff **pskb, unsigned int writable_len)
{
	struct sk_buff *nskb;

	if (writable_len > (*pskb)->len)
		return 0;

	/* Not exclusive use of packet?  Must copy. */
	if (skb_shared(*pskb) || skb_cloned(*pskb))
		goto copy_skb;

	return pskb_may_pull(*pskb, writable_len);

copy_skb:
	nskb = skb_copy(*pskb, GFP_ATOMIC);
	if (!nskb)
		return 0;
	BUG_ON(skb_is_nonlinear(nskb));

	/* Rest of kernel will get very unhappy if we pass it a
	   suddenly-orphaned skbuff */
	if ((*pskb)->sk)
		skb_set_owner_w(nskb, (*pskb)->sk);
	kfree_skb(*pskb);
	*pskb = nskb;
	return 1;
}
EXPORT_SYMBOL(skb_ip_make_writable);
#endif /*CONFIG_INET*/

/* Internal logging interface, which relies on the real 
   LOG target modules */

#define NF_LOG_PREFIXLEN		128

static nf_logfn *nf_logging[NPROTO]; /* = NULL */
static int reported = 0;
static DEFINE_SPINLOCK(nf_log_lock);

int nf_log_register(int pf, nf_logfn *logfn)
{
	int ret = -EBUSY;

	/* Any setup of logging members must be done before
	 * substituting pointer. */
	spin_lock(&nf_log_lock);
	if (!nf_logging[pf]) {
		rcu_assign_pointer(nf_logging[pf], logfn);
		ret = 0;
	}
	spin_unlock(&nf_log_lock);
	return ret;
}		

void nf_log_unregister(int pf, nf_logfn *logfn)
{
	spin_lock(&nf_log_lock);
	if (nf_logging[pf] == logfn)
		nf_logging[pf] = NULL;
	spin_unlock(&nf_log_lock);

	/* Give time to concurrent readers. */
	synchronize_net();
}		

void nf_log_packet(int pf,
		   unsigned int hooknum,
		   const struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   const char *fmt, ...)
{
	va_list args;
	char prefix[NF_LOG_PREFIXLEN];
	nf_logfn *logfn;
	
	rcu_read_lock();
	logfn = rcu_dereference(nf_logging[pf]);
	if (logfn) {
		va_start(args, fmt);
		vsnprintf(prefix, sizeof(prefix), fmt, args);
		va_end(args);
		/* We must read logging before nf_logfn[pf] */
		logfn(hooknum, skb, in, out, prefix);
	} else if (!reported) {
		printk(KERN_WARNING "nf_log_packet: can\'t log yet, "
		       "no backend logging module loaded in!\n");
		reported++;
	}
	rcu_read_unlock();
}
EXPORT_SYMBOL(nf_log_register);
EXPORT_SYMBOL(nf_log_unregister);
EXPORT_SYMBOL(nf_log_packet);

/* This does not belong here, but locally generated errors need it if connection
   tracking in use: without this, connection may not be in hash table, and hence
   manufactured ICMP or RST packets will not be associated with it. */
void (*ip_ct_attach)(struct sk_buff *, struct sk_buff *);

void nf_ct_attach(struct sk_buff *new, struct sk_buff *skb)
{
	void (*attach)(struct sk_buff *, struct sk_buff *);

	if (skb->nfct && (attach = ip_ct_attach) != NULL) {
		mb(); /* Just to be sure: must be read before executing this */
		attach(new, skb);
	}
}

void __init netfilter_init(void)
{
	int i, h;

	for (i = 0; i < NPROTO; i++) {
		for (h = 0; h < NF_MAX_HOOKS; h++)
			INIT_LIST_HEAD(&nf_hooks[i][h]);
	}
}

EXPORT_SYMBOL(ip_ct_attach);
EXPORT_SYMBOL(nf_ct_attach);
EXPORT_SYMBOL(nf_getsockopt);
EXPORT_SYMBOL(nf_hook_slow);
EXPORT_SYMBOL(nf_hooks);
EXPORT_SYMBOL(nf_register_hook);
EXPORT_SYMBOL(nf_register_queue_handler);
EXPORT_SYMBOL(nf_register_sockopt);
EXPORT_SYMBOL(nf_reinject);
EXPORT_SYMBOL(nf_setsockopt);
EXPORT_SYMBOL(nf_unregister_hook);
EXPORT_SYMBOL(nf_unregister_queue_handler);
EXPORT_SYMBOL(nf_unregister_sockopt);
