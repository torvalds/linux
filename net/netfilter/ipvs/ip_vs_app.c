// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ip_vs_app.c: Application module support for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 * Most code here is taken from ip_masq_app.c in kernel 2.2. The difference
 * is that ip_vs_app module handles the reverse direction (incoming requests
 * and outgoing responses).
 *
 *		IP_MASQ_APP application masquerading module
 *
 * Author:	Juan Jose Ciarlante, <jjciarla@raiz.uncu.edu.ar>
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>

#include <net/ip_vs.h>

EXPORT_SYMBOL(register_ip_vs_app);
EXPORT_SYMBOL(unregister_ip_vs_app);
EXPORT_SYMBOL(register_ip_vs_app_inc);

static DEFINE_MUTEX(__ip_vs_app_mutex);

/*
 *	Get an ip_vs_app object
 */
static inline int ip_vs_app_get(struct ip_vs_app *app)
{
	return try_module_get(app->module);
}


static inline void ip_vs_app_put(struct ip_vs_app *app)
{
	module_put(app->module);
}

static void ip_vs_app_inc_destroy(struct ip_vs_app *inc)
{
	kfree(inc->timeout_table);
	kfree(inc);
}

static void ip_vs_app_inc_rcu_free(struct rcu_head *head)
{
	struct ip_vs_app *inc = container_of(head, struct ip_vs_app, rcu_head);

	ip_vs_app_inc_destroy(inc);
}

/*
 *	Allocate/initialize app incarnation and register it in proto apps.
 */
static int
ip_vs_app_inc_new(struct netns_ipvs *ipvs, struct ip_vs_app *app, __u16 proto,
		  __u16 port)
{
	struct ip_vs_protocol *pp;
	struct ip_vs_app *inc;
	int ret;

	if (!(pp = ip_vs_proto_get(proto)))
		return -EPROTONOSUPPORT;

	if (!pp->unregister_app)
		return -EOPNOTSUPP;

	inc = kmemdup(app, sizeof(*inc), GFP_KERNEL);
	if (!inc)
		return -ENOMEM;
	INIT_LIST_HEAD(&inc->p_list);
	INIT_LIST_HEAD(&inc->incs_list);
	inc->app = app;
	inc->port = htons(port);
	atomic_set(&inc->usecnt, 0);

	if (app->timeouts) {
		inc->timeout_table =
			ip_vs_create_timeout_table(app->timeouts,
						   app->timeouts_size);
		if (!inc->timeout_table) {
			ret = -ENOMEM;
			goto out;
		}
	}

	ret = pp->register_app(ipvs, inc);
	if (ret)
		goto out;

	list_add(&inc->a_list, &app->incs_list);
	IP_VS_DBG(9, "%s App %s:%u registered\n",
		  pp->name, inc->name, ntohs(inc->port));

	return 0;

  out:
	ip_vs_app_inc_destroy(inc);
	return ret;
}


/*
 *	Release app incarnation
 */
static void
ip_vs_app_inc_release(struct netns_ipvs *ipvs, struct ip_vs_app *inc)
{
	struct ip_vs_protocol *pp;

	if (!(pp = ip_vs_proto_get(inc->protocol)))
		return;

	if (pp->unregister_app)
		pp->unregister_app(ipvs, inc);

	IP_VS_DBG(9, "%s App %s:%u unregistered\n",
		  pp->name, inc->name, ntohs(inc->port));

	list_del(&inc->a_list);

	call_rcu(&inc->rcu_head, ip_vs_app_inc_rcu_free);
}


/*
 *	Get reference to app inc (only called from softirq)
 *
 */
int ip_vs_app_inc_get(struct ip_vs_app *inc)
{
	int result;

	result = ip_vs_app_get(inc->app);
	if (result)
		atomic_inc(&inc->usecnt);
	return result;
}


/*
 *	Put the app inc (only called from timer or net softirq)
 */
void ip_vs_app_inc_put(struct ip_vs_app *inc)
{
	atomic_dec(&inc->usecnt);
	ip_vs_app_put(inc->app);
}


/*
 *	Register an application incarnation in protocol applications
 */
int
register_ip_vs_app_inc(struct netns_ipvs *ipvs, struct ip_vs_app *app, __u16 proto,
		       __u16 port)
{
	int result;

	mutex_lock(&__ip_vs_app_mutex);

	result = ip_vs_app_inc_new(ipvs, app, proto, port);

	mutex_unlock(&__ip_vs_app_mutex);

	return result;
}


