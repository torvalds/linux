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

#define VERSION "1.1"

static char iucv_userid[80];

static const struct proto_ops iucv_sock_ops;

static struct proto iucv_proto = {
	.name		= "AF_IUCV",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct iucv_sock),
};

/* special AF_IUCV IPRM messages */
static const u8 iprm_shutdown[8] =
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};

#define TRGCLS_SIZE	(sizeof(((struct iucv_message *)0)->class))

/* macros to set/get socket control buffer at correct offset */
#define CB_TAG(skb)	((skb)->cb)		/* iucv message tag */
#define CB_TAG_LEN	(sizeof(((struct iucv_message *) 0)->tag))
#define CB_TRGCLS(skb)	((skb)->cb + CB_TAG_LEN) /* iucv msg target class */
#define CB_TRGCLS_LEN	(TRGCLS_SIZE)

#define __iucv_sock_wait(sk, condition, timeo, ret)			\
do {									\
	DEFINE_WAIT(__wait);						\
	long __timeo = timeo;						\
	ret = 0;							\
	prepare_to_wait(sk->sk_sleep, &__wait, TASK_INTERRUPTIBLE);	\
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
	finish_wait(sk->sk_sleep, &__wait);				\
} while (0)

#define iucv_sock_wait(sk, condition, timeo)				\
({									\
	int __ret = 0;							\
	if (!(condition))						\
		__iucv_sock_wait(sk, condition, timeo, __ret);		\
	__ret;								\
})

static void iucv_sock_kill(struct sock *sk);
static void iucv_sock_close(struct sock *sk);

/* Call Back functions */
static void iucv_callback_rx(struct iucv_path *, struct iucv_message *);
static void iucv_callback_txdone(struct iucv_path *, struct iucv_message *);
static void iucv_callback_connack(struct iucv_path *, u8 ipuser[16]);
static int iucv_callback_connreq(struct iucv_path *, u8 ipvmid[8],
				 u8 ipuser[16]);
static void iucv_callback_connrej(struct iucv_path *, u8 ipuser[16]);
static void iucv_callback_shutdown(struct iucv_path *, u8 ipuser[16]);

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

static int afiucv_pm_prepare(struct device *dev)
{
#ifdef CONFIG_PM_DEBUG
	printk(KERN_WARNING "afiucv_pm_prepare\n");
#endif
	return 0;
}

static void afiucv_pm_complete(struct device *dev)
{
#ifdef CONFIG_PM_DEBUG
	printk(KERN_WARNING "afiucv_pm_complete\n");
#endif
	return;
}

/**
 * afiucv_pm_freeze() - Freeze PM callback
 * @dev:	AFIUCV dummy device
 *
 * Sever all established IUCV communication pathes
 */
static int afiucv_pm_freeze(struct device *dev)
{
	struct iucv_sock *iucv;
	struct sock *sk;
	struct hlist_node *node;
	int err = 0;

#ifdef CONFIG_PM_DEBUG
	printk(KERN_WARNING "afiucv_pm_freeze\n");
#endif
	read_lock(&iucv_sk_list.lock);
	sk_for_each(sk, node, &iucv_sk_list.head) {
		iucv = iucv_sk(sk);
		skb_queue_purge(&iucv->send_skb_q);
		skb_queue_purge(&iucv->backlog_skb_q);
		switch (sk->sk_state) {
		case IUCV_SEVERED:
		case IUCV_DISCONN:
		case IUCV_CLOSING:
		case IUCV_CONNECTED:
			if (iucv->path) {
				err = iucv_path_sever(iucv->path, NULL);
				iucv_path_free(iucv->path);
				iucv->path = NULL;
			}
			break;
		case IUCV_OPEN:
		case IUCV_BOUND:
		case IUCV_LISTEN:
		case IUCV_CLOSED:
		default:
			break;
		}
	}
	read_unlock(&iucv_sk_list.lock);
	return err;
}

