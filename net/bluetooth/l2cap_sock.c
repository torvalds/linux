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

#include <linux/module.h>
#include <linux/export.h>
#include <linux/filter.h>
#include <linux/sched/signal.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/l2cap.h>

#include "smp.h"

static struct bt_sock_list l2cap_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(l2cap_sk_list.lock)
};

static const struct proto_ops l2cap_sock_ops;
static void l2cap_sock_init(struct sock *sk, struct sock *parent);
static struct sock *l2cap_sock_alloc(struct net *net, struct socket *sock,
				     int proto, gfp_t prio, int kern);
static void l2cap_sock_cleanup_listen(struct sock *parent);

bool l2cap_is_socket(struct socket *sock)
{
	return sock && sock->ops == &l2cap_sock_ops;
}
EXPORT_SYMBOL(l2cap_is_socket);

static int l2cap_validate_bredr_psm(u16 psm)
{
	/* PSM must be odd and lsb of upper byte must be 0 */
	if ((psm & 0x0101) != 0x0001)
		return -EINVAL;

	/* Restrict usage of well-known PSMs */
	if (psm < L2CAP_PSM_DYN_START && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	return 0;
}

static int l2cap_validate_le_psm(u16 psm)
{
	/* Valid LE_PSM ranges are defined only until 0x00ff */
	if (psm > L2CAP_PSM_LE_DYN_END)
		return -EINVAL;

	/* Restrict fixed, SIG assigned PSM values to CAP_NET_BIND_SERVICE */
	if (psm < L2CAP_PSM_LE_DYN_START && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	return 0;
}

static int l2cap_sock_bind(struct socket *sock, struct sockaddr *addr, int alen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct sockaddr_l2 la;
	int len, err = 0;

	BT_DBG("sk %p", sk);

	if (!addr || alen < offsetofend(struct sockaddr, sa_family) ||
	    addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&la, 0, sizeof(la));
	len = min_t(unsigned int, sizeof(la), alen);
	memcpy(&la, addr, len);

	if (la.l2_cid && la.l2_psm)
		return -EINVAL;

	if (!bdaddr_type_is_valid(la.l2_bdaddr_type))
		return -EINVAL;

	if (bdaddr_type_is_le(la.l2_bdaddr_type)) {
		/* We only allow ATT user space socket */
		if (la.l2_cid &&
		    la.l2_cid != cpu_to_le16(L2CAP_CID_ATT))
			return -EINVAL;
	}

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	if (la.l2_psm) {
		__u16 psm = __le16_to_cpu(la.l2_psm);

		if (la.l2_bdaddr_type == BDADDR_BREDR)
			err = l2cap_validate_bredr_psm(psm);
		else
			err = l2cap_validate_le_psm(psm);

		if (err)
			goto done;
	}

	bacpy(&chan->src, &la.l2_bdaddr);
	chan->src_type = la.l2_bdaddr_type;

	if (la.l2_cid)
		err = l2cap_add_scid(chan, __le16_to_cpu(la.l2_cid));
	else
		err = l2cap_add_psm(chan, &la.l2_bdaddr, la.l2_psm);

	if (err < 0)
		goto done;

	switch (chan->chan_type) {
	case L2CAP_CHAN_CONN_LESS:
		if (__le16_to_cpu(la.l2_psm) == L2CAP_PSM_3DSP)
			chan->sec_level = BT_SECURITY_SDP;
		break;
	case L2CAP_CHAN_CONN_ORIENTED:
		if (__le16_to_cpu(la.l2_psm) == L2CAP_PSM_SDP ||
		    __le16_to_cpu(la.l2_psm) == L2CAP_PSM_RFCOMM)
			chan->sec_level = BT_SECURITY_SDP;
		break;
	case L2CAP_CHAN_RAW:
		chan->sec_level = BT_SECURITY_SDP;
		break;
	case L2CAP_CHAN_FIXED:
		/* Fixed channels default to the L2CAP core not holding a
		 * hci_conn reference for them. For fixed channels mapping to
		 * L2CAP sockets we do want to hold a reference so set the
		 * appropriate flag to request it.
		 */
		set_bit(FLAG_HOLD_HCI_CONN, &chan->flags);
		break;
	}

	/* Use L2CAP_MODE_LE_FLOWCTL (CoC) in case of LE address and
	 * L2CAP_MODE_EXT_FLOWCTL (ECRED) has not been set.
	 */
	if (chan->psm && bdaddr_type_is_le(chan->src_type) &&
	    chan->mode != L2CAP_MODE_EXT_FLOWCTL)
		chan->mode = L2CAP_MODE_LE_FLOWCTL;

	chan->state = BT_BOUND;
	sk->sk_state = BT_BOUND;

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_connect(struct socket *sock, struct sockaddr *addr,
			      int alen, int flags)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct sockaddr_l2 la;
	int len, err = 0;
	bool zapped;

	BT_DBG("sk %p", sk);

	lock_sock(sk);
	zapped = sock_flag(sk, SOCK_ZAPPED);
	release_sock(sk);

	if (zapped)
		return -EINVAL;

	if (!addr || alen < offsetofend(struct sockaddr, sa_family) ||
	    addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	memset(&la, 0, sizeof(la));
	len = min_t(unsigned int, sizeof(la), alen);
	memcpy(&la, addr, len);

	if (la.l2_cid && la.l2_psm)
		return -EINVAL;

	if (!bdaddr_type_is_valid(la.l2_bdaddr_type))
		return -EINVAL;

	/* Check that the socket wasn't bound to something that
	 * conflicts with the address given to connect(). If chan->src
	 * is BDADDR_ANY it means bind() was never used, in which case
	 * chan->src_type and la.l2_bdaddr_type do not need to match.
	 */
	if (chan->src_type == BDADDR_BREDR && bacmp(&chan->src, BDADDR_ANY) &&
	    bdaddr_type_is_le(la.l2_bdaddr_type)) {
		/* Old user space versions will try to incorrectly bind
		 * the ATT socket using BDADDR_BREDR. We need to accept
		 * this and fix up the source address type only when
		 * both the source CID and destination CID indicate
		 * ATT. Anything else is an invalid combination.
		 */
		if (chan->scid != L2CAP_CID_ATT ||
		    la.l2_cid != cpu_to_le16(L2CAP_CID_ATT))
			return -EINVAL;

		/* We don't have the hdev available here to make a
		 * better decision on random vs public, but since all
		 * user space versions that exhibit this issue anyway do
		 * not support random local addresses assuming public
		 * here is good enough.
		 */
		chan->src_type = BDADDR_LE_PUBLIC;
	}

	if (chan->src_type != BDADDR_BREDR && la.l2_bdaddr_type == BDADDR_BREDR)
		return -EINVAL;

	if (bdaddr_type_is_le(la.l2_bdaddr_type)) {
		/* We only allow ATT user space socket */
		if (la.l2_cid &&
		    la.l2_cid != cpu_to_le16(L2CAP_CID_ATT))
			return -EINVAL;
	}

	/* Use L2CAP_MODE_LE_FLOWCTL (CoC) in case of LE address and
	 * L2CAP_MODE_EXT_FLOWCTL (ECRED) has not been set.
	 */
	if (chan->psm && bdaddr_type_is_le(chan->src_type) &&
	    chan->mode != L2CAP_MODE_EXT_FLOWCTL)
		chan->mode = L2CAP_MODE_LE_FLOWCTL;

	err = l2cap_chan_connect(chan, la.l2_psm, __le16_to_cpu(la.l2_cid),
				 &la.l2_bdaddr, la.l2_bdaddr_type,
				 sk->sk_sndtimeo);
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
	case L2CAP_MODE_LE_FLOWCTL:
		break;
	case L2CAP_MODE_EXT_FLOWCTL:
		if (!enable_ecred) {
			err = -EOPNOTSUPP;
			goto done;
		}
		break;
	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		if (!disable_ertm)
			break;
		fallthrough;
	default:
		err = -EOPNOTSUPP;
		goto done;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;

	/* Listening channels need to use nested locking in order not to
	 * cause lockdep warnings when the created child channels end up
	 * being locked in the same thread as the parent channel.
	 */
	atomic_set(&chan->nesting, L2CAP_NESTING_PARENT);

	chan->state = BT_LISTEN;
	sk->sk_state = BT_LISTEN;

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_accept(struct socket *sock, struct socket *newsock,
			     struct proto_accept_arg *arg)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct sock *sk = sock->sk, *nsk;
	long timeo;
	int err = 0;

	lock_sock_nested(sk, L2CAP_NESTING_PARENT);

	timeo = sock_rcvtimeo(sk, arg->flags & O_NONBLOCK);

	BT_DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk_sleep(sk), &wait);
	while (1) {
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

		timeo = wait_woken(&wait, TASK_INTERRUPTIBLE, timeo);

		lock_sock_nested(sk, L2CAP_NESTING_PARENT);
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	if (err)
		goto done;

	newsock->state = SS_CONNECTED;

	BT_DBG("new socket %p", nsk);

done:
	release_sock(sk);
	return err;
}

static int l2cap_sock_getname(struct socket *sock, struct sockaddr *addr,
			      int peer)
{
	struct sockaddr_l2 *la = (struct sockaddr_l2 *) addr;
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (peer && sk->sk_state != BT_CONNECTED &&
	    sk->sk_state != BT_CONNECT && sk->sk_state != BT_CONNECT2 &&
	    sk->sk_state != BT_CONFIG)
		return -ENOTCONN;

	memset(la, 0, sizeof(struct sockaddr_l2));
	addr->sa_family = AF_BLUETOOTH;

	la->l2_psm = chan->psm;

	if (peer) {
		bacpy(&la->l2_bdaddr, &chan->dst);
		la->l2_cid = cpu_to_le16(chan->dcid);
		la->l2_bdaddr_type = chan->dst_type;
	} else {
		bacpy(&la->l2_bdaddr, &chan->src);
		la->l2_cid = cpu_to_le16(chan->scid);
		la->l2_bdaddr_type = chan->src_type;
	}

	return sizeof(struct sockaddr_l2);
}

static int l2cap_get_mode(struct l2cap_chan *chan)
{
	switch (chan->mode) {
	case L2CAP_MODE_BASIC:
		return BT_MODE_BASIC;
	case L2CAP_MODE_ERTM:
		return BT_MODE_ERTM;
	case L2CAP_MODE_STREAMING:
		return BT_MODE_STREAMING;
	case L2CAP_MODE_LE_FLOWCTL:
		return BT_MODE_LE_FLOWCTL;
	case L2CAP_MODE_EXT_FLOWCTL:
		return BT_MODE_EXT_FLOWCTL;
	}

	return -EINVAL;
}

static int l2cap_sock_getsockopt_old(struct socket *sock, int optname,
				     char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct l2cap_options opts;
	struct l2cap_conninfo cinfo;
	int err = 0;
	size_t len;
	u32 opt;

	BT_DBG("sk %p", sk);

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		/* LE sockets should use BT_SNDMTU/BT_RCVMTU, but since
		 * legacy ATT code depends on getsockopt for
		 * L2CAP_OPTIONS we need to let this pass.
		 */
		if (bdaddr_type_is_le(chan->src_type) &&
		    chan->scid != L2CAP_CID_ATT) {
			err = -EINVAL;
			break;
		}

		/* Only BR/EDR modes are supported here */
		switch (chan->mode) {
		case L2CAP_MODE_BASIC:
		case L2CAP_MODE_ERTM:
		case L2CAP_MODE_STREAMING:
			break;
		default:
			err = -EINVAL;
			break;
		}

		if (err < 0)
			break;

		memset(&opts, 0, sizeof(opts));
		opts.imtu     = chan->imtu;
		opts.omtu     = chan->omtu;
		opts.flush_to = chan->flush_to;
		opts.mode     = chan->mode;
		opts.fcs      = chan->fcs;
		opts.max_tx   = chan->max_tx;
		opts.txwin_size = chan->tx_win;

		BT_DBG("mode 0x%2.2x", chan->mode);

		len = min(len, sizeof(opts));
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
		case BT_SECURITY_FIPS:
			opt = L2CAP_LM_AUTH | L2CAP_LM_ENCRYPT |
			      L2CAP_LM_SECURE | L2CAP_LM_FIPS;
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

		len = min(len, sizeof(cinfo));
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

static int l2cap_sock_getsockopt(struct socket *sock, int level, int optname,
				 char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct bt_security sec;
	struct bt_power pwr;
	u32 phys;
	int len, mode, err = 0;

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
		    chan->chan_type != L2CAP_CHAN_FIXED &&
		    chan->chan_type != L2CAP_CHAN_RAW) {
			err = -EINVAL;
			break;
		}

		memset(&sec, 0, sizeof(sec));
		if (chan->conn) {
			sec.level = chan->conn->hcon->sec_level;

			if (sk->sk_state == BT_CONNECTED)
				sec.key_size = chan->conn->hcon->enc_key_size;
		} else {
			sec.level = chan->sec_level;
		}

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
		if (put_user(chan->chan_policy, (u32 __user *) optval))
			err = -EFAULT;
		break;

	case BT_SNDMTU:
		if (!bdaddr_type_is_le(chan->src_type)) {
			err = -EINVAL;
			break;
		}

		if (sk->sk_state != BT_CONNECTED) {
			err = -ENOTCONN;
			break;
		}

		if (put_user(chan->omtu, (u16 __user *) optval))
			err = -EFAULT;
		break;

	case BT_RCVMTU:
		if (!bdaddr_type_is_le(chan->src_type)) {
			err = -EINVAL;
			break;
		}

		if (put_user(chan->imtu, (u16 __user *) optval))
			err = -EFAULT;
		break;

	case BT_PHY:
		if (sk->sk_state != BT_CONNECTED) {
			err = -ENOTCONN;
			break;
		}

		phys = hci_conn_get_phy(chan->conn->hcon);

		if (put_user(phys, (u32 __user *) optval))
			err = -EFAULT;
		break;

	case BT_MODE:
		if (!enable_ecred) {
			err = -ENOPROTOOPT;
			break;
		}

		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			err = -EINVAL;
			break;
		}

		mode = l2cap_get_mode(chan);
		if (mode < 0) {
			err = mode;
			break;
		}

		if (put_user(mode, (u8 __user *) optval))
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
	case L2CAP_CID_ATT:
		if (mtu < L2CAP_LE_MIN_MTU)
			return false;
		break;

	default:
		if (mtu < L2CAP_DEFAULT_MIN_MTU)
			return false;
	}

	return true;
}

static int l2cap_sock_setsockopt_old(struct socket *sock, int optname,
				     sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct l2cap_options opts;
	int err = 0;
	u32 opt;

	BT_DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	case L2CAP_OPTIONS:
		if (bdaddr_type_is_le(chan->src_type)) {
			err = -EINVAL;
			break;
		}

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

		err = bt_copy_from_sockptr(&opts, sizeof(opts), optval, optlen);
		if (err)
			break;

		if (opts.txwin_size > L2CAP_DEFAULT_EXT_WINDOW) {
			err = -EINVAL;
			break;
		}

		if (!l2cap_valid_mtu(chan, opts.imtu)) {
			err = -EINVAL;
			break;
		}

		/* Only BR/EDR modes are supported here */
		switch (opts.mode) {
		case L2CAP_MODE_BASIC:
			clear_bit(CONF_STATE2_DEVICE, &chan->conf_state);
			break;
		case L2CAP_MODE_ERTM:
		case L2CAP_MODE_STREAMING:
			if (!disable_ertm)
				break;
			fallthrough;
		default:
			err = -EINVAL;
			break;
		}

		if (err < 0)
			break;

		chan->mode = opts.mode;

		BT_DBG("mode 0x%2.2x", chan->mode);

		chan->imtu = opts.imtu;
		chan->omtu = opts.omtu;
		chan->fcs  = opts.fcs;
		chan->max_tx = opts.max_tx;
		chan->tx_win = opts.txwin_size;
		chan->flush_to = opts.flush_to;
		break;

	case L2CAP_LM:
		err = bt_copy_from_sockptr(&opt, sizeof(opt), optval, optlen);
		if (err)
			break;

		if (opt & L2CAP_LM_FIPS) {
			err = -EINVAL;
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

static int l2cap_set_mode(struct l2cap_chan *chan, u8 mode)
{
	switch (mode) {
	case BT_MODE_BASIC:
		if (bdaddr_type_is_le(chan->src_type))
			return -EINVAL;
		mode = L2CAP_MODE_BASIC;
		clear_bit(CONF_STATE2_DEVICE, &chan->conf_state);
		break;
	case BT_MODE_ERTM:
		if (!disable_ertm || bdaddr_type_is_le(chan->src_type))
			return -EINVAL;
		mode = L2CAP_MODE_ERTM;
		break;
	case BT_MODE_STREAMING:
		if (!disable_ertm || bdaddr_type_is_le(chan->src_type))
			return -EINVAL;
		mode = L2CAP_MODE_STREAMING;
		break;
	case BT_MODE_LE_FLOWCTL:
		if (!bdaddr_type_is_le(chan->src_type))
			return -EINVAL;
		mode = L2CAP_MODE_LE_FLOWCTL;
		break;
	case BT_MODE_EXT_FLOWCTL:
		/* TODO: Add support for ECRED PDUs to BR/EDR */
		if (!bdaddr_type_is_le(chan->src_type))
			return -EINVAL;
		mode = L2CAP_MODE_EXT_FLOWCTL;
		break;
	default:
		return -EINVAL;
	}

	chan->mode = mode;

	return 0;
}

static int l2cap_sock_setsockopt(struct socket *sock, int level, int optname,
				 sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;
	struct bt_security sec;
	struct bt_power pwr;
	struct l2cap_conn *conn;
	int err = 0;
	u32 opt;
	u16 mtu;
	u8 mode;

	BT_DBG("sk %p", sk);

	if (level == SOL_L2CAP)
		return l2cap_sock_setsockopt_old(sock, optname, optval, optlen);

	if (level != SOL_BLUETOOTH)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case BT_SECURITY:
		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED &&
		    chan->chan_type != L2CAP_CHAN_FIXED &&
		    chan->chan_type != L2CAP_CHAN_RAW) {
			err = -EINVAL;
			break;
		}

		sec.level = BT_SECURITY_LOW;

		err = bt_copy_from_sockptr(&sec, sizeof(sec), optval, optlen);
		if (err)
			break;

		if (sec.level < BT_SECURITY_LOW ||
		    sec.level > BT_SECURITY_FIPS) {
			err = -EINVAL;
			break;
		}

		chan->sec_level = sec.level;

		if (!chan->conn)
			break;

		conn = chan->conn;

		/* change security for LE channels */
		if (chan->scid == L2CAP_CID_ATT) {
			if (smp_conn_security(conn->hcon, sec.level)) {
				err = -EINVAL;
				break;
			}

			set_bit(FLAG_PENDING_SECURITY, &chan->flags);
			sk->sk_state = BT_CONFIG;
			chan->state = BT_CONFIG;

		/* or for ACL link */
		} else if ((sk->sk_state == BT_CONNECT2 &&
			    test_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags)) ||
			   sk->sk_state == BT_CONNECTED) {
			if (!l2cap_chan_check_security(chan, true))
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

		err = bt_copy_from_sockptr(&opt, sizeof(opt), optval, optlen);
		if (err)
			break;

		if (opt) {
			set_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags);
			set_bit(FLAG_DEFER_SETUP, &chan->flags);
		} else {
			clear_bit(BT_SK_DEFER_SETUP, &bt_sk(sk)->flags);
			clear_bit(FLAG_DEFER_SETUP, &chan->flags);
		}
		break;

	case BT_FLUSHABLE:
		err = bt_copy_from_sockptr(&opt, sizeof(opt), optval, optlen);
		if (err)
			break;

		if (opt > BT_FLUSHABLE_ON) {
			err = -EINVAL;
			break;
		}

		if (opt == BT_FLUSHABLE_OFF) {
			conn = chan->conn;
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

		err = bt_copy_from_sockptr(&pwr, sizeof(pwr), optval, optlen);
		if (err)
			break;

		if (pwr.force_active)
			set_bit(FLAG_FORCE_ACTIVE, &chan->flags);
		else
			clear_bit(FLAG_FORCE_ACTIVE, &chan->flags);
		break;

	case BT_CHANNEL_POLICY:
		err = bt_copy_from_sockptr(&opt, sizeof(opt), optval, optlen);
		if (err)
			break;

		err = -EOPNOTSUPP;
		break;

	case BT_SNDMTU:
		if (!bdaddr_type_is_le(chan->src_type)) {
			err = -EINVAL;
			break;
		}

		/* Setting is not supported as it's the remote side that
		 * decides this.
		 */
		err = -EPERM;
		break;

	case BT_RCVMTU:
		if (!bdaddr_type_is_le(chan->src_type)) {
			err = -EINVAL;
			break;
		}

		if (chan->mode == L2CAP_MODE_LE_FLOWCTL &&
		    sk->sk_state == BT_CONNECTED) {
			err = -EISCONN;
			break;
		}

		err = bt_copy_from_sockptr(&mtu, sizeof(mtu), optval, optlen);
		if (err)
			break;

		if (chan->mode == L2CAP_MODE_EXT_FLOWCTL &&
		    sk->sk_state == BT_CONNECTED)
			err = l2cap_chan_reconfigure(chan, mtu);
		else
			chan->imtu = mtu;

		break;

	case BT_MODE:
		if (!enable_ecred) {
			err = -ENOPROTOOPT;
			break;
		}

		BT_DBG("sk->sk_state %u", sk->sk_state);

		if (sk->sk_state != BT_BOUND) {
			err = -EINVAL;
			break;
		}

		if (chan->chan_type != L2CAP_CHAN_CONN_ORIENTED) {
			err = -EINVAL;
			break;
		}

		err = bt_copy_from_sockptr(&mode, sizeof(mode), optval, optlen);
		if (err)
			break;

		BT_DBG("mode %u", mode);

		err = l2cap_set_mode(chan, mode);
		if (err)
			break;

		BT_DBG("mode 0x%2.2x", chan->mode);

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int l2cap_sock_sendmsg(struct socket *sock, struct msghdr *msg,
			      size_t len)
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

	lock_sock(sk);
	err = bt_sock_wait_ready(sk, msg->msg_flags);
	release_sock(sk);
	if (err)
		return err;

	l2cap_chan_lock(chan);
	err = l2cap_chan_send(chan, msg, len);
	l2cap_chan_unlock(chan);

	return err;
}

static void l2cap_publish_rx_avail(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;
	ssize_t avail = sk->sk_rcvbuf - atomic_read(&sk->sk_rmem_alloc);
	int expected_skbs, skb_overhead;

	if (avail <= 0) {
		l2cap_chan_rx_avail(chan, 0);
		return;
	}

	if (!chan->mps) {
		l2cap_chan_rx_avail(chan, -1);
		return;
	}

	/* Correct available memory by estimated sk_buff overhead.
	 * This is significant due to small transfer sizes. However, accept
	 * at least one full packet if receive space is non-zero.
	 */
	expected_skbs = DIV_ROUND_UP(avail, chan->mps);
	skb_overhead = expected_skbs * sizeof(struct sk_buff);
	if (skb_overhead < avail)
		l2cap_chan_rx_avail(chan, avail - skb_overhead);
	else
		l2cap_chan_rx_avail(chan, -1);
}

static int l2cap_sock_recvmsg(struct socket *sock, struct msghdr *msg,
			      size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct l2cap_pinfo *pi = l2cap_pi(sk);
	int err;

	lock_sock(sk);

	if (sk->sk_state == BT_CONNECT2 && test_bit(BT_SK_DEFER_SETUP,
						    &bt_sk(sk)->flags)) {
		if (pi->chan->mode == L2CAP_MODE_EXT_FLOWCTL) {
			sk->sk_state = BT_CONNECTED;
			pi->chan->state = BT_CONNECTED;
			__l2cap_ecred_conn_rsp_defer(pi->chan);
		} else if (bdaddr_type_is_le(pi->chan->src_type)) {
			sk->sk_state = BT_CONNECTED;
			pi->chan->state = BT_CONNECTED;
			__l2cap_le_connect_rsp_defer(pi->chan);
		} else {
			sk->sk_state = BT_CONFIG;
			pi->chan->state = BT_CONFIG;
			__l2cap_connect_rsp_defer(pi->chan);
		}

		err = 0;
		goto done;
	}

	release_sock(sk);

	if (sock->type == SOCK_STREAM)
		err = bt_sock_stream_recvmsg(sock, msg, len, flags);
	else
		err = bt_sock_recvmsg(sock, msg, len, flags);

	if (pi->chan->mode != L2CAP_MODE_ERTM &&
	    pi->chan->mode != L2CAP_MODE_LE_FLOWCTL &&
	    pi->chan->mode != L2CAP_MODE_EXT_FLOWCTL)
		return err;

	lock_sock(sk);

	l2cap_publish_rx_avail(pi->chan);

	/* Attempt to put pending rx data in the socket buffer */
	while (!list_empty(&pi->rx_busy)) {
		struct l2cap_rx_busy *rx_busy =
			list_first_entry(&pi->rx_busy,
					 struct l2cap_rx_busy,
					 list);
		if (__sock_queue_rcv_skb(sk, rx_busy->skb) < 0)
			goto done;
		list_del(&rx_busy->list);
		kfree(rx_busy);
	}

	/* Restore data flow when half of the receive buffer is
	 * available.  This avoids resending large numbers of
	 * frames.
	 */
	if (test_bit(CONN_LOCAL_BUSY, &pi->chan->conn_state) &&
	    atomic_read(&sk->sk_rmem_alloc) <= sk->sk_rcvbuf >> 1)
		l2cap_chan_busy(pi->chan, 0);

done:
	release_sock(sk);
	return err;
}

/* Kill socket (only if zapped and orphan)
 * Must be called on unlocked socket, with l2cap channel lock.
 */
static void l2cap_sock_kill(struct sock *sk)
{
	if (!sock_flag(sk, SOCK_ZAPPED) || sk->sk_socket)
		return;

	BT_DBG("sk %p state %s", sk, state_to_string(sk->sk_state));

	/* Sock is dead, so set chan data to NULL, avoid other task use invalid
	 * sock pointer.
	 */
	l2cap_pi(sk)->chan->data = NULL;
	/* Kill poor orphan */

	l2cap_chan_put(l2cap_pi(sk)->chan);
	sock_set_flag(sk, SOCK_DEAD);
	sock_put(sk);
}

static int __l2cap_wait_ack(struct sock *sk, struct l2cap_chan *chan)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;
	int timeo = L2CAP_WAIT_ACK_POLL_PERIOD;
	/* Timeout to prevent infinite loop */
	unsigned long timeout = jiffies + L2CAP_WAIT_ACK_TIMEOUT;

	add_wait_queue(sk_sleep(sk), &wait);
	set_current_state(TASK_INTERRUPTIBLE);
	do {
		BT_DBG("Waiting for %d ACKs, timeout %04d ms",
		       chan->unacked_frames, time_after(jiffies, timeout) ? 0 :
		       jiffies_to_msecs(timeout - jiffies));

		if (!timeo)
			timeo = L2CAP_WAIT_ACK_POLL_PERIOD;

		if (signal_pending(current)) {
			err = sock_intr_errno(timeo);
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);
		set_current_state(TASK_INTERRUPTIBLE);

		err = sock_error(sk);
		if (err)
			break;

		if (time_after(jiffies, timeout)) {
			err = -ENOLINK;
			break;
		}

	} while (chan->unacked_frames > 0 &&
		 chan->state == BT_CONNECTED);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk_sleep(sk), &wait);
	return err;
}