/* Register application for netns */
struct ip_vs_app *register_ip_vs_app(struct netns_ipvs *ipvs, struct ip_vs_app *app)
{
	struct ip_vs_app *a;
	int err = 0;

	mutex_lock(&__ip_vs_app_mutex);

	/* increase the module use count */
	if (!ip_vs_use_count_inc()) {
		err = -ENOENT;
		goto out_unlock;
	}

	list_for_each_entry(a, &ipvs->app_list, a_list) {
		if (!strcmp(app->name, a->name)) {
			err = -EEXIST;
			/* decrease the module use count */
			ip_vs_use_count_dec();
			goto out_unlock;
		}
	}
	a = kmemdup(app, sizeof(*app), GFP_KERNEL);
	if (!a) {
		err = -ENOMEM;
		/* decrease the module use count */
		ip_vs_use_count_dec();
		goto out_unlock;
	}
	INIT_LIST_HEAD(&a->incs_list);
	list_add(&a->a_list, &ipvs->app_list);

out_unlock:
	mutex_unlock(&__ip_vs_app_mutex);

	return err ? ERR_PTR(err) : a;
}


/*
 *	ip_vs_app unregistration routine
 *	We are sure there are no app incarnations attached to services
 *	Caller should use synchronize_rcu() or rcu_barrier()
 */
void unregister_ip_vs_app(struct netns_ipvs *ipvs, struct ip_vs_app *app)
{
	struct ip_vs_app *a, *anxt, *inc, *nxt;

	mutex_lock(&__ip_vs_app_mutex);

	list_for_each_entry_safe(a, anxt, &ipvs->app_list, a_list) {
		if (app && strcmp(app->name, a->name))
			continue;
		list_for_each_entry_safe(inc, nxt, &a->incs_list, a_list) {
			ip_vs_app_inc_release(ipvs, inc);
		}

		list_del(&a->a_list);
		kfree(a);

		/* decrease the module use count */
		ip_vs_use_count_dec();
	}

	mutex_unlock(&__ip_vs_app_mutex);
}


/*
 *	Bind ip_vs_conn to its ip_vs_app (called by cp constructor)
 */
int ip_vs_bind_app(struct ip_vs_conn *cp,
		   struct ip_vs_protocol *pp)
{
	return pp->app_conn_bind(cp);
}


/*
 *	Unbind cp from application incarnation (called by cp destructor)
 */
void ip_vs_unbind_app(struct ip_vs_conn *cp)
{
	struct ip_vs_app *inc = cp->app;

	if (!inc)
		return;

	if (inc->unbind_conn)
		inc->unbind_conn(inc, cp);
	if (inc->done_conn)
		inc->done_conn(inc, cp);
	ip_vs_app_inc_put(inc);
	cp->app = NULL;
}


/*
 *	Fixes th->seq based on ip_vs_seq info.
 */
static inline void vs_fix_seq(const struct ip_vs_seq *vseq, struct tcphdr *th)
{
	__u32 seq = ntohl(th->seq);

	/*
	 *	Adjust seq with delta-offset for all packets after
	 *	the most recent resized pkt seq and with previous_delta offset
	 *	for all packets	before most recent resized pkt seq.
	 */
	if (vseq->delta || vseq->previous_delta) {
		if(after(seq, vseq->init_seq)) {
			th->seq = htonl(seq + vseq->delta);
			IP_VS_DBG(9, "%s(): added delta (%d) to seq\n",
				  __func__, vseq->delta);
		} else {
			th->seq = htonl(seq + vseq->previous_delta);
			IP_VS_DBG(9, "%s(): added previous_delta (%d) to seq\n",
				  __func__, vseq->previous_delta);
		}
	}
}


/*
 *	Fixes th->ack_seq based on ip_vs_seq info.
 */
static inline void
vs_fix_ack_seq(const struct ip_vs_seq *vseq, struct tcphdr *th)
{
	__u32 ack_seq = ntohl(th->ack_seq);

	/*
	 * Adjust ack_seq with delta-offset for
	 * the packets AFTER most recent resized pkt has caused a shift
	 * for packets before most recent resized pkt, use previous_delta
	 */
	if (vseq->delta || vseq->previous_delta) {
		/* since ack_seq is the number of octet that is expected
		   to receive next, so compare it with init_seq+delta */
		if(after(ack_seq, vseq->init_seq+vseq->delta)) {
			th->ack_seq = htonl(ack_seq - vseq->delta);
			IP_VS_DBG(9, "%s(): subtracted delta "
				  "(%d) from ack_seq\n", __func__, vseq->delta);

		} else {
			th->ack_seq = htonl(ack_seq - vseq->previous_delta);
			IP_VS_DBG(9, "%s(): subtracted "
				  "previous_delta (%d) from ack_seq\n",
				  __func__, vseq->previous_delta);
		}
	}
}


