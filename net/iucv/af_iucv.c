/*
 *  linux/net/iucv/af_iucv.c
 *
 *  IUCV protocol stack for Linux on zSeries
 *
 *  Copyright 2006 IBM Corporation
 *
 *  Author(s):	Jennifer Hunt <jenhunt@us.ibm.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <net/sock.h>
#include <asm/ebcdic.h>
#include <asm/cpcmd.h>
#include <linux/kmod.h>

#include <net/iucv/iucv.h>
#include <net/iucv/af_iucv.h>

#define CONFIG_IUCV_SOCK_DEBUG 1

#define IPRMDATA 0x80
#define VERSION "1.0"

static char iucv_userid[80];

static struct proto_ops iucv_sock_ops;

static struct proto iucv_proto = {
	.name		= "AF_IUCV",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct iucv_sock),
};

static void iucv_sock_kill(struct sock *sk);
static void iucv_sock_close(struct sock *sk);

/* Call Back functions */
static void iucv_callback_rx(struct iucv_path *, struct iucv_message *);
static void iucv_callback_txdone(struct iucv_path *, struct iucv_message *);
static void iucv_callback_connack(struct iucv_path *, u8 ipuser[16]);
static int iucv_callback_connreq(struct iucv_path *, u8 ipvmid[8],
				 u8 ipuser[16]);
static void iucv_callback_connrej(struct iucv_path *, u8 ipuser[16]);

static struct iucv_sock_list iucv_sk_list = {
	.lock = RW_LOCK_UNLOCKED,
	.autobind_name = ATOMIC_INIT(0)
};

static struct iucv_handler af_iucv_handler = {
	.path_pending	  = iucv_callback_connreq,
	.path_complete	  = iucv_callback_connack,
	.path_severed	  = iucv_callback_connrej,
	.message_pending  = iucv_callback_rx,
	.message_complete = iucv_callback_txdone
};

static inline void high_nmcpy(unsigned char *dst, char *src)
{
       memcpy(dst, src, 8);
}

static inline void low_nmcpy(unsigned char *dst, char *src)
{
       memcpy(&dst[8], src, 8);
}

/* Timers */
static void iucv_sock_timeout(unsigned long arg)
{
	struct sock *sk = (struct sock *)arg;

	bh_lock_sock(sk);
	sk->sk_err = ETIMEDOUT;
	sk->sk_state_change(sk);
	bh_unlock_sock(sk);

	iucv_sock_kill(sk);
	sock_put(sk);
}

static void iucv_sock_clear_timer(struct sock *sk)
{
	sk_stop_timer(sk, &sk->sk_timer);
}

static struct sock *__iucv_get_sock_by_name(char *nm)
{
	struct sock *sk;
	struct hlist_node *node;

	sk_for_each(sk, node, &iucv_sk_list.head)
		if (!memcmp(&iucv_sk(sk)->src_name, nm, 8))
			return sk;

	return NULL;
}

static void iucv_sock_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_write_queue);
}

/* Cleanup Listen */
static void iucv_sock_cleanup_listen(struct sock *parent)
{
	struct sock *sk;

	/* Close non-accepted connections */
	while ((sk = iucv_accept_dequeue(parent, NULL))) {
		iucv_sock_close(sk);
		iucv_sock_kill(sk);
	}

	parent->sk_state = IUCV_CLOSED;
	sock_set_flag(parent, SOCK_ZAPPED);
}

/* Kill socket */
static void iucv_sock_kill(struct sock *sk)
{
	if (!sock_flag(sk, SOCK_ZAPPED) || sk->sk_socket)
		return;

	iucv_sock_unlink(&iucv_sk_list, sk);
	sock_set_flag(sk, SOCK_DEAD);
	sock_put(sk);
}

