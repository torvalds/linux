#include "ceph_debug.h"

#include <linux/crc32c.h>
#include <linux/ctype.h>
#include <linux/highmem.h>
#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <net/tcp.h>

#include "super.h"
#include "messenger.h"
#include "decode.h"
#include "pagelist.h"

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

const char *ceph_name_type_str(int t)
{
	switch (t) {
	case CEPH_ENTITY_TYPE_MON: return "mon";
	case CEPH_ENTITY_TYPE_MDS: return "mds";
	case CEPH_ENTITY_TYPE_OSD: return "osd";
	case CEPH_ENTITY_TYPE_CLIENT: return "client";
	case CEPH_ENTITY_TYPE_ADMIN: return "admin";
	default: return "???";
	}
}

/*
 * nicely render a sockaddr as a string.
 */
#define MAX_ADDR_STR 20
static char addr_str[MAX_ADDR_STR][40];
static DEFINE_SPINLOCK(addr_str_lock);
static int last_addr_str;

const char *pr_addr(const struct sockaddr_storage *ss)
{
	int i;
	char *s;
	struct sockaddr_in *in4 = (void *)ss;
	unsigned char *quad = (void *)&in4->sin_addr.s_addr;
	struct sockaddr_in6 *in6 = (void *)ss;

	spin_lock(&addr_str_lock);
	i = last_addr_str++;
	if (last_addr_str == MAX_ADDR_STR)
		last_addr_str = 0;
	spin_unlock(&addr_str_lock);
	s = addr_str[i];

	switch (ss->ss_family) {
	case AF_INET:
		sprintf(s, "%u.%u.%u.%u:%u",
			(unsigned int)quad[0],
			(unsigned int)quad[1],
			(unsigned int)quad[2],
			(unsigned int)quad[3],
			(unsigned int)ntohs(in4->sin_port));
		break;

	case AF_INET6:
		sprintf(s, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%u",
			in6->sin6_addr.s6_addr16[0],
			in6->sin6_addr.s6_addr16[1],
			in6->sin6_addr.s6_addr16[2],
			in6->sin6_addr.s6_addr16[3],
			in6->sin6_addr.s6_addr16[4],
			in6->sin6_addr.s6_addr16[5],
			in6->sin6_addr.s6_addr16[6],
			in6->sin6_addr.s6_addr16[7],
			(unsigned int)ntohs(in6->sin6_port));
		break;

	default:
		sprintf(s, "(unknown sockaddr family %d)", (int)ss->ss_family);
	}

	return s;
}

static void encode_my_addr(struct ceph_messenger *msgr)
{
	memcpy(&msgr->my_enc_addr, &msgr->inst.addr, sizeof(msgr->my_enc_addr));
	ceph_encode_addr(&msgr->my_enc_addr);
}

/*
 * work queue for all reading and writing to/from the socket.
 */
struct workqueue_struct *ceph_msgr_wq;

int __init ceph_msgr_init(void)
{
	ceph_msgr_wq = create_workqueue("ceph-msgr");
	if (IS_ERR(ceph_msgr_wq)) {
		int ret = PTR_ERR(ceph_msgr_wq);
		pr_err("msgr_init failed to create workqueue: %d\n", ret);
		ceph_msgr_wq = NULL;
		return ret;
	}
	return 0;
}

void ceph_msgr_exit(void)
{
	destroy_workqueue(ceph_msgr_wq);
}

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
	struct sockaddr *paddr = (struct sockaddr *)&con->peer_addr.in_addr;
	struct socket *sock;
	int ret;

	BUG_ON(con->sock);
	ret = sock_create_kern(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret)
		return ERR_PTR(ret);
	con->sock = sock;
	sock->sk->sk_allocation = GFP_NOFS;

#ifdef CONFIG_LOCKDEP
	lockdep_set_class(&sock->sk->sk_lock, &socket_class);
