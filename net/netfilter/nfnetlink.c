/* Netfilter messages via netlink socket. Allows for user space
 * protocol helpers and general trouble making from userspace.
 *
 * (C) 2001 by Jay Schulist <jschlst@samba.org>,
 * (C) 2002-2005 by Harald Welte <laforge@gnumonks.org>
 * (C) 2005-2017 by Pablo Neira Ayuso <pablo@netfilter.org>
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
#include <linux/uaccess.h>
#include <net/sock.h>
#include <linux/init.h>
#include <linux/sched/signal.h>

#include <net/netlink.h>
#include <net/netns/generic.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_ALIAS_NET_PF_PROTO(PF_NETLINK, NETLINK_NETFILTER);
MODULE_DESCRIPTION("Netfilter messages via netlink socket");

#define nfnl_dereference_protected(id) \
	rcu_dereference_protected(table[(id)].subsys, \
				  lockdep_nfnl_is_held((id)))

#define NFNL_MAX_ATTR_COUNT	32

static unsigned int nfnetlink_pernet_id __read_mostly;

#ifdef CONFIG_NF_CONNTRACK_EVENTS
static DEFINE_SPINLOCK(nfnl_grp_active_lock);
#endif

struct nfnl_net {
	struct sock *nfnl;
};

static struct {
	struct mutex				mutex;
	const struct nfnetlink_subsystem __rcu	*subsys;
} table[NFNL_SUBSYS_COUNT];

static struct lock_class_key nfnl_lockdep_keys[NFNL_SUBSYS_COUNT];

static const char *const nfnl_lockdep_names[NFNL_SUBSYS_COUNT] = {
	[NFNL_SUBSYS_NONE] = "nfnl_subsys_none",
	[NFNL_SUBSYS_CTNETLINK] = "nfnl_subsys_ctnetlink",
	[NFNL_SUBSYS_CTNETLINK_EXP] = "nfnl_subsys_ctnetlink_exp",
	[NFNL_SUBSYS_QUEUE] = "nfnl_subsys_queue",
	[NFNL_SUBSYS_ULOG] = "nfnl_subsys_ulog",
	[NFNL_SUBSYS_OSF] = "nfnl_subsys_osf",
	[NFNL_SUBSYS_IPSET] = "nfnl_subsys_ipset",
	[NFNL_SUBSYS_ACCT] = "nfnl_subsys_acct",
	[NFNL_SUBSYS_CTNETLINK_TIMEOUT] = "nfnl_subsys_cttimeout",
	[NFNL_SUBSYS_CTHELPER] = "nfnl_subsys_cthelper",
	[NFNL_SUBSYS_NFTABLES] = "nfnl_subsys_nftables",
	[NFNL_SUBSYS_NFT_COMPAT] = "nfnl_subsys_nftcompat",
	[NFNL_SUBSYS_HOOK] = "nfnl_subsys_hook",
};

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

static struct nfnl_net *nfnl_pernet(struct net *net)
{
	return net_generic(net, nfnetlink_pernet_id);
}

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
	u8 cb_id;

	/* Sanity-check attr_count size to avoid stack buffer overflow. */
	for (cb_id = 0; cb_id < n->cb_count; cb_id++)
		if (WARN_ON(n->cb[cb_id].attr_count > NFNL_MAX_ATTR_COUNT))
			return -EINVAL;

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

static inline const struct nfnetlink_subsystem *nfnetlink_get_subsys(u16 type)
{
	u8 subsys_id = NFNL_SUBSYS_ID(type);

	if (subsys_id >= NFNL_SUBSYS_COUNT)
		return NULL;

	return rcu_dereference(table[subsys_id].subsys);
}

static inline const struct nfnl_callback *
nfnetlink_find_client(u16 type, const struct nfnetlink_subsystem *ss)
{
	u8 cb_id = NFNL_MSG_TYPE(type);

	if (cb_id >= ss->cb_count)
		return NULL;

	return &ss->cb[cb_id];
}

int nfnetlink_has_listeners(struct net *net, unsigned int group)
{
	struct nfnl_net *nfnlnet = nfnl_pernet(net);

	return netlink_has_listeners(nfnlnet->nfnl, group);
}
EXPORT_SYMBOL_GPL(nfnetlink_has_listeners);

