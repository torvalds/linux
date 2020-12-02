// SPDX-License-Identifier: GPL-2.0-only
/*
 *  IUCV protocol stack for Linux on zSeries
 *
 *  Copyright IBM Corp. 2006, 2009
 *
 *  Author(s):	Jennifer Hunt <jenhunt@us.ibm.com>
 *		Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 *  PM functions:
 *		Ursula Braun <ursula.braun@de.ibm.com>
 */

#define KMSG_COMPONENT "af_iucv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/security.h>
#include <net/sock.h>
#include <asm/ebcdic.h>
#include <asm/cpcmd.h>
#include <linux/kmod.h>

#include <net/iucv/af_iucv.h>

#define VERSION "1.2"

static char iucv_userid[80];

static struct proto iucv_proto = {
	.name		= "AF_IUCV",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct iucv_sock),
};

static struct iucv_interface *pr_iucv;

/* special AF_IUCV IPRM messages */
static const u8 iprm_shutdown[8] =
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

#define TRGCLS_SIZE	sizeof_field(struct iucv_message, class)

#define __iucv_sock_wait(sk, condition, timeo, ret)			\
do {									\
	DEFINE_WAIT(__wait);						\
	long __timeo = timeo;						\
	ret = 0;							\
	prepare_to_wait(sk_sleep(sk), &__wait, TASK_INTERRUPTIBLE);	\
	while (!(condition)) {						\
		if (!__timeo) {						\
			ret = -EAGAIN;					\
			break;						\
		}							\
		if (signal_pending(current)) {				\
			ret = sock_intr_errno(__timeo);			\
			break;						\
		}							\
		release_sock(sk);					\
		__timeo = schedule_timeout(__timeo);			\
		lock_sock(sk);						\
		ret = sock_error(sk);					\
		if (ret)						\
			break;						\
	}								\
	finish_wait(sk_sleep(sk), &__wait);				\
} while (0)

#define iucv_sock_wait(sk, condition, timeo)				\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__iucv_sock_wait(sk, condition, timeo, __ret);		\
	__ret;								\
})

static struct sock *iucv_accept_dequeue(struct sock *parent,
					struct socket *newsock);
static void iucv_sock_kill(struct sock *sk);
static void iucv_sock_close(struct sock *sk);

static void afiucv_hs_callback_txnotify(struct sk_buff *, enum iucv_tx_notify);

/* Call Back functions */
static void iucv_callback_rx(struct iucv_path *, struct iucv_message *);
static void iucv_callback_txdone(struct iucv_path *, struct iucv_message *);
static void iucv_callback_connack(struct iucv_path *, u8 *);
static int iucv_callback_connreq(struct iucv_path *, u8 *, u8 *);
static void iucv_callback_connrej(struct iucv_path *, u8 *);
static void iucv_callback_shutdown(struct iucv_path *, u8 *);

static struct iucv_sock_list iucv_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(iucv_sk_list.lock),
	.autobind_name = ATOMIC_INIT(0)
};

static struct iucv_handler af_iucv_handler = {
	.path_pending	  = iucv_callback_connreq,
	.path_complete	  = iucv_callback_connack,
	.path_severed	  = iucv_callback_connrej,
	.message_pending  = iucv_callback_rx,
	.message_complete = iucv_callback_txdone,
	.path_quiesced	  = iucv_callback_shutdown,
};

static inline void high_nmcpy(unsigned char *dst, char *src)
{
       memcpy(dst, src, 8);
}

static inline void low_nmcpy(unsigned char *dst, char *src)
{
       memcpy(&dst[8], src, 8);
}

/**
 * iucv_msg_length() - Returns the length of an iucv message.
 * @msg:	Pointer to struct iucv_message, MUST NOT be NULL
 *
 * The function returns the length of the specified iucv message @msg of data
 * stored in a buffer and of data stored in the parameter list (PRMDATA).
 *
 * For IUCV_IPRMDATA, AF_IUCV uses the following convention to transport socket
 * data:
 *	PRMDATA[0..6]	socket data (max 7 bytes);
 *	PRMDATA[7]	socket data length value (len is 0xff - PRMDATA[7])
 *
 * The socket data length is computed by subtracting the socket data length
 * value from 0xFF.
 * If the socket data len is greater 7, then PRMDATA can be used for special
 * notifications (see iucv_sock_shutdown); and further,
 * if the socket data len is > 7, the function returns 8.
 *
 * Use this function to allocate socket buffers to store iucv message data.
 */
static inline size_t iucv_msg_length(struct iucv_message *msg)
{
	size_t datalen;

	if (msg->flags & IUCV_IPRMDATA) {
		datalen = 0xff - msg->rmmsg[7];
		return (datalen < 8) ? datalen : 8;
	}
	return msg->length;
}

/**
 * iucv_sock_in_state() - check for specific states
 * @sk:		sock structure
 * @state:	first iucv sk state
 * @state:	second iucv sk state
 *
 * Returns true if the socket in either in the first or second state.
 */
static int iucv_sock_in_state(struct sock *sk, int state, int state2)
{
	return (sk->sk_state == state || sk->sk_state == state2);
}

/**
 * iucv_below_msglim() - function to check if messages can be sent
 * @sk:		sock structure
 *
 * Returns true if the send queue length is lower than the message limit.
 * Always returns true if the socket is not connected (no iucv path for
 * checking the message limit).
 */
static inline int iucv_below_msglim(struct sock *sk)
{
	struct iucv_sock *iucv = iucv_sk(sk);

	if (sk->sk_state != IUCV_CONNECTED)
		return 1;
	if (iucv->transport == AF_IUCV_TRANS_IUCV)
		return (skb_queue_len(&iucv->send_skb_q) < iucv->path->msglim);
	else
		return ((atomic_read(&iucv->msg_sent) < iucv->msglimit_peer) &&
			(atomic_read(&iucv->pendings) <= 0));
}

/**
 * iucv_sock_wake_msglim() - Wake up thread waiting on msg limit
 */
static void iucv_sock_wake_msglim(struct sock *sk)
{
	struct socket_wq *wq;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_all(&wq->wait);
	sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	rcu_read_unlock();
}

/**
 * afiucv_hs_send() - send a message through HiperSockets transport
 */
static int afiucv_hs_send(struct iucv_message *imsg, struct sock *sock,
		   struct sk_buff *skb, u8 flags)
{
	struct iucv_sock *iucv = iucv_sk(sock);
	struct af_iucv_trans_hdr *phs_hdr;
	struct sk_buff *nskb;
	int err, confirm_recv = 0;

	phs_hdr = skb_push(skb, sizeof(*phs_hdr));
	memset(phs_hdr, 0, sizeof(*phs_hdr));
	skb_reset_network_header(skb);

	phs_hdr->magic = ETH_P_AF_IUCV;
	phs_hdr->version = 1;
	phs_hdr->flags = flags;
	if (flags == AF_IUCV_FLAG_SYN)
		phs_hdr->window = iucv->msglimit;
	else if ((flags == AF_IUCV_FLAG_WIN) || !flags) {
		confirm_recv = atomic_read(&iucv->msg_recv);
		phs_hdr->window = confirm_recv;
		if (confirm_recv)
			phs_hdr->flags = phs_hdr->flags | AF_IUCV_FLAG_WIN;
	}
	memcpy(phs_hdr->destUserID, iucv->dst_user_id, 8);
	memcpy(phs_hdr->destAppName, iucv->dst_name, 8);
	memcpy(phs_hdr->srcUserID, iucv->src_user_id, 8);
	memcpy(phs_hdr->srcAppName, iucv->src_name, 8);
	ASCEBC(phs_hdr->destUserID, sizeof(phs_hdr->destUserID));
	ASCEBC(phs_hdr->destAppName, sizeof(phs_hdr->destAppName));
	ASCEBC(phs_hdr->srcUserID, sizeof(phs_hdr->srcUserID));
	ASCEBC(phs_hdr->srcAppName, sizeof(phs_hdr->srcAppName));
	if (imsg)
		memcpy(&phs_hdr->iucv_hdr, imsg, sizeof(struct iucv_message));

	skb->dev = iucv->hs_dev;
	if (!skb->dev) {
		err = -ENODEV;
		goto err_free;
	}

	dev_hard_header(skb, skb->dev, ETH_P_AF_IUCV, NULL, NULL, skb->len);

	if (!(skb->dev->flags & IFF_UP) || !netif_carrier_ok(skb->dev)) {
		err = -ENETDOWN;
		goto err_free;
	}
	if (skb->len > skb->dev->mtu) {
		if (sock->sk_type == SOCK_SEQPACKET) {
			err = -EMSGSIZE;
			goto err_free;
		}
		skb_trim(skb, skb->dev->mtu);
	}
	skb->protocol = cpu_to_be16(ETH_P_AF_IUCV);

