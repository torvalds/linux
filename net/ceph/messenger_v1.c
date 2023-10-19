// SPDX-License-Identifier: GPL-2.0
#include <linux/ceph/ceph_debug.h>

#include <linux/bvec.h>
#include <linux/crc32c.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <net/sock.h>

#include <linux/ceph/ceph_features.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/libceph.h>
#include <linux/ceph/messenger.h>

/* static tag bytes (protocol control messages) */
static char tag_msg = CEPH_MSGR_TAG_MSG;
static char tag_ack = CEPH_MSGR_TAG_ACK;
static char tag_keepalive = CEPH_MSGR_TAG_KEEPALIVE;
static char tag_keepalive2 = CEPH_MSGR_TAG_KEEPALIVE2;

/*
 * If @buf is NULL, discard up to @len bytes.
 */
static int ceph_tcp_recvmsg(struct socket *sock, void *buf, size_t len)
{
	struct kvec iov = {buf, len};
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL };
	int r;

	if (!buf)
		msg.msg_flags |= MSG_TRUNC;

	iov_iter_kvec(&msg.msg_iter, ITER_DEST, &iov, 1, len);
	r = sock_recvmsg(sock, &msg, msg.msg_flags);
	if (r == -EAGAIN)
		r = 0;
	return r;
}

static int ceph_tcp_recvpage(struct socket *sock, struct page *page,
		     int page_offset, size_t length)
{
	struct bio_vec bvec = {
		.bv_page = page,
		.bv_offset = page_offset,
		.bv_len = length
	};
	struct msghdr msg = { .msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL };
	int r;

	BUG_ON(page_offset + length > PAGE_SIZE);
	iov_iter_bvec(&msg.msg_iter, ITER_DEST, &bvec, 1, length);
	r = sock_recvmsg(sock, &msg, msg.msg_flags);
	if (r == -EAGAIN)
		r = 0;
	return r;
}

/*
 * write something.  @more is true if caller will be sending more data
 * shortly.
 */
static int ceph_tcp_sendmsg(struct socket *sock, struct kvec *iov,
			    size_t kvlen, size_t len, bool more)
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
 * @more: either or both of MSG_MORE and MSG_SENDPAGE_NOTLAST
 */
static int ceph_tcp_sendpage(struct socket *sock, struct page *page,
			     int offset, size_t size, int more)
{
	ssize_t (*sendpage)(struct socket *sock, struct page *page,
			    int offset, size_t size, int flags);
	int flags = MSG_DONTWAIT | MSG_NOSIGNAL | more;
	int ret;

	/*
	 * sendpage cannot properly handle pages with page_count == 0,
	 * we need to fall back to sendmsg if that's the case.
	 *
	 * Same goes for slab pages: skb_can_coalesce() allows
	 * coalescing neighboring slab objects into a single frag which
	 * triggers one of hardened usercopy checks.
	 */
	if (sendpage_ok(page))
		sendpage = sock->ops->sendpage;
	else
		sendpage = sock_no_sendpage;

	ret = sendpage(sock, page, offset, size, flags);
	if (ret == -EAGAIN)
		ret = 0;

	return ret;
}

static void con_out_kvec_reset(struct ceph_connection *con)
{
	BUG_ON(con->v1.out_skip);

	con->v1.out_kvec_left = 0;
	con->v1.out_kvec_bytes = 0;
	con->v1.out_kvec_cur = &con->v1.out_kvec[0];
}

static void con_out_kvec_add(struct ceph_connection *con,
				size_t size, void *data)
{
	int index = con->v1.out_kvec_left;

	BUG_ON(con->v1.out_skip);
	BUG_ON(index >= ARRAY_SIZE(con->v1.out_kvec));

	con->v1.out_kvec[index].iov_len = size;
	con->v1.out_kvec[index].iov_base = data;
	con->v1.out_kvec_left++;
	con->v1.out_kvec_bytes += size;
}

/*
 * Chop off a kvec from the end.  Return residual number of bytes for
 * that kvec, i.e. how many bytes would have been written if the kvec
 * hadn't been nuked.
 */
static int con_out_kvec_skip(struct ceph_connection *con)
{
	int skip = 0;

	if (con->v1.out_kvec_bytes > 0) {
		skip = con->v1.out_kvec_cur[con->v1.out_kvec_left - 1].iov_len;
		BUG_ON(con->v1.out_kvec_bytes < skip);
		BUG_ON(!con->v1.out_kvec_left);
		con->v1.out_kvec_bytes -= skip;
		con->v1.out_kvec_left--;
	}

	return skip;
}

static size_t sizeof_footer(struct ceph_connection *con)
{
	return (con->peer_features & CEPH_FEATURE_MSG_AUTH) ?
	    sizeof(struct ceph_msg_footer) :
	    sizeof(struct ceph_msg_footer_old);
}

static void prepare_message_data(struct ceph_msg *msg, u32 data_len)
{
	/* Initialize data cursor */

	ceph_msg_data_cursor_init(&msg->cursor, msg, data_len);
}

/*
 * Prepare footer for currently outgoing message, and finish things
 * off.  Assumes out_kvec* are already valid.. we just add on to the end.
 */
static void prepare_write_message_footer(struct ceph_connection *con)
{
	struct ceph_msg *m = con->out_msg;

	m->footer.flags |= CEPH_MSG_FOOTER_COMPLETE;

	dout("prepare_write_message_footer %p\n", con);
	con_out_kvec_add(con, sizeof_footer(con), &m->footer);
	if (con->peer_features & CEPH_FEATURE_MSG_AUTH) {
		if (con->ops->sign_message)
			con->ops->sign_message(m);
		else
			m->footer.sig = 0;
	} else {
		m->old_footer.flags = m->footer.flags;
	}
	con->v1.out_more = m->more_to_follow;
	con->v1.out_msg_done = true;
}

/*
 * Prepare headers for the next outgoing message.
 */
