/*
 * ip_vs_app.c: Application module support for IPVS
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Most code here is taken from ip_masq_app.c in kernel 2.2. The difference
 * is that ip_vs_app module handles the reverse direction (incoming requests
 * and outgoing responses).
 *
 *		IP_MASQ_APP application masquerading module
 *
 * Author:	Juan Jose Ciarlante, <jjciarla@raiz.uncu.edu.ar>
 *
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <net/net_namespace.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <asm/system.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>

#include <net/ip_vs.h>

EXPORT_SYMBOL(register_ip_vs_app);
EXPORT_SYMBOL(unregister_ip_vs_app);
EXPORT_SYMBOL(register_ip_vs_app_inc);

/* ipvs application list head */
static LIST_HEAD(ip_vs_app_list);
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


/*
 *	Allocate/initialize app incarnation and register it in proto apps.
 */
static int
ip_vs_app_inc_new(struct ip_vs_app *app, __u16 proto, __u16 port)
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

	ret = pp->register_app(inc);
	if (ret)
		goto out;

	list_add(&inc->a_list, &app->incs_list);
	IP_VS_DBG(9, "%s application %s:%u registered\n",
		  pp->name, inc->name, inc->port);

	return 0;

  out:
	kfree(inc->timeout_table);
	kfree(inc);
	return ret;
}


/*
 *	Release app incarnation
 */
static void
ip_vs_app_inc_release(struct ip_vs_app *inc)
{
	struct ip_vs_protocol *pp;

	if (!(pp = ip_vs_proto_get(inc->protocol)))
		return;

	if (pp->unregister_app)
		pp->unregister_app(inc);

	IP_VS_DBG(9, "%s App %s:%u unregistered\n",
		  pp->name, inc->name, inc->port);

	list_del(&inc->a_list);

	kfree(inc->timeout_table);
	kfree(inc);
}


/*
 *	Get reference to app inc (only called from softirq)
 *
 */
int ip_vs_app_inc_get(struct ip_vs_app *inc)
{
	int result;

	atomic_inc(&inc->usecnt);
	if (unlikely((result = ip_vs_app_get(inc->app)) != 1))
		atomic_dec(&inc->usecnt);
	return result;
}


/*
 *	Put the app inc (only called from timer or net softirq)
 */
void ip_vs_app_inc_put(struct ip_vs_app *inc)
{
	ip_vs_app_put(inc->app);
	atomic_dec(&inc->usecnt);
}


/*
 *	Register an application incarnation in protocol applications
 */
int
register_ip_vs_app_inc(struct ip_vs_app *app, __u16 proto, __u16 port)
{
	int result;

	mutex_lock(&__ip_vs_app_mutex);

	result = ip_vs_app_inc_new(app, proto, port);

	mutex_unlock(&__ip_vs_app_mutex);

	return result;
}


/*
 *	ip_vs_app registration routine
 */
int register_ip_vs_app(struct ip_vs_app *app)
{
	/* increase the module use count */
	ip_vs_use_count_inc();

	mutex_lock(&__ip_vs_app_mutex);

	list_add(&app->a_list, &ip_vs_app_list);

	mutex_unlock(&__ip_vs_app_mutex);

	return 0;
}


/*
 *	ip_vs_app unregistration routine
 *	We are sure there are no app incarnations attached to services
 */
void unregister_ip_vs_app(struct ip_vs_app *app)
{
	struct ip_vs_app *inc, *nxt;

	mutex_lock(&__ip_vs_app_mutex);

	list_for_each_entry_safe(inc, nxt, &app->incs_list, a_list) {
		ip_vs_app_inc_release(inc);
	}

	list_del(&app->a_list);

	mutex_unlock(&__ip_vs_app_mutex);

	/* decrease the module use count */
	ip_vs_use_count_dec();
}


/*
 *	Bind ip_vs_conn to its ip_vs_app (called by cp constructor)
 */
int ip_vs_bind_app(struct ip_vs_conn *cp, struct ip_vs_protocol *pp)
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
				 unsigned flag, __u32 seq, int diff)
{
	/* spinlock is to keep updating cp->flags atomic */
	spin_lock(&cp->lock);
	if (!(cp->flags & flag) || after(seq, vseq->init_seq)) {
		vseq->previous_delta = vseq->delta;
		vseq->delta += diff;
		vseq->init_seq = seq;
		cp->flags |= flag;
	}
	spin_unlock(&cp->lock);
}

static inline int app_tcp_pkt_out(struct ip_vs_conn *cp, struct sk_buff *skb,
				  struct ip_vs_app *app)
{
	int diff;
	const unsigned int tcp_offset = ip_hdrlen(skb);
	struct tcphdr *th;
	__u32 seq;

