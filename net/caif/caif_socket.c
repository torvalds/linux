/*
 * Copyright (C) ST-Ericsson AB 2010
 * Author:	Sjur Brendeland
 * License terms: GNU General Public License (GPL) version 2
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s(): " fmt, __func__

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/tcp.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/caif/caif_socket.h>
#include <linux/pkt_sched.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/caif/caif_layer.h>
#include <net/caif/caif_dev.h>
#include <net/caif/cfpkt.h>

MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(AF_CAIF);

/*
 * CAIF state is re-using the TCP socket states.
 * caif_states stored in sk_state reflect the state as reported by
 * the CAIF stack, while sk_socket->state is the state of the socket.
 */
enum caif_states {
	CAIF_CONNECTED		= TCP_ESTABLISHED,
	CAIF_CONNECTING	= TCP_SYN_SENT,
	CAIF_DISCONNECTED	= TCP_CLOSE
};

#define TX_FLOW_ON_BIT	1
#define RX_FLOW_ON_BIT	2

struct caifsock {
	struct sock sk; /* must be first member */
	struct cflayer layer;
	u32 flow_state;
	struct caif_connect_request conn_req;
	struct mutex readlock;
	struct dentry *debugfs_socket_dir;
	int headroom, tailroom, maxframe;
};

static int rx_flow_is_on(struct caifsock *cf_sk)
{
	return test_bit(RX_FLOW_ON_BIT,
			(void *) &cf_sk->flow_state);
}

static int tx_flow_is_on(struct caifsock *cf_sk)
{
	return test_bit(TX_FLOW_ON_BIT,
			(void *) &cf_sk->flow_state);
}

static void set_rx_flow_off(struct caifsock *cf_sk)
{
	 clear_bit(RX_FLOW_ON_BIT,
		 (void *) &cf_sk->flow_state);
}

static void set_rx_flow_on(struct caifsock *cf_sk)
{
	 set_bit(RX_FLOW_ON_BIT,
			(void *) &cf_sk->flow_state);
}

static void set_tx_flow_off(struct caifsock *cf_sk)
{
	 clear_bit(TX_FLOW_ON_BIT,
		(void *) &cf_sk->flow_state);
}

static void set_tx_flow_on(struct caifsock *cf_sk)
{
	 set_bit(TX_FLOW_ON_BIT,
		(void *) &cf_sk->flow_state);
}

static void caif_read_lock(struct sock *sk)
{
	struct caifsock *cf_sk;
	cf_sk = container_of(sk, struct caifsock, sk);
	mutex_lock(&cf_sk->readlock);
}

static void caif_read_unlock(struct sock *sk)
{
	struct caifsock *cf_sk;
	cf_sk = container_of(sk, struct caifsock, sk);
	mutex_unlock(&cf_sk->readlock);
}

static int sk_rcvbuf_lowwater(struct caifsock *cf_sk)
{
	/* A quarter of full buffer is used a low water mark */
	return cf_sk->sk.sk_rcvbuf / 4;
}

static void caif_flow_ctrl(struct sock *sk, int mode)
{
	struct caifsock *cf_sk;
	cf_sk = container_of(sk, struct caifsock, sk);
	if (cf_sk->layer.dn && cf_sk->layer.dn->modemcmd)
		cf_sk->layer.dn->modemcmd(cf_sk->layer.dn, mode);
}

/*
 * Copied from sock.c:sock_queue_rcv_skb(), but changed so packets are
 * not dropped, but CAIF is sending flow off instead.
 */
static void caif_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	int err;
	unsigned long flags;
	struct sk_buff_head *list = &sk->sk_receive_queue;
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);
	bool queued = false;

	if (atomic_read(&sk->sk_rmem_alloc) + skb->truesize >=
		(unsigned int)sk->sk_rcvbuf && rx_flow_is_on(cf_sk)) {
		net_dbg_ratelimited("sending flow OFF (queue len = %d %d)\n",
				    atomic_read(&cf_sk->sk.sk_rmem_alloc),
				    sk_rcvbuf_lowwater(cf_sk));
		set_rx_flow_off(cf_sk);
		caif_flow_ctrl(sk, CAIF_MODEMCMD_FLOW_OFF_REQ);
	}

	err = sk_filter(sk, skb);
	if (err)
		goto out;

	if (!sk_rmem_schedule(sk, skb, skb->truesize) && rx_flow_is_on(cf_sk)) {
		set_rx_flow_off(cf_sk);
		net_dbg_ratelimited("sending flow OFF due to rmem_schedule\n");
		caif_flow_ctrl(sk, CAIF_MODEMCMD_FLOW_OFF_REQ);
	}
	skb->dev = NULL;
	skb_set_owner_r(skb, sk);
	spin_lock_irqsave(&list->lock, flags);
	queued = !sock_flag(sk, SOCK_DEAD);
	if (queued)
		__skb_queue_tail(list, skb);
	spin_unlock_irqrestore(&list->lock, flags);
