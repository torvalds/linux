/*
 * This file implement the Wireless Extensions core API.
 *
 * Authors :	Jean Tourrilhes - HPL - <jt@hpl.hp.com>
 * Copyright (c) 1997-2007 Jean Tourrilhes, All Rights Reserved.
 * Copyright	2009 Johannes Berg <johannes@sipsolutions.net>
 * Copyright (C) 2024 Intel Corporation
 *
 * (As all part of the Linux kernel, this file is GPL)
 */
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/wireless.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <net/cfg80211.h>
#include <net/iw_handler.h>
#include <net/netlink.h>
#include <net/wext.h>
#include <net/net_namespace.h>

typedef int (*wext_ioctl_func)(struct net_device *, struct iwreq *,
			       unsigned int, struct iw_request_info *,
			       iw_handler);


/*
 * Meta-data about all the standard Wireless Extension request we
 * know about.
 */
static const struct iw_ioctl_description standard_ioctl[] = {
	[IW_IOCTL_IDX(SIOCSIWCOMMIT)] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[IW_IOCTL_IDX(SIOCGIWNAME)] = {
		.header_type	= IW_HEADER_TYPE_CHAR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWNWID)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[IW_IOCTL_IDX(SIOCGIWNWID)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWFREQ)] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[IW_IOCTL_IDX(SIOCGIWFREQ)] = {
		.header_type	= IW_HEADER_TYPE_FREQ,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWMODE)] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[IW_IOCTL_IDX(SIOCGIWMODE)] = {
		.header_type	= IW_HEADER_TYPE_UINT,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWSENS)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWSENS)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWRANGE)] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[IW_IOCTL_IDX(SIOCGIWRANGE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_range),
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWPRIV)] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[IW_IOCTL_IDX(SIOCGIWPRIV)] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_priv_args),
		.max_tokens	= 16,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[IW_IOCTL_IDX(SIOCSIWSTATS)] = {
		.header_type	= IW_HEADER_TYPE_NULL,
	},
	[IW_IOCTL_IDX(SIOCGIWSTATS)] = { /* (handled directly by us) */
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_statistics),
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWSPY)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr),
		.max_tokens	= IW_MAX_SPY,
	},
	[IW_IOCTL_IDX(SIOCGIWSPY)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_SPY,
	},
	[IW_IOCTL_IDX(SIOCSIWTHRSPY)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[IW_IOCTL_IDX(SIOCGIWTHRSPY)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct iw_thrspy),
		.min_tokens	= 1,
		.max_tokens	= 1,
	},
	[IW_IOCTL_IDX(SIOCSIWAP)] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IW_IOCTL_IDX(SIOCGIWAP)] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWMLME)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_mlme),
		.max_tokens	= sizeof(struct iw_mlme),
	},
	[IW_IOCTL_IDX(SIOCGIWAPLIST)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= sizeof(struct sockaddr) +
				  sizeof(struct iw_quality),
		.max_tokens	= IW_MAX_AP,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[IW_IOCTL_IDX(SIOCSIWSCAN)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= 0,
		.max_tokens	= sizeof(struct iw_scan_req),
	},
	[IW_IOCTL_IDX(SIOCGIWSCAN)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_SCAN_MAX_DATA,
		.flags		= IW_DESCR_FLAG_NOMAX,
	},
	[IW_IOCTL_IDX(SIOCSIWESSID)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE,
		.flags		= IW_DESCR_FLAG_EVENT,
	},
	[IW_IOCTL_IDX(SIOCGIWESSID)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE,
		.flags		= IW_DESCR_FLAG_DUMP,
	},
	[IW_IOCTL_IDX(SIOCSIWNICKN)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE,
	},
	[IW_IOCTL_IDX(SIOCGIWNICKN)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ESSID_MAX_SIZE,
	},
	[IW_IOCTL_IDX(SIOCSIWRATE)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWRATE)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWRTS)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWRTS)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWFRAG)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWFRAG)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWTXPOW)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWTXPOW)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWRETRY)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWRETRY)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWENCODE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_EVENT | IW_DESCR_FLAG_RESTRICT,
	},
	[IW_IOCTL_IDX(SIOCGIWENCODE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_ENCODING_TOKEN_MAX,
		.flags		= IW_DESCR_FLAG_DUMP | IW_DESCR_FLAG_RESTRICT,
	},
	[IW_IOCTL_IDX(SIOCSIWPOWER)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWPOWER)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWGENIE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IW_IOCTL_IDX(SIOCGIWGENIE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IW_IOCTL_IDX(SIOCSIWAUTH)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCGIWAUTH)] = {
		.header_type	= IW_HEADER_TYPE_PARAM,
	},
	[IW_IOCTL_IDX(SIOCSIWENCODEEXT)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[IW_IOCTL_IDX(SIOCGIWENCODEEXT)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_encode_ext),
		.max_tokens	= sizeof(struct iw_encode_ext) +
				  IW_ENCODING_TOKEN_MAX,
	},
	[IW_IOCTL_IDX(SIOCSIWPMKSA)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.min_tokens	= sizeof(struct iw_pmksa),
		.max_tokens	= sizeof(struct iw_pmksa),
	},
};
static const unsigned int standard_ioctl_num = ARRAY_SIZE(standard_ioctl);