/* Close an IUCV socket */
static void iucv_sock_close(struct sock *sk)
{
	unsigned char user_data[16];
	struct iucv_sock *iucv = iucv_sk(sk);
	int err;
	unsigned long timeo;

	iucv_sock_clear_timer(sk);
	lock_sock(sk);

	switch (sk->sk_state) {
	case IUCV_LISTEN:
		iucv_sock_cleanup_listen(sk);
		break;

	case IUCV_CONNECTED:
	case IUCV_DISCONN:
		err = 0;

		sk->sk_state = IUCV_CLOSING;
		sk->sk_state_change(sk);

		if (!skb_queue_empty(&iucv->send_skb_q)) {
			if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime)
				timeo = sk->sk_lingertime;
			else
				timeo = IUCV_DISCONN_TIMEOUT;
			err = iucv_sock_wait_state(sk, IUCV_CLOSED, 0, timeo);
		}

		sk->sk_state = IUCV_CLOSED;
		sk->sk_state_change(sk);

		if (iucv->path) {
			low_nmcpy(user_data, iucv->src_name);
			high_nmcpy(user_data, iucv->dst_name);
			ASCEBC(user_data, sizeof(user_data));
			err = iucv_path_sever(iucv->path, user_data);
			iucv_path_free(iucv->path);
			iucv->path = NULL;
		}

		sk->sk_err = ECONNRESET;
		sk->sk_state_change(sk);

		skb_queue_purge(&iucv->send_skb_q);
		skb_queue_purge(&iucv->backlog_skb_q);

		sock_set_flag(sk, SOCK_ZAPPED);
		break;

	default:
		sock_set_flag(sk, SOCK_ZAPPED);
		break;
	}

	release_sock(sk);
	iucv_sock_kill(sk);
}

static void iucv_sock_init(struct sock *sk, struct sock *parent)
{
	if (parent)
		sk->sk_type = parent->sk_type;
}

static struct sock *iucv_sock_alloc(struct socket *sock, int proto, gfp_t prio)
{
	struct sock *sk;

	sk = sk_alloc(&init_net, PF_IUCV, prio, &iucv_proto);
	if (!sk)
		return NULL;

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&iucv_sk(sk)->accept_q);
	spin_lock_init(&iucv_sk(sk)->accept_q_lock);
	skb_queue_head_init(&iucv_sk(sk)->send_skb_q);
	INIT_LIST_HEAD(&iucv_sk(sk)->message_q.list);
	spin_lock_init(&iucv_sk(sk)->message_q.lock);
	skb_queue_head_init(&iucv_sk(sk)->backlog_skb_q);
	iucv_sk(sk)->send_tag = 0;

	sk->sk_destruct = iucv_sock_destruct;
	sk->sk_sndtimeo = IUCV_CONN_TIMEOUT;
	sk->sk_allocation = GFP_DMA;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state	= IUCV_OPEN;

	setup_timer(&sk->sk_timer, iucv_sock_timeout, (unsigned long)sk);

	iucv_sock_link(&iucv_sk_list, sk);
	return sk;
}

/* Create an IUCV socket */
static int iucv_sock_create(struct net *net, struct socket *sock, int protocol)
{
	struct sock *sk;

	if (sock->type != SOCK_STREAM)
		return -ESOCKTNOSUPPORT;

	sock->state = SS_UNCONNECTED;
	sock->ops = &iucv_sock_ops;

	sk = iucv_sock_alloc(sock, protocol, GFP_KERNEL);
	if (!sk)
		return -ENOMEM;

	iucv_sock_init(sk, NULL);

	return 0;
}

void iucv_sock_link(struct iucv_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock_bh(&l->lock);
}

void iucv_sock_unlink(struct iucv_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_del_node_init(sk);
	write_unlock_bh(&l->lock);
}

void iucv_accept_enqueue(struct sock *parent, struct sock *sk)
{
	unsigned long flags;
	struct iucv_sock *par = iucv_sk(parent);

	sock_hold(sk);
	spin_lock_irqsave(&par->accept_q_lock, flags);
	list_add_tail(&iucv_sk(sk)->accept_q, &par->accept_q);
	spin_unlock_irqrestore(&par->accept_q_lock, flags);
	iucv_sk(sk)->parent = parent;
	parent->sk_ack_backlog++;
}

void iucv_accept_unlink(struct sock *sk)
{
	unsigned long flags;
	struct iucv_sock *par = iucv_sk(iucv_sk(sk)->parent);

	spin_lock_irqsave(&par->accept_q_lock, flags);
	list_del_init(&iucv_sk(sk)->accept_q);
	spin_unlock_irqrestore(&par->accept_q_lock, flags);
	iucv_sk(sk)->parent->sk_ack_backlog--;
	iucv_sk(sk)->parent = NULL;
	sock_put(sk);
}