out:
	if (queued)
		sk->sk_data_ready(sk);
	else
		kfree_skb(skb);
}

/* Packet Receive Callback function called from CAIF Stack */
static int caif_sktrecv_cb(struct cflayer *layr, struct cfpkt *pkt)
{
	struct caifsock *cf_sk;
	struct sk_buff *skb;

	cf_sk = container_of(layr, struct caifsock, layer);
	skb = cfpkt_tonative(pkt);

	if (unlikely(cf_sk->sk.sk_state != CAIF_CONNECTED)) {
		kfree_skb(skb);
		return 0;
	}
	caif_queue_rcv_skb(&cf_sk->sk, skb);
	return 0;
}

static void cfsk_hold(struct cflayer *layr)
{
	struct caifsock *cf_sk = container_of(layr, struct caifsock, layer);
	sock_hold(&cf_sk->sk);
}

static void cfsk_put(struct cflayer *layr)
{
	struct caifsock *cf_sk = container_of(layr, struct caifsock, layer);
	sock_put(&cf_sk->sk);
}

/* Packet Control Callback function called from CAIF */
static void caif_ctrl_cb(struct cflayer *layr,
			 enum caif_ctrlcmd flow,
			 int phyid)
{
	struct caifsock *cf_sk = container_of(layr, struct caifsock, layer);
	switch (flow) {
	case CAIF_CTRLCMD_FLOW_ON_IND:
		/* OK from modem to start sending again */
		set_tx_flow_on(cf_sk);
		cf_sk->sk.sk_state_change(&cf_sk->sk);
		break;

	case CAIF_CTRLCMD_FLOW_OFF_IND:
		/* Modem asks us to shut up */
		set_tx_flow_off(cf_sk);
		cf_sk->sk.sk_state_change(&cf_sk->sk);
		break;

	case CAIF_CTRLCMD_INIT_RSP:
		/* We're now connected */
		caif_client_register_refcnt(&cf_sk->layer,
						cfsk_hold, cfsk_put);
		cf_sk->sk.sk_state = CAIF_CONNECTED;
		set_tx_flow_on(cf_sk);
		cf_sk->sk.sk_shutdown = 0;
		cf_sk->sk.sk_state_change(&cf_sk->sk);
		break;

	case CAIF_CTRLCMD_DEINIT_RSP:
		/* We're now disconnected */
		cf_sk->sk.sk_state = CAIF_DISCONNECTED;
		cf_sk->sk.sk_state_change(&cf_sk->sk);
		break;

	case CAIF_CTRLCMD_INIT_FAIL_RSP:
		/* Connect request failed */
		cf_sk->sk.sk_err = ECONNREFUSED;
		cf_sk->sk.sk_state = CAIF_DISCONNECTED;
		cf_sk->sk.sk_shutdown = SHUTDOWN_MASK;
		/*
		 * Socket "standards" seems to require POLLOUT to
		 * be set at connect failure.
		 */
		set_tx_flow_on(cf_sk);
		cf_sk->sk.sk_state_change(&cf_sk->sk);
		break;

	case CAIF_CTRLCMD_REMOTE_SHUTDOWN_IND:
		/* Modem has closed this connection, or device is down. */
		cf_sk->sk.sk_shutdown = SHUTDOWN_MASK;
		cf_sk->sk.sk_err = ECONNRESET;
		set_rx_flow_on(cf_sk);
		cf_sk->sk.sk_error_report(&cf_sk->sk);
		break;

	default:
		pr_debug("Unexpected flow command %d\n", flow);
	}
}

