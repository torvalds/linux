// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP)
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/if_arp.h>
#include <linux/net.h>
#include <linux/mctp.h>
#include <linux/module.h>
#include <linux/socket.h>

#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/sock.h>

#define CREATE_TRACE_POINTS
#include <trace/events/mctp.h>

/* socket implementation */

static int mctp_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		sock->sk = NULL;
		sk->sk_prot->close(sk, 0);
	}

	return 0;
}

static int mctp_bind(struct socket *sock, struct sockaddr *addr, int addrlen)
{
	struct sock *sk = sock->sk;
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);
	struct sockaddr_mctp *smctp;
	int rc;

	if (addrlen < sizeof(*smctp))
		return -EINVAL;

	if (addr->sa_family != AF_MCTP)
		return -EAFNOSUPPORT;

	if (!capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	/* it's a valid sockaddr for MCTP, cast and do protocol checks */
	smctp = (struct sockaddr_mctp *)addr;

	lock_sock(sk);

	/* TODO: allow rebind */
	if (sk_hashed(sk)) {
		rc = -EADDRINUSE;
		goto out_release;
	}
	msk->bind_net = smctp->smctp_network;
	msk->bind_addr = smctp->smctp_addr.s_addr;
	msk->bind_type = smctp->smctp_type & 0x7f; /* ignore the IC bit */

	rc = sk->sk_prot->hash(sk);

out_release:
	release_sock(sk);

	return rc;
}

static int mctp_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	DECLARE_SOCKADDR(struct sockaddr_mctp *, addr, msg->msg_name);
	const int hlen = MCTP_HEADER_MAXLEN + sizeof(struct mctp_hdr);
	int rc, addrlen = msg->msg_namelen;
	struct sock *sk = sock->sk;
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);
	struct mctp_skb_cb *cb;
	struct mctp_route *rt;
	struct sk_buff *skb;

	if (addr) {
		if (addrlen < sizeof(struct sockaddr_mctp))
			return -EINVAL;
		if (addr->smctp_family != AF_MCTP)
			return -EINVAL;
		if (addr->smctp_tag & ~(MCTP_TAG_MASK | MCTP_TAG_OWNER))
			return -EINVAL;

	} else {
		/* TODO: connect()ed sockets */
		return -EDESTADDRREQ;
	}

	if (!capable(CAP_NET_RAW))
		return -EACCES;

	if (addr->smctp_network == MCTP_NET_ANY)
		addr->smctp_network = mctp_default_net(sock_net(sk));

	skb = sock_alloc_send_skb(sk, hlen + 1 + len,
				  msg->msg_flags & MSG_DONTWAIT, &rc);
	if (!skb)
		return rc;

	skb_reserve(skb, hlen);

	/* set type as fist byte in payload */
	*(u8 *)skb_put(skb, 1) = addr->smctp_type;

	rc = memcpy_from_msg((void *)skb_put(skb, len), msg, len);
	if (rc < 0)
		goto err_free;

	/* set up cb */
	cb = __mctp_cb(skb);
	cb->net = addr->smctp_network;

	/* direct addressing */
	if (msk->addr_ext && addrlen >= sizeof(struct sockaddr_mctp_ext)) {
		DECLARE_SOCKADDR(struct sockaddr_mctp_ext *,
				 extaddr, msg->msg_name);

		if (extaddr->smctp_halen > sizeof(cb->haddr)) {
			rc = -EINVAL;
			goto err_free;
		}

		cb->ifindex = extaddr->smctp_ifindex;
		cb->halen = extaddr->smctp_halen;
		memcpy(cb->haddr, extaddr->smctp_haddr, cb->halen);

		rt = NULL;
	} else {
		rt = mctp_route_lookup(sock_net(sk), addr->smctp_network,
				       addr->smctp_addr.s_addr);
		if (!rt) {
			rc = -EHOSTUNREACH;
			goto err_free;
		}
	}

	rc = mctp_local_output(sk, rt, skb, addr->smctp_addr.s_addr,
			       addr->smctp_tag);

	return rc ? : len;

err_free:
	kfree_skb(skb);
	return rc;
}

