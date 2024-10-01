// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/crc32c.h>
#include <linux/ctype.h>
#include <linux/highmem.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/nsproxy.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#ifdef	CONFIG_BLOCK
#include <linux/bio.h>
#endif	/* CONFIG_BLOCK */
#include <linux/dns_resolver.h>
#include <net/tcp.h>

#include <linux/ceph/ceph_features.h>
#include <linux/ceph/libceph.h>
#include <linux/ceph/messenger.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/pagelist.h>
#include <linux/export.h>

/*
 * Ceph uses the messenger to exchange ceph_msg messages with other
 * hosts in the system.  The messenger provides ordered and reliable
 * delivery.  We tolerate TCP disconnects by reconnecting (with
 * exponential backoff) in the case of a fault (disconnection, bad
 * crc, protocol error).  Acks allow sent messages to be discarded by
 * the sender.
 */

/*
 * We track the state of the socket on a given connection using
 * values defined below.  The transition to a new socket state is
 * handled by a function which verifies we aren't coming from an
 * unexpected state.
 *
 *      --------
 *      | NEW* |  transient initial state
 *      --------
 *          | con_sock_state_init()
 *          v
 *      ----------
 *      | CLOSED |  initialized, but no socket (and no
 *      ----------  TCP connection)
 *       ^      \
 *       |       \ con_sock_state_connecting()
 *       |        ----------------------
 *       |                              \
 *       + con_sock_state_closed()       \
 *       |+---------------------------    \
 *       | \                          \    \
 *       |  -----------                \    \
 *       |  | CLOSING |  socket event;  \    \
 *       |  -----------  await close     \    \
 *       |       ^                        \   |
 *       |       |                         \  |
 *       |       + con_sock_state_closing() \ |
 *       |      / \                         | |
 *       |     /   ---------------          | |
 *       |    /                   \         v v
 *       |   /                    --------------
 *       |  /    -----------------| CONNECTING |  socket created, TCP
 *       |  |   /                 --------------  connect initiated
 *       |  |   | con_sock_state_connected()
 *       |  |   v
 *      -------------
 *      | CONNECTED |  TCP connection established
 *      -------------
 *
 * State values for ceph_connection->sock_state; NEW is assumed to be 0.
 */

#define CON_SOCK_STATE_NEW		0	/* -> CLOSED */
#define CON_SOCK_STATE_CLOSED		1	/* -> CONNECTING */
#define CON_SOCK_STATE_CONNECTING	2	/* -> CONNECTED or -> CLOSING */
#define CON_SOCK_STATE_CONNECTED	3	/* -> CLOSING or -> CLOSED */
#define CON_SOCK_STATE_CLOSING		4	/* -> CLOSED */

static bool con_flag_valid(unsigned long con_flag)
{
	switch (con_flag) {
	case CEPH_CON_F_LOSSYTX:
	case CEPH_CON_F_KEEPALIVE_PENDING:
	case CEPH_CON_F_WRITE_PENDING:
	case CEPH_CON_F_SOCK_CLOSED:
	case CEPH_CON_F_BACKOFF:
		return true;
	default:
		return false;
	}
}

void ceph_con_flag_clear(struct ceph_connection *con, unsigned long con_flag)
{
	BUG_ON(!con_flag_valid(con_flag));

	clear_bit(con_flag, &con->flags);
}

void ceph_con_flag_set(struct ceph_connection *con, unsigned long con_flag)
{
	BUG_ON(!con_flag_valid(con_flag));

	set_bit(con_flag, &con->flags);
}

bool ceph_con_flag_test(struct ceph_connection *con, unsigned long con_flag)
{
	BUG_ON(!con_flag_valid(con_flag));

	return test_bit(con_flag, &con->flags);
}

bool ceph_con_flag_test_and_clear(struct ceph_connection *con,
				  unsigned long con_flag)
{
	BUG_ON(!con_flag_valid(con_flag));

	return test_and_clear_bit(con_flag, &con->flags);
}

bool ceph_con_flag_test_and_set(struct ceph_connection *con,
				unsigned long con_flag)
{
	BUG_ON(!con_flag_valid(con_flag));

	return test_and_set_bit(con_flag, &con->flags);
}

/* Slab caches for frequently-allocated structures */

static struct kmem_cache	*ceph_msg_cache;

#ifdef CONFIG_LOCKDEP
static struct lock_class_key socket_class;
#endif

static void queue_con(struct ceph_connection *con);
static void cancel_con(struct ceph_connection *con);
static void ceph_con_workfn(struct work_struct *);
static void con_fault(struct ceph_connection *con);

/*
 * Nicely render a sockaddr as a string.  An array of formatted
 * strings is used, to approximate reentrancy.
 */
#define ADDR_STR_COUNT_LOG	5	/* log2(# address strings in array) */
#define ADDR_STR_COUNT		(1 << ADDR_STR_COUNT_LOG)
#define ADDR_STR_COUNT_MASK	(ADDR_STR_COUNT - 1)
#define MAX_ADDR_STR_LEN	64	/* 54 is enough */

static char addr_str[ADDR_STR_COUNT][MAX_ADDR_STR_LEN];
static atomic_t addr_str_seq = ATOMIC_INIT(0);

struct page *ceph_zero_page;		/* used in certain error cases */

const char *ceph_pr_addr(const struct ceph_entity_addr *addr)
{
	int i;
	char *s;
	struct sockaddr_storage ss = addr->in_addr; /* align */
	struct sockaddr_in *in4 = (struct sockaddr_in *)&ss;
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&ss;

	i = atomic_inc_return(&addr_str_seq) & ADDR_STR_COUNT_MASK;
	s = addr_str[i];

	switch (ss.ss_family) {
	case AF_INET:
		snprintf(s, MAX_ADDR_STR_LEN, "(%d)%pI4:%hu",
			 le32_to_cpu(addr->type), &in4->sin_addr,
			 ntohs(in4->sin_port));
		break;

	case AF_INET6:
		snprintf(s, MAX_ADDR_STR_LEN, "(%d)[%pI6c]:%hu",
			 le32_to_cpu(addr->type), &in6->sin6_addr,
			 ntohs(in6->sin6_port));
		break;

	default:
		snprintf(s, MAX_ADDR_STR_LEN, "(unknown sockaddr family %hu)",
			 ss.ss_family);
	}

	return s;
}
EXPORT_SYMBOL(ceph_pr_addr);

void ceph_encode_my_addr(struct ceph_messenger *msgr)
{
	if (!ceph_msgr2(from_msgr(msgr))) {
		memcpy(&msgr->my_enc_addr, &msgr->inst.addr,
		       sizeof(msgr->my_enc_addr));
		ceph_encode_banner_addr(&msgr->my_enc_addr);
	}
}

/*
 * work queue for all reading and writing to/from the socket.
 */
static struct workqueue_struct *ceph_msgr_wq;

static int ceph_msgr_slab_init(void)
{
	BUG_ON(ceph_msg_cache);
	ceph_msg_cache = KMEM_CACHE(ceph_msg, 0);
	if (!ceph_msg_cache)
		return -ENOMEM;

	return 0;
}

static void ceph_msgr_slab_exit(void)
{
	BUG_ON(!ceph_msg_cache);
	kmem_cache_destroy(ceph_msg_cache);
	ceph_msg_cache = NULL;
}

static void _ceph_msgr_exit(void)
{
	if (ceph_msgr_wq) {
		destroy_workqueue(ceph_msgr_wq);
		ceph_msgr_wq = NULL;
	}

	BUG_ON(!ceph_zero_page);
	put_page(ceph_zero_page);
	ceph_zero_page = NULL;

	ceph_msgr_slab_exit();
}

int __init ceph_msgr_init(void)
{
	if (ceph_msgr_slab_init())
		return -ENOMEM;

	BUG_ON(ceph_zero_page);
	ceph_zero_page = ZERO_PAGE(0);
	get_page(ceph_zero_page);

	/*
	 * The number of active work items is limited by the number of
	 * connections, so leave @max_active at default.
	 */
	ceph_msgr_wq = alloc_workqueue("ceph-msgr", WQ_MEM_RECLAIM, 0);
	if (ceph_msgr_wq)
		return 0;

	pr_err("msgr_init failed to create workqueue\n");
	_ceph_msgr_exit();

	return -ENOMEM;
}

void ceph_msgr_exit(void)
{
	BUG_ON(ceph_msgr_wq == NULL);

	_ceph_msgr_exit();
}

void ceph_msgr_flush(void)
{
	flush_workqueue(ceph_msgr_wq);
}
EXPORT_SYMBOL(ceph_msgr_flush);

/* Connection socket state transition functions */