static void caif_check_flow_release(struct sock *sk)
{
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);

	if (rx_flow_is_on(cf_sk))
		return;

	if (atomic_read(&sk->sk_rmem_alloc) <= sk_rcvbuf_lowwater(cf_sk)) {
			set_rx_flow_on(cf_sk);
			caif_flow_ctrl(sk, CAIF_MODEMCMD_FLOW_ON_REQ);
	}
}

/*
 * Copied from unix_dgram_recvmsg, but removed credit checks,
 * changed locking, address handling and added MSG_TRUNC.
 */
static int caif_seqpkt_recvmsg(struct socket *sock, struct msghdr *m,
			       size_t len, int flags)

{
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	int ret;
	int copylen;

	ret = -EOPNOTSUPP;
	if (flags & MSG_OOB)
		goto read_error;

	skb = skb_recv_datagram(sk, flags, 0 , &ret);
	if (!skb)
		goto read_error;
	copylen = skb->len;
	if (len < copylen) {
		m->msg_flags |= MSG_TRUNC;
		copylen = len;
	}

	ret = skb_copy_datagram_msg(skb, 0, m, copylen);
	if (ret)
		goto out_free;

	ret = (flags & MSG_TRUNC) ? skb->len : copylen;
out_free:
	skb_free_datagram(sk, skb);
	caif_check_flow_release(sk);
	return ret;

read_error:
	return ret;
}


/* Copied from unix_stream_wait_data, identical except for lock call. */
static long caif_stream_data_wait(struct sock *sk, long timeo)
{
	DEFINE_WAIT(wait);
	lock_sock(sk);

	for (;;) {
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

		if (!skb_queue_empty(&sk->sk_receive_queue) ||
			sk->sk_err ||
			sk->sk_state != CAIF_CONNECTED ||
			sock_flag(sk, SOCK_DEAD) ||
			(sk->sk_shutdown & RCV_SHUTDOWN) ||
			signal_pending(current) ||
			!timeo)
			break;

		sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock(sk);

		if (sock_flag(sk, SOCK_DEAD))
			break;

		sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	}

	finish_wait(sk_sleep(sk), &wait);
	release_sock(sk);
	return timeo;
}


/*
 * Copied from unix_stream_recvmsg, but removed credit checks,
 * changed locking calls, changed address handling.
 */