	__skb_header_release(skb);
	nskb = skb_clone(skb, GFP_ATOMIC);
	if (!nskb) {
		err = -ENOMEM;
		goto err_free;
	}

	skb_queue_tail(&iucv->send_skb_q, nskb);
	err = dev_queue_xmit(skb);
	if (net_xmit_eval(err)) {
		skb_unlink(nskb, &iucv->send_skb_q);
		kfree_skb(nskb);
	} else {
		atomic_sub(confirm_recv, &iucv->msg_recv);
		WARN_ON(atomic_read(&iucv->msg_recv) < 0);
	}
	return net_xmit_eval(err);

err_free:
	kfree_skb(skb);
	return err;
}

static struct sock *__iucv_get_sock_by_name(char *nm)
{
	struct sock *sk;

	sk_for_each(sk, &iucv_sk_list.head)
		if (!memcmp(&iucv_sk(sk)->src_name, nm, 8))
			return sk;

	return NULL;
}

static void iucv_sock_destruct(struct sock *sk)
{
	skb_queue_purge(&sk->sk_receive_queue);
	skb_queue_purge(&sk->sk_error_queue);

	sk_mem_reclaim(sk);

	if (!sock_flag(sk, SOCK_DEAD)) {
		pr_err("Attempt to release alive iucv socket %p\n", sk);
		return;
	}

	WARN_ON(atomic_read(&sk->sk_rmem_alloc));
	WARN_ON(refcount_read(&sk->sk_wmem_alloc));
	WARN_ON(sk->sk_wmem_queued);
	WARN_ON(sk->sk_forward_alloc);
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
}

static void iucv_sock_link(struct iucv_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock_bh(&l->lock);
}

static void iucv_sock_unlink(struct iucv_sock_list *l, struct sock *sk)
{
	write_lock_bh(&l->lock);
	sk_del_node_init(sk);
	write_unlock_bh(&l->lock);
}

/* Kill socket (only if zapped and orphaned) */
static void iucv_sock_kill(struct sock *sk)
{
	if (!sock_flag(sk, SOCK_ZAPPED) || sk->sk_socket)
		return;

	iucv_sock_unlink(&iucv_sk_list, sk);
	sock_set_flag(sk, SOCK_DEAD);
	sock_put(sk);
}

/* Terminate an IUCV path */
static void iucv_sever_path(struct sock *sk, int with_user_data)
{
	unsigned char user_data[16];
	struct iucv_sock *iucv = iucv_sk(sk);
	struct iucv_path *path = iucv->path;

	if (iucv->path) {
		iucv->path = NULL;
		if (with_user_data) {
			low_nmcpy(user_data, iucv->src_name);
			high_nmcpy(user_data, iucv->dst_name);
			ASCEBC(user_data, sizeof(user_data));
			pr_iucv->path_sever(path, user_data);
		} else
			pr_iucv->path_sever(path, NULL);
		iucv_path_free(path);
	}
}

/* Send controlling flags through an IUCV socket for HIPER transport */
static int iucv_send_ctrl(struct sock *sk, u8 flags)
{
	struct iucv_sock *iucv = iucv_sk(sk);
	int err = 0;
	int blen;
	struct sk_buff *skb;
	u8 shutdown = 0;

	blen = sizeof(struct af_iucv_trans_hdr) +
	       LL_RESERVED_SPACE(iucv->hs_dev);
	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		/* controlling flags should be sent anyway */
		shutdown = sk->sk_shutdown;
		sk->sk_shutdown &= RCV_SHUTDOWN;
	}
	skb = sock_alloc_send_skb(sk, blen, 1, &err);
	if (skb) {
		skb_reserve(skb, blen);
		err = afiucv_hs_send(NULL, sk, skb, flags);
	}
	if (shutdown)
		sk->sk_shutdown = shutdown;
	return err;
}

/* Close an IUCV socket */
static void iucv_sock_close(struct sock *sk)
{
	struct iucv_sock *iucv = iucv_sk(sk);
	unsigned long timeo;
	int err = 0;

	lock_sock(sk);

	switch (sk->sk_state) {
	case IUCV_LISTEN:
		iucv_sock_cleanup_listen(sk);
		break;

	case IUCV_CONNECTED:
		if (iucv->transport == AF_IUCV_TRANS_HIPER) {
			err = iucv_send_ctrl(sk, AF_IUCV_FLAG_FIN);
			sk->sk_state = IUCV_DISCONN;
			sk->sk_state_change(sk);
		}
		fallthrough;

	case IUCV_DISCONN:
		sk->sk_state = IUCV_CLOSING;
		sk->sk_state_change(sk);

		if (!err && !skb_queue_empty(&iucv->send_skb_q)) {
			if (sock_flag(sk, SOCK_LINGER) && sk->sk_lingertime)
				timeo = sk->sk_lingertime;
			else
				timeo = IUCV_DISCONN_TIMEOUT;
			iucv_sock_wait(sk,
					iucv_sock_in_state(sk, IUCV_CLOSED, 0),
					timeo);
		}
		fallthrough;

	case IUCV_CLOSING:
		sk->sk_state = IUCV_CLOSED;
		sk->sk_state_change(sk);

		sk->sk_err = ECONNRESET;
		sk->sk_state_change(sk);

		skb_queue_purge(&iucv->send_skb_q);
		skb_queue_purge(&iucv->backlog_skb_q);
		fallthrough;

	default:
		iucv_sever_path(sk, 1);
	}

	if (iucv->hs_dev) {
		dev_put(iucv->hs_dev);
		iucv->hs_dev = NULL;
		sk->sk_bound_dev_if = 0;
	}

	/* mark socket for deletion by iucv_sock_kill() */
	sock_set_flag(sk, SOCK_ZAPPED);

	release_sock(sk);
}

static void iucv_sock_init(struct sock *sk, struct sock *parent)
{
	if (parent) {
		sk->sk_type = parent->sk_type;
		security_sk_clone(parent, sk);
	}
}

static struct sock *iucv_sock_alloc(struct socket *sock, int proto, gfp_t prio, int kern)
{
	struct sock *sk;
	struct iucv_sock *iucv;

	sk = sk_alloc(&init_net, PF_IUCV, prio, &iucv_proto, kern);
	if (!sk)
		return NULL;
	iucv = iucv_sk(sk);

	sock_init_data(sock, sk);
	INIT_LIST_HEAD(&iucv->accept_q);
	spin_lock_init(&iucv->accept_q_lock);
	skb_queue_head_init(&iucv->send_skb_q);
	INIT_LIST_HEAD(&iucv->message_q.list);
	spin_lock_init(&iucv->message_q.lock);
	skb_queue_head_init(&iucv->backlog_skb_q);
	iucv->send_tag = 0;
	atomic_set(&iucv->pendings, 0);
	iucv->flags = 0;
	iucv->msglimit = 0;
	atomic_set(&iucv->msg_sent, 0);
	atomic_set(&iucv->msg_recv, 0);
	iucv->path = NULL;
	iucv->sk_txnotify = afiucv_hs_callback_txnotify;
	memset(&iucv->src_user_id , 0, 32);
	if (pr_iucv)
		iucv->transport = AF_IUCV_TRANS_IUCV;
	else
		iucv->transport = AF_IUCV_TRANS_HIPER;

	sk->sk_destruct = iucv_sock_destruct;
	sk->sk_sndtimeo = IUCV_CONN_TIMEOUT;

	sock_reset_flag(sk, SOCK_ZAPPED);

	sk->sk_protocol = proto;
	sk->sk_state	= IUCV_OPEN;

	iucv_sock_link(&iucv_sk_list, sk);
	return sk;
}

static void iucv_accept_enqueue(struct sock *parent, struct sock *sk)
{
	unsigned long flags;
	struct iucv_sock *par = iucv_sk(parent);

	sock_hold(sk);
	spin_lock_irqsave(&par->accept_q_lock, flags);
	list_add_tail(&iucv_sk(sk)->accept_q, &par->accept_q);
	spin_unlock_irqrestore(&par->accept_q_lock, flags);
	iucv_sk(sk)->parent = parent;
	sk_acceptq_added(parent);
}

static void iucv_accept_unlink(struct sock *sk)
{
	unsigned long flags;
	struct iucv_sock *par = iucv_sk(iucv_sk(sk)->parent);

	spin_lock_irqsave(&par->accept_q_lock, flags);
	list_del_init(&iucv_sk(sk)->accept_q);
	spin_unlock_irqrestore(&par->accept_q_lock, flags);
	sk_acceptq_removed(iucv_sk(sk)->parent);
	iucv_sk(sk)->parent = NULL;
	sock_put(sk);
}

