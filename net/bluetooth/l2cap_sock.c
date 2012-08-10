/*
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Copyright (C) 2009-2010 Gustavo F. Padovan <gustavo@padovan.org>
   Copyright (C) 2010 Google Inc.
   Copyright (C) 2011 ProFUSION Embedded Systems

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

#include <linux/export.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>
#include <net/bluetooth/smp.h>

static const struct proto_ops l2cap_sock_ops;
static void l2cap_sock_init(struct sock *sk, struct sock *parent);
static struct sock *l2cap_sock_alloc(struct net *net, struct socket *sock, int proto, gfp_t prio);

static int l2cap_sock_bind(struct socket *sock, struct sockaddr *addr, int alen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct sockaddr_l2 la;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&la, 0, sizeof(la));
	len = min_t(unsigned int, sizeof(la), alen);
	memcpy(&la, addr, len);

	if (la.l2_cid && la.l2_psm)
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

	if (la.l2_cid)
		err = l2cap_add_scid(chan, __le16_to_cpu(la.l2_cid));
	else
		err = l2cap_add_psm(chan, &la.l2_bdaddr, la.l2_psm);

	if (err < 0)
		goto done;

	if (__le16_to_cpu(la.l2_psm) == L2CAP_PSM_SDP ||
	    __le16_to_cpu(la.l2_psm) == L2CAP_PSM_RFCOMM)
		chan->sec_level = BT_SECURITY_SDP;

	bacpy(&bt_sk(sk)->src, &la.l2_bdaddr);

	chan->state = BT_BOUND;
	sk->sk_state = BT_BOUND;

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_connect(struct socket *sock, struct sockaddr *addr, int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct sockaddr_l2 la;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (!addr || alen < sizeof(addr->sa_family) ||
	    addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&la, 0, sizeof(la));
	len = min_t(unsigned int, sizeof(la), alen);
	memcpy(&la, addr, len);

	if (la.l2_cid && la.l2_psm)
		return -EINVAL;

	err = l2cap_chan_connect(chan, la.l2_psm, __le16_to_cpu(la.l2_cid),
				 &la.l2_bdaddr, la.l2_bdaddr_type);
	if (err)
		return err;

	lock_sock(sk);

	err = bt_sock_wait_state(sk, BT_CONNECTED,
			sock_sndtimeo(sk, flags & O_NONBLOCK));

	release_sock(sk);

	return err;
}

static int l2cap_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	int err = 0;

	BT_DBG("sk %p backlog %d", sk, backlog);

	lock_sock(sk);

	if (sk->sk_state != BT_BOUND) {
		err = -EBADFD;
		goto done;
	}

	if (sk->sk_type != SOCK_SEQPACKET && sk->sk_type != SOCK_STREAM) {
		err = -EINVAL;
		goto done;
	}

	switch (chan->mode) {
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

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;

	chan->state = BT_LISTEN;
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

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	BT_DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		if (sk->sk_state != BT_LISTEN) {
			err = -EBADFD;
			break;
		}

		nsk = bt_accept_dequeue(sk, newsock);
		if (nsk)
			break;

		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);
	}
	__set_current_state(TASK_RUNNING);
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
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;

	BT_DBG("sock %p, sk %p", sock, sk);

	addr->sa_family = AF_BLUETOOTH;
	*len = sizeof(struct sockaddr_l2);

	if (peer) {
		la->l2_psm = chan->psm;
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->dst);
		la->l2_cid = cpu_to_le16(chan->dcid);
	} else {
		la->l2_psm = chan->sport;
		bacpy(&la->l2_bdaddr, &bt_sk(sk)->src);
		la->l2_cid = cpu_to_le16(chan->scid);
	}

	return 0;
}

static int l2cap_sock_getsockopt_old(struct socket *sock, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct l2cap_options opts;
	struct l2cap_conninfo cinfo;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		memset(&opts, 0, sizeof(opts));
		opts.imtu     = chan->imtu;
		opts.omtu     = chan->omtu;
		opts.flush_to = chan->flush_to;
		opts.mode     = chan->mode;
		opts.fcs      = chan->fcs;
		opts.max_tx   = chan->max_tx;
		opts.txwin_size = chan->tx_win;

		len = min_t(unsigned int, len, sizeof(opts));
		if (copy_to_user(optval, (char *) &opts, len))
			err = -EFAULT;

		break;

	case L2CAP_LM:
		switch (chan->sec_level) {
		case BT_SECURITY_LOW:
			opt = L2CAP_LM_AUTH;
			break;
		case BT_SECURITY_MEDIUM:
			opt = L2CAP_LM_AUTH | L2CAP_LM_ENCRYPT;
			break;
		case BT_SECURITY_HIGH:
			opt = L2CAP_LM_AUTH | L2CAP_LM_ENCRYPT |
							L2CAP_LM_SECURE;
			break;
		default:
			opt = 0;
			break;
		}

		if (test_bit(FLAG_ROLE_SWITCH, &chan->flags))
			opt |= L2CAP_LM_MASTER;

		if (test_bit(FLAG_FORCE_RELIABLE, &chan->flags))
			opt |= L2CAP_LM_RELIABLE;

		if (put_user(opt, (u32 __user *) optval))
			err = -EFAULT;
		break;

	case L2CAP_CONNINFO:
		if (sk->sk_state != BT_CONNECTED &&
		    !(sk->sk_state == BT_CONNECT2 &&
		      test_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags))) {
			err = -ENOTCONN;
			break;
		}

		memset(&cinfo, 0, sizeof(cinfo));
		cinfo.hci_handle = chan->conn->hcon->handle;
		memcpy(cinfo.dev_class, chan->conn->hcon->dev_class, 3);

		len = min_t(unsigned int, len, sizeof(cinfo));
		if (copy_to_user(optval, (char *) &cinfo, len))
			err = -EFAULT;

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct bt_security sec;
	struct bt_power pwr;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (level == SOL_L2CAP)
		return l2cap_sock_getsockopt_old(sock, optname, optval, optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case BT_SECURITY:
		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED &&
					chan->chan_type != L2CAP_CHAN_RAW) {
			err = -EINVAL;
			break;
		}

		memset(&sec, 0, sizeof(sec));
		if (chan->conn)
			sec.level = chan->conn->hcon->sec_level;
		else
			sec.level = chan->sec_level;

		if (sk->sk_state == BT_CONNECTED)
			sec.key_size = chan->conn->hcon->enc_key_size;

		len = min_t(unsigned int, len, sizeof(sec));
		if (copy_to_user(optval, (char *) &sec, len))
			err = -EFAULT;

		break;

	case BT_DEFER_SETUP:
		if (sk->sk_state != BT_BOUND && sk->sk_state != BT_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (put_user(test_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags),
			     (u32 __user *) optval))
			err = -EFAULT;

		break;

	case BT_FLUSHABLE:
		if (put_user(test_bit(FLAG_FLUSHABLE, &chan->flags),
						(u32 __user *) optval))
			err = -EFAULT;

		break;

	case BT_POWER:
		if (sk->sk_type != SOCK_SEQPACKET && sk->sk_type != SOCK_STREAM
				&& sk->sk_type != SOCK_RAW) {
			err = -EINVAL;
			break;
		}

		pwr.force_active = test_bit(FLAG_FORCE_ACTIVE, &chan->flags);

		len = min_t(unsigned int, len, sizeof(pwr));
		if (copy_to_user(optval, (char *) &pwr, len))
			err = -EFAULT;

		break;

	case BT_CHANNEL_POLICY:
		if (!enable_hs) {
			err = -ENOPROTOOPT;
			break;
		}

		if (put_user(chan->chan_policy, (u32 __user *) optval))
			err = -EFAULT;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static bool l2cap_valid_mtu(struct l2cap_chan *chan, u16 mtu)
{
	switch (chan->scid) {
	case L2CAP_CID_LE_DATA:
		if (mtu < L2CAP_LE_MIN_MTU)
			return false;
		break;

	default:
		if (mtu < L2CAP_DEFAULT_MIN_MTU)
			return false;
	}

	return true;
}

static int l2cap_sock_setsockopt_old(struct socket *sock, int optname, char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct l2cap_options opts;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		if (sk->sk_state == BT_CONNECTED) {
			err = -EINVAL;
			break;
		}

		opts.imtu     = chan->imtu;
		opts.omtu     = chan->omtu;
		opts.flush_to = chan->flush_to;
		opts.mode     = chan->mode;
		opts.fcs      = chan->fcs;
		opts.max_tx   = chan->max_tx;
		opts.txwin_size = chan->tx_win;

		len = min_t(unsigned int, sizeof(opts), optlen);
		if (copy_from_user((char *) &opts, optval, len)) {
			err = -EFAULT;
			break;
		}

		if (opts.txwin_size > L2CAP_DEFAULT_EXT_WINDOW) {
			err = -EINVAL;
			break;
		}

		if (!l2cap_valid_mtu(chan, opts.imtu)) {
			err = -EINVAL;
			break;
		}

		chan->mode = opts.mode;
		switch (chan->mode) {
		case L2CAP_MODE_BASIC:
			clear_bit(CONF_STATE2_DEVICE, &chan->conf_state);
			break;
		case L2CAP_MODE_ERTM:
		case L2CAP_MODE_STREAMING:
			if (!disable_ertm)
				break;
			/* fall through */
		default:
			err = -EINVAL;
			break;
		}

		chan->imtu = opts.imtu;
		chan->omtu = opts.omtu;
		chan->fcs  = opts.fcs;
		chan->max_tx = opts.max_tx;
		chan->tx_win = opts.txwin_size;
		break;

	case L2CAP_LM:
		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt & L2CAP_LM_AUTH)
			chan->sec_level = BT_SECURITY_LOW;
		if (opt & L2CAP_LM_ENCRYPT)
			chan->sec_level = BT_SECURITY_MEDIUM;
		if (opt & L2CAP_LM_SECURE)
			chan->sec_level = BT_SECURITY_HIGH;

		if (opt & L2CAP_LM_MASTER)
			set_bit(FLAG_ROLE_SWITCH, &chan->flags);
		else
			clear_bit(FLAG_ROLE_SWITCH, &chan->flags);

		if (opt & L2CAP_LM_RELIABLE)
			set_bit(FLAG_FORCE_RELIABLE, &chan->flags);
		else
			clear_bit(FLAG_FORCE_RELIABLE, &chan->flags);
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_setsockopt(struct socket *sock, int level, int optname, char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct bt_security sec;
	struct bt_power pwr;
	struct l2cap_conn *conn;
	int len, err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	if (level == SOL_L2CAP)
		return l2cap_sock_setsockopt_old(sock, optname, optval, optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case BT_SECURITY:
		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED &&
					chan->chan_type != L2CAP_CHAN_RAW) {
			err = -EINVAL;
			break;
		}

		sec.level = BT_SECURITY_LOW;

		len = min_t(unsigned int, sizeof(sec), optlen);
		if (copy_from_user((char *) &sec, optval, len)) {
			err = -EFAULT;
			break;
		}

		if (sec.level < BT_SECURITY_LOW ||
					sec.level > BT_SECURITY_HIGH) {
			err = -EINVAL;
			break;
		}

		chan->sec_level = sec.level;

		if (!chan->conn)
			break;

		conn = chan->conn;

		/*change security for LE channels */
		if (chan->scid == L2CAP_CID_LE_DATA) {
			if (!conn->hcon->out) {
				err = -EINVAL;
				break;
			}

			if (smp_conn_security(conn, sec.level))
				break;
			sk->sk_state = BT_CONFIG;
			chan->state = BT_CONFIG;

		/* or for ACL link */
		} else if ((sk->sk_state == BT_CONNECT2 &&
			   test_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags)) ||
			   sk->sk_state == BT_CONNECTED) {
			if (!l2cap_chan_check_security(chan))
				set_bit(BT_SK_SUSPEND, &bt_sk(sk)->flags);
			else
				sk->sk_state_change(sk);
		} else {
			err = -EINVAL;
		}
		break;

	case BT_DEFER_SETUP:
		if (sk->sk_state != BT_BOUND && sk->sk_state != BT_LISTEN) {
			err = -EINVAL;
			break;
		}

		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt)
			set_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags);
		else
			clear_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags);
		break;

	case BT_FLUSHABLE:
		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt > BT_FLUSHABLE_ON) {
			err = -EINVAL;
			break;
		}

		if (opt == BT_FLUSHABLE_OFF) {
			struct l2cap_conn *conn = chan->conn;
			/* proceed further only when we have l2cap_conn and
			   No Flush support in the LM */
			if (!conn || !lmp_no_flush_capable(conn->hcon->hdev)) {
				err = -EINVAL;
				break;
			}
		}

		if (opt)
			set_bit(FLAG_FLUSHABLE, &chan->flags);
		else
			clear_bit(FLAG_FLUSHABLE, &chan->flags);
		break;

	case BT_POWER:
		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED &&
					chan->chan_type != L2CAP_CHAN_RAW) {
			err = -EINVAL;
			break;
		}

		pwr.force_active = BT_POWER_FORCE_ACTIVE_ON;

		len = min_t(unsigned int, sizeof(pwr), optlen);
		if (copy_from_user((char *) &pwr, optval, len)) {
			err = -EFAULT;
			break;
		}

		if (pwr.force_active)
			set_bit(FLAG_FORCE_ACTIVE, &chan->flags);
		else
			clear_bit(FLAG_FORCE_ACTIVE, &chan->flags);
		break;

	case BT_CHANNEL_POLICY:
		if (!enable_hs) {
			err = -ENOPROTOOPT;
			break;
		}

		if (get_user(opt, (u32 __user *) optval)) {
			err = -EFAULT;
			break;
		}

		if (opt > BT_CHANNEL_POLICY_AMP_PREFERRED) {
			err = -EINVAL;
			break;
		}

		if (chan->mode != L2CAP_MODE_ERTM &&
				chan->mode != L2CAP_MODE_STREAMING) {
			err = -EOPNOTSUPP;
			break;
		}

		chan->chan_policy = (u8) opt;
		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_sendmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	int err;

	BT_DBG("sock %p, sk %p", sock, sk);

	err = sock_error(sk);
	if (err)
		return err;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	if (sk->sk_state != BT_CONNECTED)
		return -ENOTCONN;

	l2cap_chan_lock(chan);
	err = l2cap_chan_send(chan, msg, len, sk->sk_priority);
	l2cap_chan_unlock(chan);

	return err;
}