struct sock *iucv_accept_dequeue(struct sock *parent, struct socket *newsock)
{
	struct iucv_sock *isk, *n;
	struct sock *sk;

	list_for_each_entry_safe(isk, n, &iucv_sk(parent)->accept_q, accept_q) {
		sk = (struct sock *) isk;
		lock_sock(sk);

		if (sk->sk_state == IUCV_CLOSED) {
			iucv_accept_unlink(sk);
			release_sock(sk);
			continue;
		}

		if (sk->sk_state == IUCV_CONNECTED ||
		    sk->sk_state == IUCV_SEVERED ||
		    !newsock) {
			iucv_accept_unlink(sk);
			if (newsock)
				sock_graft(sk, newsock);

			if (sk->sk_state == IUCV_SEVERED)
				sk->sk_state = IUCV_DISCONN;

			release_sock(sk);
			return sk;
		}

		release_sock(sk);
	}
	return NULL;
}

int iucv_sock_wait_state(struct sock *sk, int state, int state2,
			 unsigned long timeo)
{
	DECLARE_WAITQUEUE(wait, current);
	int err = 0;

	add_wait_queue(sk->sk_sleep, &wait);
	while (sk->sk_state != state && sk->sk_state != state2) {
		set_current_state(TASK_INTERRUPTIBLE);

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
		lock_sock(sk);

		err = sock_error(sk);
		if (err)
			break;
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sk_sleep, &wait);
	return err;
}

/* Bind an unbound socket */
static int iucv_sock_bind(struct socket *sock, struct sockaddr *addr,
			  int addr_len)
{
	struct sockaddr_iucv *sa = (struct sockaddr_iucv *) addr;
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv;
	int err;

	/* Verify the input sockaddr */
	if (!addr || addr->sa_family != AF_IUCV)
		return -EINVAL;

	lock_sock(sk);
	if (sk->sk_state != IUCV_OPEN) {
		err = -EBADFD;
		goto done;
	}

	write_lock_bh(&iucv_sk_list.lock);

	iucv = iucv_sk(sk);
	if (__iucv_get_sock_by_name(sa->siucv_name)) {
		err = -EADDRINUSE;
		goto done_unlock;
	}
	if (iucv->path) {
		err = 0;
		goto done_unlock;
	}

	/* Bind the socket */
	memcpy(iucv->src_name, sa->siucv_name, 8);

	/* Copy the user id */
	memcpy(iucv->src_user_id, iucv_userid, 8);
	sk->sk_state = IUCV_BOUND;
	err = 0;

done_unlock:
	/* Release the socket list lock */
	write_unlock_bh(&iucv_sk_list.lock);
done:
	release_sock(sk);
	return err;
}

/* Automatically bind an unbound socket */
static int iucv_sock_autobind(struct sock *sk)
{
	struct iucv_sock *iucv = iucv_sk(sk);
	char query_buffer[80];
	char name[12];
	int err = 0;

	/* Set the userid and name */
	cpcmd("QUERY USERID", query_buffer, sizeof(query_buffer), &err);
	if (unlikely(err))
		return -EPROTO;

	memcpy(iucv->src_user_id, query_buffer, 8);

	write_lock_bh(&iucv_sk_list.lock);

	sprintf(name, "%08x", atomic_inc_return(&iucv_sk_list.autobind_name));
	while (__iucv_get_sock_by_name(name)) {
		sprintf(name, "%08x",
			atomic_inc_return(&iucv_sk_list.autobind_name));
	}

	write_unlock_bh(&iucv_sk_list.lock);

	memcpy(&iucv->src_name, name, 8);

	return err;
}