static struct sock *iucv_accept_dequeue(struct sock *parent,
					struct socket *newsock)
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
		    sk->sk_state == IUCV_DISCONN ||
		    !newsock) {
			iucv_accept_unlink(sk);
			if (newsock)
				sock_graft(sk, newsock);

			release_sock(sk);
			return sk;
		}

		release_sock(sk);
	}
	return NULL;
}

static void __iucv_auto_name(struct iucv_sock *iucv)
{
	char name[12];

	sprintf(name, "%08x", atomic_inc_return(&iucv_sk_list.autobind_name));
	while (__iucv_get_sock_by_name(name)) {
		sprintf(name, "%08x",
			atomic_inc_return(&iucv_sk_list.autobind_name));
	}
	memcpy(iucv->src_name, name, 8);
}

/* Bind an unbound socket */
static int iucv_sock_bind(struct socket *sock, struct sockaddr *addr,
			  int addr_len)
{
	struct sockaddr_iucv *sa = (struct sockaddr_iucv *) addr;
	char uid[sizeof(sa->siucv_user_id)];
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv;
	int err = 0;
	struct net_device *dev;

	/* Verify the input sockaddr */
	if (addr_len < sizeof(struct sockaddr_iucv) ||
	    addr->sa_family != AF_IUCV)
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
	if (iucv->path)
		goto done_unlock;

	/* Bind the socket */
	if (pr_iucv)
		if (!memcmp(sa->siucv_user_id, iucv_userid, 8))
			goto vm_bind; /* VM IUCV transport */

	/* try hiper transport */
	memcpy(uid, sa->siucv_user_id, sizeof(uid));
	ASCEBC(uid, 8);
	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		if (!memcmp(dev->perm_addr, uid, 8)) {
			memcpy(iucv->src_user_id, sa->siucv_user_id, 8);
			/* Check for unitialized siucv_name */
			if (strncmp(sa->siucv_name, "        ", 8) == 0)
				__iucv_auto_name(iucv);
			else
				memcpy(iucv->src_name, sa->siucv_name, 8);
			sk->sk_bound_dev_if = dev->ifindex;
			iucv->hs_dev = dev;
			dev_hold(dev);
			sk->sk_state = IUCV_BOUND;
			iucv->transport = AF_IUCV_TRANS_HIPER;
			if (!iucv->msglimit)
				iucv->msglimit = IUCV_HIPER_MSGLIM_DEFAULT;
			rcu_read_unlock();
			goto done_unlock;
		}
	}
	rcu_read_unlock();
vm_bind:
	if (pr_iucv) {
		/* use local userid for backward compat */
		memcpy(iucv->src_name, sa->siucv_name, 8);
		memcpy(iucv->src_user_id, iucv_userid, 8);
		sk->sk_state = IUCV_BOUND;
		iucv->transport = AF_IUCV_TRANS_IUCV;
		sk->sk_allocation |= GFP_DMA;
		if (!iucv->msglimit)
			iucv->msglimit = IUCV_QUEUELEN_DEFAULT;
		goto done_unlock;
	}
	/* found no dev to bind */
	err = -ENODEV;
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
	int err = 0;

	if (unlikely(!pr_iucv))
		return -EPROTO;

	memcpy(iucv->src_user_id, iucv_userid, 8);
	iucv->transport = AF_IUCV_TRANS_IUCV;
	sk->sk_allocation |= GFP_DMA;

	write_lock_bh(&iucv_sk_list.lock);
	__iucv_auto_name(iucv);
	write_unlock_bh(&iucv_sk_list.lock);

	if (!iucv->msglimit)
		iucv->msglimit = IUCV_QUEUELEN_DEFAULT;

	return err;
}

static int afiucv_path_connect(struct socket *sock, struct sockaddr *addr)
{
	struct sockaddr_iucv *sa = (struct sockaddr_iucv *) addr;
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	unsigned char user_data[16];
	int err;

	high_nmcpy(user_data, sa->siucv_name);
	low_nmcpy(user_data, iucv->src_name);
	ASCEBC(user_data, sizeof(user_data));

	/* Create path. */
	iucv->path = iucv_path_alloc(iucv->msglimit,
				     IUCV_IPRMDATA, GFP_KERNEL);
	if (!iucv->path) {
		err = -ENOMEM;
		goto done;
	}
	err = pr_iucv->path_connect(iucv->path, &af_iucv_handler,
				    sa->siucv_user_id, NULL, user_data,
				    sk);
	if (err) {
		iucv_path_free(iucv->path);
		iucv->path = NULL;
		switch (err) {
		case 0x0b:	/* Target communicator is not logged on */
			err = -ENETUNREACH;
			break;
		case 0x0d:	/* Max connections for this guest exceeded */
		case 0x0e:	/* Max connections for target guest exceeded */
			err = -EAGAIN;
			break;
		case 0x0f:	/* Missing IUCV authorization */
			err = -EACCES;
			break;
		default:
			err = -ECONNREFUSED;
			break;
		}
	}
done:
	return err;
}

/* Connect an unconnected socket */
static int iucv_sock_connect(struct socket *sock, struct sockaddr *addr,
			     int alen, int flags)
{
	struct sockaddr_iucv *sa = (struct sockaddr_iucv *) addr;
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	int err;

	if (alen < sizeof(struct sockaddr_iucv) || addr->sa_family != AF_IUCV)
		return -EINVAL;

	if (sk->sk_state != IUCV_OPEN && sk->sk_state != IUCV_BOUND)
		return -EBADFD;

	if (sk->sk_state == IUCV_OPEN &&
	    iucv->transport == AF_IUCV_TRANS_HIPER)
		return -EBADFD; /* explicit bind required */

	if (sk->sk_type != SOCK_STREAM && sk->sk_type != SOCK_SEQPACKET)
		return -EINVAL;

	if (sk->sk_state == IUCV_OPEN) {
		err = iucv_sock_autobind(sk);
		if (unlikely(err))
			return err;
	}

	lock_sock(sk);

	/* Set the destination information */
	memcpy(iucv->dst_user_id, sa->siucv_user_id, 8);
	memcpy(iucv->dst_name, sa->siucv_name, 8);

	if (iucv->transport == AF_IUCV_TRANS_HIPER)
		err = iucv_send_ctrl(sock->sk, AF_IUCV_FLAG_SYN);
	else
		err = afiucv_path_connect(sock, addr);
	if (err)
		goto done;

	if (sk->sk_state != IUCV_CONNECTED)
		err = iucv_sock_wait(sk, iucv_sock_in_state(sk, IUCV_CONNECTED,
							    IUCV_DISCONN),
				     sock_sndtimeo(sk, flags & O_NONBLOCK));

	if (sk->sk_state == IUCV_DISCONN || sk->sk_state == IUCV_CLOSED)
		err = -ECONNREFUSED;

	if (err && iucv->transport == AF_IUCV_TRANS_IUCV)
		iucv_sever_path(sk, 0);

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
	if (sk->sk_state != IUCV_BOUND)
		goto done;

	if (sock->type != SOCK_STREAM && sock->type != SOCK_SEQPACKET)
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
			    int flags, bool kern)
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
	add_wait_queue_exclusive(sk_sleep(sk), &wait);
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
	remove_wait_queue(sk_sleep(sk), &wait);

	if (err)
		goto done;

	newsock->state = SS_CONNECTED;

done:
	release_sock(sk);
	return err;
}

static int iucv_sock_getname(struct socket *sock, struct sockaddr *addr,
			     int peer)
{
	struct sockaddr_iucv *siucv = (struct sockaddr_iucv *) addr;
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);

	addr->sa_family = AF_IUCV;

	if (peer) {
		memcpy(siucv->siucv_user_id, iucv->dst_user_id, 8);
		memcpy(siucv->siucv_name, iucv->dst_name, 8);
	} else {
		memcpy(siucv->siucv_user_id, iucv->src_user_id, 8);
		memcpy(siucv->siucv_name, iucv->src_name, 8);
	}
	memset(&siucv->siucv_port, 0, sizeof(siucv->siucv_port));
	memset(&siucv->siucv_addr, 0, sizeof(siucv->siucv_addr));
	memset(&siucv->siucv_nodeid, 0, sizeof(siucv->siucv_nodeid));

	return sizeof(struct sockaddr_iucv);
}

/**
 * iucv_send_iprm() - Send socket data in parameter list of an iucv message.
 * @path:	IUCV path
 * @msg:	Pointer to a struct iucv_message
 * @skb:	The socket data to send, skb->len MUST BE <= 7
 *
 * Send the socket data in the parameter list in the iucv message
 * (IUCV_IPRMDATA). The socket data is stored at index 0 to 6 in the parameter
 * list and the socket data len at index 7 (last byte).
 * See also iucv_msg_length().
 *
 * Returns the error code from the iucv_message_send() call.
 */
