#include <linux/ceph/ceph_debug.h>

#include <linux/crc32c.h>
#include <linux/ctype.h>
#include <linux/highmem.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/dns_resolver.h>
#include <net/tcp.h>

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

/* static tag bytes (protocol control messages) */
static char tag_msg = CEPH_MSGR_TAG_MSG;
static char tag_ack = CEPH_MSGR_TAG_ACK;
static char tag_keepalive = CEPH_MSGR_TAG_KEEPALIVE;

#ifdef CONFIG_LOCKDEP
static struct lock_class_key socket_class;
#endif


static void queue_con(struct ceph_connection *con);
static void con_work(struct work_struct *);
static void ceph_fault(struct ceph_connection *con);

/*
 * nicely render a sockaddr as a string.
 */
#define MAX_ADDR_STR 20
#define MAX_ADDR_STR_LEN 60
static char addr_str[MAX_ADDR_STR][MAX_ADDR_STR_LEN];
static DEFINE_SPINLOCK(addr_str_lock);
static int last_addr_str;

const char *ceph_pr_addr(const struct sockaddr_storage *ss)
{
	int i;
	char *s;
	struct sockaddr_in *in4 = (void *)ss;
	struct sockaddr_in6 *in6 = (void *)ss;

	spin_lock(&addr_str_lock);
	i = last_addr_str++;
	if (last_addr_str == MAX_ADDR_STR)
		last_addr_str = 0;
	spin_unlock(&addr_str_lock);
	s = addr_str[i];

	switch (ss->ss_family) {
	case AF_INET:
		snprintf(s, MAX_ADDR_STR_LEN, "%pI4:%u", &in4->sin_addr,
			 (unsigned int)ntohs(in4->sin_port));
		break;

	case AF_INET6:
		snprintf(s, MAX_ADDR_STR_LEN, "[%pI6c]:%u", &in6->sin6_addr,
			 (unsigned int)ntohs(in6->sin6_port));
		break;

	default:
		snprintf(s, MAX_ADDR_STR_LEN, "(unknown sockaddr family %d)",
			 (int)ss->ss_family);
	}

	return s;
}
EXPORT_SYMBOL(ceph_pr_addr);

static void encode_my_addr(struct ceph_messenger *msgr)
{
	memcpy(&msgr->my_enc_addr, &msgr->inst.addr, sizeof(msgr->my_enc_addr));
	ceph_encode_addr(&msgr->my_enc_addr);
}

/*
 * work queue for all reading and writing to/from the socket.
 */
struct workqueue_struct *ceph_msgr_wq;