#endif

	set_sock_callbacks(sock, con);

	dout("connect %s\n", pr_addr(&con->peer_addr.in_addr));

	ret = sock->ops->connect(sock, paddr, sizeof(*paddr), O_NONBLOCK);
	if (ret == -EINPROGRESS) {
		dout("connect %s EINPROGRESS sk_state = %u\n",
		     pr_addr(&con->peer_addr.in_addr),
		     sock->sk->sk_state);
		ret = 0;
	}
	if (ret < 0) {
		pr_err("connect %s error %d\n",
		       pr_addr(&con->peer_addr.in_addr), ret);
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

	return kernel_recvmsg(sock, &msg, &iov, 1, len, msg.msg_flags);
}

/*
 * write something.  @more is true if caller will be sending more data
 * shortly.
 */
static int ceph_tcp_sendmsg(struct socket *sock, struct kvec *iov,
		     size_t kvlen, size_t len, int more)
{
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL };

	if (more)
		msg.msg_flags |= MSG_MORE;
	else
		msg.msg_flags |= MSG_EOR;  /* superfluous, but what the hell */

	return kernel_sendmsg(sock, &msg, iov, kvlen, len);
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
	dout("con_close %p peer %s\n", con, pr_addr(&con->peer_addr.in_addr));
	set_bit(CLOSED, &con->state);  /* in case there's queued work */
	clear_bit(STANDBY, &con->state);  /* avoid connect_seq bump */
	clear_bit(LOSSYTX, &con->state);  /* so we retry next connect */
	clear_bit(KEEPALIVE_PENDING, &con->state);
	clear_bit(WRITE_PENDING, &con->state);
	mutex_lock(&con->mutex);
	reset_connection(con);
	cancel_delayed_work(&con->work);
	mutex_unlock(&con->mutex);
	queue_con(con);
}

/*
 * Reopen a closed connection, with a new peer address.
 */
void ceph_con_open(struct ceph_connection *con, struct ceph_entity_addr *addr)
{
	dout("con_open %p %s\n", con, pr_addr(&addr->in_addr));
	set_bit(OPENING, &con->state);
	clear_bit(CLOSED, &con->state);
	memcpy(&con->peer_addr, addr, sizeof(*addr));
	con->delay = 0;      /* reset backoff memory */
	queue_con(con);
}

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
	if (test_bit(LOSSYTX, &con->state)) {
		list_del_init(&m->list_head);
	} else {
		/* put message on sent list */
		ceph_msg_get(m);
		list_move_tail(&m->list_head, &con->out_sent);
	}

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
		con->out_msg_pos.page_pos =
			le16_to_cpu(m->hdr.data_off) & ~PAGE_MASK;
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

static void prepare_connect_authorizer(struct ceph_connection *con)
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

	con->out_connect.authorizer_protocol = cpu_to_le32(auth_protocol);
	con->out_connect.authorizer_len = cpu_to_le32(auth_len);

	con->out_kvec[con->out_kvec_left].iov_base = auth_buf;
	con->out_kvec[con->out_kvec_left].iov_len = auth_len;
	con->out_kvec_left++;
	con->out_kvec_bytes += auth_len;
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