static int iucv_send_iprm(struct iucv_path *path, struct iucv_message *msg,
			  struct sk_buff *skb)
{
	u8 prmdata[8];

	memcpy(prmdata, (void *) skb->data, skb->len);
	prmdata[7] = 0xff - (u8) skb->len;
	return pr_iucv->message_send(path, msg, IUCV_IPRMDATA, 0,
				 (void *) prmdata, 8);
}

static int iucv_sock_sendmsg(struct socket *sock, struct msghdr *msg,
			     size_t len)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	size_t headroom = 0;
	size_t linear;
	struct sk_buff *skb;
	struct iucv_message txmsg = {0};
	struct cmsghdr *cmsg;
	int cmsg_done;
	long timeo;
	char user_id[9];
	char appl_id[9];
	int err;
	int noblock = msg->msg_flags & MSG_DONTWAIT;

	err = sock_error(sk);
	if (err)
		return err;

	if (msg->msg_flags & MSG_OOB)
		return -EOPNOTSUPP;

	/* SOCK_SEQPACKET: we do not support segmented records */
	if (sk->sk_type == SOCK_SEQPACKET && !(msg->msg_flags & MSG_EOR))
		return -EOPNOTSUPP;

	lock_sock(sk);

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		err = -EPIPE;
		goto out;
	}

	/* Return if the socket is not in connected state */
	if (sk->sk_state != IUCV_CONNECTED) {
		err = -ENOTCONN;
		goto out;
	}

	/* initialize defaults */
	cmsg_done   = 0;	/* check for duplicate headers */

	/* iterate over control messages */
	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg)) {
			err = -EINVAL;
			goto out;
		}

		if (cmsg->cmsg_level != SOL_IUCV)
			continue;

		if (cmsg->cmsg_type & cmsg_done) {
			err = -EINVAL;
			goto out;
		}
		cmsg_done |= cmsg->cmsg_type;

		switch (cmsg->cmsg_type) {
		case SCM_IUCV_TRGCLS:
			if (cmsg->cmsg_len != CMSG_LEN(TRGCLS_SIZE)) {
				err = -EINVAL;
				goto out;
			}

			/* set iucv message target class */
			memcpy(&txmsg.class,
				(void *) CMSG_DATA(cmsg), TRGCLS_SIZE);

			break;

		default:
			err = -EINVAL;
			goto out;
		}
	}

	/* allocate one skb for each iucv message:
	 * this is fine for SOCK_SEQPACKET (unless we want to support
	 * segmented records using the MSG_EOR flag), but
	 * for SOCK_STREAM we might want to improve it in future */
	if (iucv->transport == AF_IUCV_TRANS_HIPER) {
		headroom = sizeof(struct af_iucv_trans_hdr) +
			   LL_RESERVED_SPACE(iucv->hs_dev);
		linear = len;
	} else {
		if (len < PAGE_SIZE) {
			linear = len;
		} else {
			/* In nonlinear "classic" iucv skb,
			 * reserve space for iucv_array
			 */
			headroom = sizeof(struct iucv_array) *
				   (MAX_SKB_FRAGS + 1);
			linear = PAGE_SIZE - headroom;
		}
	}
	skb = sock_alloc_send_pskb(sk, headroom + linear, len - linear,
				   noblock, &err, 0);
	if (!skb)
		goto out;
	if (headroom)
		skb_reserve(skb, headroom);
	skb_put(skb, linear);
	skb->len = len;
	skb->data_len = len - linear;
	err = skb_copy_datagram_from_iter(skb, 0, &msg->msg_iter, len);
	if (err)
		goto fail;

	/* wait if outstanding messages for iucv path has reached */
	timeo = sock_sndtimeo(sk, noblock);
	err = iucv_sock_wait(sk, iucv_below_msglim(sk), timeo);
	if (err)
		goto fail;

	/* return -ECONNRESET if the socket is no longer connected */
	if (sk->sk_state != IUCV_CONNECTED) {
		err = -ECONNRESET;
		goto fail;
	}

	/* increment and save iucv message tag for msg_completion cbk */
	txmsg.tag = iucv->send_tag++;
	IUCV_SKB_CB(skb)->tag = txmsg.tag;

	if (iucv->transport == AF_IUCV_TRANS_HIPER) {
		atomic_inc(&iucv->msg_sent);
		err = afiucv_hs_send(&txmsg, sk, skb, 0);
		if (err) {
			atomic_dec(&iucv->msg_sent);
			goto out;
		}
	} else { /* Classic VM IUCV transport */
		skb_queue_tail(&iucv->send_skb_q, skb);

		if (((iucv->path->flags & IUCV_IPRMDATA) & iucv->flags) &&
		    skb->len <= 7) {
			err = iucv_send_iprm(iucv->path, &txmsg, skb);

			/* on success: there is no message_complete callback */
			/* for an IPRMDATA msg; remove skb from send queue   */
			if (err == 0) {
				skb_unlink(skb, &iucv->send_skb_q);
				kfree_skb(skb);
			}

			/* this error should never happen since the	*/
			/* IUCV_IPRMDATA path flag is set... sever path */
			if (err == 0x15) {
				pr_iucv->path_sever(iucv->path, NULL);
				skb_unlink(skb, &iucv->send_skb_q);
				err = -EPIPE;
				goto fail;
			}
		} else if (skb_is_nonlinear(skb)) {
			struct iucv_array *iba = (struct iucv_array *)skb->head;
			int i;

			/* skip iucv_array lying in the headroom */
			iba[0].address = (u32)(addr_t)skb->data;
			iba[0].length = (u32)skb_headlen(skb);
			for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
				skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

				iba[i + 1].address =
					(u32)(addr_t)skb_frag_address(frag);
				iba[i + 1].length = (u32)skb_frag_size(frag);
			}
			err = pr_iucv->message_send(iucv->path, &txmsg,
						    IUCV_IPBUFLST, 0,
						    (void *)iba, skb->len);
		} else { /* non-IPRM Linear skb */
			err = pr_iucv->message_send(iucv->path, &txmsg,
					0, 0, (void *)skb->data, skb->len);
		}
		if (err) {
			if (err == 3) {
				user_id[8] = 0;
				memcpy(user_id, iucv->dst_user_id, 8);
				appl_id[8] = 0;
				memcpy(appl_id, iucv->dst_name, 8);
				pr_err(
		"Application %s on z/VM guest %s exceeds message limit\n",
					appl_id, user_id);
				err = -EAGAIN;
			} else {
				err = -EPIPE;
			}
			skb_unlink(skb, &iucv->send_skb_q);
			goto fail;
		}
	}

	release_sock(sk);
	return len;

fail:
	kfree_skb(skb);
out:
	release_sock(sk);
	return err;
}

static struct sk_buff *alloc_iucv_recv_skb(unsigned long len)
{
	size_t headroom, linear;
	struct sk_buff *skb;
	int err;

	if (len < PAGE_SIZE) {
		headroom = 0;
		linear = len;
	} else {
		headroom = sizeof(struct iucv_array) * (MAX_SKB_FRAGS + 1);
		linear = PAGE_SIZE - headroom;
	}
	skb = alloc_skb_with_frags(headroom + linear, len - linear,
				   0, &err, GFP_ATOMIC | GFP_DMA);
	WARN_ONCE(!skb,
		  "alloc of recv iucv skb len=%lu failed with errcode=%d\n",
		  len, err);
	if (skb) {
		if (headroom)
			skb_reserve(skb, headroom);
		skb_put(skb, linear);
		skb->len = len;
		skb->data_len = len - linear;
	}
	return skb;
}

/* iucv_process_message() - Receive a single outstanding IUCV message
 *
 * Locking: must be called with message_q.lock held
 */
static void iucv_process_message(struct sock *sk, struct sk_buff *skb,
				 struct iucv_path *path,
				 struct iucv_message *msg)
{
	int rc;
	unsigned int len;

	len = iucv_msg_length(msg);

	/* store msg target class in the second 4 bytes of skb ctrl buffer */
	/* Note: the first 4 bytes are reserved for msg tag */
	IUCV_SKB_CB(skb)->class = msg->class;