int ceph_msgr_init(void)
{
	ceph_msgr_wq = alloc_workqueue("ceph-msgr", WQ_NON_REENTRANT, 0);
	if (!ceph_msgr_wq) {
		pr_err("msgr_init failed to create workqueue\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(ceph_msgr_init);

void ceph_msgr_exit(void)
{
	destroy_workqueue(ceph_msgr_wq);
}
EXPORT_SYMBOL(ceph_msgr_exit);

void ceph_msgr_flush(void)
{
	flush_workqueue(ceph_msgr_wq);
}
EXPORT_SYMBOL(ceph_msgr_flush);


/*
 * socket callback functions
 */

/* data available on socket, or listen socket received a connect */
static void ceph_data_ready(struct sock *sk, int count_unused)
{
	struct ceph_connection *con =
		(struct ceph_connection *)sk->sk_user_data;
	if (sk->sk_state != TCP_CLOSE_WAIT) {
		dout("ceph_data_ready on %p state = %lu, queueing work\n",
		     con, con->state);
		queue_con(con);
	}
}

/* socket has buffer space for writing */
static void ceph_write_space(struct sock *sk)
{
	struct ceph_connection *con =
		(struct ceph_connection *)sk->sk_user_data;

	/* only queue to workqueue if there is data we want to write. */
	if (test_bit(WRITE_PENDING, &con->state)) {
		dout("ceph_write_space %p queueing write work\n", con);
		queue_con(con);
	} else {
		dout("ceph_write_space %p nothing to write\n", con);
	}

	/* since we have our own write_space, clear the SOCK_NOSPACE flag */
	clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
}

/* socket's state has changed */
static void ceph_state_change(struct sock *sk)
{
	struct ceph_connection *con =
		(struct ceph_connection *)sk->sk_user_data;

	dout("ceph_state_change %p state = %lu sk_state = %u\n",
	     con, con->state, sk->sk_state);

	if (test_bit(CLOSED, &con->state))
		return;

	switch (sk->sk_state) {
	case TCP_CLOSE:
		dout("ceph_state_change TCP_CLOSE\n");
	case TCP_CLOSE_WAIT:
		dout("ceph_state_change TCP_CLOSE_WAIT\n");
		if (test_and_set_bit(SOCK_CLOSED, &con->state) == 0) {
			if (test_bit(CONNECTING, &con->state))
				con->error_msg = "connection failed";
			else
				con->error_msg = "socket closed";
			queue_con(con);
		}
		break;
	case TCP_ESTABLISHED:
		dout("ceph_state_change TCP_ESTABLISHED\n");
		queue_con(con);
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
	sk->sk_user_data = (void *)con;
	sk->sk_data_ready = ceph_data_ready;
	sk->sk_write_space = ceph_write_space;
	sk->sk_state_change = ceph_state_change;
}


/*
 * socket helpers
 */

/*
 * initiate connection to a remote socket.
 */
static struct socket *ceph_tcp_connect(struct ceph_connection *con)
{
	struct sockaddr_storage *paddr = &con->peer_addr.in_addr;
	struct socket *sock;
	int ret;

	BUG_ON(con->sock);
	ret = sock_create_kern(con->peer_addr.in_addr.ss_family, SOCK_STREAM,
			       IPPROTO_TCP, &sock);
	if (ret)
		return ERR_PTR(ret);
	con->sock = sock;
	sock->sk->sk_allocation = GFP_NOFS;

#ifdef CONFIG_LOCKDEP
	lockdep_set_class(&sock->sk->sk_lock, &socket_class);
#endif

	set_sock_callbacks(sock, con);

	dout("connect %s\n", ceph_pr_addr(&con->peer_addr.in_addr));

	ret = sock->ops->connect(sock, (struct sockaddr *)paddr, sizeof(*paddr),
				 O_NONBLOCK);
	if (ret == -EINPROGRESS) {
		dout("connect %s EINPROGRESS sk_state = %u\n",
		     ceph_pr_addr(&con->peer_addr.in_addr),
		     sock->sk->sk_state);
		ret = 0;
	}
	if (ret < 0) {
		pr_err("connect %s error %d\n",
		       ceph_pr_addr(&con->peer_addr.in_addr), ret);
		sock_release(sock);
		con->sock = NULL;
		con->error_msg = "connect error";
	}

	if (ret < 0)
		return ERR_PTR(ret);
	return sock;
}

static int ceph_tcp_recvmsg(struct socket *sock, void *buf, size_t len)
{
	struct kvec iov = {buf, len};
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL };
	int r;

	r = kernel_recvmsg(sock, &msg, &iov, 1, len, msg.msg_flags);
	if (r == -EAGAIN)
		r = 0;
	return r;
}

/*
 * write something.  @more is true if caller will be sending more data
 * shortly.
 */
static int ceph_tcp_sendmsg(struct socket *sock, struct kvec *iov,
		     size_t kvlen, size_t len, int more)
{
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL };
	int r;

	if (more)
		msg.msg_flags |= MSG_MORE;
	else
		msg.msg_flags |= MSG_EOR;  /* superfluous, but what the hell */

	r = kernel_sendmsg(sock, &msg, iov, kvlen, len);
	if (r == -EAGAIN)
		r = 0;
	return r;
}


/*
 * Shutdown/close the socket for the given connection.
 */
static int con_close_socket(struct ceph_connection *con)
{
	int rc;

	dout("con_close_socket on %p sock %p\n", con, con->sock);
	if (!con->sock)
		return 0;
	set_bit(SOCK_CLOSED, &con->state);
	rc = con->sock->ops->shutdown(con->sock, SHUT_RDWR);
	sock_release(con->sock);
	con->sock = NULL;
	clear_bit(SOCK_CLOSED, &con->state);
	return rc;
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

static void reset_connection(struct ceph_connection *con)
{
	/* reset connection, out_queue, msg_ and connect_seq */
	/* discard existing out_queue and msg_seq */
	ceph_msg_remove_list(&con->out_queue);
	ceph_msg_remove_list(&con->out_sent);

	if (con->in_msg) {
		ceph_msg_put(con->in_msg);
		con->in_msg = NULL;
	}

	con->connect_seq = 0;
	con->out_seq = 0;
	if (con->out_msg) {
		ceph_msg_put(con->out_msg);
		con->out_msg = NULL;
	}
	con->in_seq = 0;
	con->in_seq_acked = 0;
}

/*
 * mark a peer down.  drop any open connections.
 */
void ceph_con_close(struct ceph_connection *con)
{
	dout("con_close %p peer %s\n", con,
	     ceph_pr_addr(&con->peer_addr.in_addr));
	set_bit(CLOSED, &con->state);  /* in case there's queued work */
	clear_bit(STANDBY, &con->state);  /* avoid connect_seq bump */
	clear_bit(LOSSYTX, &con->state);  /* so we retry next connect */
	clear_bit(KEEPALIVE_PENDING, &con->state);
	clear_bit(WRITE_PENDING, &con->state);
	mutex_lock(&con->mutex);
	reset_connection(con);
	con->peer_global_seq = 0;
	cancel_delayed_work(&con->work);
	mutex_unlock(&con->mutex);
	queue_con(con);
}
EXPORT_SYMBOL(ceph_con_close);

/*
 * Reopen a closed connection, with a new peer address.
 */
void ceph_con_open(struct ceph_connection *con, struct ceph_entity_addr *addr)
{
	dout("con_open %p %s\n", con, ceph_pr_addr(&addr->in_addr));
	set_bit(OPENING, &con->state);
	clear_bit(CLOSED, &con->state);
	memcpy(&con->peer_addr, addr, sizeof(*addr));
	con->delay = 0;      /* reset backoff memory */
	queue_con(con);
}
EXPORT_SYMBOL(ceph_con_open);

/*
 * return true if this connection ever successfully opened
 */
bool ceph_con_opened(struct ceph_connection *con)
{
	return con->connect_seq > 0;
}

/*
 * generic get/put
 */
struct ceph_connection *ceph_con_get(struct ceph_connection *con)
{
	dout("con_get %p nref = %d -> %d\n", con,
	     atomic_read(&con->nref), atomic_read(&con->nref) + 1);
	if (atomic_inc_not_zero(&con->nref))
		return con;
	return NULL;
}

void ceph_con_put(struct ceph_connection *con)
{
	dout("con_put %p nref = %d -> %d\n", con,
	     atomic_read(&con->nref), atomic_read(&con->nref) - 1);
	BUG_ON(atomic_read(&con->nref) == 0);
	if (atomic_dec_and_test(&con->nref)) {
		BUG_ON(con->sock);
		kfree(con);
	}
}

/*
 * initialize a new connection.
 */
void ceph_con_init(struct ceph_messenger *msgr, struct ceph_connection *con)
{
	dout("con_init %p\n", con);
	memset(con, 0, sizeof(*con));
	atomic_set(&con->nref, 1);
	con->msgr = msgr;
	mutex_init(&con->mutex);
	INIT_LIST_HEAD(&con->out_queue);
	INIT_LIST_HEAD(&con->out_sent);
	INIT_DELAYED_WORK(&con->work, con_work);
}
EXPORT_SYMBOL(ceph_con_init);


/*
 * We maintain a global counter to order connection attempts.  Get
 * a unique seq greater than @gt.
 */
static u32 get_global_seq(struct ceph_messenger *msgr, u32 gt)
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
 * Prepare footer for currently outgoing message, and finish things
 * off.  Assumes out_kvec* are already valid.. we just add on to the end.
 */
static void prepare_write_message_footer(struct ceph_connection *con, int v)
{
	struct ceph_msg *m = con->out_msg;

	dout("prepare_write_message_footer %p\n", con);
	con->out_kvec_is_msg = true;
	con->out_kvec[v].iov_base = &m->footer;
	con->out_kvec[v].iov_len = sizeof(m->footer);
	con->out_kvec_bytes += sizeof(m->footer);
	con->out_kvec_left++;
	con->out_more = m->more_to_follow;
	con->out_msg_done = true;
}

/*
 * Prepare headers for the next outgoing message.
 */
static void prepare_write_message(struct ceph_connection *con)
{
	struct ceph_msg *m;
	int v = 0;

	con->out_kvec_bytes = 0;
	con->out_kvec_is_msg = true;
	con->out_msg_done = false;

	/* Sneak an ack in there first?  If we can get it into the same
	 * TCP packet that's a good thing. */
	if (con->in_seq > con->in_seq_acked) {
		con->in_seq_acked = con->in_seq;
		con->out_kvec[v].iov_base = &tag_ack;
		con->out_kvec[v++].iov_len = 1;
		con->out_temp_ack = cpu_to_le64(con->in_seq_acked);
		con->out_kvec[v].iov_base = &con->out_temp_ack;
		con->out_kvec[v++].iov_len = sizeof(con->out_temp_ack);
		con->out_kvec_bytes = 1 + sizeof(con->out_temp_ack);
	}

	m = list_first_entry(&con->out_queue,
		       struct ceph_msg, list_head);
	con->out_msg = m;

	/* put message on sent list */
	ceph_msg_get(m);
	list_move_tail(&m->list_head, &con->out_sent);

	/*
	 * only assign outgoing seq # if we haven't sent this message
	 * yet.  if it is requeued, resend with it's original seq.
	 */
	if (m->needs_out_seq) {
		m->hdr.seq = cpu_to_le64(++con->out_seq);
		m->needs_out_seq = false;
	}

	dout("prepare_write_message %p seq %lld type %d len %d+%d+%d %d pgs\n",
	     m, con->out_seq, le16_to_cpu(m->hdr.type),
	     le32_to_cpu(m->hdr.front_len), le32_to_cpu(m->hdr.middle_len),
	     le32_to_cpu(m->hdr.data_len),
	     m->nr_pages);
	BUG_ON(le32_to_cpu(m->hdr.front_len) != m->front.iov_len);

	/* tag + hdr + front + middle */
	con->out_kvec[v].iov_base = &tag_msg;
	con->out_kvec[v++].iov_len = 1;
	con->out_kvec[v].iov_base = &m->hdr;
	con->out_kvec[v++].iov_len = sizeof(m->hdr);
	con->out_kvec[v++] = m->front;
	if (m->middle)
		con->out_kvec[v++] = m->middle->vec;
	con->out_kvec_left = v;
	con->out_kvec_bytes += 1 + sizeof(m->hdr) + m->front.iov_len +
		(m->middle ? m->middle->vec.iov_len : 0);
	con->out_kvec_cur = con->out_kvec;

	/* fill in crc (except data pages), footer */
	con->out_msg->hdr.crc =
		cpu_to_le32(crc32c(0, (void *)&m->hdr,
				      sizeof(m->hdr) - sizeof(m->hdr.crc)));
	con->out_msg->footer.flags = CEPH_MSG_FOOTER_COMPLETE;
	con->out_msg->footer.front_crc =
		cpu_to_le32(crc32c(0, m->front.iov_base, m->front.iov_len));
	if (m->middle)
		con->out_msg->footer.middle_crc =
			cpu_to_le32(crc32c(0, m->middle->vec.iov_base,
					   m->middle->vec.iov_len));
	else
		con->out_msg->footer.middle_crc = 0;
	con->out_msg->footer.data_crc = 0;
	dout("prepare_write_message front_crc %u data_crc %u\n",
	     le32_to_cpu(con->out_msg->footer.front_crc),
	     le32_to_cpu(con->out_msg->footer.middle_crc));

	/* is there a data payload? */
	if (le32_to_cpu(m->hdr.data_len) > 0) {
		/* initialize page iterator */
		con->out_msg_pos.page = 0;
		if (m->pages)
			con->out_msg_pos.page_pos = m->page_alignment;
		else
			con->out_msg_pos.page_pos = 0;
		con->out_msg_pos.data_pos = 0;
		con->out_msg_pos.did_page_crc = 0;
		con->out_more = 1;  /* data + footer will follow */
	} else {
		/* no, queue up footer too and be done */
		prepare_write_message_footer(con, v);
	}

	set_bit(WRITE_PENDING, &con->state);
}

/*
 * Prepare an ack.
 */
static void prepare_write_ack(struct ceph_connection *con)
{
	dout("prepare_write_ack %p %llu -> %llu\n", con,
	     con->in_seq_acked, con->in_seq);
	con->in_seq_acked = con->in_seq;

	con->out_kvec[0].iov_base = &tag_ack;
	con->out_kvec[0].iov_len = 1;
	con->out_temp_ack = cpu_to_le64(con->in_seq_acked);
	con->out_kvec[1].iov_base = &con->out_temp_ack;
	con->out_kvec[1].iov_len = sizeof(con->out_temp_ack);
	con->out_kvec_left = 2;
	con->out_kvec_bytes = 1 + sizeof(con->out_temp_ack);
	con->out_kvec_cur = con->out_kvec;
	con->out_more = 1;  /* more will follow.. eventually.. */
	set_bit(WRITE_PENDING, &con->state);
}

/*
 * Prepare to write keepalive byte.
 */
static void prepare_write_keepalive(struct ceph_connection *con)
{
	dout("prepare_write_keepalive %p\n", con);
	con->out_kvec[0].iov_base = &tag_keepalive;
	con->out_kvec[0].iov_len = 1;
	con->out_kvec_left = 1;
	con->out_kvec_bytes = 1;
	con->out_kvec_cur = con->out_kvec;
	set_bit(WRITE_PENDING, &con->state);
}

/*
 * Connection negotiation.
 */

static int prepare_connect_authorizer(struct ceph_connection *con)
{
	void *auth_buf;
	int auth_len = 0;
	int auth_protocol = 0;

	mutex_unlock(&con->mutex);
	if (con->ops->get_authorizer)
		con->ops->get_authorizer(con, &auth_buf, &auth_len,
					 &auth_protocol, &con->auth_reply_buf,
					 &con->auth_reply_buf_len,
					 con->auth_retry);
	mutex_lock(&con->mutex);

	if (test_bit(CLOSED, &con->state) ||
	    test_bit(OPENING, &con->state))
		return -EAGAIN;

	con->out_connect.authorizer_protocol = cpu_to_le32(auth_protocol);
	con->out_connect.authorizer_len = cpu_to_le32(auth_len);

	if (auth_len) {
		con->out_kvec[con->out_kvec_left].iov_base = auth_buf;
		con->out_kvec[con->out_kvec_left].iov_len = auth_len;
		con->out_kvec_left++;
		con->out_kvec_bytes += auth_len;
	}
	return 0;
}

/*
 * We connected to a peer and are saying hello.
 */
static void prepare_write_banner(struct ceph_messenger *msgr,
				 struct ceph_connection *con)
{
	int len = strlen(CEPH_BANNER);

	con->out_kvec[0].iov_base = CEPH_BANNER;
	con->out_kvec[0].iov_len = len;
	con->out_kvec[1].iov_base = &msgr->my_enc_addr;
	con->out_kvec[1].iov_len = sizeof(msgr->my_enc_addr);
	con->out_kvec_left = 2;
	con->out_kvec_bytes = len + sizeof(msgr->my_enc_addr);
	con->out_kvec_cur = con->out_kvec;
	con->out_more = 0;
	set_bit(WRITE_PENDING, &con->state);
}

static int prepare_write_connect(struct ceph_messenger *msgr,
				 struct ceph_connection *con,
				 int after_banner)
{
	unsigned global_seq = get_global_seq(con->msgr, 0);
	int proto;

	switch (con->peer_name.type) {
	case CEPH_ENTITY_TYPE_MON:
		proto = CEPH_MONC_PROTOCOL;
		break;
	case CEPH_ENTITY_TYPE_OSD:
		proto = CEPH_OSDC_PROTOCOL;
		break;
	case CEPH_ENTITY_TYPE_MDS:
		proto = CEPH_MDSC_PROTOCOL;
		break;
	default:
		BUG();
	}

	dout("prepare_write_connect %p cseq=%d gseq=%d proto=%d\n", con,
	     con->connect_seq, global_seq, proto);

	con->out_connect.features = cpu_to_le64(msgr->supported_features);
	con->out_connect.host_type = cpu_to_le32(CEPH_ENTITY_TYPE_CLIENT);
	con->out_connect.connect_seq = cpu_to_le32(con->connect_seq);
	con->out_connect.global_seq = cpu_to_le32(global_seq);
	con->out_connect.protocol_version = cpu_to_le32(proto);
	con->out_connect.flags = 0;

	if (!after_banner) {
		con->out_kvec_left = 0;
		con->out_kvec_bytes = 0;
	}
	con->out_kvec[con->out_kvec_left].iov_base = &con->out_connect;
	con->out_kvec[con->out_kvec_left].iov_len = sizeof(con->out_connect);
	con->out_kvec_left++;
	con->out_kvec_bytes += sizeof(con->out_connect);
	con->out_kvec_cur = con->out_kvec;
	con->out_more = 0;
	set_bit(WRITE_PENDING, &con->state);

	return prepare_connect_authorizer(con);
}


/*
 * write as much of pending kvecs to the socket as we can.
 *  1 -> done
 *  0 -> socket full, but more to do
 * <0 -> error
 */
static int write_partial_kvec(struct ceph_connection *con)
{
	int ret;

	dout("write_partial_kvec %p %d left\n", con, con->out_kvec_bytes);
	while (con->out_kvec_bytes > 0) {
		ret = ceph_tcp_sendmsg(con->sock, con->out_kvec_cur,
				       con->out_kvec_left, con->out_kvec_bytes,
				       con->out_more);
		if (ret <= 0)
			goto out;
		con->out_kvec_bytes -= ret;
		if (con->out_kvec_bytes == 0)
			break;            /* done */
		while (ret > 0) {
			if (ret >= con->out_kvec_cur->iov_len) {
				ret -= con->out_kvec_cur->iov_len;
				con->out_kvec_cur++;
				con->out_kvec_left--;
			} else {
				con->out_kvec_cur->iov_len -= ret;
				con->out_kvec_cur->iov_base += ret;
				ret = 0;
				break;
			}
		}
	}
	con->out_kvec_left = 0;
	con->out_kvec_is_msg = false;
	ret = 1;
out:
	dout("write_partial_kvec %p %d left in %d kvecs ret = %d\n", con,
	     con->out_kvec_bytes, con->out_kvec_left, ret);
	return ret;  /* done! */
}

#ifdef CONFIG_BLOCK
static void init_bio_iter(struct bio *bio, struct bio **iter, int *seg)
{
	if (!bio) {
		*iter = NULL;
		*seg = 0;
		return;
	}
	*iter = bio;
	*seg = bio->bi_idx;
}

static void iter_bio_next(struct bio **bio_iter, int *seg)
{
	if (*bio_iter == NULL)
		return;

	BUG_ON(*seg >= (*bio_iter)->bi_vcnt);

	(*seg)++;
	if (*seg == (*bio_iter)->bi_vcnt)
		init_bio_iter((*bio_iter)->bi_next, bio_iter, seg);
}
#endif

/*
 * Write as much message data payload as we can.  If we finish, queue
 * up the footer.
 *  1 -> done, footer is now queued in out_kvec[].
 *  0 -> socket full, but more to do
 * <0 -> error
 */
static int write_partial_msg_pages(struct ceph_connection *con)
{
	struct ceph_msg *msg = con->out_msg;
	unsigned data_len = le32_to_cpu(msg->hdr.data_len);
	size_t len;
	int crc = con->msgr->nocrc;
	int ret;
	int total_max_write;
	int in_trail = 0;
	size_t trail_len = (msg->trail ? msg->trail->length : 0);

	dout("write_partial_msg_pages %p msg %p page %d/%d offset %d\n",
	     con, con->out_msg, con->out_msg_pos.page, con->out_msg->nr_pages,
	     con->out_msg_pos.page_pos);

#ifdef CONFIG_BLOCK
	if (msg->bio && !msg->bio_iter)
		init_bio_iter(msg->bio, &msg->bio_iter, &msg->bio_seg);
#endif

	while (data_len > con->out_msg_pos.data_pos) {
		struct page *page = NULL;
		void *kaddr = NULL;
		int max_write = PAGE_SIZE;
		int page_shift = 0;

		total_max_write = data_len - trail_len -
			con->out_msg_pos.data_pos;

		/*
		 * if we are calculating the data crc (the default), we need
		 * to map the page.  if our pages[] has been revoked, use the
		 * zero page.
		 */

		/* have we reached the trail part of the data? */
		if (con->out_msg_pos.data_pos >= data_len - trail_len) {
			in_trail = 1;

			total_max_write = data_len - con->out_msg_pos.data_pos;

			page = list_first_entry(&msg->trail->head,
						struct page, lru);
			if (crc)
				kaddr = kmap(page);
			max_write = PAGE_SIZE;
		} else if (msg->pages) {
			page = msg->pages[con->out_msg_pos.page];
			if (crc)
				kaddr = kmap(page);
		} else if (msg->pagelist) {
			page = list_first_entry(&msg->pagelist->head,
						struct page, lru);
			if (crc)
				kaddr = kmap(page);
#ifdef CONFIG_BLOCK
		} else if (msg->bio) {
			struct bio_vec *bv;

			bv = bio_iovec_idx(msg->bio_iter, msg->bio_seg);
			page = bv->bv_page;
			page_shift = bv->bv_offset;
			if (crc)
				kaddr = kmap(page) + page_shift;
			max_write = bv->bv_len;
#endif
		} else {
			page = con->msgr->zero_page;
			if (crc)
				kaddr = page_address(con->msgr->zero_page);
		}
		len = min_t(int, max_write - con->out_msg_pos.page_pos,
			    total_max_write);

		if (crc && !con->out_msg_pos.did_page_crc) {
			void *base = kaddr + con->out_msg_pos.page_pos;
			u32 tmpcrc = le32_to_cpu(con->out_msg->footer.data_crc);

			BUG_ON(kaddr == NULL);
			con->out_msg->footer.data_crc =
				cpu_to_le32(crc32c(tmpcrc, base, len));
			con->out_msg_pos.did_page_crc = 1;
		}
		ret = kernel_sendpage(con->sock, page,
				      con->out_msg_pos.page_pos + page_shift,
				      len,
				      MSG_DONTWAIT | MSG_NOSIGNAL |
				      MSG_MORE);

		if (crc &&
		    (msg->pages || msg->pagelist || msg->bio || in_trail))
			kunmap(page);

		if (ret == -EAGAIN)
			ret = 0;
		if (ret <= 0)
			goto out;

		con->out_msg_pos.data_pos += ret;
		con->out_msg_pos.page_pos += ret;
		if (ret == len) {
			con->out_msg_pos.page_pos = 0;
			con->out_msg_pos.page++;
			con->out_msg_pos.did_page_crc = 0;
			if (in_trail)
				list_move_tail(&page->lru,
					       &msg->trail->head);
			else if (msg->pagelist)
				list_move_tail(&page->lru,
					       &msg->pagelist->head);
#ifdef CONFIG_BLOCK
			else if (msg->bio)
				iter_bio_next(&msg->bio_iter, &msg->bio_seg);
#endif
		}
	}

	dout("write_partial_msg_pages %p msg %p done\n", con, msg);

	/* prepare and queue up footer, too */
	if (!crc)
		con->out_msg->footer.flags |= CEPH_MSG_FOOTER_NOCRC;
	con->out_kvec_bytes = 0;
	con->out_kvec_left = 0;
	con->out_kvec_cur = con->out_kvec;
	prepare_write_message_footer(con, 0);
	ret = 1;
out:
	return ret;
}

/*
 * write some zeros
 */
static int write_partial_skip(struct ceph_connection *con)
{
	int ret;

	while (con->out_skip > 0) {
		struct kvec iov = {
			.iov_base = page_address(con->msgr->zero_page),
			.iov_len = min(con->out_skip, (int)PAGE_CACHE_SIZE)
		};

		ret = ceph_tcp_sendmsg(con->sock, &iov, 1, iov.iov_len, 1);
		if (ret <= 0)
			goto out;
		con->out_skip -= ret;
	}
	ret = 1;
out:
	return ret;
}

/*
 * Prepare to read connection handshake, or an ack.
 */
static void prepare_read_banner(struct ceph_connection *con)
{
	dout("prepare_read_banner %p\n", con);
	con->in_base_pos = 0;
}

static void prepare_read_connect(struct ceph_connection *con)
{
	dout("prepare_read_connect %p\n", con);
	con->in_base_pos = 0;
}

static void prepare_read_ack(struct ceph_connection *con)
{
	dout("prepare_read_ack %p\n", con);
	con->in_base_pos = 0;
}

static void prepare_read_tag(struct ceph_connection *con)
{
	dout("prepare_read_tag %p\n", con);
	con->in_base_pos = 0;
	con->in_tag = CEPH_MSGR_TAG_READY;
}

/*
 * Prepare to read a message.
 */
static int prepare_read_message(struct ceph_connection *con)
{
	dout("prepare_read_message %p\n", con);
	BUG_ON(con->in_msg != NULL);
	con->in_base_pos = 0;
	con->in_front_crc = con->in_middle_crc = con->in_data_crc = 0;
	return 0;
}


static int read_partial(struct ceph_connection *con,
			int *to, int size, void *object)
{
	*to += size;
	while (con->in_base_pos < *to) {
		int left = *to - con->in_base_pos;
		int have = size - left;
		int ret = ceph_tcp_recvmsg(con->sock, object + have, left);
		if (ret <= 0)
			return ret;
		con->in_base_pos += ret;
	}
	return 1;
}


/*
 * Read all or part of the connect-side handshake on a new connection
 */
static int read_partial_banner(struct ceph_connection *con)
{
	int ret, to = 0;

	dout("read_partial_banner %p at %d\n", con, con->in_base_pos);

	/* peer's banner */
	ret = read_partial(con, &to, strlen(CEPH_BANNER), con->in_banner);
	if (ret <= 0)
		goto out;
	ret = read_partial(con, &to, sizeof(con->actual_peer_addr),
			   &con->actual_peer_addr);
	if (ret <= 0)
		goto out;
	ret = read_partial(con, &to, sizeof(con->peer_addr_for_me),
			   &con->peer_addr_for_me);
	if (ret <= 0)
		goto out;
out:
	return ret;
}

static int read_partial_connect(struct ceph_connection *con)
{
	int ret, to = 0;

	dout("read_partial_connect %p at %d\n", con, con->in_base_pos);

	ret = read_partial(con, &to, sizeof(con->in_reply), &con->in_reply);
	if (ret <= 0)
		goto out;
	ret = read_partial(con, &to, le32_to_cpu(con->in_reply.authorizer_len),
			   con->auth_reply_buf);
	if (ret <= 0)
		goto out;

	dout("read_partial_connect %p tag %d, con_seq = %u, g_seq = %u\n",
	     con, (int)con->in_reply.tag,
	     le32_to_cpu(con->in_reply.connect_seq),
	     le32_to_cpu(con->in_reply.global_seq));
out:
	return ret;

}

/*
 * Verify the hello banner looks okay.
 */
static int verify_hello(struct ceph_connection *con)
{
	if (memcmp(con->in_banner, CEPH_BANNER, strlen(CEPH_BANNER))) {
		pr_err("connect to %s got bad banner\n",
		       ceph_pr_addr(&con->peer_addr.in_addr));
		con->error_msg = "protocol error, bad banner";
		return -1;
	}
	return 0;
}

static bool addr_is_blank(struct sockaddr_storage *ss)
{
	switch (ss->ss_family) {
	case AF_INET:
		return ((struct sockaddr_in *)ss)->sin_addr.s_addr == 0;
	case AF_INET6:
		return
		     ((struct sockaddr_in6 *)ss)->sin6_addr.s6_addr32[0] == 0 &&
		     ((struct sockaddr_in6 *)ss)->sin6_addr.s6_addr32[1] == 0 &&
		     ((struct sockaddr_in6 *)ss)->sin6_addr.s6_addr32[2] == 0 &&
		     ((struct sockaddr_in6 *)ss)->sin6_addr.s6_addr32[3] == 0;
	}
	return false;
}

static int addr_port(struct sockaddr_storage *ss)
{
	switch (ss->ss_family) {
	case AF_INET:
		return ntohs(((struct sockaddr_in *)ss)->sin_port);
	case AF_INET6:
		return ntohs(((struct sockaddr_in6 *)ss)->sin6_port);
	}
	return 0;
}

static void addr_set_port(struct sockaddr_storage *ss, int p)
{
	switch (ss->ss_family) {
	case AF_INET:
		((struct sockaddr_in *)ss)->sin_port = htons(p);
		break;
	case AF_INET6:
		((struct sockaddr_in6 *)ss)->sin6_port = htons(p);
		break;
	}
}

/*
 * Unlike other *_pton function semantics, zero indicates success.
 */
static int ceph_pton(const char *str, size_t len, struct sockaddr_storage *ss,
		char delim, const char **ipend)
{
	struct sockaddr_in *in4 = (void *)ss;
	struct sockaddr_in6 *in6 = (void *)ss;

	memset(ss, 0, sizeof(*ss));

	if (in4_pton(str, len, (u8 *)&in4->sin_addr.s_addr, delim, ipend)) {
		ss->ss_family = AF_INET;
		return 0;
	}

	if (in6_pton(str, len, (u8 *)&in6->sin6_addr.s6_addr, delim, ipend)) {
		ss->ss_family = AF_INET6;
		return 0;
	}

	return -EINVAL;
}

/*
 * Extract hostname string and resolve using kernel DNS facility.
 */
#ifdef CONFIG_CEPH_LIB_USE_DNS_RESOLVER
static int ceph_dns_resolve_name(const char *name, size_t namelen,
		struct sockaddr_storage *ss, char delim, const char **ipend)
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
	ip_len = dns_query(NULL, name, end - name, NULL, &ip_addr, NULL);
	if (ip_len > 0)
		ret = ceph_pton(ip_addr, ip_len, ss, -1, NULL);
	else
		ret = -ESRCH;

	kfree(ip_addr);

	*ipend = end;

	pr_info("resolve '%.*s' (ret=%d): %s\n", (int)(end - name), name,
			ret, ret ? "failed" : ceph_pr_addr(ss));

	return ret;
}
#else
static inline int ceph_dns_resolve_name(const char *name, size_t namelen,
		struct sockaddr_storage *ss, char delim, const char **ipend)
{
	return -EINVAL;
}
#endif

/*
 * Parse a server name (IP or hostname). If a valid IP address is not found
 * then try to extract a hostname to resolve using userspace DNS upcall.
 */
static int ceph_parse_server_name(const char *name, size_t namelen,
			struct sockaddr_storage *ss, char delim, const char **ipend)
{
	int ret;

	ret = ceph_pton(name, namelen, ss, delim, ipend);
	if (ret)
		ret = ceph_dns_resolve_name(name, namelen, ss, delim, ipend);

	return ret;
}

/*
 * Parse an ip[:port] list into an addr array.  Use the default
 * monitor port if a port isn't specified.
 */
int ceph_parse_ips(const char *c, const char *end,
		   struct ceph_entity_addr *addr,
		   int max_count, int *count)
{
	int i, ret = -EINVAL;
	const char *p = c;

	dout("parse_ips on '%.*s'\n", (int)(end-c), c);
	for (i = 0; i < max_count; i++) {
		const char *ipend;
		struct sockaddr_storage *ss = &addr[i].in_addr;
		int port;
		char delim = ',';

		if (*p == '[') {
			delim = ']';
			p++;
		}

		ret = ceph_parse_server_name(p, end - p, ss, delim, &ipend);
		if (ret)
			goto bad;
		ret = -EINVAL;

		p = ipend;

		if (delim == ']') {
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
			if (port > 65535 || port == 0)
				goto bad;
		} else {
			port = CEPH_MON_PORT;
		}

		addr_set_port(ss, port);

		dout("parse_ips got %s\n", ceph_pr_addr(ss));

		if (p == end)
			break;
		if (*p != ',')
			goto bad;
		p++;
	}

	if (p != end)
		goto bad;

	if (count)
		*count = i + 1;
	return 0;

bad:
	pr_err("parse_ips bad ip '%.*s'\n", (int)(end - c), c);
	return ret;
}
EXPORT_SYMBOL(ceph_parse_ips);

static int process_banner(struct ceph_connection *con)
{
	dout("process_banner on %p\n", con);

	if (verify_hello(con) < 0)
		return -1;

	ceph_decode_addr(&con->actual_peer_addr);
	ceph_decode_addr(&con->peer_addr_for_me);

	/*
	 * Make sure the other end is who we wanted.  note that the other
	 * end may not yet know their ip address, so if it's 0.0.0.0, give
	 * them the benefit of the doubt.
	 */
	if (memcmp(&con->peer_addr, &con->actual_peer_addr,
		   sizeof(con->peer_addr)) != 0 &&
	    !(addr_is_blank(&con->actual_peer_addr.in_addr) &&
	      con->actual_peer_addr.nonce == con->peer_addr.nonce)) {
		pr_warning("wrong peer, want %s/%d, got %s/%d\n",
			   ceph_pr_addr(&con->peer_addr.in_addr),
			   (int)le32_to_cpu(con->peer_addr.nonce),
			   ceph_pr_addr(&con->actual_peer_addr.in_addr),
			   (int)le32_to_cpu(con->actual_peer_addr.nonce));
		con->error_msg = "wrong peer at address";
		return -1;
	}

	/*
	 * did we learn our address?
	 */
	if (addr_is_blank(&con->msgr->inst.addr.in_addr)) {
		int port = addr_port(&con->msgr->inst.addr.in_addr);

		memcpy(&con->msgr->inst.addr.in_addr,
		       &con->peer_addr_for_me.in_addr,
		       sizeof(con->peer_addr_for_me.in_addr));
		addr_set_port(&con->msgr->inst.addr.in_addr, port);
		encode_my_addr(con->msgr);
		dout("process_banner learned my addr is %s\n",
		     ceph_pr_addr(&con->msgr->inst.addr.in_addr));
	}

	set_bit(NEGOTIATING, &con->state);
	prepare_read_connect(con);
	return 0;
}

static void fail_protocol(struct ceph_connection *con)
{
	reset_connection(con);
	set_bit(CLOSED, &con->state);  /* in case there's queued work */

	mutex_unlock(&con->mutex);
	if (con->ops->bad_proto)
		con->ops->bad_proto(con);
	mutex_lock(&con->mutex);
}

static int process_connect(struct ceph_connection *con)
{
	u64 sup_feat = con->msgr->supported_features;
	u64 req_feat = con->msgr->required_features;
	u64 server_feat = le64_to_cpu(con->in_reply.features);
	int ret;

	dout("process_connect on %p tag %d\n", con, (int)con->in_tag);

	switch (con->in_reply.tag) {
	case CEPH_MSGR_TAG_FEATURES:
		pr_err("%s%lld %s feature set mismatch,"
		       " my %llx < server's %llx, missing %llx\n",
		       ENTITY_NAME(con->peer_name),
		       ceph_pr_addr(&con->peer_addr.in_addr),
		       sup_feat, server_feat, server_feat & ~sup_feat);
		con->error_msg = "missing required protocol features";
		fail_protocol(con);
		return -1;

	case CEPH_MSGR_TAG_BADPROTOVER:
		pr_err("%s%lld %s protocol version mismatch,"
		       " my %d != server's %d\n",
		       ENTITY_NAME(con->peer_name),
		       ceph_pr_addr(&con->peer_addr.in_addr),
		       le32_to_cpu(con->out_connect.protocol_version),
		       le32_to_cpu(con->in_reply.protocol_version));
		con->error_msg = "protocol version mismatch";
		fail_protocol(con);
		return -1;

	case CEPH_MSGR_TAG_BADAUTHORIZER:
		con->auth_retry++;
		dout("process_connect %p got BADAUTHORIZER attempt %d\n", con,
		     con->auth_retry);
		if (con->auth_retry == 2) {
			con->error_msg = "connect authorization failure";
			return -1;
		}
		con->auth_retry = 1;
		ret = prepare_write_connect(con->msgr, con, 0);
		if (ret < 0)
			return ret;
		prepare_read_connect(con);
		break;

	case CEPH_MSGR_TAG_RESETSESSION:
		/*
		 * If we connected with a large connect_seq but the peer
		 * has no record of a session with us (no connection, or
		 * connect_seq == 0), they will send RESETSESION to indicate
		 * that they must have reset their session, and may have
		 * dropped messages.
		 */
		dout("process_connect got RESET peer seq %u\n",
		     le32_to_cpu(con->in_connect.connect_seq));
		pr_err("%s%lld %s connection reset\n",
		       ENTITY_NAME(con->peer_name),
		       ceph_pr_addr(&con->peer_addr.in_addr));
		reset_connection(con);
		prepare_write_connect(con->msgr, con, 0);
		prepare_read_connect(con);

		/* Tell ceph about it. */
		mutex_unlock(&con->mutex);
		pr_info("reset on %s%lld\n", ENTITY_NAME(con->peer_name));
		if (con->ops->peer_reset)
			con->ops->peer_reset(con);
		mutex_lock(&con->mutex);
		if (test_bit(CLOSED, &con->state) ||
		    test_bit(OPENING, &con->state))
			return -EAGAIN;
		break;

	case CEPH_MSGR_TAG_RETRY_SESSION:
		/*
		 * If we sent a smaller connect_seq than the peer has, try
		 * again with a larger value.
		 */
		dout("process_connect got RETRY my seq = %u, peer_seq = %u\n",
		     le32_to_cpu(con->out_connect.connect_seq),
		     le32_to_cpu(con->in_connect.connect_seq));
		con->connect_seq = le32_to_cpu(con->in_connect.connect_seq);
		prepare_write_connect(con->msgr, con, 0);
		prepare_read_connect(con);
		break;

	case CEPH_MSGR_TAG_RETRY_GLOBAL:
		/*
		 * If we sent a smaller global_seq than the peer has, try
		 * again with a larger value.
		 */
		dout("process_connect got RETRY_GLOBAL my %u peer_gseq %u\n",
		     con->peer_global_seq,
		     le32_to_cpu(con->in_connect.global_seq));
		get_global_seq(con->msgr,
			       le32_to_cpu(con->in_connect.global_seq));
		prepare_write_connect(con->msgr, con, 0);
		prepare_read_connect(con);
		break;

	case CEPH_MSGR_TAG_READY:
		if (req_feat & ~server_feat) {
			pr_err("%s%lld %s protocol feature mismatch,"
			       " my required %llx > server's %llx, need %llx\n",
			       ENTITY_NAME(con->peer_name),
			       ceph_pr_addr(&con->peer_addr.in_addr),
			       req_feat, server_feat, req_feat & ~server_feat);
			con->error_msg = "missing required protocol features";
			fail_protocol(con);
			return -1;
		}
		clear_bit(CONNECTING, &con->state);
		con->peer_global_seq = le32_to_cpu(con->in_reply.global_seq);
		con->connect_seq++;
		con->peer_features = server_feat;
		dout("process_connect got READY gseq %d cseq %d (%d)\n",
		     con->peer_global_seq,
		     le32_to_cpu(con->in_reply.connect_seq),
		     con->connect_seq);
		WARN_ON(con->connect_seq !=
			le32_to_cpu(con->in_reply.connect_seq));

		if (con->in_reply.flags & CEPH_MSG_CONNECT_LOSSY)
			set_bit(LOSSYTX, &con->state);

		prepare_read_tag(con);
		break;

	case CEPH_MSGR_TAG_WAIT:
		/*
		 * If there is a connection race (we are opening
		 * connections to each other), one of us may just have
		 * to WAIT.  This shouldn't happen if we are the
		 * client.
		 */
		pr_err("process_connect got WAIT as client\n");
		con->error_msg = "protocol error, got WAIT as client";
		return -1;

	default:
		pr_err("connect protocol error, will retry\n");
		con->error_msg = "protocol error, garbage tag during connect";
		return -1;
	}
	return 0;
}


/*
 * read (part of) an ack
 */
static int read_partial_ack(struct ceph_connection *con)
{
	int to = 0;

	return read_partial(con, &to, sizeof(con->in_temp_ack),
			    &con->in_temp_ack);
}


/*
 * We can finally discard anything that's been acked.
 */
static void process_ack(struct ceph_connection *con)
{
	struct ceph_msg *m;
	u64 ack = le64_to_cpu(con->in_temp_ack);
	u64 seq;

	while (!list_empty(&con->out_sent)) {
		m = list_first_entry(&con->out_sent, struct ceph_msg,
				     list_head);
		seq = le64_to_cpu(m->hdr.seq);
		if (seq > ack)
			break;
		dout("got ack for seq %llu type %d at %p\n", seq,
		     le16_to_cpu(m->hdr.type), m);
		m->ack_stamp = jiffies;
		ceph_msg_remove(m);
	}
	prepare_read_tag(con);
}




static int read_partial_message_section(struct ceph_connection *con,
					struct kvec *section,
					unsigned int sec_len, u32 *crc)
{
	int ret, left;

	BUG_ON(!section);

	while (section->iov_len < sec_len) {
		BUG_ON(section->iov_base == NULL);
		left = sec_len - section->iov_len;
		ret = ceph_tcp_recvmsg(con->sock, (char *)section->iov_base +
				       section->iov_len, left);
		if (ret <= 0)
			return ret;
		section->iov_len += ret;
		if (section->iov_len == sec_len)
			*crc = crc32c(0, section->iov_base,
				      section->iov_len);
	}

	return 1;
}

static struct ceph_msg *ceph_alloc_msg(struct ceph_connection *con,
				struct ceph_msg_header *hdr,
				int *skip);


static int read_partial_message_pages(struct ceph_connection *con,
				      struct page **pages,
				      unsigned data_len, int datacrc)
{
	void *p;
	int ret;
	int left;

	left = min((int)(data_len - con->in_msg_pos.data_pos),
		   (int)(PAGE_SIZE - con->in_msg_pos.page_pos));
	/* (page) data */
	BUG_ON(pages == NULL);
	p = kmap(pages[con->in_msg_pos.page]);
	ret = ceph_tcp_recvmsg(con->sock, p + con->in_msg_pos.page_pos,
			       left);
	if (ret > 0 && datacrc)
		con->in_data_crc =
			crc32c(con->in_data_crc,
				  p + con->in_msg_pos.page_pos, ret);
	kunmap(pages[con->in_msg_pos.page]);
	if (ret <= 0)
		return ret;
	con->in_msg_pos.data_pos += ret;
	con->in_msg_pos.page_pos += ret;
	if (con->in_msg_pos.page_pos == PAGE_SIZE) {
		con->in_msg_pos.page_pos = 0;
		con->in_msg_pos.page++;
	}

	return ret;
}

#ifdef CONFIG_BLOCK
static int read_partial_message_bio(struct ceph_connection *con,
				    struct bio **bio_iter, int *bio_seg,
				    unsigned data_len, int datacrc)
{
	struct bio_vec *bv = bio_iovec_idx(*bio_iter, *bio_seg);
	void *p;
	int ret, left;

	if (IS_ERR(bv))
		return PTR_ERR(bv);

	left = min((int)(data_len - con->in_msg_pos.data_pos),
		   (int)(bv->bv_len - con->in_msg_pos.page_pos));

	p = kmap(bv->bv_page) + bv->bv_offset;

	ret = ceph_tcp_recvmsg(con->sock, p + con->in_msg_pos.page_pos,
			       left);
	if (ret > 0 && datacrc)
		con->in_data_crc =
			crc32c(con->in_data_crc,
				  p + con->in_msg_pos.page_pos, ret);
	kunmap(bv->bv_page);
	if (ret <= 0)
		return ret;
	con->in_msg_pos.data_pos += ret;
	con->in_msg_pos.page_pos += ret;
	if (con->in_msg_pos.page_pos == bv->bv_len) {
		con->in_msg_pos.page_pos = 0;
		iter_bio_next(bio_iter, bio_seg);
	}

	return ret;
}
#endif

/*
 * read (part of) a message.
 */
static int read_partial_message(struct ceph_connection *con)
{
	struct ceph_msg *m = con->in_msg;
	int ret;
	int to, left;
	unsigned front_len, middle_len, data_len;
	int datacrc = con->msgr->nocrc;
	int skip;
	u64 seq;

	dout("read_partial_message con %p msg %p\n", con, m);

	/* header */
	while (con->in_base_pos < sizeof(con->in_hdr)) {
		left = sizeof(con->in_hdr) - con->in_base_pos;
		ret = ceph_tcp_recvmsg(con->sock,
				       (char *)&con->in_hdr + con->in_base_pos,
				       left);
		if (ret <= 0)
			return ret;
		con->in_base_pos += ret;
		if (con->in_base_pos == sizeof(con->in_hdr)) {
			u32 crc = crc32c(0, (void *)&con->in_hdr,
				 sizeof(con->in_hdr) - sizeof(con->in_hdr.crc));
			if (crc != le32_to_cpu(con->in_hdr.crc)) {
				pr_err("read_partial_message bad hdr "
				       " crc %u != expected %u\n",
				       crc, con->in_hdr.crc);
				return -EBADMSG;
			}
		}
	}
	front_len = le32_to_cpu(con->in_hdr.front_len);
	if (front_len > CEPH_MSG_MAX_FRONT_LEN)
		return -EIO;
	middle_len = le32_to_cpu(con->in_hdr.middle_len);
	if (middle_len > CEPH_MSG_MAX_DATA_LEN)
		return -EIO;
	data_len = le32_to_cpu(con->in_hdr.data_len);
	if (data_len > CEPH_MSG_MAX_DATA_LEN)
		return -EIO;

	/* verify seq# */
	seq = le64_to_cpu(con->in_hdr.seq);
	if ((s64)seq - (s64)con->in_seq < 1) {
		pr_info("skipping %s%lld %s seq %lld expected %lld\n",
			ENTITY_NAME(con->peer_name),
			ceph_pr_addr(&con->peer_addr.in_addr),
			seq, con->in_seq + 1);
		con->in_base_pos = -front_len - middle_len - data_len -
			sizeof(m->footer);
		con->in_tag = CEPH_MSGR_TAG_READY;
		return 0;
	} else if ((s64)seq - (s64)con->in_seq > 1) {
		pr_err("read_partial_message bad seq %lld expected %lld\n",
		       seq, con->in_seq + 1);
		con->error_msg = "bad message sequence # for incoming message";
		return -EBADMSG;
	}

	/* allocate message? */
	if (!con->in_msg) {
		dout("got hdr type %d front %d data %d\n", con->in_hdr.type,
		     con->in_hdr.front_len, con->in_hdr.data_len);
		skip = 0;
		con->in_msg = ceph_alloc_msg(con, &con->in_hdr, &skip);
		if (skip) {
			/* skip this message */
			dout("alloc_msg said skip message\n");
			BUG_ON(con->in_msg);
			con->in_base_pos = -front_len - middle_len - data_len -
				sizeof(m->footer);
			con->in_tag = CEPH_MSGR_TAG_READY;
			con->in_seq++;
			return 0;
		}
		if (!con->in_msg) {
			con->error_msg =
				"error allocating memory for incoming message";
			return -ENOMEM;
		}
		m = con->in_msg;
		m->front.iov_len = 0;    /* haven't read it yet */
		if (m->middle)
			m->middle->vec.iov_len = 0;

		con->in_msg_pos.page = 0;
		if (m->pages)
			con->in_msg_pos.page_pos = m->page_alignment;
		else
			con->in_msg_pos.page_pos = 0;
		con->in_msg_pos.data_pos = 0;
	}

	/* front */
	ret = read_partial_message_section(con, &m->front, front_len,
					   &con->in_front_crc);
	if (ret <= 0)
		return ret;

	/* middle */
	if (m->middle) {
		ret = read_partial_message_section(con, &m->middle->vec,
						   middle_len,
						   &con->in_middle_crc);
		if (ret <= 0)
			return ret;
	}
#ifdef CONFIG_BLOCK
	if (m->bio && !m->bio_iter)
		init_bio_iter(m->bio, &m->bio_iter, &m->bio_seg);
#endif

	/* (page) data */
	while (con->in_msg_pos.data_pos < data_len) {
		if (m->pages) {
			ret = read_partial_message_pages(con, m->pages,
						 data_len, datacrc);
			if (ret <= 0)
				return ret;
#ifdef CONFIG_BLOCK
		} else if (m->bio) {

			ret = read_partial_message_bio(con,
						 &m->bio_iter, &m->bio_seg,
						 data_len, datacrc);
			if (ret <= 0)
				return ret;
#endif
		} else {
			BUG_ON(1);
		}
	}

	/* footer */
	to = sizeof(m->hdr) + sizeof(m->footer);
	while (con->in_base_pos < to) {
		left = to - con->in_base_pos;
		ret = ceph_tcp_recvmsg(con->sock, (char *)&m->footer +
				       (con->in_base_pos - sizeof(m->hdr)),
				       left);
		if (ret <= 0)
			return ret;
		con->in_base_pos += ret;
	}
	dout("read_partial_message got msg %p %d (%u) + %d (%u) + %d (%u)\n",
	     m, front_len, m->footer.front_crc, middle_len,
	     m->footer.middle_crc, data_len, m->footer.data_crc);

	/* crc ok? */
	if (con->in_front_crc != le32_to_cpu(m->footer.front_crc)) {
		pr_err("read_partial_message %p front crc %u != exp. %u\n",
		       m, con->in_front_crc, m->footer.front_crc);
		return -EBADMSG;
	}
	if (con->in_middle_crc != le32_to_cpu(m->footer.middle_crc)) {
		pr_err("read_partial_message %p middle crc %u != exp %u\n",
		       m, con->in_middle_crc, m->footer.middle_crc);
		return -EBADMSG;
	}
	if (datacrc &&
	    (m->footer.flags & CEPH_MSG_FOOTER_NOCRC) == 0 &&
	    con->in_data_crc != le32_to_cpu(m->footer.data_crc)) {
		pr_err("read_partial_message %p data crc %u != exp. %u\n", m,
		       con->in_data_crc, le32_to_cpu(m->footer.data_crc));
		return -EBADMSG;
	}

	return 1; /* done! */
}

/*
 * Process message.  This happens in the worker thread.  The callback should
 * be careful not to do anything that waits on other incoming messages or it
 * may deadlock.
 */
static void process_message(struct ceph_connection *con)
{
	struct ceph_msg *msg;

	msg = con->in_msg;
	con->in_msg = NULL;

	/* if first message, set peer_name */
	if (con->peer_name.type == 0)
		con->peer_name = msg->hdr.src;

	con->in_seq++;
	mutex_unlock(&con->mutex);

	dout("===== %p %llu from %s%lld %d=%s len %d+%d (%u %u %u) =====\n",
	     msg, le64_to_cpu(msg->hdr.seq),
	     ENTITY_NAME(msg->hdr.src),
	     le16_to_cpu(msg->hdr.type),
	     ceph_msg_type_name(le16_to_cpu(msg->hdr.type)),
	     le32_to_cpu(msg->hdr.front_len),
	     le32_to_cpu(msg->hdr.data_len),
	     con->in_front_crc, con->in_middle_crc, con->in_data_crc);
	con->ops->dispatch(con, msg);

	mutex_lock(&con->mutex);
	prepare_read_tag(con);
}


/*
 * Write something to the socket.  Called in a worker thread when the
 * socket appears to be writeable and we have something ready to send.
 */
static int try_write(struct ceph_connection *con)
{
	struct ceph_messenger *msgr = con->msgr;
	int ret = 1;

	dout("try_write start %p state %lu nref %d\n", con, con->state,
	     atomic_read(&con->nref));

more:
	dout("try_write out_kvec_bytes %d\n", con->out_kvec_bytes);

	/* open the socket first? */
	if (con->sock == NULL) {
		prepare_write_banner(msgr, con);
		prepare_write_connect(msgr, con, 1);
		prepare_read_banner(con);
		set_bit(CONNECTING, &con->state);
		clear_bit(NEGOTIATING, &con->state);

		BUG_ON(con->in_msg);
		con->in_tag = CEPH_MSGR_TAG_READY;
		dout("try_write initiating connect on %p new state %lu\n",
		     con, con->state);
		con->sock = ceph_tcp_connect(con);
		if (IS_ERR(con->sock)) {
			con->sock = NULL;
			con->error_msg = "connect error";
			ret = -1;
			goto out;
		}
	}

more_kvec:
	/* kvec data queued? */
	if (con->out_skip) {
		ret = write_partial_skip(con);
		if (ret <= 0)
			goto out;
	}
	if (con->out_kvec_left) {
		ret = write_partial_kvec(con);
		if (ret <= 0)
			goto out;
	}

	/* msg pages? */
	if (con->out_msg) {
		if (con->out_msg_done) {
			ceph_msg_put(con->out_msg);
			con->out_msg = NULL;   /* we're done with this one */
			goto do_next;
		}

		ret = write_partial_msg_pages(con);
		if (ret == 1)
			goto more_kvec;  /* we need to send the footer, too! */
		if (ret == 0)
			goto out;
		if (ret < 0) {
			dout("try_write write_partial_msg_pages err %d\n",
			     ret);
			goto out;
		}
	}

do_next:
	if (!test_bit(CONNECTING, &con->state)) {
		/* is anything else pending? */
		if (!list_empty(&con->out_queue)) {
			prepare_write_message(con);
			goto more;
		}
		if (con->in_seq > con->in_seq_acked) {
			prepare_write_ack(con);
			goto more;
		}
		if (test_and_clear_bit(KEEPALIVE_PENDING, &con->state)) {
			prepare_write_keepalive(con);
			goto more;
		}
	}

	/* Nothing to do! */
	clear_bit(WRITE_PENDING, &con->state);
	dout("try_write nothing else to write.\n");
	ret = 0;
out:
	dout("try_write done on %p ret %d\n", con, ret);
	return ret;
}



/*
 * Read what we can from the socket.
 */
static int try_read(struct ceph_connection *con)
{
	int ret = -1;

	if (!con->sock)
		return 0;

	if (test_bit(STANDBY, &con->state))
		return 0;

	dout("try_read start on %p\n", con);

more:
	dout("try_read tag %d in_base_pos %d\n", (int)con->in_tag,
	     con->in_base_pos);

	/*
	 * process_connect and process_message drop and re-take
	 * con->mutex.  make sure we handle a racing close or reopen.
	 */
	if (test_bit(CLOSED, &con->state) ||
	    test_bit(OPENING, &con->state)) {
		ret = -EAGAIN;
		goto out;
	}

	if (test_bit(CONNECTING, &con->state)) {
		if (!test_bit(NEGOTIATING, &con->state)) {
			dout("try_read connecting\n");
			ret = read_partial_banner(con);
			if (ret <= 0)
				goto out;
			ret = process_banner(con);
			if (ret < 0)
				goto out;
		}
		ret = read_partial_connect(con);
		if (ret <= 0)
			goto out;
		ret = process_connect(con);
		if (ret < 0)
			goto out;
		goto more;
	}

	if (con->in_base_pos < 0) {
		/*
		 * skipping + discarding content.
		 *
		 * FIXME: there must be a better way to do this!
		 */
		static char buf[1024];
		int skip = min(1024, -con->in_base_pos);
		dout("skipping %d / %d bytes\n", skip, -con->in_base_pos);
		ret = ceph_tcp_recvmsg(con->sock, buf, skip);
		if (ret <= 0)
			goto out;
		con->in_base_pos += ret;
		if (con->in_base_pos)
			goto more;
	}
	if (con->in_tag == CEPH_MSGR_TAG_READY) {
		/*
		 * what's next?
		 */
		ret = ceph_tcp_recvmsg(con->sock, &con->in_tag, 1);
		if (ret <= 0)
			goto out;
		dout("try_read got tag %d\n", (int)con->in_tag);
		switch (con->in_tag) {
		case CEPH_MSGR_TAG_MSG:
			prepare_read_message(con);
			break;
		case CEPH_MSGR_TAG_ACK:
			prepare_read_ack(con);
			break;
		case CEPH_MSGR_TAG_CLOSE:
			set_bit(CLOSED, &con->state);   /* fixme */
			goto out;
		default:
			goto bad_tag;
		}
	}
	if (con->in_tag == CEPH_MSGR_TAG_MSG) {
		ret = read_partial_message(con);
		if (ret <= 0) {
			switch (ret) {
			case -EBADMSG:
				con->error_msg = "bad crc";
				ret = -EIO;
				break;
			case -EIO:
				con->error_msg = "io error";
				break;
			}
			goto out;
		}
		if (con->in_tag == CEPH_MSGR_TAG_READY)
			goto more;
		process_message(con);
		goto more;
	}
	if (con->in_tag == CEPH_MSGR_TAG_ACK) {
		ret = read_partial_ack(con);
		if (ret <= 0)
			goto out;
		process_ack(con);
		goto more;
	}

out:
	dout("try_read done on %p ret %d\n", con, ret);
	return ret;

bad_tag:
	pr_err("try_read bad con->in_tag = %d\n", (int)con->in_tag);
	con->error_msg = "protocol error, garbage tag";
	ret = -1;
	goto out;
}


/*
 * Atomically queue work on a connection.  Bump @con reference to
 * avoid races with connection teardown.
 */
static void queue_con(struct ceph_connection *con)
{
	if (test_bit(DEAD, &con->state)) {
		dout("queue_con %p ignoring: DEAD\n",
		     con);
		return;
	}

	if (!con->ops->get(con)) {
		dout("queue_con %p ref count 0\n", con);
		return;
	}

	if (!queue_delayed_work(ceph_msgr_wq, &con->work, 0)) {
		dout("queue_con %p - already queued\n", con);
		con->ops->put(con);
	} else {
		dout("queue_con %p\n", con);
	}
}

/*
 * Do some work on a connection.  Drop a connection ref when we're done.
 */
static void con_work(struct work_struct *work)
{
	struct ceph_connection *con = container_of(work, struct ceph_connection,
						   work.work);
	int ret;

	mutex_lock(&con->mutex);
restart:
	if (test_and_clear_bit(BACKOFF, &con->state)) {
		dout("con_work %p backing off\n", con);
		if (queue_delayed_work(ceph_msgr_wq, &con->work,
				       round_jiffies_relative(con->delay))) {
			dout("con_work %p backoff %lu\n", con, con->delay);
			mutex_unlock(&con->mutex);
			return;
		} else {
			con->ops->put(con);
			dout("con_work %p FAILED to back off %lu\n", con,
			     con->delay);
		}
	}

	if (test_bit(STANDBY, &con->state)) {
		dout("con_work %p STANDBY\n", con);
		goto done;
	}
	if (test_bit(CLOSED, &con->state)) { /* e.g. if we are replaced */
		dout("con_work CLOSED\n");
		con_close_socket(con);
		goto done;
	}
	if (test_and_clear_bit(OPENING, &con->state)) {
		/* reopen w/ new peer */
		dout("con_work OPENING\n");
		con_close_socket(con);
	}

	if (test_and_clear_bit(SOCK_CLOSED, &con->state))
		goto fault;

	ret = try_read(con);
	if (ret == -EAGAIN)
		goto restart;
	if (ret < 0)
		goto fault;

	ret = try_write(con);
	if (ret == -EAGAIN)
		goto restart;
	if (ret < 0)
		goto fault;

done:
	mutex_unlock(&con->mutex);
done_unlocked:
	con->ops->put(con);
	return;

fault:
	mutex_unlock(&con->mutex);
	ceph_fault(con);     /* error/fault path */
	goto done_unlocked;
}


/*
 * Generic error/fault handler.  A retry mechanism is used with
 * exponential backoff
 */
static void ceph_fault(struct ceph_connection *con)
{
	pr_err("%s%lld %s %s\n", ENTITY_NAME(con->peer_name),
	       ceph_pr_addr(&con->peer_addr.in_addr), con->error_msg);
	dout("fault %p state %lu to peer %s\n",
	     con, con->state, ceph_pr_addr(&con->peer_addr.in_addr));

	if (test_bit(LOSSYTX, &con->state)) {
		dout("fault on LOSSYTX channel\n");
		goto out;
	}

	mutex_lock(&con->mutex);
	if (test_bit(CLOSED, &con->state))
		goto out_unlock;

	con_close_socket(con);

	if (con->in_msg) {
		ceph_msg_put(con->in_msg);
		con->in_msg = NULL;
	}

	/* Requeue anything that hasn't been acked */
	list_splice_init(&con->out_sent, &con->out_queue);

	/* If there are no messages queued or keepalive pending, place
	 * the connection in a STANDBY state */
	if (list_empty(&con->out_queue) &&
	    !test_bit(KEEPALIVE_PENDING, &con->state)) {
		dout("fault %p setting STANDBY clearing WRITE_PENDING\n", con);
		clear_bit(WRITE_PENDING, &con->state);
		set_bit(STANDBY, &con->state);
	} else {
		/* retry after a delay. */
		if (con->delay == 0)
			con->delay = BASE_DELAY_INTERVAL;
		else if (con->delay < MAX_DELAY_INTERVAL)
			con->delay *= 2;
		con->ops->get(con);
		if (queue_delayed_work(ceph_msgr_wq, &con->work,
				       round_jiffies_relative(con->delay))) {
			dout("fault queued %p delay %lu\n", con, con->delay);
		} else {
			con->ops->put(con);
			dout("fault failed to queue %p delay %lu, backoff\n",
			     con, con->delay);
			/*
			 * In many cases we see a socket state change
			 * while con_work is running and end up
			 * queuing (non-delayed) work, such that we
			 * can't backoff with a delay.  Set a flag so
			 * that when con_work restarts we schedule the
			 * delay then.
			 */
			set_bit(BACKOFF, &con->state);
		}
	}

out_unlock:
	mutex_unlock(&con->mutex);
out:
	/*
	 * in case we faulted due to authentication, invalidate our
	 * current tickets so that we can get new ones.
	 */
	if (con->auth_retry && con->ops->invalidate_authorizer) {
		dout("calling invalidate_authorizer()\n");
		con->ops->invalidate_authorizer(con);
	}

	if (con->ops->fault)
		con->ops->fault(con);
}



/*
 * create a new messenger instance
 */
struct ceph_messenger *ceph_messenger_create(struct ceph_entity_addr *myaddr,
					     u32 supported_features,
					     u32 required_features)
{
	struct ceph_messenger *msgr;

	msgr = kzalloc(sizeof(*msgr), GFP_KERNEL);
	if (msgr == NULL)
		return ERR_PTR(-ENOMEM);

	msgr->supported_features = supported_features;
	msgr->required_features = required_features;

	spin_lock_init(&msgr->global_seq_lock);

	/* the zero page is needed if a request is "canceled" while the message
	 * is being written over the socket */
	msgr->zero_page = __page_cache_alloc(GFP_KERNEL | __GFP_ZERO);
	if (!msgr->zero_page) {
		kfree(msgr);
		return ERR_PTR(-ENOMEM);
	}
	kmap(msgr->zero_page);

	if (myaddr)
		msgr->inst.addr = *myaddr;

	/* select a random nonce */
	msgr->inst.addr.type = 0;
	get_random_bytes(&msgr->inst.addr.nonce, sizeof(msgr->inst.addr.nonce));
	encode_my_addr(msgr);

	dout("messenger_create %p\n", msgr);
	return msgr;
}
EXPORT_SYMBOL(ceph_messenger_create);

void ceph_messenger_destroy(struct ceph_messenger *msgr)
{
	dout("destroy %p\n", msgr);
	kunmap(msgr->zero_page);
	__free_page(msgr->zero_page);
	kfree(msgr);
	dout("destroyed messenger %p\n", msgr);
}
EXPORT_SYMBOL(ceph_messenger_destroy);

static void clear_standby(struct ceph_connection *con)
{
	/* come back from STANDBY? */
	if (test_and_clear_bit(STANDBY, &con->state)) {
		mutex_lock(&con->mutex);
		dout("clear_standby %p and ++connect_seq\n", con);
		con->connect_seq++;
		WARN_ON(test_bit(WRITE_PENDING, &con->state));
		WARN_ON(test_bit(KEEPALIVE_PENDING, &con->state));
		mutex_unlock(&con->mutex);
	}
}

/*
 * Queue up an outgoing message on the given connection.
 */
void ceph_con_send(struct ceph_connection *con, struct ceph_msg *msg)
{
	if (test_bit(CLOSED, &con->state)) {
		dout("con_send %p closed, dropping %p\n", con, msg);
		ceph_msg_put(msg);
		return;
	}

	/* set src+dst */
	msg->hdr.src = con->msgr->inst.name;

	BUG_ON(msg->front.iov_len != le32_to_cpu(msg->hdr.front_len));

	msg->needs_out_seq = true;

	/* queue */
	mutex_lock(&con->mutex);
	BUG_ON(!list_empty(&msg->list_head));
	list_add_tail(&msg->list_head, &con->out_queue);
	dout("----- %p to %s%lld %d=%s len %d+%d+%d -----\n", msg,
	     ENTITY_NAME(con->peer_name), le16_to_cpu(msg->hdr.type),
	     ceph_msg_type_name(le16_to_cpu(msg->hdr.type)),
	     le32_to_cpu(msg->hdr.front_len),
	     le32_to_cpu(msg->hdr.middle_len),
	     le32_to_cpu(msg->hdr.data_len));
	mutex_unlock(&con->mutex);

	/* if there wasn't anything waiting to send before, queue
	 * new work */
	clear_standby(con);
	if (test_and_set_bit(WRITE_PENDING, &con->state) == 0)
		queue_con(con);
}
EXPORT_SYMBOL(ceph_con_send);

/*
 * Revoke a message that was previously queued for send
 */
void ceph_con_revoke(struct ceph_connection *con, struct ceph_msg *msg)
{
	mutex_lock(&con->mutex);
	if (!list_empty(&msg->list_head)) {
		dout("con_revoke %p msg %p - was on queue\n", con, msg);
		list_del_init(&msg->list_head);
		ceph_msg_put(msg);
		msg->hdr.seq = 0;
	}
	if (con->out_msg == msg) {
		dout("con_revoke %p msg %p - was sending\n", con, msg);
		con->out_msg = NULL;
		if (con->out_kvec_is_msg) {
			con->out_skip = con->out_kvec_bytes;
			con->out_kvec_is_msg = false;
		}
		ceph_msg_put(msg);
		msg->hdr.seq = 0;
	}
	mutex_unlock(&con->mutex);
}

/*
 * Revoke a message that we may be reading data into
 */
void ceph_con_revoke_message(struct ceph_connection *con, struct ceph_msg *msg)
{
	mutex_lock(&con->mutex);
	if (con->in_msg && con->in_msg == msg) {
		unsigned front_len = le32_to_cpu(con->in_hdr.front_len);
		unsigned middle_len = le32_to_cpu(con->in_hdr.middle_len);
		unsigned data_len = le32_to_cpu(con->in_hdr.data_len);

		/* skip rest of message */
		dout("con_revoke_pages %p msg %p revoked\n", con, msg);
			con->in_base_pos = con->in_base_pos -
				sizeof(struct ceph_msg_header) -
				front_len -
				middle_len -
				data_len -
				sizeof(struct ceph_msg_footer);
		ceph_msg_put(con->in_msg);
		con->in_msg = NULL;
		con->in_tag = CEPH_MSGR_TAG_READY;
		con->in_seq++;
	} else {
		dout("con_revoke_pages %p msg %p pages %p no-op\n",
		     con, con->in_msg, msg);
	}
	mutex_unlock(&con->mutex);
}

/*
 * Queue a keepalive byte to ensure the tcp connection is alive.
 */
void ceph_con_keepalive(struct ceph_connection *con)
{
	dout("con_keepalive %p\n", con);
	clear_standby(con);
	if (test_and_set_bit(KEEPALIVE_PENDING, &con->state) == 0 &&
	    test_and_set_bit(WRITE_PENDING, &con->state) == 0)
		queue_con(con);
}
EXPORT_SYMBOL(ceph_con_keepalive);


/*
 * construct a new message with given type, size
 * the new msg has a ref count of 1.
 */
struct ceph_msg *ceph_msg_new(int type, int front_len, gfp_t flags,
			      bool can_fail)
{
	struct ceph_msg *m;

	m = kmalloc(sizeof(*m), flags);
	if (m == NULL)
		goto out;
	kref_init(&m->kref);
	INIT_LIST_HEAD(&m->list_head);

	m->hdr.tid = 0;
	m->hdr.type = cpu_to_le16(type);
	m->hdr.priority = cpu_to_le16(CEPH_MSG_PRIO_DEFAULT);
	m->hdr.version = 0;
	m->hdr.front_len = cpu_to_le32(front_len);
	m->hdr.middle_len = 0;
	m->hdr.data_len = 0;
	m->hdr.data_off = 0;
	m->hdr.reserved = 0;
	m->footer.front_crc = 0;
	m->footer.middle_crc = 0;
	m->footer.data_crc = 0;
	m->footer.flags = 0;
	m->front_max = front_len;
	m->front_is_vmalloc = false;
	m->more_to_follow = false;
	m->ack_stamp = 0;
	m->pool = NULL;

	/* middle */
	m->middle = NULL;

	/* data */
	m->nr_pages = 0;
	m->page_alignment = 0;
	m->pages = NULL;
	m->pagelist = NULL;
	m->bio = NULL;
	m->bio_iter = NULL;
	m->bio_seg = 0;
	m->trail = NULL;

	/* front */
	if (front_len) {
		if (front_len > PAGE_CACHE_SIZE) {
			m->front.iov_base = __vmalloc(front_len, flags,
						      PAGE_KERNEL);
			m->front_is_vmalloc = true;
		} else {
			m->front.iov_base = kmalloc(front_len, flags);
		}
		if (m->front.iov_base == NULL) {
			dout("ceph_msg_new can't allocate %d bytes\n",
			     front_len);
			goto out2;
		}
	} else {
		m->front.iov_base = NULL;
	}
	m->front.iov_len = front_len;

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
 * Generic message allocator, for incoming messages.
 */
static struct ceph_msg *ceph_alloc_msg(struct ceph_connection *con,
				struct ceph_msg_header *hdr,
				int *skip)
{
	int type = le16_to_cpu(hdr->type);
	int front_len = le32_to_cpu(hdr->front_len);
	int middle_len = le32_to_cpu(hdr->middle_len);
	struct ceph_msg *msg = NULL;
	int ret;

	if (con->ops->alloc_msg) {
		mutex_unlock(&con->mutex);
		msg = con->ops->alloc_msg(con, hdr, skip);
		mutex_lock(&con->mutex);
		if (!msg || *skip)
			return NULL;
	}
	if (!msg) {
		*skip = 0;
		msg = ceph_msg_new(type, front_len, GFP_NOFS, false);
		if (!msg) {
			pr_err("unable to allocate msg type %d len %d\n",
			       type, front_len);
			return NULL;
		}
		msg->page_alignment = le16_to_cpu(hdr->data_off);
	}
	memcpy(&msg->hdr, &con->in_hdr, sizeof(con->in_hdr));

	if (middle_len && !msg->middle) {
		ret = ceph_alloc_middle(con, msg);
		if (ret < 0) {
			ceph_msg_put(msg);
			return NULL;
		}
	}

	return msg;
}


/*
 * Free a generically kmalloc'd message.
 */
void ceph_msg_kfree(struct ceph_msg *m)
{
	dout("msg_kfree %p\n", m);
	if (m->front_is_vmalloc)
		vfree(m->front.iov_base);
	else
		kfree(m->front.iov_base);
	kfree(m);
}

/*
 * Drop a msg ref.  Destroy as needed.
 */
void ceph_msg_last_put(struct kref *kref)
{
	struct ceph_msg *m = container_of(kref, struct ceph_msg, kref);

	dout("ceph_msg_put last one on %p\n", m);
	WARN_ON(!list_empty(&m->list_head));

	/* drop middle, data, if any */
	if (m->middle) {
		ceph_buffer_put(m->middle);
		m->middle = NULL;
	}
	m->nr_pages = 0;
	m->pages = NULL;

	if (m->pagelist) {
		ceph_pagelist_release(m->pagelist);
		kfree(m->pagelist);
		m->pagelist = NULL;
	}

	m->trail = NULL;

	if (m->pool)
		ceph_msgpool_put(m->pool, m);
	else
		ceph_msg_kfree(m);
}
EXPORT_SYMBOL(ceph_msg_last_put);

void ceph_msg_dump(struct ceph_msg *msg)
{
	pr_debug("msg_dump %p (front_max %d nr_pages %d)\n", msg,
		 msg->front_max, msg->nr_pages);
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
