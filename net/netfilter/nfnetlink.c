/* Netfilter messages via netlink socket. Allows for user space
 * protocol helpers and general trouble making from userspace.
 *
 * (C) 2001 by Jay Schulist <jschlst@samba.org>,
 * (C) 2002-2005 by Harald Welte <laforge@gnumonks.org>
 * (C) 2005,2007 by Pablo Neira Ayuso <pablo@netfilter.org>
 *
 * Initial netfilter messages via netlink development funded and
 * generally made possible by Network Robots, Inc. (www.networkrobots.com)
 *
 * Further development of this code funded by Astaro AG (http://www.astaro.com)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/skbuff.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <linux/init.h>

#include <linux/netlink.h>
#include <linux/netfilter/nfnetlink.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_NETFILTER);

static char __initdata nfversion[] = "0.30";

static struct sock *nfnl = NULL;
static struct nfnetlink_subsystem *subsys_table[NFNL_SUBSYS_COUNT];
static DEFINE_MUTEX(nfnl_mutex);

static void nfnl_lock(void)
{
	mutex_lock(&nfnl_mutex);
}

static int nfnl_trylock(void)
{
	return !mutex_trylock(&nfnl_mutex);
}

static void __nfnl_unlock(void)
{
	mutex_unlock(&nfnl_mutex);
}

static void nfnl_unlock(void)
{
	mutex_unlock(&nfnl_mutex);
	if (nfnl->sk_receive_queue.qlen)
		nfnl->sk_data_ready(nfnl, 0);
}

int nfnetlink_subsys_register(struct nfnetlink_subsystem *n)
{
	nfnl_lock();
	if (subsys_table[n->subsys_id]) {
		nfnl_unlock();
		return -EBUSY;
	}
	subsys_table[n->subsys_id] = n;
	nfnl_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(nfnetlink_subsys_register);

int nfnetlink_subsys_unregister(struct nfnetlink_subsystem *n)
{
	nfnl_lock();
	subsys_table[n->subsys_id] = NULL;
	nfnl_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(nfnetlink_subsys_unregister);

static inline struct nfnetlink_subsystem *nfnetlink_get_subsys(u_int16_t type)
{
	u_int8_t subsys_id = NFNL_SUBSYS_ID(type);

	if (subsys_id >= NFNL_SUBSYS_COUNT)
		return NULL;

	return subsys_table[subsys_id];
}

static inline struct nfnl_callback *
nfnetlink_find_client(u_int16_t type, struct nfnetlink_subsystem *ss)
{
	u_int8_t cb_id = NFNL_MSG_TYPE(type);

	if (cb_id >= ss->cb_count)
		return NULL;

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
EXPORT_SYMBOL_GPL(__nfa_fill);

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
EXPORT_SYMBOL_GPL(nfattr_parse);

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
	int min_len = NLMSG_SPACE(sizeof(struct nfgenmsg));
	u_int8_t cb_id = NFNL_MSG_TYPE(nlh->nlmsg_type);
	u_int16_t attr_count = subsys->cb[cb_id].attr_count;

	/* check attribute lengths. */
	if (likely(nlh->nlmsg_len > min_len)) {
		struct nfattr *attr = NFM_NFA(NLMSG_DATA(nlh));
		int attrlen = nlh->nlmsg_len - NLMSG_ALIGN(min_len);
		nfattr_parse(cda, attr_count, attr, attrlen);
	}

	/* implicit: if nlmsg_len == min_len, we return 0, and an empty
	 * (zeroed) cda[] array. The message is valid, but empty. */

	return 0;
}

int nfnetlink_has_listeners(unsigned int group)
{
	return netlink_has_listeners(nfnl, group);
}
EXPORT_SYMBOL_GPL(nfnetlink_has_listeners);

int nfnetlink_send(struct sk_buff *skb, u32 pid, unsigned group, int echo)
{
	int err = 0;

	NETLINK_CB(skb).dst_group = group;
	if (echo)
		atomic_inc(&skb->users);
	netlink_broadcast(nfnl, skb, pid, group, gfp_any());
	if (echo)
		err = netlink_unicast(nfnl, skb, pid, MSG_DONTWAIT);

	return err;
}
EXPORT_SYMBOL_GPL(nfnetlink_send);

int nfnetlink_unicast(struct sk_buff *skb, u_int32_t pid, int flags)
{
	return netlink_unicast(nfnl, skb, pid, flags);
}
EXPORT_SYMBOL_GPL(nfnetlink_unicast);

/* Process one complete nfnetlink message. */
static int nfnetlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct nfnl_callback *nc;
	struct nfnetlink_subsystem *ss;
	int type, err;

	if (security_netlink_recv(skb, CAP_NET_ADMIN))
		return -EPERM;

	/* All the messages must at least contain nfgenmsg */
	if (nlh->nlmsg_len < NLMSG_SPACE(sizeof(struct nfgenmsg)))
		return 0;

	type = nlh->nlmsg_type;
	ss = nfnetlink_get_subsys(type);
	if (!ss) {
#ifdef CONFIG_KMOD
		/* don't call nfnl_unlock, since it would reenter
		 * with further packet processing */
		__nfnl_unlock();
		request_module("nfnetlink-subsys-%d", NFNL_SUBSYS_ID(type));
		nfnl_lock();
		ss = nfnetlink_get_subsys(type);
		if (!ss)
#endif
			return -EINVAL;
	}

	nc = nfnetlink_find_client(type, ss);
	if (!nc)
		return -EINVAL;

	{
		u_int16_t attr_count =
			ss->cb[NFNL_MSG_TYPE(nlh->nlmsg_type)].attr_count;
		struct nfattr *cda[attr_count];

		memset(cda, 0, sizeof(struct nfattr *) * attr_count);

		err = nfnetlink_check_attributes(ss, nlh, cda);
		if (err < 0)
			return err;
		return nc->call(nfnl, skb, nlh, cda);
	}
}

static void nfnetlink_rcv(struct sock *sk, int len)
{
	unsigned int qlen = 0;

	do {
		if (nfnl_trylock())
			return;
		netlink_run_queue(sk, &qlen, nfnetlink_rcv_msg);
		__nfnl_unlock();
	} while (qlen);
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
				     nfnetlink_rcv, NULL, THIS_MODULE);
	if (!nfnl) {
		printk(KERN_ERR "cannot initialize nfnetlink!\n");
		return -1;
	}

	return 0;
}

module_init(nfnetlink_init);
module_exit(nfnetlink_exit);