static void prepare_write_message(struct ceph_connection *con)
{
	struct ceph_msg *m;
	u32 crc;

	con_out_kvec_reset(con);
	con->v1.out_msg_done = false;

	/* Sneak an ack in there first?  If we can get it into the same
	 * TCP packet that's a good thing. */
	if (con->in_seq > con->in_seq_acked) {
		con->in_seq_acked = con->in_seq;
		con_out_kvec_add(con, sizeof (tag_ack), &tag_ack);
		con->v1.out_temp_ack = cpu_to_le64(con->in_seq_acked);
		con_out_kvec_add(con, sizeof(con->v1.out_temp_ack),
			&con->v1.out_temp_ack);
	}

	ceph_con_get_out_msg(con);
	m = con->out_msg;

	dout("prepare_write_message %p seq %lld type %d len %d+%d+%zd\n",
	     m, con->out_seq, le16_to_cpu(m->hdr.type),
	     le32_to_cpu(m->hdr.front_len), le32_to_cpu(m->hdr.middle_len),
	     m->data_length);
	WARN_ON(m->front.iov_len != le32_to_cpu(m->hdr.front_len));
	WARN_ON(m->data_length != le32_to_cpu(m->hdr.data_len));

	/* tag + hdr + front + middle */
	con_out_kvec_add(con, sizeof (tag_msg), &tag_msg);
	con_out_kvec_add(con, sizeof(con->v1.out_hdr), &con->v1.out_hdr);
	con_out_kvec_add(con, m->front.iov_len, m->front.iov_base);

	if (m->middle)
		con_out_kvec_add(con, m->middle->vec.iov_len,
			m->middle->vec.iov_base);

	/* fill in hdr crc and finalize hdr */
	crc = crc32c(0, &m->hdr, offsetof(struct ceph_msg_header, crc));
	con->out_msg->hdr.crc = cpu_to_le32(crc);
	memcpy(&con->v1.out_hdr, &con->out_msg->hdr, sizeof(con->v1.out_hdr));

	/* fill in front and middle crc, footer */
	crc = crc32c(0, m->front.iov_base, m->front.iov_len);
	con->out_msg->footer.front_crc = cpu_to_le32(crc);
	if (m->middle) {
		crc = crc32c(0, m->middle->vec.iov_base,
				m->middle->vec.iov_len);
		con->out_msg->footer.middle_crc = cpu_to_le32(crc);
	} else
		con->out_msg->footer.middle_crc = 0;
	dout("%s front_crc %u middle_crc %u\n", __func__,
	     le32_to_cpu(con->out_msg->footer.front_crc),
	     le32_to_cpu(con->out_msg->footer.middle_crc));
	con->out_msg->footer.flags = 0;

	/* is there a data payload? */
	con->out_msg->footer.data_crc = 0;
	if (m->data_length) {
		prepare_message_data(con->out_msg, m->data_length);
		con->v1.out_more = 1;  /* data + footer will follow */
	} else {
		/* no, queue up footer too and be done */
		prepare_write_message_footer(con);
	}

	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
}

/*
 * Prepare an ack.
 */
static void prepare_write_ack(struct ceph_connection *con)
{
	dout("prepare_write_ack %p %llu -> %llu\n", con,
	     con->in_seq_acked, con->in_seq);
	con->in_seq_acked = con->in_seq;

	con_out_kvec_reset(con);

	con_out_kvec_add(con, sizeof (tag_ack), &tag_ack);

	con->v1.out_temp_ack = cpu_to_le64(con->in_seq_acked);
	con_out_kvec_add(con, sizeof(con->v1.out_temp_ack),
			 &con->v1.out_temp_ack);

	con->v1.out_more = 1;  /* more will follow.. eventually.. */
	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
}

/*
 * Prepare to share the seq during handshake
 */
static void prepare_write_seq(struct ceph_connection *con)
{
	dout("prepare_write_seq %p %llu -> %llu\n", con,
	     con->in_seq_acked, con->in_seq);
	con->in_seq_acked = con->in_seq;

	con_out_kvec_reset(con);

	con->v1.out_temp_ack = cpu_to_le64(con->in_seq_acked);
	con_out_kvec_add(con, sizeof(con->v1.out_temp_ack),
			 &con->v1.out_temp_ack);

	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
}

/*
 * Prepare to write keepalive byte.
 */
static void prepare_write_keepalive(struct ceph_connection *con)
{
	dout("prepare_write_keepalive %p\n", con);
	con_out_kvec_reset(con);
	if (con->peer_features & CEPH_FEATURE_MSGR_KEEPALIVE2) {
		struct timespec64 now;

		ktime_get_real_ts64(&now);
		con_out_kvec_add(con, sizeof(tag_keepalive2), &tag_keepalive2);
		ceph_encode_timespec64(&con->v1.out_temp_keepalive2, &now);
		con_out_kvec_add(con, sizeof(con->v1.out_temp_keepalive2),
				 &con->v1.out_temp_keepalive2);
	} else {
		con_out_kvec_add(con, sizeof(tag_keepalive), &tag_keepalive);
	}
	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
}

/*
 * Connection negotiation.
 */

static int get_connect_authorizer(struct ceph_connection *con)
{
	struct ceph_auth_handshake *auth;
	int auth_proto;

	if (!con->ops->get_authorizer) {
		con->v1.auth = NULL;
		con->v1.out_connect.authorizer_protocol = CEPH_AUTH_UNKNOWN;
		con->v1.out_connect.authorizer_len = 0;
		return 0;
	}

	auth = con->ops->get_authorizer(con, &auth_proto, con->v1.auth_retry);
	if (IS_ERR(auth))
		return PTR_ERR(auth);

	con->v1.auth = auth;
	con->v1.out_connect.authorizer_protocol = cpu_to_le32(auth_proto);
	con->v1.out_connect.authorizer_len =
		cpu_to_le32(auth->authorizer_buf_len);
	return 0;
}

/*
 * We connected to a peer and are saying hello.
 */