/*
 *	Updates ip_vs_seq if pkt has been resized
 *	Assumes already checked proto==IPPROTO_TCP and diff!=0.
 */
static inline void vs_seq_update(struct ip_vs_conn *cp, struct ip_vs_seq *vseq,
				 unsigned int flag, __u32 seq, int diff)
{
	/* spinlock is to keep updating cp->flags atomic */
	spin_lock_bh(&cp->lock);
	if (!(cp->flags & flag) || after(seq, vseq->init_seq)) {
		vseq->previous_delta = vseq->delta;
		vseq->delta += diff;
		vseq->init_seq = seq;
		cp->flags |= flag;
	}
	spin_unlock_bh(&cp->lock);
}

static inline int app_tcp_pkt_out(struct ip_vs_conn *cp, struct sk_buff *skb,
				  struct ip_vs_app *app,
				  struct ip_vs_iphdr *ipvsh)
{
	int diff;
	const unsigned int tcp_offset = ip_hdrlen(skb);
	struct tcphdr *th;
	__u32 seq;

	if (skb_ensure_writable(skb, tcp_offset + sizeof(*th)))
		return 0;

	th = (struct tcphdr *)(skb_network_header(skb) + tcp_offset);

	/*
	 *	Remember seq number in case this pkt gets resized
	 */
	seq = ntohl(th->seq);

	/*
	 *	Fix seq stuff if flagged as so.
	 */
	if (cp->flags & IP_VS_CONN_F_OUT_SEQ)
		vs_fix_seq(&cp->out_seq, th);
	if (cp->flags & IP_VS_CONN_F_IN_SEQ)
		vs_fix_ack_seq(&cp->in_seq, th);

	/*
	 *	Call private output hook function
	 */
	if (app->pkt_out == NULL)
		return 1;

	if (!app->pkt_out(app, cp, skb, &diff, ipvsh))
		return 0;

	/*
	 *	Update ip_vs seq stuff if len has changed.
	 */
	if (diff != 0)
		vs_seq_update(cp, &cp->out_seq,
			      IP_VS_CONN_F_OUT_SEQ, seq, diff);

	return 1;
}

/*
 *	Output pkt hook. Will call bound ip_vs_app specific function
 *	called by ipvs packet handler, assumes previously checked cp!=NULL
 *	returns false if it can't handle packet (oom)
 */
int ip_vs_app_pkt_out(struct ip_vs_conn *cp, struct sk_buff *skb,
		      struct ip_vs_iphdr *ipvsh)
{
	struct ip_vs_app *app;

	/*
	 *	check if application module is bound to
	 *	this ip_vs_conn.
	 */
	if ((app = cp->app) == NULL)
		return 1;

	/* TCP is complicated */
	if (cp->protocol == IPPROTO_TCP)
		return app_tcp_pkt_out(cp, skb, app, ipvsh);

	/*
	 *	Call private output hook function
	 */
	if (app->pkt_out == NULL)
		return 1;

	return app->pkt_out(app, cp, skb, NULL, ipvsh);
}


static inline int app_tcp_pkt_in(struct ip_vs_conn *cp, struct sk_buff *skb,
				 struct ip_vs_app *app,
				 struct ip_vs_iphdr *ipvsh)
{
	int diff;
	const unsigned int tcp_offset = ip_hdrlen(skb);
	struct tcphdr *th;
	__u32 seq;

	if (skb_ensure_writable(skb, tcp_offset + sizeof(*th)))
		return 0;

	th = (struct tcphdr *)(skb_network_header(skb) + tcp_offset);

	/*
	 *	Remember seq number in case this pkt gets resized
	 */
	seq = ntohl(th->seq);

	/*
	 *	Fix seq stuff if flagged as so.
	 */
	if (cp->flags & IP_VS_CONN_F_IN_SEQ)
		vs_fix_seq(&cp->in_seq, th);
	if (cp->flags & IP_VS_CONN_F_OUT_SEQ)
		vs_fix_ack_seq(&cp->out_seq, th);