static int l2cap_sock_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int err;

	lock_sock(sk);

	if (sk->sk_state == BT_CONNECT2 && test_bit(BT_SK_DEFER_SETUP,
						    &bt_sk(sk)->flags)) {
		sk->sk_state = BT_CONFIG;
		pi->chan->state = BT_CONFIG;

		__l2cap_connect_rsp_defer(pi->chan);
		release_sock(sk);
		return 0;
	}

	release_sock(sk);

	if (sock->type == SOCK_STREAM)
		err = bt_sock_stream_recvmsg(iocb, sock, msg, len, flags);
	else
		err = bt_sock_recvmsg(iocb, sock, msg, len, flags);

	if (pi->chan->mode != L2CAP_MODE_ERTM)
		return err;

	/* Attempt to put pending rx data in the socket buffer */

	lock_sock(sk);

	if (!test_bit(CONN_LOCAL_BUSY, &pi->chan->conn_state))
		goto done;

	if (pi->rx_busy_skb) {
		if (!sock_queue_rcv_skb(sk, pi->rx_busy_skb))
			pi->rx_busy_skb = NULL;
		else
			goto done;
	}

	/* Restore data flow when half of the receive buffer is
	 * available.  This avoids resending large numbers of
	 * frames.
	 */
	if (atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf >> 1)
		l2cap_chan_busy(pi->chan, 0);