static void prepare_write_banner(struct ceph_connection *con)
{
	con_out_kvec_add(con, strlen(CEPH_BANNER), CEPH_BANNER);
	con_out_kvec_add(con, sizeof (con->msgr->my_enc_addr),
					&con->msgr->my_enc_addr);

	con->v1.out_more = 0;
	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
}

static void __prepare_write_connect(struct ceph_connection *con)
{
	con_out_kvec_add(con, sizeof(con->v1.out_connect),
			 &con->v1.out_connect);
	if (con->v1.auth)
		con_out_kvec_add(con, con->v1.auth->authorizer_buf_len,
				 con->v1.auth->authorizer_buf);

	con->v1.out_more = 0;
	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
}

static int prepare_write_connect(struct ceph_connection *con)
{
	unsigned int global_seq = ceph_get_global_seq(con->msgr, 0);
	int proto;
	int ret;

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
	     con->v1.connect_seq, global_seq, proto);

	con->v1.out_connect.features =
		cpu_to_le64(from_msgr(con->msgr)->supported_features);
	con->v1.out_connect.host_type = cpu_to_le32(CEPH_ENTITY_TYPE_CLIENT);
	con->v1.out_connect.connect_seq = cpu_to_le32(con->v1.connect_seq);
	con->v1.out_connect.global_seq = cpu_to_le32(global_seq);
	con->v1.out_connect.protocol_version = cpu_to_le32(proto);
	con->v1.out_connect.flags = 0;

	ret = get_connect_authorizer(con);
	if (ret)
		return ret;

	__prepare_write_connect(con);
	return 0;
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

	dout("write_partial_kvec %p %d left\n", con, con->v1.out_kvec_bytes);
	while (con->v1.out_kvec_bytes > 0) {
		ret = ceph_tcp_sendmsg(con->sock, con->v1.out_kvec_cur,
				       con->v1.out_kvec_left,
				       con->v1.out_kvec_bytes,
				       con->v1.out_more);
		if (ret <= 0)
			goto out;
		con->v1.out_kvec_bytes -= ret;
		if (!con->v1.out_kvec_bytes)
			break;            /* done */

		/* account for full iov entries consumed */
		while (ret >= con->v1.out_kvec_cur->iov_len) {
			BUG_ON(!con->v1.out_kvec_left);
			ret -= con->v1.out_kvec_cur->iov_len;
			con->v1.out_kvec_cur++;
			con->v1.out_kvec_left--;
		}
		/* and for a partially-consumed entry */
		if (ret) {
			con->v1.out_kvec_cur->iov_len -= ret;
			con->v1.out_kvec_cur->iov_base += ret;
		}
	}
	con->v1.out_kvec_left = 0;
	ret = 1;
out:
	dout("write_partial_kvec %p %d left in %d kvecs ret = %d\n", con,
	     con->v1.out_kvec_bytes, con->v1.out_kvec_left, ret);
	return ret;  /* done! */
}

/*
 * Write as much message data payload as we can.  If we finish, queue
 * up the footer.
 *  1 -> done, footer is now queued in out_kvec[].
 *  0 -> socket full, but more to do
 * <0 -> error
 */
static int write_partial_message_data(struct ceph_connection *con)
{
	struct ceph_msg *msg = con->out_msg;
	struct ceph_msg_data_cursor *cursor = &msg->cursor;
	bool do_datacrc = !ceph_test_opt(from_msgr(con->msgr), NOCRC);
	int more = MSG_MORE | MSG_SENDPAGE_NOTLAST;
	u32 crc;

	dout("%s %p msg %p\n", __func__, con, msg);

	if (!msg->num_data_items)
		return -EINVAL;

	/*
	 * Iterate through each page that contains data to be
	 * written, and send as much as possible for each.
	 *
	 * If we are calculating the data crc (the default), we will
	 * need to map the page.  If we have no pages, they have
	 * been revoked, so use the zero page.
	 */
	crc = do_datacrc ? le32_to_cpu(msg->footer.data_crc) : 0;
	while (cursor->total_resid) {
		struct page *page;
		size_t page_offset;
		size_t length;
		int ret;

		if (!cursor->resid) {
			ceph_msg_data_advance(cursor, 0);
			continue;
		}

		page = ceph_msg_data_next(cursor, &page_offset, &length);
		if (length == cursor->total_resid)
			more = MSG_MORE;
		ret = ceph_tcp_sendpage(con->sock, page, page_offset, length,
					more);
		if (ret <= 0) {
			if (do_datacrc)
				msg->footer.data_crc = cpu_to_le32(crc);

			return ret;
		}
		if (do_datacrc && cursor->need_crc)
			crc = ceph_crc32c_page(crc, page, page_offset, length);
		ceph_msg_data_advance(cursor, (size_t)ret);
	}

	dout("%s %p msg %p done\n", __func__, con, msg);

	/* prepare and queue up footer, too */
	if (do_datacrc)
		msg->footer.data_crc = cpu_to_le32(crc);
	else
		msg->footer.flags |= CEPH_MSG_FOOTER_NOCRC;
	con_out_kvec_reset(con);
	prepare_write_message_footer(con);

	return 1;	/* must return > 0 to indicate success */
}

/*
 * write some zeros
 */