static void con_sock_state_init(struct ceph_connection *con)
{
	int old_state;

	old_state = atomic_xchg(&con->sock_state, CON_SOCK_STATE_CLOSED);
	if (WARN_ON(old_state != CON_SOCK_STATE_NEW))
		printk("%s: unexpected old state %d\n", __func__, old_state);
	dout("%s con %p sock %d -> %d\n", __func__, con, old_state,
	     CON_SOCK_STATE_CLOSED);
}

static void con_sock_state_connecting(struct ceph_connection *con)
{
	int old_state;

	old_state = atomic_xchg(&con->sock_state, CON_SOCK_STATE_CONNECTING);
	if (WARN_ON(old_state != CON_SOCK_STATE_CLOSED))
		printk("%s: unexpected old state %d\n", __func__, old_state);
	dout("%s con %p sock %d -> %d\n", __func__, con, old_state,
	     CON_SOCK_STATE_CONNECTING);
}

static void con_sock_state_connected(struct ceph_connection *con)
{
	int old_state;

	old_state = atomic_xchg(&con->sock_state, CON_SOCK_STATE_CONNECTED);
	if (WARN_ON(old_state != CON_SOCK_STATE_CONNECTING))
		printk("%s: unexpected old state %d\n", __func__, old_state);
	dout("%s con %p sock %d -> %d\n", __func__, con, old_state,
	     CON_SOCK_STATE_CONNECTED);
}

static void con_sock_state_closing(struct ceph_connection *con)
{
	int old_state;

	old_state = atomic_xchg(&con->sock_state, CON_SOCK_STATE_CLOSING);
	if (WARN_ON(old_state != CON_SOCK_STATE_CONNECTING &&
			old_state != CON_SOCK_STATE_CONNECTED &&
			old_state != CON_SOCK_STATE_CLOSING))
		printk("%s: unexpected old state %d\n", __func__, old_state);
	dout("%s con %p sock %d -> %d\n", __func__, con, old_state,
	     CON_SOCK_STATE_CLOSING);
}

static void con_sock_state_closed(struct ceph_connection *con)
{
	int old_state;

	old_state = atomic_xchg(&con->sock_state, CON_SOCK_STATE_CLOSED);
	if (WARN_ON(old_state != CON_SOCK_STATE_CONNECTED &&
		    old_state != CON_SOCK_STATE_CLOSING &&
		    old_state != CON_SOCK_STATE_CONNECTING &&
		    old_state != CON_SOCK_STATE_CLOSED))
		printk("%s: unexpected old state %d\n", __func__, old_state);
	dout("%s con %p sock %d -> %d\n", __func__, con, old_state,
	     CON_SOCK_STATE_CLOSED);
}

/*
 * socket callback functions
 */

/* data available on socket, or listen socket received a connect */
static void ceph_sock_data_ready(struct sock *sk)
{
	struct ceph_connection *con = sk->sk_user_data;
	if (atomic_read(&con->msgr->stopping)) {
		return;
	}

	if (sk->sk_state != TCP_CLOSE_WAIT) {
		dout("%s %p state = %d, queueing work\n", __func__,
		     con, con->state);
		queue_con(con);
	}
}

/* socket has buffer space for writing */
static void ceph_sock_write_space(struct sock *sk)
{
	struct ceph_connection *con = sk->sk_user_data;

	/* only queue to workqueue if there is data we want to write,
	 * and there is sufficient space in the socket buffer to accept
	 * more data.  clear SOCK_NOSPACE so that ceph_sock_write_space()
	 * doesn't get called again until try_write() fills the socket
	 * buffer. See net/ipv4/tcp_input.c:tcp_check_space()
	 * and net/core/stream.c:sk_stream_write_space().
	 */
	if (ceph_con_flag_test(con, CEPH_CON_F_WRITE_PENDING)) {
		if (sk_stream_is_writeable(sk)) {
			dout("%s %p queueing write work\n", __func__, con);
			clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
			queue_con(con);
		}
	} else {
		dout("%s %p nothing to write\n", __func__, con);
	}
}

/* socket's state has changed */
static void ceph_sock_state_change(struct sock *sk)
{
	struct ceph_connection *con = sk->sk_user_data;

	dout("%s %p state = %d sk_state = %u\n", __func__,
	     con, con->state, sk->sk_state);

	switch (sk->sk_state) {
	case TCP_CLOSE:
		dout("%s TCP_CLOSE\n", __func__);
		fallthrough;
	case TCP_CLOSE_WAIT:
		dout("%s TCP_CLOSE_WAIT\n", __func__);
		con_sock_state_closing(con);
		ceph_con_flag_set(con, CEPH_CON_F_SOCK_CLOSED);
		queue_con(con);
		break;
	case TCP_ESTABLISHED:
		dout("%s TCP_ESTABLISHED\n", __func__);
		con_sock_state_connected(con);
		queue_con(con);
		break;
	default:	/* Everything else is uninteresting */
		break;
	}
}

/*
 * set up socket callbacks
 */
static void set_sock_callbacks(struct socket *sock,
			       struct ceph_connection *con)
{
	struct sock *sk = sock->sk;
	sk->sk_user_data = con;
	sk->sk_data_ready = ceph_sock_data_ready;
	sk->sk_write_space = ceph_sock_write_space;
	sk->sk_state_change = ceph_sock_state_change;
}


/*
 * socket helpers
 */

/*
 * initiate connection to a remote socket.
 */
int ceph_tcp_connect(struct ceph_connection *con)
{
	struct sockaddr_storage ss = con->peer_addr.in_addr; /* align */
	struct socket *sock;
	unsigned int noio_flag;
	int ret;

	dout("%s con %p peer_addr %s\n", __func__, con,
	     ceph_pr_addr(&con->peer_addr));
	BUG_ON(con->sock);

	/* sock_create_kern() allocates with GFP_KERNEL */
	noio_flag = memalloc_noio_save();
	ret = sock_create_kern(read_pnet(&con->msgr->net), ss.ss_family,
			       SOCK_STREAM, IPPROTO_TCP, &sock);
	memalloc_noio_restore(noio_flag);
	if (ret)
		return ret;
	sock->sk->sk_allocation = GFP_NOFS;

#ifdef CONFIG_LOCKDEP
	lockdep_set_class(&sock->sk->sk_lock, &socket_class);
#endif

	set_sock_callbacks(sock, con);

	con_sock_state_connecting(con);
	ret = kernel_connect(sock, (struct sockaddr *)&ss, sizeof(ss),
			     O_NONBLOCK);
	if (ret == -EINPROGRESS) {
		dout("connect %s EINPROGRESS sk_state = %u\n",
		     ceph_pr_addr(&con->peer_addr),
		     sock->sk->sk_state);
	} else if (ret < 0) {
		pr_err("connect %s error %d\n",
		       ceph_pr_addr(&con->peer_addr), ret);
		sock_release(sock);
		return ret;
	}

	if (ceph_test_opt(from_msgr(con->msgr), TCP_NODELAY))
		tcp_sock_set_nodelay(sock->sk);

	con->sock = sock;
	return 0;
}

/*
 * Shutdown/close the socket for the given connection.
 */
int ceph_con_close_socket(struct ceph_connection *con)
{
	int rc = 0;

	dout("%s con %p sock %p\n", __func__, con, con->sock);
	if (con->sock) {
		rc = con->sock->ops->shutdown(con->sock, SHUT_RDWR);
		sock_release(con->sock);
		con->sock = NULL;
	}

	/*
	 * Forcibly clear the SOCK_CLOSED flag.  It gets set
	 * independent of the connection mutex, and we could have
	 * received a socket close event before we had the chance to
	 * shut the socket down.
	 */
	ceph_con_flag_clear(con, CEPH_CON_F_SOCK_CLOSED);

	con_sock_state_closed(con);
	return rc;
}

static void ceph_con_reset_protocol(struct ceph_connection *con)
{
	dout("%s con %p\n", __func__, con);

	ceph_con_close_socket(con);
	if (con->in_msg) {
		WARN_ON(con->in_msg->con != con);
		ceph_msg_put(con->in_msg);
		con->in_msg = NULL;
	}
	if (con->out_msg) {
		WARN_ON(con->out_msg->con != con);
		ceph_msg_put(con->out_msg);
		con->out_msg = NULL;
	}
	if (con->bounce_page) {
		__free_page(con->bounce_page);
		con->bounce_page = NULL;
	}

	if (ceph_msgr2(from_msgr(con->msgr)))
		ceph_con_v2_reset_protocol(con);
	else
		ceph_con_v1_reset_protocol(con);
}

/*
 * Reset a connection.  Discard all incoming and outgoing messages
 * and clear *_seq state.
 */
static void ceph_msg_remove(struct ceph_msg *msg)
{
	list_del_init(&msg->list_head);

	ceph_msg_put(msg);
}