/*
 * Meta-data about all the additional standard Wireless Extension events
 * we know about.
 */
static const struct iw_ioctl_description standard_event[] = {
	[IW_EVENT_IDX(IWEVTXDROP)] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IW_EVENT_IDX(IWEVQUAL)] = {
		.header_type	= IW_HEADER_TYPE_QUAL,
	},
	[IW_EVENT_IDX(IWEVCUSTOM)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_CUSTOM_MAX,
	},
	[IW_EVENT_IDX(IWEVREGISTERED)] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IW_EVENT_IDX(IWEVEXPIRED)] = {
		.header_type	= IW_HEADER_TYPE_ADDR,
	},
	[IW_EVENT_IDX(IWEVGENIE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IW_EVENT_IDX(IWEVMICHAELMICFAILURE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_michaelmicfailure),
	},
	[IW_EVENT_IDX(IWEVASSOCREQIE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IW_EVENT_IDX(IWEVASSOCRESPIE)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= IW_GENERIC_IE_MAX,
	},
	[IW_EVENT_IDX(IWEVPMKIDCAND)] = {
		.header_type	= IW_HEADER_TYPE_POINT,
		.token_size	= 1,
		.max_tokens	= sizeof(struct iw_pmkid_cand),
	},
};
static const unsigned int standard_event_num = ARRAY_SIZE(standard_event);

/* Size (in bytes) of various events */
static const int event_type_size[] = {
	IW_EV_LCP_LEN,			/* IW_HEADER_TYPE_NULL */
	0,
	IW_EV_CHAR_LEN,			/* IW_HEADER_TYPE_CHAR */
	0,
	IW_EV_UINT_LEN,			/* IW_HEADER_TYPE_UINT */
	IW_EV_FREQ_LEN,			/* IW_HEADER_TYPE_FREQ */
	IW_EV_ADDR_LEN,			/* IW_HEADER_TYPE_ADDR */
	0,
	IW_EV_POINT_LEN,		/* Without variable payload */
	IW_EV_PARAM_LEN,		/* IW_HEADER_TYPE_PARAM */
	IW_EV_QUAL_LEN,			/* IW_HEADER_TYPE_QUAL */
};

#ifdef CONFIG_COMPAT
static const int compat_event_type_size[] = {
	IW_EV_COMPAT_LCP_LEN,		/* IW_HEADER_TYPE_NULL */
	0,
	IW_EV_COMPAT_CHAR_LEN,		/* IW_HEADER_TYPE_CHAR */
	0,
	IW_EV_COMPAT_UINT_LEN,		/* IW_HEADER_TYPE_UINT */
	IW_EV_COMPAT_FREQ_LEN,		/* IW_HEADER_TYPE_FREQ */
	IW_EV_COMPAT_ADDR_LEN,		/* IW_HEADER_TYPE_ADDR */
	0,
	IW_EV_COMPAT_POINT_LEN,		/* Without variable payload */
	IW_EV_COMPAT_PARAM_LEN,		/* IW_HEADER_TYPE_PARAM */
	IW_EV_COMPAT_QUAL_LEN,		/* IW_HEADER_TYPE_QUAL */
};
#endif


/* IW event code */

void wireless_nlevent_flush(void)
{
	struct sk_buff *skb;
	struct net *net;

	down_read(&net_rwsem);
	for_each_net(net) {
		while ((skb = skb_dequeue(&net->wext_nlevents)))
			rtnl_notify(skb, net, 0, RTNLGRP_LINK, NULL,
				    GFP_KERNEL);
	}
	up_read(&net_rwsem);
}
EXPORT_SYMBOL_GPL(wireless_nlevent_flush);

static int wext_netdev_notifier_call(struct notifier_block *nb,
				     unsigned long state, void *ptr)
{
	/*
	 * When a netdev changes state in any way, flush all pending messages
	 * to avoid them going out in a strange order, e.g. RTM_NEWLINK after
	 * RTM_DELLINK, or with IFF_UP after without IFF_UP during dev_close()
	 * or similar - all of which could otherwise happen due to delays from
	 * schedule_work().
	 */
	wireless_nlevent_flush();

	return NOTIFY_OK;
}

static struct notifier_block wext_netdev_notifier = {
	.notifier_call = wext_netdev_notifier_call,
};

static int __net_init wext_pernet_init(struct net *net)
{
	skb_queue_head_init(&net->wext_nlevents);
	return 0;
}

static void __net_exit wext_pernet_exit(struct net *net)
{
	skb_queue_purge(&net->wext_nlevents);
}

static struct pernet_operations wext_pernet_ops = {
	.init = wext_pernet_init,
	.exit = wext_pernet_exit,
};

static int __init wireless_nlevent_init(void)
{
	int err = register_pernet_subsys(&wext_pernet_ops);

	if (err)
		return err;

	err = register_netdevice_notifier(&wext_netdev_notifier);
	if (err)
		unregister_pernet_subsys(&wext_pernet_ops);
	return err;
}

subsys_initcall(wireless_nlevent_init);

/* Process events generated by the wireless layer or the driver. */
static void wireless_nlevent_process(struct work_struct *work)
{
	wireless_nlevent_flush();
}

static DECLARE_WORK(wireless_nlevent_work, wireless_nlevent_process);

static struct nlmsghdr *rtnetlink_ifinfo_prep(struct net_device *dev,
					      struct sk_buff *skb)
{
	struct ifinfomsg *r;
	struct nlmsghdr  *nlh;

	nlh = nlmsg_put(skb, 0, 0, RTM_NEWLINK, sizeof(*r), 0);
	if (!nlh)
		return NULL;

	r = nlmsg_data(nlh);
	r->ifi_family = AF_UNSPEC;
	r->__ifi_pad = 0;
	r->ifi_type = dev->type;
	r->ifi_index = dev->ifindex;
	r->ifi_flags = dev_get_flags(dev);
	r->ifi_change = 0;	/* Wireless changes don't affect those flags */

	if (nla_put_string(skb, IFLA_IFNAME, dev->name))
		goto nla_put_failure;

	return nlh;
 nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return NULL;
}


/*
 * Main event dispatcher. Called from other parts and drivers.
 * Send the event on the appropriate channels.
 * May be called from interrupt context.
 */
void wireless_send_event(struct net_device *	dev,
			 unsigned int		cmd,
			 union iwreq_data *	wrqu,
			 const char *		extra)
{
	const struct iw_ioctl_description *	descr = NULL;
	int extra_len = 0;
	struct iw_event  *event;		/* Mallocated whole event */
	int event_len;				/* Its size */
	int hdr_len;				/* Size of the event header */
	int wrqu_off = 0;			/* Offset in wrqu */
	/* Don't "optimise" the following variable, it will crash */
	unsigned int	cmd_index;		/* *MUST* be unsigned */
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct nlattr *nla;
#ifdef CONFIG_COMPAT
	struct __compat_iw_event *compat_event;
	struct compat_iw_point compat_wrqu;
	struct sk_buff *compskb;
	int ptr_len;
#endif

	/*
	 * Nothing in the kernel sends scan events with data, be safe.
	 * This is necessary because we cannot fix up scan event data
	 * for compat, due to being contained in 'extra', but normally
	 * applications are required to retrieve the scan data anyway
	 * and no data is included in the event, this codifies that
	 * practice.
	 */
	if (WARN_ON(cmd == SIOCGIWSCAN && extra))
		extra = NULL;

	/* Get the description of the Event */
	if (cmd <= SIOCIWLAST) {
		cmd_index = IW_IOCTL_IDX(cmd);
		if (cmd_index < standard_ioctl_num)
			descr = &(standard_ioctl[cmd_index]);
	} else {
		cmd_index = IW_EVENT_IDX(cmd);
		if (cmd_index < standard_event_num)
			descr = &(standard_event[cmd_index]);
	}
	/* Don't accept unknown events */
	if (descr == NULL) {
		/* Note : we don't return an error to the driver, because
		 * the driver would not know what to do about it. It can't
		 * return an error to the user, because the event is not
		 * initiated by a user request.
		 * The best the driver could do is to log an error message.
		 * We will do it ourselves instead...
		 */
		netdev_err(dev, "(WE) : Invalid/Unknown Wireless Event (0x%04X)\n",
			   cmd);
		return;
	}

	/* Check extra parameters and set extra_len */
	if (descr->header_type == IW_HEADER_TYPE_POINT) {
		/* Check if number of token fits within bounds */
		if (wrqu->data.length > descr->max_tokens) {
			netdev_err(dev, "(WE) : Wireless Event (cmd=0x%04X) too big (%d)\n",
				   cmd, wrqu->data.length);
			return;
		}
		if (wrqu->data.length < descr->min_tokens) {
			netdev_err(dev, "(WE) : Wireless Event (cmd=0x%04X) too small (%d)\n",
				   cmd, wrqu->data.length);
			return;
		}
		/* Calculate extra_len - extra is NULL for restricted events */
		if (extra != NULL)
			extra_len = wrqu->data.length * descr->token_size;
		/* Always at an offset in wrqu */
		wrqu_off = IW_EV_POINT_OFF;
	}

	/* Total length of the event */
	hdr_len = event_type_size[descr->header_type];
	event_len = hdr_len + extra_len;

	/*
	 * The problem for 64/32 bit.
	 *
	 * On 64-bit, a regular event is laid out as follows:
	 *      |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
	 *      | event.len | event.cmd |     p a d d i n g     |
	 *      | wrqu data ... (with the correct size)         |
	 *
	 * This padding exists because we manipulate event->u,
	 * and 'event' is not packed.
	 *
	 * An iw_point event is laid out like this instead:
	 *      |  0  |  1  |  2  |  3  |  4  |  5  |  6  |  7  |
	 *      | event.len | event.cmd |     p a d d i n g     |
	 *      | iwpnt.len | iwpnt.flg |     p a d d i n g     |
	 *      | extra data  ...
	 *
	 * The second padding exists because struct iw_point is extended,
	 * but this depends on the platform...
	 *
	 * On 32-bit, all the padding shouldn't be there.
	 */

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return;

	/* Send via the RtNetlink event channel */
	nlh = rtnetlink_ifinfo_prep(dev, skb);
	if (WARN_ON(!nlh)) {
		kfree_skb(skb);
		return;
	}

	/* Add the wireless events in the netlink packet */
	nla = nla_reserve(skb, IFLA_WIRELESS, event_len);
	if (!nla) {
		kfree_skb(skb);
		return;
	}
	event = nla_data(nla);

	/* Fill event - first clear to avoid data leaking */
	memset(event, 0, hdr_len);
	event->len = event_len;
	event->cmd = cmd;
	memcpy(&event->u, ((char *) wrqu) + wrqu_off, hdr_len - IW_EV_LCP_LEN);
	if (extra_len)
		memcpy(((char *) event) + hdr_len, extra, extra_len);

	nlmsg_end(skb, nlh);
#ifdef CONFIG_COMPAT
	hdr_len = compat_event_type_size[descr->header_type];

	/* ptr_len is remaining size in event header apart from LCP */
	ptr_len = hdr_len - IW_EV_COMPAT_LCP_LEN;
	event_len = hdr_len + extra_len;

	compskb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!compskb) {
		kfree_skb(skb);
		return;
	}

	/* Send via the RtNetlink event channel */
	nlh = rtnetlink_ifinfo_prep(dev, compskb);
	if (WARN_ON(!nlh)) {
		kfree_skb(skb);
		kfree_skb(compskb);
		return;
	}

	/* Add the wireless events in the netlink packet */
	nla = nla_reserve(compskb, IFLA_WIRELESS, event_len);
	if (!nla) {
		kfree_skb(skb);
		kfree_skb(compskb);
		return;
	}
	compat_event = nla_data(nla);

	compat_event->len = event_len;
	compat_event->cmd = cmd;
	if (descr->header_type == IW_HEADER_TYPE_POINT) {
		compat_wrqu.length = wrqu->data.length;
		compat_wrqu.flags = wrqu->data.flags;
		memcpy(compat_event->ptr_bytes,
		       ((char *)&compat_wrqu) + IW_EV_COMPAT_POINT_OFF,
			ptr_len);
		if (extra_len)
			memcpy(&compat_event->ptr_bytes[ptr_len],
			       extra, extra_len);
	} else {
		/* extra_len must be zero, so no if (extra) needed */
		memcpy(compat_event->ptr_bytes, wrqu, ptr_len);
	}

	nlmsg_end(compskb, nlh);

	skb_shinfo(skb)->frag_list = compskb;
#endif
	skb_queue_tail(&dev_net(dev)->wext_nlevents, skb);
	schedule_work(&wireless_nlevent_work);
}
EXPORT_SYMBOL(wireless_send_event);

