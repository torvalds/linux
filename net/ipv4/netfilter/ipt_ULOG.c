/*
 * netfilter module for userspace packet logging daemons
 *
 * (C) 2000-2004 by Harald Welte <laforge@netfilter.org>
 *
 * 2000/09/22 ulog-cprange feature added
 * 2001/01/04 in-kernel queue as proposed by Sebastian Zander 
 * 						<zander@fokus.gmd.de>
 * 2001/01/30 per-rule nlgroup conflicts with global queue. 
 *            nlgroup now global (sysctl)
 * 2001/04/19 ulog-queue reworked, now fixed buffer size specified at
 * 	      module loadtime -HW
 * 2002/07/07 remove broken nflog_rcv() function -HW
 * 2002/08/29 fix shifted/unshifted nlgroup bug -HW
 * 2002/10/30 fix uninitialized mac_len field - <Anders K. Pedersen>
 * 2004/10/25 fix erroneous calculation of 'len' parameter to NLMSG_PUT
 *	      resulting in bogus 'error during NLMSG_PUT' messages.
 *
 * (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
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
 * flushtimeout:
 *   Specify, after how many hundredths of a second the queue should be
 *   flushed even if it is not full yet.
 *
 * ipt_ULOG.c,v 1.22 2002/10/30 09:07:31 laforge Exp
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/netlink.h>
#include <linux/netdevice.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_ULOG.h>
#include <net/sock.h>
#include <linux/bitops.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("iptables userspace logging module");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_NFLOG);

#define ULOG_NL_EVENT		111		/* Harald's favorite number */
#define ULOG_MAXNLGROUPS	32		/* numer of nlgroups */

#if 0
#define DEBUGP(format, args...) printk("%s:%s:" format, \
                                       __FILE__, __FUNCTION__ , ## args)
#else
#define DEBUGP(format, args...)
#endif

#define PRINTR(format, args...) do { if (net_ratelimit()) printk(format , ## args); } while (0)

static unsigned int nlbufsiz = 4096;
module_param(nlbufsiz, uint, 0600); /* FIXME: Check size < 128k --RR */
MODULE_PARM_DESC(nlbufsiz, "netlink buffer size");

static unsigned int flushtimeout = 10;
module_param(flushtimeout, int, 0600);
MODULE_PARM_DESC(flushtimeout, "buffer flush timeout (hundredths of a second)");

static unsigned int nflog = 1;
module_param(nflog, int, 0400);
MODULE_PARM_DESC(nflog, "register as internal netfilter logging module");

/* global data structures */

typedef struct {
	unsigned int qlen;		/* number of nlmsgs' in the skb */
	struct nlmsghdr *lastnlh;	/* netlink header of last msg in skb */
	struct sk_buff *skb;		/* the pre-allocated skb */
	struct timer_list timer;	/* the timer function */
} ulog_buff_t;

static ulog_buff_t ulog_buffers[ULOG_MAXNLGROUPS];	/* array of buffers */

static struct sock *nflognl;		/* our socket */
static DEFINE_SPINLOCK(ulog_lock);	/* spinlock */

/* send one ulog_buff_t to userspace */
static void ulog_send(unsigned int nlgroupnum)
{
	ulog_buff_t *ub = &ulog_buffers[nlgroupnum];

	if (timer_pending(&ub->timer)) {
		DEBUGP("ipt_ULOG: ulog_send: timer was pending, deleting\n");
		del_timer(&ub->timer);
	}

	/* last nlmsg needs NLMSG_DONE */
	if (ub->qlen > 1)
		ub->lastnlh->nlmsg_type = NLMSG_DONE;

	NETLINK_CB(ub->skb).dst_group = nlgroupnum + 1;
	DEBUGP("ipt_ULOG: throwing %d packets to netlink group %u\n",
		ub->qlen, nlgroupnum + 1);
	netlink_broadcast(nflognl, ub->skb, 0, nlgroupnum + 1, GFP_ATOMIC);

	ub->qlen = 0;
	ub->skb = NULL;
	ub->lastnlh = NULL;

}


