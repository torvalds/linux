/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

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

/* Bluetooth SCO sockets. */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/device.h>
#include <linux/list.h>
#include <net/sock.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/sco.h>

#ifndef CONFIG_BT_SCO_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#define VERSION "0.5"

static const struct proto_ops sco_sock_ops;

static struct bt_sock_list sco_sk_list = {
	.lock = RW_LOCK_UNLOCKED
};

static void __sco_chan_add(struct sco_conn *conn, struct sock *sk, struct sock *parent);
static void sco_chan_del(struct sock *sk, int err);

static int  sco_conn_del(struct hci_conn *conn, int err);

static void sco_sock_close(struct sock *sk);
static void sco_sock_kill(struct sock *sk);

/* ---- SCO timers ---- */
static void sco_sock_timeout(unsigned long arg)
{
	struct sock *sk = (struct sock *) arg;

	BT_DBG("sock %p state %d", sk, sk->sk_state);

	bh_lock_sock(sk);
	sk->sk_err = ETIMEDOUT;
	sk->sk_state_change(sk);
	bh_unlock_sock(sk);

	sco_sock_kill(sk);
	sock_put(sk);
}

static void sco_sock_set_timer(struct sock *sk, long timeout)
{
	BT_DBG("sock %p state %d timeout %ld", sk, sk->sk_state, timeout);
	sk_reset_timer(sk, &sk->sk_timer, jiffies + timeout);
}

static void sco_sock_clear_timer(struct sock *sk)
{
	BT_DBG("sock %p state %d", sk, sk->sk_state);
	sk_stop_timer(sk, &sk->sk_timer);
}

static void sco_sock_init_timer(struct sock *sk)
{
	init_timer(&sk->sk_timer);
	sk->sk_timer.function = sco_sock_timeout;
	sk->sk_timer.data = (unsigned long)sk;
}

/* ---- SCO connections ---- */
static struct sco_conn *sco_conn_add(struct hci_conn *hcon, __u8 status)
{
	struct hci_dev *hdev = hcon->hdev;
	struct sco_conn *conn;

	if ((conn = hcon->sco_data))
		return conn;

	if (status)
		return conn;

	if (!(conn = kmalloc(sizeof(struct sco_conn), GFP_ATOMIC)))
		return NULL;
	memset(conn, 0, sizeof(struct sco_conn));

	spin_lock_init(&conn->lock);

	hcon->sco_data = conn;
	conn->hcon = hcon;

	conn->src = &hdev->bdaddr;
	conn->dst = &hcon->dst;

	if (hdev->sco_mtu > 0)
		conn->mtu = hdev->sco_mtu;
	else
		conn->mtu = 60;

	BT_DBG("hcon %p conn %p", hcon, conn);
	return conn;
}

static inline struct sock *sco_chan_get(struct sco_conn *conn)
{
	struct sock *sk = NULL;
	sco_conn_lock(conn);
	sk = conn->sk;
	sco_conn_unlock(conn);
	return sk;
}

static int sco_conn_del(struct hci_conn *hcon, int err)
{
	struct sco_conn *conn;
	struct sock *sk;

	if (!(conn = hcon->sco_data)) 
		return 0;

	BT_DBG("hcon %p conn %p, err %d", hcon, conn, err);

	/* Kill socket */
	if ((sk = sco_chan_get(conn))) {
		bh_lock_sock(sk);
		sco_sock_clear_timer(sk);
		sco_chan_del(sk, err);
		bh_unlock_sock(sk);
		sco_sock_kill(sk);
	}

	hcon->sco_data = NULL;
	kfree(conn);
	return 0;
}

static inline int sco_chan_add(struct sco_conn *conn, struct sock *sk, struct sock *parent)
{
	int err = 0;

	sco_conn_lock(conn);
	if (conn->sk) {
		err = -EBUSY;
	} else {
		__sco_chan_add(conn, sk, parent);
	}
	sco_conn_unlock(conn);
	return err;
}

