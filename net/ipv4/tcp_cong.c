/*
 * Plugable TCP congestion control support and newReno
 * congestion control.
 * Based on ideas from I/O scheduler suport and Web100.
 *
 * Copyright (C) 2005 Stephen Hemminger <shemminger@osdl.org>
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/list.h>
#include <net/tcp.h>

static DEFINE_SPINLOCK(tcp_cong_list_lock);
static LIST_HEAD(tcp_cong_list);

/* Simple linear search, don't expect many entries! */
static struct tcp_congestion_ops *tcp_ca_find(const char *name)
{
	struct tcp_congestion_ops *e;

	list_for_each_entry_rcu(e, &tcp_cong_list, list) {
		if (strcmp(e->name, name) == 0)
			return e;
	}

	return NULL;
}

/*
 * Attach new congestion control algorthim to the list
 * of available options.
 */
int tcp_register_congestion_control(struct tcp_congestion_ops *ca)
{
	int ret = 0;

	/* all algorithms must implement ssthresh and cong_avoid ops */
	if (!ca->ssthresh || !ca->cong_avoid) {
		printk(KERN_ERR "TCP %s does not implement required ops\n",
		       ca->name);
		return -EINVAL;
	}

	spin_lock(&tcp_cong_list_lock);
	if (tcp_ca_find(ca->name)) {
		printk(KERN_NOTICE "TCP %s already registered\n", ca->name);
		ret = -EEXIST;
	} else {
		list_add_tail_rcu(&ca->list, &tcp_cong_list);
		printk(KERN_INFO "TCP %s registered\n", ca->name);
	}
	spin_unlock(&tcp_cong_list_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(tcp_register_congestion_control);

/*
 * Remove congestion control algorithm, called from
 * the module's remove function.  Module ref counts are used
 * to ensure that this can't be done till all sockets using
 * that method are closed.
 */
void tcp_unregister_congestion_control(struct tcp_congestion_ops *ca)
{
	spin_lock(&tcp_cong_list_lock);
	list_del_rcu(&ca->list);
	spin_unlock(&tcp_cong_list_lock);
}
EXPORT_SYMBOL_GPL(tcp_unregister_congestion_control);

/* Assign choice of congestion control. */
void tcp_init_congestion_control(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_congestion_ops *ca;

	if (icsk->icsk_ca_ops != &tcp_init_congestion_ops)
		return;

	rcu_read_lock();
	list_for_each_entry_rcu(ca, &tcp_cong_list, list) {
		if (try_module_get(ca->owner)) {
			icsk->icsk_ca_ops = ca;
			break;
		}

	}
	rcu_read_unlock();

	if (icsk->icsk_ca_ops->init)
		icsk->icsk_ca_ops->init(sk);
}

/* Manage refcounts on socket close. */
void tcp_cleanup_congestion_control(struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	if (icsk->icsk_ca_ops->release)
		icsk->icsk_ca_ops->release(sk);
	module_put(icsk->icsk_ca_ops->owner);
}

/* Used by sysctl to change default congestion control */
int tcp_set_default_congestion_control(const char *name)
{
	struct tcp_congestion_ops *ca;
	int ret = -ENOENT;

	spin_lock(&tcp_cong_list_lock);
	ca = tcp_ca_find(name);
#ifdef CONFIG_KMOD
	if (!ca && capable(CAP_SYS_MODULE)) {
		spin_unlock(&tcp_cong_list_lock);

		request_module("tcp_%s", name);
		spin_lock(&tcp_cong_list_lock);
		ca = tcp_ca_find(name);
	}
#endif

	if (ca) {
		ca->non_restricted = 1;	/* default is always allowed */
		list_move(&ca->list, &tcp_cong_list);
		ret = 0;
	}
	spin_unlock(&tcp_cong_list_lock);

	return ret;
}

/* Set default value from kernel configuration at bootup */
static int __init tcp_congestion_default(void)
{
	return tcp_set_default_congestion_control(CONFIG_DEFAULT_TCP_CONG);
}
late_initcall(tcp_congestion_default);


/* Build string with list of available congestion control values */
void tcp_get_available_congestion_control(char *buf, size_t maxlen)
{
	struct tcp_congestion_ops *ca;
	size_t offs = 0;

	rcu_read_lock();
	list_for_each_entry_rcu(ca, &tcp_cong_list, list) {
		offs += snprintf(buf + offs, maxlen - offs,
				 "%s%s",
				 offs == 0 ? "" : " ", ca->name);

	}
	rcu_read_unlock();
}

/* Get current default congestion control */
void tcp_get_default_congestion_control(char *name)
{
	struct tcp_congestion_ops *ca;
	/* We will always have reno... */
	BUG_ON(list_empty(&tcp_cong_list));

	rcu_read_lock();
	ca = list_entry(tcp_cong_list.next, struct tcp_congestion_ops, list);
	strncpy(name, ca->name, TCP_CA_NAME_MAX);
	rcu_read_unlock();
}

/* Built list of non-restricted congestion control values */
void tcp_get_allowed_congestion_control(char *buf, size_t maxlen)
{
	struct tcp_congestion_ops *ca;
	size_t offs = 0;

	*buf = '\0';
	rcu_read_lock();
	list_for_each_entry_rcu(ca, &tcp_cong_list, list) {
		if (!ca->non_restricted)
			continue;
		offs += snprintf(buf + offs, maxlen - offs,
				 "%s%s",
				 offs == 0 ? "" : " ", ca->name);

	}
	rcu_read_unlock();
}

/* Change list of non-restricted congestion control */
int tcp_set_allowed_congestion_control(char *val)
{
	struct tcp_congestion_ops *ca;
	char *clone, *name;
	int ret = 0;

	clone = kstrdup(val, GFP_USER);
	if (!clone)
		return -ENOMEM;

	spin_lock(&tcp_cong_list_lock);
	/* pass 1 check for bad entries */
	while ((name = strsep(&clone, " ")) && *name) {
		ca = tcp_ca_find(name);
		if (!ca) {
			ret = -ENOENT;
			goto out;
		}
	}

	/* pass 2 clear */
	list_for_each_entry_rcu(ca, &tcp_cong_list, list)
		ca->non_restricted = 0;

	/* pass 3 mark as allowed */
	while ((name = strsep(&val, " ")) && *name) {
		ca = tcp_ca_find(name);
		WARN_ON(!ca);
		if (ca)
			ca->non_restricted = 1;
	}
out:
	spin_unlock(&tcp_cong_list_lock);

	return ret;
}


/* Change congestion control for socket */
int tcp_set_congestion_control(struct sock *sk, const char *name)
{
	struct inet_connection_sock *icsk = inet_csk(sk);
	struct tcp_congestion_ops *ca;
	int err = 0;

	rcu_read_lock();
	ca = tcp_ca_find(name);
	/* no change asking for existing value */
	if (ca == icsk->icsk_ca_ops)
		goto out;

#ifdef CONFIG_KMOD
	/* not found attempt to autoload module */
	if (!ca && capable(CAP_SYS_MODULE)) {
		rcu_read_unlock();
		request_module("tcp_%s", name);
		rcu_read_lock();
		ca = tcp_ca_find(name);
	}
#endif
	if (!ca)
		err = -ENOENT;

	else if (!(ca->non_restricted || capable(CAP_NET_ADMIN)))
		err = -EPERM;

	else if (!try_module_get(ca->owner))
		err = -EBUSY;

	else {
		tcp_cleanup_congestion_control(sk);
		icsk->icsk_ca_ops = ca;
		if (icsk->icsk_ca_ops->init)
			icsk->icsk_ca_ops->init(sk);
	}
 out:
	rcu_read_unlock();
	return err;
}


/*
 * Linear increase during slow start
 */
void tcp_slow_start(struct tcp_sock *tp)
{
	if (sysctl_tcp_abc) {
		/* RFC3465: Slow Start
		 * TCP sender SHOULD increase cwnd by the number of
		 * previously unacknowledged bytes ACKed by each incoming
		 * acknowledgment, provided the increase is not more than L
		 */
		if (tp->bytes_acked < tp->mss_cache)
			return;

		/* We MAY increase by 2 if discovered delayed ack */
		if (sysctl_tcp_abc > 1 && tp->bytes_acked >= 2*tp->mss_cache) {
			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
				tp->snd_cwnd++;
		}
	}
	tp->bytes_acked = 0;

	if (tp->snd_cwnd < tp->snd_cwnd_clamp)
		tp->snd_cwnd++;
}
EXPORT_SYMBOL_GPL(tcp_slow_start);

/*
 * TCP Reno congestion control
 * This is special case used for fallback as well.
 */
/* This is Jacobson's slow start and congestion avoidance.
 * SIGCOMM '88, p. 328.
 */
void tcp_reno_cong_avoid(struct sock *sk, u32 ack, u32 rtt, u32 in_flight,
			 int flag)
{
	struct tcp_sock *tp = tcp_sk(sk);

	if (!tcp_is_cwnd_limited(sk, in_flight))
		return;

	/* In "safe" area, increase. */
        if (tp->snd_cwnd <= tp->snd_ssthresh)
		tcp_slow_start(tp);

 	/* In dangerous area, increase slowly. */
	else if (sysctl_tcp_abc) {
 		/* RFC3465: Appropriate Byte Count
 		 * increase once for each full cwnd acked
 		 */
 		if (tp->bytes_acked >= tp->snd_cwnd*tp->mss_cache) {
 			tp->bytes_acked -= tp->snd_cwnd*tp->mss_cache;
 			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
 				tp->snd_cwnd++;
 		}
 	} else {
 		/* In theory this is tp->snd_cwnd += 1 / tp->snd_cwnd */
 		if (tp->snd_cwnd_cnt >= tp->snd_cwnd) {
 			if (tp->snd_cwnd < tp->snd_cwnd_clamp)
 				tp->snd_cwnd++;
 			tp->snd_cwnd_cnt = 0;
 		} else
 			tp->snd_cwnd_cnt++;
 	}
}
EXPORT_SYMBOL_GPL(tcp_reno_cong_avoid);

/* Slow start threshold is half the congestion window (min 2) */
u32 tcp_reno_ssthresh(struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	return max(tp->snd_cwnd >> 1U, 2U);
}
EXPORT_SYMBOL_GPL(tcp_reno_ssthresh);

/* Lower bound on congestion window with halving. */
u32 tcp_reno_min_cwnd(const struct sock *sk)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	return tp->snd_ssthresh/2;
}
EXPORT_SYMBOL_GPL(tcp_reno_min_cwnd);

struct tcp_congestion_ops tcp_reno = {
	.name		= "reno",
	.non_restricted = 1,
	.owner		= THIS_MODULE,
	.ssthresh	= tcp_reno_ssthresh,
	.cong_avoid	= tcp_reno_cong_avoid,
	.min_cwnd	= tcp_reno_min_cwnd,
};

/* Initial congestion control used (until SYN)
 * really reno under another name so we can tell difference
 * during tcp_set_default_congestion_control
 */
struct tcp_congestion_ops tcp_init_congestion_ops  = {
	.name		= "",
	.owner		= THIS_MODULE,
	.ssthresh	= tcp_reno_ssthresh,
	.cong_avoid	= tcp_reno_cong_avoid,
	.min_cwnd	= tcp_reno_min_cwnd,
};
EXPORT_SYMBOL_GPL(tcp_init_congestion_ops);