static int l2cap_sock_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct l2cap_chan *chan;
	struct l2cap_conn *conn;
	int err = 0;

	BT_DBG("sock %p, sk %p, how %d", sock, sk, how);

	/* 'how' parameter is mapped to sk_shutdown as follows:
	 * SHUT_RD   (0) --> RCV_SHUTDOWN  (1)
	 * SHUT_WR   (1) --> SEND_SHUTDOWN (2)
	 * SHUT_RDWR (2) --> SHUTDOWN_MASK (3)
	 */
	how++;

	if (!sk)
		return 0;

	lock_sock(sk);

	if ((sk->sk_shutdown & how) == how)
		goto shutdown_already;

	BT_DBG("Handling sock shutdown");

	/* prevent sk structure from being freed whilst unlocked */
	sock_hold(sk);

	chan = l2cap_pi(sk)->chan;
	/* prevent chan structure from being freed whilst unlocked */
	l2cap_chan_hold(chan);

	BT_DBG("chan %p state %s", chan, state_to_string(chan->state));

	if (chan->mode == L2CAP_MODE_ERTM &&
	    chan->unacked_frames > 0 &&
	    chan->state == BT_CONNECTED) {
		err = __l2cap_wait_ack(sk, chan);

		/* After waiting for ACKs, check whether shutdown
		 * has already been actioned to close the L2CAP
		 * link such as by l2cap_disconnection_req().
		 */
		if ((sk->sk_shutdown & how) == how)
			goto shutdown_matched;
	}

	/* Try setting the RCV_SHUTDOWN bit, return early if SEND_SHUTDOWN
	 * is already set
	 */
	if ((how & RCV_SHUTDOWN) && !(sk->sk_shutdown & RCV_SHUTDOWN)) {
		sk->sk_shutdown |= RCV_SHUTDOWN;
		if ((sk->sk_shutdown & how) == how)
			goto shutdown_matched;
	}

	sk->sk_shutdown |= SEND_SHUTDOWN;
	release_sock(sk);

	l2cap_chan_lock(chan);
	conn = chan->conn;
	if (conn)
		/* prevent conn structure from being freed */
		l2cap_conn_get(conn);
	l2cap_chan_unlock(chan);

	if (conn)
		/* mutex lock must be taken before l2cap_chan_lock() */
		mutex_lock(&conn->chan_lock);

	l2cap_chan_lock(chan);
	l2cap_chan_close(chan, 0);
	l2cap_chan_unlock(chan);

	if (conn) {
		mutex_unlock(&conn->chan_lock);
		l2cap_conn_put(conn);
	}

	lock_sock(sk);

	if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime &&
	    !(current->flags & PF_EXITING))
		err = bt_sock_wait_state(sk, BT_CLOSED,
					 sk->sk_lingertime);