	if (!skb_make_writable(skb, tcp_offset + sizeof(*th)))
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

	if (!app->pkt_out(app, cp, skb, &diff))
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
int ip_vs_app_pkt_out(struct ip_vs_conn *cp, struct sk_buff *skb)
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
		return app_tcp_pkt_out(cp, skb, app);

	/*
	 *	Call private output hook function
	 */
	if (app->pkt_out == NULL)
		return 1;

	return app->pkt_out(app, cp, skb, NULL);
}


static inline int app_tcp_pkt_in(struct ip_vs_conn *cp, struct sk_buff *skb,
				 struct ip_vs_app *app)
{
	int diff;
	const unsigned int tcp_offset = ip_hdrlen(skb);
	struct tcphdr *th;
	__u32 seq;

	if (!skb_make_writable(skb, tcp_offset + sizeof(*th)))
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

	if (!app->pkt_in(app, cp, skb, &diff))
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
int ip_vs_app_pkt_in(struct ip_vs_conn *cp, struct sk_buff *skb)
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
		return app_tcp_pkt_in(cp, skb, app);

	/*
	 *	Call private input hook function
	 */
	if (app->pkt_in == NULL)
		return 1;

	return app->pkt_in(app, cp, skb, NULL);
}


#ifdef CONFIG_PROC_FS
/*
 *	/proc/net/ip_vs_app entry function
 */

static struct ip_vs_app *ip_vs_app_idx(loff_t pos)
{
	struct ip_vs_app *app, *inc;

	list_for_each_entry(app, &ip_vs_app_list, a_list) {
		list_for_each_entry(inc, &app->incs_list, a_list) {
			if (pos-- == 0)
				return inc;
		}
	}
	return NULL;

}

static void *ip_vs_app_seq_start(struct seq_file *seq, loff_t *pos)
{
	mutex_lock(&__ip_vs_app_mutex);

	return *pos ? ip_vs_app_idx(*pos - 1) : SEQ_START_TOKEN;
}

static void *ip_vs_app_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ip_vs_app *inc, *app;
	struct list_head *e;

	++*pos;
	if (v == SEQ_START_TOKEN)
		return ip_vs_app_idx(0);

	inc = v;
	app = inc->app;

	if ((e = inc->a_list.next) != &app->incs_list)
		return list_entry(e, struct ip_vs_app, a_list);

	/* go on to next application */
	for (e = app->a_list.next; e != &ip_vs_app_list; e = e->next) {
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

static int ip_vs_app_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ip_vs_app_seq_ops);
}

static const struct file_operations ip_vs_app_fops = {
	.owner	 = THIS_MODULE,
	.open	 = ip_vs_app_open,
	.read	 = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};
#endif


/*
 *	Replace a segment of data with a new segment
 */
int ip_vs_skb_replace(struct sk_buff *skb, gfp_t pri,
		      char *o_buf, int o_len, char *n_buf, int n_len)
{
	int diff;
	int o_offset;
	int o_left;

	EnterFunction(9);

	diff = n_len - o_len;
	o_offset = o_buf - (char *)skb->data;
	/* The length of left data after o_buf+o_len in the skb data */
	o_left = skb->len - (o_offset + o_len);

	if (diff <= 0) {
		memmove(o_buf + n_len, o_buf + o_len, o_left);
		memcpy(o_buf, n_buf, n_len);
		skb_trim(skb, skb->len + diff);
	} else if (diff <= skb_tailroom(skb)) {
		skb_put(skb, diff);
		memmove(o_buf + n_len, o_buf + o_len, o_left);
		memcpy(o_buf, n_buf, n_len);
	} else {
		if (pskb_expand_head(skb, skb_headroom(skb), diff, pri))
			return -ENOMEM;
		skb_put(skb, diff);
		memmove(skb->data + o_offset + n_len,
			skb->data + o_offset + o_len, o_left);
		skb_copy_to_linear_data_offset(skb, o_offset, n_buf, n_len);
	}

	/* must update the iph total length here */
	ip_hdr(skb)->tot_len = htons(skb->len);

	LeaveFunction(9);
	return 0;
}


int __init ip_vs_app_init(void)
{
	/* we will replace it with proc_net_ipvs_create() soon */
	proc_net_fops_create(&init_net, "ip_vs_app", 0, &ip_vs_app_fops);
	return 0;
}


void ip_vs_app_cleanup(void)
{
	proc_net_remove(&init_net, "ip_vs_app");
}
