/*
 * netfilter module for userspace packet logging daemons
 *
 * (C) 2000-2004 by Harald Welte <laforge@netfilter.org>
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 * (C) 2005-2007 Patrick McHardy <kaber@trash.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This module accepts two parameters:
 *
 * nlbufsiz:
 *   The parameter specifies how big the buffer for each netlink multicast
 * group is. e.g. If you say nlbufsiz=8192, up to eight kb of packets will
 * get accumulated in the kernel until they are sent to userspace. It is
 * NOT possible to allocate more than 128kB, and it is strongly discouraged,
 * because atomically allocating 128kB inside the network rx softirq is not
 * reliable. Please also keep in mind that this buffer size is allocated for
 * each nlgroup you are using, so the total kernel memory usage increases
 * by that factor.
 *
 * Actually you should use nlbufsiz a bit smaller than PAGE_SIZE, since
 * nlbufsiz is used with alloc_skb, which adds another
 * sizeof(struct skb_shared_info).  Use NLMSG_GOODSIZE instead.
 *
 * flushtimeout:
 *   Specify, after how many hundredths of a second the queue should be
 *   flushed even if it is not full yet.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <net/netlink.h>
#include <linux/netdevice.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/netfilter.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ipt_ULOG.h>
#include <net/netfilter/nf_log.h>
#include <net/netns/generic.h>
#include <net/sock.h>
#include <linux/bitops.h>
#include <asm/unaligned.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("Xtables: packet logging to netlink using ULOG");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_NFLOG);

#define ULOG_NL_EVENT		111		/* Harald's favorite number */
#define ULOG_MAXNLGROUPS	32		/* numer of nlgroups */

static unsigned int nlbufsiz = NLMSG_GOODSIZE;
module_param(nlbufsiz, uint, 0400);
MODULE_PARM_DESC(nlbufsiz, "netlink buffer size");

static unsigned int flushtimeout = 10;
module_param(flushtimeout, uint, 0600);
MODULE_PARM_DESC(flushtimeout, "buffer flush timeout (hundredths of a second)");

static bool nflog = true;
module_param(nflog, bool, 0400);
MODULE_PARM_DESC(nflog, "register as internal netfilter logging module");

/* global data structures */

typedef struct {
	unsigned int qlen;		/* number of nlmsgs' in the skb */
	struct nlmsghdr *lastnlh;	/* netlink header of last msg in skb */
	struct sk_buff *skb;		/* the pre-allocated skb */
	struct timer_list timer;	/* the timer function */
} ulog_buff_t;

static int ulog_net_id __read_mostly;
struct ulog_net {
	unsigned int nlgroup[ULOG_MAXNLGROUPS];
	ulog_buff_t ulog_buffers[ULOG_MAXNLGROUPS];
	struct sock *nflognl;
	spinlock_t lock;
};

static struct ulog_net *ulog_pernet(struct net *net)
{
	return net_generic(net, ulog_net_id);
}

/* send one ulog_buff_t to userspace */
static void ulog_send(struct ulog_net *ulog, unsigned int nlgroupnum)
{
	ulog_buff_t *ub = &ulog->ulog_buffers[nlgroupnum];

	pr_debug("ulog_send: timer is deleting\n");
	del_timer(&ub->timer);

	if (!ub->skb) {
		pr_debug("ulog_send: nothing to send\n");
		return;
	}

	/* last nlmsg needs NLMSG_DONE */
	if (ub->qlen > 1)
		ub->lastnlh->nlmsg_type = NLMSG_DONE;

	NETLINK_CB(ub->skb).dst_group = nlgroupnum + 1;
	pr_debug("throwing %d packets to netlink group %u\n",
		 ub->qlen, nlgroupnum + 1);
	netlink_broadcast(ulog->nflognl, ub->skb, 0, nlgroupnum + 1,
			  GFP_ATOMIC);

	ub->qlen = 0;
	ub->skb = NULL;
	ub->lastnlh = NULL;
}