static int caif_stream_recvmsg(struct socket *sock, struct msghdr *msg,
			       size_t size, int flags)
{
	struct sock *sk = sock->sk;
	int copied = 0;
	int target;
	int err = 0;
	long timeo;

	err = -EOPNOTSUPP;
	if (flags&MSG_OOB)
		goto out;

	/*
	 * Lock the socket to prevent queue disordering
	 * while sleeps in memcpy_tomsg
	 */
	err = -EAGAIN;
	if (sk->sk_state == CAIF_CONNECTING)
		goto out;

	caif_read_lock(sk);
	target = sock_rcvlowat(sk, flags&MSG_WAITALL, size);
	timeo = sock_rcvtimeo(sk, flags&MSG_DONTWAIT);

	do {
		int chunk;
		struct sk_buff *skb;

		lock_sock(sk);
		if (sock_flag(sk, SOCK_DEAD)) {
			err = -ECONNRESET;
			goto unlock;
		}
		skb = skb_dequeue(&sk->sk_receive_queue);
		caif_check_flow_release(sk);

		if (skb == NULL) {
			if (copied >= target)
				goto unlock;
			/*
			 *	POSIX 1003.1g mandates this order.
			 */
			err = sock_error(sk);
			if (err)
				goto unlock;
			err = -ECONNRESET;
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				goto unlock;

			err = -EPIPE;
			if (sk->sk_state != CAIF_CONNECTED)
				goto unlock;
			if (sock_flag(sk, SOCK_DEAD))
				goto unlock;

			release_sock(sk);

			err = -EAGAIN;
			if (!timeo)
				break;

			caif_read_unlock(sk);

			timeo = caif_stream_data_wait(sk, timeo);

			if (signal_pending(current)) {
				err = sock_intr_errno(timeo);
				goto out;
			}
			caif_read_lock(sk);
			continue;
unlock:
			release_sock(sk);
			break;
		}
		release_sock(sk);
		chunk = min_t(unsigned int, skb->len, size);
		if (memcpy_to_msg(msg, skb->data, chunk)) {
			skb_queue_head(&sk->sk_receive_queue, skb);
			if (copied == 0)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size -= chunk;

		/* Mark read part of skb as used */
		if (!(flags & MSG_PEEK)) {
			skb_pull(skb, chunk);

			/* put the skb back if we didn't use it up. */
			if (skb->len) {
				skb_queue_head(&sk->sk_receive_queue, skb);
				break;
			}
			kfree_skb(skb);

		} else {
			/*
			 * It is questionable, see note in unix_dgram_recvmsg.
			 */
			/* put message back and return */
			skb_queue_head(&sk->sk_receive_queue, skb);
			break;
		}
	} while (size);
	caif_read_unlock(sk);

out:
	return copied ? : err;
}

/*
 * Copied from sock.c:sock_wait_for_wmem, but change to wait for
 * CAIF flow-on and sock_writable.
 */
static long caif_wait_for_flow_on(struct caifsock *cf_sk,
				  int wait_writeable, long timeo, int *err)
{
	struct sock *sk = &cf_sk->sk;
	DEFINE_WAIT(wait);
	for (;;) {
		*err = 0;
		if (tx_flow_is_on(cf_sk) &&
			(!wait_writeable || sock_writeable(&cf_sk->sk)))
			break;
		*err = -ETIMEDOUT;
		if (!timeo)
			break;
		*err = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		*err = -ECONNRESET;
		if (sk->sk_shutdown & SHUTDOWN_MASK)
			break;
		*err = -sk->sk_err;
		if (sk->sk_err)
			break;
		*err = -EPIPE;
		if (cf_sk->sk.sk_state != CAIF_CONNECTED)
			break;
		timeo = schedule_timeout(timeo);
	}
	finish_wait(sk_sleep(sk), &wait);
	return timeo;
}

/*
 * Transmit a SKB. The device may temporarily request re-transmission
 * by returning EAGAIN.
 */
static int transmit_skb(struct sk_buff *skb, struct caifsock *cf_sk,
			int noblock, long timeo)
{
	struct cfpkt *pkt;

	pkt = cfpkt_fromnative(CAIF_DIR_OUT, skb);
	memset(skb->cb, 0, sizeof(struct caif_payload_info));
	cfpkt_set_prio(pkt, cf_sk->sk.sk_priority);

	if (cf_sk->layer.dn == NULL) {
		kfree_skb(skb);
		return -EINVAL;
	}

	return cf_sk->layer.dn->transmit(cf_sk->layer.dn, pkt);
}

/* Copied from af_unix:unix_dgram_sendmsg, and adapted to CAIF */
static int caif_seqpkt_sendmsg(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	struct sock *sk = sock->sk;
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);
	int buffer_size;
	int ret = 0;
	struct sk_buff *skb = NULL;
	int noblock;
	long timeo;
	caif_assert(cf_sk);
	ret = sock_error(sk);
	if (ret)
		goto err;

	ret = -EOPNOTSUPP;
	if (msg->msg_flags&MSG_OOB)
		goto err;

	ret = -EOPNOTSUPP;
	if (msg->msg_namelen)
		goto err;

	ret = -EINVAL;
	if (unlikely(msg->msg_iter.iov->iov_base == NULL))
		goto err;
	noblock = msg->msg_flags & MSG_DONTWAIT;

	timeo = sock_sndtimeo(sk, noblock);
	timeo = caif_wait_for_flow_on(container_of(sk, struct caifsock, sk),
				1, timeo, &ret);

	if (ret)
		goto err;
	ret = -EPIPE;
	if (cf_sk->sk.sk_state != CAIF_CONNECTED ||
		sock_flag(sk, SOCK_DEAD) ||
		(sk->sk_shutdown & RCV_SHUTDOWN))
		goto err;

	/* Error if trying to write more than maximum frame size. */
	ret = -EMSGSIZE;
	if (len > cf_sk->maxframe && cf_sk->sk.sk_protocol != CAIFPROTO_RFM)
		goto err;

	buffer_size = len + cf_sk->headroom + cf_sk->tailroom;

	ret = -ENOMEM;
	skb = sock_alloc_send_skb(sk, buffer_size, noblock, &ret);

	if (!skb || skb_tailroom(skb) < buffer_size)
		goto err;

	skb_reserve(skb, cf_sk->headroom);

	ret = memcpy_from_msg(skb_put(skb, len), msg, len);

	if (ret)
		goto err;
	ret = transmit_skb(skb, cf_sk, noblock, timeo);
	if (ret < 0)
		/* skb is already freed */
		return ret;

	return len;
err:
	kfree_skb(skb);
	return ret;
}