/* timer function to flush queue in flushtimeout time */
static void ulog_timer(unsigned long data)
{
	DEBUGP("ipt_ULOG: timer function called, calling ulog_send\n");

	/* lock to protect against somebody modifying our structure
	 * from ipt_ulog_target at the same time */
	spin_lock_bh(&ulog_lock);
	ulog_send(data);
	spin_unlock_bh(&ulog_lock);
}

static struct sk_buff *ulog_alloc_skb(unsigned int size)
{
	struct sk_buff *skb;

	/* alloc skb which should be big enough for a whole
	 * multipart message. WARNING: has to be <= 131000
	 * due to slab allocator restrictions */

	skb = alloc_skb(nlbufsiz, GFP_ATOMIC);
	if (!skb) {
		PRINTR("ipt_ULOG: can't alloc whole buffer %ub!\n",
			nlbufsiz);

		/* try to allocate only as much as we need for 
		 * current packet */

		skb = alloc_skb(size, GFP_ATOMIC);
		if (!skb)
			PRINTR("ipt_ULOG: can't even allocate %ub\n", size);
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

	/* ffs == find first bit set, necessary because userspace
	 * is already shifting groupnumber, but we need unshifted.
	 * ffs() returns [1..32], we need [0..31] */
	unsigned int groupnum = ffs(loginfo->nl_group) - 1;

	/* calculate the size of the skb needed */
	if ((loginfo->copy_range == 0) ||
	    (loginfo->copy_range > skb->len)) {
		copy_len = skb->len;
	} else {
		copy_len = loginfo->copy_range;
	}

	size = NLMSG_SPACE(sizeof(*pm) + copy_len);

	ub = &ulog_buffers[groupnum];
	
	spin_lock_bh(&ulog_lock);

	if (!ub->skb) {
		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	} else if (ub->qlen >= loginfo->qthreshold ||
		   size > skb_tailroom(ub->skb)) {
		/* either the queue len is too high or we don't have 
		 * enough room in nlskb left. send it to userspace. */

		ulog_send(groupnum);

		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	}

	DEBUGP("ipt_ULOG: qlen %d, qthreshold %d\n", ub->qlen, 
		loginfo->qthreshold);

	/* NLMSG_PUT contains a hidden goto nlmsg_failure !!! */
	nlh = NLMSG_PUT(ub->skb, 0, ub->qlen, ULOG_NL_EVENT, 
			sizeof(*pm)+copy_len);
	ub->qlen++;

	pm = NLMSG_DATA(nlh);

	/* We might not have a timestamp, get one */
	if (skb->tstamp.off_sec == 0)
		__net_timestamp((struct sk_buff *)skb);

	/* copy hook, prefix, timestamp, payload, etc. */
	pm->data_len = copy_len;
	pm->timestamp_sec = skb->tstamp.off_sec;
	pm->timestamp_usec = skb->tstamp.off_usec;
	pm->mark = skb->nfmark;
	pm->hook = hooknum;
	if (prefix != NULL)
		strncpy(pm->prefix, prefix, sizeof(pm->prefix));
	else if (loginfo->prefix[0] != '\0')
		strncpy(pm->prefix, loginfo->prefix, sizeof(pm->prefix));
	else
		*(pm->prefix) = '\0';

	if (in && in->hard_header_len > 0
	    && skb->mac.raw != (void *) skb->nh.iph
	    && in->hard_header_len <= ULOG_MAC_LEN) {
		memcpy(pm->mac, skb->mac.raw, in->hard_header_len);
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
	if (ub->qlen > 1) {
		ub->lastnlh->nlmsg_flags |= NLM_F_MULTI;
	}

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
		ulog_send(groupnum);
	}

	spin_unlock_bh(&ulog_lock);

	return;

nlmsg_failure:
	PRINTR("ipt_ULOG: error during NLMSG_PUT\n");

alloc_failure:
	PRINTR("ipt_ULOG: Error building netlink message\n");

	spin_unlock_bh(&ulog_lock);
}

static unsigned int ipt_ulog_target(struct sk_buff **pskb,
				    const struct net_device *in,
				    const struct net_device *out,
				    unsigned int hooknum,
				    const void *targinfo, void *userinfo)
{
	struct ipt_ulog_info *loginfo = (struct ipt_ulog_info *) targinfo;

	ipt_ulog_packet(hooknum, *pskb, in, out, loginfo, NULL);
 
 	return IPT_CONTINUE;
}
 
static void ipt_logfn(unsigned int pf,
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

static int ipt_ulog_checkentry(const char *tablename,
			       const struct ipt_entry *e,
			       void *targinfo,
			       unsigned int targinfosize,
			       unsigned int hookmask)
{
	struct ipt_ulog_info *loginfo = (struct ipt_ulog_info *) targinfo;

	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_ulog_info))) {
		DEBUGP("ipt_ULOG: targinfosize %u != 0\n", targinfosize);
		return 0;
	}

	if (loginfo->prefix[sizeof(loginfo->prefix) - 1] != '\0') {
		DEBUGP("ipt_ULOG: prefix term %i\n",
		       loginfo->prefix[sizeof(loginfo->prefix) - 1]);
		return 0;
	}

	if (loginfo->qthreshold > ULOG_MAX_QLEN) {
		DEBUGP("ipt_ULOG: queue threshold %i > MAX_QLEN\n",
			loginfo->qthreshold);
		return 0;
	}

	return 1;
}