/* timer function to flush queue in flushtimeout time */
static void ulog_timer(unsigned long data)
{
	struct ulog_net *ulog = container_of((void *)data,
					     struct ulog_net,
					     nlgroup[*(unsigned int *)data]);
	pr_debug("timer function called, calling ulog_send\n");

	/* lock to protect against somebody modifying our structure
	 * from ipt_ulog_target at the same time */
	spin_lock_bh(&ulog->lock);
	ulog_send(ulog, data);
	spin_unlock_bh(&ulog->lock);
}

static struct sk_buff *ulog_alloc_skb(unsigned int size)
{
	struct sk_buff *skb;
	unsigned int n;

	/* alloc skb which should be big enough for a whole
	 * multipart message. WARNING: has to be <= 131000
	 * due to slab allocator restrictions */

	n = max(size, nlbufsiz);
	skb = alloc_skb(n, GFP_ATOMIC | __GFP_NOWARN);
	if (!skb) {
		if (n > size) {
			/* try to allocate only as much as we need for
			 * current packet */

			skb = alloc_skb(size, GFP_ATOMIC);
			if (!skb)
				pr_debug("cannot even allocate %ub\n", size);
		}
	}

	return skb;
}

static void ipt_ulog_packet(unsigned int hooknum,
			    const struct sk_buff *skb,
			    const struct net_device *in,
			    const struct net_device *out,
			    const struct ipt_ulog_info *loginfo,
			    const char *prefix)
{
	ulog_buff_t *ub;
	ulog_packet_msg_t *pm;
	size_t size, copy_len;
	struct nlmsghdr *nlh;
	struct timeval tv;
	struct net *net = dev_net(in ? in : out);
	struct ulog_net *ulog = ulog_pernet(net);

	/* ffs == find first bit set, necessary because userspace
	 * is already shifting groupnumber, but we need unshifted.
	 * ffs() returns [1..32], we need [0..31] */
	unsigned int groupnum = ffs(loginfo->nl_group) - 1;

	/* calculate the size of the skb needed */
	if (loginfo->copy_range == 0 || loginfo->copy_range > skb->len)
		copy_len = skb->len;
	else
		copy_len = loginfo->copy_range;

	size = nlmsg_total_size(sizeof(*pm) + copy_len);

	ub = &ulog->ulog_buffers[groupnum];

	spin_lock_bh(&ulog->lock);

	if (!ub->skb) {
		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	} else if (ub->qlen >= loginfo->qthreshold ||
		   size > skb_tailroom(ub->skb)) {
		/* either the queue len is too high or we don't have
		 * enough room in nlskb left. send it to userspace. */

		ulog_send(ulog, groupnum);

		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	}

	pr_debug("qlen %d, qthreshold %Zu\n", ub->qlen, loginfo->qthreshold);

	nlh = nlmsg_put(ub->skb, 0, ub->qlen, ULOG_NL_EVENT,
			sizeof(*pm)+copy_len, 0);
	if (!nlh) {
		pr_debug("error during nlmsg_put\n");
		goto out_unlock;
	}
	ub->qlen++;

	pm = nlmsg_data(nlh);

	/* We might not have a timestamp, get one */
	if (skb->tstamp.tv64 == 0)
		__net_timestamp((struct sk_buff *)skb);

	/* copy hook, prefix, timestamp, payload, etc. */
	pm->data_len = copy_len;
	tv = ktime_to_timeval(skb->tstamp);
	put_unaligned(tv.tv_sec, &pm->timestamp_sec);
	put_unaligned(tv.tv_usec, &pm->timestamp_usec);
	put_unaligned(skb->mark, &pm->mark);
	pm->hook = hooknum;
	if (prefix != NULL)
		strncpy(pm->prefix, prefix, sizeof(pm->prefix));
	else if (loginfo->prefix[0] != '\0')
		strncpy(pm->prefix, loginfo->prefix, sizeof(pm->prefix));
	else
		*(pm->prefix) = '\0';

	if (in && in->hard_header_len > 0 &&
	    skb->mac_header != skb->network_header &&
	    in->hard_header_len <= ULOG_MAC_LEN) {
		memcpy(pm->mac, skb_mac_header(skb), in->hard_header_len);
		pm->mac_len = in->hard_header_len;
	} else
		pm->mac_len = 0;

	if (in)
		strncpy(pm->indev_name, in->name, sizeof(pm->indev_name));
	else
		pm->indev_name[0] = '\0';

	if (out)
		strncpy(pm->outdev_name, out->name, sizeof(pm->outdev_name));
	else
		pm->outdev_name[0] = '\0';

	/* copy_len <= skb->len, so can't fail. */
	if (skb_copy_bits(skb, 0, pm->payload, copy_len) < 0)
		BUG();

	/* check if we are building multi-part messages */
	if (ub->qlen > 1)
		ub->lastnlh->nlmsg_flags |= NLM_F_MULTI;

	ub->lastnlh = nlh;

	/* if timer isn't already running, start it */
	if (!timer_pending(&ub->timer)) {
		ub->timer.expires = jiffies + flushtimeout * HZ / 100;
		add_timer(&ub->timer);
	}

	/* if threshold is reached, send message to userspace */
	if (ub->qlen >= loginfo->qthreshold) {
		if (loginfo->qthreshold > 1)
			nlh->nlmsg_type = NLMSG_DONE;
		ulog_send(ulog, groupnum);
	}
out_unlock:
	spin_unlock_bh(&ulog->lock);

	return;

alloc_failure:
	pr_debug("Error building netlink message\n");
	spin_unlock_bh(&ulog->lock);
}