/* Connect an unconnected socket */
static int iucv_sock_connect(struct socket *sock, struct sockaddr *addr,
			     int alen, int flags)
{
	struct sockaddr_iucv *sa = (struct sockaddr_iucv *) addr;
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv;
	unsigned char user_data[16];
	int err;

	if (addr->sa_family != AF_IUCV || alen < sizeof(struct sockaddr_iucv))
		return -EINVAL;

	if (sk->sk_state != IUCV_OPEN && sk->sk_state != IUCV_BOUND)
		return -EBADFD;

	if (sk->sk_type != SOCK_STREAM)
		return -EINVAL;

	iucv = iucv_sk(sk);

	if (sk->sk_state == IUCV_OPEN) {
		err = iucv_sock_autobind(sk);
		if (unlikely(err))
			return err;
	}

	lock_sock(sk);

	/* Set the destination information */
	memcpy(iucv_sk(sk)->dst_user_id, sa->siucv_user_id, 8);
	memcpy(iucv_sk(sk)->dst_name, sa->siucv_name, 8);

	high_nmcpy(user_data, sa->siucv_name);
	low_nmcpy(user_data, iucv_sk(sk)->src_name);
	ASCEBC(user_data, sizeof(user_data));

	iucv = iucv_sk(sk);
	/* Create path. */
	iucv->path = iucv_path_alloc(IUCV_QUEUELEN_DEFAULT,
				     IPRMDATA, GFP_KERNEL);
	if (!iucv->path) {
		err = -ENOMEM;
		goto done;
	}
	err = iucv_path_connect(iucv->path, &af_iucv_handler,
				sa->siucv_user_id, NULL, user_data, sk);
	if (err) {
		iucv_path_free(iucv->path);
		iucv->path = NULL;
		err = -ECONNREFUSED;
		goto done;
	}

	if (sk->sk_state != IUCV_CONNECTED) {
		err = iucv_sock_wait_state(sk, IUCV_CONNECTED, IUCV_DISCONN,
				sock_sndtimeo(sk, flags & O_NONBLOCK));
	}

	if (sk->sk_state == IUCV_DISCONN) {
		release_sock(sk);
		return -ECONNREFUSED;
	}
done:
	release_sock(sk);
	return err;
}

/* Move a socket into listening state. */
static int iucv_sock_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err;

	lock_sock(sk);

	err = -EINVAL;
	if (sk->sk_state != IUCV_BOUND || sock->type != SOCK_STREAM)
		goto done;

	sk->sk_max_ack_backlog = backlog;
	sk->sk_ack_backlog = 0;
	sk->sk_state = IUCV_LISTEN;
	err = 0;

done:
	release_sock(sk);
	return err;
}

/* Accept a pending connection */
static int iucv_sock_accept(struct socket *sock, struct socket *newsock,
			    int flags)
{
	DECLARE_WAITQUEUE(wait, current);
	struct sock *sk = sock->sk, *nsk;
	long timeo;
	int err = 0;

	lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

	if (sk->sk_state != IUCV_LISTEN) {
		err = -EBADFD;
		goto done;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	/* Wait for an incoming connection */
	add_wait_queue_exclusive(sk->sk_sleep, &wait);
	while (!(nsk = iucv_accept_dequeue(sk, newsock))) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!timeo) {
			err = -EAGAIN;
			break;
		}

		release_sock(sk);
		timeo = schedule_timeout(timeo);
		lock_sock_nested(sk, SINGLE_DEPTH_NESTING);

		if (sk->sk_state != IUCV_LISTEN) {
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

done:
	release_sock(sk);
	return err;
}

static int iucv_sock_getname(struct socket *sock, struct sockaddr *addr,
			     int *len, int peer)
{
	struct sockaddr_iucv *siucv = (struct sockaddr_iucv *) addr;
	struct sock *sk = sock->sk;

	addr->sa_family = AF_IUCV;
	*len = sizeof(struct sockaddr_iucv);

	if (peer) {
		memcpy(siucv->siucv_user_id, iucv_sk(sk)->dst_user_id, 8);
		memcpy(siucv->siucv_name, &iucv_sk(sk)->dst_name, 8);
	} else {
		memcpy(siucv->siucv_user_id, iucv_sk(sk)->src_user_id, 8);
		memcpy(siucv->siucv_name, iucv_sk(sk)->src_name, 8);
	}
	memset(&siucv->siucv_port, 0, sizeof(siucv->siucv_port));
	memset(&siucv->siucv_addr, 0, sizeof(siucv->siucv_addr));
	memset(siucv->siucv_nodeid, 0, sizeof(siucv->siucv_nodeid));

	return 0;
}

static int iucv_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
			     struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	struct sk_buff *skb;
	struct iucv_message txmsg;
	int err;

	err = sock_error(sk);
	if (err)
		return err;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	lock_sock(sk);

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		err = -EPIPE;
		goto out;
	}

	if (sk->sk_state == IUCV_CONNECTED) {
		if (!(skb = sock_alloc_send_skb(sk, len,
						msg->msg_flags & MSG_DONTWAIT,
						&err)))
			goto out;

		if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
			err = -EFAULT;
			goto fail;
		}

		txmsg.class = 0;
		txmsg.tag = iucv->send_tag++;
		memcpy(skb->cb, &txmsg.tag, 4);
		skb_queue_tail(&iucv->send_skb_q, skb);
		err = iucv_message_send(iucv->path, &txmsg, 0, 0,
					(void *) skb->data, skb->len);
		if (err) {
			if (err == 3)
				printk(KERN_ERR "AF_IUCV msg limit exceeded\n");
			skb_unlink(skb, &iucv->send_skb_q);
			err = -EPIPE;
			goto fail;
		}

	} else {
		err = -ENOTCONN;
		goto out;
	}

	release_sock(sk);
	return len;

