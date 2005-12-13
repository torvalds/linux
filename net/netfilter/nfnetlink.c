/* Netfilter messages via netlink socket. Allows for user space
 * protocol helpers and general trouble making from userspace.
 *
 * (C) 2001 by Jay Schulist <jschlst@samba.org>,
 * (C) 2002-2005 by Harald Welte <laforge@gnumonks.org>
 * (C) 2005 by Pablo Neira Ayuso <pablo@eurodev.net>
 *
 * Initial netfilter messages via netlink development funded and
 * generally made possible by Network Robots, Inc. (www.networkrobots.com)
 *
 * Further development of this code funded by Astaro AG (http://www.astaro.com)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/skbuff.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <net/sock.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include <linux/netfilter.h>
#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_NETFILTER);

static char __initdata nfversion[] = "0.30";

#if 0
#define DEBUGP(format, args...)	\
		printk(KERN_DEBUG "%s(%d):%s(): " format, __FILE__, \
			__LINE__, __FUNCTION__, ## args)
#else
#define DEBUGP(format, args...)
#endif

static struct sock *nfnl = NULL;
static struct nfnetlink_subsystem *subsys_table[NFNL_SUBSYS_COUNT];
DECLARE_MUTEX(nfnl_sem);

void nfnl_lock(void)
{
	nfnl_shlock();
}

void nfnl_unlock(void)
{
	nfnl_shunlock();
}

int nfnetlink_subsys_register(struct nfnetlink_subsystem *n)
{
	DEBUGP("registering subsystem ID %u\n", n->subsys_id);

	nfnl_lock();
	if (subsys_table[n->subsys_id]) {
		nfnl_unlock();
		return -EBUSY;
	}
	subsys_table[n->subsys_id] = n;
	nfnl_unlock();

	return 0;
}

int nfnetlink_subsys_unregister(struct nfnetlink_subsystem *n)
{
	DEBUGP("unregistering subsystem ID %u\n", n->subsys_id);

	nfnl_lock();
	subsys_table[n->subsys_id] = NULL;
	nfnl_unlock();

	return 0;
}

static inline struct nfnetlink_subsystem *nfnetlink_get_subsys(u_int16_t type)
{
	u_int8_t subsys_id = NFNL_SUBSYS_ID(type);

	if (subsys_id >= NFNL_SUBSYS_COUNT
	    || subsys_table[subsys_id] == NULL)
		return NULL;

	return subsys_table[subsys_id];
}

static inline struct nfnl_callback *
nfnetlink_find_client(u_int16_t type, struct nfnetlink_subsystem *ss)
{
	u_int8_t cb_id = NFNL_MSG_TYPE(type);
	
	if (cb_id >= ss->cb_count) {
		DEBUGP("msgtype %u >= %u, returning\n", type, ss->cb_count);
		return NULL;
	}

	return &ss->cb[cb_id];
}

void __nfa_fill(struct sk_buff *skb, int attrtype, int attrlen,
		const void *data)
{
	struct nfattr *nfa;
	int size = NFA_LENGTH(attrlen);

	nfa = (struct nfattr *)skb_put(skb, NFA_ALIGN(size));
	nfa->nfa_type = attrtype;
	nfa->nfa_len  = size;
	memcpy(NFA_DATA(nfa), data, attrlen);
	memset(NFA_DATA(nfa) + attrlen, 0, NFA_ALIGN(size) - size);
}

void nfattr_parse(struct nfattr *tb[], int maxattr, struct nfattr *nfa, int len)
{
	memset(tb, 0, sizeof(struct nfattr *) * maxattr);

	while (NFA_OK(nfa, len)) {
		unsigned flavor = NFA_TYPE(nfa);
		if (flavor && flavor <= maxattr)
			tb[flavor-1] = nfa;
		nfa = NFA_NEXT(nfa, len);
	}
}

/**
 * nfnetlink_check_attributes - check and parse nfnetlink attributes
 *
 * subsys: nfnl subsystem for which this message is to be parsed
 * nlmsghdr: netlink message to be checked/parsed
 * cda: array of pointers, needs to be at least subsys->attr_count big
 *
 */
static int
nfnetlink_check_attributes(struct nfnetlink_subsystem *subsys,
			   struct nlmsghdr *nlh, struct nfattr *cda[])
{
	int min_len;
	u_int16_t attr_count;
	u_int8_t cb_id = NFNL_MSG_TYPE(nlh->nlmsg_type);

	if (unlikely(cb_id >= subsys->cb_count)) {
		DEBUGP("msgtype %u >= %u, returning\n",
			cb_id, subsys->cb_count);
		return -EINVAL;
	}

	min_len = NLMSG_SPACE(sizeof(struct nfgenmsg));
	if (unlikely(nlh->nlmsg_len < min_len))
		return -EINVAL;

	attr_count = subsys->cb[cb_id].attr_count;
	memset(cda, 0, sizeof(struct nfattr *) * attr_count);

	/* check attribute lengths. */
	if (likely(nlh->nlmsg_len > min_len)) {
		struct nfattr *attr = NFM_NFA(NLMSG_DATA(nlh));
		int attrlen = nlh->nlmsg_len - NLMSG_ALIGN(min_len);

		while (NFA_OK(attr, attrlen)) {
			unsigned flavor = NFA_TYPE(attr);
			if (flavor) {
				if (flavor > attr_count)
					return -EINVAL;
				cda[flavor - 1] = attr;
			}
			attr = NFA_NEXT(attr, attrlen);
		}
	}

	/* implicit: if nlmsg_len == min_len, we return 0, and an empty
	 * (zeroed) cda[] array. The message is valid, but empty. */

        return 0;
}