static unsigned int
ulog_tg(struct sk_buff *skb, const struct xt_action_param *par)
{
	ipt_ulog_packet(par->hooknum, skb, par->in, par->out,
	                par->targinfo, NULL);
	return XT_CONTINUE;
}

static void ipt_logfn(u_int8_t pf,
		      unsigned int hooknum,
		      const struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      const struct nf_loginfo *li,
		      const char *prefix)
{
	struct ipt_ulog_info loginfo;

	if (!li || li->type != NF_LOG_TYPE_ULOG) {
		loginfo.nl_group = ULOG_DEFAULT_NLGROUP;
		loginfo.copy_range = 0;
		loginfo.qthreshold = ULOG_DEFAULT_QTHRESHOLD;
		loginfo.prefix[0] = '\0';
	} else {
		loginfo.nl_group = li->u.ulog.group;
		loginfo.copy_range = li->u.ulog.copy_len;
		loginfo.qthreshold = li->u.ulog.qthreshold;
		strlcpy(loginfo.prefix, prefix, sizeof(loginfo.prefix));
	}

	ipt_ulog_packet(hooknum, skb, in, out, &loginfo, prefix);
}

static int ulog_tg_check(const struct xt_tgchk_param *par)
{
	const struct ipt_ulog_info *loginfo = par->targinfo;

	if (!par->net->xt.ulog_warn_deprecated) {
		pr_info("ULOG is deprecated and it will be removed soon, "
			"use NFLOG instead\n");
		par->net->xt.ulog_warn_deprecated = true;
	}

	if (loginfo->prefix[sizeof(loginfo->prefix) - 1] != '\0') {
		pr_debug("prefix not null-terminated\n");
		return -EINVAL;
	}
	if (loginfo->qthreshold > ULOG_MAX_QLEN) {
		pr_debug("queue threshold %Zu > MAX_QLEN\n",
			 loginfo->qthreshold);
		return -EINVAL;
	}
	return 0;
}

#ifdef CONFIG_COMPAT
struct compat_ipt_ulog_info {
	compat_uint_t	nl_group;
	compat_size_t	copy_range;
	compat_size_t	qthreshold;
	char		prefix[ULOG_PREFIX_LEN];
};

static void ulog_tg_compat_from_user(void *dst, const void *src)
{
	const struct compat_ipt_ulog_info *cl = src;
	struct ipt_ulog_info l = {
		.nl_group	= cl->nl_group,
		.copy_range	= cl->copy_range,
		.qthreshold	= cl->qthreshold,
	};

	memcpy(l.prefix, cl->prefix, sizeof(l.prefix));
	memcpy(dst, &l, sizeof(l));
}