int nfnetlink_send(struct sk_buff *skb, struct net *net, u32 portid,
		   unsigned int group, int echo, gfp_t flags)
{
	struct nfnl_net *nfnlnet = nfnl_pernet(net);

	return nlmsg_notify(nfnlnet->nfnl, skb, portid, group, echo, flags);
}
EXPORT_SYMBOL_GPL(nfnetlink_send);

int nfnetlink_set_err(struct net *net, u32 portid, u32 group, int error)
{
	struct nfnl_net *nfnlnet = nfnl_pernet(net);

	return netlink_set_err(nfnlnet->nfnl, portid, group, error);
}
EXPORT_SYMBOL_GPL(nfnetlink_set_err);

int nfnetlink_unicast(struct sk_buff *skb, struct net *net, u32 portid)
{
	struct nfnl_net *nfnlnet = nfnl_pernet(net);
	int err;

	err = nlmsg_unicast(nfnlnet->nfnl, skb, portid);
	if (err == -EAGAIN)
		err = -ENOBUFS;

	return err;
}
EXPORT_SYMBOL_GPL(nfnetlink_unicast);

void nfnetlink_broadcast(struct net *net, struct sk_buff *skb, __u32 portid,
			 __u32 group, gfp_t allocation)
{
	struct nfnl_net *nfnlnet = nfnl_pernet(net);

	netlink_broadcast(nfnlnet->nfnl, skb, portid, group, allocation);
}
EXPORT_SYMBOL_GPL(nfnetlink_broadcast);

/* Process one complete nfnetlink message. */
static int nfnetlink_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh,
			     struct netlink_ext_ack *extack)
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
		struct nfnl_net *nfnlnet = nfnl_pernet(net);
		u8 cb_id = NFNL_MSG_TYPE(nlh->nlmsg_type);
		struct nlattr *cda[NFNL_MAX_ATTR_COUNT + 1];
		struct nlattr *attr = (void *)nlh + min_len;
		int attrlen = nlh->nlmsg_len - min_len;
		__u8 subsys_id = NFNL_SUBSYS_ID(type);
		struct nfnl_info info = {
			.net	= net,
			.sk	= nfnlnet->nfnl,
			.nlh	= nlh,
			.nfmsg	= nlmsg_data(nlh),
			.extack	= extack,
		};

		/* Sanity-check NFNL_MAX_ATTR_COUNT */
		if (ss->cb[cb_id].attr_count > NFNL_MAX_ATTR_COUNT) {
			rcu_read_unlock();
			return -ENOMEM;
		}

		err = nla_parse_deprecated(cda, ss->cb[cb_id].attr_count,
					   attr, attrlen,
					   ss->cb[cb_id].policy, extack);
		if (err < 0) {
			rcu_read_unlock();
			return err;
		}

		if (!nc->call) {
			rcu_read_unlock();
			return -EINVAL;
		}

		switch (nc->type) {
		case NFNL_CB_RCU:
			err = nc->call(skb, &info, (const struct nlattr **)cda);
			rcu_read_unlock();
			break;
		case NFNL_CB_MUTEX:
			rcu_read_unlock();
			nfnl_lock(subsys_id);
			if (nfnl_dereference_protected(subsys_id) != ss ||
			    nfnetlink_find_client(type, ss) != nc) {
				nfnl_unlock(subsys_id);
				err = -EAGAIN;
				break;
			}
			err = nc->call(skb, &info, (const struct nlattr **)cda);
			nfnl_unlock(subsys_id);
			break;
		default:
			rcu_read_unlock();
			err = -EINVAL;
			break;
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
	struct netlink_ext_ack	extack;
};

static int nfnl_err_add(struct list_head *list, struct nlmsghdr *nlh, int err,
			const struct netlink_ext_ack *extack)
{
	struct nfnl_err *nfnl_err;

	nfnl_err = kmalloc(sizeof(struct nfnl_err), GFP_KERNEL);
	if (nfnl_err == NULL)
		return -ENOMEM;

	nfnl_err->nlh = nlh;
	nfnl_err->err = err;
	nfnl_err->extack = *extack;
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
		netlink_ack(skb, nfnl_err->nlh, nfnl_err->err,
			    &nfnl_err->extack);
		nfnl_err_del(nfnl_err);
	}
}