#ifdef CONFIG_CFG80211_WEXT
static void wireless_warn_cfg80211_wext(void)
{
	char name[sizeof(current->comm)];

	pr_warn_once("warning: `%s' uses wireless extensions which will stop working for Wi-Fi 7 hardware; use nl80211\n",
		     get_task_comm(name, current));
}
#endif

/* IW handlers */

struct iw_statistics *get_wireless_stats(struct net_device *dev)
{
#ifdef CONFIG_WIRELESS_EXT
	if ((dev->wireless_handlers != NULL) &&
	   (dev->wireless_handlers->get_wireless_stats != NULL))
		return dev->wireless_handlers->get_wireless_stats(dev);
#endif

#ifdef CONFIG_CFG80211_WEXT
	if (dev->ieee80211_ptr &&
	    dev->ieee80211_ptr->wiphy &&
	    dev->ieee80211_ptr->wiphy->wext &&
	    dev->ieee80211_ptr->wiphy->wext->get_wireless_stats) {
		wireless_warn_cfg80211_wext();
		if (dev->ieee80211_ptr->wiphy->flags & (WIPHY_FLAG_SUPPORTS_MLO |
							WIPHY_FLAG_DISABLE_WEXT))
			return NULL;
		return dev->ieee80211_ptr->wiphy->wext->get_wireless_stats(dev);
	}
#endif

