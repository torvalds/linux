// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2010-2011 EIA Electronics,
//                         Pieter Beyens <pieter.beyens@eia.be>
// Copyright (c) 2010-2011 EIA Electronics,
//                         Kurt Van Dijck <kurt.van.dijck@eia.be>
// Copyright (c) 2018 Protonic,
//                         Robin van der Gracht <robin@protonic.nl>
// Copyright (c) 2017-2019 Pengutronix,
//                         Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (c) 2017-2019 Pengutronix,
//                         Oleksij Rempel <kernel@pengutronix.de>

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/can/can-ml.h>
#include <linux/can/core.h>
#include <linux/can/skb.h>
#include <linux/errqueue.h>
#include <linux/if_arp.h>

#include "j1939-priv.h"

#define J1939_MIN_NAMELEN CAN_REQUIRED_SIZE(struct sockaddr_can, can_addr.j1939)

/* conversion function between struct sock::sk_priority from linux and
 * j1939 priority field
 */
static inline priority_t j1939_prio(u32 sk_priority)
{
	sk_priority = min(sk_priority, 7U);

	return 7 - sk_priority;
}

static inline u32 j1939_to_sk_priority(priority_t prio)
{
	return 7 - prio;
}

/* function to see if pgn is to be evaluated */
static inline bool j1939_pgn_is_valid(pgn_t pgn)
{
	return pgn <= J1939_PGN_MAX;
}

/* test function to avoid non-zero DA placeholder for pdu1 pgn's */
static inline bool j1939_pgn_is_clean_pdu(pgn_t pgn)
{
	if (j1939_pgn_is_pdu1(pgn))
		return !(pgn & 0xff);
	else
		return true;
}

static inline void j1939_sock_pending_add(struct sock *sk)
{
	struct j1939_sock *jsk = j1939_sk(sk);

	atomic_inc(&jsk->skb_pending);
}

static int j1939_sock_pending_get(struct sock *sk)
{
	struct j1939_sock *jsk = j1939_sk(sk);

	return atomic_read(&jsk->skb_pending);
}

void j1939_sock_pending_del(struct sock *sk)
{
	struct j1939_sock *jsk = j1939_sk(sk);

	/* atomic_dec_return returns the new value */
	if (!atomic_dec_return(&jsk->skb_pending))
		wake_up(&jsk->waitq);	/* no pending SKB's */
}

static void j1939_jsk_add(struct j1939_priv *priv, struct j1939_sock *jsk)
{
	jsk->state |= J1939_SOCK_BOUND;
	j1939_priv_get(priv);

	write_lock_bh(&priv->j1939_socks_lock);
	list_add_tail(&jsk->list, &priv->j1939_socks);
	write_unlock_bh(&priv->j1939_socks_lock);
}

static void j1939_jsk_del(struct j1939_priv *priv, struct j1939_sock *jsk)
{
	write_lock_bh(&priv->j1939_socks_lock);
	list_del_init(&jsk->list);
	write_unlock_bh(&priv->j1939_socks_lock);

	j1939_priv_put(priv);
	jsk->state &= ~J1939_SOCK_BOUND;
}

static bool j1939_sk_queue_session(struct j1939_session *session)
{
	struct j1939_sock *jsk = j1939_sk(session->sk);
	bool empty;

	spin_lock_bh(&jsk->sk_session_queue_lock);
	empty = list_empty(&jsk->sk_session_queue);
	j1939_session_get(session);
	list_add_tail(&session->sk_session_queue_entry, &jsk->sk_session_queue);
	spin_unlock_bh(&jsk->sk_session_queue_lock);
	j1939_sock_pending_add(&jsk->sk);

	return empty;
}

static struct
j1939_session *j1939_sk_get_incomplete_session(struct j1939_sock *jsk)
{
	struct j1939_session *session = NULL;

	spin_lock_bh(&jsk->sk_session_queue_lock);
	if (!list_empty(&jsk->sk_session_queue)) {
		session = list_last_entry(&jsk->sk_session_queue,
					  struct j1939_session,
					  sk_session_queue_entry);
		if (session->total_queued_size == session->total_message_size)
			session = NULL;
		else
			j1939_session_get(session);
	}
	spin_unlock_bh(&jsk->sk_session_queue_lock);

	return session;
}

static void j1939_sk_queue_drop_all(struct j1939_priv *priv,
				    struct j1939_sock *jsk, int err)
{
	struct j1939_session *session, *tmp;

	netdev_dbg(priv->ndev, "%s: err: %i\n", __func__, err);
	spin_lock_bh(&jsk->sk_session_queue_lock);
	list_for_each_entry_safe(session, tmp, &jsk->sk_session_queue,
				 sk_session_queue_entry) {
		list_del_init(&session->sk_session_queue_entry);
		session->err = err;
		j1939_session_put(session);
	}
	spin_unlock_bh(&jsk->sk_session_queue_lock);
}