enum {
	NFNL_BATCH_FAILURE	= (1 << 0),
	NFNL_BATCH_DONE		= (1 << 1),
	NFNL_BATCH_REPLAY	= (1 << 2),
};

static void nfnetlink_rcv_batch(struct sk_buff *skb, struct nlmsghdr *nlh,
				u16 subsys_id, u32 genid)
{
	struct sk_buff *oskb = skb;
	struct net *net = sock_net(skb->sk);
	const struct nfnetlink_subsystem *ss;
	const struct nfnl_callback *nc;
	struct netlink_ext_ack extack;
	LIST_HEAD(err_list);
	u32 status;
	int err;

	if (subsys_id >= NFNL_SUBSYS_COUNT)
		return netlink_ack(skb, nlh, -EINVAL, NULL);
replay:
	status = 0;
replay_abort:
	skb = netlink_skb_clone(oskb, GFP_KERNEL);
	if (!skb)
		return netlink_ack(oskb, nlh, -ENOMEM, NULL);

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
			netlink_ack(oskb, nlh, -EOPNOTSUPP, NULL);
			return kfree_skb(skb);
		}
	}

	if (!ss->valid_genid || !ss->commit || !ss->abort) {
		nfnl_unlock(subsys_id);
		netlink_ack(oskb, nlh, -EOPNOTSUPP, NULL);
		return kfree_skb(skb);
	}

	if (!try_module_get(ss->owner)) {
		nfnl_unlock(subsys_id);
		netlink_ack(oskb, nlh, -EOPNOTSUPP, NULL);
		return kfree_skb(skb);
	}

	if (!ss->valid_genid(net, genid)) {
		module_put(ss->owner);
		nfnl_unlock(subsys_id);
		netlink_ack(oskb, nlh, -ERESTART, NULL);
		return kfree_skb(skb);
	}

	nfnl_unlock(subsys_id);

	if (nlh->nlmsg_flags & NLM_F_ACK) {
		memset(&extack, 0, sizeof(extack));
		nfnl_err_add(&err_list, nlh, 0, &extack);
	}

	while (skb->len >= nlmsg_total_size(0)) {
		int msglen, type;

		if (fatal_signal_pending(current)) {
			nfnl_err_reset(&err_list);
			err = -EINTR;
			status = NFNL_BATCH_FAILURE;
			goto done;
		}

		memset(&extack, 0, sizeof(extack));
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

		if (nc->type != NFNL_CB_BATCH) {
			err = -EINVAL;
			goto ack;
		}

		{
			int min_len = nlmsg_total_size(sizeof(struct nfgenmsg));
			struct nfnl_net *nfnlnet = nfnl_pernet(net);
			struct nlattr *cda[NFNL_MAX_ATTR_COUNT + 1];
			struct nlattr *attr = (void *)nlh + min_len;
			u8 cb_id = NFNL_MSG_TYPE(nlh->nlmsg_type);
			int attrlen = nlh->nlmsg_len - min_len;
			struct nfnl_info info = {
				.net	= net,
				.sk	= nfnlnet->nfnl,
				.nlh	= nlh,
				.nfmsg	= nlmsg_data(nlh),
				.extack	= &extack,
			};

			/* Sanity-check NFTA_MAX_ATTR */
			if (ss->cb[cb_id].attr_count > NFNL_MAX_ATTR_COUNT) {
				err = -ENOMEM;
				goto ack;
			}

			err = nla_parse_deprecated(cda,
						   ss->cb[cb_id].attr_count,
						   attr, attrlen,
						   ss->cb[cb_id].policy, NULL);
			if (err < 0)
				goto ack;

			err = nc->call(skb, &info, (const struct nlattr **)cda);

			/* The lock was released to autoload some module, we
			 * have to abort and start from scratch using the
			 * original skb.
			 */
			if (err == -EAGAIN) {
				status |= NFNL_BATCH_REPLAY;
				goto done;
			}
		}
ack:
		if (nlh->nlmsg_flags & NLM_F_ACK || err) {
			/* Errors are delivered once the full batch has been
			 * processed, this avoids that the same error is
			 * reported several times when replaying the batch.
			 */
			if (err == -ENOMEM ||
			    nfnl_err_add(&err_list, nlh, err, &extack) < 0) {
				/* We failed to enqueue an error, reset the
				 * list of errors and send OOM to userspace
				 * pointing to the batch header.
				 */
				nfnl_err_reset(&err_list);
				netlink_ack(oskb, nlmsg_hdr(oskb), -ENOMEM,
					    NULL);
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

		msglen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (msglen > skb->len)
			msglen = skb->len;
		skb_pull(skb, msglen);
	}
done:
	if (status & NFNL_BATCH_REPLAY) {
		ss->abort(net, oskb, NFNL_ABORT_AUTOLOAD);
		nfnl_err_reset(&err_list);
		kfree_skb(skb);
		module_put(ss->owner);
		goto replay;
	} else if (status == NFNL_BATCH_DONE) {
		err = ss->commit(net, oskb);
		if (err == -EAGAIN) {
			status |= NFNL_BATCH_REPLAY;
			goto done;
		} else if (err) {
			ss->abort(net, oskb, NFNL_ABORT_NONE);
			netlink_ack(oskb, nlmsg_hdr(oskb), err, NULL);
		} else if (nlh->nlmsg_flags & NLM_F_ACK) {
			memset(&extack, 0, sizeof(extack));
			nfnl_err_add(&err_list, nlh, 0, &extack);
		}
	} else {
		enum nfnl_abort_action abort_action;

		if (status & NFNL_BATCH_FAILURE)
			abort_action = NFNL_ABORT_NONE;
		else
			abort_action = NFNL_ABORT_VALIDATE;

		err = ss->abort(net, oskb, abort_action);
		if (err == -EAGAIN) {
			nfnl_err_reset(&err_list);
			kfree_skb(skb);
			module_put(ss->owner);
			status |= NFNL_BATCH_FAILURE;
			goto replay_abort;
		}
	}

	nfnl_err_deliver(&err_list, oskb);
	kfree_skb(skb);
	module_put(ss->owner);
}

static const struct nla_policy nfnl_batch_policy[NFNL_BATCH_MAX + 1] = {
	[NFNL_BATCH_GENID]	= { .type = NLA_U32 },
};

static void nfnetlink_rcv_skb_batch(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	int min_len = nlmsg_total_size(sizeof(struct nfgenmsg));
	struct nlattr *attr = (void *)nlh + min_len;
	struct nlattr *cda[NFNL_BATCH_MAX + 1];
	int attrlen = nlh->nlmsg_len - min_len;
	struct nfgenmsg *nfgenmsg;
	int msglen, err;
	u32 gen_id = 0;
	u16 res_id;

	msglen = NLMSG_ALIGN(nlh->nlmsg_len);
	if (msglen > skb->len)
		msglen = skb->len;

	if (skb->len < NLMSG_HDRLEN + sizeof(struct nfgenmsg))
		return;

	err = nla_parse_deprecated(cda, NFNL_BATCH_MAX, attr, attrlen,
				   nfnl_batch_policy, NULL);
	if (err < 0) {
		netlink_ack(skb, nlh, err, NULL);
		return;
	}
	if (cda[NFNL_BATCH_GENID])
		gen_id = ntohl(nla_get_be32(cda[NFNL_BATCH_GENID]));

	nfgenmsg = nlmsg_data(nlh);
	skb_pull(skb, msglen);
	/* Work around old nft using host byte order */
	if (nfgenmsg->res_id == (__force __be16)NFNL_SUBSYS_NFTABLES)
		res_id = NFNL_SUBSYS_NFTABLES;
	else
		res_id = ntohs(nfgenmsg->res_id);

	nfnetlink_rcv_batch(skb, nlh, res_id, gen_id);
}

static void nfnetlink_rcv(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = nlmsg_hdr(skb);

	if (skb->len < NLMSG_HDRLEN ||
	    nlh->nlmsg_len < NLMSG_HDRLEN ||
	    skb->len < nlh->nlmsg_len)
		return;

	if (!netlink_net_capable(skb, CAP_NET_ADMIN)) {
		netlink_ack(skb, nlh, -EPERM, NULL);
		return;
	}

	if (nlh->nlmsg_type == NFNL_MSG_BATCH_BEGIN)
		nfnetlink_rcv_skb_batch(skb, nlh);
	else
		netlink_rcv_skb(skb, nfnetlink_rcv_msg);
}

static void nfnetlink_bind_event(struct net *net, unsigned int group)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	int type, group_bit;
	u8 v;

	/* All NFNLGRP_CONNTRACK_* group bits fit into u8.
	 * The other groups are not relevant and can be ignored.
	 */
	if (group >= 8)
		return;

	type = nfnl_group2type[group];

	switch (type) {
	case NFNL_SUBSYS_CTNETLINK:
		break;
	case NFNL_SUBSYS_CTNETLINK_EXP:
		break;
	default:
		return;
	}

	group_bit = (1 << group);

	spin_lock(&nfnl_grp_active_lock);
	v = READ_ONCE(nf_ctnetlink_has_listener);
	if ((v & group_bit) == 0) {
		v |= group_bit;

		/* read concurrently without nfnl_grp_active_lock held. */
		WRITE_ONCE(nf_ctnetlink_has_listener, v);
	}

	spin_unlock(&nfnl_grp_active_lock);
#endif
}

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
		request_module_nowait("nfnetlink-subsys-%d", type);

	nfnetlink_bind_event(net, group);
	return 0;
}