	/* check for special IPRM messages (e.g. iucv_sock_shutdown) */
	if ((msg->flags & IUCV_IPRMDATA) && len > 7) {
		if (memcmp(msg->rmmsg, iprm_shutdown, 8) == 0) {
			skb->data = NULL;
			skb->len = 0;
		}
	} else {
		if (skb_is_nonlinear(skb)) {
			struct iucv_array *iba = (struct iucv_array *)skb->head;
			int i;

			iba[0].address = (u32)(addr_t)skb->data;
			iba[0].length = (u32)skb_headlen(skb);
			for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
				skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

				iba[i + 1].address =
					(u32)(addr_t)skb_frag_address(frag);
				iba[i + 1].length = (u32)skb_frag_size(frag);
			}
			rc = pr_iucv->message_receive(path, msg,
					      IUCV_IPBUFLST,
					      (void *)iba, len, NULL);
		} else {
			rc = pr_iucv->message_receive(path, msg,
					      msg->flags & IUCV_IPRMDATA,
					      skb->data, len, NULL);
		}
		if (rc) {
			kfree_skb(skb);
			return;
		}
		WARN_ON_ONCE(skb->len != len);
	}

	IUCV_SKB_CB(skb)->offset = 0;
	if (sk_filter(sk, skb)) {
		atomic_inc(&sk->sk_drops);	/* skb rejected by filter */
		kfree_skb(skb);
		return;
	}
	if (__sock_queue_rcv_skb(sk, skb))	/* handle rcv queue full */
		skb_queue_tail(&iucv_sk(sk)->backlog_skb_q, skb);
}

/* iucv_process_message_q() - Process outstanding IUCV messages
 *
 * Locking: must be called with message_q.lock held
 */
static void iucv_process_message_q(struct sock *sk)
{
	struct iucv_sock *iucv = iucv_sk(sk);
	struct sk_buff *skb;
	struct sock_msg_q *p, *n;

	list_for_each_entry_safe(p, n, &iucv->message_q.list, list) {
		skb = alloc_iucv_recv_skb(iucv_msg_length(&p->msg));
		if (!skb)
			break;
		iucv_process_message(sk, skb, p->path, &p->msg);
		list_del(&p->list);
		kfree(p);
		if (!skb_queue_empty(&iucv->backlog_skb_q))
			break;
	}
}

static int iucv_sock_recvmsg(struct socket *sock, struct msghdr *msg,
			     size_t len, int flags)
{
	int noblock = flags & MSG_DONTWAIT;
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	unsigned int copied, rlen;
	struct sk_buff *skb, *rskb, *cskb;
	int err = 0;
	u32 offset;

	if ((sk->sk_state == IUCV_DISCONN) &&
	    skb_queue_empty(&iucv->backlog_skb_q) &&
	    skb_queue_empty(&sk->sk_receive_queue) &&
	    list_empty(&iucv->message_q.list))
		return 0;

	if (flags & (MSG_OOB))
		return -EOPNOTSUPP;

	/* receive/dequeue next skb:
	 * the function understands MSG_PEEK and, thus, does not dequeue skb */
	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb) {
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			return 0;
		return err;
	}

	offset = IUCV_SKB_CB(skb)->offset;
	rlen   = skb->len - offset;		/* real length of skb */
	copied = min_t(unsigned int, rlen, len);
	if (!rlen)
		sk->sk_shutdown = sk->sk_shutdown | RCV_SHUTDOWN;

	cskb = skb;
	if (skb_copy_datagram_msg(cskb, offset, msg, copied)) {
		if (!(flags & MSG_PEEK))
			skb_queue_head(&sk->sk_receive_queue, skb);
		return -EFAULT;
	}

	/* SOCK_SEQPACKET: set MSG_TRUNC if recv buf size is too small */
	if (sk->sk_type == SOCK_SEQPACKET) {
		if (copied < rlen)
			msg->msg_flags |= MSG_TRUNC;
		/* each iucv message contains a complete record */
		msg->msg_flags |= MSG_EOR;
	}

	/* create control message to store iucv msg target class:
	 * get the trgcls from the control buffer of the skb due to
	 * fragmentation of original iucv message. */
	err = put_cmsg(msg, SOL_IUCV, SCM_IUCV_TRGCLS,
		       sizeof(IUCV_SKB_CB(skb)->class),
		       (void *)&IUCV_SKB_CB(skb)->class);
	if (err) {
		if (!(flags & MSG_PEEK))
			skb_queue_head(&sk->sk_receive_queue, skb);
		return err;
	}

	/* Mark read part of skb as used */
	if (!(flags & MSG_PEEK)) {

		/* SOCK_STREAM: re-queue skb if it contains unreceived data */
		if (sk->sk_type == SOCK_STREAM) {
			if (copied < rlen) {
				IUCV_SKB_CB(skb)->offset = offset + copied;
				skb_queue_head(&sk->sk_receive_queue, skb);
				goto done;
			}
		}

		kfree_skb(skb);
		if (iucv->transport == AF_IUCV_TRANS_HIPER) {
			atomic_inc(&iucv->msg_recv);
			if (atomic_read(&iucv->msg_recv) > iucv->msglimit) {
				WARN_ON(1);
				iucv_sock_close(sk);
				return -EFAULT;
			}
		}

		/* Queue backlog skbs */
		spin_lock_bh(&iucv->message_q.lock);
		rskb = skb_dequeue(&iucv->backlog_skb_q);
		while (rskb) {
			IUCV_SKB_CB(rskb)->offset = 0;
			if (__sock_queue_rcv_skb(sk, rskb)) {
				/* handle rcv queue full */
				skb_queue_head(&iucv->backlog_skb_q,
						rskb);
				break;
			}
			rskb = skb_dequeue(&iucv->backlog_skb_q);
		}
		if (skb_queue_empty(&iucv->backlog_skb_q)) {
			if (!list_empty(&iucv->message_q.list))
				iucv_process_message_q(sk);
			if (atomic_read(&iucv->msg_recv) >=
							iucv->msglimit / 2) {
				err = iucv_send_ctrl(sk, AF_IUCV_FLAG_WIN);
				if (err) {
					sk->sk_state = IUCV_DISCONN;
					sk->sk_state_change(sk);
				}
			}
		}
		spin_unlock_bh(&iucv->message_q.lock);
	}

done:
	/* SOCK_SEQPACKET: return real length if MSG_TRUNC is set */
	if (sk->sk_type == SOCK_SEQPACKET && (flags & MSG_TRUNC))
		copied = rlen;

	return copied;
}

static inline __poll_t iucv_accept_poll(struct sock *parent)
{
	struct iucv_sock *isk, *n;
	struct sock *sk;

	list_for_each_entry_safe(isk, n, &iucv_sk(parent)->accept_q, accept_q) {
		sk = (struct sock *) isk;

		if (sk->sk_state == IUCV_CONNECTED)
			return EPOLLIN | EPOLLRDNORM;
	}

	return 0;
}