static void j1939_sk_queue_activate_next_locked(struct j1939_session *session)
{
	struct j1939_sock *jsk;
	struct j1939_session *first;
	int err;

	/* RX-Session don't have a socket (yet) */
	if (!session->sk)
		return;

	jsk = j1939_sk(session->sk);
	lockdep_assert_held(&jsk->sk_session_queue_lock);

	err = session->err;

	first = list_first_entry_or_null(&jsk->sk_session_queue,
					 struct j1939_session,
					 sk_session_queue_entry);

	/* Some else has already activated the next session */
	if (first != session)
		return;

activate_next:
	list_del_init(&first->sk_session_queue_entry);
	j1939_session_put(first);
	first = list_first_entry_or_null(&jsk->sk_session_queue,
					 struct j1939_session,
					 sk_session_queue_entry);
	if (!first)
		return;

	if (j1939_session_activate(first)) {
		netdev_warn_once(first->priv->ndev,
				 "%s: 0x%p: Identical session is already activated.\n",
				 __func__, first);
		first->err = -EBUSY;
		goto activate_next;
	} else {
		/* Give receiver some time (arbitrary chosen) to recover */
		int time_ms = 0;

		if (err)
			time_ms = 10 + prandom_u32_max(16);

		j1939_tp_schedule_txtimer(first, time_ms);
	}
}

void j1939_sk_queue_activate_next(struct j1939_session *session)
{
	struct j1939_sock *jsk;

	if (!session->sk)
		return;

	jsk = j1939_sk(session->sk);

	spin_lock_bh(&jsk->sk_session_queue_lock);
	j1939_sk_queue_activate_next_locked(session);
	spin_unlock_bh(&jsk->sk_session_queue_lock);
}

static bool j1939_sk_match_dst(struct j1939_sock *jsk,
			       const struct j1939_sk_buff_cb *skcb)
{
	if ((jsk->state & J1939_SOCK_PROMISC))
		return true;

	/* Destination address filter */
	if (jsk->addr.src_name && skcb->addr.dst_name) {
		if (jsk->addr.src_name != skcb->addr.dst_name)
			return false;
	} else {
		/* receive (all sockets) if
		 * - all packages that match our bind() address
		 * - all broadcast on a socket if SO_BROADCAST
		 *   is set
		 */
		if (j1939_address_is_unicast(skcb->addr.da)) {
			if (jsk->addr.sa != skcb->addr.da)
				return false;
		} else if (!sock_flag(&jsk->sk, SOCK_BROADCAST)) {
			/* receiving broadcast without SO_BROADCAST
			 * flag is not allowed
			 */
			return false;
		}
	}

	/* Source address filter */
	if (jsk->state & J1939_SOCK_CONNECTED) {
		/* receive (all sockets) if
		 * - all packages that match our connect() name or address
		 */
		if (jsk->addr.dst_name && skcb->addr.src_name) {
			if (jsk->addr.dst_name != skcb->addr.src_name)
				return false;
		} else {
			if (jsk->addr.da != skcb->addr.sa)
				return false;
		}
	}

	/* PGN filter */
	if (j1939_pgn_is_valid(jsk->pgn_rx_filter) &&
	    jsk->pgn_rx_filter != skcb->addr.pgn)
		return false;

	return true;
}

/* matches skb control buffer (addr) with a j1939 filter */
static bool j1939_sk_match_filter(struct j1939_sock *jsk,
				  const struct j1939_sk_buff_cb *skcb)
{
	const struct j1939_filter *f;
	int nfilter;

	spin_lock_bh(&jsk->filters_lock);

	f = jsk->filters;
	nfilter = jsk->nfilters;

	if (!nfilter)
		/* receive all when no filters are assigned */
		goto filter_match_found;

	for (; nfilter; ++f, --nfilter) {
		if ((skcb->addr.pgn & f->pgn_mask) != f->pgn)
			continue;
		if ((skcb->addr.sa & f->addr_mask) != f->addr)
			continue;
		if ((skcb->addr.src_name & f->name_mask) != f->name)
			continue;
		goto filter_match_found;
	}

	spin_unlock_bh(&jsk->filters_lock);
	return false;

filter_match_found:
	spin_unlock_bh(&jsk->filters_lock);
	return true;
}

static bool j1939_sk_recv_match_one(struct j1939_sock *jsk,
				    const struct j1939_sk_buff_cb *skcb)
{
	if (!(jsk->state & J1939_SOCK_BOUND))
		return false;

	if (!j1939_sk_match_dst(jsk, skcb))
		return false;

	if (!j1939_sk_match_filter(jsk, skcb))
		return false;

	return true;
}

static void j1939_sk_recv_one(struct j1939_sock *jsk, struct sk_buff *oskb)
{
	const struct j1939_sk_buff_cb *oskcb = j1939_skb_to_cb(oskb);
	struct j1939_sk_buff_cb *skcb;
	struct sk_buff *skb;

	if (oskb->sk == &jsk->sk)
		return;

	if (!j1939_sk_recv_match_one(jsk, oskcb))
		return;

	skb = skb_clone(oskb, GFP_ATOMIC);
	if (!skb) {
		pr_warn("skb clone failed\n");
		return;
	}
	can_skb_set_owner(skb, oskb->sk);

	skcb = j1939_skb_to_cb(skb);
	skcb->msg_flags &= ~(MSG_DONTROUTE);
	if (skb->sk)
		skcb->msg_flags |= MSG_DONTROUTE;

	if (sock_queue_rcv_skb(&jsk->sk, skb) < 0)
		kfree_skb(skb);
}

bool j1939_sk_recv_match(struct j1939_priv *priv, struct j1939_sk_buff_cb *skcb)
{
	struct j1939_sock *jsk;
	bool match = false;

	read_lock_bh(&priv->j1939_socks_lock);
	list_for_each_entry(jsk, &priv->j1939_socks, list) {
		match = j1939_sk_recv_match_one(jsk, skcb);
		if (match)
			break;
	}
	read_unlock_bh(&priv->j1939_socks_lock);

	return match;
}