	/*
	 *	Call private input hook function
	 */
	if (app->pkt_in == NULL)
		return 1;

	if (!app->pkt_in(app, cp, skb, &diff, ipvsh))
		return 0;

	/*
	 *	Update ip_vs seq stuff if len has changed.
	 */
	if (diff != 0)
		vs_seq_update(cp, &cp->in_seq,
			      IP_VS_CONN_F_IN_SEQ, seq, diff);

	return 1;
}

/*
 *	Input pkt hook. Will call bound ip_vs_app specific function
 *	called by ipvs packet handler, assumes previously checked cp!=NULL.
 *	returns false if can't handle packet (oom).
 */
int ip_vs_app_pkt_in(struct ip_vs_conn *cp, struct sk_buff *skb,
		     struct ip_vs_iphdr *ipvsh)
{
	struct ip_vs_app *app;

	/*
	 *	check if application module is bound to
	 *	this ip_vs_conn.
	 */
	if ((app = cp->app) == NULL)
		return 1;

	/* TCP is complicated */
	if (cp->protocol == IPPROTO_TCP)
		return app_tcp_pkt_in(cp, skb, app, ipvsh);

	/*
	 *	Call private input hook function
	 */
	if (app->pkt_in == NULL)
		return 1;

	return app->pkt_in(app, cp, skb, NULL, ipvsh);
}


#ifdef CONFIG_PROC_FS
/*
 *	/proc/net/ip_vs_app entry function
 */

static struct ip_vs_app *ip_vs_app_idx(struct netns_ipvs *ipvs, loff_t pos)
{
	struct ip_vs_app *app, *inc;

	list_for_each_entry(app, &ipvs->app_list, a_list) {
		list_for_each_entry(inc, &app->incs_list, a_list) {
			if (pos-- == 0)
				return inc;
		}
	}
	return NULL;

}

static void *ip_vs_app_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct net *net = seq_file_net(seq);
	struct netns_ipvs *ipvs = net_ipvs(net);

	mutex_lock(&__ip_vs_app_mutex);

	return *pos ? ip_vs_app_idx(ipvs, *pos - 1) : SEQ_START_TOKEN;
}

static void *ip_vs_app_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ip_vs_app *inc, *app;
	struct list_head *e;
	struct net *net = seq_file_net(seq);
	struct netns_ipvs *ipvs = net_ipvs(net);

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ip_vs_app_idx(ipvs, 0);

	inc = v;
	app = inc->app;

	if ((e = inc->a_list.next) != &app->incs_list)
		return list_entry(e, struct ip_vs_app, a_list);

	/* go on to next application */
	for (e = app->a_list.next; e != &ipvs->app_list; e = e->next) {
		app = list_entry(e, struct ip_vs_app, a_list);
		list_for_each_entry(inc, &app->incs_list, a_list) {
			return inc;
		}
	}
	return NULL;
}

static void ip_vs_app_seq_stop(struct seq_file *seq, void *v)
{
	mutex_unlock(&__ip_vs_app_mutex);
}

static int ip_vs_app_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq, "prot port    usecnt name\n");
	else {
		const struct ip_vs_app *inc = v;

		seq_printf(seq, "%-3s  %-7u %-6d %-17s\n",
			   ip_vs_proto_name(inc->protocol),
			   ntohs(inc->port),
			   atomic_read(&inc->usecnt),
			   inc->name);
	}
	return 0;
}

static const struct seq_operations ip_vs_app_seq_ops = {
	.start = ip_vs_app_seq_start,
	.next  = ip_vs_app_seq_next,
	.stop  = ip_vs_app_seq_stop,
	.show  = ip_vs_app_seq_show,
};
#endif

int __net_init ip_vs_app_net_init(struct netns_ipvs *ipvs)
{
	INIT_LIST_HEAD(&ipvs->app_list);
#ifdef CONFIG_PROC_FS
	if (!proc_create_net("ip_vs_app", 0, ipvs->net->proc_net,
			     &ip_vs_app_seq_ops,
			     sizeof(struct seq_net_private)))
		return -ENOMEM;
#endif
	return 0;
}

void __net_exit ip_vs_app_net_cleanup(struct netns_ipvs *ipvs)
{
	unregister_ip_vs_app(ipvs, NULL /* all */);
#ifdef CONFIG_PROC_FS
	remove_proc_entry("ip_vs_app", ipvs->net->proc_net);
#endif
}