static void ceph_msg_remove_list(struct list_head *head)
{
	while (!list_empty(head)) {
		struct ceph_msg *msg = list_first_entry(head, struct ceph_msg,
							list_head);
		ceph_msg_remove(msg);
	}
}

void ceph_con_reset_session(struct ceph_connection *con)
{
	dout("%s con %p\n", __func__, con);

	WARN_ON(con->in_msg);
	WARN_ON(con->out_msg);
	ceph_msg_remove_list(&con->out_queue);
	ceph_msg_remove_list(&con->out_sent);
	con->out_seq = 0;
	con->in_seq = 0;
	con->in_seq_acked = 0;

	if (ceph_msgr2(from_msgr(con->msgr)))
		ceph_con_v2_reset_session(con);
	else
		ceph_con_v1_reset_session(con);
}

/*
 * mark a peer down.  drop any open connections.
 */
void ceph_con_close(struct ceph_connection *con)
{
	mutex_lock(&con->mutex);
	dout("con_close %p peer %s\n", con, ceph_pr_addr(&con->peer_addr));
	con->state = CEPH_CON_S_CLOSED;

	ceph_con_flag_clear(con, CEPH_CON_F_LOSSYTX);  /* so we retry next
							  connect */
	ceph_con_flag_clear(con, CEPH_CON_F_KEEPALIVE_PENDING);
	ceph_con_flag_clear(con, CEPH_CON_F_WRITE_PENDING);
	ceph_con_flag_clear(con, CEPH_CON_F_BACKOFF);

	ceph_con_reset_protocol(con);
	ceph_con_reset_session(con);
	cancel_con(con);
	mutex_unlock(&con->mutex);
}
EXPORT_SYMBOL(ceph_con_close);

/*
 * Reopen a closed connection, with a new peer address.
 */
void ceph_con_open(struct ceph_connection *con,
		   __u8 entity_type, __u64 entity_num,
		   struct ceph_entity_addr *addr)
{
	mutex_lock(&con->mutex);
	dout("con_open %p %s\n", con, ceph_pr_addr(addr));

	WARN_ON(con->state != CEPH_CON_S_CLOSED);
	con->state = CEPH_CON_S_PREOPEN;

	con->peer_name.type = (__u8) entity_type;
	con->peer_name.num = cpu_to_le64(entity_num);

	memcpy(&con->peer_addr, addr, sizeof(*addr));
	con->delay = 0;      /* reset backoff memory */
	mutex_unlock(&con->mutex);
	queue_con(con);
}
EXPORT_SYMBOL(ceph_con_open);

/*
 * return true if this connection ever successfully opened
 */
bool ceph_con_opened(struct ceph_connection *con)
{
	if (ceph_msgr2(from_msgr(con->msgr)))
		return ceph_con_v2_opened(con);

	return ceph_con_v1_opened(con);
}

/*
 * initialize a new connection.
 */
void ceph_con_init(struct ceph_connection *con, void *private,
	const struct ceph_connection_operations *ops,
	struct ceph_messenger *msgr)
{
	dout("con_init %p\n", con);
	memset(con, 0, sizeof(*con));
	con->private = private;
	con->ops = ops;
	con->msgr = msgr;

	con_sock_state_init(con);

	mutex_init(&con->mutex);
	INIT_LIST_HEAD(&con->out_queue);
	INIT_LIST_HEAD(&con->out_sent);
	INIT_DELAYED_WORK(&con->work, ceph_con_workfn);

	con->state = CEPH_CON_S_CLOSED;
}
EXPORT_SYMBOL(ceph_con_init);

/*
 * We maintain a global counter to order connection attempts.  Get
 * a unique seq greater than @gt.
 */
u32 ceph_get_global_seq(struct ceph_messenger *msgr, u32 gt)
{
	u32 ret;

	spin_lock(&msgr->global_seq_lock);
	if (msgr->global_seq < gt)
		msgr->global_seq = gt;
	ret = ++msgr->global_seq;
	spin_unlock(&msgr->global_seq_lock);
	return ret;
}

/*
 * Discard messages that have been acked by the server.
 */
void ceph_con_discard_sent(struct ceph_connection *con, u64 ack_seq)
{
	struct ceph_msg *msg;
	u64 seq;

	dout("%s con %p ack_seq %llu\n", __func__, con, ack_seq);
	while (!list_empty(&con->out_sent)) {
		msg = list_first_entry(&con->out_sent, struct ceph_msg,
				       list_head);
		WARN_ON(msg->needs_out_seq);
		seq = le64_to_cpu(msg->hdr.seq);
		if (seq > ack_seq)
			break;

		dout("%s con %p discarding msg %p seq %llu\n", __func__, con,
		     msg, seq);
		ceph_msg_remove(msg);
	}
}

/*
 * Discard messages that have been requeued in con_fault(), up to
 * reconnect_seq.  This avoids gratuitously resending messages that
 * the server had received and handled prior to reconnect.
 */
void ceph_con_discard_requeued(struct ceph_connection *con, u64 reconnect_seq)
{
	struct ceph_msg *msg;
	u64 seq;

	dout("%s con %p reconnect_seq %llu\n", __func__, con, reconnect_seq);
	while (!list_empty(&con->out_queue)) {
		msg = list_first_entry(&con->out_queue, struct ceph_msg,
				       list_head);
		if (msg->needs_out_seq)
			break;
		seq = le64_to_cpu(msg->hdr.seq);
		if (seq > reconnect_seq)
			break;

		dout("%s con %p discarding msg %p seq %llu\n", __func__, con,
		     msg, seq);
		ceph_msg_remove(msg);
	}
}

#ifdef CONFIG_BLOCK

/*
 * For a bio data item, a piece is whatever remains of the next
 * entry in the current bio iovec, or the first entry in the next
 * bio in the list.
 */
static void ceph_msg_data_bio_cursor_init(struct ceph_msg_data_cursor *cursor,
					size_t length)
{
	struct ceph_msg_data *data = cursor->data;
	struct ceph_bio_iter *it = &cursor->bio_iter;

	cursor->resid = min_t(size_t, length, data->bio_length);
	*it = data->bio_pos;
	if (cursor->resid < it->iter.bi_size)
		it->iter.bi_size = cursor->resid;

	BUG_ON(cursor->resid < bio_iter_len(it->bio, it->iter));
}

static struct page *ceph_msg_data_bio_next(struct ceph_msg_data_cursor *cursor,
						size_t *page_offset,
						size_t *length)
{
	struct bio_vec bv = bio_iter_iovec(cursor->bio_iter.bio,
					   cursor->bio_iter.iter);

	*page_offset = bv.bv_offset;
	*length = bv.bv_len;
	return bv.bv_page;
}

static bool ceph_msg_data_bio_advance(struct ceph_msg_data_cursor *cursor,
					size_t bytes)
{
	struct ceph_bio_iter *it = &cursor->bio_iter;
	struct page *page = bio_iter_page(it->bio, it->iter);

	BUG_ON(bytes > cursor->resid);
	BUG_ON(bytes > bio_iter_len(it->bio, it->iter));
	cursor->resid -= bytes;
	bio_advance_iter(it->bio, &it->iter, bytes);

	if (!cursor->resid)
		return false;   /* no more data */

	if (!bytes || (it->iter.bi_size && it->iter.bi_bvec_done &&
		       page == bio_iter_page(it->bio, it->iter)))
		return false;	/* more bytes to process in this segment */

	if (!it->iter.bi_size) {
		it->bio = it->bio->bi_next;
		it->iter = it->bio->bi_iter;
		if (cursor->resid < it->iter.bi_size)
			it->iter.bi_size = cursor->resid;
	}

	BUG_ON(cursor->resid < bio_iter_len(it->bio, it->iter));
	return true;
}
#endif /* CONFIG_BLOCK */

static void ceph_msg_data_bvecs_cursor_init(struct ceph_msg_data_cursor *cursor,
					size_t length)
{
	struct ceph_msg_data *data = cursor->data;
	struct bio_vec *bvecs = data->bvec_pos.bvecs;

	cursor->resid = min_t(size_t, length, data->bvec_pos.iter.bi_size);
	cursor->bvec_iter = data->bvec_pos.iter;
	cursor->bvec_iter.bi_size = cursor->resid;

	BUG_ON(cursor->resid < bvec_iter_len(bvecs, cursor->bvec_iter));
}

static struct page *ceph_msg_data_bvecs_next(struct ceph_msg_data_cursor *cursor,
						size_t *page_offset,
						size_t *length)
{
	struct bio_vec bv = bvec_iter_bvec(cursor->data->bvec_pos.bvecs,
					   cursor->bvec_iter);

	*page_offset = bv.bv_offset;
	*length = bv.bv_len;
	return bv.bv_page;
}