shutdown_matched:
	l2cap_chan_put(chan);
	sock_put(sk);

shutdown_already:
	if (!err && sk->sk_err)
		err = -sk->sk_err;

	release_sock(sk);

	BT_DBG("Sock shutdown complete err: %d", err);

	return err;
}

static int l2cap_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int err;
	struct l2cap_chan *chan;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	l2cap_sock_cleanup_listen(sk);
	bt_sock_unlink(&l2cap_sk_list, sk);

	err = l2cap_sock_shutdown(sock, SHUT_RDWR);
	chan = l2cap_pi(sk)->chan;

	l2cap_chan_hold(chan);
	l2cap_chan_lock(chan);

	sock_orphan(sk);
	l2cap_sock_kill(sk);

	l2cap_chan_unlock(chan);
	l2cap_chan_put(chan);

	return err;
}

static void l2cap_sock_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	BT_DBG("parent %p state %s", parent,
	       state_to_string(parent->sk_state));

	/* Close not yet accepted channels */
	while ((sk = bt_accept_dequeue(parent, NULL))) {
		struct l2cap_chan *chan = l2cap_pi(sk)->chan;

		BT_DBG("child chan %p state %s", chan,
		       state_to_string(chan->state));

		l2cap_chan_hold(chan);
		l2cap_chan_lock(chan);

		__clear_chan_timer(chan);
		l2cap_chan_close(chan, ECONNRESET);
		l2cap_sock_kill(sk);

		l2cap_chan_unlock(chan);
		l2cap_chan_put(chan);
	}
}

