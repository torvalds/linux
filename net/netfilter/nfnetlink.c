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
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <asm/uaccess.h>
#include <net/sock.h>
#include <linux/init.h>

#include <net/netlink.h>
#include <linux/netfilter/nfnetlink.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_NETFILTER);

#define nfnl_dereference_protected(id) \
	rcu_dereference_protected(table[(id)].subsys, \
				  lockdep_nfnl_is_held((id)))

static char __initdata nfversion[] = "0.30";

static struct {
	struct mutex				mutex;
	const struct nfnetlink_subsystem __rcu	*subsys;
} table[NFNL_SUBSYS_COUNT];

static const int nfnl_group2type[NFNLGRP_MAX+1] = {
	[NFNLGRP_CONNTRACK_NEW]		= NFNL_SUBSYS_CTNETLINK,
	[NFNLGRP_CONNTRACK_UPDATE]	= NFNL_SUBSYS_CTNETLINK,
	[NFNLGRP_CONNTRACK_DESTROY]	= NFNL_SUBSYS_CTNETLINK,
	[NFNLGRP_CONNTRACK_EXP_NEW]	= NFNL_SUBSYS_CTNETLINK_EXP,
	[NFNLGRP_CONNTRACK_EXP_UPDATE]	= NFNL_SUBSYS_CTNETLINK_EXP,
	[NFNLGRP_CONNTRACK_EXP_DESTROY] = NFNL_SUBSYS_CTNETLINK_EXP,
	[NFNLGRP_NFTABLES]		= NFNL_SUBSYS_NFTABLES,
	[NFNLGRP_ACCT_QUOTA]		= NFNL_SUBSYS_ACCT,
	[NFNLGRP_NFTRACE]		= NFNL_SUBSYS_NFTABLES,
};

void nfnl_lock(__u8 subsys_id)
{
	mutex_lock(&table[subsys_id].mutex);
}
EXPORT_SYMBOL_GPL(nfnl_lock);

void nfnl_unlock(__u8 subsys_id)
{
	mutex_unlock(&table[subsys_id].mutex);
}
EXPORT_SYMBOL_GPL(nfnl_unlock);

#ifdef CONFIG_PROVE_LOCKING
bool lockdep_nfnl_is_held(u8 subsys_id)
{
	return lockdep_is_held(&table[subsys_id].mutex);
}
EXPORT_SYMBOL_GPL(lockdep_nfnl_is_held);
#endif

int nfnetlink_subsys_register(const struct nfnetlink_subsystem *n)
{
	nfnl_lock(n->subsys_id);
	if (table[n->subsys_id].subsys) {
		nfnl_unlock(n->subsys_id);
		return -EBUSY;
	}
	rcu_assign_pointer(table[n->subsys_id].subsys, n);
	nfnl_unlock(n->subsys_id);

	return 0;
}
EXPORT_SYMBOL_GPL(nfnetlink_subsys_register);

int nfnetlink_subsys_unregister(const struct nfnetlink_subsystem *n)
{
	nfnl_lock(n->subsys_id);
	table[n->subsys_id].subsys = NULL;
	nfnl_unlock(n->subsys_id);
	synchronize_rcu();
	return 0;
}
EXPORT_SYMBOL_GPL(nfnetlink_subsys_unregister);

static inline const struct nfnetlink_subsystem *nfnetlink_get_subsys(u_int16_t type)
{
	u_int8_t subsys_id = NFNL_SUBSYS_ID(type);

	if (subsys_id >= NFNL_SUBSYS_COUNT)
		return NULL;

	return rcu_dereference(table[subsys_id].subsys);
}

static inline const struct nfnl_callback *
nfnetlink_find_client(u_int16_t type, const struct nfnetlink_subsystem *ss)
{
	u_int8_t cb_id = NFNL_MSG_TYPE(type);

	if (cb_id >= ss->cb_count)
		return NULL;

	return &ss->cb[cb_id];
}

int nfnetlink_has_listeners(struct net *net, unsigned int group)
{
	return netlink_has_listeners(net->nfnl, group);
}
EXPORT_SYMBOL_GPL(nfnetlink_has_listeners);

struct sk_buff *nfnetlink_alloc_skb(struct net *net, unsigned int size,
				    u32 dst_portid, gfp_t gfp_mask)
{
	return netlink_alloc_skb(net->nfnl, size, dst_portid, gfp_mask);
}
EXPORT_SYMBOL_GPL(nfnetlink_alloc_skb);