static bool ceph_msg_data_bvecs_advance(struct ceph_msg_data_cursor *cursor,
					size_t bytes)
{
	struct bio_vec *bvecs = cursor->data->bvec_pos.bvecs;
	struct page *page = bvec_iter_page(bvecs, cursor->bvec_iter);

	BUG_ON(bytes > cursor->resid);
	BUG_ON(bytes > bvec_iter_len(bvecs, cursor->bvec_iter));
	cursor->resid -= bytes;
	bvec_iter_advance(bvecs, &cursor->bvec_iter, bytes);

	if (!cursor->resid)
		return false;   /* no more data */

	if (!bytes || (cursor->bvec_iter.bi_bvec_done &&
		       page == bvec_iter_page(bvecs, cursor->bvec_iter)))
		return false;	/* more bytes to process in this segment */

	BUG_ON(cursor->resid < bvec_iter_len(bvecs, cursor->bvec_iter));
	return true;
}

/*
 * For a page array, a piece comes from the first page in the array
 * that has not already been fully consumed.
 */
static void ceph_msg_data_pages_cursor_init(struct ceph_msg_data_cursor *cursor,
					size_t length)
{
	struct ceph_msg_data *data = cursor->data;
	int page_count;

	BUG_ON(data->type != CEPH_MSG_DATA_PAGES);

	BUG_ON(!data->pages);
	BUG_ON(!data->length);

	cursor->resid = min(length, data->length);
	page_count = calc_pages_for(data->alignment, (u64)data->length);
	cursor->page_offset = data->alignment & ~PAGE_MASK;
	cursor->page_index = 0;
	BUG_ON(page_count > (int)USHRT_MAX);
	cursor->page_count = (unsigned short)page_count;
	BUG_ON(length > SIZE_MAX - cursor->page_offset);
}

static struct page *
ceph_msg_data_pages_next(struct ceph_msg_data_cursor *cursor,
					size_t *page_offset, size_t *length)
{
	struct ceph_msg_data *data = cursor->data;

	BUG_ON(data->type != CEPH_MSG_DATA_PAGES);

	BUG_ON(cursor->page_index >= cursor->page_count);
	BUG_ON(cursor->page_offset >= PAGE_SIZE);

	*page_offset = cursor->page_offset;
	*length = min_t(size_t, cursor->resid, PAGE_SIZE - *page_offset);
	return data->pages[cursor->page_index];
}

static bool ceph_msg_data_pages_advance(struct ceph_msg_data_cursor *cursor,
						size_t bytes)
{
	BUG_ON(cursor->data->type != CEPH_MSG_DATA_PAGES);

	BUG_ON(cursor->page_offset + bytes > PAGE_SIZE);

	/* Advance the cursor page offset */

	cursor->resid -= bytes;
	cursor->page_offset = (cursor->page_offset + bytes) & ~PAGE_MASK;
	if (!bytes || cursor->page_offset)
		return false;	/* more bytes to process in the current page */

	if (!cursor->resid)
		return false;   /* no more data */

	/* Move on to the next page; offset is already at 0 */

	BUG_ON(cursor->page_index >= cursor->page_count);
	cursor->page_index++;
	return true;
}

/*
 * For a pagelist, a piece is whatever remains to be consumed in the
 * first page in the list, or the front of the next page.
 */
static void
ceph_msg_data_pagelist_cursor_init(struct ceph_msg_data_cursor *cursor,
					size_t length)
{
	struct ceph_msg_data *data = cursor->data;
	struct ceph_pagelist *pagelist;
	struct page *page;

	BUG_ON(data->type != CEPH_MSG_DATA_PAGELIST);

	pagelist = data->pagelist;
	BUG_ON(!pagelist);

	if (!length)
		return;		/* pagelist can be assigned but empty */

	BUG_ON(list_empty(&pagelist->head));
	page = list_first_entry(&pagelist->head, struct page, lru);

	cursor->resid = min(length, pagelist->length);
	cursor->page = page;
	cursor->offset = 0;
}

static struct page *
ceph_msg_data_pagelist_next(struct ceph_msg_data_cursor *cursor,
				size_t *page_offset, size_t *length)
{
	struct ceph_msg_data *data = cursor->data;
	struct ceph_pagelist *pagelist;

	BUG_ON(data->type != CEPH_MSG_DATA_PAGELIST);

	pagelist = data->pagelist;
	BUG_ON(!pagelist);

	BUG_ON(!cursor->page);
	BUG_ON(cursor->offset + cursor->resid != pagelist->length);

	/* offset of first page in pagelist is always 0 */
	*page_offset = cursor->offset & ~PAGE_MASK;
	*length = min_t(size_t, cursor->resid, PAGE_SIZE - *page_offset);
	return cursor->page;
}

static bool ceph_msg_data_pagelist_advance(struct ceph_msg_data_cursor *cursor,
						size_t bytes)
{
	struct ceph_msg_data *data = cursor->data;
	struct ceph_pagelist *pagelist;

	BUG_ON(data->type != CEPH_MSG_DATA_PAGELIST);

	pagelist = data->pagelist;
	BUG_ON(!pagelist);

	BUG_ON(cursor->offset + cursor->resid != pagelist->length);
	BUG_ON((cursor->offset & ~PAGE_MASK) + bytes > PAGE_SIZE);

	/* Advance the cursor offset */

	cursor->resid -= bytes;
	cursor->offset += bytes;
	/* offset of first page in pagelist is always 0 */
	if (!bytes || cursor->offset & ~PAGE_MASK)
		return false;	/* more bytes to process in the current page */

	if (!cursor->resid)
		return false;   /* no more data */

	/* Move on to the next page */

	BUG_ON(list_is_last(&cursor->page->lru, &pagelist->head));
	cursor->page = list_next_entry(cursor->page, lru);
	return true;
}

/*
 * Message data is handled (sent or received) in pieces, where each
 * piece resides on a single page.  The network layer might not
 * consume an entire piece at once.  A data item's cursor keeps
 * track of which piece is next to process and how much remains to
 * be processed in that piece.  It also tracks whether the current
 * piece is the last one in the data item.
 */
static void __ceph_msg_data_cursor_init(struct ceph_msg_data_cursor *cursor)
{
	size_t length = cursor->total_resid;

	switch (cursor->data->type) {
	case CEPH_MSG_DATA_PAGELIST:
		ceph_msg_data_pagelist_cursor_init(cursor, length);
		break;
	case CEPH_MSG_DATA_PAGES:
		ceph_msg_data_pages_cursor_init(cursor, length);
		break;
#ifdef CONFIG_BLOCK
	case CEPH_MSG_DATA_BIO:
		ceph_msg_data_bio_cursor_init(cursor, length);
		break;
#endif /* CONFIG_BLOCK */
	case CEPH_MSG_DATA_BVECS:
		ceph_msg_data_bvecs_cursor_init(cursor, length);
		break;
	case CEPH_MSG_DATA_NONE:
	default:
		/* BUG(); */
		break;
	}
	cursor->need_crc = true;
}

void ceph_msg_data_cursor_init(struct ceph_msg_data_cursor *cursor,
			       struct ceph_msg *msg, size_t length)
{
	BUG_ON(!length);
	BUG_ON(length > msg->data_length);
	BUG_ON(!msg->num_data_items);

	cursor->total_resid = length;
	cursor->data = msg->data;

	__ceph_msg_data_cursor_init(cursor);
}

/*
 * Return the page containing the next piece to process for a given
 * data item, and supply the page offset and length of that piece.
 * Indicate whether this is the last piece in this data item.
 */
struct page *ceph_msg_data_next(struct ceph_msg_data_cursor *cursor,
				size_t *page_offset, size_t *length)
{
	struct page *page;

	switch (cursor->data->type) {
	case CEPH_MSG_DATA_PAGELIST:
		page = ceph_msg_data_pagelist_next(cursor, page_offset, length);
		break;
	case CEPH_MSG_DATA_PAGES:
		page = ceph_msg_data_pages_next(cursor, page_offset, length);
		break;
#ifdef CONFIG_BLOCK
	case CEPH_MSG_DATA_BIO:
		page = ceph_msg_data_bio_next(cursor, page_offset, length);
		break;
#endif /* CONFIG_BLOCK */
	case CEPH_MSG_DATA_BVECS:
		page = ceph_msg_data_bvecs_next(cursor, page_offset, length);
		break;
	case CEPH_MSG_DATA_NONE:
	default:
		page = NULL;
		break;
	}

	BUG_ON(!page);
	BUG_ON(*page_offset + *length > PAGE_SIZE);
	BUG_ON(!*length);
	BUG_ON(*length > cursor->resid);

	return page;
}