/*
 * Copied from unix_stream_sendmsg and adapted to CAIF:
 * Changed removed permission handling and added waiting for flow on
 * and other minor adaptations.
 */
static int caif_stream_sendmsg(struct socket *sock, struct msghdr *msg,
			       size_t len)
{
	struct sock *sk = sock->sk;
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);
	int err, size;
	struct sk_buff *skb;
	int sent = 0;
	long timeo;

	err = -EOPNOTSUPP;
	if (unlikely(msg->msg_flags&MSG_OOB))
		goto out_err;

	if (unlikely(msg->msg_namelen))
		goto out_err;

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	timeo = caif_wait_for_flow_on(cf_sk, 1, timeo, &err);

	if (unlikely(sk->sk_shutdown & SEND_SHUTDOWN))
		goto pipe_err;

	while (sent < len) {

		size = len-sent;

		if (size > cf_sk->maxframe)
			size = cf_sk->maxframe;

		/* If size is more than half of sndbuf, chop up message */
		if (size > ((sk->sk_sndbuf >> 1) - 64))
			size = (sk->sk_sndbuf >> 1) - 64;

		if (size > SKB_MAX_ALLOC)
			size = SKB_MAX_ALLOC;

		skb = sock_alloc_send_skb(sk,
					size + cf_sk->headroom +
					cf_sk->tailroom,
					msg->msg_flags&MSG_DONTWAIT,
					&err);
		if (skb == NULL)
			goto out_err;

		skb_reserve(skb, cf_sk->headroom);
		/*
		 *	If you pass two values to the sock_alloc_send_skb
		 *	it tries to grab the large buffer with GFP_NOFS
		 *	(which can fail easily), and if it fails grab the
		 *	fallback size buffer which is under a page and will
		 *	succeed. [Alan]
		 */
		size = min_t(int, size, skb_tailroom(skb));

		err = memcpy_from_msg(skb_put(skb, size), msg, size);
		if (err) {
			kfree_skb(skb);
			goto out_err;
		}
		err = transmit_skb(skb, cf_sk,
				msg->msg_flags&MSG_DONTWAIT, timeo);
		if (err < 0)
			/* skb is already freed */
			goto pipe_err;

		sent += size;
	}

	return sent;

pipe_err:
	if (sent == 0 && !(msg->msg_flags&MSG_NOSIGNAL))
		send_sig(SIGPIPE, current, 0);
	err = -EPIPE;
out_err:
	return sent ? : err;
}

static int setsockopt(struct socket *sock,
		      int lvl, int opt, char __user *ov, unsigned int ol)
{
	struct sock *sk = sock->sk;
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);
	int linksel;

	if (cf_sk->sk.sk_socket->state != SS_UNCONNECTED)
		return -ENOPROTOOPT;

	switch (opt) {
	case CAIFSO_LINK_SELECT:
		if (ol < sizeof(int))
			return -EINVAL;
		if (lvl != SOL_CAIF)
			goto bad_sol;
		if (copy_from_user(&linksel, ov, sizeof(int)))
			return -EINVAL;
		lock_sock(&(cf_sk->sk));
		cf_sk->conn_req.link_selector = linksel;
		release_sock(&cf_sk->sk);
		return 0;

	case CAIFSO_REQ_PARAM:
		if (lvl != SOL_CAIF)
			goto bad_sol;
		if (cf_sk->sk.sk_protocol != CAIFPROTO_UTIL)
			return -ENOPROTOOPT;
		lock_sock(&(cf_sk->sk));
		if (ol > sizeof(cf_sk->conn_req.param.data) ||
			copy_from_user(&cf_sk->conn_req.param.data, ov, ol)) {
			release_sock(&cf_sk->sk);
			return -EINVAL;
		}
		cf_sk->conn_req.param.size = ol;
		release_sock(&cf_sk->sk);
		return 0;

	default:
		return -ENOPROTOOPT;
	}

	return 0;
bad_sol:
	return -ENOPROTOOPT;

}