int nfnetlink_send(struct sk_buff *skb, struct net *net, u32 portid,
		   unsigned int group, int echo, gfp_t flags)
{
	return nlmsg_notify(net->nfnl, skb, portid, group, echo, flags);
}
EXPORT_SYMBOL_GPL(nfnetlink_send);

int nfnetlink_set_err(struct net *net, u32 portid, u32 group, int error)
{
	return netlink_set_err(net->nfnl, portid, group, error);
}
EXPORT_SYMBOL_GPL(nfnetlink_set_err);

int nfnetlink_unicast(struct sk_buff *skb, struct net *net, u32 portid,
		      int flags)
{
	return netlink_unicast(net->nfnl, skb, portid, flags);
}
EXPORT_SYMBOL_GPL(nfnetlink_unicast);

/* Process one complete nfnetlink message. */
static int nfnetlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct net *net = sock_net(skb->sk);
	const struct nfnl_callback *nc;
	const struct nfnetlink_subsystem *ss;
	int type, err;

	/* All the messages must at least contain nfgenmsg */
	if (nlmsg_len(nlh) < sizeof(struct nfgenmsg))
		return 0;

	type = nlh->nlmsg_type;
replay:
	rcu_read_lock();
	ss = nfnetlink_get_subsys(type);
	if (!ss) {
#ifdef CONFIG_MODULES
		rcu_read_unlock();
		request_module("nfnetlink-subsys-%d", NFNL_SUBSYS_ID(type));
		rcu_read_lock();
		ss = nfnetlink_get_subsys(type);
		if (!ss)
#endif
		{
			rcu_read_unlock();
			return -EINVAL;
		}
	}

	nc = nfnetlink_find_client(type, ss);
	if (!nc) {
		rcu_read_unlock();
		return -EINVAL;
	}

	{
		int min_len = nlmsg_total_size(sizeof(struct nfgenmsg));
		u_int8_t cb_id = NFNL_MSG_TYPE(nlh->nlmsg_type);
		struct nlattr *cda[ss->cb[cb_id].attr_count + 1];
		struct nlattr *attr = (void *)nlh + min_len;
		int attrlen = nlh->nlmsg_len - min_len;
		__u8 subsys_id = NFNL_SUBSYS_ID(type);

		err = nla_parse(cda, ss->cb[cb_id].attr_count,
				attr, attrlen, ss->cb[cb_id].policy);
		if (err < 0) {
			rcu_read_unlock();
			return err;
		}

		if (nc->call_rcu) {
			err = nc->call_rcu(net, net->nfnl, skb, nlh,
					   (const struct nlattr **)cda);
			rcu_read_unlock();
		} else {
			rcu_read_unlock();
			nfnl_lock(subsys_id);
			if (nfnl_dereference_protected(subsys_id) != ss ||
			    nfnetlink_find_client(type, ss) != nc)
				err = -EAGAIN;
			else if (nc->call)
				err = nc->call(net, net->nfnl, skb, nlh,
					       (const struct nlattr **)cda);
			else
				err = -EINVAL;
			nfnl_unlock(subsys_id);
		}
		if (err == -EAGAIN)
			goto replay;
		return err;
	}
}

struct nfnl_err {
	struct list_head	head;
	struct nlmsghdr		*nlh;
	int			err;
};

static int nfnl_err_add(struct list_head *list, struct nlmsghdr *nlh, int err)
{
	struct nfnl_err *nfnl_err;

	nfnl_err = kmalloc(sizeof(struct nfnl_err), GFP_KERNEL);
	if (nfnl_err == NULL)
		return -ENOMEM;

	nfnl_err->nlh = nlh;
	nfnl_err->err = err;
	list_add_tail(&nfnl_err->head, list);

	return 0;
}

static void nfnl_err_del(struct nfnl_err *nfnl_err)
{
	list_del(&nfnl_err->head);
	kfree(nfnl_err);
}

static void nfnl_err_reset(struct list_head *err_list)
{
	struct nfnl_err *nfnl_err, *next;

	list_for_each_entry_safe(nfnl_err, next, err_list, head)
		nfnl_err_del(nfnl_err);
}

static void nfnl_err_deliver(struct list_head *err_list, struct sk_buff *skb)
{
	struct nfnl_err *nfnl_err, *next;

	list_for_each_entry_safe(nfnl_err, next, err_list, head) {
		netlink_ack(skb, nfnl_err->nlh, nfnl_err->err);
		nfnl_err_del(nfnl_err);
	}
}

enum {
	NFNL_BATCH_FAILURE	= (1 << 0),
	NFNL_BATCH_DONE		= (1 << 1),
	NFNL_BATCH_REPLAY	= (1 << 2),
};