void j1939_sk_recv(struct j1939_priv *priv, struct sk_buff *skb)
{
	struct j1939_sock *jsk;

	read_lock_bh(&priv->j1939_socks_lock);
	list_for_each_entry(jsk, &priv->j1939_socks, list) {
		j1939_sk_recv_one(jsk, skb);
	}
	read_unlock_bh(&priv->j1939_socks_lock);
}

static void j1939_sk_sock_destruct(struct sock *sk)
{
	struct j1939_sock *jsk = j1939_sk(sk);

	/* This function will be called by the generic networking code, when
	 * the socket is ultimately closed (sk->sk_destruct).
	 *
	 * The race between
	 * - processing a received CAN frame
	 *   (can_receive -> j1939_can_recv)
	 *   and accessing j1939_priv
	 * ... and ...
	 * - closing a socket
	 *   (j1939_can_rx_unregister -> can_rx_unregister)
	 *   and calling the final j1939_priv_put()
	 *
	 * is avoided by calling the final j1939_priv_put() from this
	 * RCU deferred cleanup call.
	 */
	if (jsk->priv) {
		j1939_priv_put(jsk->priv);
		jsk->priv = NULL;
	}

	/* call generic CAN sock destruct */
	can_sock_destruct(sk);
}

static int j1939_sk_init(struct sock *sk)
{
	struct j1939_sock *jsk = j1939_sk(sk);

	/* Ensure that "sk" is first member in "struct j1939_sock", so that we
	 * can skip it during memset().
	 */
	BUILD_BUG_ON(offsetof(struct j1939_sock, sk) != 0);
	memset((void *)jsk + sizeof(jsk->sk), 0x0,
	       sizeof(*jsk) - sizeof(jsk->sk));

	INIT_LIST_HEAD(&jsk->list);
	init_waitqueue_head(&jsk->waitq);
	jsk->sk.sk_priority = j1939_to_sk_priority(6);
	jsk->sk.sk_reuse = 1; /* per default */
	jsk->addr.sa = J1939_NO_ADDR;
	jsk->addr.da = J1939_NO_ADDR;
	jsk->addr.pgn = J1939_NO_PGN;
	jsk->pgn_rx_filter = J1939_NO_PGN;
	atomic_set(&jsk->skb_pending, 0);
	spin_lock_init(&jsk->sk_session_queue_lock);
	INIT_LIST_HEAD(&jsk->sk_session_queue);
	spin_lock_init(&jsk->filters_lock);

	/* j1939_sk_sock_destruct() depends on SOCK_RCU_FREE flag */
	sock_set_flag(sk, SOCK_RCU_FREE);
	sk->sk_destruct = j1939_sk_sock_destruct;
	sk->sk_protocol = CAN_J1939;

	return 0;
}

static int j1939_sk_sanity_check(struct sockaddr_can *addr, int len)
{
	if (!addr)
		return -EDESTADDRREQ;
	if (len < J1939_MIN_NAMELEN)
		return -EINVAL;
	if (addr->can_family != AF_CAN)
		return -EINVAL;
	if (!addr->can_ifindex)
		return -ENODEV;
	if (j1939_pgn_is_valid(addr->can_addr.j1939.pgn) &&
	    !j1939_pgn_is_clean_pdu(addr->can_addr.j1939.pgn))
		return -EINVAL;

	return 0;
}

static int j1939_sk_bind(struct socket *sock, struct sockaddr *uaddr, int len)
{
	struct sockaddr_can *addr = (struct sockaddr_can *)uaddr;
	struct j1939_sock *jsk = j1939_sk(sock->sk);
	struct j1939_priv *priv;
	struct sock *sk;
	struct net *net;
	int ret = 0;

	ret = j1939_sk_sanity_check(addr, len);
	if (ret)
		return ret;

	lock_sock(sock->sk);

	priv = jsk->priv;
	sk = sock->sk;
	net = sock_net(sk);

	/* Already bound to an interface? */
	if (jsk->state & J1939_SOCK_BOUND) {
		/* A re-bind() to a different interface is not
		 * supported.
		 */
		if (jsk->ifindex != addr->can_ifindex) {
			ret = -EINVAL;
			goto out_release_sock;
		}

		/* drop old references */
		j1939_jsk_del(priv, jsk);
		j1939_local_ecu_put(priv, jsk->addr.src_name, jsk->addr.sa);
	} else {
		struct can_ml_priv *can_ml;
		struct net_device *ndev;

		ndev = dev_get_by_index(net, addr->can_ifindex);
		if (!ndev) {
			ret = -ENODEV;
			goto out_release_sock;
		}

		can_ml = can_get_ml_priv(ndev);
		if (!can_ml) {
			dev_put(ndev);
			ret = -ENODEV;
			goto out_release_sock;
		}

		if (!(ndev->flags & IFF_UP)) {
			dev_put(ndev);
			ret = -ENETDOWN;
			goto out_release_sock;
		}

		priv = j1939_netdev_start(ndev);
		dev_put(ndev);
		if (IS_ERR(priv)) {
			ret = PTR_ERR(priv);
			goto out_release_sock;
		}

		jsk->ifindex = addr->can_ifindex;

		/* the corresponding j1939_priv_put() is called via
		 * sk->sk_destruct, which points to j1939_sk_sock_destruct()
		 */
		j1939_priv_get(priv);
		jsk->priv = priv;
	}

	/* set default transmit pgn */
	if (j1939_pgn_is_valid(addr->can_addr.j1939.pgn))
		jsk->pgn_rx_filter = addr->can_addr.j1939.pgn;
	jsk->addr.src_name = addr->can_addr.j1939.name;
	jsk->addr.sa = addr->can_addr.j1939.addr;

	/* get new references */
	ret = j1939_local_ecu_get(priv, jsk->addr.src_name, jsk->addr.sa);
	if (ret) {
		j1939_netdev_stop(priv);
		goto out_release_sock;
	}

	j1939_jsk_add(priv, jsk);

 out_release_sock: /* fall through */
	release_sock(sock->sk);

	return ret;
}

