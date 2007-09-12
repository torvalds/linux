/*
 * netfilter module for userspace bridged Ethernet frames logging daemons
 *
 *	Authors:
 *	Bart De Schuymer <bdschuym@pandora.be>
 *	Harald Welte <laforge@netfilter.org>
 *
 *  November, 2004
 *
 * Based on ipt_ULOG.c, which is
 * (C) 2000-2002 by Harald Welte <laforge@netfilter.org>
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
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/netlink.h>
#include <linux/netdevice.h>
#include <linux/netfilter_bridge/ebtables.h>
#include <linux/netfilter_bridge/ebt_ulog.h>
#include <net/sock.h>
#include "../br_private.h"

#define PRINTR(format, args...) do { if (net_ratelimit()) \
				printk(format , ## args); } while (0)

static unsigned int nlbufsiz = NLMSG_GOODSIZE;
module_param(nlbufsiz, uint, 0600);
MODULE_PARM_DESC(nlbufsiz, "netlink buffer size (number of bytes) "
			   "(defaults to 4096)");

static unsigned int flushtimeout = 10;
module_param(flushtimeout, uint, 0600);
MODULE_PARM_DESC(flushtimeout, "buffer flush timeout (hundredths ofa second) "
			       "(defaults to 10)");

typedef struct {
	unsigned int qlen;		/* number of nlmsgs' in the skb */
	struct nlmsghdr *lastnlh;	/* netlink header of last msg in skb */
	struct sk_buff *skb;		/* the pre-allocated skb */
	struct timer_list timer;	/* the timer function */
	spinlock_t lock;		/* the per-queue lock */
} ebt_ulog_buff_t;

static ebt_ulog_buff_t ulog_buffers[EBT_ULOG_MAXNLGROUPS];
static struct sock *ebtulognl;

/* send one ulog_buff_t to userspace */
static void ulog_send(unsigned int nlgroup)
{
	ebt_ulog_buff_t *ub = &ulog_buffers[nlgroup];

	if (timer_pending(&ub->timer))
		del_timer(&ub->timer);

	if (!ub->skb)
		return;

	/* last nlmsg needs NLMSG_DONE */
	if (ub->qlen > 1)
		ub->lastnlh->nlmsg_type = NLMSG_DONE;

	NETLINK_CB(ub->skb).dst_group = nlgroup + 1;
	netlink_broadcast(ebtulognl, ub->skb, 0, nlgroup + 1, GFP_ATOMIC);

	ub->qlen = 0;
	ub->skb = NULL;
}

/* timer function to flush queue in flushtimeout time */
static void ulog_timer(unsigned long data)
{
	spin_lock_bh(&ulog_buffers[data].lock);
	if (ulog_buffers[data].skb)
		ulog_send(data);
	spin_unlock_bh(&ulog_buffers[data].lock);
}

static struct sk_buff *ulog_alloc_skb(unsigned int size)
{
	struct sk_buff *skb;
	unsigned int n;

	n = max(size, nlbufsiz);
	skb = alloc_skb(n, GFP_ATOMIC);
	if (!skb) {
		PRINTR(KERN_ERR "ebt_ulog: can't alloc whole buffer "
		       "of size %ub!\n", n);
		if (n > size) {
			/* try to allocate only as much as we need for
			 * current packet */
			skb = alloc_skb(size, GFP_ATOMIC);
			if (!skb)
				PRINTR(KERN_ERR "ebt_ulog: can't even allocate "
				       "buffer of size %ub\n", size);
		}
	}

	return skb;
}

static void ebt_ulog_packet(unsigned int hooknr, const struct sk_buff *skb,
   const struct net_device *in, const struct net_device *out,
   const struct ebt_ulog_info *uloginfo, const char *prefix)
{
	ebt_ulog_packet_msg_t *pm;
	size_t size, copy_len;
	struct nlmsghdr *nlh;
	unsigned int group = uloginfo->nlgroup;
	ebt_ulog_buff_t *ub = &ulog_buffers[group];
	spinlock_t *lock = &ub->lock;
	ktime_t kt;

	if ((uloginfo->cprange == 0) ||
	    (uloginfo->cprange > skb->len + ETH_HLEN))
		copy_len = skb->len + ETH_HLEN;
	else
		copy_len = uloginfo->cprange;

	size = NLMSG_SPACE(sizeof(*pm) + copy_len);
	if (size > nlbufsiz) {
		PRINTR("ebt_ulog: Size %Zd needed, but nlbufsiz=%d\n",
		       size, nlbufsiz);
		return;
	}

	spin_lock_bh(lock);

	if (!ub->skb) {
		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	} else if (size > skb_tailroom(ub->skb)) {
		ulog_send(group);

		if (!(ub->skb = ulog_alloc_skb(size)))
			goto alloc_failure;
	}

	nlh = NLMSG_PUT(ub->skb, 0, ub->qlen, 0,
			size - NLMSG_ALIGN(sizeof(*nlh)));
	ub->qlen++;

	pm = NLMSG_DATA(nlh);

	/* Fill in the ulog data */
	pm->version = EBT_ULOG_VERSION;
	kt = ktime_get_real();
	pm->stamp = ktime_to_timeval(kt);
	if (ub->qlen == 1)
		ub->skb->tstamp = kt;
	pm->data_len = copy_len;
	pm->mark = skb->mark;
	pm->hook = hooknr;
	if (uloginfo->prefix != NULL)
		strcpy(pm->prefix, uloginfo->prefix);
	else
		*(pm->prefix) = '\0';

	if (in) {
		strcpy(pm->physindev, in->name);
		/* If in isn't a bridge, then physindev==indev */
		if (in->br_port)
			strcpy(pm->indev, in->br_port->br->dev->name);
		else
			strcpy(pm->indev, in->name);
	} else
		pm->indev[0] = pm->physindev[0] = '\0';

	if (out) {
		/* If out exists, then out is a bridge port */
		strcpy(pm->physoutdev, out->name);
		strcpy(pm->outdev, out->br_port->br->dev->name);
	} else
		pm->outdev[0] = pm->physoutdev[0] = '\0';

	if (skb_copy_bits(skb, -ETH_HLEN, pm->data, copy_len) < 0)
		BUG();

	if (ub->qlen > 1)
		ub->lastnlh->nlmsg_flags |= NLM_F_MULTI;

	ub->lastnlh = nlh;

	if (ub->qlen >= uloginfo->qthreshold)
		ulog_send(group);
	else if (!timer_pending(&ub->timer)) {
		ub->timer.expires = jiffies + flushtimeout * HZ / 100;
		add_timer(&ub->timer);
	}

unlock:
	spin_unlock_bh(lock);

	return;

nlmsg_failure:
	printk(KERN_CRIT "ebt_ulog: error during NLMSG_PUT. This should "
	       "not happen, please report to author.\n");
	goto unlock;
alloc_failure:
	goto unlock;
}