/*
 * Returns true if the result moves the cursor on to the next piece
 * of the data item.
 */
void ceph_msg_data_advance(struct ceph_msg_data_cursor *cursor, size_t bytes)
{
	bool new_piece;

	BUG_ON(bytes > cursor->resid);
	switch (cursor->data->type) {
	case CEPH_MSG_DATA_PAGELIST:
		new_piece = ceph_msg_data_pagelist_advance(cursor, bytes);
		break;
	case CEPH_MSG_DATA_PAGES:
		new_piece = ceph_msg_data_pages_advance(cursor, bytes);
		break;
#ifdef CONFIG_BLOCK
	case CEPH_MSG_DATA_BIO:
		new_piece = ceph_msg_data_bio_advance(cursor, bytes);
		break;
#endif /* CONFIG_BLOCK */
	case CEPH_MSG_DATA_BVECS:
		new_piece = ceph_msg_data_bvecs_advance(cursor, bytes);
		break;
	case CEPH_MSG_DATA_NONE:
	default:
		BUG();
		break;
	}
	cursor->total_resid -= bytes;

	if (!cursor->resid && cursor->total_resid) {
		cursor->data++;
		__ceph_msg_data_cursor_init(cursor);
		new_piece = true;
	}
	cursor->need_crc = new_piece;
}

u32 ceph_crc32c_page(u32 crc, struct page *page, unsigned int page_offset,
		     unsigned int length)
{
	char *kaddr;

	kaddr = kmap(page);
	BUG_ON(kaddr == NULL);
	crc = crc32c(crc, kaddr + page_offset, length);
	kunmap(page);

	return crc;
}

bool ceph_addr_is_blank(const struct ceph_entity_addr *addr)
{
	struct sockaddr_storage ss = addr->in_addr; /* align */
	struct in_addr *addr4 = &((struct sockaddr_in *)&ss)->sin_addr;
	struct in6_addr *addr6 = &((struct sockaddr_in6 *)&ss)->sin6_addr;

	switch (ss.ss_family) {
	case AF_INET:
		return addr4->s_addr == htonl(INADDR_ANY);
	case AF_INET6:
		return ipv6_addr_any(addr6);
	default:
		return true;
	}
}
EXPORT_SYMBOL(ceph_addr_is_blank);

int ceph_addr_port(const struct ceph_entity_addr *addr)
{
	switch (get_unaligned(&addr->in_addr.ss_family)) {
	case AF_INET:
		return ntohs(get_unaligned(&((struct sockaddr_in *)&addr->in_addr)->sin_port));
	case AF_INET6:
		return ntohs(get_unaligned(&((struct sockaddr_in6 *)&addr->in_addr)->sin6_port));
	}
	return 0;
}

void ceph_addr_set_port(struct ceph_entity_addr *addr, int p)
{
	switch (get_unaligned(&addr->in_addr.ss_family)) {
	case AF_INET:
		put_unaligned(htons(p), &((struct sockaddr_in *)&addr->in_addr)->sin_port);
		break;
	case AF_INET6:
		put_unaligned(htons(p), &((struct sockaddr_in6 *)&addr->in_addr)->sin6_port);
		break;
	}
}

/*
 * Unlike other *_pton function semantics, zero indicates success.
 */
static int ceph_pton(const char *str, size_t len, struct ceph_entity_addr *addr,
		char delim, const char **ipend)
{
	memset(&addr->in_addr, 0, sizeof(addr->in_addr));

	if (in4_pton(str, len, (u8 *)&((struct sockaddr_in *)&addr->in_addr)->sin_addr.s_addr, delim, ipend)) {
		put_unaligned(AF_INET, &addr->in_addr.ss_family);
		return 0;
	}

	if (in6_pton(str, len, (u8 *)&((struct sockaddr_in6 *)&addr->in_addr)->sin6_addr.s6_addr, delim, ipend)) {
		put_unaligned(AF_INET6, &addr->in_addr.ss_family);
		return 0;
	}

	return -EINVAL;
}

/*
 * Extract hostname string and resolve using kernel DNS facility.
 */
#ifdef CONFIG_CEPH_LIB_USE_DNS_RESOLVER
static int ceph_dns_resolve_name(const char *name, size_t namelen,
		struct ceph_entity_addr *addr, char delim, const char **ipend)
{
	const char *end, *delim_p;
	char *colon_p, *ip_addr = NULL;
	int ip_len, ret;

	/*
	 * The end of the hostname occurs immediately preceding the delimiter or
	 * the port marker (':') where the delimiter takes precedence.
	 */
	delim_p = memchr(name, delim, namelen);
	colon_p = memchr(name, ':', namelen);

	if (delim_p && colon_p)
		end = delim_p < colon_p ? delim_p : colon_p;
	else if (!delim_p && colon_p)
		end = colon_p;
	else {
		end = delim_p;
		if (!end) /* case: hostname:/ */
			end = name + namelen;
	}

	if (end <= name)
		return -EINVAL;

	/* do dns_resolve upcall */
	ip_len = dns_query(current->nsproxy->net_ns,
			   NULL, name, end - name, NULL, &ip_addr, NULL, false);
	if (ip_len > 0)
		ret = ceph_pton(ip_addr, ip_len, addr, -1, NULL);
	else
		ret = -ESRCH;

	kfree(ip_addr);

	*ipend = end;

	pr_info("resolve '%.*s' (ret=%d): %s\n", (int)(end - name), name,
			ret, ret ? "failed" : ceph_pr_addr(addr));

	return ret;
}
#else
static inline int ceph_dns_resolve_name(const char *name, size_t namelen,
		struct ceph_entity_addr *addr, char delim, const char **ipend)
{
	return -EINVAL;
}
#endif

/*
 * Parse a server name (IP or hostname). If a valid IP address is not found
 * then try to extract a hostname to resolve using userspace DNS upcall.
 */
static int ceph_parse_server_name(const char *name, size_t namelen,
		struct ceph_entity_addr *addr, char delim, const char **ipend)
{
	int ret;

	ret = ceph_pton(name, namelen, addr, delim, ipend);
	if (ret)
		ret = ceph_dns_resolve_name(name, namelen, addr, delim, ipend);

	return ret;
}

/*
 * Parse an ip[:port] list into an addr array.  Use the default
 * monitor port if a port isn't specified.
 */
int ceph_parse_ips(const char *c, const char *end,
		   struct ceph_entity_addr *addr,
		   int max_count, int *count, char delim)
{
	int i, ret = -EINVAL;
	const char *p = c;

	dout("parse_ips on '%.*s'\n", (int)(end-c), c);
	for (i = 0; i < max_count; i++) {
		char cur_delim = delim;
		const char *ipend;
		int port;

		if (*p == '[') {
			cur_delim = ']';
			p++;
		}

		ret = ceph_parse_server_name(p, end - p, &addr[i], cur_delim,
					     &ipend);
		if (ret)
			goto bad;
		ret = -EINVAL;

		p = ipend;

		if (cur_delim == ']') {
			if (*p != ']') {
				dout("missing matching ']'\n");
				goto bad;
			}
			p++;
		}

		/* port? */
		if (p < end && *p == ':') {
			port = 0;
			p++;
			while (p < end && *p >= '0' && *p <= '9') {
				port = (port * 10) + (*p - '0');
				p++;
			}
			if (port == 0)
				port = CEPH_MON_PORT;
			else if (port > 65535)
				goto bad;
		} else {
			port = CEPH_MON_PORT;
		}

		ceph_addr_set_port(&addr[i], port);
		/*
		 * We want the type to be set according to ms_mode
		 * option, but options are normally parsed after mon
		 * addresses.  Rather than complicating parsing, set
		 * to LEGACY and override in build_initial_monmap()
		 * for mon addresses and ceph_messenger_init() for
		 * ip option.
		 */
		addr[i].type = CEPH_ENTITY_ADDR_TYPE_LEGACY;
		addr[i].nonce = 0;

		dout("%s got %s\n", __func__, ceph_pr_addr(&addr[i]));

		if (p == end)
			break;
		if (*p != delim)
			goto bad;
		p++;
	}

	if (p != end)
		goto bad;

	if (count)
		*count = i + 1;
	return 0;

bad:
	return ret;
}

/*
 * Process message.  This happens in the worker thread.  The callback should
 * be careful not to do anything that waits on other incoming messages or it
 * may deadlock.
 */