static struct ipt_target ipt_ulog_reg = {
	.name		= "ULOG",
	.target		= ipt_ulog_target,
	.checkentry	= ipt_ulog_checkentry,
	.me		= THIS_MODULE,
};

static struct nf_logger ipt_ulog_logger = {
	.name		= "ipt_ULOG",
	.logfn		= &ipt_logfn,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int i;

	DEBUGP("ipt_ULOG: init module\n");

	if (nlbufsiz >= 128*1024) {
		printk("Netlink buffer has to be <= 128kB\n");
		return -EINVAL;
	}

	/* initialize ulog_buffers */
	for (i = 0; i < ULOG_MAXNLGROUPS; i++) {
		init_timer(&ulog_buffers[i].timer);
		ulog_buffers[i].timer.function = ulog_timer;
		ulog_buffers[i].timer.data = i;
	}

	nflognl = netlink_kernel_create(NETLINK_NFLOG, ULOG_MAXNLGROUPS, NULL,
	                                THIS_MODULE);
	if (!nflognl)
		return -ENOMEM;

	if (ipt_register_target(&ipt_ulog_reg) != 0) {
		sock_release(nflognl->sk_socket);
		return -EINVAL;
	}
	if (nflog)
		nf_log_register(PF_INET, &ipt_ulog_logger);
	
	return 0;
}

static void __exit fini(void)
{
	ulog_buff_t *ub;
	int i;

	DEBUGP("ipt_ULOG: cleanup_module\n");

	if (nflog)
		nf_log_unregister_logger(&ipt_ulog_logger);
	ipt_unregister_target(&ipt_ulog_reg);
	sock_release(nflognl->sk_socket);

	/* remove pending timers and free allocated skb's */
	for (i = 0; i < ULOG_MAXNLGROUPS; i++) {
		ub = &ulog_buffers[i];
		if (timer_pending(&ub->timer)) {
			DEBUGP("timer was pending, deleting\n");
			del_timer(&ub->timer);
		}

		if (ub->skb) {
			kfree_skb(ub->skb);
			ub->skb = NULL;
		}
	}

}

module_init(init);
module_exit(fini);