static struct l2cap_chan *l2cap_sock_new_connection_cb(struct l2cap_chan *chan)
{
	struct sock *sk, *parent = chan->data;

	lock_sock(parent);

	/* Check for backlog size */
	if (sk_acceptq_is_full(parent)) {
		BT_DBG("backlog full %d", parent->sk_ack_backlog);
		release_sock(parent);
		return NULL;
	}

	sk = l2cap_sock_alloc(sock_net(parent), NULL, BTPROTO_L2CAP,
			      GFP_ATOMIC, 0);
	if (!sk) {
		release_sock(parent);
		return NULL;
        }

	bt_sock_reclassify_lock(sk, BTPROTO_L2CAP);

	l2cap_sock_init(sk, parent);

	bt_accept_enqueue(parent, sk, false);

	release_sock(parent);

	return l2cap_pi(sk)->chan;
}

static int l2cap_sock_recv_cb(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct sock *sk;
	struct l2cap_pinfo *pi;
	int err;

	sk = chan->data;
	if (!sk)
		return -ENXIO;

	pi = l2cap_pi(sk);
	lock_sock(sk);
	if (chan->mode == L2CAP_MODE_ERTM && !list_empty(&pi->rx_busy)) {
		err = -ENOMEM;
		goto done;
	}

	if (chan->mode != L2CAP_MODE_ERTM &&
	    chan->mode != L2CAP_MODE_STREAMING &&
	    chan->mode != L2CAP_MODE_LE_FLOWCTL &&
	    chan->mode != L2CAP_MODE_EXT_FLOWCTL) {
		/* Even if no filter is attached, we could potentially
		 * get errors from security modules, etc.
		 */
		err = sk_filter(sk, skb);
		if (err)
			goto done;
	}

	err = __sock_queue_rcv_skb(sk, skb);

	l2cap_publish_rx_avail(chan);

	/* For ERTM and LE, handle a skb that doesn't fit into the recv
	 * buffer.  This is important to do because the data frames
	 * have already been acked, so the skb cannot be discarded.
	 *
	 * Notify the l2cap core that the buffer is full, so the
	 * LOCAL_BUSY state is entered and no more frames are
	 * acked and reassembled until there is buffer space
	 * available.
	 */
	if (err < 0 &&
	    (chan->mode == L2CAP_MODE_ERTM ||
	     chan->mode == L2CAP_MODE_LE_FLOWCTL ||
	     chan->mode == L2CAP_MODE_EXT_FLOWCTL)) {
		struct l2cap_rx_busy *rx_busy =
			kmalloc(sizeof(*rx_busy), GFP_KERNEL);
		if (!rx_busy) {
			err = -ENOMEM;
			goto done;
		}
		rx_busy->skb = skb;
		list_add_tail(&rx_busy->list, &pi->rx_busy);
		l2cap_chan_busy(chan, 1);
		err = 0;
	}

done:
	release_sock(sk);

	return err;
}