void ceph_con_process_message(struct ceph_connection *con)
{
	struct ceph_msg *msg = con->in_msg;

	BUG_ON(con->in_msg->con != con);
	con->in_msg = NULL;

	/* if first message, set peer_name */
	if (con->peer_name.type == 0)
		con->peer_name = msg->hdr.src;

	con->in_seq++;
	mutex_unlock(&con->mutex);

	dout("===== %p %llu from %s%lld %d=%s len %d+%d+%d (%u %u %u) =====\n",
	     msg, le64_to_cpu(msg->hdr.seq),
	     ENTITY_NAME(msg->hdr.src),
	     le16_to_cpu(msg->hdr.type),
	     ceph_msg_type_name(le16_to_cpu(msg->hdr.type)),
	     le32_to_cpu(msg->hdr.front_len),
	     le32_to_cpu(msg->hdr.middle_len),
	     le32_to_cpu(msg->hdr.data_len),
	     con->in_front_crc, con->in_middle_crc, con->in_data_crc);
	con->ops->dispatch(con, msg);

	mutex_lock(&con->mutex);
}

/*
 * Atomically queue work on a connection after the specified delay.
 * Bump @con reference to avoid races with connection teardown.
 * Returns 0 if work was queued, or an error code otherwise.
 */
static int queue_con_delay(struct ceph_connection *con, unsigned long delay)
{
	if (!con->ops->get(con)) {
		dout("%s %p ref count 0\n", __func__, con);
		return -ENOENT;
	}

	if (delay >= HZ)
		delay = round_jiffies_relative(delay);

	dout("%s %p %lu\n", __func__, con, delay);
	if (!queue_delayed_work(ceph_msgr_wq, &con->work, delay)) {
		dout("%s %p - already queued\n", __func__, con);
		con->ops->put(con);
		return -EBUSY;
	}

	return 0;
}

static void queue_con(struct ceph_connection *con)
{
	(void) queue_con_delay(con, 0);
}

static void cancel_con(struct ceph_connection *con)
{
	if (cancel_delayed_work(&con->work)) {
		dout("%s %p\n", __func__, con);
		con->ops->put(con);
	}
}

static bool con_sock_closed(struct ceph_connection *con)
{
	if (!ceph_con_flag_test_and_clear(con, CEPH_CON_F_SOCK_CLOSED))
		return false;

#define CASE(x)								\
	case CEPH_CON_S_ ## x:						\
		con->error_msg = "socket closed (con state " #x ")";	\
		break;

	switch (con->state) {
	CASE(CLOSED);
	CASE(PREOPEN);
	CASE(V1_BANNER);
	CASE(V1_CONNECT_MSG);
	CASE(V2_BANNER_PREFIX);
	CASE(V2_BANNER_PAYLOAD);
	CASE(V2_HELLO);
	CASE(V2_AUTH);
	CASE(V2_AUTH_SIGNATURE);
	CASE(V2_SESSION_CONNECT);
	CASE(V2_SESSION_RECONNECT);
	CASE(OPEN);
	CASE(STANDBY);
	default:
		BUG();
	}
#undef CASE

	return true;
}

static bool con_backoff(struct ceph_connection *con)
{
	int ret;

	if (!ceph_con_flag_test_and_clear(con, CEPH_CON_F_BACKOFF))
		return false;

	ret = queue_con_delay(con, con->delay);
	if (ret) {
		dout("%s: con %p FAILED to back off %lu\n", __func__,
			con, con->delay);
		BUG_ON(ret == -ENOENT);
		ceph_con_flag_set(con, CEPH_CON_F_BACKOFF);
	}

	return true;
}

/* Finish fault handling; con->mutex must *not* be held here */

static void con_fault_finish(struct ceph_connection *con)
{
	dout("%s %p\n", __func__, con);

	/*
	 * in case we faulted due to authentication, invalidate our
	 * current tickets so that we can get new ones.
	 */
	if (con->v1.auth_retry) {
		dout("auth_retry %d, invalidating\n", con->v1.auth_retry);
		if (con->ops->invalidate_authorizer)
			con->ops->invalidate_authorizer(con);
		con->v1.auth_retry = 0;
	}

	if (con->ops->fault)
		con->ops->fault(con);
}

/*
 * Do some work on a connection.  Drop a connection ref when we're done.
 */
static void ceph_con_workfn(struct work_struct *work)
{
	struct ceph_connection *con = container_of(work, struct ceph_connection,
						   work.work);
	bool fault;

	mutex_lock(&con->mutex);
	while (true) {
		int ret;

		if ((fault = con_sock_closed(con))) {
			dout("%s: con %p SOCK_CLOSED\n", __func__, con);
			break;
		}
		if (con_backoff(con)) {
			dout("%s: con %p BACKOFF\n", __func__, con);
			break;
		}
		if (con->state == CEPH_CON_S_STANDBY) {
			dout("%s: con %p STANDBY\n", __func__, con);
			break;
		}
		if (con->state == CEPH_CON_S_CLOSED) {
			dout("%s: con %p CLOSED\n", __func__, con);
			BUG_ON(con->sock);
			break;
		}
		if (con->state == CEPH_CON_S_PREOPEN) {
			dout("%s: con %p PREOPEN\n", __func__, con);
			BUG_ON(con->sock);
		}

		if (ceph_msgr2(from_msgr(con->msgr)))
			ret = ceph_con_v2_try_read(con);
		else
			ret = ceph_con_v1_try_read(con);
		if (ret < 0) {
			if (ret == -EAGAIN)
				continue;
			if (!con->error_msg)
				con->error_msg = "socket error on read";
			fault = true;
			break;
		}

		if (ceph_msgr2(from_msgr(con->msgr)))
			ret = ceph_con_v2_try_write(con);
		else
			ret = ceph_con_v1_try_write(con);
		if (ret < 0) {
			if (ret == -EAGAIN)
				continue;
			if (!con->error_msg)
				con->error_msg = "socket error on write";
			fault = true;
		}

		break;	/* If we make it to here, we're done */
	}
	if (fault)
		con_fault(con);
	mutex_unlock(&con->mutex);

	if (fault)
		con_fault_finish(con);

	con->ops->put(con);
}

/*
 * Generic error/fault handler.  A retry mechanism is used with
 * exponential backoff
 */
static void con_fault(struct ceph_connection *con)
{
	dout("fault %p state %d to peer %s\n",
	     con, con->state, ceph_pr_addr(&con->peer_addr));

	pr_warn("%s%lld %s %s\n", ENTITY_NAME(con->peer_name),
		ceph_pr_addr(&con->peer_addr), con->error_msg);
	con->error_msg = NULL;

	WARN_ON(con->state == CEPH_CON_S_STANDBY ||
		con->state == CEPH_CON_S_CLOSED);

	ceph_con_reset_protocol(con);

	if (ceph_con_flag_test(con, CEPH_CON_F_LOSSYTX)) {
		dout("fault on LOSSYTX channel, marking CLOSED\n");
		con->state = CEPH_CON_S_CLOSED;
		return;
	}

	/* Requeue anything that hasn't been acked */
	list_splice_init(&con->out_sent, &con->out_queue);

	/* If there are no messages queued or keepalive pending, place
	 * the connection in a STANDBY state */
	if (list_empty(&con->out_queue) &&
	    !ceph_con_flag_test(con, CEPH_CON_F_KEEPALIVE_PENDING)) {
		dout("fault %p setting STANDBY clearing WRITE_PENDING\n", con);
		ceph_con_flag_clear(con, CEPH_CON_F_WRITE_PENDING);
		con->state = CEPH_CON_S_STANDBY;
	} else {
		/* retry after a delay. */
		con->state = CEPH_CON_S_PREOPEN;
		if (!con->delay) {
			con->delay = BASE_DELAY_INTERVAL;
		} else if (con->delay < MAX_DELAY_INTERVAL) {
			con->delay *= 2;
			if (con->delay > MAX_DELAY_INTERVAL)
				con->delay = MAX_DELAY_INTERVAL;
		}
		ceph_con_flag_set(con, CEPH_CON_F_BACKOFF);
		queue_con(con);
	}
}

void ceph_messenger_reset_nonce(struct ceph_messenger *msgr)
{
	u32 nonce = le32_to_cpu(msgr->inst.addr.nonce) + 1000000;
	msgr->inst.addr.nonce = cpu_to_le32(nonce);
	ceph_encode_my_addr(msgr);
}

/*
 * initialize a new messenger instance
 */
void ceph_messenger_init(struct ceph_messenger *msgr,
			 struct ceph_entity_addr *myaddr)
{
	spin_lock_init(&msgr->global_seq_lock);

	if (myaddr) {
		memcpy(&msgr->inst.addr.in_addr, &myaddr->in_addr,
		       sizeof(msgr->inst.addr.in_addr));
		ceph_addr_set_port(&msgr->inst.addr, 0);
	}

	/*
	 * Since nautilus, clients are identified using type ANY.
	 * For msgr1, ceph_encode_banner_addr() munges it to NONE.
	 */
	msgr->inst.addr.type = CEPH_ENTITY_ADDR_TYPE_ANY;