	/* not found */
	return NULL;
}

/* noinline to avoid a bogus warning with -O3 */
static noinline int iw_handler_get_iwstats(struct net_device *	dev,
				  struct iw_request_info *	info,
				  union iwreq_data *		wrqu,
				  char *			extra)
{
	/* Get stats from the driver */
	struct iw_statistics *stats;

	stats = get_wireless_stats(dev);
	if (stats) {
		/* Copy statistics to extra */
		memcpy(extra, stats, sizeof(struct iw_statistics));
		wrqu->data.length = sizeof(struct iw_statistics);

		/* Check if we need to clear the updated flag */
		if (wrqu->data.flags != 0)
			stats->qual.updated &= ~IW_QUAL_ALL_UPDATED;
		return 0;
	} else
		return -EOPNOTSUPP;
}

static iw_handler get_handler(struct net_device *dev, unsigned int cmd)
{
	/* Don't "optimise" the following variable, it will crash */
	unsigned int	index;		/* *MUST* be unsigned */
	const struct iw_handler_def *handlers = NULL;

#ifdef CONFIG_CFG80211_WEXT
	if (dev->ieee80211_ptr && dev->ieee80211_ptr->wiphy) {
		wireless_warn_cfg80211_wext();
		if (dev->ieee80211_ptr->wiphy->flags & (WIPHY_FLAG_SUPPORTS_MLO |
							WIPHY_FLAG_DISABLE_WEXT))
			return NULL;
		handlers = dev->ieee80211_ptr->wiphy->wext;
	}
#endif
#ifdef CONFIG_WIRELESS_EXT
	if (dev->wireless_handlers)
		handlers = dev->wireless_handlers;
#endif

	if (!handlers)
		return NULL;

	/* Try as a standard command */
	index = IW_IOCTL_IDX(cmd);
	if (index < handlers->num_standard)
		return handlers->standard[index];

#ifdef CONFIG_WEXT_PRIV
	/* Try as a private command */
	index = cmd - SIOCIWFIRSTPRIV;
	if (index < handlers->num_private)
		return handlers->private[index];
#endif

	/* Not found */
	return NULL;
}