static void l2cap_sock_close_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;

	if (!sk)
		return;

	l2cap_sock_kill(sk);
}

static void l2cap_sock_teardown_cb(struct l2cap_chan *chan, int err)
{
	struct sock *sk = chan->data;
	struct sock *parent;

	if (!sk)
		return;

	BT_DBG("chan %p state %s", chan, state_to_string(chan->state));

	/* This callback can be called both for server (BT_LISTEN)
	 * sockets as well as "normal" ones. To avoid lockdep warnings
	 * with child socket locking (through l2cap_sock_cleanup_listen)
	 * we need separation into separate nesting levels. The simplest
	 * way to accomplish this is to inherit the nesting level used
	 * for the channel.
	 */
	lock_sock_nested(sk, atomic_read(&chan->nesting));

	parent = bt_sk(sk)->parent;

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
			parent->sk_data_ready(parent);
		} else {
			sk->sk_state_change(sk);
		}

		break;
	}
	release_sock(sk);

	/* Only zap after cleanup to avoid use after free race */
	sock_set_flag(sk, SOCK_ZAPPED);

}

static void l2cap_sock_state_change_cb(struct l2cap_chan *chan, int state,
				       int err)
{
	struct sock *sk = chan->data;

	sk->sk_state = state;

	if (err)
		sk->sk_err = err;
}

