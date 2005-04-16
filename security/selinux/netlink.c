/*
 * Netlink event notifications for SELinux.
 *
 * Author: James Morris <jmorris@redhat.com>
 *
 * Copyright (C) 2004 Red Hat, Inc., James Morris <jmorris@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2,
 * as published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/selinux_netlink.h>

static struct sock *selnl;

static int selnl_msglen(int msgtype)
{
	int ret = 0;
	
	switch (msgtype) {
	case SELNL_MSG_SETENFORCE:
		ret = sizeof(struct selnl_msg_setenforce);
		break;
	
	case SELNL_MSG_POLICYLOAD:
		ret = sizeof(struct selnl_msg_policyload);
		break;
		
	default:
		BUG();
	}
	return ret;
}

static void selnl_add_payload(struct nlmsghdr *nlh, int len, int msgtype, void *data)
{
	switch (msgtype) {
	case SELNL_MSG_SETENFORCE: {
		struct selnl_msg_setenforce *msg = NLMSG_DATA(nlh);
		
		memset(msg, 0, len);
		msg->val = *((int *)data);
		break;
	}
	
	case SELNL_MSG_POLICYLOAD: {
		struct selnl_msg_policyload *msg = NLMSG_DATA(nlh);
		
		memset(msg, 0, len);
		msg->seqno = *((u32 *)data);
		break;
	}

	default:
		BUG();
	}
}

static void selnl_notify(int msgtype, void *data)
{
	int len;
	unsigned char *tmp;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	
	len = selnl_msglen(msgtype);
	
	skb = alloc_skb(NLMSG_SPACE(len), GFP_USER);
	if (!skb)
		goto oom;

	tmp = skb->tail;
	nlh = NLMSG_PUT(skb, 0, 0, msgtype, len);
	selnl_add_payload(nlh, len, msgtype, data);
	nlh->nlmsg_len = skb->tail - tmp;
	netlink_broadcast(selnl, skb, 0, SELNL_GRP_AVC, GFP_USER);
out:
	return;
	
nlmsg_failure:
	kfree_skb(skb);
oom:
	printk(KERN_ERR "SELinux:  OOM in %s\n", __FUNCTION__);
	goto out;
}

void selnl_notify_setenforce(int val)
{
	selnl_notify(SELNL_MSG_SETENFORCE, &val);
}

void selnl_notify_policyload(u32 seqno)
{
	selnl_notify(SELNL_MSG_POLICYLOAD, &seqno);
}

static int __init selnl_init(void)
{
	selnl = netlink_kernel_create(NETLINK_SELINUX, NULL);
	if (selnl == NULL)
		panic("SELinux:  Cannot create netlink socket.");
	netlink_set_nonroot(NETLINK_SELINUX, NL_NONROOT_RECV);	
	return 0;
}

__initcall(selnl_init);