static int write_partial_skip(struct ceph_connection *con)
{
	int more = MSG_MORE | MSG_SENDPAGE_NOTLAST;
	int ret;

	dout("%s %p %d left\n", __func__, con, con->v1.out_skip);
	while (con->v1.out_skip > 0) {
		size_t size = min(con->v1.out_skip, (int)PAGE_SIZE);

		if (size == con->v1.out_skip)
			more = MSG_MORE;
		ret = ceph_tcp_sendpage(con->sock, ceph_zero_page, 0, size,
					more);
		if (ret <= 0)
			goto out;
		con->v1.out_skip -= ret;
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
	con->v1.in_base_pos = 0;
}

static void prepare_read_connect(struct ceph_connection *con)
{
	dout("prepare_read_connect %p\n", con);
	con->v1.in_base_pos = 0;
}

static void prepare_read_ack(struct ceph_connection *con)
{
	dout("prepare_read_ack %p\n", con);
	con->v1.in_base_pos = 0;
}

static void prepare_read_seq(struct ceph_connection *con)
{
	dout("prepare_read_seq %p\n", con);
	con->v1.in_base_pos = 0;
	con->v1.in_tag = CEPH_MSGR_TAG_SEQ;
}

static void prepare_read_tag(struct ceph_connection *con)
{
	dout("prepare_read_tag %p\n", con);
	con->v1.in_base_pos = 0;
	con->v1.in_tag = CEPH_MSGR_TAG_READY;
}

static void prepare_read_keepalive_ack(struct ceph_connection *con)
{
	dout("prepare_read_keepalive_ack %p\n", con);
	con->v1.in_base_pos = 0;
}

/*
 * Prepare to read a message.
 */
static int prepare_read_message(struct ceph_connection *con)
{
	dout("prepare_read_message %p\n", con);
	BUG_ON(con->in_msg != NULL);
	con->v1.in_base_pos = 0;
	con->in_front_crc = con->in_middle_crc = con->in_data_crc = 0;
	return 0;
}

static int read_partial(struct ceph_connection *con,
			int end, int size, void *object)
{
	while (con->v1.in_base_pos < end) {
		int left = end - con->v1.in_base_pos;
		int have = size - left;
		int ret = ceph_tcp_recvmsg(con->sock, object + have, left);
		if (ret <= 0)
			return ret;
		con->v1.in_base_pos += ret;
	}
	return 1;
}

/*
 * Read all or part of the connect-side handshake on a new connection
 */
static int read_partial_banner(struct ceph_connection *con)
{
	int size;
	int end;
	int ret;

	dout("read_partial_banner %p at %d\n", con, con->v1.in_base_pos);

	/* peer's banner */
	size = strlen(CEPH_BANNER);
	end = size;
	ret = read_partial(con, end, size, con->v1.in_banner);
	if (ret <= 0)
		goto out;

	size = sizeof(con->v1.actual_peer_addr);
	end += size;
	ret = read_partial(con, end, size, &con->v1.actual_peer_addr);
	if (ret <= 0)
		goto out;
	ceph_decode_banner_addr(&con->v1.actual_peer_addr);

	size = sizeof(con->v1.peer_addr_for_me);
	end += size;
	ret = read_partial(con, end, size, &con->v1.peer_addr_for_me);
	if (ret <= 0)
		goto out;
	ceph_decode_banner_addr(&con->v1.peer_addr_for_me);

out:
	return ret;
}

static int read_partial_connect(struct ceph_connection *con)
{
	int size;
	int end;
	int ret;

	dout("read_partial_connect %p at %d\n", con, con->v1.in_base_pos);

	size = sizeof(con->v1.in_reply);
	end = size;
	ret = read_partial(con, end, size, &con->v1.in_reply);
	if (ret <= 0)
		goto out;

	if (con->v1.auth) {
		size = le32_to_cpu(con->v1.in_reply.authorizer_len);
		if (size > con->v1.auth->authorizer_reply_buf_len) {
			pr_err("authorizer reply too big: %d > %zu\n", size,
			       con->v1.auth->authorizer_reply_buf_len);
			ret = -EINVAL;
			goto out;
		}

		end += size;
		ret = read_partial(con, end, size,
				   con->v1.auth->authorizer_reply_buf);
		if (ret <= 0)
			goto out;
	}

	dout("read_partial_connect %p tag %d, con_seq = %u, g_seq = %u\n",
	     con, con->v1.in_reply.tag,
	     le32_to_cpu(con->v1.in_reply.connect_seq),
	     le32_to_cpu(con->v1.in_reply.global_seq));
out:
	return ret;
}

/*
 * Verify the hello banner looks okay.
 */
static int verify_hello(struct ceph_connection *con)
{
	if (memcmp(con->v1.in_banner, CEPH_BANNER, strlen(CEPH_BANNER))) {
		pr_err("connect to %s got bad banner\n",
		       ceph_pr_addr(&con->peer_addr));
		con->error_msg = "protocol error, bad banner";
		return -1;
	}
	return 0;
}

static int process_banner(struct ceph_connection *con)
{
	struct ceph_entity_addr *my_addr = &con->msgr->inst.addr;

	dout("process_banner on %p\n", con);

	if (verify_hello(con) < 0)
		return -1;

	/*
	 * Make sure the other end is who we wanted.  note that the other
	 * end may not yet know their ip address, so if it's 0.0.0.0, give
	 * them the benefit of the doubt.
	 */
	if (memcmp(&con->peer_addr, &con->v1.actual_peer_addr,
		   sizeof(con->peer_addr)) != 0 &&
	    !(ceph_addr_is_blank(&con->v1.actual_peer_addr) &&
	      con->v1.actual_peer_addr.nonce == con->peer_addr.nonce)) {
		pr_warn("wrong peer, want %s/%u, got %s/%u\n",
			ceph_pr_addr(&con->peer_addr),
			le32_to_cpu(con->peer_addr.nonce),
			ceph_pr_addr(&con->v1.actual_peer_addr),
			le32_to_cpu(con->v1.actual_peer_addr.nonce));
		con->error_msg = "wrong peer at address";
		return -1;
	}

	/*
	 * did we learn our address?
	 */
	if (ceph_addr_is_blank(my_addr)) {
		memcpy(&my_addr->in_addr,
		       &con->v1.peer_addr_for_me.in_addr,
		       sizeof(con->v1.peer_addr_for_me.in_addr));
		ceph_addr_set_port(my_addr, 0);
		ceph_encode_my_addr(con->msgr);
		dout("process_banner learned my addr is %s\n",
		     ceph_pr_addr(my_addr));
	}

	return 0;
}

static int process_connect(struct ceph_connection *con)
{
	u64 sup_feat = from_msgr(con->msgr)->supported_features;
	u64 req_feat = from_msgr(con->msgr)->required_features;
	u64 server_feat = le64_to_cpu(con->v1.in_reply.features);
	int ret;

	dout("process_connect on %p tag %d\n", con, con->v1.in_tag);

	if (con->v1.auth) {
		int len = le32_to_cpu(con->v1.in_reply.authorizer_len);

		/*
		 * Any connection that defines ->get_authorizer()
		 * should also define ->add_authorizer_challenge() and
		 * ->verify_authorizer_reply().
		 *
		 * See get_connect_authorizer().
		 */
		if (con->v1.in_reply.tag ==
				CEPH_MSGR_TAG_CHALLENGE_AUTHORIZER) {
			ret = con->ops->add_authorizer_challenge(
				con, con->v1.auth->authorizer_reply_buf, len);
			if (ret < 0)
				return ret;

			con_out_kvec_reset(con);
			__prepare_write_connect(con);
			prepare_read_connect(con);
			return 0;
		}

		if (len) {
			ret = con->ops->verify_authorizer_reply(con);
			if (ret < 0) {
				con->error_msg = "bad authorize reply";
				return ret;
			}
		}
	}

	switch (con->v1.in_reply.tag) {
	case CEPH_MSGR_TAG_FEATURES:
		pr_err("%s%lld %s feature set mismatch,"
		       " my %llx < server's %llx, missing %llx\n",
		       ENTITY_NAME(con->peer_name),
		       ceph_pr_addr(&con->peer_addr),
		       sup_feat, server_feat, server_feat & ~sup_feat);
		con->error_msg = "missing required protocol features";
		return -1;

	case CEPH_MSGR_TAG_BADPROTOVER:
		pr_err("%s%lld %s protocol version mismatch,"
		       " my %d != server's %d\n",
		       ENTITY_NAME(con->peer_name),
		       ceph_pr_addr(&con->peer_addr),
		       le32_to_cpu(con->v1.out_connect.protocol_version),
		       le32_to_cpu(con->v1.in_reply.protocol_version));
		con->error_msg = "protocol version mismatch";
		return -1;

	case CEPH_MSGR_TAG_BADAUTHORIZER:
		con->v1.auth_retry++;
		dout("process_connect %p got BADAUTHORIZER attempt %d\n", con,
		     con->v1.auth_retry);
		if (con->v1.auth_retry == 2) {
			con->error_msg = "connect authorization failure";
			return -1;
		}
		con_out_kvec_reset(con);
		ret = prepare_write_connect(con);
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
		     le32_to_cpu(con->v1.in_reply.connect_seq));
		pr_info("%s%lld %s session reset\n",
			ENTITY_NAME(con->peer_name),
			ceph_pr_addr(&con->peer_addr));
		ceph_con_reset_session(con);
		con_out_kvec_reset(con);
		ret = prepare_write_connect(con);
		if (ret < 0)
			return ret;
		prepare_read_connect(con);

		/* Tell ceph about it. */
		mutex_unlock(&con->mutex);
		if (con->ops->peer_reset)
			con->ops->peer_reset(con);
		mutex_lock(&con->mutex);
		if (con->state != CEPH_CON_S_V1_CONNECT_MSG)
			return -EAGAIN;
		break;

	case CEPH_MSGR_TAG_RETRY_SESSION:
		/*
		 * If we sent a smaller connect_seq than the peer has, try
		 * again with a larger value.
		 */
		dout("process_connect got RETRY_SESSION my seq %u, peer %u\n",
		     le32_to_cpu(con->v1.out_connect.connect_seq),
		     le32_to_cpu(con->v1.in_reply.connect_seq));
		con->v1.connect_seq = le32_to_cpu(con->v1.in_reply.connect_seq);
		con_out_kvec_reset(con);
		ret = prepare_write_connect(con);
		if (ret < 0)
			return ret;
		prepare_read_connect(con);
		break;

	case CEPH_MSGR_TAG_RETRY_GLOBAL:
		/*
		 * If we sent a smaller global_seq than the peer has, try
		 * again with a larger value.
		 */
		dout("process_connect got RETRY_GLOBAL my %u peer_gseq %u\n",
		     con->v1.peer_global_seq,
		     le32_to_cpu(con->v1.in_reply.global_seq));
		ceph_get_global_seq(con->msgr,
				    le32_to_cpu(con->v1.in_reply.global_seq));
		con_out_kvec_reset(con);
		ret = prepare_write_connect(con);
		if (ret < 0)
			return ret;
		prepare_read_connect(con);
		break;

	case CEPH_MSGR_TAG_SEQ:
	case CEPH_MSGR_TAG_READY:
		if (req_feat & ~server_feat) {
			pr_err("%s%lld %s protocol feature mismatch,"
			       " my required %llx > server's %llx, need %llx\n",
			       ENTITY_NAME(con->peer_name),
			       ceph_pr_addr(&con->peer_addr),
			       req_feat, server_feat, req_feat & ~server_feat);
			con->error_msg = "missing required protocol features";
			return -1;
		}

		WARN_ON(con->state != CEPH_CON_S_V1_CONNECT_MSG);
		con->state = CEPH_CON_S_OPEN;
		con->v1.auth_retry = 0;    /* we authenticated; clear flag */
		con->v1.peer_global_seq =
			le32_to_cpu(con->v1.in_reply.global_seq);
		con->v1.connect_seq++;
		con->peer_features = server_feat;
		dout("process_connect got READY gseq %d cseq %d (%d)\n",
		     con->v1.peer_global_seq,
		     le32_to_cpu(con->v1.in_reply.connect_seq),
		     con->v1.connect_seq);
		WARN_ON(con->v1.connect_seq !=
			le32_to_cpu(con->v1.in_reply.connect_seq));

		if (con->v1.in_reply.flags & CEPH_MSG_CONNECT_LOSSY)
			ceph_con_flag_set(con, CEPH_CON_F_LOSSYTX);

		con->delay = 0;      /* reset backoff memory */

		if (con->v1.in_reply.tag == CEPH_MSGR_TAG_SEQ) {
			prepare_write_seq(con);
			prepare_read_seq(con);
		} else {
			prepare_read_tag(con);
		}
		break;

	case CEPH_MSGR_TAG_WAIT:
		/*
		 * If there is a connection race (we are opening
		 * connections to each other), one of us may just have
		 * to WAIT.  This shouldn't happen if we are the
		 * client.
		 */
		con->error_msg = "protocol error, got WAIT as client";
		return -1;

	default:
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
	int size = sizeof(con->v1.in_temp_ack);
	int end = size;

	return read_partial(con, end, size, &con->v1.in_temp_ack);
}

/*
 * We can finally discard anything that's been acked.
 */
static void process_ack(struct ceph_connection *con)
{
	u64 ack = le64_to_cpu(con->v1.in_temp_ack);

	if (con->v1.in_tag == CEPH_MSGR_TAG_ACK)
		ceph_con_discard_sent(con, ack);
	else
		ceph_con_discard_requeued(con, ack);

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
	}
	if (section->iov_len == sec_len)
		*crc = crc32c(0, section->iov_base, section->iov_len);

	return 1;
}