/**
 * afiucv_pm_restore_thaw() - Thaw and restore PM callback
 * @dev:	AFIUCV dummy device
 *
 * socket clean up after freeze
 */
static int afiucv_pm_restore_thaw(struct device *dev)
{
	struct iucv_sock *iucv;
	struct sock *sk;
	struct hlist_node *node;

#ifdef CONFIG_PM_DEBUG
	printk(KERN_WARNING "afiucv_pm_restore_thaw\n");
#endif
	read_lock(&iucv_sk_list.lock);
	sk_for_each(sk, node, &iucv_sk_list.head) {
		iucv = iucv_sk(sk);
		switch (sk->sk_state) {
		case IUCV_CONNECTED:
			sk->sk_err = EPIPE;
			sk->sk_state = IUCV_DISCONN;
			sk->sk_state_change(sk);
			break;
		case IUCV_DISCONN:
		case IUCV_SEVERED:
		case IUCV_CLOSING:
		case IUCV_LISTEN:
		case IUCV_BOUND:
		case IUCV_OPEN:
		default:
			break;
		}
	}
	read_unlock(&iucv_sk_list.lock);
	return 0;
}

static struct dev_pm_ops afiucv_pm_ops = {
	.prepare = afiucv_pm_prepare,
	.complete = afiucv_pm_complete,
	.freeze = afiucv_pm_freeze,
	.thaw = afiucv_pm_restore_thaw,
	.restore = afiucv_pm_restore_thaw,
};

static struct device_driver af_iucv_driver = {
	.owner = THIS_MODULE,
	.name = "afiucv",
	.bus  = &iucv_bus,
	.pm   = &afiucv_pm_ops,
};

/* dummy device used as trigger for PM functions */
static struct device *af_iucv_dev;

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
 * The socket data length is computed by substracting the socket data length
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
	return (skb_queue_len(&iucv->send_skb_q) < iucv->path->msglim);
}

/**
 * iucv_sock_wake_msglim() - Wake up thread waiting on msg limit
 */
static void iucv_sock_wake_msglim(struct sock *sk)
{
	read_lock(&sk->sk_callback_lock);
	if (sk_has_sleeper(sk))
		wake_up_interruptible_all(sk->sk_sleep);
	sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	read_unlock(&sk->sk_callback_lock);
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
			err = iucv_sock_wait(sk,
					iucv_sock_in_state(sk, IUCV_CLOSED, 0),
					timeo);
		}

	case IUCV_CLOSING:   /* fall through */
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
		break;

	default:
		/* nothing to do here */
		break;
	}

	/* mark socket for deletion by iucv_sock_kill() */
	sock_set_flag(sk, SOCK_ZAPPED);

	release_sock(sk);
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
	iucv_sk(sk)->flags = 0;
	iucv_sk(sk)->msglimit = IUCV_QUEUELEN_DEFAULT;
	iucv_sk(sk)->path = NULL;
	memset(&iucv_sk(sk)->src_user_id , 0, 32);

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
static int iucv_sock_create(struct net *net, struct socket *sock, int protocol,
			    int kern)
{
	struct sock *sk;

	if (protocol && protocol != PF_IUCV)
		return -EPROTONOSUPPORT;

	sock->state = SS_UNCONNECTED;

	switch (sock->type) {
	case SOCK_STREAM:
		sock->ops = &iucv_sock_ops;
		break;
	case SOCK_SEQPACKET:
		/* currently, proto ops can handle both sk types */
		sock->ops = &iucv_sock_ops;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

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
	sk_acceptq_added(parent);
}

void iucv_accept_unlink(struct sock *sk)
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
		    sk->sk_state == IUCV_DISCONN ||	/* due to PM restore */
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

	if (sk->sk_type != SOCK_STREAM && sk->sk_type != SOCK_SEQPACKET)
		return -EINVAL;

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
	iucv->path = iucv_path_alloc(iucv->msglimit,
				     IUCV_IPRMDATA, GFP_KERNEL);
	if (!iucv->path) {
		err = -ENOMEM;
		goto done;
	}
	err = iucv_path_connect(iucv->path, &af_iucv_handler,
				sa->siucv_user_id, NULL, user_data, sk);
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
		goto done;
	}

	if (sk->sk_state != IUCV_CONNECTED) {
		err = iucv_sock_wait(sk, iucv_sock_in_state(sk, IUCV_CONNECTED,
							    IUCV_DISCONN),
				     sock_sndtimeo(sk, flags & O_NONBLOCK));
	}

	if (sk->sk_state == IUCV_DISCONN) {
		err = -ECONNREFUSED;
	}

	if (err) {
		iucv_path_sever(iucv->path, NULL);
		iucv_path_free(iucv->path);
		iucv->path = NULL;
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
	return iucv_message_send(path, msg, IUCV_IPRMDATA, 0,
				 (void *) prmdata, 8);
}