static struct sk_buff *l2cap_sock_alloc_skb_cb(struct l2cap_chan *chan,
					       unsigned long hdr_len,
					       unsigned long len, int nb)
{
	struct sock *sk = chan->data;
	struct sk_buff *skb;
	int err;

	l2cap_chan_unlock(chan);
	skb = bt_skb_send_alloc(sk, hdr_len + len, nb, &err);
	l2cap_chan_lock(chan);

	if (!skb)
		return ERR_PTR(err);

	/* Channel lock is released before requesting new skb and then
	 * reacquired thus we need to recheck channel state.
	 */
	if (chan->state != BT_CONNECTED) {
		kfree_skb(skb);
		return ERR_PTR(-ENOTCONN);
	}

	skb->priority = READ_ONCE(sk->sk_priority);

	bt_cb(skb)->l2cap.chan = chan;

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
		parent->sk_data_ready(parent);

	release_sock(sk);
}

static void l2cap_sock_defer_cb(struct l2cap_chan *chan)
{
	struct sock *parent, *sk = chan->data;

	lock_sock(sk);

	parent = bt_sk(sk)->parent;
	if (parent)
		parent->sk_data_ready(parent);

	release_sock(sk);
}

static void l2cap_sock_resume_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;

	if (test_and_clear_bit(FLAG_PENDING_SECURITY, &chan->flags)) {
		sk->sk_state = BT_CONNECTED;
		chan->state = BT_CONNECTED;
	}

	clear_bit(BT_SK_SUSPEND, &bt_sk(sk)->flags);
	sk->sk_state_change(sk);
}