static void prepare_write_connect(struct ceph_messenger *msgr,
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

	con->out_connect.features = CEPH_FEATURE_SUPPORTED;
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

	prepare_connect_authorizer(con);
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

	dout("write_partial_msg_pages %p msg %p page %d/%d offset %d\n",
	     con, con->out_msg, con->out_msg_pos.page, con->out_msg->nr_pages,
	     con->out_msg_pos.page_pos);

	while (con->out_msg_pos.page < con->out_msg->nr_pages) {
		struct page *page = NULL;
		void *kaddr = NULL;

		/*
		 * if we are calculating the data crc (the default), we need
		 * to map the page.  if our pages[] has been revoked, use the
		 * zero page.
		 */
		if (msg->pages) {
			page = msg->pages[con->out_msg_pos.page];
			if (crc)
				kaddr = kmap(page);
		} else if (msg->pagelist) {
			page = list_first_entry(&msg->pagelist->head,
						struct page, lru);
			if (crc)
				kaddr = kmap(page);
		} else {
			page = con->msgr->zero_page;
			if (crc)
				kaddr = page_address(con->msgr->zero_page);
		}
		len = min((int)(PAGE_SIZE - con->out_msg_pos.page_pos),
			  (int)(data_len - con->out_msg_pos.data_pos));
		if (crc && !con->out_msg_pos.did_page_crc) {
			void *base = kaddr + con->out_msg_pos.page_pos;
			u32 tmpcrc = le32_to_cpu(con->out_msg->footer.data_crc);

			BUG_ON(kaddr == NULL);
			con->out_msg->footer.data_crc =
				cpu_to_le32(crc32c(tmpcrc, base, len));
			con->out_msg_pos.did_page_crc = 1;
		}

		ret = kernel_sendpage(con->sock, page,
				      con->out_msg_pos.page_pos, len,
				      MSG_DONTWAIT | MSG_NOSIGNAL |
				      MSG_MORE);

		if (crc && (msg->pages || msg->pagelist))
			kunmap(page);

		if (ret <= 0)
			goto out;

		con->out_msg_pos.data_pos += ret;
		con->out_msg_pos.page_pos += ret;
		if (ret == len) {
			con->out_msg_pos.page_pos = 0;
			con->out_msg_pos.page++;
			con->out_msg_pos.did_page_crc = 0;
			if (msg->pagelist)
				list_move_tail(&page->lru,
					       &msg->pagelist->head);
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
		       pr_addr(&con->peer_addr.in_addr));
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
	case AF_INET6:
		((struct sockaddr_in6 *)ss)->sin6_port = htons(p);
	}
}

/*
 * Parse an ip[:port] list into an addr array.  Use the default
 * monitor port if a port isn't specified.
 */
int ceph_parse_ips(const char *c, const char *end,
		   struct ceph_entity_addr *addr,
		   int max_count, int *count)
{
	int i;
	const char *p = c;

	dout("parse_ips on '%.*s'\n", (int)(end-c), c);
	for (i = 0; i < max_count; i++) {
		const char *ipend;
		struct sockaddr_storage *ss = &addr[i].in_addr;
		struct sockaddr_in *in4 = (void *)ss;
		struct sockaddr_in6 *in6 = (void *)ss;
		int port;

		memset(ss, 0, sizeof(*ss));
		if (in4_pton(p, end - p, (u8 *)&in4->sin_addr.s_addr,
			     ',', &ipend)) {
			ss->ss_family = AF_INET;
		} else if (in6_pton(p, end - p, (u8 *)&in6->sin6_addr.s6_addr,
				    ',', &ipend)) {
			ss->ss_family = AF_INET6;
		} else {
			goto bad;
		}
		p = ipend;

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

		dout("parse_ips got %s\n", pr_addr(ss));

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
	pr_err("parse_ips bad ip '%s'\n", c);
	return -EINVAL;
}

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
		pr_warning("wrong peer, want %s/%lld, got %s/%lld\n",
			   pr_addr(&con->peer_addr.in_addr),
			   le64_to_cpu(con->peer_addr.nonce),
			   pr_addr(&con->actual_peer_addr.in_addr),
			   le64_to_cpu(con->actual_peer_addr.nonce));
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
		     pr_addr(&con->msgr->inst.addr.in_addr));
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
	u64 sup_feat = CEPH_FEATURE_SUPPORTED;
	u64 req_feat = CEPH_FEATURE_REQUIRED;
	u64 server_feat = le64_to_cpu(con->in_reply.features);

	dout("process_connect on %p tag %d\n", con, (int)con->in_tag);

	switch (con->in_reply.tag) {
	case CEPH_MSGR_TAG_FEATURES:
		pr_err("%s%lld %s feature set mismatch,"
		       " my %llx < server's %llx, missing %llx\n",
		       ENTITY_NAME(con->peer_name),
		       pr_addr(&con->peer_addr.in_addr),
		       sup_feat, server_feat, server_feat & ~sup_feat);
		con->error_msg = "missing required protocol features";
		fail_protocol(con);
		return -1;

	case CEPH_MSGR_TAG_BADPROTOVER:
		pr_err("%s%lld %s protocol version mismatch,"
		       " my %d != server's %d\n",
		       ENTITY_NAME(con->peer_name),
		       pr_addr(&con->peer_addr.in_addr),
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
			reset_connection(con);
			set_bit(CLOSED, &con->state);
			return -1;
		}
		con->auth_retry = 1;
		prepare_write_connect(con->msgr, con, 0);
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
		       pr_addr(&con->peer_addr.in_addr));
		reset_connection(con);
		prepare_write_connect(con->msgr, con, 0);
		prepare_read_connect(con);

		/* Tell ceph about it. */
		mutex_unlock(&con->mutex);
		pr_info("reset on %s%lld\n", ENTITY_NAME(con->peer_name));
		if (con->ops->peer_reset)
			con->ops->peer_reset(con);
		mutex_lock(&con->mutex);
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
			       pr_addr(&con->peer_addr.in_addr),
			       req_feat, server_feat, req_feat & ~server_feat);
			con->error_msg = "missing required protocol features";
			fail_protocol(con);
			return -1;
		}
		clear_bit(CONNECTING, &con->state);
		con->peer_global_seq = le32_to_cpu(con->in_reply.global_seq);
		con->connect_seq++;
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
		pr_err("process_connect peer connecting WAIT\n");

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
		ceph_msg_remove(m);
	}
	prepare_read_tag(con);
}