static __poll_t iucv_sock_poll(struct file *file, struct socket *sock,
			       poll_table *wait)
{
	struct sock *sk = sock->sk;
	__poll_t mask = 0;

	sock_poll_wait(file, sock, wait);

	if (sk->sk_state == IUCV_LISTEN)
		return iucv_accept_poll(sk);

	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= EPOLLERR |
			(sock_flag(sk, SOCK_SELECT_ERR_QUEUE) ? EPOLLPRI : 0);

	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= EPOLLRDHUP;

	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= EPOLLHUP;

	if (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= EPOLLIN | EPOLLRDNORM;

	if (sk->sk_state == IUCV_CLOSED)
		mask |= EPOLLHUP;

	if (sk->sk_state == IUCV_DISCONN)
		mask |= EPOLLIN;

	if (sock_writeable(sk) && iucv_below_msglim(sk))
		mask |= EPOLLOUT | EPOLLWRNORM | EPOLLWRBAND;
	else
		sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	return mask;
}

static int iucv_sock_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	struct iucv_message txmsg;
	int err = 0;

	how++;

	if ((how & ~SHUTDOWN_MASK) || !how)
		return -EINVAL;

	lock_sock(sk);
	switch (sk->sk_state) {
	case IUCV_LISTEN:
	case IUCV_DISCONN:
	case IUCV_CLOSING:
	case IUCV_CLOSED:
		err = -ENOTCONN;
		goto fail;
	default:
		break;
	}

	if ((how == SEND_SHUTDOWN || how == SHUTDOWN_MASK) &&
	    sk->sk_state == IUCV_CONNECTED) {
		if (iucv->transport == AF_IUCV_TRANS_IUCV) {
			txmsg.class = 0;
			txmsg.tag = 0;
			err = pr_iucv->message_send(iucv->path, &txmsg,
				IUCV_IPRMDATA, 0, (void *) iprm_shutdown, 8);
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
		} else
			iucv_send_ctrl(sk, AF_IUCV_FLAG_SHT);
	}

	sk->sk_shutdown |= how;
	if (how == RCV_SHUTDOWN || how == SHUTDOWN_MASK) {
		if ((iucv->transport == AF_IUCV_TRANS_IUCV) &&
		    iucv->path) {
			err = pr_iucv->path_quiesce(iucv->path, NULL);
			if (err)
				err = -ENOTCONN;
/*			skb_queue_purge(&sk->sk_receive_queue); */
		}
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

	sock_orphan(sk);
	iucv_sock_kill(sk);
	return err;
}

/* getsockopt and setsockopt */
static int iucv_sock_setsockopt(struct socket *sock, int level, int optname,
				sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	int val;
	int rc;

	if (level != SOL_IUCV)
		return -ENOPROTOOPT;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (copy_from_sockptr(&val, optval, sizeof(int)))
		return -EFAULT;

	rc = 0;

	lock_sock(sk);
	switch (optname) {
	case SO_IPRMDATA_MSG:
		if (val)
			iucv->flags |= IUCV_IPRMDATA;
		else
			iucv->flags &= ~IUCV_IPRMDATA;
		break;
	case SO_MSGLIMIT:
		switch (sk->sk_state) {
		case IUCV_OPEN:
		case IUCV_BOUND:
			if (val < 1 || val > U16_MAX)
				rc = -EINVAL;
			else
				iucv->msglimit = val;
			break;
		default:
			rc = -EINVAL;
			break;
		}
		break;
	default:
		rc = -ENOPROTOOPT;
		break;
	}
	release_sock(sk);

	return rc;
}

static int iucv_sock_getsockopt(struct socket *sock, int level, int optname,
				char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	unsigned int val;
	int len;

	if (level != SOL_IUCV)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < 0)
		return -EINVAL;

	len = min_t(unsigned int, len, sizeof(int));

	switch (optname) {
	case SO_IPRMDATA_MSG:
		val = (iucv->flags & IUCV_IPRMDATA) ? 1 : 0;
		break;
	case SO_MSGLIMIT:
		lock_sock(sk);
		val = (iucv->path != NULL) ? iucv->path->msglim	/* connected */
					   : iucv->msglimit;	/* default */
		release_sock(sk);
		break;
	case SO_MSGSIZE:
		if (sk->sk_state == IUCV_OPEN)
			return -EBADFD;
		val = (iucv->hs_dev) ? iucv->hs_dev->mtu -
				sizeof(struct af_iucv_trans_hdr) - ETH_HLEN :
				0x7fffffff;
		break;
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}


/* Callback wrappers - called from iucv base support */
static int iucv_callback_connreq(struct iucv_path *path,
				 u8 ipvmid[8], u8 ipuser[16])
{
	unsigned char user_data[16];
	unsigned char nuser_data[16];
	unsigned char src_name[8];
	struct sock *sk, *nsk;
	struct iucv_sock *iucv, *niucv;
	int err;

	memcpy(src_name, ipuser, 8);
	EBCASC(src_name, 8);
	/* Find out if this path belongs to af_iucv. */
	read_lock(&iucv_sk_list.lock);
	iucv = NULL;
	sk = NULL;
	sk_for_each(sk, &iucv_sk_list.head)
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
		err = pr_iucv->path_sever(path, user_data);
		iucv_path_free(path);
		goto fail;
	}

	/* Check for backlog size */
	if (sk_acceptq_is_full(sk)) {
		err = pr_iucv->path_sever(path, user_data);
		iucv_path_free(path);
		goto fail;
	}

	/* Create the new socket */
	nsk = iucv_sock_alloc(NULL, sk->sk_protocol, GFP_ATOMIC, 0);
	if (!nsk) {
		err = pr_iucv->path_sever(path, user_data);
		iucv_path_free(path);
		goto fail;
	}

	niucv = iucv_sk(nsk);
	iucv_sock_init(nsk, sk);
	niucv->transport = AF_IUCV_TRANS_IUCV;
	nsk->sk_allocation |= GFP_DMA;

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

	/* set message limit for path based on msglimit of accepting socket */
	niucv->msglimit = iucv->msglimit;
	path->msglim = iucv->msglimit;
	err = pr_iucv->path_accept(path, &af_iucv_handler, nuser_data, nsk);
	if (err) {
		iucv_sever_path(nsk, 1);
		iucv_sock_kill(nsk);
		goto fail;
	}

	iucv_accept_enqueue(sk, nsk);

	/* Wake up accept */
	nsk->sk_state = IUCV_CONNECTED;
	sk->sk_data_ready(sk);
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

	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		pr_iucv->message_reject(path, msg);
		return;
	}

	spin_lock(&iucv->message_q.lock);

	if (!list_empty(&iucv->message_q.list) ||
	    !skb_queue_empty(&iucv->backlog_skb_q))
		goto save_message;

	len = atomic_read(&sk->sk_rmem_alloc);
	len += SKB_TRUESIZE(iucv_msg_length(msg));
	if (len > sk->sk_rcvbuf)
		goto save_message;

	skb = alloc_iucv_recv_skb(iucv_msg_length(msg));
	if (!skb)
		goto save_message;

	iucv_process_message(sk, skb, path, msg);
	goto out_unlock;

save_message:
	save_msg = kzalloc(sizeof(struct sock_msg_q), GFP_ATOMIC | GFP_DMA);
	if (!save_msg)
		goto out_unlock;
	save_msg->path = path;
	save_msg->msg = *msg;

	list_add_tail(&save_msg->list, &iucv->message_q.list);

out_unlock:
	spin_unlock(&iucv->message_q.lock);
}

static void iucv_callback_txdone(struct iucv_path *path,
				 struct iucv_message *msg)
{
	struct sock *sk = path->private;
	struct sk_buff *this = NULL;
	struct sk_buff_head *list = &iucv_sk(sk)->send_skb_q;
	struct sk_buff *list_skb;
	unsigned long flags;

	bh_lock_sock(sk);

	spin_lock_irqsave(&list->lock, flags);
	skb_queue_walk(list, list_skb) {
		if (msg->tag == IUCV_SKB_CB(list_skb)->tag) {
			this = list_skb;
			break;
		}
	}
	if (this)
		__skb_unlink(this, list);
	spin_unlock_irqrestore(&list->lock, flags);

	if (this) {
		kfree_skb(this);
		/* wake up any process waiting for sending */
		iucv_sock_wake_msglim(sk);
	}

	if (sk->sk_state == IUCV_CLOSING) {
		if (skb_queue_empty(&iucv_sk(sk)->send_skb_q)) {
			sk->sk_state = IUCV_CLOSED;
			sk->sk_state_change(sk);
		}
	}
	bh_unlock_sock(sk);

}

static void iucv_callback_connrej(struct iucv_path *path, u8 ipuser[16])
{
	struct sock *sk = path->private;

	if (sk->sk_state == IUCV_CLOSED)
		return;

	bh_lock_sock(sk);
	iucv_sever_path(sk, 1);
	sk->sk_state = IUCV_DISCONN;

	sk->sk_state_change(sk);
	bh_unlock_sock(sk);
}

/* called if the other communication side shuts down its RECV direction;
 * in turn, the callback sets SEND_SHUTDOWN to disable sending of data.
 */
static void iucv_callback_shutdown(struct iucv_path *path, u8 ipuser[16])
{
	struct sock *sk = path->private;

	bh_lock_sock(sk);
	if (sk->sk_state != IUCV_CLOSED) {
		sk->sk_shutdown |= SEND_SHUTDOWN;
		sk->sk_state_change(sk);
	}
	bh_unlock_sock(sk);
}

/***************** HiperSockets transport callbacks ********************/
static void afiucv_swap_src_dest(struct sk_buff *skb)
{
	struct af_iucv_trans_hdr *trans_hdr = iucv_trans_hdr(skb);
	char tmpID[8];
	char tmpName[8];

	ASCEBC(trans_hdr->destUserID, sizeof(trans_hdr->destUserID));
	ASCEBC(trans_hdr->destAppName, sizeof(trans_hdr->destAppName));
	ASCEBC(trans_hdr->srcUserID, sizeof(trans_hdr->srcUserID));
	ASCEBC(trans_hdr->srcAppName, sizeof(trans_hdr->srcAppName));
	memcpy(tmpID, trans_hdr->srcUserID, 8);
	memcpy(tmpName, trans_hdr->srcAppName, 8);
	memcpy(trans_hdr->srcUserID, trans_hdr->destUserID, 8);
	memcpy(trans_hdr->srcAppName, trans_hdr->destAppName, 8);
	memcpy(trans_hdr->destUserID, tmpID, 8);
	memcpy(trans_hdr->destAppName, tmpName, 8);
	skb_push(skb, ETH_HLEN);
	memset(skb->data, 0, ETH_HLEN);
}

/**
 * afiucv_hs_callback_syn - react on received SYN
 **/