static int ioctl_standard_iw_point(struct iw_point *iwp, unsigned int cmd,
				   const struct iw_ioctl_description *descr,
				   iw_handler handler, struct net_device *dev,
				   struct iw_request_info *info)
{
	int err, extra_size, user_length = 0, essid_compat = 0;
	char *extra;

	/* Calculate space needed by arguments. Always allocate
	 * for max space.
	 */
	extra_size = descr->max_tokens * descr->token_size;

	/* Check need for ESSID compatibility for WE < 21 */
	switch (cmd) {
	case SIOCSIWESSID:
	case SIOCGIWESSID:
	case SIOCSIWNICKN:
	case SIOCGIWNICKN:
		if (iwp->length == descr->max_tokens + 1)
			essid_compat = 1;
		else if (IW_IS_SET(cmd) && (iwp->length != 0)) {
			char essid[IW_ESSID_MAX_SIZE + 1];
			unsigned int len;
			len = iwp->length * descr->token_size;

			if (len > IW_ESSID_MAX_SIZE)
				return -EFAULT;

			err = copy_from_user(essid, iwp->pointer, len);
			if (err)
				return -EFAULT;

			if (essid[iwp->length - 1] == '\0')
				essid_compat = 1;
		}
		break;
	default:
		break;
	}

	iwp->length -= essid_compat;