static int j1939_sk_connect(struct socket *sock, struct sockaddr *uaddr,
			    int len, int flags)
{
	struct sockaddr_can *addr = (struct sockaddr_can *)uaddr;
	struct j1939_sock *jsk = j1939_sk(sock->sk);
	int ret = 0;

	ret = j1939_sk_sanity_check(addr, len);
	if (ret)
		return ret;

	lock_sock(sock->sk);

	/* bind() before connect() is mandatory */
	if (!(jsk->state & J1939_SOCK_BOUND)) {
		ret = -EINVAL;
		goto out_release_sock;
	}

	/* A connect() to a different interface is not supported. */
	if (jsk->ifindex != addr->can_ifindex) {
		ret = -EINVAL;
		goto out_release_sock;
	}

	if (!addr->can_addr.j1939.name &&
	    addr->can_addr.j1939.addr == J1939_NO_ADDR &&
	    !sock_flag(&jsk->sk, SOCK_BROADCAST)) {
		/* broadcast, but SO_BROADCAST not set */
		ret = -EACCES;
		goto out_release_sock;
	}

	jsk->addr.dst_name = addr->can_addr.j1939.name;
	jsk->addr.da = addr->can_addr.j1939.addr;

	if (j1939_pgn_is_valid(addr->can_addr.j1939.pgn))
		jsk->addr.pgn = addr->can_addr.j1939.pgn;

	jsk->state |= J1939_SOCK_CONNECTED;

 out_release_sock: /* fall through */
	release_sock(sock->sk);

	return ret;
}

static void j1939_sk_sock2sockaddr_can(struct sockaddr_can *addr,
				       const struct j1939_sock *jsk, int peer)
{
	/* There are two holes (2 bytes and 3 bytes) to clear to avoid
	 * leaking kernel information to user space.
	 */
	memset(addr, 0, J1939_MIN_NAMELEN);

	addr->can_family = AF_CAN;
	addr->can_ifindex = jsk->ifindex;
	addr->can_addr.j1939.pgn = jsk->addr.pgn;
	if (peer) {
		addr->can_addr.j1939.name = jsk->addr.dst_name;
		addr->can_addr.j1939.addr = jsk->addr.da;
	} else {
		addr->can_addr.j1939.name = jsk->addr.src_name;
		addr->can_addr.j1939.addr = jsk->addr.sa;
	}
}

static int j1939_sk_getname(struct socket *sock, struct sockaddr *uaddr,
			    int peer)
{
	struct sockaddr_can *addr = (struct sockaddr_can *)uaddr;
	struct sock *sk = sock->sk;
	struct j1939_sock *jsk = j1939_sk(sk);
	int ret = 0;

	lock_sock(sk);

	if (peer && !(jsk->state & J1939_SOCK_CONNECTED)) {
		ret = -EADDRNOTAVAIL;
		goto failure;
	}

	j1939_sk_sock2sockaddr_can(addr, jsk, peer);
	ret = J1939_MIN_NAMELEN;

 failure:
	release_sock(sk);

	return ret;
}

static int j1939_sk_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct j1939_sock *jsk;

	if (!sk)
		return 0;

	lock_sock(sk);
	jsk = j1939_sk(sk);

	if (jsk->state & J1939_SOCK_BOUND) {
		struct j1939_priv *priv = jsk->priv;

		if (wait_event_interruptible(jsk->waitq,
					     !j1939_sock_pending_get(&jsk->sk))) {
			j1939_cancel_active_session(priv, sk);
			j1939_sk_queue_drop_all(priv, jsk, ESHUTDOWN);
		}

		j1939_jsk_del(priv, jsk);

		j1939_local_ecu_put(priv, jsk->addr.src_name,
				    jsk->addr.sa);

		j1939_netdev_stop(priv);
	}

	kfree(jsk->filters);
	sock_orphan(sk);
	sock->sk = NULL;

	release_sock(sk);
	sock_put(sk);

	return 0;
}

static int j1939_sk_setsockopt_flag(struct j1939_sock *jsk, sockptr_t optval,
				    unsigned int optlen, int flag)
{
	int tmp;

	if (optlen != sizeof(tmp))
		return -EINVAL;
	if (copy_from_sockptr(&tmp, optval, optlen))
		return -EFAULT;
	lock_sock(&jsk->sk);
	if (tmp)
		jsk->state |= flag;
	else
		jsk->state &= ~flag;
	release_sock(&jsk->sk);
	return tmp;
}