static int sco_connect(struct sock *sk)
{
	bdaddr_t *src = &bt_sk(sk)->src;
	bdaddr_t *dst = &bt_sk(sk)->dst;
	struct sco_conn *conn;
	struct hci_conn *hcon;
	struct hci_dev  *hdev;
	int err = 0;

	BT_DBG("%s -> %s", batostr(src), batostr(dst));

	if (!(hdev = hci_get_route(dst, src)))
		return -EHOSTUNREACH;

	hci_dev_lock_bh(hdev);

	err = -ENOMEM;

	hcon = hci_connect(hdev, SCO_LINK, dst);
	if (!hcon)
		goto done;

	conn = sco_conn_add(hcon, 0);
	if (!conn) {
		hci_conn_put(hcon);
		goto done;
	}

	/* Update source addr of the socket */
	bacpy(src, conn->src);

	err = sco_chan_add(conn, sk, NULL);
	if (err)
		goto done;

	if (hcon->state == BT_CONNECTED) {
		sco_sock_clear_timer(sk);
		sk->sk_state = BT_CONNECTED;
	} else {
		sk->sk_state = BT_CONNECT;
		sco_sock_set_timer(sk, sk->sk_sndtimeo);
	}
done:
	hci_dev_unlock_bh(hdev);
	hci_dev_put(hdev);
	return err;
}

static inline int sco_send_frame(struct sock *sk, struct msghdr *msg, int len)
{
	struct sco_conn *conn = sco_pi(sk)->conn;
	struct sk_buff *skb;
	int err, count;

	/* Check outgoing MTU */
	if (len > conn->mtu)
		return -EINVAL;

	BT_DBG("sk %p len %d", sk, len);

	count = min_t(unsigned int, conn->mtu, len);
	if (!(skb = bt_skb_send_alloc(sk, count, msg->msg_flags & MSG_DONTWAIT, &err)))
		return err;

	if (memcpy_fromiovec(skb_put(skb, count), msg->msg_iov, count)) {
		err = -EFAULT;
		goto fail;
	}

	if ((err = hci_send_sco(conn->hcon, skb)) < 0)
		goto fail;

	return count;

fail:
	kfree_skb(skb);
	return err;
}

static inline void sco_recv_frame(struct sco_conn *conn, struct sk_buff *skb)
{
	struct sock *sk = sco_chan_get(conn);

	if (!sk)
		goto drop;

	BT_DBG("sk %p len %d", sk, skb->len);

	if (sk->sk_state != BT_CONNECTED)
		goto drop;

	if (!sock_queue_rcv_skb(sk, skb))
		return;

drop:
	kfree_skb(skb);
	return;
}

/* -------- Socket interface ---------- */
static struct sock *__sco_get_sock_by_addr(bdaddr_t *ba)
{
	struct sock *sk;
	struct hlist_node *node;

	sk_for_each(sk, node, &sco_sk_list.head)
		if (!bacmp(&bt_sk(sk)->src, ba))
			goto found;
	sk = NULL;
found:
	return sk;
}

/* Find socket listening on source bdaddr.
 * Returns closest match.
 */
static struct sock *sco_get_sock_listen(bdaddr_t *src)
{
	struct sock *sk = NULL, *sk1 = NULL;
	struct hlist_node *node;

	read_lock(&sco_sk_list.lock);

	sk_for_each(sk, node, &sco_sk_list.head) {
		if (sk->sk_state != BT_LISTEN)
			continue;

		/* Exact match. */
		if (!bacmp(&bt_sk(sk)->src, src))
			break;

		/* Closest match */
		if (!bacmp(&bt_sk(sk)->src, BDADDR_ANY))
			sk1 = sk;
	}

	read_unlock(&sco_sk_list.lock);

	return node ? sk : sk1;
}

static void sco_sock_destruct(struct sock *sk)
{
	BT_DBG("sk %p", sk);

	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
}