	/* generate a random non-zero nonce */
	do {
		get_random_bytes(&msgr->inst.addr.nonce,
				 sizeof(msgr->inst.addr.nonce));
	} while (!msgr->inst.addr.nonce);
	ceph_encode_my_addr(msgr);

	atomic_set(&msgr->stopping, 0);
	write_pnet(&msgr->net, get_net(current->nsproxy->net_ns));

	dout("%s %p\n", __func__, msgr);
}

void ceph_messenger_fini(struct ceph_messenger *msgr)
{
	put_net(read_pnet(&msgr->net));
}

static void msg_con_set(struct ceph_msg *msg, struct ceph_connection *con)
{
	if (msg->con)
		msg->con->ops->put(msg->con);

	msg->con = con ? con->ops->get(con) : NULL;
	BUG_ON(msg->con != con);
}

static void clear_standby(struct ceph_connection *con)
{
	/* come back from STANDBY? */
	if (con->state == CEPH_CON_S_STANDBY) {
		dout("clear_standby %p and ++connect_seq\n", con);
		con->state = CEPH_CON_S_PREOPEN;
		con->v1.connect_seq++;
		WARN_ON(ceph_con_flag_test(con, CEPH_CON_F_WRITE_PENDING));
		WARN_ON(ceph_con_flag_test(con, CEPH_CON_F_KEEPALIVE_PENDING));
	}
}

/*
 * Queue up an outgoing message on the given connection.
 *
 * Consumes a ref on @msg.
 */
void ceph_con_send(struct ceph_connection *con, struct ceph_msg *msg)
{
	/* set src+dst */
	msg->hdr.src = con->msgr->inst.name;
	BUG_ON(msg->front.iov_len != le32_to_cpu(msg->hdr.front_len));
	msg->needs_out_seq = true;

	mutex_lock(&con->mutex);

	if (con->state == CEPH_CON_S_CLOSED) {
		dout("con_send %p closed, dropping %p\n", con, msg);
		ceph_msg_put(msg);
		mutex_unlock(&con->mutex);
		return;
	}

	msg_con_set(msg, con);

	BUG_ON(!list_empty(&msg->list_head));
	list_add_tail(&msg->list_head, &con->out_queue);
	dout("----- %p to %s%lld %d=%s len %d+%d+%d -----\n", msg,
	     ENTITY_NAME(con->peer_name), le16_to_cpu(msg->hdr.type),
	     ceph_msg_type_name(le16_to_cpu(msg->hdr.type)),
	     le32_to_cpu(msg->hdr.front_len),
	     le32_to_cpu(msg->hdr.middle_len),
	     le32_to_cpu(msg->hdr.data_len));

	clear_standby(con);
	mutex_unlock(&con->mutex);

	/* if there wasn't anything waiting to send before, queue
	 * new work */
	if (!ceph_con_flag_test_and_set(con, CEPH_CON_F_WRITE_PENDING))
		queue_con(con);
}
EXPORT_SYMBOL(ceph_con_send);

/*
 * Revoke a message that was previously queued for send
 */
void ceph_msg_revoke(struct ceph_msg *msg)
{
	struct ceph_connection *con = msg->con;

	if (!con) {
		dout("%s msg %p null con\n", __func__, msg);
		return;		/* Message not in our possession */
	}

	mutex_lock(&con->mutex);
	if (list_empty(&msg->list_head)) {
		WARN_ON(con->out_msg == msg);
		dout("%s con %p msg %p not linked\n", __func__, con, msg);
		mutex_unlock(&con->mutex);
		return;
	}

	dout("%s con %p msg %p was linked\n", __func__, con, msg);
	msg->hdr.seq = 0;
	ceph_msg_remove(msg);

	if (con->out_msg == msg) {
		WARN_ON(con->state != CEPH_CON_S_OPEN);
		dout("%s con %p msg %p was sending\n", __func__, con, msg);
		if (ceph_msgr2(from_msgr(con->msgr)))
			ceph_con_v2_revoke(con);
		else
			ceph_con_v1_revoke(con);
		ceph_msg_put(con->out_msg);
		con->out_msg = NULL;
	} else {
		dout("%s con %p msg %p not current, out_msg %p\n", __func__,
		     con, msg, con->out_msg);
	}
	mutex_unlock(&con->mutex);
}

/*
 * Revoke a message that we may be reading data into
 */
void ceph_msg_revoke_incoming(struct ceph_msg *msg)
{
	struct ceph_connection *con = msg->con;

	if (!con) {
		dout("%s msg %p null con\n", __func__, msg);
		return;		/* Message not in our possession */
	}

	mutex_lock(&con->mutex);
	if (con->in_msg == msg) {
		WARN_ON(con->state != CEPH_CON_S_OPEN);
		dout("%s con %p msg %p was recving\n", __func__, con, msg);
		if (ceph_msgr2(from_msgr(con->msgr)))
			ceph_con_v2_revoke_incoming(con);
		else
			ceph_con_v1_revoke_incoming(con);
		ceph_msg_put(con->in_msg);
		con->in_msg = NULL;
	} else {
		dout("%s con %p msg %p not current, in_msg %p\n", __func__,
		     con, msg, con->in_msg);
	}
	mutex_unlock(&con->mutex);
}

/*
 * Queue a keepalive byte to ensure the tcp connection is alive.
 */
void ceph_con_keepalive(struct ceph_connection *con)
{
	dout("con_keepalive %p\n", con);
	mutex_lock(&con->mutex);
	clear_standby(con);
	ceph_con_flag_set(con, CEPH_CON_F_KEEPALIVE_PENDING);
	mutex_unlock(&con->mutex);

	if (!ceph_con_flag_test_and_set(con, CEPH_CON_F_WRITE_PENDING))
		queue_con(con);
}
EXPORT_SYMBOL(ceph_con_keepalive);

bool ceph_con_keepalive_expired(struct ceph_connection *con,
			       unsigned long interval)
{
	if (interval > 0 &&
	    (con->peer_features & CEPH_FEATURE_MSGR_KEEPALIVE2)) {
		struct timespec64 now;
		struct timespec64 ts;
		ktime_get_real_ts64(&now);
		jiffies_to_timespec64(interval, &ts);
		ts = timespec64_add(con->last_keepalive_ack, ts);
		return timespec64_compare(&now, &ts) >= 0;
	}
	return false;
}

static struct ceph_msg_data *ceph_msg_data_add(struct ceph_msg *msg)
{
	BUG_ON(msg->num_data_items >= msg->max_data_items);
	return &msg->data[msg->num_data_items++];
}

static void ceph_msg_data_destroy(struct ceph_msg_data *data)
{
	if (data->type == CEPH_MSG_DATA_PAGES && data->own_pages) {
		int num_pages = calc_pages_for(data->alignment, data->length);
		ceph_release_page_vector(data->pages, num_pages);
	} else if (data->type == CEPH_MSG_DATA_PAGELIST) {
		ceph_pagelist_release(data->pagelist);
	}
}

void ceph_msg_data_add_pages(struct ceph_msg *msg, struct page **pages,
			     size_t length, size_t alignment, bool own_pages)
{
	struct ceph_msg_data *data;

	BUG_ON(!pages);
	BUG_ON(!length);

	data = ceph_msg_data_add(msg);
	data->type = CEPH_MSG_DATA_PAGES;
	data->pages = pages;
	data->length = length;
	data->alignment = alignment & ~PAGE_MASK;
	data->own_pages = own_pages;

	msg->data_length += length;
}
EXPORT_SYMBOL(ceph_msg_data_add_pages);

void ceph_msg_data_add_pagelist(struct ceph_msg *msg,
				struct ceph_pagelist *pagelist)
{
	struct ceph_msg_data *data;

	BUG_ON(!pagelist);
	BUG_ON(!pagelist->length);

	data = ceph_msg_data_add(msg);
	data->type = CEPH_MSG_DATA_PAGELIST;
	refcount_inc(&pagelist->refcnt);
	data->pagelist = pagelist;

	msg->data_length += pagelist->length;
}
EXPORT_SYMBOL(ceph_msg_data_add_pagelist);

#ifdef	CONFIG_BLOCK
void ceph_msg_data_add_bio(struct ceph_msg *msg, struct ceph_bio_iter *bio_pos,
			   u32 length)
{
	struct ceph_msg_data *data;

	data = ceph_msg_data_add(msg);
	data->type = CEPH_MSG_DATA_BIO;
	data->bio_pos = *bio_pos;
	data->bio_length = length;

	msg->data_length += length;
}
EXPORT_SYMBOL(ceph_msg_data_add_bio);
#endif	/* CONFIG_BLOCK */