static int j1939_sk_setsockopt(struct socket *sock, int level, int optname,
			       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct j1939_sock *jsk = j1939_sk(sk);
	int tmp, count = 0, ret = 0;
	struct j1939_filter *filters = NULL, *ofilters;

	if (level != SOL_CAN_J1939)
		return -EINVAL;

	switch (optname) {
	case SO_J1939_FILTER:
		if (!sockptr_is_null(optval) && optlen != 0) {
			struct j1939_filter *f;
			int c;

			if (optlen % sizeof(*filters) != 0)
				return -EINVAL;

			if (optlen > J1939_FILTER_MAX *
			    sizeof(struct j1939_filter))
				return -EINVAL;

			count = optlen / sizeof(*filters);
			filters = memdup_sockptr(optval, optlen);
			if (IS_ERR(filters))
				return PTR_ERR(filters);

			for (f = filters, c = count; c; f++, c--) {
				f->name &= f->name_mask;
				f->pgn &= f->pgn_mask;
				f->addr &= f->addr_mask;
			}
		}

		lock_sock(&jsk->sk);
		spin_lock_bh(&jsk->filters_lock);
		ofilters = jsk->filters;
		jsk->filters = filters;
		jsk->nfilters = count;
		spin_unlock_bh(&jsk->filters_lock);
		release_sock(&jsk->sk);
		kfree(ofilters);
		return 0;
	case SO_J1939_PROMISC:
		return j1939_sk_setsockopt_flag(jsk, optval, optlen,
						J1939_SOCK_PROMISC);
	case SO_J1939_ERRQUEUE:
		ret = j1939_sk_setsockopt_flag(jsk, optval, optlen,
					       J1939_SOCK_ERRQUEUE);
		if (ret < 0)
			return ret;

		if (!(jsk->state & J1939_SOCK_ERRQUEUE))
			skb_queue_purge(&sk->sk_error_queue);
		return ret;
	case SO_J1939_SEND_PRIO:
		if (optlen != sizeof(tmp))
			return -EINVAL;
		if (copy_from_sockptr(&tmp, optval, optlen))
			return -EFAULT;
		if (tmp < 0 || tmp > 7)
			return -EDOM;
		if (tmp < 2 && !capable(CAP_NET_ADMIN))
			return -EPERM;
		lock_sock(&jsk->sk);
		jsk->sk.sk_priority = j1939_to_sk_priority(tmp);
		release_sock(&jsk->sk);
		return 0;
	default:
		return -ENOPROTOOPT;
	}
}

static int j1939_sk_getsockopt(struct socket *sock, int level, int optname,
			       char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct j1939_sock *jsk = j1939_sk(sk);
	int ret, ulen;
	/* set defaults for using 'int' properties */
	int tmp = 0;
	int len = sizeof(tmp);
	void *val = &tmp;

	if (level != SOL_CAN_J1939)
		return -EINVAL;
	if (get_user(ulen, optlen))
		return -EFAULT;
	if (ulen < 0)
		return -EINVAL;

	lock_sock(&jsk->sk);
	switch (optname) {
	case SO_J1939_PROMISC:
		tmp = (jsk->state & J1939_SOCK_PROMISC) ? 1 : 0;
		break;
	case SO_J1939_ERRQUEUE:
		tmp = (jsk->state & J1939_SOCK_ERRQUEUE) ? 1 : 0;
		break;
	case SO_J1939_SEND_PRIO:
		tmp = j1939_prio(jsk->sk.sk_priority);
		break;
	default:
		ret = -ENOPROTOOPT;
		goto no_copy;
	}

	/* copy to user, based on 'len' & 'val'
	 * but most sockopt's are 'int' properties, and have 'len' & 'val'
	 * left unchanged, but instead modified 'tmp'
	 */
	if (len > ulen)
		ret = -EFAULT;
	else if (put_user(len, optlen))
		ret = -EFAULT;
	else if (copy_to_user(optval, val, len))
		ret = -EFAULT;
	else
		ret = 0;
 no_copy:
	release_sock(&jsk->sk);
	return ret;
}

static int j1939_sk_recvmsg(struct socket *sock, struct msghdr *msg,
			    size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	struct j1939_sk_buff_cb *skcb;
	int ret = 0;

	if (flags & ~(MSG_DONTWAIT | MSG_ERRQUEUE | MSG_CMSG_COMPAT))
		return -EINVAL;

	if (flags & MSG_ERRQUEUE)
		return sock_recv_errqueue(sock->sk, msg, size, SOL_CAN_J1939,
					  SCM_J1939_ERRQUEUE);

	skb = skb_recv_datagram(sk, flags, &ret);
	if (!skb)
		return ret;

	if (size < skb->len)
		msg->msg_flags |= MSG_TRUNC;
	else
		size = skb->len;

	ret = memcpy_to_msg(msg, skb->data, size);
	if (ret < 0) {
		skb_free_datagram(sk, skb);
		return ret;
	}

	skcb = j1939_skb_to_cb(skb);
	if (j1939_address_is_valid(skcb->addr.da))
		put_cmsg(msg, SOL_CAN_J1939, SCM_J1939_DEST_ADDR,
			 sizeof(skcb->addr.da), &skcb->addr.da);

	if (skcb->addr.dst_name)
		put_cmsg(msg, SOL_CAN_J1939, SCM_J1939_DEST_NAME,
			 sizeof(skcb->addr.dst_name), &skcb->addr.dst_name);

	put_cmsg(msg, SOL_CAN_J1939, SCM_J1939_PRIO,
		 sizeof(skcb->priority), &skcb->priority);

	if (msg->msg_name) {
		struct sockaddr_can *paddr = msg->msg_name;

		msg->msg_namelen = J1939_MIN_NAMELEN;
		memset(msg->msg_name, 0, msg->msg_namelen);
		paddr->can_family = AF_CAN;
		paddr->can_ifindex = skb->skb_iif;
		paddr->can_addr.j1939.name = skcb->addr.src_name;
		paddr->can_addr.j1939.addr = skcb->addr.sa;
		paddr->can_addr.j1939.pgn = skcb->addr.pgn;
	}

	sock_recv_cmsgs(msg, sk, skb);
	msg->msg_flags |= skcb->msg_flags;
	skb_free_datagram(sk, skb);

	return size;
}