static void l2cap_sock_set_shutdown_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;

	lock_sock(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;
	release_sock(sk);
}

static long l2cap_sock_get_sndtimeo_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;

	return sk->sk_sndtimeo;
}

static struct pid *l2cap_sock_get_peer_pid_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;

	return sk->sk_peer_pid;
}

static void l2cap_sock_suspend_cb(struct l2cap_chan *chan)
{
	struct sock *sk = chan->data;

	set_bit(BT_SK_SUSPEND, &bt_sk(sk)->flags);
	sk->sk_state_change(sk);
}

static int l2cap_sock_filter(struct l2cap_chan *chan, struct sk_buff *skb)
{
	struct sock *sk = chan->data;

	switch (chan->mode) {
	case L2CAP_MODE_ERTM:
	case L2CAP_MODE_STREAMING:
		return sk_filter(sk, skb);
	}

	return 0;
}

static const struct l2cap_ops l2cap_chan_ops = {
	.name			= "L2CAP Socket Interface",
	.new_connection		= l2cap_sock_new_connection_cb,
	.recv			= l2cap_sock_recv_cb,
	.close			= l2cap_sock_close_cb,
	.teardown		= l2cap_sock_teardown_cb,
	.state_change		= l2cap_sock_state_change_cb,
	.ready			= l2cap_sock_ready_cb,
	.defer			= l2cap_sock_defer_cb,
	.resume			= l2cap_sock_resume_cb,
	.suspend		= l2cap_sock_suspend_cb,
	.set_shutdown		= l2cap_sock_set_shutdown_cb,
	.get_sndtimeo		= l2cap_sock_get_sndtimeo_cb,
	.get_peer_pid		= l2cap_sock_get_peer_pid_cb,
	.alloc_skb		= l2cap_sock_alloc_skb_cb,
	.filter			= l2cap_sock_filter,
};