/*
 * caif_connect() - Connect a CAIF Socket
 * Copied and modified af_irda.c:irda_connect().
 *
 * Note : by consulting "errno", the user space caller may learn the cause
 * of the failure. Most of them are visible in the function, others may come
 * from subroutines called and are listed here :
 *  o -EAFNOSUPPORT: bad socket family or type.
 *  o -ESOCKTNOSUPPORT: bad socket type or protocol
 *  o -EINVAL: bad socket address, or CAIF link type
 *  o -ECONNREFUSED: remote end refused the connection.
 *  o -EINPROGRESS: connect request sent but timed out (or non-blocking)
 *  o -EISCONN: already connected.
 *  o -ETIMEDOUT: Connection timed out (send timeout)
 *  o -ENODEV: No link layer to send request
 *  o -ECONNRESET: Received Shutdown indication or lost link layer
 *  o -ENOMEM: Out of memory
 *
 *  State Strategy:
 *  o sk_state: holds the CAIF_* protocol state, it's updated by
 *	caif_ctrl_cb.
 *  o sock->state: holds the SS_* socket state and is updated by connect and
 *	disconnect.
 */
static int caif_connect(struct socket *sock, struct sockaddr *uaddr,
			int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);
	long timeo;
	int err;
	int ifindex, headroom, tailroom;
	unsigned int mtu;
	struct net_device *dev;

	lock_sock(sk);

	err = -EINVAL;
	if (addr_len < offsetofend(struct sockaddr, sa_family))
		goto out;

	err = -EAFNOSUPPORT;
	if (uaddr->sa_family != AF_CAIF)
		goto out;

	switch (sock->state) {
	case SS_UNCONNECTED:
		/* Normal case, a fresh connect */
		caif_assert(sk->sk_state == CAIF_DISCONNECTED);
		break;
	case SS_CONNECTING:
		switch (sk->sk_state) {
		case CAIF_CONNECTED:
			sock->state = SS_CONNECTED;
			err = -EISCONN;
			goto out;
		case CAIF_DISCONNECTED:
			/* Reconnect allowed */
			break;
		case CAIF_CONNECTING:
			err = -EALREADY;
			if (flags & O_NONBLOCK)
				goto out;
			goto wait_connect;
		}
		break;
	case SS_CONNECTED:
		caif_assert(sk->sk_state == CAIF_CONNECTED ||
				sk->sk_state == CAIF_DISCONNECTED);
		if (sk->sk_shutdown & SHUTDOWN_MASK) {
			/* Allow re-connect after SHUTDOWN_IND */
			caif_disconnect_client(sock_net(sk), &cf_sk->layer);
			caif_free_client(&cf_sk->layer);
			break;
		}
		/* No reconnect on a seqpacket socket */
		err = -EISCONN;
		goto out;
	case SS_DISCONNECTING:
	case SS_FREE:
		caif_assert(1); /*Should never happen */
		break;
	}
	sk->sk_state = CAIF_DISCONNECTED;
	sock->state = SS_UNCONNECTED;
	sk_stream_kill_queues(&cf_sk->sk);

	err = -EINVAL;
	if (addr_len != sizeof(struct sockaddr_caif))
		goto out;

	memcpy(&cf_sk->conn_req.sockaddr, uaddr,
		sizeof(struct sockaddr_caif));

	/* Move to connecting socket, start sending Connect Requests */
	sock->state = SS_CONNECTING;
	sk->sk_state = CAIF_CONNECTING;

	/* Check priority value comming from socket */
	/* if priority value is out of range it will be ajusted */
	if (cf_sk->sk.sk_priority > CAIF_PRIO_MAX)
		cf_sk->conn_req.priority = CAIF_PRIO_MAX;
	else if (cf_sk->sk.sk_priority < CAIF_PRIO_MIN)
		cf_sk->conn_req.priority = CAIF_PRIO_MIN;
	else
		cf_sk->conn_req.priority = cf_sk->sk.sk_priority;

	/*ifindex = id of the interface.*/
	cf_sk->conn_req.ifindex = cf_sk->sk.sk_bound_dev_if;

	cf_sk->layer.receive = caif_sktrecv_cb;

	err = caif_connect_client(sock_net(sk), &cf_sk->conn_req,
				&cf_sk->layer, &ifindex, &headroom, &tailroom);

	if (err < 0) {
		cf_sk->sk.sk_socket->state = SS_UNCONNECTED;
		cf_sk->sk.sk_state = CAIF_DISCONNECTED;
		goto out;
	}

	err = -ENODEV;
	rcu_read_lock();
	dev = dev_get_by_index_rcu(sock_net(sk), ifindex);
	if (!dev) {
		rcu_read_unlock();
		goto out;
	}
	cf_sk->headroom = LL_RESERVED_SPACE_EXTRA(dev, headroom);
	mtu = dev->mtu;
	rcu_read_unlock();

	cf_sk->tailroom = tailroom;
	cf_sk->maxframe = mtu - (headroom + tailroom);
	if (cf_sk->maxframe < 1) {
		pr_warn("CAIF Interface MTU too small (%d)\n", dev->mtu);
		err = -ENODEV;
		goto out;
	}

	err = -EINPROGRESS;