static struct sk_buff *j1939_sk_alloc_skb(struct net_device *ndev,
					  struct sock *sk,
					  struct msghdr *msg, size_t size,
					  int *errcode)
{
	struct j1939_sock *jsk = j1939_sk(sk);
	struct j1939_sk_buff_cb *skcb;
	struct sk_buff *skb;
	int ret;

	skb = sock_alloc_send_skb(sk,
				  size +
				  sizeof(struct can_frame) -
				  sizeof(((struct can_frame *)NULL)->data) +
				  sizeof(struct can_skb_priv),
				  msg->msg_flags & MSG_DONTWAIT, &ret);
	if (!skb)
		goto failure;

	can_skb_reserve(skb);
	can_skb_prv(skb)->ifindex = ndev->ifindex;
	can_skb_prv(skb)->skbcnt = 0;
	skb_reserve(skb, offsetof(struct can_frame, data));

	ret = memcpy_from_msg(skb_put(skb, size), msg, size);
	if (ret < 0)
		goto free_skb;

	skb->dev = ndev;

	skcb = j1939_skb_to_cb(skb);
	memset(skcb, 0, sizeof(*skcb));
	skcb->addr = jsk->addr;
	skcb->priority = j1939_prio(sk->sk_priority);

	if (msg->msg_name) {
		struct sockaddr_can *addr = msg->msg_name;

		if (addr->can_addr.j1939.name ||
		    addr->can_addr.j1939.addr != J1939_NO_ADDR) {
			skcb->addr.dst_name = addr->can_addr.j1939.name;
			skcb->addr.da = addr->can_addr.j1939.addr;
		}
		if (j1939_pgn_is_valid(addr->can_addr.j1939.pgn))
			skcb->addr.pgn = addr->can_addr.j1939.pgn;
	}

	*errcode = ret;
	return skb;

free_skb:
	kfree_skb(skb);
failure:
	*errcode = ret;
	return NULL;
}

static size_t j1939_sk_opt_stats_get_size(enum j1939_sk_errqueue_type type)
{
	switch (type) {
	case J1939_ERRQUEUE_RX_RTS:
		return
			nla_total_size(sizeof(u32)) + /* J1939_NLA_TOTAL_SIZE */
			nla_total_size(sizeof(u32)) + /* J1939_NLA_PGN */
			nla_total_size(sizeof(u64)) + /* J1939_NLA_SRC_NAME */
			nla_total_size(sizeof(u64)) + /* J1939_NLA_DEST_NAME */
			nla_total_size(sizeof(u8)) +  /* J1939_NLA_SRC_ADDR */
			nla_total_size(sizeof(u8)) +  /* J1939_NLA_DEST_ADDR */
			0;
	default:
		return
			nla_total_size(sizeof(u32)) + /* J1939_NLA_BYTES_ACKED */
			0;
	}
}

static struct sk_buff *
j1939_sk_get_timestamping_opt_stats(struct j1939_session *session,
				    enum j1939_sk_errqueue_type type)
{
	struct sk_buff *stats;
	u32 size;

	stats = alloc_skb(j1939_sk_opt_stats_get_size(type), GFP_ATOMIC);
	if (!stats)
		return NULL;

	if (session->skcb.addr.type == J1939_SIMPLE)
		size = session->total_message_size;
	else
		size = min(session->pkt.tx_acked * 7,
			   session->total_message_size);

	switch (type) {
	case J1939_ERRQUEUE_RX_RTS:
		nla_put_u32(stats, J1939_NLA_TOTAL_SIZE,
			    session->total_message_size);
		nla_put_u32(stats, J1939_NLA_PGN,
			    session->skcb.addr.pgn);
		nla_put_u64_64bit(stats, J1939_NLA_SRC_NAME,
				  session->skcb.addr.src_name, J1939_NLA_PAD);
		nla_put_u64_64bit(stats, J1939_NLA_DEST_NAME,
				  session->skcb.addr.dst_name, J1939_NLA_PAD);
		nla_put_u8(stats, J1939_NLA_SRC_ADDR,
			   session->skcb.addr.sa);
		nla_put_u8(stats, J1939_NLA_DEST_ADDR,
			   session->skcb.addr.da);
		break;
	default:
		nla_put_u32(stats, J1939_NLA_BYTES_ACKED, size);
	}

	return stats;
}

static void __j1939_sk_errqueue(struct j1939_session *session, struct sock *sk,
				enum j1939_sk_errqueue_type type)
{
	struct j1939_priv *priv = session->priv;
	struct j1939_sock *jsk;
	struct sock_exterr_skb *serr;
	struct sk_buff *skb;
	char *state = "UNK";
	u32 tsflags;
	int err;

	jsk = j1939_sk(sk);

	if (!(jsk->state & J1939_SOCK_ERRQUEUE))
		return;