static void l2cap_sock_destruct(struct sock *sk)
{
	struct l2cap_rx_busy *rx_busy, *next;

	BT_DBG("sk %p", sk);

	if (l2cap_pi(sk)->chan) {
		l2cap_pi(sk)->chan->data = NULL;
		l2cap_chan_put(l2cap_pi(sk)->chan);
	}

	list_for_each_entry_safe(rx_busy, next, &l2cap_pi(sk)->rx_busy, list) {
		kfree_skb(rx_busy->skb);
		list_del(&rx_busy->list);
		kfree(rx_busy);
	}

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
}

static void l2cap_skb_msg_name(struct sk_buff *skb, void *msg_name,
			       int *msg_namelen)
{
	DECLARE_SOCKADDR(struct sockaddr_l2 *, la, msg_name);

	memset(la, 0, sizeof(struct sockaddr_l2));
	la->l2_family = AF_BLUETOOTH;
	la->l2_psm = bt_cb(skb)->l2cap.psm;
	bacpy(&la->l2_bdaddr, &bt_cb(skb)->l2cap.bdaddr);

	*msg_namelen = sizeof(struct sockaddr_l2);
}

static void l2cap_sock_init(struct sock *sk, struct sock *parent)
{
	struct l2cap_chan *chan = l2cap_pi(sk)->chan;

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
		chan->tx_credits = pchan->tx_credits;
		chan->rx_credits = pchan->rx_credits;

		if (chan->chan_type == L2CAP_CHAN_FIXED) {
			chan->scid = pchan->scid;
			chan->dcid = pchan->scid;
		}

		security_sk_clone(parent, sk);
	} else {
		switch (sk->sk_type) {
		case SOCK_RAW:
			chan->chan_type = L2CAP_CHAN_RAW;
			break;
		case SOCK_DGRAM:
			chan->chan_type = L2CAP_CHAN_CONN_LESS;
			bt_sk(sk)->skb_msg_name = l2cap_skb_msg_name;
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

	l2cap_publish_rx_avail(chan);
}

static struct proto l2cap_proto = {
	.name		= "L2CAP",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct l2cap_pinfo)
};

static struct sock *l2cap_sock_alloc(struct net *net, struct socket *sock,
				     int proto, gfp_t prio, int kern)
{
	struct sock *sk;
	struct l2cap_chan *chan;

	sk = bt_sock_alloc(net, sock, &l2cap_proto, proto, prio, kern);
	if (!sk)
		return NULL;

	sk->sk_destruct = l2cap_sock_destruct;
	sk->sk_sndtimeo = L2CAP_CONN_TIMEOUT;

	INIT_LIST_HEAD(&l2cap_pi(sk)->rx_busy);

	chan = l2cap_chan_create();
	if (!chan) {
		sk_free(sk);
		sock->sk = NULL;
		return NULL;
	}

	l2cap_chan_hold(chan);

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

	sk = l2cap_sock_alloc(net, sock, protocol, GFP_ATOMIC, kern);
	if (!sk)
		return -ENOMEM;

	l2cap_sock_init(sk, NULL);
	bt_sock_link(&l2cap_sk_list, sk);
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
	.gettstamp	= sock_gettstamp,
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

	BUILD_BUG_ON(sizeof(struct sockaddr_l2) > sizeof(struct sockaddr));

	err = proto_register(&l2cap_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_L2CAP, &l2cap_sock_family_ops);
	if (err < 0) {
		BT_ERR("L2CAP socket registration failed");
		goto error;
	}

	err = bt_procfs_init(&init_net, "l2cap", &l2cap_sk_list,
			     NULL);
	if (err < 0) {
		BT_ERR("Failed to create L2CAP proc file");
		bt_sock_unregister(BTPROTO_L2CAP);
		goto error;
	}

	BT_INFO("L2CAP socket layer initialized");

	return 0;

error:
	proto_unregister(&l2cap_proto);
	return err;
}

void l2cap_cleanup_sockets(void)
{
	bt_procfs_cleanup(&init_net, "l2cap");
	bt_sock_unregister(BTPROTO_L2CAP);
	proto_unregister(&l2cap_proto);
}
