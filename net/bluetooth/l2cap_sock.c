/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright (C) 2009-2010 Gustavo F. Padovan <gustavo@padovan.org>
   Copyright (C) 2010 Google Inc.

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

/* Bluetooth L2CAP sockets. */

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/l2cap.h>

static void l2cap_sock_timeout(unsigned long arg)
{
	struct sock *sk = (struct sock *) arg;
	int reason;

	BT_DBG("sock %p state %d", sk, sk->sk_state);

	bh_lock_sock(sk);

	if (sock_owned_by_user(sk)) {
		/* sk is owned by user. Try again later */
		l2cap_sock_set_timer(sk, HZ / 5);
		bh_unlock_sock(sk);
		sock_put(sk);
		return;
	}

	if (sk->sk_state == BT_CONNECTED || sk->sk_state == BT_CONFIG)
		reason = ECONNREFUSED;
	else if (sk->sk_state == BT_CONNECT &&
				l2cap_pi(sk)->sec_level != BT_SECURITY_SDP)
		reason = ECONNREFUSED;
	else
		reason = ETIMEDOUT;

	__l2cap_sock_close(sk, reason);

	bh_unlock_sock(sk);

	l2cap_sock_kill(sk);
	sock_put(sk);
}

static struct sock *__l2cap_get_sock_by_addr(__le16 psm, bdaddr_t *src)
{
	struct sock *sk;
	struct hlist_node *node;
	sk_for_each(sk, node, &l2cap_sk_list.head)
		if (l2cap_pi(sk)->sport == psm && !bacmp(&bt_sk(sk)->src, src))
			goto found;
	sk = NULL;
found:
	return sk;
}

static int l2cap_sock_bind(struct socket *sock, struct sockaddr *addr, int alen)
{
	struct sock *sk = sock->sk;
	struct sockaddr_l2 la;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&la, 0, sizeof(la));
	len = min_t(unsigned int, sizeof(la), alen);
	memcpy(&la, addr, len);

	if (la.l2_cid)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	if (la.l2_psm) {
		__u16 psm = __le16_to_cpu(la.l2_psm);

		/* PSM must be odd and lsb of upper byte must be 0 */
		if ((psm & 0x0101) != 0x0001) {
			err = -EINVAL;
			goto done;
		}

		/* Restrict usage of well-known PSMs */
		if (psm < 0x1001 && !capable(CAP_NET_BIND_SERVICE)) {
			err = -EACCES;
			goto done;
		}
	}

	write_lock_bh(&l2cap_sk_list.lock);

	if (la.l2_psm && __l2cap_get_sock_by_addr(la.l2_psm, &la.l2_bdaddr)) {
		err = -EADDRINUSE;
	} else {
		/* Save source address */
		bacpy(&bt_sk(sk)->src, &la.l2_bdaddr);
		l2cap_pi(sk)->psm   = la.l2_psm;
		l2cap_pi(sk)->sport = la.l2_psm;
		sk->sk_state = BT_BOUND;

		if (__le16_to_cpu(la.l2_psm) == 0x0001 ||
					__le16_to_cpu(la.l2_psm) == 0x0003)
			l2cap_pi(sk)->sec_level = BT_SECURITY_SDP;
	}

	write_unlock_bh(&l2cap_sk_list.lock);

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p backlog %d", sk, backlog);

	lock_sock(sk);

	if ((sock->type != SOCK_SEQPACKET && sock->type != SOCK_STREAM)
			|| sk->sk_state != BT_BOUND) {
		err = -EBADFD;
		goto done;
	}

	switch (l2cap_pi(sk)->mode) {
	case L2CAP_MODE_BASIC:
		break;
	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		if (!disable_ertm)
			break;
		/* fall through */
	default:
		err = -ENOTSUPP;
		goto done;
	}

	if (!l2cap_pi(sk)->psm) {
		bdaddr_t *src = &bt_sk(sk)->src;
		u16 psm;

		err = -EINVAL;

		write_lock_bh(&l2cap_sk_list.lock);

		for (psm = 0x1001; psm < 0x1100; psm += 2)
			if (!__l2cap_get_sock_by_addr(cpu_to_le16(psm), src)) {
				l2cap_pi(sk)->psm   = cpu_to_le16(psm);
				l2cap_pi(sk)->sport = cpu_to_le16(psm);
				err = 0;
				break;
			}

		write_unlock_bh(&l2cap_sk_list.lock);

		if (err < 0)
			goto done;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;
	sk->sk_state = BT_LISTEN;

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_accept(struct socket *sock, struct socket *newsock, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sock *sk = sock->sk, *nsk;
	long timeo;
	int err = 0;

	lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

	if (sk->sk_state != BT_LISTEN) {
		err = -EBADFD;
		goto done;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	BT_DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	while (!(nsk = bt_accept_dequeue(sk, newsock))) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

		if (sk->sk_state != BT_LISTEN) {
			err = -EBADFD;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);

	if (err)
		goto done;

	newsock->state = SS_CONNECTED;

	BT_DBG("new socket %p", nsk);

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_getname(struct socket *sock, struct sockaddr *addr, int *len, int peer)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;

	BT_DBG("sock %p, sk %p", sock, sk);

	addr->sa_family = AF_BLUETOOTH;
	*len = sizeof(struct sockaddr_l2);

	if (peer) {
		la->l2_psm = l2cap_pi(sk)->psm;
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->dst);
		la->l2_cid = cpu_to_le16(l2cap_pi(sk)->dcid);
	} else {
		la->l2_psm = l2cap_pi(sk)->sport;
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->src);
		la->l2_cid = cpu_to_le16(l2cap_pi(sk)->scid);
	}

	return 0;
}