static int iucv_sock_sendmsg(struct kiocb *iocb, struct socket *sock,
			     struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	struct sk_buff *skb;
	struct iucv_message txmsg;
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
	txmsg.class = 0;

	/* iterate over control messages */
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg;
		cmsg = CMSG_NXTHDR(msg, cmsg)) {

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
			break;
		}
	}

	/* allocate one skb for each iucv message:
	 * this is fine for SOCK_SEQPACKET (unless we want to support
	 * segmented records using the MSG_EOR flag), but
	 * for SOCK_STREAM we might want to improve it in future */
	skb = sock_alloc_send_skb(sk, len, noblock, &err);
	if (!skb)
		goto out;
	if (memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len)) {
		err = -EFAULT;
		goto fail;
	}

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
	memcpy(CB_TAG(skb), &txmsg.tag, CB_TAG_LEN);
	skb_queue_tail(&iucv->send_skb_q, skb);

	if (((iucv->path->flags & IUCV_IPRMDATA) & iucv->flags)
	      && skb->len <= 7) {
		err = iucv_send_iprm(iucv->path, &txmsg, skb);

		/* on success: there is no message_complete callback
		 * for an IPRMDATA msg; remove skb from send queue */
		if (err == 0) {
			skb_unlink(skb, &iucv->send_skb_q);
			kfree_skb(skb);
		}

		/* this error should never happen since the
		 * IUCV_IPRMDATA path flag is set... sever path */
		if (err == 0x15) {
			iucv_path_sever(iucv->path, NULL);
			skb_unlink(skb, &iucv->send_skb_q);
			err = -EPIPE;
			goto fail;
		}
	} else
		err = iucv_message_send(iucv->path, &txmsg, 0, 0,
					(void *) skb->data, skb->len);
	if (err) {
		if (err == 3) {
			user_id[8] = 0;
			memcpy(user_id, iucv->dst_user_id, 8);
			appl_id[8] = 0;
			memcpy(appl_id, iucv->dst_name, 8);
			pr_err("Application %s on z/VM guest %s"
				" exceeds message limit\n",
				appl_id, user_id);
			err = -EAGAIN;
		} else
			err = -EPIPE;
		skb_unlink(skb, &iucv->send_skb_q);
		goto fail;
	}

	release_sock(sk);
	return len;

fail:
	kfree_skb(skb);
out:
	release_sock(sk);
	return err;
}