static int read_partial_msg_data(struct ceph_connection *con)
{
	struct ceph_msg_data_cursor *cursor = &con->in_msg->cursor;
	bool do_datacrc = !ceph_test_opt(from_msgr(con->msgr), NOCRC);
	struct page *page;
	size_t page_offset;
	size_t length;
	u32 crc = 0;
	int ret;

	if (do_datacrc)
		crc = con->in_data_crc;
	while (cursor->total_resid) {
		if (!cursor->resid) {
			ceph_msg_data_advance(cursor, 0);
			continue;
		}

		page = ceph_msg_data_next(cursor, &page_offset, &length);
		ret = ceph_tcp_recvpage(con->sock, page, page_offset, length);
		if (ret <= 0) {
			if (do_datacrc)
				con->in_data_crc = crc;

			return ret;
		}

		if (do_datacrc)
			crc = ceph_crc32c_page(crc, page, page_offset, ret);
		ceph_msg_data_advance(cursor, (size_t)ret);
	}
	if (do_datacrc)
		con->in_data_crc = crc;

	return 1;	/* must return > 0 to indicate success */
}

static int read_partial_msg_data_bounce(struct ceph_connection *con)
{
	struct ceph_msg_data_cursor *cursor = &con->in_msg->cursor;
	struct page *page;
	size_t off, len;
	u32 crc;
	int ret;

	if (unlikely(!con->bounce_page)) {
		con->bounce_page = alloc_page(GFP_NOIO);
		if (!con->bounce_page) {
			pr_err("failed to allocate bounce page\n");
			return -ENOMEM;
		}
	}

	crc = con->in_data_crc;
	while (cursor->total_resid) {
		if (!cursor->resid) {
			ceph_msg_data_advance(cursor, 0);
			continue;
		}

		page = ceph_msg_data_next(cursor, &off, &len);
		ret = ceph_tcp_recvpage(con->sock, con->bounce_page, 0, len);
		if (ret <= 0) {
			con->in_data_crc = crc;
			return ret;
		}

		crc = crc32c(crc, page_address(con->bounce_page), ret);
		memcpy_to_page(page, off, page_address(con->bounce_page), ret);

		ceph_msg_data_advance(cursor, ret);
	}
	con->in_data_crc = crc;

	return 1;	/* must return > 0 to indicate success */
}