	tsflags = READ_ONCE(sk->sk_tsflags);
	switch (type) {
	case J1939_ERRQUEUE_TX_ACK:
		if (!(tsflags & SOF_TIMESTAMPING_TX_ACK))
			return;
		break;
	case J1939_ERRQUEUE_TX_SCHED:
		if (!(tsflags & SOF_TIMESTAMPING_TX_SCHED))
			return;
		break;
	case J1939_ERRQUEUE_TX_ABORT:
		break;
	case J1939_ERRQUEUE_RX_RTS:
		fallthrough;
	case J1939_ERRQUEUE_RX_DPO:
		fallthrough;
	case J1939_ERRQUEUE_RX_ABORT:
		if (!(tsflags & SOF_TIMESTAMPING_RX_SOFTWARE))
			return;
		break;
	default:
		netdev_err(priv->ndev, "Unknown errqueue type %i\n", type);
	}

	skb = j1939_sk_get_timestamping_opt_stats(session, type);
	if (!skb)
		return;

	skb->tstamp = ktime_get_real();

	BUILD_BUG_ON(sizeof(struct sock_exterr_skb) > sizeof(skb->cb));

	serr = SKB_EXT_ERR(skb);
	memset(serr, 0, sizeof(*serr));
	switch (type) {
	case J1939_ERRQUEUE_TX_ACK:
		serr->ee.ee_errno = ENOMSG;
		serr->ee.ee_origin = SO_EE_ORIGIN_TIMESTAMPING;
		serr->ee.ee_info = SCM_TSTAMP_ACK;
		state = "TX ACK";
		break;
	case J1939_ERRQUEUE_TX_SCHED:
		serr->ee.ee_errno = ENOMSG;
		serr->ee.ee_origin = SO_EE_ORIGIN_TIMESTAMPING;
		serr->ee.ee_info = SCM_TSTAMP_SCHED;
		state = "TX SCH";
		break;
	case J1939_ERRQUEUE_TX_ABORT:
		serr->ee.ee_errno = session->err;
		serr->ee.ee_origin = SO_EE_ORIGIN_LOCAL;
		serr->ee.ee_info = J1939_EE_INFO_TX_ABORT;
		state = "TX ABT";
		break;
	case J1939_ERRQUEUE_RX_RTS:
		serr->ee.ee_errno = ENOMSG;
		serr->ee.ee_origin = SO_EE_ORIGIN_LOCAL;
		serr->ee.ee_info = J1939_EE_INFO_RX_RTS;
		state = "RX RTS";
		break;
	case J1939_ERRQUEUE_RX_DPO:
		serr->ee.ee_errno = ENOMSG;
		serr->ee.ee_origin = SO_EE_ORIGIN_LOCAL;
		serr->ee.ee_info = J1939_EE_INFO_RX_DPO;
		state = "RX DPO";
		break;
	case J1939_ERRQUEUE_RX_ABORT:
		serr->ee.ee_errno = session->err;
		serr->ee.ee_origin = SO_EE_ORIGIN_LOCAL;
		serr->ee.ee_info = J1939_EE_INFO_RX_ABORT;
		state = "RX ABT";
		break;
	}

	serr->opt_stats = true;
	if (tsflags & SOF_TIMESTAMPING_OPT_ID)
		serr->ee.ee_data = session->tskey;

	netdev_dbg(session->priv->ndev, "%s: 0x%p tskey: %i, state: %s\n",
		   __func__, session, session->tskey, state);
	err = sock_queue_err_skb(sk, skb);

	if (err)
		kfree_skb(skb);
};

void j1939_sk_errqueue(struct j1939_session *session,
		       enum j1939_sk_errqueue_type type)
{
	struct j1939_priv *priv = session->priv;
	struct j1939_sock *jsk;

	if (session->sk) {
		/* send TX notifications to the socket of origin  */
		__j1939_sk_errqueue(session, session->sk, type);
		return;
	}

	/* spread RX notifications to all sockets subscribed to this session */
	read_lock_bh(&priv->j1939_socks_lock);
	list_for_each_entry(jsk, &priv->j1939_socks, list) {
		if (j1939_sk_recv_match_one(jsk, &session->skcb))
			__j1939_sk_errqueue(session, &jsk->sk, type);
	}
	read_unlock_bh(&priv->j1939_socks_lock);
};

void j1939_sk_send_loop_abort(struct sock *sk, int err)
{
	struct j1939_sock *jsk = j1939_sk(sk);

	if (jsk->state & J1939_SOCK_ERRQUEUE)
		return;

	sk->sk_err = err;

	sk_error_report(sk);
}

static int j1939_sk_send_loop(struct j1939_priv *priv,  struct sock *sk,
			      struct msghdr *msg, size_t size)

{
	struct j1939_sock *jsk = j1939_sk(sk);
	struct j1939_session *session = j1939_sk_get_incomplete_session(jsk);
	struct sk_buff *skb;
	size_t segment_size, todo_size;
	int ret = 0;

	if (session &&
	    session->total_message_size != session->total_queued_size + size) {
		j1939_session_put(session);
		return -EIO;
	}

	todo_size = size;