static void sco_sock_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	BT_DBG("parent %p", parent);

	/* Close not yet accepted channels */
	while ((sk = bt_accept_dequeue(parent, NULL))) {
		sco_sock_close(sk);
		sco_sock_kill(sk);
	}

	parent->sk_state  = BT_CLOSED;
	sock_set_flag(parent, SOCK_ZAPPED);
}

/* Kill socket (only if zapped and orphan)
 * Must be called on unlocked socket.
 */
static void sco_sock_kill(struct sock *sk)
{
	if (!sock_flag(sk, SOCK_ZAPPED) || sk->sk_socket)
		return;

	BT_DBG("sk %p state %d", sk, sk->sk_state);

	/* Kill poor orphan */
	bt_sock_unlink(&sco_sk_list, sk);
	sock_set_flag(sk, SOCK_DEAD);
	sock_put(sk);
}

/* Close socket.
 * Must be called on unlocked socket.
 */
static void sco_sock_close(struct sock *sk)
{
	struct sco_conn *conn;

	sco_sock_clear_timer(sk);

	lock_sock(sk);

	conn = sco_pi(sk)->conn;

	BT_DBG("sk %p state %d conn %p socket %p", sk, sk->sk_state, conn, sk->sk_socket);

	switch (sk->sk_state) {
	case BT_LISTEN:
		sco_sock_cleanup_listen(sk);
		break;

	case BT_CONNECTED:
	case BT_CONFIG:
	case BT_CONNECT:
	case BT_DISCONN:
		sco_chan_del(sk, ECONNRESET);
		break;

	default:
		sock_set_flag(sk, SOCK_ZAPPED);
		break;
	};

	release_sock(sk);

	sco_sock_kill(sk);
}

static void sco_sock_init(struct sock *sk, struct sock *parent)
{
	BT_DBG("sk %p", sk);

	if (parent) 
		sk->sk_type = parent->sk_type;
}

static struct proto sco_proto = {
	.name		= "SCO",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct sco_pinfo)
};

static struct sock *sco_sock_alloc(struct socket *sock, int proto, gfp_t prio)
{
	struct sock *sk;

	sk = sk_alloc(PF_BLUETOOTH, prio, &sco_proto, 1);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&bt_sk(sk)->accept_q);

	sk->sk_destruct = sco_sock_destruct;
	sk->sk_sndtimeo = SCO_CONN_TIMEOUT;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state    = BT_OPEN;

	sco_sock_init_timer(sk);

	bt_sock_link(&sco_sk_list, sk);
	return sk;
}

static int sco_sock_create(struct socket *sock, int protocol)
{
	struct sock *sk;

	BT_DBG("sock %p", sock);

	sock->state = SS_UNCONNECTED;

	if (sock->type != SOCK_SEQPACKET)
		return -ESOCKTNOSUPPORT;

	sock->ops = &sco_sock_ops;

	if (!(sk = sco_sock_alloc(sock, protocol, GFP_KERNEL)))
		return -ENOMEM;

	sco_sock_init(sk, NULL);
	return 0;
}

static int sco_sock_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct sockaddr_sco *sa = (struct sockaddr_sco *) addr;
	struct sock *sk = sock->sk;
	bdaddr_t *src = &sa->sco_bdaddr;
	int err = 0;

	BT_DBG("sk %p %s", sk, batostr(&sa->sco_bdaddr));

	if (!addr || addr->sa_family != AF_BLUETOOTH)
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_state != BT_OPEN) {
		err = -EBADFD;
		goto done;
	}

	write_lock_bh(&sco_sk_list.lock);

	if (bacmp(src, BDADDR_ANY) && __sco_get_sock_by_addr(src)) {
		err = -EADDRINUSE;
	} else {
		/* Save source address */
		bacpy(&bt_sk(sk)->src, &sa->sco_bdaddr);
		sk->sk_state = BT_BOUND;
	}

	write_unlock_bh(&sco_sk_list.lock);

done:
	release_sock(sk);
	return err;
}