	/* Check what user space is giving us */
	if (IW_IS_SET(cmd)) {
		/* Check NULL pointer */
		if (!iwp->pointer && iwp->length != 0)
			return -EFAULT;
		/* Check if number of token fits within bounds */
		if (iwp->length > descr->max_tokens)
			return -E2BIG;
		if (iwp->length < descr->min_tokens)
			return -EINVAL;
	} else {
		/* Check NULL pointer */
		if (!iwp->pointer)
			return -EFAULT;
		/* Save user space buffer size for checking */
		user_length = iwp->length;

		/* Don't check if user_length > max to allow forward
		 * compatibility. The test user_length < min is
		 * implied by the test at the end.
		 */

		/* Support for very large requests */
		if ((descr->flags & IW_DESCR_FLAG_NOMAX) &&
		    (user_length > descr->max_tokens)) {
			/* Allow userspace to GET more than max so
			 * we can support any size GET requests.
			 * There is still a limit : -ENOMEM.
			 */
			extra_size = user_length * descr->token_size;

			/* Note : user_length is originally a __u16,
			 * and token_size is controlled by us,
			 * so extra_size won't get negative and
			 * won't overflow...
			 */
		}
	}

	/* Sanity-check to ensure we never end up _allocating_ zero
	 * bytes of data for extra.
	 */
	if (extra_size <= 0)
		return -EFAULT;

	/* kzalloc() ensures NULL-termination for essid_compat. */
	extra = kzalloc(extra_size, GFP_KERNEL);
	if (!extra)
		return -ENOMEM;

	/* If it is a SET, get all the extra data in here */
	if (IW_IS_SET(cmd) && (iwp->length != 0)) {
		if (copy_from_user(extra, iwp->pointer,
				   iwp->length *
				   descr->token_size)) {
			err = -EFAULT;
			goto out;
		}

		if (cmd == SIOCSIWENCODEEXT) {
			struct iw_encode_ext *ee = (void *) extra;

			if (iwp->length < sizeof(*ee) + ee->key_len) {
				err = -EFAULT;
				goto out;
			}
		}
	}

	if (IW_IS_GET(cmd) && !(descr->flags & IW_DESCR_FLAG_NOMAX)) {
		/*
		 * If this is a GET, but not NOMAX, it means that the extra
		 * data is not bounded by userspace, but by max_tokens. Thus
		 * set the length to max_tokens. This matches the extra data
		 * allocation.
		 * The driver should fill it with the number of tokens it
		 * provided, and it may check iwp->length rather than having
		 * knowledge of max_tokens. If the driver doesn't change the
		 * iwp->length, this ioctl just copies back max_token tokens
		 * filled with zeroes. Hopefully the driver isn't claiming
		 * them to be valid data.
		 */
		iwp->length = descr->max_tokens;
	}

	err = handler(dev, info, (union iwreq_data *) iwp, extra);

	iwp->length += essid_compat;

	/* If we have something to return to the user */
	if (!err && IW_IS_GET(cmd)) {
		/* Check if there is enough buffer up there */
		if (user_length < iwp->length) {
			err = -E2BIG;
			goto out;
		}

		if (copy_to_user(iwp->pointer, extra,
				 iwp->length *
				 descr->token_size)) {
			err = -EFAULT;
			goto out;
		}
	}

	/* Generate an event to notify listeners of the change */
	if ((descr->flags & IW_DESCR_FLAG_EVENT) &&
	    ((err == 0) || (err == -EIWCOMMIT))) {
		union iwreq_data *data = (union iwreq_data *) iwp;

		if (descr->flags & IW_DESCR_FLAG_RESTRICT)
			/* If the event is restricted, don't
			 * export the payload.
			 */
			wireless_send_event(dev, cmd, data, NULL);
		else
			wireless_send_event(dev, cmd, data, extra);
	}

out:
	kfree(extra);
	return err;
}

/*
 * Call the commit handler in the driver
 * (if exist and if conditions are right)
 *
 * Note : our current commit strategy is currently pretty dumb,
 * but we will be able to improve on that...
 * The goal is to try to agreagate as many changes as possible
 * before doing the commit. Drivers that will define a commit handler
 * are usually those that need a reset after changing parameters, so
 * we want to minimise the number of reset.
 * A cool idea is to use a timer : at each "set" command, we re-set the
 * timer, when the timer eventually fires, we call the driver.
 * Hopefully, more on that later.
 *
 * Also, I'm waiting to see how many people will complain about the
 * netif_running(dev) test. I'm open on that one...
 * Hopefully, the driver will remember to do a commit in "open()" ;-)
 */
int call_commit_handler(struct net_device *dev)
{
#ifdef CONFIG_WIRELESS_EXT
	if (netif_running(dev) &&
	    dev->wireless_handlers &&
	    dev->wireless_handlers->standard[0])
		/* Call the commit handler on the driver */
		return dev->wireless_handlers->standard[0](dev, NULL,
							   NULL, NULL);
	else
		return 0;		/* Command completed successfully */
#else
	/* cfg80211 has no commit */
	return 0;
#endif
}