static void nfnetlink_rcv_batch(struct sk_buff *skb, struct nlmsghdr *nlh,
				u_int16_t subsys_id)
{
	struct sk_buff *oskb = skb;
	struct net *net = sock_net(skb->sk);
	const struct nfnetlink_subsystem *ss;
	const struct nfnl_callback *nc;
	static LIST_HEAD(err_list);
	u32 status;
	int err;

	if (subsys_id >= NFNL_SUBSYS_COUNT)
		return netlink_ack(skb, nlh, -EINVAL);
replay:
	status = 0;

	skb = netlink_skb_clone(oskb, GFP_KERNEL);
	if (!skb)
		return netlink_ack(oskb, nlh, -ENOMEM);

	nfnl_lock(subsys_id);
	ss = nfnl_dereference_protected(subsys_id);
	if (!ss) {
#ifdef CONFIG_MODULES
		nfnl_unlock(subsys_id);
		request_module("nfnetlink-subsys-%d", subsys_id);
		nfnl_lock(subsys_id);
		ss = nfnl_dereference_protected(subsys_id);
		if (!ss)
#endif
		{
			nfnl_unlock(subsys_id);
			netlink_ack(oskb, nlh, -EOPNOTSUPP);
			return kfree_skb(skb);
		}
	}

	if (!ss->commit || !ss->abort) {
		nfnl_unlock(subsys_id);
		netlink_ack(oskb, nlh, -EOPNOTSUPP);
		return kfree_skb(skb);
	}

	while (skb->len >= nlmsg_total_size(0)) {
		int msglen, type;

		nlh = nlmsg_hdr(skb);
		err = 0;

		if (nlh->nlmsg_len < NLMSG_HDRLEN ||
		    skb->len < nlh->nlmsg_len ||
		    nlmsg_len(nlh) < sizeof(struct nfgenmsg)) {
			nfnl_err_reset(&err_list);
			status |= NFNL_BATCH_FAILURE;
			goto done;
		}

		/* Only requests are handled by the kernel */
		if (!(nlh->nlmsg_flags & NLM_F_REQUEST)) {
			err = -EINVAL;
			goto ack;
		}

		type = nlh->nlmsg_type;
		if (type == NFNL_MSG_BATCH_BEGIN) {
			/* Malformed: Batch begin twice */
			nfnl_err_reset(&err_list);
			status |= NFNL_BATCH_FAILURE;
			goto done;
		} else if (type == NFNL_MSG_BATCH_END) {
			status |= NFNL_BATCH_DONE;
			goto done;
		} else if (type < NLMSG_MIN_TYPE) {
			err = -EINVAL;
			goto ack;
		}

		/* We only accept a batch with messages for the same
		 * subsystem.
		 */
		if (NFNL_SUBSYS_ID(type) != subsys_id) {
			err = -EINVAL;
			goto ack;
		}

		nc = nfnetlink_find_client(type, ss);
		if (!nc) {
			err = -EINVAL;
			goto ack;
		}

		{
			int min_len = nlmsg_total_size(sizeof(struct nfgenmsg));
			u_int8_t cb_id = NFNL_MSG_TYPE(nlh->nlmsg_type);
			struct nlattr *cda[ss->cb[cb_id].attr_count + 1];
			struct nlattr *attr = (void *)nlh + min_len;
			int attrlen = nlh->nlmsg_len - min_len;

			err = nla_parse(cda, ss->cb[cb_id].attr_count,
					attr, attrlen, ss->cb[cb_id].policy);
			if (err < 0)
				goto ack;

			if (nc->call_batch) {
				err = nc->call_batch(net, net->nfnl, skb, nlh,
						     (const struct nlattr **)cda);
			}

			/* The lock was released to autoload some module, we
			 * have to abort and start from scratch using the
			 * original skb.
			 */
			if (err == -EAGAIN) {
				status |= NFNL_BATCH_REPLAY;
				goto next;
			}
		}
ack:
		if (nlh->nlmsg_flags & NLM_F_ACK || err) {
			/* Errors are delivered once the full batch has been
			 * processed, this avoids that the same error is
			 * reported several times when replaying the batch.
			 */
			if (nfnl_err_add(&err_list, nlh, err) < 0) {
				/* We failed to enqueue an error, reset the
				 * list of errors and send OOM to userspace
				 * pointing to the batch header.
				 */
				nfnl_err_reset(&err_list);
				netlink_ack(oskb, nlmsg_hdr(oskb), -ENOMEM);
				status |= NFNL_BATCH_FAILURE;
				goto done;
			}
			/* We don't stop processing the batch on errors, thus,
			 * userspace gets all the errors that the batch
			 * triggers.
			 */
			if (err)
				status |= NFNL_BATCH_FAILURE;
		}
next:
		msglen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (msglen > skb->len)
			msglen = skb->len;
		skb_pull(skb, msglen);
	}
done:
	if (status & NFNL_BATCH_REPLAY) {
		ss->abort(net, oskb);
		nfnl_err_reset(&err_list);
		nfnl_unlock(subsys_id);
		kfree_skb(skb);
		goto replay;
	} else if (status == NFNL_BATCH_DONE) {
		ss->commit(net, oskb);
	} else {
		ss->abort(net, oskb);
	}

	nfnl_err_deliver(&err_list, oskb);
	nfnl_unlock(subsys_id);
	kfree_skb(skb);
}