void ceph_msg_data_add_bvecs(struct ceph_msg *msg,
			     struct ceph_bvec_iter *bvec_pos)
{
	struct ceph_msg_data *data;

	data = ceph_msg_data_add(msg);
	data->type = CEPH_MSG_DATA_BVECS;
	data->bvec_pos = *bvec_pos;

	msg->data_length += bvec_pos->iter.bi_size;
}
EXPORT_SYMBOL(ceph_msg_data_add_bvecs);

/*
 * construct a new message with given type, size
 * the new msg has a ref count of 1.
 */
struct ceph_msg *ceph_msg_new2(int type, int front_len, int max_data_items,
			       gfp_t flags, bool can_fail)
{
	struct ceph_msg *m;

	m = kmem_cache_zalloc(ceph_msg_cache, flags);
	if (m == NULL)
		goto out;

	m->hdr.type = cpu_to_le16(type);
	m->hdr.priority = cpu_to_le16(CEPH_MSG_PRIO_DEFAULT);
	m->hdr.front_len = cpu_to_le32(front_len);

	INIT_LIST_HEAD(&m->list_head);
	kref_init(&m->kref);

	/* front */
	if (front_len) {
		m->front.iov_base = kvmalloc(front_len, flags);
		if (m->front.iov_base == NULL) {
			dout("ceph_msg_new can't allocate %d bytes\n",
			     front_len);
			goto out2;
		}
	} else {
		m->front.iov_base = NULL;
	}
	m->front_alloc_len = m->front.iov_len = front_len;

	if (max_data_items) {
		m->data = kmalloc_array(max_data_items, sizeof(*m->data),
					flags);
		if (!m->data)
			goto out2;

		m->max_data_items = max_data_items;
	}

	dout("ceph_msg_new %p front %d\n", m, front_len);
	return m;

out2:
	ceph_msg_put(m);
out:
	if (!can_fail) {
		pr_err("msg_new can't create type %d front %d\n", type,
		       front_len);
		WARN_ON(1);
	} else {
		dout("msg_new can't create type %d front %d\n", type,
		     front_len);
	}
	return NULL;
}
EXPORT_SYMBOL(ceph_msg_new2);

struct ceph_msg *ceph_msg_new(int type, int front_len, gfp_t flags,
			      bool can_fail)
{
	return ceph_msg_new2(type, front_len, 0, flags, can_fail);
}
EXPORT_SYMBOL(ceph_msg_new);

/*
 * Allocate "middle" portion of a message, if it is needed and wasn't
 * allocated by alloc_msg.  This allows us to read a small fixed-size
 * per-type header in the front and then gracefully fail (i.e.,
 * propagate the error to the caller based on info in the front) when
 * the middle is too large.
 */
static int ceph_alloc_middle(struct ceph_connection *con, struct ceph_msg *msg)
{
	int type = le16_to_cpu(msg->hdr.type);
	int middle_len = le32_to_cpu(msg->hdr.middle_len);

	dout("alloc_middle %p type %d %s middle_len %d\n", msg, type,
	     ceph_msg_type_name(type), middle_len);
	BUG_ON(!middle_len);
	BUG_ON(msg->middle);

	msg->middle = ceph_buffer_new(middle_len, GFP_NOFS);
	if (!msg->middle)
		return -ENOMEM;
	return 0;
}

/*
 * Allocate a message for receiving an incoming message on a
 * connection, and save the result in con->in_msg.  Uses the
 * connection's private alloc_msg op if available.
 *
 * Returns 0 on success, or a negative error code.
 *
 * On success, if we set *skip = 1:
 *  - the next message should be skipped and ignored.
 *  - con->in_msg == NULL
 * or if we set *skip = 0:
 *  - con->in_msg is non-null.
 * On error (ENOMEM, EAGAIN, ...),
 *  - con->in_msg == NULL
 */
int ceph_con_in_msg_alloc(struct ceph_connection *con,
			  struct ceph_msg_header *hdr, int *skip)
{
	int middle_len = le32_to_cpu(hdr->middle_len);
	struct ceph_msg *msg;
	int ret = 0;

	BUG_ON(con->in_msg != NULL);
	BUG_ON(!con->ops->alloc_msg);

	mutex_unlock(&con->mutex);
	msg = con->ops->alloc_msg(con, hdr, skip);
	mutex_lock(&con->mutex);
	if (con->state != CEPH_CON_S_OPEN) {
		if (msg)
			ceph_msg_put(msg);
		return -EAGAIN;
	}
	if (msg) {
		BUG_ON(*skip);
		msg_con_set(msg, con);
		con->in_msg = msg;
	} else {
		/*
		 * Null message pointer means either we should skip
		 * this message or we couldn't allocate memory.  The
		 * former is not an error.
		 */
		if (*skip)
			return 0;

		con->error_msg = "error allocating memory for incoming message";
		return -ENOMEM;
	}
	memcpy(&con->in_msg->hdr, hdr, sizeof(*hdr));

	if (middle_len && !con->in_msg->middle) {
		ret = ceph_alloc_middle(con, con->in_msg);
		if (ret < 0) {
			ceph_msg_put(con->in_msg);
			con->in_msg = NULL;
		}
	}

	return ret;
}

void ceph_con_get_out_msg(struct ceph_connection *con)
{
	struct ceph_msg *msg;

	BUG_ON(list_empty(&con->out_queue));
	msg = list_first_entry(&con->out_queue, struct ceph_msg, list_head);
	WARN_ON(msg->con != con);

	/*
	 * Put the message on "sent" list using a ref from ceph_con_send().
	 * It is put when the message is acked or revoked.
	 */
	list_move_tail(&msg->list_head, &con->out_sent);

	/*
	 * Only assign outgoing seq # if we haven't sent this message
	 * yet.  If it is requeued, resend with it's original seq.
	 */
	if (msg->needs_out_seq) {
		msg->hdr.seq = cpu_to_le64(++con->out_seq);
		msg->needs_out_seq = false;

		if (con->ops->reencode_message)
			con->ops->reencode_message(msg);
	}

	/*
	 * Get a ref for out_msg.  It is put when we are done sending the
	 * message or in case of a fault.
	 */
	WARN_ON(con->out_msg);
	con->out_msg = ceph_msg_get(msg);
}

/*
 * Free a generically kmalloc'd message.
 */
static void ceph_msg_free(struct ceph_msg *m)
{
	dout("%s %p\n", __func__, m);
	kvfree(m->front.iov_base);
	kfree(m->data);
	kmem_cache_free(ceph_msg_cache, m);
}

static void ceph_msg_release(struct kref *kref)
{
	struct ceph_msg *m = container_of(kref, struct ceph_msg, kref);
	int i;

	dout("%s %p\n", __func__, m);
	WARN_ON(!list_empty(&m->list_head));

	msg_con_set(m, NULL);

	/* drop middle, data, if any */
	if (m->middle) {
		ceph_buffer_put(m->middle);
		m->middle = NULL;
	}

	for (i = 0; i < m->num_data_items; i++)
		ceph_msg_data_destroy(&m->data[i]);

	if (m->pool)
		ceph_msgpool_put(m->pool, m);
	else
		ceph_msg_free(m);
}

struct ceph_msg *ceph_msg_get(struct ceph_msg *msg)
{
	dout("%s %p (was %d)\n", __func__, msg,
	     kref_read(&msg->kref));
	kref_get(&msg->kref);
	return msg;
}
EXPORT_SYMBOL(ceph_msg_get);

void ceph_msg_put(struct ceph_msg *msg)
{
	dout("%s %p (was %d)\n", __func__, msg,
	     kref_read(&msg->kref));
	kref_put(&msg->kref, ceph_msg_release);
}
EXPORT_SYMBOL(ceph_msg_put);

void ceph_msg_dump(struct ceph_msg *msg)
{
	pr_debug("msg_dump %p (front_alloc_len %d length %zd)\n", msg,
		 msg->front_alloc_len, msg->data_length);
	print_hex_dump(KERN_DEBUG, "header: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       &msg->hdr, sizeof(msg->hdr), true);
	print_hex_dump(KERN_DEBUG, " front: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       msg->front.iov_base, msg->front.iov_len, true);
	if (msg->middle)
		print_hex_dump(KERN_DEBUG, "middle: ",
			       DUMP_PREFIX_OFFSET, 16, 1,
			       msg->middle->vec.iov_base,
			       msg->middle->vec.iov_len, true);
	print_hex_dump(KERN_DEBUG, "footer: ",
		       DUMP_PREFIX_OFFSET, 16, 1,
		       &msg->footer, sizeof(msg->footer), true);
}
EXPORT_SYMBOL(ceph_msg_dump);