	while (todo_size) {
		struct j1939_sk_buff_cb *skcb;

		segment_size = min_t(size_t, J1939_MAX_TP_PACKET_SIZE,
				     todo_size);

		/* Allocate skb for one segment */
		skb = j1939_sk_alloc_skb(priv->ndev, sk, msg, segment_size,
					 &ret);
		if (ret)
			break;

		skcb = j1939_skb_to_cb(skb);

		if (!session) {
			/* at this point the size should be full size
			 * of the session
			 */
			skcb->offset = 0;
			session = j1939_tp_send(priv, skb, size);
			if (IS_ERR(session)) {
				ret = PTR_ERR(session);
				goto kfree_skb;
			}
			if (j1939_sk_queue_session(session)) {
				/* try to activate session if we a
				 * fist in the queue
				 */
				if (!j1939_session_activate(session)) {
					j1939_tp_schedule_txtimer(session, 0);
				} else {
					ret = -EBUSY;
					session->err = ret;
					j1939_sk_queue_drop_all(priv, jsk,
								EBUSY);
					break;
				}
			}
		} else {
			skcb->offset = session->total_queued_size;
			j1939_session_skb_queue(session, skb);
		}

		todo_size -= segment_size;
		session->total_queued_size += segment_size;
	}

	switch (ret) {
	case 0: /* OK */
		if (todo_size)
			netdev_warn(priv->ndev,
				    "no error found and not completely queued?! %zu\n",
				    todo_size);
		ret = size;
		break;
	case -ERESTARTSYS:
		ret = -EINTR;
		fallthrough;
	case -EAGAIN: /* OK */
		if (todo_size != size)
			ret = size - todo_size;
		break;
	default: /* ERROR */
		break;
	}

	if (session)
		j1939_session_put(session);

	return ret;

 kfree_skb:
	kfree_skb(skb);
	return ret;
}

static int j1939_sk_sendmsg(struct socket *sock, struct msghdr *msg,
			    size_t size)
{
	struct sock *sk = sock->sk;
	struct j1939_sock *jsk = j1939_sk(sk);
	struct j1939_priv *priv;
	int ifindex;
	int ret;

	lock_sock(sock->sk);
	/* various socket state tests */
	if (!(jsk->state & J1939_SOCK_BOUND)) {
		ret = -EBADFD;
		goto sendmsg_done;
	}

	priv = jsk->priv;
	ifindex = jsk->ifindex;

	if (!jsk->addr.src_name && jsk->addr.sa == J1939_NO_ADDR) {
		/* no source address assigned yet */
		ret = -EBADFD;
		goto sendmsg_done;
	}

	/* deal with provided destination address info */
	if (msg->msg_name) {
		struct sockaddr_can *addr = msg->msg_name;

		if (msg->msg_namelen < J1939_MIN_NAMELEN) {
			ret = -EINVAL;
			goto sendmsg_done;
		}

		if (addr->can_family != AF_CAN) {
			ret = -EINVAL;
			goto sendmsg_done;
		}

		if (addr->can_ifindex && addr->can_ifindex != ifindex) {
			ret = -EBADFD;
			goto sendmsg_done;
		}

		if (j1939_pgn_is_valid(addr->can_addr.j1939.pgn) &&
		    !j1939_pgn_is_clean_pdu(addr->can_addr.j1939.pgn)) {
			ret = -EINVAL;
			goto sendmsg_done;
		}

		if (!addr->can_addr.j1939.name &&
		    addr->can_addr.j1939.addr == J1939_NO_ADDR &&
		    !sock_flag(sk, SOCK_BROADCAST)) {
			/* broadcast, but SO_BROADCAST not set */
			ret = -EACCES;
			goto sendmsg_done;
		}
	} else {
		if (!jsk->addr.dst_name && jsk->addr.da == J1939_NO_ADDR &&
		    !sock_flag(sk, SOCK_BROADCAST)) {
			/* broadcast, but SO_BROADCAST not set */
			ret = -EACCES;
			goto sendmsg_done;
		}
	}

	ret = j1939_sk_send_loop(priv, sk, msg, size);

sendmsg_done:
	release_sock(sock->sk);

	return ret;
}

void j1939_sk_netdev_event_netdown(struct j1939_priv *priv)
{
	struct j1939_sock *jsk;
	int error_code = ENETDOWN;

	read_lock_bh(&priv->j1939_socks_lock);
	list_for_each_entry(jsk, &priv->j1939_socks, list) {
		jsk->sk.sk_err = error_code;
		if (!sock_flag(&jsk->sk, SOCK_DEAD))
			sk_error_report(&jsk->sk);

		j1939_sk_queue_drop_all(priv, jsk, error_code);
	}
	read_unlock_bh(&priv->j1939_socks_lock);
}

static int j1939_sk_no_ioctlcmd(struct socket *sock, unsigned int cmd,
				unsigned long arg)
{
	/* no ioctls for socket layer -> hand it down to NIC layer */
	return -ENOIOCTLCMD;
}

static const struct proto_ops j1939_ops = {
	.family = PF_CAN,
	.release = j1939_sk_release,
	.bind = j1939_sk_bind,
	.connect = j1939_sk_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = j1939_sk_getname,
	.poll = datagram_poll,
	.ioctl = j1939_sk_no_ioctlcmd,
	.listen = sock_no_listen,
	.shutdown = sock_no_shutdown,
	.setsockopt = j1939_sk_setsockopt,
	.getsockopt = j1939_sk_getsockopt,
	.sendmsg = j1939_sk_sendmsg,
	.recvmsg = j1939_sk_recvmsg,
	.mmap = sock_no_mmap,
	.sendpage = sock_no_sendpage,
};

static struct proto j1939_proto __read_mostly = {
	.name = "CAN_J1939",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct j1939_sock),
	.init = j1939_sk_init,
};

const struct can_proto j1939_can_proto = {
	.type = SOCK_DGRAM,
	.protocol = CAN_J1939,
	.ops = &j1939_ops,
	.prot = &j1939_proto,
};