int nfnetlink_send(struct sk_buff *skb, u32 pid, unsigned group, int echo)
{
	gfp_t allocation = in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;
	int err = 0;

	NETLINK_CB(skb).dst_group = group;
	if (echo)
		atomic_inc(&skb->users);
	netlink_broadcast(nfnl, skb, pid, group, allocation);
	if (echo)
		err = netlink_unicast(nfnl, skb, pid, MSG_DONTWAIT);

	return err;
}

int nfnetlink_unicast(struct sk_buff *skb, u_int32_t pid, int flags)
{
	return netlink_unicast(nfnl, skb, pid, flags);
}

/* Process one complete nfnetlink message. */
static inline int nfnetlink_rcv_msg(struct sk_buff *skb,
				    struct nlmsghdr *nlh, int *errp)
{
	struct nfnl_callback *nc;
	struct nfnetlink_subsystem *ss;
	int type, err = 0;

	DEBUGP("entered; subsys=%u, msgtype=%u\n",
		 NFNL_SUBSYS_ID(nlh->nlmsg_type),
		 NFNL_MSG_TYPE(nlh->nlmsg_type));

	if (!cap_raised(NETLINK_CB(skb).eff_cap, CAP_NET_ADMIN)) {
		DEBUGP("missing CAP_NET_ADMIN\n");
		*errp = -EPERM;
		return -1;
	}

	/* Only requests are handled by kernel now. */
	if (!(nlh->nlmsg_flags & NLM_F_REQUEST)) {
		DEBUGP("received non-request message\n");
		return 0;
	}

	/* All the messages must at least contain nfgenmsg */
	if (nlh->nlmsg_len < NLMSG_SPACE(sizeof(struct nfgenmsg))) {
		DEBUGP("received message was too short\n");
		return 0;
	}

	type = nlh->nlmsg_type;
	ss = nfnetlink_get_subsys(type);
	if (!ss) {
#ifdef CONFIG_KMOD
		/* don't call nfnl_shunlock, since it would reenter
		 * with further packet processing */
		up(&nfnl_sem);
		request_module("nfnetlink-subsys-%d", NFNL_SUBSYS_ID(type));
		nfnl_shlock();
		ss = nfnetlink_get_subsys(type);
		if (!ss)
#endif
			goto err_inval;
	}

	nc = nfnetlink_find_client(type, ss);
	if (!nc) {
		DEBUGP("unable to find client for type %d\n", type);
		goto err_inval;
	}

	{
		u_int16_t attr_count = 
			ss->cb[NFNL_MSG_TYPE(nlh->nlmsg_type)].attr_count;
		struct nfattr *cda[attr_count];

		memset(cda, 0, sizeof(struct nfattr *) * attr_count);
		
		err = nfnetlink_check_attributes(ss, nlh, cda);
		if (err < 0)
			goto err_inval;

		DEBUGP("calling handler\n");
		err = nc->call(nfnl, skb, nlh, cda, errp);
		*errp = err;
		return err;
	}

err_inval:
	DEBUGP("returning -EINVAL\n");
	*errp = -EINVAL;
	return -1;
}

/* Process one packet of messages. */
static inline int nfnetlink_rcv_skb(struct sk_buff *skb)
{
	int err;
	struct nlmsghdr *nlh;

	while (skb->len >= NLMSG_SPACE(0)) {
		u32 rlen;

		nlh = (struct nlmsghdr *)skb->data;
		if (nlh->nlmsg_len < sizeof(struct nlmsghdr)
		    || skb->len < nlh->nlmsg_len)
			return 0;
		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len)
			rlen = skb->len;
		if (nfnetlink_rcv_msg(skb, nlh, &err)) {
			if (!err)
				return -1;
			netlink_ack(skb, nlh, err);
		} else
			if (nlh->nlmsg_flags & NLM_F_ACK)
				netlink_ack(skb, nlh, 0);
		skb_pull(skb, rlen);
	}

	return 0;
}

static void nfnetlink_rcv(struct sock *sk, int len)
{
	do {
		struct sk_buff *skb;

		if (nfnl_shlock_nowait())
			return;

		while ((skb = skb_dequeue(&sk->sk_receive_queue)) != NULL) {
			if (nfnetlink_rcv_skb(skb)) {
				if (skb->len)
					skb_queue_head(&sk->sk_receive_queue,
						       skb);
				else
					kfree_skb(skb);
				break;
			}
			kfree_skb(skb);
		}

		/* don't call nfnl_shunlock, since it would reenter
		 * with further packet processing */
		up(&nfnl_sem);
	} while(nfnl && nfnl->sk_receive_queue.qlen);
}

static void __exit nfnetlink_exit(void)
{
	printk("Removing netfilter NETLINK layer.\n");
	sock_release(nfnl->sk_socket);
	return;
}

static int __init nfnetlink_init(void)
{
	printk("Netfilter messages via NETLINK v%s.\n", nfversion);

	nfnl = netlink_kernel_create(NETLINK_NETFILTER, NFNLGRP_MAX,
	                             nfnetlink_rcv, THIS_MODULE);
	if (!nfnl) {
		printk(KERN_ERR "cannot initialize nfnetlink!\n");
		return -1;
	}

	return 0;
}

module_init(nfnetlink_init);
module_exit(nfnetlink_exit);

EXPORT_SYMBOL_GPL(nfnetlink_subsys_register);
EXPORT_SYMBOL_GPL(nfnetlink_subsys_unregister);
EXPORT_SYMBOL_GPL(nfnetlink_send);
EXPORT_SYMBOL_GPL(nfnetlink_unicast);
EXPORT_SYMBOL_GPL(nfattr_parse);
EXPORT_SYMBOL_GPL(__nfa_fill);