static int ulog_tg_compat_to_user(void __user *dst, const void *src)
{
	const struct ipt_ulog_info *l = src;
	struct compat_ipt_ulog_info cl = {
		.nl_group	= l->nl_group,
		.copy_range	= l->copy_range,
		.qthreshold	= l->qthreshold,
	};

	memcpy(cl.prefix, l->prefix, sizeof(cl.prefix));
	return copy_to_user(dst, &cl, sizeof(cl)) ? -EFAULT : 0;
}
#endif /* CONFIG_COMPAT */

static struct xt_target ulog_tg_reg __read_mostly = {
	.name		= "ULOG",
	.family		= NFPROTO_IPV4,
	.target		= ulog_tg,
	.targetsize	= sizeof(struct ipt_ulog_info),
	.checkentry	= ulog_tg_check,
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(struct compat_ipt_ulog_info),
	.compat_from_user = ulog_tg_compat_from_user,
	.compat_to_user	= ulog_tg_compat_to_user,
#endif
	.me		= THIS_MODULE,
};

static struct nf_logger ipt_ulog_logger __read_mostly = {
	.name		= "ipt_ULOG",
	.logfn		= ipt_logfn,
	.me		= THIS_MODULE,
};

static int __net_init ulog_tg_net_init(struct net *net)
{
	int i;
	struct ulog_net *ulog = ulog_pernet(net);
	struct netlink_kernel_cfg cfg = {
		.groups	= ULOG_MAXNLGROUPS,
	};

	spin_lock_init(&ulog->lock);
	/* initialize ulog_buffers */
	for (i = 0; i < ULOG_MAXNLGROUPS; i++)
		setup_timer(&ulog->ulog_buffers[i].timer, ulog_timer, i);

	ulog->nflognl = netlink_kernel_create(net, NETLINK_NFLOG, &cfg);
	if (!ulog->nflognl)
		return -ENOMEM;

	if (nflog)
		nf_log_set(net, NFPROTO_IPV4, &ipt_ulog_logger);

	return 0;
}

static void __net_exit ulog_tg_net_exit(struct net *net)
{
	ulog_buff_t *ub;
	int i;
	struct ulog_net *ulog = ulog_pernet(net);

	if (nflog)
		nf_log_unset(net, &ipt_ulog_logger);

	netlink_kernel_release(ulog->nflognl);

	/* remove pending timers and free allocated skb's */
	for (i = 0; i < ULOG_MAXNLGROUPS; i++) {
		ub = &ulog->ulog_buffers[i];
		pr_debug("timer is deleting\n");
		del_timer(&ub->timer);

		if (ub->skb) {
			kfree_skb(ub->skb);
			ub->skb = NULL;
		}
	}
}

static struct pernet_operations ulog_tg_net_ops = {
	.init = ulog_tg_net_init,
	.exit = ulog_tg_net_exit,
	.id   = &ulog_net_id,
	.size = sizeof(struct ulog_net),
};

static int __init ulog_tg_init(void)
{
	int ret;
	pr_debug("init module\n");

	if (nlbufsiz > 128*1024) {
		pr_warn("Netlink buffer has to be <= 128kB\n");
		return -EINVAL;
	}

	ret = register_pernet_subsys(&ulog_tg_net_ops);
	if (ret)
		goto out_pernet;

	ret = xt_register_target(&ulog_tg_reg);
	if (ret < 0)
		goto out_target;

	if (nflog)
		nf_log_register(NFPROTO_IPV4, &ipt_ulog_logger);

	return 0;

out_target:
	unregister_pernet_subsys(&ulog_tg_net_ops);
out_pernet:
	return ret;
}

static void __exit ulog_tg_exit(void)
{
	pr_debug("cleanup_module\n");
	if (nflog)
		nf_log_unregister(&ipt_ulog_logger);
	xt_unregister_target(&ulog_tg_reg);
	unregister_pernet_subsys(&ulog_tg_net_ops);
}

module_init(ulog_tg_init);
module_exit(ulog_tg_exit);