static void nfnetlink_rcv(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);
	u_int16_t res_id;
	int msglen;

	if (nlh->nlmsg_len < NLMSG_HDRLEN ||
	    skb->len < nlh->nlmsg_len)
		return;

	if (!netlink_net_capable(skb, CAP_NET_ADMIN)) {
		netlink_ack(skb, nlh, -EPERM);
		return;
	}

	if (nlh->nlmsg_type == NFNL_MSG_BATCH_BEGIN) {
		struct nfgenmsg *nfgenmsg;

		msglen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (msglen > skb->len)
			msglen = skb->len;

		if (nlh->nlmsg_len < NLMSG_HDRLEN ||
		    skb->len < NLMSG_HDRLEN + sizeof(struct nfgenmsg))
			return;

		nfgenmsg = nlmsg_data(nlh);
		skb_pull(skb, msglen);
		/* Work around old nft using host byte order */
		if (nfgenmsg->res_id == NFNL_SUBSYS_NFTABLES)
			res_id = NFNL_SUBSYS_NFTABLES;
		else
			res_id = ntohs(nfgenmsg->res_id);
		nfnetlink_rcv_batch(skb, nlh, res_id);
	} else {
		netlink_rcv_skb(skb, &nfnetlink_rcv_msg);
	}
}

#ifdef CONFIG_MODULES
static int nfnetlink_bind(struct net *net, int group)
{
	const struct nfnetlink_subsystem *ss;
	int type;

	if (group <= NFNLGRP_NONE || group > NFNLGRP_MAX)
		return 0;

	type = nfnl_group2type[group];

	rcu_read_lock();
	ss = nfnetlink_get_subsys(type << 8);
	rcu_read_unlock();
	if (!ss)
		request_module("nfnetlink-subsys-%d", type);
	return 0;
}
#endif

static int __net_init nfnetlink_net_init(struct net *net)
{
	struct sock *nfnl;
	struct netlink_kernel_cfg cfg = {
		.groups	= NFNLGRP_MAX,
		.input	= nfnetlink_rcv,
#ifdef CONFIG_MODULES
		.bind	= nfnetlink_bind,
#endif
	};

	nfnl = netlink_kernel_create(net, NETLINK_NETFILTER, &cfg);
	if (!nfnl)
		return -ENOMEM;
	net->nfnl_stash = nfnl;
	rcu_assign_pointer(net->nfnl, nfnl);
	return 0;
}

static void __net_exit nfnetlink_net_exit_batch(struct list_head *net_exit_list)
{
	struct net *net;

	list_for_each_entry(net, net_exit_list, exit_list)
		RCU_INIT_POINTER(net->nfnl, NULL);
	synchronize_net();
	list_for_each_entry(net, net_exit_list, exit_list)
		netlink_kernel_release(net->nfnl_stash);
}

static struct pernet_operations nfnetlink_net_ops = {
	.init		= nfnetlink_net_init,
	.exit_batch	= nfnetlink_net_exit_batch,
};

static int __init nfnetlink_init(void)
{
	int i;

	for (i = NFNLGRP_NONE + 1; i <= NFNLGRP_MAX; i++)
		BUG_ON(nfnl_group2type[i] == NFNL_SUBSYS_NONE);

	for (i=0; i<NFNL_SUBSYS_COUNT; i++)
		mutex_init(&table[i].mutex);

	pr_info("Netfilter messages via NETLINK v%s.\n", nfversion);
	return register_pernet_subsys(&nfnetlink_net_ops);
}

static void __exit nfnetlink_exit(void)
{
	pr_info("Removing netfilter NETLINK layer.\n");
	unregister_pernet_subsys(&nfnetlink_net_ops);
}
module_init(nfnetlink_init);
module_exit(nfnetlink_exit);