fail:
	kfree_skb(skb);
out:
	release_sock(sk);
	return err;
}

static int iucv_fragment_skb(struct sock *sk, struct sk_buff *skb, int len)
{
	int dataleft, size, copied = 0;
	struct sk_buff *nskb;

	dataleft = len;
	while (dataleft) {
		if (dataleft >= sk->sk_rcvbuf / 4)
			size = sk->sk_rcvbuf / 4;
		else
			size = dataleft;

		nskb = alloc_skb(size, GFP_ATOMIC | GFP_DMA);
		if (!nskb)
			return -ENOMEM;

		memcpy(nskb->data, skb->data + copied, size);
		copied += size;
		dataleft -= size;

		skb_reset_transport_header(nskb);
		skb_reset_network_header(nskb);
		nskb->len = size;

		skb_queue_tail(&iucv_sk(sk)->backlog_skb_q, nskb);
	}

	return 0;
}

static void iucv_process_message(struct sock *sk, struct sk_buff *skb,
				 struct iucv_path *path,
				 struct iucv_message *msg)
{
	int rc;

	if (msg->flags & IPRMDATA) {
		skb->data = NULL;
		skb->len = 0;
	} else {
		rc = iucv_message_receive(path, msg, 0, skb->data,
					  msg->length, NULL);
		if (rc) {
			kfree_skb(skb);
			return;
		}
		if (skb->truesize >= sk->sk_rcvbuf / 4) {
			rc = iucv_fragment_skb(sk, skb, msg->length);
			kfree_skb(skb);
			skb = NULL;
			if (rc) {
				iucv_path_sever(path, NULL);
				return;
			}
			skb = skb_dequeue(&iucv_sk(sk)->backlog_skb_q);
		} else {
			skb_reset_transport_header(skb);
			skb_reset_network_header(skb);
			skb->len = msg->length;
		}
	}

	if (sock_queue_rcv_skb(sk, skb))
		skb_queue_head(&iucv_sk(sk)->backlog_skb_q, skb);
}

static void iucv_process_message_q(struct sock *sk)
{
	struct iucv_sock *iucv = iucv_sk(sk);
	struct sk_buff *skb;
	struct sock_msg_q *p, *n;

	list_for_each_entry_safe(p, n, &iucv->message_q.list, list) {
		skb = alloc_skb(p->msg.length, GFP_ATOMIC | GFP_DMA);
		if (!skb)
			break;
		iucv_process_message(sk, skb, p->path, &p->msg);
		list_del(&p->list);
		kfree(p);
		if (!skb_queue_empty(&iucv->backlog_skb_q))
			break;
	}
}