done:
	release_sock(sk);
	return err;
}

/* Kill socket (only if zapped and orphan)
 * Must be called on unlocked socket.
 */
static void l2cap_sock_kill(struct sock *sk)
{
	if (!sock_flag(sk, SOCK_ZAPPED) || sk->sk_socket)
		return;

	BT_DBG("sk %p state %s", sk, state_to_string(sk->sk_state));

	/* Kill poor orphan */

	l2cap_chan_destroy(l2cap_pi(sk)->chan);
	sock_set_flag(sk, SOCK_DEAD);
	sock_put(sk);
}

static int l2cap_sock_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan;
	struct l2cap_conn *conn;
	int err = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	chan = l2cap_pi(sk)->chan;
	conn = chan->conn;

	if (conn)
		mutex_lock(&conn->chan_lock);

	l2cap_chan_lock(chan);
	lock_sock(sk);

	if (!sk->sk_shutdown) {
		if (chan->mode == L2CAP_MODE_ERTM)
			err = __l2cap_wait_ack(sk);

		sk->sk_shutdown = SHUTDOWN_MASK;

		release_sock(sk);
		l2cap_chan_close(chan, 0);
		lock_sock(sk);

		if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime)
			err = bt_sock_wait_state(sk, BT_CLOSED,
							sk->sk_lingertime);
	}

	if (!err && sk->sk_err)
		err = -sk->sk_err;

	release_sock(sk);
	l2cap_chan_unlock(chan);

	if (conn)
		mutex_unlock(&conn->chan_lock);

	return err;
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