static int mctp_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			int flags)
{
	DECLARE_SOCKADDR(struct sockaddr_mctp *, addr, msg->msg_name);
	struct sock *sk = sock->sk;
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);
	struct sk_buff *skb;
	size_t msglen;
	u8 type;
	int rc;

	if (flags & ~(MSG_DONTWAIT | MSG_TRUNC | MSG_PEEK))
		return -EOPNOTSUPP;

	skb = skb_recv_datagram(sk, flags, flags & MSG_DONTWAIT, &rc);
	if (!skb)
		return rc;

	if (!skb->len) {
		rc = 0;
		goto out_free;
	}

	/* extract message type, remove from data */
	type = *((u8 *)skb->data);
	msglen = skb->len - 1;

	if (len < msglen)
		msg->msg_flags |= MSG_TRUNC;
	else
		len = msglen;

	rc = skb_copy_datagram_msg(skb, 1, msg, len);
	if (rc < 0)
		goto out_free;

	sock_recv_ts_and_drops(msg, sk, skb);

	if (addr) {
		struct mctp_skb_cb *cb = mctp_cb(skb);
		/* TODO: expand mctp_skb_cb for header fields? */
		struct mctp_hdr *hdr = mctp_hdr(skb);

		addr = msg->msg_name;
		addr->smctp_family = AF_MCTP;
		addr->smctp_network = cb->net;
		addr->smctp_addr.s_addr = hdr->src;
		addr->smctp_type = type;
		addr->smctp_tag = hdr->flags_seq_tag &
					(MCTP_HDR_TAG_MASK | MCTP_HDR_FLAG_TO);
		msg->msg_namelen = sizeof(*addr);

		if (msk->addr_ext) {
			DECLARE_SOCKADDR(struct sockaddr_mctp_ext *, ae,
					 msg->msg_name);
			msg->msg_namelen = sizeof(*ae);
			ae->smctp_ifindex = cb->ifindex;
			ae->smctp_halen = cb->halen;
			memset(ae->smctp_haddr, 0x0, sizeof(ae->smctp_haddr));
			memcpy(ae->smctp_haddr, cb->haddr, cb->halen);
		}
	}

	rc = len;

	if (flags & MSG_TRUNC)
		rc = msglen;

out_free:
	skb_free_datagram(sk, skb);
	return rc;
}

static int mctp_setsockopt(struct socket *sock, int level, int optname,
			   sockptr_t optval, unsigned int optlen)
{
	struct mctp_sock *msk = container_of(sock->sk, struct mctp_sock, sk);
	int val;

	if (level != SOL_MCTP)
		return -EINVAL;

	if (optname == MCTP_OPT_ADDR_EXT) {
		if (optlen != sizeof(int))
			return -EINVAL;
		if (copy_from_sockptr(&val, optval, sizeof(int)))
			return -EFAULT;
		msk->addr_ext = val;
		return 0;
	}

	return -ENOPROTOOPT;
}

static int mctp_getsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, int __user *optlen)
{
	struct mctp_sock *msk = container_of(sock->sk, struct mctp_sock, sk);
	int len, val;

	if (level != SOL_MCTP)
		return -EINVAL;

	if (get_user(len, optlen))
		return -EFAULT;

	if (optname == MCTP_OPT_ADDR_EXT) {
		if (len != sizeof(int))
			return -EINVAL;
		val = !!msk->addr_ext;
		if (copy_to_user(optval, &val, len))
			return -EFAULT;
		return 0;
	}

	return -EINVAL;
}

static const struct proto_ops mctp_dgram_ops = {
	.family		= PF_MCTP,
	.release	= mctp_release,
	.bind		= mctp_bind,
	.connect	= sock_no_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= sock_no_getname,
	.poll		= datagram_poll,
	.ioctl		= sock_no_ioctl,
	.gettstamp	= sock_gettstamp,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= mctp_setsockopt,
	.getsockopt	= mctp_getsockopt,
	.sendmsg	= mctp_sendmsg,
	.recvmsg	= mctp_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

static void mctp_sk_expire_keys(struct timer_list *timer)
{
	struct mctp_sock *msk = container_of(timer, struct mctp_sock,
					     key_expiry);
	struct net *net = sock_net(&msk->sk);
	unsigned long next_expiry, flags;
	struct mctp_sk_key *key;
	struct hlist_node *tmp;
	bool next_expiry_valid = false;

	spin_lock_irqsave(&net->mctp.keys_lock, flags);

	hlist_for_each_entry_safe(key, tmp, &msk->keys, sklist) {
		spin_lock(&key->lock);

		if (!time_after_eq(key->expiry, jiffies)) {
			trace_mctp_key_release(key, MCTP_TRACE_KEY_TIMEOUT);
			key->valid = false;
			hlist_del_rcu(&key->hlist);
			hlist_del_rcu(&key->sklist);
			spin_unlock(&key->lock);
			mctp_key_unref(key);
			continue;
		}

		if (next_expiry_valid) {
			if (time_before(key->expiry, next_expiry))
				next_expiry = key->expiry;
		} else {
			next_expiry = key->expiry;
			next_expiry_valid = true;
		}
		spin_unlock(&key->lock);
	}

	spin_unlock_irqrestore(&net->mctp.keys_lock, flags);

	if (next_expiry_valid)
		mod_timer(timer, next_expiry);
}

static int mctp_sk_init(struct sock *sk)
{
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);

	INIT_HLIST_HEAD(&msk->keys);
	timer_setup(&msk->key_expiry, mctp_sk_expire_keys, 0);
	return 0;
}