static int iucv_sock_recvmsg(struct kiocb *iocb, struct socket *sock,
			     struct msghdr *msg, size_t len, int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	int target, copied = 0;
	struct sk_buff *skb, *rskb, *cskb;
	int err = 0;

	if ((sk->sk_state == IUCV_DISCONN || sk->sk_state == IUCV_SEVERED) &&
	    skb_queue_empty(&iucv->backlog_skb_q) &&
	    skb_queue_empty(&sk->sk_receive_queue) &&
	    list_empty(&iucv->message_q.list))
		return 0;

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb) {
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			return 0;
		return err;
	}

	copied = min_t(unsigned int, skb->len, len);

	cskb = skb;
	if (memcpy_toiovec(msg->msg_iov, cskb->data, copied)) {
		skb_queue_head(&sk->sk_receive_queue, skb);
		if (copied == 0)
			return -EFAULT;
		goto done;
	}

	len -= copied;

	/* Mark read part of skb as used */
	if (!(flags & MSG_PEEK)) {
		skb_pull(skb, copied);

		if (skb->len) {
			skb_queue_head(&sk->sk_receive_queue, skb);
			goto done;
		}

		kfree_skb(skb);

		/* Queue backlog skbs */
		rskb = skb_dequeue(&iucv->backlog_skb_q);
		while (rskb) {
			if (sock_queue_rcv_skb(sk, rskb)) {
				skb_queue_head(&iucv->backlog_skb_q,
						rskb);
				break;
			} else {
				rskb = skb_dequeue(&iucv->backlog_skb_q);
			}
		}
		if (skb_queue_empty(&iucv->backlog_skb_q)) {
			spin_lock_bh(&iucv->message_q.lock);
			if (!list_empty(&iucv->message_q.list))
				iucv_process_message_q(sk);
			spin_unlock_bh(&iucv->message_q.lock);
		}

	} else
		skb_queue_head(&sk->sk_receive_queue, skb);

done:
	return err ? : copied;
}

static inline unsigned int iucv_accept_poll(struct sock *parent)
{
	struct iucv_sock *isk, *n;
	struct sock *sk;

	list_for_each_entry_safe(isk, n, &iucv_sk(parent)->accept_q, accept_q) {
		sk = (struct sock *) isk;

		if (sk->sk_state == IUCV_CONNECTED)
			return POLLIN | POLLRDNORM;
	}

	return 0;
}