wait_connect:

	if (sk->sk_state != CAIF_CONNECTED && (flags & O_NONBLOCK))
		goto out;

	timeo = sock_sndtimeo(sk, flags & O_NONBLOCK);

	release_sock(sk);
	err = -ERESTARTSYS;
	timeo = wait_event_interruptible_timeout(*sk_sleep(sk),
			sk->sk_state != CAIF_CONNECTING,
			timeo);
	lock_sock(sk);
	if (timeo < 0)
		goto out; /* -ERESTARTSYS */

	err = -ETIMEDOUT;
	if (timeo == 0 && sk->sk_state != CAIF_CONNECTED)
		goto out;
	if (sk->sk_state != CAIF_CONNECTED) {
		sock->state = SS_UNCONNECTED;
		err = sock_error(sk);
		if (!err)
			err = -ECONNREFUSED;
		goto out;
	}
	sock->state = SS_CONNECTED;
	err = 0;
out:
	release_sock(sk);
	return err;
}

/*
 * caif_release() - Disconnect a CAIF Socket
 * Copied and modified af_irda.c:irda_release().
 */
static int caif_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);

	if (!sk)
		return 0;

	set_tx_flow_off(cf_sk);

	/*
	 * Ensure that packets are not queued after this point in time.
	 * caif_queue_rcv_skb checks SOCK_DEAD holding the queue lock,
	 * this ensures no packets when sock is dead.
	 */
	spin_lock_bh(&sk->sk_receive_queue.lock);
	sock_set_flag(sk, SOCK_DEAD);
	spin_unlock_bh(&sk->sk_receive_queue.lock);
	sock->sk = NULL;

	WARN_ON(IS_ERR(cf_sk->debugfs_socket_dir));
	debugfs_remove_recursive(cf_sk->debugfs_socket_dir);

	lock_sock(&(cf_sk->sk));
	sk->sk_state = CAIF_DISCONNECTED;
	sk->sk_shutdown = SHUTDOWN_MASK;

	caif_disconnect_client(sock_net(sk), &cf_sk->layer);
	cf_sk->sk.sk_socket->state = SS_DISCONNECTING;
	wake_up_interruptible_poll(sk_sleep(sk), POLLERR|POLLHUP);

	sock_orphan(sk);
	sk_stream_kill_queues(&cf_sk->sk);
	release_sock(sk);
	sock_put(sk);
	return 0;
}

/* Copied from af_unix.c:unix_poll(), added CAIF tx_flow handling */
static __poll_t caif_poll(struct file *file,
			      struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	__poll_t mask;
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);

	sock_poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	/* exceptional events? */
	if (sk->sk_err)
		mask |= POLLERR;
	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP;

	/* readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue) ||
		(sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	/*
	 * we set writable also when the other side has shut down the
	 * connection. This prevents stuck sockets.
	 */
	if (sock_writeable(sk) && tx_flow_is_on(cf_sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}

static const struct proto_ops caif_seqpacket_ops = {
	.family = PF_CAIF,
	.owner = THIS_MODULE,
	.release = caif_release,
	.bind = sock_no_bind,
	.connect = caif_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = sock_no_getname,
	.poll = caif_poll,
	.ioctl = sock_no_ioctl,
	.listen = sock_no_listen,
	.shutdown = sock_no_shutdown,
	.setsockopt = setsockopt,
	.getsockopt = sock_no_getsockopt,
	.sendmsg = caif_seqpkt_sendmsg,
	.recvmsg = caif_seqpkt_recvmsg,
	.mmap = sock_no_mmap,
	.sendpage = sock_no_sendpage,
};

static const struct proto_ops caif_stream_ops = {
	.family = PF_CAIF,
	.owner = THIS_MODULE,
	.release = caif_release,
	.bind = sock_no_bind,
	.connect = caif_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = sock_no_getname,
	.poll = caif_poll,
	.ioctl = sock_no_ioctl,
	.listen = sock_no_listen,
	.shutdown = sock_no_shutdown,
	.setsockopt = setsockopt,
	.getsockopt = sock_no_getsockopt,
	.sendmsg = caif_stream_sendmsg,
	.recvmsg = caif_stream_recvmsg,
	.mmap = sock_no_mmap,
	.sendpage = sock_no_sendpage,
};

/* This function is called when a socket is finally destroyed. */
static void caif_sock_destructor(struct sock *sk)
{
	struct caifsock *cf_sk = container_of(sk, struct caifsock, sk);
	caif_assert(!refcount_read(&sk->sk_wmem_alloc));
	caif_assert(sk_unhashed(sk));
	caif_assert(!sk->sk_socket);
	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_debug("Attempt to release alive CAIF socket: %p\n", sk);
		return;
	}
	sk_stream_kill_queues(&cf_sk->sk);
	caif_free_client(&cf_sk->layer);
}