static int afiucv_hs_callback_syn(struct sock *sk, struct sk_buff *skb)
{
	struct af_iucv_trans_hdr *trans_hdr = iucv_trans_hdr(skb);
	struct sock *nsk;
	struct iucv_sock *iucv, *niucv;
	int err;

	iucv = iucv_sk(sk);
	if (!iucv) {
		/* no sock - connection refused */
		afiucv_swap_src_dest(skb);
		trans_hdr->flags = AF_IUCV_FLAG_SYN | AF_IUCV_FLAG_FIN;
		err = dev_queue_xmit(skb);
		goto out;
	}

	nsk = iucv_sock_alloc(NULL, sk->sk_protocol, GFP_ATOMIC, 0);
	bh_lock_sock(sk);
	if ((sk->sk_state != IUCV_LISTEN) ||
	    sk_acceptq_is_full(sk) ||
	    !nsk) {
		/* error on server socket - connection refused */
		afiucv_swap_src_dest(skb);
		trans_hdr->flags = AF_IUCV_FLAG_SYN | AF_IUCV_FLAG_FIN;
		err = dev_queue_xmit(skb);
		iucv_sock_kill(nsk);
		bh_unlock_sock(sk);
		goto out;
	}

	niucv = iucv_sk(nsk);
	iucv_sock_init(nsk, sk);
	niucv->transport = AF_IUCV_TRANS_HIPER;
	niucv->msglimit = iucv->msglimit;
	if (!trans_hdr->window)
		niucv->msglimit_peer = IUCV_HIPER_MSGLIM_DEFAULT;
	else
		niucv->msglimit_peer = trans_hdr->window;
	memcpy(niucv->dst_name, trans_hdr->srcAppName, 8);
	memcpy(niucv->dst_user_id, trans_hdr->srcUserID, 8);
	memcpy(niucv->src_name, iucv->src_name, 8);
	memcpy(niucv->src_user_id, iucv->src_user_id, 8);
	nsk->sk_bound_dev_if = sk->sk_bound_dev_if;
	niucv->hs_dev = iucv->hs_dev;
	dev_hold(niucv->hs_dev);
	afiucv_swap_src_dest(skb);
	trans_hdr->flags = AF_IUCV_FLAG_SYN | AF_IUCV_FLAG_ACK;
	trans_hdr->window = niucv->msglimit;
	/* if receiver acks the xmit connection is established */
	err = dev_queue_xmit(skb);
	if (!err) {
		iucv_accept_enqueue(sk, nsk);
		nsk->sk_state = IUCV_CONNECTED;
		sk->sk_data_ready(sk);
	} else
		iucv_sock_kill(nsk);
	bh_unlock_sock(sk);

out:
	return NET_RX_SUCCESS;
}

/**
 * afiucv_hs_callback_synack() - react on received SYN-ACK
 **/
static int afiucv_hs_callback_synack(struct sock *sk, struct sk_buff *skb)
{
	struct iucv_sock *iucv = iucv_sk(sk);

	if (!iucv)
		goto out;
	if (sk->sk_state != IUCV_BOUND)
		goto out;
	bh_lock_sock(sk);
	iucv->msglimit_peer = iucv_trans_hdr(skb)->window;
	sk->sk_state = IUCV_CONNECTED;
	sk->sk_state_change(sk);
	bh_unlock_sock(sk);
out:
	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

/**
 * afiucv_hs_callback_synfin() - react on received SYN_FIN
 **/
static int afiucv_hs_callback_synfin(struct sock *sk, struct sk_buff *skb)
{
	struct iucv_sock *iucv = iucv_sk(sk);

	if (!iucv)
		goto out;
	if (sk->sk_state != IUCV_BOUND)
		goto out;
	bh_lock_sock(sk);
	sk->sk_state = IUCV_DISCONN;
	sk->sk_state_change(sk);
	bh_unlock_sock(sk);
out:
	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

/**
 * afiucv_hs_callback_fin() - react on received FIN
 **/
static int afiucv_hs_callback_fin(struct sock *sk, struct sk_buff *skb)
{
	struct iucv_sock *iucv = iucv_sk(sk);

	/* other end of connection closed */
	if (!iucv)
		goto out;
	bh_lock_sock(sk);
	if (sk->sk_state == IUCV_CONNECTED) {
		sk->sk_state = IUCV_DISCONN;
		sk->sk_state_change(sk);
	}
	bh_unlock_sock(sk);
out:
	kfree_skb(skb);
	return NET_RX_SUCCESS;
}

/**
 * afiucv_hs_callback_win() - react on received WIN
 **/
static int afiucv_hs_callback_win(struct sock *sk, struct sk_buff *skb)
{
	struct iucv_sock *iucv = iucv_sk(sk);

	if (!iucv)
		return NET_RX_SUCCESS;

	if (sk->sk_state != IUCV_CONNECTED)
		return NET_RX_SUCCESS;

	atomic_sub(iucv_trans_hdr(skb)->window, &iucv->msg_sent);
	iucv_sock_wake_msglim(sk);
	return NET_RX_SUCCESS;
}

/**
 * afiucv_hs_callback_rx() - react on received data
 **/
static int afiucv_hs_callback_rx(struct sock *sk, struct sk_buff *skb)
{
	struct iucv_sock *iucv = iucv_sk(sk);

	if (!iucv) {
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}

	if (sk->sk_state != IUCV_CONNECTED) {
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}

	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}

	/* write stuff from iucv_msg to skb cb */
	skb_pull(skb, sizeof(struct af_iucv_trans_hdr));
	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);
	IUCV_SKB_CB(skb)->offset = 0;
	if (sk_filter(sk, skb)) {
		atomic_inc(&sk->sk_drops);	/* skb rejected by filter */
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}

	spin_lock(&iucv->message_q.lock);
	if (skb_queue_empty(&iucv->backlog_skb_q)) {
		if (__sock_queue_rcv_skb(sk, skb))
			/* handle rcv queue full */
			skb_queue_tail(&iucv->backlog_skb_q, skb);
	} else
		skb_queue_tail(&iucv_sk(sk)->backlog_skb_q, skb);
	spin_unlock(&iucv->message_q.lock);
	return NET_RX_SUCCESS;
}

/**
 * afiucv_hs_rcv() - base function for arriving data through HiperSockets
 *                   transport
 *                   called from netif RX softirq
 **/
static int afiucv_hs_rcv(struct sk_buff *skb, struct net_device *dev,
	struct packet_type *pt, struct net_device *orig_dev)
{
	struct sock *sk;
	struct iucv_sock *iucv;
	struct af_iucv_trans_hdr *trans_hdr;
	int err = NET_RX_SUCCESS;
	char nullstring[8];

	if (!pskb_may_pull(skb, sizeof(*trans_hdr))) {
		WARN_ONCE(1, "AF_IUCV failed to receive skb, len=%u", skb->len);
		kfree_skb(skb);
		return NET_RX_SUCCESS;
	}

	trans_hdr = iucv_trans_hdr(skb);
	EBCASC(trans_hdr->destAppName, sizeof(trans_hdr->destAppName));
	EBCASC(trans_hdr->destUserID, sizeof(trans_hdr->destUserID));
	EBCASC(trans_hdr->srcAppName, sizeof(trans_hdr->srcAppName));
	EBCASC(trans_hdr->srcUserID, sizeof(trans_hdr->srcUserID));
	memset(nullstring, 0, sizeof(nullstring));
	iucv = NULL;
	sk = NULL;
	read_lock(&iucv_sk_list.lock);
	sk_for_each(sk, &iucv_sk_list.head) {
		if (trans_hdr->flags == AF_IUCV_FLAG_SYN) {
			if ((!memcmp(&iucv_sk(sk)->src_name,
				     trans_hdr->destAppName, 8)) &&
			    (!memcmp(&iucv_sk(sk)->src_user_id,
				     trans_hdr->destUserID, 8)) &&
			    (!memcmp(&iucv_sk(sk)->dst_name, nullstring, 8)) &&
			    (!memcmp(&iucv_sk(sk)->dst_user_id,
				     nullstring, 8))) {
				iucv = iucv_sk(sk);
				break;
			}
		} else {
			if ((!memcmp(&iucv_sk(sk)->src_name,
				     trans_hdr->destAppName, 8)) &&
			    (!memcmp(&iucv_sk(sk)->src_user_id,
				     trans_hdr->destUserID, 8)) &&
			    (!memcmp(&iucv_sk(sk)->dst_name,
				     trans_hdr->srcAppName, 8)) &&
			    (!memcmp(&iucv_sk(sk)->dst_user_id,
				     trans_hdr->srcUserID, 8))) {
				iucv = iucv_sk(sk);
				break;
			}
		}
	}
	read_unlock(&iucv_sk_list.lock);
	if (!iucv)
		sk = NULL;