/*
 * read (part of) a message.
 */
static int read_partial_message(struct ceph_connection *con)
{
	struct ceph_msg *m = con->in_msg;
	int size;
	int end;
	int ret;
	unsigned int front_len, middle_len, data_len;
	bool do_datacrc = !ceph_test_opt(from_msgr(con->msgr), NOCRC);
	bool need_sign = (con->peer_features & CEPH_FEATURE_MSG_AUTH);
	u64 seq;
	u32 crc;

	dout("read_partial_message con %p msg %p\n", con, m);

	/* header */
	size = sizeof(con->v1.in_hdr);
	end = size;
	ret = read_partial(con, end, size, &con->v1.in_hdr);
	if (ret <= 0)
		return ret;

	crc = crc32c(0, &con->v1.in_hdr, offsetof(struct ceph_msg_header, crc));
	if (cpu_to_le32(crc) != con->v1.in_hdr.crc) {
		pr_err("read_partial_message bad hdr crc %u != expected %u\n",
		       crc, con->v1.in_hdr.crc);
		return -EBADMSG;
	}

	front_len = le32_to_cpu(con->v1.in_hdr.front_len);
	if (front_len > CEPH_MSG_MAX_FRONT_LEN)
		return -EIO;
	middle_len = le32_to_cpu(con->v1.in_hdr.middle_len);
	if (middle_len > CEPH_MSG_MAX_MIDDLE_LEN)
		return -EIO;
	data_len = le32_to_cpu(con->v1.in_hdr.data_len);
	if (data_len > CEPH_MSG_MAX_DATA_LEN)
		return -EIO;

	/* verify seq# */
	seq = le64_to_cpu(con->v1.in_hdr.seq);
	if ((s64)seq - (s64)con->in_seq < 1) {
		pr_info("skipping %s%lld %s seq %lld expected %lld\n",
			ENTITY_NAME(con->peer_name),
			ceph_pr_addr(&con->peer_addr),
			seq, con->in_seq + 1);
		con->v1.in_base_pos = -front_len - middle_len - data_len -
				      sizeof_footer(con);
		con->v1.in_tag = CEPH_MSGR_TAG_READY;
		return 1;
	} else if ((s64)seq - (s64)con->in_seq > 1) {
		pr_err("read_partial_message bad seq %lld expected %lld\n",
		       seq, con->in_seq + 1);
		con->error_msg = "bad message sequence # for incoming message";
		return -EBADE;
	}

	/* allocate message? */
	if (!con->in_msg) {
		int skip = 0;

		dout("got hdr type %d front %d data %d\n", con->v1.in_hdr.type,
		     front_len, data_len);
		ret = ceph_con_in_msg_alloc(con, &con->v1.in_hdr, &skip);
		if (ret < 0)
			return ret;

		BUG_ON((!con->in_msg) ^ skip);
		if (skip) {
			/* skip this message */
			dout("alloc_msg said skip message\n");
			con->v1.in_base_pos = -front_len - middle_len -
					      data_len - sizeof_footer(con);
			con->v1.in_tag = CEPH_MSGR_TAG_READY;
			con->in_seq++;
			return 1;
		}

		BUG_ON(!con->in_msg);
		BUG_ON(con->in_msg->con != con);
		m = con->in_msg;
		m->front.iov_len = 0;    /* haven't read it yet */
		if (m->middle)
			m->middle->vec.iov_len = 0;

		/* prepare for data payload, if any */

		if (data_len)
			prepare_message_data(con->in_msg, data_len);
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

	/* (page) data */
	if (data_len) {
		if (!m->num_data_items)
			return -EIO;

		if (ceph_test_opt(from_msgr(con->msgr), RXBOUNCE))
			ret = read_partial_msg_data_bounce(con);
		else
			ret = read_partial_msg_data(con);
		if (ret <= 0)
			return ret;
	}

	/* footer */
	size = sizeof_footer(con);
	end += size;
	ret = read_partial(con, end, size, &m->footer);
	if (ret <= 0)
		return ret;

	if (!need_sign) {
		m->footer.flags = m->old_footer.flags;
		m->footer.sig = 0;
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
	if (do_datacrc &&
	    (m->footer.flags & CEPH_MSG_FOOTER_NOCRC) == 0 &&
	    con->in_data_crc != le32_to_cpu(m->footer.data_crc)) {
		pr_err("read_partial_message %p data crc %u != exp. %u\n", m,
		       con->in_data_crc, le32_to_cpu(m->footer.data_crc));
		return -EBADMSG;
	}

	if (need_sign && con->ops->check_message_signature &&
	    con->ops->check_message_signature(m)) {
		pr_err("read_partial_message %p signature check failed\n", m);
		return -EBADMSG;
	}

	return 1; /* done! */
}

static int read_keepalive_ack(struct ceph_connection *con)
{
	struct ceph_timespec ceph_ts;
	size_t size = sizeof(ceph_ts);
	int ret = read_partial(con, size, size, &ceph_ts);
	if (ret <= 0)
		return ret;
	ceph_decode_timespec64(&con->last_keepalive_ack, &ceph_ts);
	prepare_read_tag(con);
	return 1;
}

/*
 * Read what we can from the socket.
 */
int ceph_con_v1_try_read(struct ceph_connection *con)
{
	int ret = -1;

more:
	dout("try_read start %p state %d\n", con, con->state);
	if (con->state != CEPH_CON_S_V1_BANNER &&
	    con->state != CEPH_CON_S_V1_CONNECT_MSG &&
	    con->state != CEPH_CON_S_OPEN)
		return 0;

	BUG_ON(!con->sock);

	dout("try_read tag %d in_base_pos %d\n", con->v1.in_tag,
	     con->v1.in_base_pos);

	if (con->state == CEPH_CON_S_V1_BANNER) {
		ret = read_partial_banner(con);
		if (ret <= 0)
			goto out;
		ret = process_banner(con);
		if (ret < 0)
			goto out;

		con->state = CEPH_CON_S_V1_CONNECT_MSG;

		/*
		 * Received banner is good, exchange connection info.
		 * Do not reset out_kvec, as sending our banner raced
		 * with receiving peer banner after connect completed.
		 */
		ret = prepare_write_connect(con);
		if (ret < 0)
			goto out;
		prepare_read_connect(con);

		/* Send connection info before awaiting response */
		goto out;
	}

	if (con->state == CEPH_CON_S_V1_CONNECT_MSG) {
		ret = read_partial_connect(con);
		if (ret <= 0)
			goto out;
		ret = process_connect(con);
		if (ret < 0)
			goto out;
		goto more;
	}

	WARN_ON(con->state != CEPH_CON_S_OPEN);

	if (con->v1.in_base_pos < 0) {
		/*
		 * skipping + discarding content.
		 */
		ret = ceph_tcp_recvmsg(con->sock, NULL, -con->v1.in_base_pos);
		if (ret <= 0)
			goto out;
		dout("skipped %d / %d bytes\n", ret, -con->v1.in_base_pos);
		con->v1.in_base_pos += ret;
		if (con->v1.in_base_pos)
			goto more;
	}
	if (con->v1.in_tag == CEPH_MSGR_TAG_READY) {
		/*
		 * what's next?
		 */
		ret = ceph_tcp_recvmsg(con->sock, &con->v1.in_tag, 1);
		if (ret <= 0)
			goto out;
		dout("try_read got tag %d\n", con->v1.in_tag);
		switch (con->v1.in_tag) {
		case CEPH_MSGR_TAG_MSG:
			prepare_read_message(con);
			break;
		case CEPH_MSGR_TAG_ACK:
			prepare_read_ack(con);
			break;
		case CEPH_MSGR_TAG_KEEPALIVE2_ACK:
			prepare_read_keepalive_ack(con);
			break;
		case CEPH_MSGR_TAG_CLOSE:
			ceph_con_close_socket(con);
			con->state = CEPH_CON_S_CLOSED;
			goto out;
		default:
			goto bad_tag;
		}
	}
	if (con->v1.in_tag == CEPH_MSGR_TAG_MSG) {
		ret = read_partial_message(con);
		if (ret <= 0) {
			switch (ret) {
			case -EBADMSG:
				con->error_msg = "bad crc/signature";
				fallthrough;
			case -EBADE:
				ret = -EIO;
				break;
			case -EIO:
				con->error_msg = "io error";
				break;
			}
			goto out;
		}
		if (con->v1.in_tag == CEPH_MSGR_TAG_READY)
			goto more;
		ceph_con_process_message(con);
		if (con->state == CEPH_CON_S_OPEN)
			prepare_read_tag(con);
		goto more;
	}
	if (con->v1.in_tag == CEPH_MSGR_TAG_ACK ||
	    con->v1.in_tag == CEPH_MSGR_TAG_SEQ) {
		/*
		 * the final handshake seq exchange is semantically
		 * equivalent to an ACK
		 */
		ret = read_partial_ack(con);
		if (ret <= 0)
			goto out;
		process_ack(con);
		goto more;
	}
	if (con->v1.in_tag == CEPH_MSGR_TAG_KEEPALIVE2_ACK) {
		ret = read_keepalive_ack(con);
		if (ret <= 0)
			goto out;
		goto more;
	}

out:
	dout("try_read done on %p ret %d\n", con, ret);
	return ret;

bad_tag:
	pr_err("try_read bad tag %d\n", con->v1.in_tag);
	con->error_msg = "protocol error, garbage tag";
	ret = -1;
	goto out;
}

/*
 * Write something to the socket.  Called in a worker thread when the
 * socket appears to be writeable and we have something ready to send.
 */
int ceph_con_v1_try_write(struct ceph_connection *con)
{
	int ret = 1;

	dout("try_write start %p state %d\n", con, con->state);
	if (con->state != CEPH_CON_S_PREOPEN &&
	    con->state != CEPH_CON_S_V1_BANNER &&
	    con->state != CEPH_CON_S_V1_CONNECT_MSG &&
	    con->state != CEPH_CON_S_OPEN)
		return 0;

	/* open the socket first? */
	if (con->state == CEPH_CON_S_PREOPEN) {
		BUG_ON(con->sock);
		con->state = CEPH_CON_S_V1_BANNER;

		con_out_kvec_reset(con);
		prepare_write_banner(con);
		prepare_read_banner(con);

		BUG_ON(con->in_msg);
		con->v1.in_tag = CEPH_MSGR_TAG_READY;
		dout("try_write initiating connect on %p new state %d\n",
		     con, con->state);
		ret = ceph_tcp_connect(con);
		if (ret < 0) {
			con->error_msg = "connect error";
			goto out;
		}
	}

more:
	dout("try_write out_kvec_bytes %d\n", con->v1.out_kvec_bytes);
	BUG_ON(!con->sock);

	/* kvec data queued? */
	if (con->v1.out_kvec_left) {
		ret = write_partial_kvec(con);
		if (ret <= 0)
			goto out;
	}
	if (con->v1.out_skip) {
		ret = write_partial_skip(con);
		if (ret <= 0)
			goto out;
	}

	/* msg pages? */
	if (con->out_msg) {
		if (con->v1.out_msg_done) {
			ceph_msg_put(con->out_msg);
			con->out_msg = NULL;   /* we're done with this one */
			goto do_next;
		}

		ret = write_partial_message_data(con);
		if (ret == 1)
			goto more;  /* we need to send the footer, too! */
		if (ret == 0)
			goto out;
		if (ret < 0) {
			dout("try_write write_partial_message_data err %d\n",
			     ret);
			goto out;
		}
	}

do_next:
	if (con->state == CEPH_CON_S_OPEN) {
		if (ceph_con_flag_test_and_clear(con,
				CEPH_CON_F_KEEPALIVE_PENDING)) {
			prepare_write_keepalive(con);
			goto more;
		}
		/* is anything else pending? */
		if (!list_empty(&con->out_queue)) {
			prepare_write_message(con);
			goto more;
		}
		if (con->in_seq > con->in_seq_acked) {
			prepare_write_ack(con);
			goto more;
		}
	}

	/* Nothing to do! */
	ceph_con_flag_clear(con, CEPH_CON_F_WRITE_PENDING);
	dout("try_write nothing else to write.\n");
	ret = 0;
out:
	dout("try_write done on %p ret %d\n", con, ret);
	return ret;
}

void ceph_con_v1_revoke(struct ceph_connection *con)
{
	struct ceph_msg *msg = con->out_msg;

	WARN_ON(con->v1.out_skip);
	/* footer */
	if (con->v1.out_msg_done) {
		con->v1.out_skip += con_out_kvec_skip(con);
	} else {
		WARN_ON(!msg->data_length);
		con->v1.out_skip += sizeof_footer(con);
	}
	/* data, middle, front */
	if (msg->data_length)
		con->v1.out_skip += msg->cursor.total_resid;
	if (msg->middle)
		con->v1.out_skip += con_out_kvec_skip(con);
	con->v1.out_skip += con_out_kvec_skip(con);

	dout("%s con %p out_kvec_bytes %d out_skip %d\n", __func__, con,
	     con->v1.out_kvec_bytes, con->v1.out_skip);
}

void ceph_con_v1_revoke_incoming(struct ceph_connection *con)
{
	unsigned int front_len = le32_to_cpu(con->v1.in_hdr.front_len);
	unsigned int middle_len = le32_to_cpu(con->v1.in_hdr.middle_len);
	unsigned int data_len = le32_to_cpu(con->v1.in_hdr.data_len);

	/* skip rest of message */
	con->v1.in_base_pos = con->v1.in_base_pos -
			sizeof(struct ceph_msg_header) -
			front_len -
			middle_len -
			data_len -
			sizeof(struct ceph_msg_footer);

	con->v1.in_tag = CEPH_MSGR_TAG_READY;
	con->in_seq++;

	dout("%s con %p in_base_pos %d\n", __func__, con, con->v1.in_base_pos);
}

bool ceph_con_v1_opened(struct ceph_connection *con)
{
	return con->v1.connect_seq;
}

void ceph_con_v1_reset_session(struct ceph_connection *con)
{
	con->v1.connect_seq = 0;
	con->v1.peer_global_seq = 0;
}

void ceph_con_v1_reset_protocol(struct ceph_connection *con)
{
	con->v1.out_skip = 0;
}