/*
 * Main IOCTl dispatcher.
 * Check the type of IOCTL and call the appropriate wrapper...
 */
static int wireless_process_ioctl(struct net *net, struct iwreq *iwr,
				  unsigned int cmd,
				  struct iw_request_info *info,
				  wext_ioctl_func standard,
				  wext_ioctl_func private)
{
	struct net_device *dev;
	iw_handler	handler;

	/* Permissions are already checked in dev_ioctl() before calling us.
	 * The copy_to/from_user() of ifr is also dealt with in there */

	/* Make sure the device exist */
	if ((dev = __dev_get_by_name(net, iwr->ifr_name)) == NULL)
		return -ENODEV;

	/* A bunch of special cases, then the generic case...
	 * Note that 'cmd' is already filtered in dev_ioctl() with
	 * (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) */
	if (cmd == SIOCGIWSTATS)
		return standard(dev, iwr, cmd, info,
				&iw_handler_get_iwstats);

#ifdef CONFIG_WEXT_PRIV
	if (cmd == SIOCGIWPRIV && dev->wireless_handlers)
		return standard(dev, iwr, cmd, info,
				iw_handler_get_private);
#endif

	/* Basic check */
	if (!netif_device_present(dev))
		return -ENODEV;

	/* New driver API : try to find the handler */
	handler = get_handler(dev, cmd);
	if (handler) {
		/* Standard and private are not the same */
		if (cmd < SIOCIWFIRSTPRIV)
			return standard(dev, iwr, cmd, info, handler);
		else if (private)
			return private(dev, iwr, cmd, info, handler);
	}
	return -EOPNOTSUPP;
}

/* If command is `set a parameter', or `get the encoding parameters',
 * check if the user has the right to do it.
 */
static int wext_permission_check(unsigned int cmd)
{
	if ((IW_IS_SET(cmd) || cmd == SIOCGIWENCODE ||
	     cmd == SIOCGIWENCODEEXT) &&
	    !capable(CAP_NET_ADMIN))
		return -EPERM;

	return 0;
}

/* entry point from dev ioctl */
static int wext_ioctl_dispatch(struct net *net, struct iwreq *iwr,
			       unsigned int cmd, struct iw_request_info *info,
			       wext_ioctl_func standard,
			       wext_ioctl_func private)
{
	int ret = wext_permission_check(cmd);

	if (ret)
		return ret;

	dev_load(net, iwr->ifr_name);
	rtnl_lock();
	ret = wireless_process_ioctl(net, iwr, cmd, info, standard, private);
	rtnl_unlock();

	return ret;
}

/*
 * Wrapper to call a standard Wireless Extension handler.
 * We do various checks and also take care of moving data between
 * user space and kernel space.
 */
static int ioctl_standard_call(struct net_device *	dev,
			       struct iwreq		*iwr,
			       unsigned int		cmd,
			       struct iw_request_info	*info,
			       iw_handler		handler)
{
	const struct iw_ioctl_description *	descr;
	int					ret = -EINVAL;

	/* Get the description of the IOCTL */
	if (IW_IOCTL_IDX(cmd) >= standard_ioctl_num)
		return -EOPNOTSUPP;
	descr = &(standard_ioctl[IW_IOCTL_IDX(cmd)]);

	/* Check if we have a pointer to user space data or not */
	if (descr->header_type != IW_HEADER_TYPE_POINT) {

		/* No extra arguments. Trivial to handle */
		ret = handler(dev, info, &(iwr->u), NULL);

		/* Generate an event to notify listeners of the change */
		if ((descr->flags & IW_DESCR_FLAG_EVENT) &&
		   ((ret == 0) || (ret == -EIWCOMMIT)))
			wireless_send_event(dev, cmd, &(iwr->u), NULL);
	} else {
		ret = ioctl_standard_iw_point(&iwr->u.data, cmd, descr,
					      handler, dev, info);
	}

	/* Call commit handler if needed and defined */
	if (ret == -EIWCOMMIT)
		ret = call_commit_handler(dev);

	/* Here, we will generate the appropriate event if needed */

	return ret;
}


int wext_handle_ioctl(struct net *net, unsigned int cmd, void __user *arg)
{
	struct iw_request_info info = { .cmd = cmd, .flags = 0 };
	struct iwreq iwr;
	int ret;

	if (copy_from_user(&iwr, arg, sizeof(iwr)))
		return -EFAULT;

	iwr.ifr_name[sizeof(iwr.ifr_name) - 1] = 0;

	ret = wext_ioctl_dispatch(net, &iwr, cmd, &info,
				  ioctl_standard_call,
				  ioctl_private_call);
	if (ret >= 0 &&
	    IW_IS_GET(cmd) &&
	    copy_to_user(arg, &iwr, sizeof(struct iwreq)))
		return -EFAULT;

	return ret;
}