/* this function is registered with the netfilter core */
static void ebt_log_packet(unsigned int pf, unsigned int hooknum,
   const struct sk_buff *skb, const struct net_device *in,
   const struct net_device *out, const struct nf_loginfo *li,
   const char *prefix)
{
	struct ebt_ulog_info loginfo;

	if (!li || li->type != NF_LOG_TYPE_ULOG) {
		loginfo.nlgroup = EBT_ULOG_DEFAULT_NLGROUP;
		loginfo.cprange = 0;
		loginfo.qthreshold = EBT_ULOG_DEFAULT_QTHRESHOLD;
		loginfo.prefix[0] = '\0';
	} else {
		loginfo.nlgroup = li->u.ulog.group;
		loginfo.cprange = li->u.ulog.copy_len;
		loginfo.qthreshold = li->u.ulog.qthreshold;
		strlcpy(loginfo.prefix, prefix, sizeof(loginfo.prefix));
	}

	ebt_ulog_packet(hooknum, skb, in, out, &loginfo, prefix);
}

static void ebt_ulog(const struct sk_buff *skb, unsigned int hooknr,
   const struct net_device *in, const struct net_device *out,
   const void *data, unsigned int datalen)
{
	struct ebt_ulog_info *uloginfo = (struct ebt_ulog_info *)data;

	ebt_ulog_packet(hooknr, skb, in, out, uloginfo, NULL);
}


static int ebt_ulog_check(const char *tablename, unsigned int hookmask,
   const struct ebt_entry *e, void *data, unsigned int datalen)
{
	struct ebt_ulog_info *uloginfo = (struct ebt_ulog_info *)data;

	if (datalen != EBT_ALIGN(sizeof(struct ebt_ulog_info)) ||
	    uloginfo->nlgroup > 31)
		return -EINVAL;

	uloginfo->prefix[EBT_ULOG_PREFIX_LEN - 1] = '\0';

	if (uloginfo->qthreshold > EBT_ULOG_MAX_QLEN)
		uloginfo->qthreshold = EBT_ULOG_MAX_QLEN;

	return 0;
}

static struct ebt_watcher ulog = {
	.name		= EBT_ULOG_WATCHER,
	.watcher	= ebt_ulog,
	.check		= ebt_ulog_check,
	.me		= THIS_MODULE,
};

static struct nf_logger ebt_ulog_logger = {
	.name		= EBT_ULOG_WATCHER,
	.logfn		= &ebt_log_packet,
	.me		= THIS_MODULE,
};

static int __init ebt_ulog_init(void)
{
	int i, ret = 0;

	if (nlbufsiz >= 128*1024) {
		printk(KERN_NOTICE "ebt_ulog: Netlink buffer has to be <= 128kB,"
		       " please try a smaller nlbufsiz parameter.\n");
		return -EINVAL;
	}

	/* initialize ulog_buffers */
	for (i = 0; i < EBT_ULOG_MAXNLGROUPS; i++) {
		setup_timer(&ulog_buffers[i].timer, ulog_timer, i);
		spin_lock_init(&ulog_buffers[i].lock);
	}

	ebtulognl = netlink_kernel_create(&init_net, NETLINK_NFLOG,
					  EBT_ULOG_MAXNLGROUPS, NULL, NULL,
					  THIS_MODULE);
	if (!ebtulognl)
		ret = -ENOMEM;
	else if ((ret = ebt_register_watcher(&ulog)))
		sock_release(ebtulognl->sk_socket);

	if (ret == 0)
		nf_log_register(PF_BRIDGE, &ebt_ulog_logger);

	return ret;
}

static void __exit ebt_ulog_fini(void)
{
	ebt_ulog_buff_t *ub;
	int i;

	nf_log_unregister(&ebt_ulog_logger);
	ebt_unregister_watcher(&ulog);
	for (i = 0; i < EBT_ULOG_MAXNLGROUPS; i++) {
		ub = &ulog_buffers[i];
		if (timer_pending(&ub->timer))
			del_timer(&ub->timer);
		spin_lock_bh(&ub->lock);
		if (ub->skb) {
			kfree_skb(ub->skb);
			ub->skb = NULL;
		}
		spin_unlock_bh(&ub->lock);
	}
	sock_release(ebtulognl->sk_socket);
}

module_init(ebt_ulog_init);
module_exit(ebt_ulog_fini);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bart De Schuymer <bdschuym@pandora.be>");
MODULE_DESCRIPTION("ebtables userspace logging module for bridged Ethernet"
		   " frames");