	/* no sock
	how should we send with no sock
	1) send without sock no send rc checking?
	2) introduce default sock to handle this cases

	 SYN -> send SYN|ACK in good case, send SYN|FIN in bad case
	 data -> send FIN
	 SYN|ACK, SYN|FIN, FIN -> no action? */

	switch (trans_hdr->flags) {
	case AF_IUCV_FLAG_SYN:
		/* connect request */
		err = afiucv_hs_callback_syn(sk, skb);
		break;
	case (AF_IUCV_FLAG_SYN | AF_IUCV_FLAG_ACK):
		/* connect request confirmed */
		err = afiucv_hs_callback_synack(sk, skb);
		break;
	case (AF_IUCV_FLAG_SYN | AF_IUCV_FLAG_FIN):
		/* connect request refused */
		err = afiucv_hs_callback_synfin(sk, skb);
		break;
	case (AF_IUCV_FLAG_FIN):
		/* close request */
		err = afiucv_hs_callback_fin(sk, skb);
		break;
	case (AF_IUCV_FLAG_WIN):
		err = afiucv_hs_callback_win(sk, skb);
		if (skb->len == sizeof(struct af_iucv_trans_hdr)) {
			kfree_skb(skb);
			break;
		}
		fallthrough;	/* and receive non-zero length data */
	case (AF_IUCV_FLAG_SHT):
		/* shutdown request */
		fallthrough;	/* and receive zero length data */
	case 0:
		/* plain data frame */
		IUCV_SKB_CB(skb)->class = trans_hdr->iucv_hdr.class;
		err = afiucv_hs_callback_rx(sk, skb);
		break;
	default:
		kfree_skb(skb);
	}

	return err;
}

/**
 * afiucv_hs_callback_txnotify() - handle send notifcations from HiperSockets
 *                                 transport
 **/
static void afiucv_hs_callback_txnotify(struct sk_buff *skb,
					enum iucv_tx_notify n)
{
	struct sock *isk = skb->sk;
	struct sock *sk = NULL;
	struct iucv_sock *iucv = NULL;
	struct sk_buff_head *list;
	struct sk_buff *list_skb;
	struct sk_buff *nskb;
	unsigned long flags;

	read_lock_irqsave(&iucv_sk_list.lock, flags);
	sk_for_each(sk, &iucv_sk_list.head)
		if (sk == isk) {
			iucv = iucv_sk(sk);
			break;
		}
	read_unlock_irqrestore(&iucv_sk_list.lock, flags);

	if (!iucv || sock_flag(sk, SOCK_ZAPPED))
		return;

	list = &iucv->send_skb_q;
	spin_lock_irqsave(&list->lock, flags);
	skb_queue_walk_safe(list, list_skb, nskb) {
		if (skb_shinfo(list_skb) == skb_shinfo(skb)) {
			switch (n) {
			case TX_NOTIFY_OK:
				__skb_unlink(list_skb, list);
				kfree_skb(list_skb);
				iucv_sock_wake_msglim(sk);
				break;
			case TX_NOTIFY_PENDING:
				atomic_inc(&iucv->pendings);
				break;
			case TX_NOTIFY_DELAYED_OK:
				__skb_unlink(list_skb, list);
				atomic_dec(&iucv->pendings);
				if (atomic_read(&iucv->pendings) <= 0)
					iucv_sock_wake_msglim(sk);
				kfree_skb(list_skb);
				break;
			case TX_NOTIFY_UNREACHABLE:
			case TX_NOTIFY_DELAYED_UNREACHABLE:
			case TX_NOTIFY_TPQFULL: /* not yet used */
			case TX_NOTIFY_GENERALERROR:
			case TX_NOTIFY_DELAYED_GENERALERROR:
				__skb_unlink(list_skb, list);
				kfree_skb(list_skb);
				if (sk->sk_state == IUCV_CONNECTED) {
					sk->sk_state = IUCV_DISCONN;
					sk->sk_state_change(sk);
				}
				break;
			}
			break;
		}
	}
	spin_unlock_irqrestore(&list->lock, flags);

	if (sk->sk_state == IUCV_CLOSING) {
		if (skb_queue_empty(&iucv_sk(sk)->send_skb_q)) {
			sk->sk_state = IUCV_CLOSED;
			sk->sk_state_change(sk);
		}
	}

}

/*
 * afiucv_netdev_event: handle netdev notifier chain events
 */
static int afiucv_netdev_event(struct notifier_block *this,
			       unsigned long event, void *ptr)
{
	struct net_device *event_dev = netdev_notifier_info_to_dev(ptr);
	struct sock *sk;
	struct iucv_sock *iucv;

	switch (event) {
	case NETDEV_REBOOT:
	case NETDEV_GOING_DOWN:
		sk_for_each(sk, &iucv_sk_list.head) {
			iucv = iucv_sk(sk);
			if ((iucv->hs_dev == event_dev) &&
			    (sk->sk_state == IUCV_CONNECTED)) {
				if (event == NETDEV_GOING_DOWN)
					iucv_send_ctrl(sk, AF_IUCV_FLAG_FIN);
				sk->sk_state = IUCV_DISCONN;
				sk->sk_state_change(sk);
			}
		}
		break;
	case NETDEV_DOWN:
	case NETDEV_UNREGISTER:
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block afiucv_netdev_notifier = {
	.notifier_call = afiucv_netdev_event,
};

static const struct proto_ops iucv_sock_ops = {
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
	.setsockopt	= iucv_sock_setsockopt,
	.getsockopt	= iucv_sock_getsockopt,
};

static int iucv_sock_create(struct net *net, struct socket *sock, int protocol,
			    int kern)
{
	struct sock *sk;

	if (protocol && protocol != PF_IUCV)
		return -EPROTONOSUPPORT;

	sock->state = SS_UNCONNECTED;

	switch (sock->type) {
	case SOCK_STREAM:
	case SOCK_SEQPACKET:
		/* currently, proto ops can handle both sk types */
		sock->ops = &iucv_sock_ops;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	sk = iucv_sock_alloc(sock, protocol, GFP_KERNEL, kern);
	if (!sk)
		return -ENOMEM;

	iucv_sock_init(sk, NULL);

	return 0;
}

static const struct net_proto_family iucv_sock_family_ops = {
	.family	= AF_IUCV,
	.owner	= THIS_MODULE,
	.create	= iucv_sock_create,
};

static struct packet_type iucv_packet_type = {
	.type = cpu_to_be16(ETH_P_AF_IUCV),
	.func = afiucv_hs_rcv,
};

static int afiucv_iucv_init(void)
{
	return pr_iucv->iucv_register(&af_iucv_handler, 0);
}

static void afiucv_iucv_exit(void)
{
	pr_iucv->iucv_unregister(&af_iucv_handler, 0);
}

static int __init afiucv_init(void)
{
	int err;

	if (MACHINE_IS_VM) {
		cpcmd("QUERY USERID", iucv_userid, sizeof(iucv_userid), &err);
		if (unlikely(err)) {
			WARN_ON(err);
			err = -EPROTONOSUPPORT;
			goto out;
		}

		pr_iucv = try_then_request_module(symbol_get(iucv_if), "iucv");
		if (!pr_iucv) {
			printk(KERN_WARNING "iucv_if lookup failed\n");
			memset(&iucv_userid, 0, sizeof(iucv_userid));
		}
	} else {
		memset(&iucv_userid, 0, sizeof(iucv_userid));
		pr_iucv = NULL;
	}

	err = proto_register(&iucv_proto, 0);
	if (err)
		goto out;
	err = sock_register(&iucv_sock_family_ops);
	if (err)
		goto out_proto;

	if (pr_iucv) {
		err = afiucv_iucv_init();
		if (err)
			goto out_sock;
	}

	err = register_netdevice_notifier(&afiucv_netdev_notifier);
	if (err)
		goto out_notifier;

	dev_add_pack(&iucv_packet_type);
	return 0;

out_notifier:
	if (pr_iucv)
		afiucv_iucv_exit();
out_sock:
	sock_unregister(PF_IUCV);
out_proto:
	proto_unregister(&iucv_proto);
out:
	if (pr_iucv)
		symbol_put(iucv_if);
	return err;
}

static void __exit afiucv_exit(void)
{
	if (pr_iucv) {
		afiucv_iucv_exit();
		symbol_put(iucv_if);
	}

	unregister_netdevice_notifier(&afiucv_netdev_notifier);
	dev_remove_pack(&iucv_packet_type);
	sock_unregister(PF_IUCV);
	proto_unregister(&iucv_proto);
}

module_init(afiucv_init);
module_exit(afiucv_exit);

MODULE_AUTHOR("Jennifer Hunt <jenhunt@us.ibm.com>");
MODULE_DESCRIPTION("IUCV Sockets ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_IUCV);