#ifdef CONFIG_COMPAT
static int compat_standard_call(struct net_device	*dev,
				struct iwreq		*iwr,
				unsigned int		cmd,
				struct iw_request_info	*info,
				iw_handler		handler)
{
	const struct iw_ioctl_description *descr;
	struct compat_iw_point *iwp_compat;
	struct iw_point iwp;
	int err;

	descr = standard_ioctl + IW_IOCTL_IDX(cmd);

	if (descr->header_type != IW_HEADER_TYPE_POINT)
		return ioctl_standard_call(dev, iwr, cmd, info, handler);

	iwp_compat = (struct compat_iw_point *) &iwr->u.data;
	iwp.pointer = compat_ptr(iwp_compat->pointer);
	iwp.length = iwp_compat->length;
	iwp.flags = iwp_compat->flags;

	err = ioctl_standard_iw_point(&iwp, cmd, descr, handler, dev, info);

	iwp_compat->pointer = ptr_to_compat(iwp.pointer);
	iwp_compat->length = iwp.length;
	iwp_compat->flags = iwp.flags;

	return err;
}

int compat_wext_handle_ioctl(struct net *net, unsigned int cmd,
			     unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	struct iw_request_info info;
	struct iwreq iwr;
	char *colon;
	int ret;

	if (copy_from_user(&iwr, argp, sizeof(struct iwreq)))
		return -EFAULT;

	iwr.ifr_name[IFNAMSIZ-1] = 0;
	colon = strchr(iwr.ifr_name, ':');
	if (colon)
		*colon = 0;

	info.cmd = cmd;
	info.flags = IW_REQUEST_FLAG_COMPAT;

	ret = wext_ioctl_dispatch(net, &iwr, cmd, &info,
				  compat_standard_call,
				  compat_private_call);

	if (ret >= 0 &&
	    IW_IS_GET(cmd) &&
	    copy_to_user(argp, &iwr, sizeof(struct iwreq)))
		return -EFAULT;

	return ret;
}
#endif

char *iwe_stream_add_event(struct iw_request_info *info, char *stream,
			   char *ends, struct iw_event *iwe, int event_len)
{
	int lcp_len = iwe_stream_lcp_len(info);

	event_len = iwe_stream_event_len_adjust(info, event_len);

	/* Check if it's possible */
	if (likely((stream + event_len) < ends)) {
		iwe->len = event_len;
		/* Beware of alignment issues on 64 bits */
		memcpy(stream, (char *) iwe, IW_EV_LCP_PK_LEN);
		memcpy(stream + lcp_len, &iwe->u,
		       event_len - lcp_len);
		stream += event_len;
	}

	return stream;
}
EXPORT_SYMBOL(iwe_stream_add_event);

char *iwe_stream_add_point(struct iw_request_info *info, char *stream,
			   char *ends, struct iw_event *iwe, char *extra)
{
	int event_len = iwe_stream_point_len(info) + iwe->u.data.length;
	int point_len = iwe_stream_point_len(info);
	int lcp_len   = iwe_stream_lcp_len(info);

	/* Check if it's possible */
	if (likely((stream + event_len) < ends)) {
		iwe->len = event_len;
		memcpy(stream, (char *) iwe, IW_EV_LCP_PK_LEN);
		memcpy(stream + lcp_len,
		       ((char *) &iwe->u) + IW_EV_POINT_OFF,
		       IW_EV_POINT_PK_LEN - IW_EV_LCP_PK_LEN);
		if (iwe->u.data.length && extra)
			memcpy(stream + point_len, extra, iwe->u.data.length);
		stream += event_len;
	}

	return stream;
}
EXPORT_SYMBOL(iwe_stream_add_point);

char *iwe_stream_add_value(struct iw_request_info *info, char *event,
			   char *value, char *ends, struct iw_event *iwe,
			   int event_len)
{
	int lcp_len = iwe_stream_lcp_len(info);

	/* Don't duplicate LCP */
	event_len -= IW_EV_LCP_LEN;

	/* Check if it's possible */
	if (likely((value + event_len) < ends)) {
		/* Add new value */
		memcpy(value, &iwe->u, event_len);
		value += event_len;
		/* Patch LCP */
		iwe->len = value - event;
		memcpy(event, (char *) iwe, lcp_len);
	}

	return value;
}
EXPORT_SYMBOL(iwe_stream_add_value);