/* iucv_fragment_skb() - Fragment a single IUCV message into multiple skb's
 *
 * Locking: must be called with message_q.lock held
 */
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

		/* copy target class to control buffer of new skb */
		memcpy(CB_TRGCLS(nskb), CB_TRGCLS(skb), CB_TRGCLS_LEN);

		/* copy data fragment */
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
	memcpy(CB_TRGCLS(skb), &msg->class, CB_TRGCLS_LEN);

	/* check for special IPRM messages (e.g. iucv_sock_shutdown) */
	if ((msg->flags & IUCV_IPRMDATA) && len > 7) {
		if (memcmp(msg->rmmsg, iprm_shutdown, 8) == 0) {
			skb->data = NULL;
			skb->len = 0;
		}
	} else {
		rc = iucv_message_receive(path, msg, msg->flags & IUCV_IPRMDATA,
					  skb->data, len, NULL);
		if (rc) {
			kfree_skb(skb);
			return;
		}
		/* we need to fragment iucv messages for SOCK_STREAM only;
		 * for SOCK_SEQPACKET, it is only relevant if we support
		 * record segmentation using MSG_EOR (see also recvmsg()) */
		if (sk->sk_type == SOCK_STREAM &&
		    skb->truesize >= sk->sk_rcvbuf / 4) {
			rc = iucv_fragment_skb(sk, skb, len);
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
			skb->len = len;
		}
	}

	if (sock_queue_rcv_skb(sk, skb))
		skb_queue_head(&iucv_sk(sk)->backlog_skb_q, skb);
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
		skb = alloc_skb(iucv_msg_length(&p->msg), GFP_ATOMIC | GFP_DMA);
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
	unsigned int copied, rlen;
	struct sk_buff *skb, *rskb, *cskb;
	int err = 0;

	if ((sk->sk_state == IUCV_DISCONN || sk->sk_state == IUCV_SEVERED) &&
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

	rlen   = skb->len;		/* real length of skb */
	copied = min_t(unsigned int, rlen, len);

	cskb = skb;
	if (memcpy_toiovec(msg->msg_iov, cskb->data, copied)) {
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
			CB_TRGCLS_LEN, CB_TRGCLS(skb));
	if (err) {
		if (!(flags & MSG_PEEK))
			skb_queue_head(&sk->sk_receive_queue, skb);
		return err;
	}

	/* Mark read part of skb as used */
	if (!(flags & MSG_PEEK)) {

		/* SOCK_STREAM: re-queue skb if it contains unreceived data */
		if (sk->sk_type == SOCK_STREAM) {
			skb_pull(skb, copied);
			if (skb->len) {
				skb_queue_head(&sk->sk_receive_queue, skb);
				goto done;
			}
		}

		kfree_skb(skb);

		/* Queue backlog skbs */
		spin_lock_bh(&iucv->message_q.lock);
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
			if (!list_empty(&iucv->message_q.list))
				iucv_process_message_q(sk);
		}
		spin_unlock_bh(&iucv->message_q.lock);
	}

done:
	/* SOCK_SEQPACKET: return real length if MSG_TRUNC is set */
	if (sk->sk_type == SOCK_SEQPACKET && (flags & MSG_TRUNC))
		copied = rlen;

	return copied;
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

	sock_poll_wait(file, sk->sk_sleep, wait);

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

	how++;

	if ((how & ~SHUTDOWN_MASK) || !how)
		return -EINVAL;

	lock_sock(sk);
	switch (sk->sk_state) {
	case IUCV_DISCONN:
	case IUCV_CLOSING:
	case IUCV_SEVERED:
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
					(void *) iprm_shutdown, 8);
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