static int l2cap_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int err;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	err = l2cap_sock_shutdown(sock, 2);

	sock_orphan(sk);
	l2cap_sock_kill(sk);
	return err;
}

static void l2cap_sock_destruct(struct sock *sk)
{
	BT_DBG("sk %p", sk);

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
}

void l2cap_sock_init(struct sock *sk, struct sock *parent)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);

	BT_DBG("sk %p", sk);

	if (parent) {
		sk->sk_type = parent->sk_type;
		bt_sk(sk)->defer_setup = bt_sk(parent)->defer_setup;

		pi->imtu = l2cap_pi(parent)->imtu;
		pi->omtu = l2cap_pi(parent)->omtu;
		pi->conf_state = l2cap_pi(parent)->conf_state;
		pi->mode = l2cap_pi(parent)->mode;
		pi->fcs  = l2cap_pi(parent)->fcs;
		pi->max_tx = l2cap_pi(parent)->max_tx;
		pi->tx_win = l2cap_pi(parent)->tx_win;
		pi->sec_level = l2cap_pi(parent)->sec_level;
		pi->role_switch = l2cap_pi(parent)->role_switch;
		pi->force_reliable = l2cap_pi(parent)->force_reliable;
		pi->flushable = l2cap_pi(parent)->flushable;
	} else {
		pi->imtu = L2CAP_DEFAULT_MTU;
		pi->omtu = 0;
		if (!disable_ertm && sk->sk_type == SOCK_STREAM) {
			pi->mode = L2CAP_MODE_ERTM;
			pi->conf_state |= L2CAP_CONF_STATE2_DEVICE;
		} else {
			pi->mode = L2CAP_MODE_BASIC;
		}
		pi->max_tx = L2CAP_DEFAULT_MAX_TX;
		pi->fcs  = L2CAP_FCS_CRC16;
		pi->tx_win = L2CAP_DEFAULT_TX_WINDOW;
		pi->sec_level = BT_SECURITY_LOW;
		pi->role_switch = 0;
		pi->force_reliable = 0;
		pi->flushable = BT_FLUSHABLE_OFF;
	}

	/* Default config options */
	pi->conf_len = 0;
	pi->flush_to = L2CAP_DEFAULT_FLUSH_TO;
	skb_queue_head_init(TX_QUEUE(sk));
	skb_queue_head_init(SREJ_QUEUE(sk));
	skb_queue_head_init(BUSY_QUEUE(sk));
	INIT_LIST_HEAD(SREJ_LIST(sk));
}

static struct proto l2cap_proto = {
	.name		= "L2CAP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct l2cap_pinfo)
};

struct sock *l2cap_sock_alloc(struct net *net, struct socket *sock, int proto, gfp_t prio)
{
	struct sock *sk;

	sk = sk_alloc(net, PF_BLUETOOTH, prio, &l2cap_proto);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);

	sk->sk_destruct = l2cap_sock_destruct;
	sk->sk_sndtimeo = msecs_to_jiffies(L2CAP_CONN_TIMEOUT);

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state = BT_OPEN;

	setup_timer(&sk->sk_timer, l2cap_sock_timeout, (unsigned long) sk);

	bt_sock_link(&l2cap_sk_list, sk);
	return sk;
}

static int l2cap_sock_create(struct net *net, struct socket *sock, int protocol,
			     int kern)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_SEQPACKET && sock->type != SOCK_STREAM &&
			sock->type != SOCK_DGRAM && sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	if (sock->type == SOCK_RAW && !kern && !capable(CAP_NET_RAW))
		return -EPERM;

	sock->ops = &l2cap_sock_ops;

	sk = l2cap_sock_alloc(net, sock, protocol, GFP_ATOMIC);
	if (!sk)
		return -ENOMEM;

	l2cap_sock_init(sk, NULL);
	return 0;
}

const struct proto_ops l2cap_sock_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.release	= l2cap_sock_release,
	.bind		= l2cap_sock_bind,
	.connect	= l2cap_sock_connect,
	.listen		= l2cap_sock_listen,
	.accept		= l2cap_sock_accept,
	.getname	= l2cap_sock_getname,
	.sendmsg	= l2cap_sock_sendmsg,
	.recvmsg	= l2cap_sock_recvmsg,
	.poll		= bt_sock_poll,
	.ioctl		= bt_sock_ioctl,
	.mmap		= sock_no_mmap,
	.socketpair	= sock_no_socketpair,
	.shutdown	= l2cap_sock_shutdown,
	.setsockopt	= l2cap_sock_setsockopt,
	.getsockopt	= l2cap_sock_getsockopt
};

static const struct net_proto_family l2cap_sock_family_ops = {
	.family	= PF_BLUETOOTH,
	.owner	= THIS_MODULE,
	.create	= l2cap_sock_create,
};

int __init l2cap_init_sockets(void)
{
       int err;

       err = proto_register(&l2cap_proto, 0);
       if (err < 0)
               return err;

       err = bt_sock_register(BTPROTO_L2CAP, &l2cap_sock_family_ops);
       if (err < 0)
               goto error;

       BT_INFO("L2CAP socket layer initialized");

       return 0;

error:
       BT_ERR("L2CAP socket registration failed");
       proto_unregister(&l2cap_proto);
       return err;
}

void l2cap_cleanup_sockets(void)
{
       if (bt_sock_unregister(BTPROTO_L2CAP) < 0)
               BT_ERR("L2CAP socket unregistration failed");

       proto_unregister(&l2cap_proto);
}