unsigned int iucv_sock_poll(struct file *file, struct socket *sock,
			    poll_table *wait)
{
	struct sock *sk = sock->sk;
	unsigned int mask = 0;

	poll_wait(file, sk->sk_sleep, wait);

	if (sk->sk_state == IUCV_LISTEN)
		return iucv_accept_poll(sk);

	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	if (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	if (sk->sk_state == IUCV_CLOSED)
		mask |= POLLHUP;

	if (sk->sk_state == IUCV_DISCONN || sk->sk_state == IUCV_SEVERED)
		mask |= POLLIN;

	if (sock_writeable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
	else
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	return mask;
}

static int iucv_sock_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	struct iucv_message txmsg;
	int err = 0;
	u8 prmmsg[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

	how++;

	if ((how & ~SHUTDOWN_MASK) || !how)
		return -EINVAL;

	lock_sock(sk);
	switch (sk->sk_state) {
	case IUCV_CLOSED:
		err = -ENOTCONN;
		goto fail;

	default:
		sk->sk_shutdown |= how;
		break;
	}

	if (how == SEND_SHUTDOWN || how == SHUTDOWN_MASK) {
		txmsg.class = 0;
		txmsg.tag = 0;
		err = iucv_message_send(iucv->path, &txmsg, IUCV_IPRMDATA, 0,
					(void *) prmmsg, 8);
		if (err) {
			switch (err) {
			case 1:
				err = -ENOTCONN;
				break;
			case 2:
				err = -ECONNRESET;
				break;
			default:
				err = -ENOTCONN;
				break;
			}
		}
	}

	if (how == RCV_SHUTDOWN || how == SHUTDOWN_MASK) {
		err = iucv_path_quiesce(iucv_sk(sk)->path, NULL);
		if (err)
			err = -ENOTCONN;

		skb_queue_purge(&sk->sk_receive_queue);
	}

	/* Wake up anyone sleeping in poll */
	sk->sk_state_change(sk);

fail:
	release_sock(sk);
	return err;
}

static int iucv_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	int err = 0;

	if (!sk)
		return 0;

	iucv_sock_close(sk);

	/* Unregister with IUCV base support */
	if (iucv_sk(sk)->path) {
		iucv_path_sever(iucv_sk(sk)->path, NULL);
		iucv_path_free(iucv_sk(sk)->path);
		iucv_sk(sk)->path = NULL;
	}

	sock_orphan(sk);
	iucv_sock_kill(sk);
	return err;
}

/* Callback wrappers - called from iucv base support */
static int iucv_callback_connreq(struct iucv_path *path,
				 u8 ipvmid[8], u8 ipuser[16])
{
	unsigned char user_data[16];
	unsigned char nuser_data[16];
	unsigned char src_name[8];
	struct hlist_node *node;
	struct sock *sk, *nsk;
	struct iucv_sock *iucv, *niucv;
	int err;

	memcpy(src_name, ipuser, 8);
	EBCASC(src_name, 8);
	/* Find out if this path belongs to af_iucv. */
	read_lock(&iucv_sk_list.lock);
	iucv = NULL;
	sk = NULL;
	sk_for_each(sk, node, &iucv_sk_list.head)
		if (sk->sk_state == IUCV_LISTEN &&
		    !memcmp(&iucv_sk(sk)->src_name, src_name, 8)) {
			/*
			 * Found a listening socket with
			 * src_name == ipuser[0-7].
			 */
			iucv = iucv_sk(sk);
			break;
		}
	read_unlock(&iucv_sk_list.lock);
	if (!iucv)
		/* No socket found, not one of our paths. */
		return -EINVAL;

	bh_lock_sock(sk);

	/* Check if parent socket is listening */
	low_nmcpy(user_data, iucv->src_name);
	high_nmcpy(user_data, iucv->dst_name);
	ASCEBC(user_data, sizeof(user_data));
	if (sk->sk_state != IUCV_LISTEN) {
		err = iucv_path_sever(path, user_data);
		goto fail;
	}

	/* Check for backlog size */
	if (sk_acceptq_is_full(sk)) {
		err = iucv_path_sever(path, user_data);
		goto fail;
	}

	/* Create the new socket */
	nsk = iucv_sock_alloc(NULL, SOCK_STREAM, GFP_ATOMIC);
	if (!nsk) {
		err = iucv_path_sever(path, user_data);
		goto fail;
	}

	niucv = iucv_sk(nsk);
	iucv_sock_init(nsk, sk);

	/* Set the new iucv_sock */
	memcpy(niucv->dst_name, ipuser + 8, 8);
	EBCASC(niucv->dst_name, 8);
	memcpy(niucv->dst_user_id, ipvmid, 8);
	memcpy(niucv->src_name, iucv->src_name, 8);
	memcpy(niucv->src_user_id, iucv->src_user_id, 8);
	niucv->path = path;

	/* Call iucv_accept */
	high_nmcpy(nuser_data, ipuser + 8);
	memcpy(nuser_data + 8, niucv->src_name, 8);
	ASCEBC(nuser_data + 8, 8);

	path->msglim = IUCV_QUEUELEN_DEFAULT;
	err = iucv_path_accept(path, &af_iucv_handler, nuser_data, nsk);
	if (err) {
		err = iucv_path_sever(path, user_data);
		goto fail;
	}

	iucv_accept_enqueue(sk, nsk);

	/* Wake up accept */
	nsk->sk_state = IUCV_CONNECTED;
	sk->sk_data_ready(sk, 1);
	err = 0;
fail:
	bh_unlock_sock(sk);
	return 0;
}

static void iucv_callback_connack(struct iucv_path *path, u8 ipuser[16])
{
	struct sock *sk = path->private;

	sk->sk_state = IUCV_CONNECTED;
	sk->sk_state_change(sk);
}

static void iucv_callback_rx(struct iucv_path *path, struct iucv_message *msg)
{
	struct sock *sk = path->private;
	struct iucv_sock *iucv = iucv_sk(sk);
	struct sk_buff *skb;
	struct sock_msg_q *save_msg;
	int len;

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		return;

	if (!list_empty(&iucv->message_q.list) ||
	    !skb_queue_empty(&iucv->backlog_skb_q))
		goto save_message;

	len = atomic_read(&sk->sk_rmem_alloc);
	len += msg->length + sizeof(struct sk_buff);
	if (len > sk->sk_rcvbuf)
		goto save_message;

	skb = alloc_skb(msg->length, GFP_ATOMIC | GFP_DMA);
	if (!skb)
		goto save_message;

	spin_lock(&iucv->message_q.lock);
	iucv_process_message(sk, skb, path, msg);
	spin_unlock(&iucv->message_q.lock);

	return;

save_message:
	save_msg = kzalloc(sizeof(struct sock_msg_q), GFP_ATOMIC | GFP_DMA);
	if (!save_msg)
		return;
	save_msg->path = path;
	save_msg->msg = *msg;

	spin_lock(&iucv->message_q.lock);
	list_add_tail(&save_msg->list, &iucv->message_q.list);
	spin_unlock(&iucv->message_q.lock);
}

static void iucv_callback_txdone(struct iucv_path *path,
				 struct iucv_message *msg)
{
	struct sock *sk = path->private;
	struct sk_buff *this = NULL;
	struct sk_buff_head *list = &iucv_sk(sk)->send_skb_q;
	struct sk_buff *list_skb = list->next;
	unsigned long flags;

	if (!skb_queue_empty(list)) {
		spin_lock_irqsave(&list->lock, flags);

		while (list_skb != (struct sk_buff *)list) {
			if (!memcmp(&msg->tag, list_skb->cb, 4)) {
				this = list_skb;
				break;
			}
			list_skb = list_skb->next;
		}
		if (this)
			__skb_unlink(this, list);

		spin_unlock_irqrestore(&list->lock, flags);

		if (this)
			kfree_skb(this);
	}
	if (!this)
		printk(KERN_ERR "AF_IUCV msg tag %u not found\n", msg->tag);

	if (sk->sk_state == IUCV_CLOSING) {
		if (skb_queue_empty(&iucv_sk(sk)->send_skb_q)) {
			sk->sk_state = IUCV_CLOSED;
			sk->sk_state_change(sk);
		}
	}

}

static void iucv_callback_connrej(struct iucv_path *path, u8 ipuser[16])
{
	struct sock *sk = path->private;

	if (!list_empty(&iucv_sk(sk)->accept_q))
		sk->sk_state = IUCV_SEVERED;
	else
		sk->sk_state = IUCV_DISCONN;

	sk->sk_state_change(sk);
}

static struct proto_ops iucv_sock_ops = {
	.family		= PF_IUCV,
	.owner		= THIS_MODULE,
	.release	= iucv_sock_release,
	.bind		= iucv_sock_bind,
	.connect	= iucv_sock_connect,
	.listen		= iucv_sock_listen,
	.accept		= iucv_sock_accept,
	.getname	= iucv_sock_getname,
	.sendmsg	= iucv_sock_sendmsg,
	.recvmsg	= iucv_sock_recvmsg,
	.poll		= iucv_sock_poll,
	.ioctl		= sock_no_ioctl,
	.mmap		= sock_no_mmap,
	.socketpair	= sock_no_socketpair,
	.shutdown	= iucv_sock_shutdown,
	.setsockopt	= sock_no_setsockopt,
	.getsockopt	= sock_no_getsockopt
};

static struct net_proto_family iucv_sock_family_ops = {
	.family	= AF_IUCV,
	.owner	= THIS_MODULE,
	.create	= iucv_sock_create,
};

static int __init afiucv_init(void)
{
	int err;

	if (!MACHINE_IS_VM) {
		printk(KERN_ERR "AF_IUCV connection needs VM as base\n");
		err = -EPROTONOSUPPORT;
		goto out;
	}
	cpcmd("QUERY USERID", iucv_userid, sizeof(iucv_userid), &err);
	if (unlikely(err)) {
		printk(KERN_ERR "AF_IUCV needs the VM userid\n");
		err = -EPROTONOSUPPORT;
		goto out;
	}

	err = iucv_register(&af_iucv_handler, 0);
	if (err)
		goto out;
	err = proto_register(&iucv_proto, 0);
	if (err)
		goto out_iucv;
	err = sock_register(&iucv_sock_family_ops);
	if (err)
		goto out_proto;
	printk(KERN_INFO "AF_IUCV lowlevel driver initialized\n");
	return 0;

out_proto:
	proto_unregister(&iucv_proto);
out_iucv:
	iucv_unregister(&af_iucv_handler, 0);
out:
	return err;
}

static void __exit afiucv_exit(void)
{
	sock_unregister(PF_IUCV);
	proto_unregister(&iucv_proto);
	iucv_unregister(&af_iucv_handler, 0);

	printk(KERN_INFO "AF_IUCV lowlevel driver unloaded\n");
}

module_init(afiucv_init);
module_exit(afiucv_exit);

MODULE_AUTHOR("Jennifer Hunt <jenhunt@us.ibm.com>");
MODULE_DESCRIPTION("IUCV Sockets ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_IUCV);