static int sco_sock_connect(struct socket *sock, struct sockaddr *addr, int alen, int flags)
{
	struct sockaddr_sco *sa = (struct sockaddr_sco *) addr;
	struct sock *sk = sock->sk;
	int err = 0;


	BT_DBG("sk %p", sk);

	if (addr->sa_family != AF_BLUETOOTH || alen < sizeof(struct sockaddr_sco))
		return -EINVAL;

	if (sk->sk_state != BT_OPEN && sk->sk_state != BT_BOUND)
		return -EBADFD;

	if (sk->sk_type != SOCK_SEQPACKET)
		return -EINVAL;

	lock_sock(sk);

	/* Set destination address and psm */
	bacpy(&bt_sk(sk)->dst, &sa->sco_bdaddr);

	if ((err = sco_connect(sk)))
		goto done;

	err = bt_sock_wait_state(sk, BT_CONNECTED, 
			sock_sndtimeo(sk, flags & O_NONBLOCK));

done:
	release_sock(sk);
	return err;
}

static int sco_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p backlog %d", sk, backlog);

	lock_sock(sk);

	if (sk->sk_state != BT_BOUND || sock->type != SOCK_SEQPACKET) {
		err = -EBADFD;
		goto done;
	}

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;
	sk->sk_state = BT_LISTEN;

done:
	release_sock(sk);
	return err;
}

static int sco_sock_accept(struct socket *sock, struct socket *newsock, int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sock *sk = sock->sk, *ch;
	long timeo;
	int err = 0;

	lock_sock(sk);

	if (sk->sk_state != BT_LISTEN) {
		err = -EBADFD;
		goto done;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	BT_DBG("sk %p timeo %ld", sk, timeo);

	/* Wait for an incoming connection. (wake-one). */
	add_wait_queue_exclusive(sk->sk_sleep, &wait);
	while (!(ch = bt_accept_dequeue(sk, newsock))) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

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
	remove_wait_queue(sk->sk_sleep, &wait);

	if (err)
		goto done;

	newsock->state = SS_CONNECTED;

	BT_DBG("new socket %p", ch);

done:
	release_sock(sk);
	return err;
}

static int sco_sock_getname(struct socket *sock, struct sockaddr *addr, int *len, int peer)
{
	struct sockaddr_sco *sa = (struct sockaddr_sco *) addr;
	struct sock *sk = sock->sk;

	BT_DBG("sock %p, sk %p", sock, sk);

	addr->sa_family = AF_BLUETOOTH;
	*len = sizeof(struct sockaddr_sco);

	if (peer)
		bacpy(&sa->sco_bdaddr, &bt_sk(sk)->dst);
	else
		bacpy(&sa->sco_bdaddr, &bt_sk(sk)->src);

	return 0;
}

static int sco_sock_sendmsg(struct kiocb *iocb, struct socket *sock, 
			    struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	err = sock_error(sk);
	if (err)
		return err;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	lock_sock(sk);

	if (sk->sk_state == BT_CONNECTED)
		err = sco_send_frame(sk, msg, len);
	else
		err = -ENOTCONN;

	release_sock(sk);
	return err;
}

static int sco_sock_setsockopt(struct socket *sock, int level, int optname, char __user *optval, int optlen)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sk %p", sk);

	lock_sock(sk);

	switch (optname) {
	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int sco_sock_getsockopt(struct socket *sock, int level, int optname, char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct sco_options opts;
	struct sco_conninfo cinfo;
	int len, err = 0; 

	BT_DBG("sk %p", sk);

	if (get_user(len, optlen))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	case SCO_OPTIONS:
		if (sk->sk_state != BT_CONNECTED) {
			err = -ENOTCONN;
			break;
		}

		opts.mtu = sco_pi(sk)->conn->mtu;

		BT_DBG("mtu %d", opts.mtu);

		len = min_t(unsigned int, len, sizeof(opts));
		if (copy_to_user(optval, (char *)&opts, len))
			err = -EFAULT;

		break;

	case SCO_CONNINFO:
		if (sk->sk_state != BT_CONNECTED) {
			err = -ENOTCONN;
			break;
		}

		cinfo.hci_handle = sco_pi(sk)->conn->hcon->handle;
		memcpy(cinfo.dev_class, sco_pi(sk)->conn->hcon->dev_class, 3);

		len = min_t(unsigned int, len, sizeof(cinfo));
		if (copy_to_user(optval, (char *)&cinfo, len))
			err = -EFAULT;

		break;

	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);
	return err;
}

static int sco_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int err = 0;

	BT_DBG("sock %p, sk %p", sock, sk);

	if (!sk)
		return 0;

	sco_sock_close(sk);

	if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime) {
		lock_sock(sk);
		err = bt_sock_wait_state(sk, BT_CLOSED, sk->sk_lingertime);
		release_sock(sk);
	}

	sock_orphan(sk);
	sco_sock_kill(sk);
	return err;
}