static void l2cap_sock_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	BT_DBG("parent %p", parent);

	/* Close not yet accepted channels */
	while ((sk = bt_accept_dequeue(parent, NULL))) {
		struct l2cap_chan *chan = l2cap_pi(sk)->chan;

		l2cap_chan_lock(chan);
		__clear_chan_timer(chan);
		l2cap_chan_close(chan, ECONNRESET);
		l2cap_chan_unlock(chan);

		l2cap_sock_kill(sk);
	}
}

static struct l2cap_chan *l2cap_sock_new_connection_cb(struct l2cap_chan *chan)
{
	struct sock *sk, *parent = chan->data;

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		return NULL;
	}

	sk = l2cap_sock_alloc(sock_net(parent), NULL, BTPROTO_L2CAP,
								GFP_ATOMIC);
	if (!sk)
		return NULL;

	bt_sock_reclassify_lock(sk, BTPROTO_L2CAP);

	l2cap_sock_init(sk, parent);

	return l2cap_pi(sk)->chan;
}

static int l2cap_sock_recv_cb(struct l2cap_chan *chan, struct sk_buff *skb)
{
	int err;
	struct sock *sk = chan->data;
	struct l2cap_pinfo *pi = l2cap_pi(sk);

	lock_sock(sk);

	if (pi->rx_busy_skb) {
		err = -ENOMEM;
		goto done;
	}

	err = sock_queue_rcv_skb(sk, skb);

	/* For ERTM, handle one skb that doesn't fit into the recv
	 * buffer.  This is important to do because the data frames
	 * have already been acked, so the skb cannot be discarded.
	 *
	 * Notify the l2cap core that the buffer is full, so the
	 * LOCAL_BUSY state is entered and no more frames are
	 * acked and reassembled until there is buffer space
	 * available.
	 */
	if (err < 0 && pi->chan->mode == L2CAP_MODE_ERTM) {
		pi->rx_busy_skb = skb;
		l2cap_chan_busy(pi->chan, 1);
		err = 0;
	}

done:
	release_sock(sk);

	return err;
}