static void nfnetlink_unbind(struct net *net, int group)
{
#ifdef CONFIG_NF_CONNTRACK_EVENTS
	int type, group_bit;

	if (group <= NFNLGRP_NONE || group > NFNLGRP_MAX)
		return;

	type = nfnl_group2type[group];

	switch (type) {
	case NFNL_SUBSYS_CTNETLINK:
		break;
	case NFNL_SUBSYS_CTNETLINK_EXP:
		break;
	default:
		return;
	}

	/* ctnetlink_has_listener is u8 */
	if (group >= 8)
		return;

	group_bit = (1 << group);

	spin_lock(&nfnl_grp_active_lock);
	if (!nfnetlink_has_listeners(net, group)) {
		u8 v = READ_ONCE(nf_ctnetlink_has_listener);

		v &= ~group_bit;

		/* read concurrently without nfnl_grp_active_lock held. */
		WRITE_ONCE(nf_ctnetlink_has_listener, v);
	}
	spin_unlock(&nfnl_grp_active_lock);
#endif
}

static int __net_init nfnetlink_net_init(struct net *net)
{
	struct nfnl_net *nfnlnet = nfnl_pernet(net);
	struct netlink_kernel_cfg cfg = {
		.groups	= NFNLGRP_MAX,
		.input	= nfnetlink_rcv,
		.bind	= nfnetlink_bind,
		.unbind	= nfnetlink_unbind,
	};

	nfnlnet->nfnl = netlink_kernel_create(net, NETLINK_NETFILTER, &cfg);
	if (!nfnlnet->nfnl)
		return -ENOMEM;
	return 0;
}