static void __sco_chan_add(struct sco_conn *conn, struct sock *sk, struct sock *parent)
{
	BT_DBG("conn %p", conn);

	sco_pi(sk)->conn = conn;
	conn->sk = sk;

	if (parent)
		bt_accept_enqueue(parent, sk);
}

/* Delete channel. 
 * Must be called on the locked socket. */
static void sco_chan_del(struct sock *sk, int err)
{
	struct sco_conn *conn;

	conn = sco_pi(sk)->conn;

	BT_DBG("sk %p, conn %p, err %d", sk, conn, err);

	if (conn) { 
		sco_conn_lock(conn);
		conn->sk = NULL;
		sco_pi(sk)->conn = NULL;
		sco_conn_unlock(conn);
		hci_conn_put(conn->hcon);
	}

	sk->sk_state = BT_CLOSED;
	sk->sk_err   = err;
	sk->sk_state_change(sk);

	sock_set_flag(sk, SOCK_ZAPPED);
}

static void sco_conn_ready(struct sco_conn *conn)
{
	struct sock *parent, *sk;

	BT_DBG("conn %p", conn);

	sco_conn_lock(conn);

	if ((sk = conn->sk)) {
		sco_sock_clear_timer(sk);
		bh_lock_sock(sk);
		sk->sk_state = BT_CONNECTED;
		sk->sk_state_change(sk);
		bh_unlock_sock(sk);
	} else {
		parent = sco_get_sock_listen(conn->src);
		if (!parent)
			goto done;

		bh_lock_sock(parent);

		sk = sco_sock_alloc(NULL, BTPROTO_SCO, GFP_ATOMIC);
		if (!sk) {
			bh_unlock_sock(parent);
			goto done;
		}

		sco_sock_init(sk, parent);

		bacpy(&bt_sk(sk)->src, conn->src);
		bacpy(&bt_sk(sk)->dst, conn->dst);

		hci_conn_hold(conn->hcon);
		__sco_chan_add(conn, sk, parent);

		sk->sk_state = BT_CONNECTED;

		/* Wake up parent */
		parent->sk_data_ready(parent, 1);

		bh_unlock_sock(parent);
	}

done:
	sco_conn_unlock(conn);
}

/* ----- SCO interface with lower layer (HCI) ----- */
static int sco_connect_ind(struct hci_dev *hdev, bdaddr_t *bdaddr, __u8 type)
{
	BT_DBG("hdev %s, bdaddr %s", hdev->name, batostr(bdaddr));

	/* Always accept connection */
	return HCI_LM_ACCEPT;
}

static int sco_connect_cfm(struct hci_conn *hcon, __u8 status)
{
	BT_DBG("hcon %p bdaddr %s status %d", hcon, batostr(&hcon->dst), status);

	if (hcon->type != SCO_LINK)
		return 0;

	if (!status) {
		struct sco_conn *conn;

		conn = sco_conn_add(hcon, status);
		if (conn)
			sco_conn_ready(conn);
	} else 
		sco_conn_del(hcon, bt_err(status));

	return 0;
}