static void l2cap_sock_close_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;

	l2cap_sock_kill(sk);
}

static void l2cap_sock_teardown_cb(struct l2cap_chan *chan, int err)
{
	struct sock *sk = chan->data;
	struct sock *parent;

	lock_sock(sk);

	parent = bt_sk(sk)->parent;

	sock_set_flag(sk, SOCK_ZAPPED);

	switch (chan->state) {
	case BT_OPEN:
	case BT_BOUND:
	case BT_CLOSED:
		break;
	case BT_LISTEN:
		l2cap_sock_cleanup_listen(sk);
		sk->sk_state = BT_CLOSED;
		chan->state = BT_CLOSED;

		break;
	default:
		sk->sk_state = BT_CLOSED;
		chan->state = BT_CLOSED;

		sk->sk_err = err;

		if (parent) {
			bt_accept_unlink(sk);
			parent->sk_data_ready(parent, 0);
		} else {
			sk->sk_state_change(sk);
		}

		break;
	}

	release_sock(sk);
}

static void l2cap_sock_state_change_cb(struct l2cap_chan *chan, int state)
{
	struct sock *sk = chan->data;

	sk->sk_state = state;
}

static struct sk_buff *l2cap_sock_alloc_skb_cb(struct l2cap_chan *chan,
					       unsigned long len, int nb)
{
	struct sk_buff *skb;
	int err;

	l2cap_chan_unlock(chan);
	skb = bt_skb_send_alloc(chan->sk, len, nb, &err);
	l2cap_chan_lock(chan);

	if (!skb)
		return ERR_PTR(err);

	return skb;
}