/* getsockopt and setsockopt */
static int iucv_sock_setsockopt(struct socket *sock, int level, int optname,
				char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct iucv_sock *iucv = iucv_sk(sk);
	int val;
	int rc;

	if (level != SOL_IUCV)
		return -ENOPROTOOPT;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *) optval))
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
			if (val < 1 || val > (u16)(~0))
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
	int val, len;

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
		iucv_path_free(path);
		goto fail;
	}

	/* Check for backlog size */
	if (sk_acceptq_is_full(sk)) {
		err = iucv_path_sever(path, user_data);
		iucv_path_free(path);
		goto fail;
	}

	/* Create the new socket */
	nsk = iucv_sock_alloc(NULL, sk->sk_type, GFP_ATOMIC);
	if (!nsk) {
		err = iucv_path_sever(path, user_data);
		iucv_path_free(path);
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

	/* set message limit for path based on msglimit of accepting socket */
	niucv->msglimit = iucv->msglimit;
	path->msglim = iucv->msglimit;
	err = iucv_path_accept(path, &af_iucv_handler, nuser_data, nsk);
	if (err) {
		err = iucv_path_sever(path, user_data);
		iucv_path_free(path);
		iucv_sock_kill(nsk);
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

	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		iucv_message_reject(path, msg);
		return;
	}

	spin_lock(&iucv->message_q.lock);

	if (!list_empty(&iucv->message_q.list) ||
	    !skb_queue_empty(&iucv->backlog_skb_q))
		goto save_message;

	len = atomic_read(&sk->sk_rmem_alloc);
	len += iucv_msg_length(msg) + sizeof(struct sk_buff);
	if (len > sk->sk_rcvbuf)
		goto save_message;

	skb = alloc_skb(iucv_msg_length(msg), GFP_ATOMIC | GFP_DMA);
	if (!skb)
		goto save_message;

	iucv_process_message(sk, skb, path, msg);
	goto out_unlock;

save_message:
	save_msg = kzalloc(sizeof(struct sock_msg_q), GFP_ATOMIC | GFP_DMA);
	if (!save_msg)
		return;
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
	struct sk_buff *list_skb = list->next;
	unsigned long flags;

	if (!skb_queue_empty(list)) {
		spin_lock_irqsave(&list->lock, flags);

		while (list_skb != (struct sk_buff *)list) {
			if (!memcmp(&msg->tag, CB_TAG(list_skb), CB_TAG_LEN)) {
				this = list_skb;
				break;
			}
			list_skb = list_skb->next;
		}
		if (this)
			__skb_unlink(this, list);

		spin_unlock_irqrestore(&list->lock, flags);

		if (this) {
			kfree_skb(this);
			/* wake up any process waiting for sending */
			iucv_sock_wake_msglim(sk);
		}
	}
	BUG_ON(!this);

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

static const struct net_proto_family iucv_sock_family_ops = {
	.family	= AF_IUCV,
	.owner	= THIS_MODULE,
	.create	= iucv_sock_create,
};

static int __init afiucv_init(void)
{
	int err;

	if (!MACHINE_IS_VM) {
		pr_err("The af_iucv module cannot be loaded"
		       " without z/VM\n");
		err = -EPROTONOSUPPORT;
		goto out;
	}
	cpcmd("QUERY USERID", iucv_userid, sizeof(iucv_userid), &err);
	if (unlikely(err)) {
		WARN_ON(err);
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
	/* establish dummy device */
	err = driver_register(&af_iucv_driver);
	if (err)
		goto out_sock;
	af_iucv_dev = kzalloc(sizeof(struct device), GFP_KERNEL);
	if (!af_iucv_dev) {
		err = -ENOMEM;
		goto out_driver;
	}
	dev_set_name(af_iucv_dev, "af_iucv");
	af_iucv_dev->bus = &iucv_bus;
	af_iucv_dev->parent = iucv_root;
	af_iucv_dev->release = (void (*)(struct device *))kfree;
	af_iucv_dev->driver = &af_iucv_driver;
	err = device_register(af_iucv_dev);
	if (err)
		goto out_driver;

	return 0;

out_driver:
	driver_unregister(&af_iucv_driver);
out_sock:
	sock_unregister(PF_IUCV);
out_proto:
	proto_unregister(&iucv_proto);
out_iucv:
	iucv_unregister(&af_iucv_handler, 0);
out:
	return err;
}

static void __exit afiucv_exit(void)
{
	device_unregister(af_iucv_dev);
	driver_unregister(&af_iucv_driver);
	sock_unregister(PF_IUCV);
	proto_unregister(&iucv_proto);
	iucv_unregister(&af_iucv_handler, 0);
}

module_init(afiucv_init);
module_exit(afiucv_exit);

MODULE_AUTHOR("Jennifer Hunt <jenhunt@us.ibm.com>");
MODULE_DESCRIPTION("IUCV Sockets ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(PF_IUCV);