static void __net_exit nfnetlink_net_exit_batch(struct list_head *net_exit_list)
{
	struct nfnl_net *nfnlnet;
	struct net *net;

	list_for_each_entry(net, net_exit_list, exit_list) {
		nfnlnet = nfnl_pernet(net);

		netlink_kernel_release(nfnlnet->nfnl);
	}
}

static struct pernet_operations nfnetlink_net_ops = {
	.init		= nfnetlink_net_init,
	.exit_batch	= nfnetlink_net_exit_batch,
	.id		= &nfnetlink_pernet_id,
	.size		= sizeof(struct nfnl_net),
};

static int __init nfnetlink_init(void)
{
	int i;

	for (i = NFNLGRP_NONE + 1; i <= NFNLGRP_MAX; i++)
		BUG_ON(nfnl_group2type[i] == NFNL_SUBSYS_NONE);

	for (i=0; i<NFNL_SUBSYS_COUNT; i++)
		__mutex_init(&table[i].mutex, nfnl_lockdep_names[i], &nfnl_lockdep_keys[i]);

	return register_pernet_subsys(&nfnetlink_net_ops);
}

static void __exit nfnetlink_exit(void)
{
	unregister_pernet_subsys(&nfnetlink_net_ops);
}
module_init(nfnetlink_init);
module_exit(nfnetlink_exit);