static void l2cap_sock_ready_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;
	struct sock *parent;

	lock_sock(sk);

	parent = bt_sk(sk)->parent;

	BT_DBG("sk %p, parent %p", sk, parent);

	sk->sk_state = BT_CONNECTED;
	sk->sk_state_change(sk);

	if (parent)
		parent->sk_data_ready(parent, 0);

	release_sock(sk);
}

static struct l2cap_ops l2cap_chan_ops = {
	.name		= "L2CAP Socket Interface",
	.new_connection	= l2cap_sock_new_connection_cb,
	.recv		= l2cap_sock_recv_cb,
	.close		= l2cap_sock_close_cb,
	.teardown	= l2cap_sock_teardown_cb,
	.state_change	= l2cap_sock_state_change_cb,
	.ready		= l2cap_sock_ready_cb,
	.alloc_skb	= l2cap_sock_alloc_skb_cb,
};

static void l2cap_sock_destruct(struct sock *sk)
{
	BT_DBG("sk %p", sk);

	l2cap_chan_put(l2cap_pi(sk)->chan);
	if (l2cap_pi(sk)->rx_busy_skb) {
		kfree_skb(l2cap_pi(sk)->rx_busy_skb);
		l2cap_pi(sk)->rx_busy_skb = NULL;
	}

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
}

static void l2cap_sock_init(struct sock *sk, struct sock *parent)
{
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	struct l2cap_chan *chan = pi->chan;

	BT_DBG("sk %p", sk);

	if (parent) {
		struct l2cap_chan *pchan = l2cap_pi(parent)->chan;

		sk->sk_type = parent->sk_type;
		bt_sk(sk)->flags = bt_sk(parent)->flags;

		chan->chan_type = pchan->chan_type;
		chan->imtu = pchan->imtu;
		chan->omtu = pchan->omtu;
		chan->conf_state = pchan->conf_state;
		chan->mode = pchan->mode;
		chan->fcs  = pchan->fcs;
		chan->max_tx = pchan->max_tx;
		chan->tx_win = pchan->tx_win;
		chan->tx_win_max = pchan->tx_win_max;
		chan->sec_level = pchan->sec_level;
		chan->flags = pchan->flags;

		security_sk_clone(parent, sk);
	} else {

		switch (sk->sk_type) {
		case SOCK_RAW:
			chan->chan_type = L2CAP_CHAN_RAW;
			break;
		case SOCK_DGRAM:
			chan->chan_type = L2CAP_CHAN_CONN_LESS;
			break;
		case SOCK_SEQPACKET:
		case SOCK_STREAM:
			chan->chan_type = L2CAP_CHAN_CONN_ORIENTED;
			break;
		}

		chan->imtu = L2CAP_DEFAULT_MTU;
		chan->omtu = 0;
		if (!disable_ertm && sk->sk_type == SOCK_STREAM) {
			chan->mode = L2CAP_MODE_ERTM;
			set_bit(CONF_STATE2_DEVICE, &chan->conf_state);
		} else {
			chan->mode = L2CAP_MODE_BASIC;
		}

		l2cap_chan_set_defaults(chan);
	}

	/* Default config options */
	chan->flush_to = L2CAP_DEFAULT_FLUSH_TO;

	chan->data = sk;
	chan->ops = &l2cap_chan_ops;
}

static struct proto l2cap_proto = {
	.name		= "L2CAP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct l2cap_pinfo)
};

static struct sock *l2cap_sock_alloc(struct net *net, struct socket *sock, int proto, gfp_t prio)
{
	struct sock *sk;
	struct l2cap_chan *chan;

	sk = sk_alloc(net, PF_BLUETOOTH, prio, &l2cap_proto);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);

	sk->sk_destruct = l2cap_sock_destruct;
	sk->sk_sndtimeo = L2CAP_CONN_TIMEOUT;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state = BT_OPEN;

	chan = l2cap_chan_create();
	if (!chan) {
		sk_free(sk);
		return NULL;
	}

	l2cap_chan_hold(chan);

	chan->sk = sk;

	l2cap_pi(sk)->chan = chan;

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

static const struct proto_ops l2cap_sock_ops = {
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