static int read_partial_message_section(struct ceph_connection *con,
					struct kvec *section, unsigned int sec_len,
					u32 *crc)
{
	int left;
	int ret;

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
/*
 * read (part of) a message.
 */
static int read_partial_message(struct ceph_connection *con)
{
	struct ceph_msg *m = con->in_msg;
	void *p;
	int ret;
	int to, left;
	unsigned front_len, middle_len, data_len, data_off;
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
	data_off = le16_to_cpu(con->in_hdr.data_off);

	/* verify seq# */
	seq = le64_to_cpu(con->in_hdr.seq);
	if ((s64)seq - (s64)con->in_seq < 1) {
		pr_info("skipping %s%lld %s seq %lld, expected %lld\n",
			ENTITY_NAME(con->peer_name),
			pr_addr(&con->peer_addr.in_addr),
			seq, con->in_seq + 1);
		con->in_base_pos = -front_len - middle_len - data_len -
			sizeof(m->footer);
		con->in_tag = CEPH_MSGR_TAG_READY;
		con->in_seq++;
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
		con->in_msg = ceph_alloc_msg(con, &con->in_hdr, &skip);
		if (skip) {
			/* skip this message */
			dout("alloc_msg returned NULL, skipping message\n");
			con->in_base_pos = -front_len - middle_len - data_len -
				sizeof(m->footer);
			con->in_tag = CEPH_MSGR_TAG_READY;
			con->in_seq++;
			return 0;
		}
		if (IS_ERR(con->in_msg)) {
			ret = PTR_ERR(con->in_msg);
			con->in_msg = NULL;
			con->error_msg =
				"error allocating memory for incoming message";
			return ret;
		}
		m = con->in_msg;
		m->front.iov_len = 0;    /* haven't read it yet */
		if (m->middle)
			m->middle->vec.iov_len = 0;

		con->in_msg_pos.page = 0;
		con->in_msg_pos.page_pos = data_off & ~PAGE_MASK;
		con->in_msg_pos.data_pos = 0;
	}

	/* front */
	ret = read_partial_message_section(con, &m->front, front_len,
					   &con->in_front_crc);
	if (ret <= 0)
		return ret;

	/* middle */
	if (m->middle) {
		ret = read_partial_message_section(con, &m->middle->vec, middle_len,
						   &con->in_middle_crc);
		if (ret <= 0)
			return ret;
	}

	/* (page) data */
	while (con->in_msg_pos.data_pos < data_len) {
		left = min((int)(data_len - con->in_msg_pos.data_pos),
			   (int)(PAGE_SIZE - con->in_msg_pos.page_pos));
		BUG_ON(m->pages == NULL);
		p = kmap(m->pages[con->in_msg_pos.page]);
		ret = ceph_tcp_recvmsg(con->sock, p + con->in_msg_pos.page_pos,
				       left);
		if (ret > 0 && datacrc)
			con->in_data_crc =
				crc32c(con->in_data_crc,
					  p + con->in_msg_pos.page_pos, ret);
		kunmap(m->pages[con->in_msg_pos.page]);
		if (ret <= 0)
			return ret;
		con->in_msg_pos.data_pos += ret;
		con->in_msg_pos.page_pos += ret;
		if (con->in_msg_pos.page_pos == PAGE_SIZE) {
			con->in_msg_pos.page_pos = 0;
			con->in_msg_pos.page++;
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
		con->peer_name = msg->hdr.src.name;

	con->in_seq++;
	mutex_unlock(&con->mutex);

	dout("===== %p %llu from %s%lld %d=%s len %d+%d (%u %u %u) =====\n",
	     msg, le64_to_cpu(msg->hdr.seq),
	     ENTITY_NAME(msg->hdr.src.name),
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

	mutex_lock(&con->mutex);
more:
	dout("try_write out_kvec_bytes %d\n", con->out_kvec_bytes);

	/* open the socket first? */
	if (con->sock == NULL) {
		/*
		 * if we were STANDBY and are reconnecting _this_
		 * connection, bump connect_seq now.  Always bump
		 * global_seq.
		 */
		if (test_and_clear_bit(STANDBY, &con->state))
			con->connect_seq++;

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
			goto done;
		if (ret < 0) {
			dout("try_write write_partial_skip err %d\n", ret);
			goto done;
		}
	}
	if (con->out_kvec_left) {
		ret = write_partial_kvec(con);
		if (ret <= 0)
			goto done;
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
			goto done;
		if (ret < 0) {
			dout("try_write write_partial_msg_pages err %d\n",
			     ret);
			goto done;
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
done:
	ret = 0;
out:
	mutex_unlock(&con->mutex);
	dout("try_write done on %p\n", con);
	return ret;
}



/*
 * Read what we can from the socket.
 */
static int try_read(struct ceph_connection *con)
{
	struct ceph_messenger *msgr;
	int ret = -1;

	if (!con->sock)
		return 0;

	if (test_bit(STANDBY, &con->state))
		return 0;

	dout("try_read start on %p\n", con);
	msgr = con->msgr;

	mutex_lock(&con->mutex);

more:
	dout("try_read tag %d in_base_pos %d\n", (int)con->in_tag,
	     con->in_base_pos);
	if (test_bit(CONNECTING, &con->state)) {
		if (!test_bit(NEGOTIATING, &con->state)) {
			dout("try_read connecting\n");
			ret = read_partial_banner(con);
			if (ret <= 0)
				goto done;
			if (process_banner(con) < 0) {
				ret = -1;
				goto out;
			}
		}
		ret = read_partial_connect(con);
		if (ret <= 0)
			goto done;
		if (process_connect(con) < 0) {
			ret = -1;
			goto out;
		}
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
			goto done;
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
			goto done;
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
			goto done;
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
				goto out;
			case -EIO:
				con->error_msg = "io error";
				goto out;
			default:
				goto done;
			}
		}
		if (con->in_tag == CEPH_MSGR_TAG_READY)
			goto more;
		process_message(con);
		goto more;
	}
	if (con->in_tag == CEPH_MSGR_TAG_ACK) {
		ret = read_partial_ack(con);
		if (ret <= 0)
			goto done;
		process_ack(con);
		goto more;
	}

done:
	ret = 0;
out:
	mutex_unlock(&con->mutex);
	dout("try_read done on %p\n", con);
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
 *
 * There is some trickery going on with QUEUED and BUSY because we
 * only want a _single_ thread operating on each connection at any
 * point in time, but we want to use all available CPUs.
 *
 * The worker thread only proceeds if it can atomically set BUSY.  It
 * clears QUEUED and does it's thing.  When it thinks it's done, it
 * clears BUSY, then rechecks QUEUED.. if it's set again, it loops
 * (tries again to set BUSY).
 *
 * To queue work, we first set QUEUED, _then_ if BUSY isn't set, we
 * try to queue work.  If that fails (work is already queued, or BUSY)
 * we give up (work also already being done or is queued) but leave QUEUED
 * set so that the worker thread will loop if necessary.
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

	set_bit(QUEUED, &con->state);
	if (test_bit(BUSY, &con->state)) {
		dout("queue_con %p - already BUSY\n", con);
		con->ops->put(con);
	} else if (!queue_work(ceph_msgr_wq, &con->work.work)) {
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
	int backoff = 0;

more:
	if (test_and_set_bit(BUSY, &con->state) != 0) {
		dout("con_work %p BUSY already set\n", con);
		goto out;
	}
	dout("con_work %p start, clearing QUEUED\n", con);
	clear_bit(QUEUED, &con->state);

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

	if (test_and_clear_bit(SOCK_CLOSED, &con->state) ||
	    try_read(con) < 0 ||
	    try_write(con) < 0) {
		backoff = 1;
		ceph_fault(con);     /* error/fault path */
	}

done:
	clear_bit(BUSY, &con->state);
	dout("con->state=%lu\n", con->state);
	if (test_bit(QUEUED, &con->state)) {
		if (!backoff || test_bit(OPENING, &con->state)) {
			dout("con_work %p QUEUED reset, looping\n", con);
			goto more;
		}
		dout("con_work %p QUEUED reset, but just faulted\n", con);
		clear_bit(QUEUED, &con->state);
	}
	dout("con_work %p done\n", con);

out:
	con->ops->put(con);
}


/*
 * Generic error/fault handler.  A retry mechanism is used with
 * exponential backoff
 */
static void ceph_fault(struct ceph_connection *con)
{
	pr_err("%s%lld %s %s\n", ENTITY_NAME(con->peer_name),
	       pr_addr(&con->peer_addr.in_addr), con->error_msg);
	dout("fault %p state %lu to peer %s\n",
	     con, con->state, pr_addr(&con->peer_addr.in_addr));

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

	/* If there are no messages in the queue, place the connection
	 * in a STANDBY state (i.e., don't try to reconnect just yet). */
	if (list_empty(&con->out_queue) && !con->out_keepalive_pending) {
		dout("fault setting STANDBY\n");
		set_bit(STANDBY, &con->state);
	} else {
		/* retry after a delay. */
		if (con->delay == 0)
			con->delay = BASE_DELAY_INTERVAL;
		else if (con->delay < MAX_DELAY_INTERVAL)
			con->delay *= 2;
		dout("fault queueing %p delay %lu\n", con, con->delay);
		con->ops->get(con);
		if (queue_delayed_work(ceph_msgr_wq, &con->work,
				       round_jiffies_relative(con->delay)) == 0)
			con->ops->put(con);
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
struct ceph_messenger *ceph_messenger_create(struct ceph_entity_addr *myaddr)
{
	struct ceph_messenger *msgr;

	msgr = kzalloc(sizeof(*msgr), GFP_KERNEL);
	if (msgr == NULL)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&msgr->global_seq_lock);

	/* the zero page is needed if a request is "canceled" while the message
	 * is being written over the socket */
	msgr->zero_page = alloc_page(GFP_KERNEL | __GFP_ZERO);
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

void ceph_messenger_destroy(struct ceph_messenger *msgr)
{
	dout("destroy %p\n", msgr);
	kunmap(msgr->zero_page);
	__free_page(msgr->zero_page);
	kfree(msgr);
	dout("destroyed messenger %p\n", msgr);
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
	msg->hdr.src.name = con->msgr->inst.name;
	msg->hdr.src.addr = con->msgr->my_enc_addr;
	msg->hdr.orig_src = msg->hdr.src;

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
	if (test_and_set_bit(WRITE_PENDING, &con->state) == 0)
		queue_con(con);
}

/*
 * Revoke a message that was previously queued for send
 */
void ceph_con_revoke(struct ceph_connection *con, struct ceph_msg *msg)
{
	mutex_lock(&con->mutex);
	if (!list_empty(&msg->list_head)) {
		dout("con_revoke %p msg %p\n", con, msg);
		list_del_init(&msg->list_head);
		ceph_msg_put(msg);
		msg->hdr.seq = 0;
		if (con->out_msg == msg) {
			ceph_msg_put(con->out_msg);
			con->out_msg = NULL;
		}
		if (con->out_kvec_is_msg) {
			con->out_skip = con->out_kvec_bytes;
			con->out_kvec_is_msg = false;
		}
	} else {
		dout("con_revoke %p msg %p - not queued (sent?)\n", con, msg);
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
	if (test_and_set_bit(KEEPALIVE_PENDING, &con->state) == 0 &&
	    test_and_set_bit(WRITE_PENDING, &con->state) == 0)
		queue_con(con);
}


/*
 * construct a new message with given type, size
 * the new msg has a ref count of 1.
 */
struct ceph_msg *ceph_msg_new(int type, int front_len,
			      int page_len, int page_off, struct page **pages)
{
	struct ceph_msg *m;

	m = kmalloc(sizeof(*m), GFP_NOFS);
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
	m->hdr.data_len = cpu_to_le32(page_len);
	m->hdr.data_off = cpu_to_le16(page_off);
	m->hdr.reserved = 0;
	m->footer.front_crc = 0;
	m->footer.middle_crc = 0;
	m->footer.data_crc = 0;
	m->footer.flags = 0;
	m->front_max = front_len;
	m->front_is_vmalloc = false;
	m->more_to_follow = false;
	m->pool = NULL;

	/* front */
	if (front_len) {
		if (front_len > PAGE_CACHE_SIZE) {
			m->front.iov_base = __vmalloc(front_len, GFP_NOFS,
						      PAGE_KERNEL);
			m->front_is_vmalloc = true;
		} else {
			m->front.iov_base = kmalloc(front_len, GFP_NOFS);
		}
		if (m->front.iov_base == NULL) {
			pr_err("msg_new can't allocate %d bytes\n",
			     front_len);
			goto out2;
		}
	} else {
		m->front.iov_base = NULL;
	}
	m->front.iov_len = front_len;

	/* middle */
	m->middle = NULL;

	/* data */
	m->nr_pages = calc_pages_for(page_off, page_len);
	m->pages = pages;
	m->pagelist = NULL;

	dout("ceph_msg_new %p page %d~%d -> %d\n", m, page_off, page_len,
	     m->nr_pages);
	return m;

out2:
	ceph_msg_put(m);
out:
	pr_err("msg_new can't create type %d len %d\n", type, front_len);
	return ERR_PTR(-ENOMEM);
}

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
		if (IS_ERR(msg))
			return msg;

		if (*skip)
			return NULL;
	}
	if (!msg) {
		*skip = 0;
		msg = ceph_msg_new(type, front_len, 0, 0, NULL);
		if (!msg) {
			pr_err("unable to allocate msg type %d len %d\n",
			       type, front_len);
			return ERR_PTR(-ENOMEM);
		}
	}
	memcpy(&msg->hdr, &con->in_hdr, sizeof(con->in_hdr));

	if (middle_len) {
		ret = ceph_alloc_middle(con, msg);

		if (ret < 0) {
			ceph_msg_put(msg);
			return msg;
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

	if (m->pool)
		ceph_msgpool_put(m->pool, m);
	else
		ceph_msg_kfree(m);
}

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