static void mctp_sk_close(struct sock *sk, long timeout)
{
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);

	del_timer_sync(&msk->key_expiry);
	sk_common_release(sk);
}

static int mctp_sk_hash(struct sock *sk)
{
	struct net *net = sock_net(sk);

	mutex_lock(&net->mctp.bind_lock);
	sk_add_node_rcu(sk, &net->mctp.binds);
	mutex_unlock(&net->mctp.bind_lock);

	return 0;
}

static void mctp_sk_unhash(struct sock *sk)
{
	struct mctp_sock *msk = container_of(sk, struct mctp_sock, sk);
	struct net *net = sock_net(sk);
	struct mctp_sk_key *key;
	struct hlist_node *tmp;
	unsigned long flags;

	/* remove from any type-based binds */
	mutex_lock(&net->mctp.bind_lock);
	sk_del_node_init_rcu(sk);
	mutex_unlock(&net->mctp.bind_lock);

	/* remove tag allocations */
	spin_lock_irqsave(&net->mctp.keys_lock, flags);
	hlist_for_each_entry_safe(key, tmp, &msk->keys, sklist) {
		hlist_del(&key->sklist);
		hlist_del(&key->hlist);

		trace_mctp_key_release(key, MCTP_TRACE_KEY_CLOSED);

		spin_lock(&key->lock);
		if (key->reasm_head)
			kfree_skb(key->reasm_head);
		key->reasm_head = NULL;
		key->reasm_dead = true;
		key->valid = false;
		spin_unlock(&key->lock);

		/* key is no longer on the lookup lists, unref */
		mctp_key_unref(key);
	}
	spin_unlock_irqrestore(&net->mctp.keys_lock, flags);
}

static struct proto mctp_proto = {
	.name		= "MCTP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct mctp_sock),
	.init		= mctp_sk_init,
	.close		= mctp_sk_close,
	.hash		= mctp_sk_hash,
	.unhash		= mctp_sk_unhash,
};

static int mctp_pf_create(struct net *net, struct socket *sock,
			  int protocol, int kern)
{
	const struct proto_ops *ops;
	struct proto *proto;
	struct sock *sk;
	int rc;

	if (protocol)
		return -EPROTONOSUPPORT;

	/* only datagram sockets are supported */
	if (sock->type != SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;

	proto = &mctp_proto;
	ops = &mctp_dgram_ops;

	sock->state = SS_UNCONNECTED;
	sock->ops = ops;

	sk = sk_alloc(net, PF_MCTP, GFP_KERNEL, proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);

	rc = 0;
	if (sk->sk_prot->init)
		rc = sk->sk_prot->init(sk);

	if (rc)
		goto err_sk_put;

	return 0;

err_sk_put:
	sock_orphan(sk);
	sock_put(sk);
	return rc;
}

static struct net_proto_family mctp_pf = {
	.family = PF_MCTP,
	.create = mctp_pf_create,
	.owner = THIS_MODULE,
};

static __init int mctp_init(void)
{
	int rc;

	/* ensure our uapi tag definitions match the header format */
	BUILD_BUG_ON(MCTP_TAG_OWNER != MCTP_HDR_FLAG_TO);
	BUILD_BUG_ON(MCTP_TAG_MASK != MCTP_HDR_TAG_MASK);

	pr_info("mctp: management component transport protocol core\n");

	rc = sock_register(&mctp_pf);
	if (rc)
		return rc;

	rc = proto_register(&mctp_proto, 0);
	if (rc)
		goto err_unreg_sock;

	rc = mctp_routes_init();
	if (rc)
		goto err_unreg_proto;

	rc = mctp_neigh_init();
	if (rc)
		goto err_unreg_proto;

	mctp_device_init();

	return 0;

err_unreg_proto:
	proto_unregister(&mctp_proto);
err_unreg_sock:
	sock_unregister(PF_MCTP);

	return rc;
}

static __exit void mctp_exit(void)
{
	mctp_device_exit();
	mctp_neigh_exit();
	mctp_routes_exit();
	proto_unregister(&mctp_proto);
	sock_unregister(PF_MCTP);
}

subsys_initcall(mctp_init);
module_exit(mctp_exit);

MODULE_DESCRIPTION("MCTP core");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jeremy Kerr <jk@codeconstruct.com.au>");

MODULE_ALIAS_NETPROTO(PF_MCTP);