static int caif_create(struct net *net, struct socket *sock, int protocol,
		       int kern)
{
	struct sock *sk = NULL;
	struct caifsock *cf_sk = NULL;
	static struct proto prot = {.name = "PF_CAIF",
		.owner = THIS_MODULE,
		.obj_size = sizeof(struct caifsock),
	};

	if (!capable(CAP_SYS_ADMIN) && !capable(CAP_NET_ADMIN))
		return -EPERM;
	/*
	 * The sock->type specifies the socket type to use.
	 * The CAIF socket is a packet stream in the sense
	 * that it is packet based. CAIF trusts the reliability
	 * of the link, no resending is implemented.
	 */
	if (sock->type == SOCK_SEQPACKET)
		sock->ops = &caif_seqpacket_ops;
	else if (sock->type == SOCK_STREAM)
		sock->ops = &caif_stream_ops;
	else
		return -ESOCKTNOSUPPORT;

	if (protocol < 0 || protocol >= CAIFPROTO_MAX)
		return -EPROTONOSUPPORT;
	/*
	 * Set the socket state to unconnected.	 The socket state
	 * is really not used at all in the net/core or socket.c but the
	 * initialization makes sure that sock->state is not uninitialized.
	 */
	sk = sk_alloc(net, PF_CAIF, GFP_KERNEL, &prot, kern);
	if (!sk)
		return -ENOMEM;

	cf_sk = container_of(sk, struct caifsock, sk);

	/* Store the protocol */
	sk->sk_protocol = (unsigned char) protocol;

	/* Initialize default priority for well-known cases */
	switch (protocol) {
	case CAIFPROTO_AT:
		sk->sk_priority = TC_PRIO_CONTROL;
		break;
	case CAIFPROTO_RFM:
		sk->sk_priority = TC_PRIO_INTERACTIVE_BULK;
		break;
	default:
		sk->sk_priority = TC_PRIO_BESTEFFORT;
	}

	/*
	 * Lock in order to try to stop someone from opening the socket
	 * too early.
	 */
	lock_sock(&(cf_sk->sk));

	/* Initialize the nozero default sock structure data. */
	sock_init_data(sock, sk);
	sk->sk_destruct = caif_sock_destructor;

	mutex_init(&cf_sk->readlock); /* single task reading lock */
	cf_sk->layer.ctrlcmd = caif_ctrl_cb;
	cf_sk->sk.sk_socket->state = SS_UNCONNECTED;
	cf_sk->sk.sk_state = CAIF_DISCONNECTED;

	set_tx_flow_off(cf_sk);
	set_rx_flow_on(cf_sk);

	/* Set default options on configuration */
	cf_sk->conn_req.link_selector = CAIF_LINK_LOW_LATENCY;
	cf_sk->conn_req.protocol = protocol;
	release_sock(&cf_sk->sk);
	return 0;
}


static const struct net_proto_family caif_family_ops = {
	.family = PF_CAIF,
	.create = caif_create,
	.owner = THIS_MODULE,
};

static int __init caif_sktinit_module(void)
{
	return sock_register(&caif_family_ops);
}

static void __exit caif_sktexit_module(void)
{
	sock_unregister(PF_CAIF);
}
module_init(caif_sktinit_module);
module_exit(caif_sktexit_module);