static int sco_disconn_ind(struct hci_conn *hcon, __u8 reason)
{
	BT_DBG("hcon %p reason %d", hcon, reason);

	if (hcon->type != SCO_LINK)
		return 0;

	sco_conn_del(hcon, bt_err(reason));
	return 0;
}

static int sco_recv_scodata(struct hci_conn *hcon, struct sk_buff *skb)
{
	struct sco_conn *conn = hcon->sco_data;

	if (!conn)
		goto drop;

	BT_DBG("conn %p len %d", conn, skb->len);

	if (skb->len) {
		sco_recv_frame(conn, skb);
		return 0;
	}

drop:
	kfree_skb(skb);	
	return 0;
}

static ssize_t sco_sysfs_show(struct class *dev, char *buf)
{
	struct sock *sk;
	struct hlist_node *node;
	char *str = buf;

	read_lock_bh(&sco_sk_list.lock);

	sk_for_each(sk, node, &sco_sk_list.head) {
		str += sprintf(str, "%s %s %d\n",
				batostr(&bt_sk(sk)->src), batostr(&bt_sk(sk)->dst),
				sk->sk_state);
	}

	read_unlock_bh(&sco_sk_list.lock);

	return (str - buf);
}

static CLASS_ATTR(sco, S_IRUGO, sco_sysfs_show, NULL);

static const struct proto_ops sco_sock_ops = {
	.family		= PF_BLUETOOTH,
	.owner		= THIS_MODULE,
	.release	= sco_sock_release,
	.bind		= sco_sock_bind,
	.connect	= sco_sock_connect,
	.listen		= sco_sock_listen,
	.accept		= sco_sock_accept,
	.getname	= sco_sock_getname,
	.sendmsg	= sco_sock_sendmsg,
	.recvmsg	= bt_sock_recvmsg,
	.poll		= bt_sock_poll,
	.ioctl		= sock_no_ioctl,
	.mmap		= sock_no_mmap,
	.socketpair	= sock_no_socketpair,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sco_sock_setsockopt,
	.getsockopt	= sco_sock_getsockopt
};

static struct net_proto_family sco_sock_family_ops = {
	.family	= PF_BLUETOOTH,
	.owner	= THIS_MODULE,
	.create	= sco_sock_create,
};

static struct hci_proto sco_hci_proto = {
	.name		= "SCO",
	.id		= HCI_PROTO_SCO,
	.connect_ind	= sco_connect_ind,
	.connect_cfm	= sco_connect_cfm,
	.disconn_ind	= sco_disconn_ind,
	.recv_scodata	= sco_recv_scodata
};

static int __init sco_init(void)
{
	int err;

	err = proto_register(&sco_proto, 0);
	if (err < 0)
		return err;

	err = bt_sock_register(BTPROTO_SCO, &sco_sock_family_ops);
	if (err < 0) {
		BT_ERR("SCO socket registration failed");
		goto error;
	}

	err = hci_register_proto(&sco_hci_proto);
	if (err < 0) {
		BT_ERR("SCO protocol registration failed");
		bt_sock_unregister(BTPROTO_SCO);
		goto error;
	}

	class_create_file(&bt_class, &class_attr_sco);

	BT_INFO("SCO (Voice Link) ver %s", VERSION);
	BT_INFO("SCO socket layer initialized");

	return 0;

error:
	proto_unregister(&sco_proto);
	return err;
}

static void __exit sco_exit(void)
{
	class_remove_file(&bt_class, &class_attr_sco);

	if (bt_sock_unregister(BTPROTO_SCO) < 0)
		BT_ERR("SCO socket unregistration failed");

	if (hci_unregister_proto(&sco_hci_proto) < 0)
		BT_ERR("SCO protocol unregistration failed");

	proto_unregister(&sco_proto);
}

module_init(sco_init);
module_exit(sco_exit);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>, Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth SCO ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bt-proto-2");
