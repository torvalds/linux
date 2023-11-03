// SPDX-License-Identifier: GPL-2.0
/*
 * Ceph msgr2 protocol implementation
 *
 * Copyright (C) 2020 Ilya Dryomov <idryomov@gmail.com>
 */

#include <linux/ceph/ceph_debug.h>

#include <crypto/aead.h>
#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <crypto/utils.h>
#include <linux/bvec.h>
#include <linux/crc32c.h>
#include <linux/net.h>
#include <linux/scatterlist.h>
#include <linux/socket.h>
#include <linux/sched/mm.h>
#include <net/sock.h>
#include <net/tcp.h>

#include <linux/ceph/ceph_features.h>
#include <linux/ceph/decode.h>
#include <linux/ceph/libceph.h>
#include <linux/ceph/messenger.h>

#include "crypto.h"  /* for CEPH_KEY_LEN and CEPH_MAX_CON_SECRET_LEN */

#define FRAME_TAG_HELLO			1
#define FRAME_TAG_AUTH_REQUEST		2
#define FRAME_TAG_AUTH_BAD_METHOD	3
#define FRAME_TAG_AUTH_REPLY_MORE	4
#define FRAME_TAG_AUTH_REQUEST_MORE	5
#define FRAME_TAG_AUTH_DONE		6
#define FRAME_TAG_AUTH_SIGNATURE	7
#define FRAME_TAG_CLIENT_IDENT		8
#define FRAME_TAG_SERVER_IDENT		9
#define FRAME_TAG_IDENT_MISSING_FEATURES 10
#define FRAME_TAG_SESSION_RECONNECT	11
#define FRAME_TAG_SESSION_RESET		12
#define FRAME_TAG_SESSION_RETRY		13
#define FRAME_TAG_SESSION_RETRY_GLOBAL	14
#define FRAME_TAG_SESSION_RECONNECT_OK	15
#define FRAME_TAG_WAIT			16
#define FRAME_TAG_MESSAGE		17
#define FRAME_TAG_KEEPALIVE2		18
#define FRAME_TAG_KEEPALIVE2_ACK	19
#define FRAME_TAG_ACK			20

#define FRAME_LATE_STATUS_ABORTED	0x1
#define FRAME_LATE_STATUS_COMPLETE	0xe
#define FRAME_LATE_STATUS_ABORTED_MASK	0xf

#define IN_S_HANDLE_PREAMBLE			1
#define IN_S_HANDLE_CONTROL			2
#define IN_S_HANDLE_CONTROL_REMAINDER		3
#define IN_S_PREPARE_READ_DATA			4
#define IN_S_PREPARE_READ_DATA_CONT		5
#define IN_S_PREPARE_READ_ENC_PAGE		6
#define IN_S_PREPARE_SPARSE_DATA		7
#define IN_S_PREPARE_SPARSE_DATA_CONT		8
#define IN_S_HANDLE_EPILOGUE			9
#define IN_S_FINISH_SKIP			10

#define OUT_S_QUEUE_DATA		1
#define OUT_S_QUEUE_DATA_CONT		2
#define OUT_S_QUEUE_ENC_PAGE		3
#define OUT_S_QUEUE_ZEROS		4
#define OUT_S_FINISH_MESSAGE		5
#define OUT_S_GET_NEXT			6

#define CTRL_BODY(p)	((void *)(p) + CEPH_PREAMBLE_LEN)
#define FRONT_PAD(p)	((void *)(p) + CEPH_EPILOGUE_SECURE_LEN)
#define MIDDLE_PAD(p)	(FRONT_PAD(p) + CEPH_GCM_BLOCK_LEN)
#define DATA_PAD(p)	(MIDDLE_PAD(p) + CEPH_GCM_BLOCK_LEN)

#define CEPH_MSG_FLAGS (MSG_DONTWAIT | MSG_NOSIGNAL)

static int do_recvmsg(struct socket *sock, struct iov_iter *it)
{
	struct msghdr msg = { .msg_flags = CEPH_MSG_FLAGS };
	int ret;

	msg.msg_iter = *it;
	while (iov_iter_count(it)) {
		ret = sock_recvmsg(sock, &msg, msg.msg_flags);
		if (ret <= 0) {
			if (ret == -EAGAIN)
				ret = 0;
			return ret;
		}

		iov_iter_advance(it, ret);
	}

	WARN_ON(msg_data_left(&msg));
	return 1;
}

/*
 * Read as much as possible.
 *
 * Return:
 *   1 - done, nothing (else) to read
 *   0 - socket is empty, need to wait
 *  <0 - error
 */
static int ceph_tcp_recv(struct ceph_connection *con)
{
	int ret;

	dout("%s con %p %s %zu\n", __func__, con,
	     iov_iter_is_discard(&con->v2.in_iter) ? "discard" : "need",
	     iov_iter_count(&con->v2.in_iter));
	ret = do_recvmsg(con->sock, &con->v2.in_iter);
	dout("%s con %p ret %d left %zu\n", __func__, con, ret,
	     iov_iter_count(&con->v2.in_iter));
	return ret;
}

static int do_sendmsg(struct socket *sock, struct iov_iter *it)
{
	struct msghdr msg = { .msg_flags = CEPH_MSG_FLAGS };
	int ret;

	msg.msg_iter = *it;
	while (iov_iter_count(it)) {
		ret = sock_sendmsg(sock, &msg);
		if (ret <= 0) {
			if (ret == -EAGAIN)
				ret = 0;
			return ret;
		}

		iov_iter_advance(it, ret);
	}

	WARN_ON(msg_data_left(&msg));
	return 1;
}

static int do_try_sendpage(struct socket *sock, struct iov_iter *it)
{
	struct msghdr msg = { .msg_flags = CEPH_MSG_FLAGS };
	struct bio_vec bv;
	int ret;

	if (WARN_ON(!iov_iter_is_bvec(it)))
		return -EINVAL;

	while (iov_iter_count(it)) {
		/* iov_iter_iovec() for ITER_BVEC */
		bvec_set_page(&bv, it->bvec->bv_page,
			      min(iov_iter_count(it),
				  it->bvec->bv_len - it->iov_offset),
			      it->bvec->bv_offset + it->iov_offset);

		/*
		 * MSG_SPLICE_PAGES cannot properly handle pages with
		 * page_count == 0, we need to fall back to sendmsg if
		 * that's the case.
		 *
		 * Same goes for slab pages: skb_can_coalesce() allows
		 * coalescing neighboring slab objects into a single frag
		 * which triggers one of hardened usercopy checks.
		 */
		if (sendpage_ok(bv.bv_page))
			msg.msg_flags |= MSG_SPLICE_PAGES;
		else
			msg.msg_flags &= ~MSG_SPLICE_PAGES;

		iov_iter_bvec(&msg.msg_iter, ITER_SOURCE, &bv, 1, bv.bv_len);
		ret = sock_sendmsg(sock, &msg);
		if (ret <= 0) {
			if (ret == -EAGAIN)
				ret = 0;
			return ret;
		}

		iov_iter_advance(it, ret);
	}

	return 1;
}

/*
 * Write as much as possible.  The socket is expected to be corked,
 * so we don't bother with MSG_MORE here.
 *
 * Return:
 *   1 - done, nothing (else) to write
 *   0 - socket is full, need to wait
 *  <0 - error
 */
static int ceph_tcp_send(struct ceph_connection *con)
{
	int ret;

	dout("%s con %p have %zu try_sendpage %d\n", __func__, con,
	     iov_iter_count(&con->v2.out_iter), con->v2.out_iter_sendpage);
	if (con->v2.out_iter_sendpage)
		ret = do_try_sendpage(con->sock, &con->v2.out_iter);
	else
		ret = do_sendmsg(con->sock, &con->v2.out_iter);
	dout("%s con %p ret %d left %zu\n", __func__, con, ret,
	     iov_iter_count(&con->v2.out_iter));
	return ret;
}

static void add_in_kvec(struct ceph_connection *con, void *buf, int len)
{
	BUG_ON(con->v2.in_kvec_cnt >= ARRAY_SIZE(con->v2.in_kvecs));
	WARN_ON(!iov_iter_is_kvec(&con->v2.in_iter));

	con->v2.in_kvecs[con->v2.in_kvec_cnt].iov_base = buf;
	con->v2.in_kvecs[con->v2.in_kvec_cnt].iov_len = len;
	con->v2.in_kvec_cnt++;

	con->v2.in_iter.nr_segs++;
	con->v2.in_iter.count += len;
}

static void reset_in_kvecs(struct ceph_connection *con)
{
	WARN_ON(iov_iter_count(&con->v2.in_iter));

	con->v2.in_kvec_cnt = 0;
	iov_iter_kvec(&con->v2.in_iter, ITER_DEST, con->v2.in_kvecs, 0, 0);
}

static void set_in_bvec(struct ceph_connection *con, const struct bio_vec *bv)
{
	WARN_ON(iov_iter_count(&con->v2.in_iter));

	con->v2.in_bvec = *bv;
	iov_iter_bvec(&con->v2.in_iter, ITER_DEST, &con->v2.in_bvec, 1, bv->bv_len);
}

static void set_in_skip(struct ceph_connection *con, int len)
{
	WARN_ON(iov_iter_count(&con->v2.in_iter));

	dout("%s con %p len %d\n", __func__, con, len);
	iov_iter_discard(&con->v2.in_iter, ITER_DEST, len);
}

static void add_out_kvec(struct ceph_connection *con, void *buf, int len)
{
	BUG_ON(con->v2.out_kvec_cnt >= ARRAY_SIZE(con->v2.out_kvecs));
	WARN_ON(!iov_iter_is_kvec(&con->v2.out_iter));
	WARN_ON(con->v2.out_zero);

	con->v2.out_kvecs[con->v2.out_kvec_cnt].iov_base = buf;
	con->v2.out_kvecs[con->v2.out_kvec_cnt].iov_len = len;
	con->v2.out_kvec_cnt++;

	con->v2.out_iter.nr_segs++;
	con->v2.out_iter.count += len;
}

static void reset_out_kvecs(struct ceph_connection *con)
{
	WARN_ON(iov_iter_count(&con->v2.out_iter));
	WARN_ON(con->v2.out_zero);

	con->v2.out_kvec_cnt = 0;

	iov_iter_kvec(&con->v2.out_iter, ITER_SOURCE, con->v2.out_kvecs, 0, 0);
	con->v2.out_iter_sendpage = false;
}

static void set_out_bvec(struct ceph_connection *con, const struct bio_vec *bv,
			 bool zerocopy)
{
	WARN_ON(iov_iter_count(&con->v2.out_iter));
	WARN_ON(con->v2.out_zero);

	con->v2.out_bvec = *bv;
	con->v2.out_iter_sendpage = zerocopy;
	iov_iter_bvec(&con->v2.out_iter, ITER_SOURCE, &con->v2.out_bvec, 1,
		      con->v2.out_bvec.bv_len);
}

static void set_out_bvec_zero(struct ceph_connection *con)
{
	WARN_ON(iov_iter_count(&con->v2.out_iter));
	WARN_ON(!con->v2.out_zero);

	bvec_set_page(&con->v2.out_bvec, ceph_zero_page,
		      min(con->v2.out_zero, (int)PAGE_SIZE), 0);
	con->v2.out_iter_sendpage = true;
	iov_iter_bvec(&con->v2.out_iter, ITER_SOURCE, &con->v2.out_bvec, 1,
		      con->v2.out_bvec.bv_len);
}

static void out_zero_add(struct ceph_connection *con, int len)
{
	dout("%s con %p len %d\n", __func__, con, len);
	con->v2.out_zero += len;
}

static void *alloc_conn_buf(struct ceph_connection *con, int len)
{
	void *buf;

	dout("%s con %p len %d\n", __func__, con, len);

	if (WARN_ON(con->v2.conn_buf_cnt >= ARRAY_SIZE(con->v2.conn_bufs)))
		return NULL;

	buf = kvmalloc(len, GFP_NOIO);
	if (!buf)
		return NULL;

	con->v2.conn_bufs[con->v2.conn_buf_cnt++] = buf;
	return buf;
}

static void free_conn_bufs(struct ceph_connection *con)
{
	while (con->v2.conn_buf_cnt)
		kvfree(con->v2.conn_bufs[--con->v2.conn_buf_cnt]);
}

static void add_in_sign_kvec(struct ceph_connection *con, void *buf, int len)
{
	BUG_ON(con->v2.in_sign_kvec_cnt >= ARRAY_SIZE(con->v2.in_sign_kvecs));

	con->v2.in_sign_kvecs[con->v2.in_sign_kvec_cnt].iov_base = buf;
	con->v2.in_sign_kvecs[con->v2.in_sign_kvec_cnt].iov_len = len;
	con->v2.in_sign_kvec_cnt++;
}

static void clear_in_sign_kvecs(struct ceph_connection *con)
{
	con->v2.in_sign_kvec_cnt = 0;
}

static void add_out_sign_kvec(struct ceph_connection *con, void *buf, int len)
{
	BUG_ON(con->v2.out_sign_kvec_cnt >= ARRAY_SIZE(con->v2.out_sign_kvecs));

	con->v2.out_sign_kvecs[con->v2.out_sign_kvec_cnt].iov_base = buf;
	con->v2.out_sign_kvecs[con->v2.out_sign_kvec_cnt].iov_len = len;
	con->v2.out_sign_kvec_cnt++;
}

static void clear_out_sign_kvecs(struct ceph_connection *con)
{
	con->v2.out_sign_kvec_cnt = 0;
}

static bool con_secure(struct ceph_connection *con)
{
	return con->v2.con_mode == CEPH_CON_MODE_SECURE;
}

static int front_len(const struct ceph_msg *msg)
{
	return le32_to_cpu(msg->hdr.front_len);
}

static int middle_len(const struct ceph_msg *msg)
{
	return le32_to_cpu(msg->hdr.middle_len);
}

static int data_len(const struct ceph_msg *msg)
{
	return le32_to_cpu(msg->hdr.data_len);
}

static bool need_padding(int len)
{
	return !IS_ALIGNED(len, CEPH_GCM_BLOCK_LEN);
}

static int padded_len(int len)
{
	return ALIGN(len, CEPH_GCM_BLOCK_LEN);
}

static int padding_len(int len)
{
	return padded_len(len) - len;
}

/* preamble + control segment */
static int head_onwire_len(int ctrl_len, bool secure)
{
	int head_len;
	int rem_len;

	BUG_ON(ctrl_len < 0 || ctrl_len > CEPH_MSG_MAX_CONTROL_LEN);

	if (secure) {
		head_len = CEPH_PREAMBLE_SECURE_LEN;
		if (ctrl_len > CEPH_PREAMBLE_INLINE_LEN) {
			rem_len = ctrl_len - CEPH_PREAMBLE_INLINE_LEN;
			head_len += padded_len(rem_len) + CEPH_GCM_TAG_LEN;
		}
	} else {
		head_len = CEPH_PREAMBLE_PLAIN_LEN;
		if (ctrl_len)
			head_len += ctrl_len + CEPH_CRC_LEN;
	}
	return head_len;
}

/* front, middle and data segments + epilogue */
static int __tail_onwire_len(int front_len, int middle_len, int data_len,
			     bool secure)
{
	BUG_ON(front_len < 0 || front_len > CEPH_MSG_MAX_FRONT_LEN ||
	       middle_len < 0 || middle_len > CEPH_MSG_MAX_MIDDLE_LEN ||
	       data_len < 0 || data_len > CEPH_MSG_MAX_DATA_LEN);

	if (!front_len && !middle_len && !data_len)
		return 0;

	if (!secure)
		return front_len + middle_len + data_len +
		       CEPH_EPILOGUE_PLAIN_LEN;

	return padded_len(front_len) + padded_len(middle_len) +
	       padded_len(data_len) + CEPH_EPILOGUE_SECURE_LEN;
}

static int tail_onwire_len(const struct ceph_msg *msg, bool secure)
{
	return __tail_onwire_len(front_len(msg), middle_len(msg),
				 data_len(msg), secure);
}

/* head_onwire_len(sizeof(struct ceph_msg_header2), false) */
#define MESSAGE_HEAD_PLAIN_LEN	(CEPH_PREAMBLE_PLAIN_LEN +		\
				 sizeof(struct ceph_msg_header2) +	\
				 CEPH_CRC_LEN)

static const int frame_aligns[] = {
	sizeof(void *),
	sizeof(void *),
	sizeof(void *),
	PAGE_SIZE
};

/*
 * Discards trailing empty segments, unless there is just one segment.
 * A frame always has at least one (possibly empty) segment.
 */
static int calc_segment_count(const int *lens, int len_cnt)
{
	int i;

	for (i = len_cnt - 1; i >= 0; i--) {
		if (lens[i])
			return i + 1;
	}

	return 1;
}

static void init_frame_desc(struct ceph_frame_desc *desc, int tag,
			    const int *lens, int len_cnt)
{
	int i;

	memset(desc, 0, sizeof(*desc));

	desc->fd_tag = tag;
	desc->fd_seg_cnt = calc_segment_count(lens, len_cnt);
	BUG_ON(desc->fd_seg_cnt > CEPH_FRAME_MAX_SEGMENT_COUNT);
	for (i = 0; i < desc->fd_seg_cnt; i++) {
		desc->fd_lens[i] = lens[i];
		desc->fd_aligns[i] = frame_aligns[i];
	}
}

/*
 * Preamble crc covers everything up to itself (28 bytes) and
 * is calculated and verified irrespective of the connection mode
 * (i.e. even if the frame is encrypted).
 */
static void encode_preamble(const struct ceph_frame_desc *desc, void *p)
{
	void *crcp = p + CEPH_PREAMBLE_LEN - CEPH_CRC_LEN;
	void *start = p;
	int i;

	memset(p, 0, CEPH_PREAMBLE_LEN);

	ceph_encode_8(&p, desc->fd_tag);
	ceph_encode_8(&p, desc->fd_seg_cnt);
	for (i = 0; i < desc->fd_seg_cnt; i++) {
		ceph_encode_32(&p, desc->fd_lens[i]);
		ceph_encode_16(&p, desc->fd_aligns[i]);
	}

	put_unaligned_le32(crc32c(0, start, crcp - start), crcp);
}

static int decode_preamble(void *p, struct ceph_frame_desc *desc)
{
	void *crcp = p + CEPH_PREAMBLE_LEN - CEPH_CRC_LEN;
	u32 crc, expected_crc;
	int i;

	crc = crc32c(0, p, crcp - p);
	expected_crc = get_unaligned_le32(crcp);
	if (crc != expected_crc) {
		pr_err("bad preamble crc, calculated %u, expected %u\n",
		       crc, expected_crc);
		return -EBADMSG;
	}

	memset(desc, 0, sizeof(*desc));

	desc->fd_tag = ceph_decode_8(&p);
	desc->fd_seg_cnt = ceph_decode_8(&p);
	if (desc->fd_seg_cnt < 1 ||
	    desc->fd_seg_cnt > CEPH_FRAME_MAX_SEGMENT_COUNT) {
		pr_err("bad segment count %d\n", desc->fd_seg_cnt);
		return -EINVAL;
	}
	for (i = 0; i < desc->fd_seg_cnt; i++) {
		desc->fd_lens[i] = ceph_decode_32(&p);
		desc->fd_aligns[i] = ceph_decode_16(&p);
	}

	if (desc->fd_lens[0] < 0 ||
	    desc->fd_lens[0] > CEPH_MSG_MAX_CONTROL_LEN) {
		pr_err("bad control segment length %d\n", desc->fd_lens[0]);
		return -EINVAL;
	}
	if (desc->fd_lens[1] < 0 ||
	    desc->fd_lens[1] > CEPH_MSG_MAX_FRONT_LEN) {
		pr_err("bad front segment length %d\n", desc->fd_lens[1]);
		return -EINVAL;
	}
	if (desc->fd_lens[2] < 0 ||
	    desc->fd_lens[2] > CEPH_MSG_MAX_MIDDLE_LEN) {
		pr_err("bad middle segment length %d\n", desc->fd_lens[2]);
		return -EINVAL;
	}
	if (desc->fd_lens[3] < 0 ||
	    desc->fd_lens[3] > CEPH_MSG_MAX_DATA_LEN) {
		pr_err("bad data segment length %d\n", desc->fd_lens[3]);
		return -EINVAL;
	}

	/*
	 * This would fire for FRAME_TAG_WAIT (it has one empty
	 * segment), but we should never get it as client.
	 */
	if (!desc->fd_lens[desc->fd_seg_cnt - 1]) {
		pr_err("last segment empty, segment count %d\n",
		       desc->fd_seg_cnt);
		return -EINVAL;
	}

	return 0;
}

static void encode_epilogue_plain(struct ceph_connection *con, bool aborted)
{
	con->v2.out_epil.late_status = aborted ? FRAME_LATE_STATUS_ABORTED :
						 FRAME_LATE_STATUS_COMPLETE;
	cpu_to_le32s(&con->v2.out_epil.front_crc);
	cpu_to_le32s(&con->v2.out_epil.middle_crc);
	cpu_to_le32s(&con->v2.out_epil.data_crc);
}

static void encode_epilogue_secure(struct ceph_connection *con, bool aborted)
{
	memset(&con->v2.out_epil, 0, sizeof(con->v2.out_epil));
	con->v2.out_epil.late_status = aborted ? FRAME_LATE_STATUS_ABORTED :
						 FRAME_LATE_STATUS_COMPLETE;
}

static int decode_epilogue(void *p, u32 *front_crc, u32 *middle_crc,
			   u32 *data_crc)
{
	u8 late_status;

	late_status = ceph_decode_8(&p);
	if ((late_status & FRAME_LATE_STATUS_ABORTED_MASK) !=
			FRAME_LATE_STATUS_COMPLETE) {
		/* we should never get an aborted message as client */
		pr_err("bad late_status 0x%x\n", late_status);
		return -EINVAL;
	}

	if (front_crc && middle_crc && data_crc) {
		*front_crc = ceph_decode_32(&p);
		*middle_crc = ceph_decode_32(&p);
		*data_crc = ceph_decode_32(&p);
	}

	return 0;
}

static void fill_header(struct ceph_msg_header *hdr,
			const struct ceph_msg_header2 *hdr2,
			int front_len, int middle_len, int data_len,
			const struct ceph_entity_name *peer_name)
{
	hdr->seq = hdr2->seq;
	hdr->tid = hdr2->tid;
	hdr->type = hdr2->type;
	hdr->priority = hdr2->priority;
	hdr->version = hdr2->version;
	hdr->front_len = cpu_to_le32(front_len);
	hdr->middle_len = cpu_to_le32(middle_len);
	hdr->data_len = cpu_to_le32(data_len);
	hdr->data_off = hdr2->data_off;
	hdr->src = *peer_name;
	hdr->compat_version = hdr2->compat_version;
	hdr->reserved = 0;
	hdr->crc = 0;
}

static void fill_header2(struct ceph_msg_header2 *hdr2,
			 const struct ceph_msg_header *hdr, u64 ack_seq)
{
	hdr2->seq = hdr->seq;
	hdr2->tid = hdr->tid;
	hdr2->type = hdr->type;
	hdr2->priority = hdr->priority;
	hdr2->version = hdr->version;
	hdr2->data_pre_padding_len = 0;
	hdr2->data_off = hdr->data_off;
	hdr2->ack_seq = cpu_to_le64(ack_seq);
	hdr2->flags = 0;
	hdr2->compat_version = hdr->compat_version;
	hdr2->reserved = 0;
}

static int verify_control_crc(struct ceph_connection *con)
{
	int ctrl_len = con->v2.in_desc.fd_lens[0];
	u32 crc, expected_crc;

	WARN_ON(con->v2.in_kvecs[0].iov_len != ctrl_len);
	WARN_ON(con->v2.in_kvecs[1].iov_len != CEPH_CRC_LEN);

	crc = crc32c(-1, con->v2.in_kvecs[0].iov_base, ctrl_len);
	expected_crc = get_unaligned_le32(con->v2.in_kvecs[1].iov_base);
	if (crc != expected_crc) {
		pr_err("bad control crc, calculated %u, expected %u\n",
		       crc, expected_crc);
		return -EBADMSG;
	}

	return 0;
}

static int verify_epilogue_crcs(struct ceph_connection *con, u32 front_crc,
				u32 middle_crc, u32 data_crc)
{
	if (front_len(con->in_msg)) {
		con->in_front_crc = crc32c(-1, con->in_msg->front.iov_base,
					   front_len(con->in_msg));
	} else {
		WARN_ON(!middle_len(con->in_msg) && !data_len(con->in_msg));
		con->in_front_crc = -1;
	}

	if (middle_len(con->in_msg))
		con->in_middle_crc = crc32c(-1,
					    con->in_msg->middle->vec.iov_base,
					    middle_len(con->in_msg));
	else if (data_len(con->in_msg))
		con->in_middle_crc = -1;
	else
		con->in_middle_crc = 0;

	if (!data_len(con->in_msg))
		con->in_data_crc = 0;

	dout("%s con %p msg %p crcs %u %u %u\n", __func__, con, con->in_msg,
	     con->in_front_crc, con->in_middle_crc, con->in_data_crc);

	if (con->in_front_crc != front_crc) {
		pr_err("bad front crc, calculated %u, expected %u\n",
		       con->in_front_crc, front_crc);
		return -EBADMSG;
	}
	if (con->in_middle_crc != middle_crc) {
		pr_err("bad middle crc, calculated %u, expected %u\n",
		       con->in_middle_crc, middle_crc);
		return -EBADMSG;
	}
	if (con->in_data_crc != data_crc) {
		pr_err("bad data crc, calculated %u, expected %u\n",
		       con->in_data_crc, data_crc);
		return -EBADMSG;
	}

	return 0;
}

static int setup_crypto(struct ceph_connection *con,
			const u8 *session_key, int session_key_len,
			const u8 *con_secret, int con_secret_len)
{
	unsigned int noio_flag;
	int ret;

	dout("%s con %p con_mode %d session_key_len %d con_secret_len %d\n",
	     __func__, con, con->v2.con_mode, session_key_len, con_secret_len);
	WARN_ON(con->v2.hmac_tfm || con->v2.gcm_tfm || con->v2.gcm_req);

	if (con->v2.con_mode != CEPH_CON_MODE_CRC &&
	    con->v2.con_mode != CEPH_CON_MODE_SECURE) {
		pr_err("bad con_mode %d\n", con->v2.con_mode);
		return -EINVAL;
	}

	if (!session_key_len) {
		WARN_ON(con->v2.con_mode != CEPH_CON_MODE_CRC);
		WARN_ON(con_secret_len);
		return 0;  /* auth_none */
	}

	noio_flag = memalloc_noio_save();
	con->v2.hmac_tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	memalloc_noio_restore(noio_flag);
	if (IS_ERR(con->v2.hmac_tfm)) {
		ret = PTR_ERR(con->v2.hmac_tfm);
		con->v2.hmac_tfm = NULL;
		pr_err("failed to allocate hmac tfm context: %d\n", ret);
		return ret;
	}

	ret = crypto_shash_setkey(con->v2.hmac_tfm, session_key,
				  session_key_len);
	if (ret) {
		pr_err("failed to set hmac key: %d\n", ret);
		return ret;
	}

	if (con->v2.con_mode == CEPH_CON_MODE_CRC) {
		WARN_ON(con_secret_len);
		return 0;  /* auth_x, plain mode */
	}

	if (con_secret_len < CEPH_GCM_KEY_LEN + 2 * CEPH_GCM_IV_LEN) {
		pr_err("con_secret too small %d\n", con_secret_len);
		return -EINVAL;
	}

	noio_flag = memalloc_noio_save();
	con->v2.gcm_tfm = crypto_alloc_aead("gcm(aes)", 0, 0);
	memalloc_noio_restore(noio_flag);
	if (IS_ERR(con->v2.gcm_tfm)) {
		ret = PTR_ERR(con->v2.gcm_tfm);
		con->v2.gcm_tfm = NULL;
		pr_err("failed to allocate gcm tfm context: %d\n", ret);
		return ret;
	}

	WARN_ON((unsigned long)con_secret &
		crypto_aead_alignmask(con->v2.gcm_tfm));
	ret = crypto_aead_setkey(con->v2.gcm_tfm, con_secret, CEPH_GCM_KEY_LEN);
	if (ret) {
		pr_err("failed to set gcm key: %d\n", ret);
		return ret;
	}

	WARN_ON(crypto_aead_ivsize(con->v2.gcm_tfm) != CEPH_GCM_IV_LEN);
	ret = crypto_aead_setauthsize(con->v2.gcm_tfm, CEPH_GCM_TAG_LEN);
	if (ret) {
		pr_err("failed to set gcm tag size: %d\n", ret);
		return ret;
	}

	con->v2.gcm_req = aead_request_alloc(con->v2.gcm_tfm, GFP_NOIO);
	if (!con->v2.gcm_req) {
		pr_err("failed to allocate gcm request\n");
		return -ENOMEM;
	}

	crypto_init_wait(&con->v2.gcm_wait);
	aead_request_set_callback(con->v2.gcm_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  crypto_req_done, &con->v2.gcm_wait);

	memcpy(&con->v2.in_gcm_nonce, con_secret + CEPH_GCM_KEY_LEN,
	       CEPH_GCM_IV_LEN);
	memcpy(&con->v2.out_gcm_nonce,
	       con_secret + CEPH_GCM_KEY_LEN + CEPH_GCM_IV_LEN,
	       CEPH_GCM_IV_LEN);
	return 0;  /* auth_x, secure mode */
}

static int hmac_sha256(struct ceph_connection *con, const struct kvec *kvecs,
		       int kvec_cnt, u8 *hmac)
{
	SHASH_DESC_ON_STACK(desc, con->v2.hmac_tfm);  /* tfm arg is ignored */
	int ret;
	int i;

	dout("%s con %p hmac_tfm %p kvec_cnt %d\n", __func__, con,
	     con->v2.hmac_tfm, kvec_cnt);

	if (!con->v2.hmac_tfm) {
		memset(hmac, 0, SHA256_DIGEST_SIZE);
		return 0;  /* auth_none */
	}

	desc->tfm = con->v2.hmac_tfm;
	ret = crypto_shash_init(desc);
	if (ret)
		goto out;

	for (i = 0; i < kvec_cnt; i++) {
		ret = crypto_shash_update(desc, kvecs[i].iov_base,
					  kvecs[i].iov_len);
		if (ret)
			goto out;
	}

	ret = crypto_shash_final(desc, hmac);

out:
	shash_desc_zero(desc);
	return ret;  /* auth_x, both plain and secure modes */
}

static void gcm_inc_nonce(struct ceph_gcm_nonce *nonce)
{
	u64 counter;

	counter = le64_to_cpu(nonce->counter);
	nonce->counter = cpu_to_le64(counter + 1);
}

static int gcm_crypt(struct ceph_connection *con, bool encrypt,
		     struct scatterlist *src, struct scatterlist *dst,
		     int src_len)
{
	struct ceph_gcm_nonce *nonce;
	int ret;

	nonce = encrypt ? &con->v2.out_gcm_nonce : &con->v2.in_gcm_nonce;

	aead_request_set_ad(con->v2.gcm_req, 0);  /* no AAD */
	aead_request_set_crypt(con->v2.gcm_req, src, dst, src_len, (u8 *)nonce);
	ret = crypto_wait_req(encrypt ? crypto_aead_encrypt(con->v2.gcm_req) :
					crypto_aead_decrypt(con->v2.gcm_req),
			      &con->v2.gcm_wait);
	if (ret)
		return ret;

	gcm_inc_nonce(nonce);
	return 0;
}

static void get_bvec_at(struct ceph_msg_data_cursor *cursor,
			struct bio_vec *bv)
{
	struct page *page;
	size_t off, len;

	WARN_ON(!cursor->total_resid);

	/* skip zero-length data items */
	while (!cursor->resid)
		ceph_msg_data_advance(cursor, 0);

	/* get a piece of data, cursor isn't advanced */
	page = ceph_msg_data_next(cursor, &off, &len);
	bvec_set_page(bv, page, len, off);
}

static int calc_sg_cnt(void *buf, int buf_len)
{
	int sg_cnt;

	if (!buf_len)
		return 0;

	sg_cnt = need_padding(buf_len) ? 1 : 0;
	if (is_vmalloc_addr(buf)) {
		WARN_ON(offset_in_page(buf));
		sg_cnt += PAGE_ALIGN(buf_len) >> PAGE_SHIFT;
	} else {
		sg_cnt++;
	}

	return sg_cnt;
}

static int calc_sg_cnt_cursor(struct ceph_msg_data_cursor *cursor)
{
	int data_len = cursor->total_resid;
	struct bio_vec bv;
	int sg_cnt;

	if (!data_len)
		return 0;

	sg_cnt = need_padding(data_len) ? 1 : 0;
	do {
		get_bvec_at(cursor, &bv);
		sg_cnt++;

		ceph_msg_data_advance(cursor, bv.bv_len);
	} while (cursor->total_resid);

	return sg_cnt;
}

static void init_sgs(struct scatterlist **sg, void *buf, int buf_len, u8 *pad)
{
	void *end = buf + buf_len;
	struct page *page;
	int len;
	void *p;

	if (!buf_len)
		return;

	if (is_vmalloc_addr(buf)) {
		p = buf;
		do {
			page = vmalloc_to_page(p);
			len = min_t(int, end - p, PAGE_SIZE);
			WARN_ON(!page || !len || offset_in_page(p));
			sg_set_page(*sg, page, len, 0);
			*sg = sg_next(*sg);
			p += len;
		} while (p != end);
	} else {
		sg_set_buf(*sg, buf, buf_len);
		*sg = sg_next(*sg);
	}

	if (need_padding(buf_len)) {
		sg_set_buf(*sg, pad, padding_len(buf_len));
		*sg = sg_next(*sg);
	}
}

static void init_sgs_cursor(struct scatterlist **sg,
			    struct ceph_msg_data_cursor *cursor, u8 *pad)
{
	int data_len = cursor->total_resid;
	struct bio_vec bv;

	if (!data_len)
		return;

	do {
		get_bvec_at(cursor, &bv);
		sg_set_page(*sg, bv.bv_page, bv.bv_len, bv.bv_offset);
		*sg = sg_next(*sg);

		ceph_msg_data_advance(cursor, bv.bv_len);
	} while (cursor->total_resid);

	if (need_padding(data_len)) {
		sg_set_buf(*sg, pad, padding_len(data_len));
		*sg = sg_next(*sg);
	}
}

/**
 * init_sgs_pages: set up scatterlist on an array of page pointers
 * @sg:		scatterlist to populate
 * @pages:	pointer to page array
 * @dpos:	position in the array to start (bytes)
 * @dlen:	len to add to sg (bytes)
 * @pad:	pointer to pad destination (if any)
 *
 * Populate the scatterlist from the page array, starting at an arbitrary
 * byte in the array and running for a specified length.
 */
static void init_sgs_pages(struct scatterlist **sg, struct page **pages,
			   int dpos, int dlen, u8 *pad)
{
	int idx = dpos >> PAGE_SHIFT;
	int off = offset_in_page(dpos);
	int resid = dlen;

	do {
		int len = min(resid, (int)PAGE_SIZE - off);

		sg_set_page(*sg, pages[idx], len, off);
		*sg = sg_next(*sg);
		off = 0;
		++idx;
		resid -= len;
	} while (resid);

	if (need_padding(dlen)) {
		sg_set_buf(*sg, pad, padding_len(dlen));
		*sg = sg_next(*sg);
	}
}

static int setup_message_sgs(struct sg_table *sgt, struct ceph_msg *msg,
			     u8 *front_pad, u8 *middle_pad, u8 *data_pad,
			     void *epilogue, struct page **pages, int dpos,
			     bool add_tag)
{
	struct ceph_msg_data_cursor cursor;
	struct scatterlist *cur_sg;
	int dlen = data_len(msg);
	int sg_cnt;
	int ret;

	if (!front_len(msg) && !middle_len(msg) && !data_len(msg))
		return 0;

	sg_cnt = 1;  /* epilogue + [auth tag] */
	if (front_len(msg))
		sg_cnt += calc_sg_cnt(msg->front.iov_base,
				      front_len(msg));
	if (middle_len(msg))
		sg_cnt += calc_sg_cnt(msg->middle->vec.iov_base,
				      middle_len(msg));
	if (dlen) {
		if (pages) {
			sg_cnt += calc_pages_for(dpos, dlen);
			if (need_padding(dlen))
				sg_cnt++;
		} else {
			ceph_msg_data_cursor_init(&cursor, msg, dlen);
			sg_cnt += calc_sg_cnt_cursor(&cursor);
		}
	}

	ret = sg_alloc_table(sgt, sg_cnt, GFP_NOIO);
	if (ret)
		return ret;

	cur_sg = sgt->sgl;
	if (front_len(msg))
		init_sgs(&cur_sg, msg->front.iov_base, front_len(msg),
			 front_pad);
	if (middle_len(msg))
		init_sgs(&cur_sg, msg->middle->vec.iov_base, middle_len(msg),
			 middle_pad);
	if (dlen) {
		if (pages) {
			init_sgs_pages(&cur_sg, pages, dpos, dlen, data_pad);
		} else {
			ceph_msg_data_cursor_init(&cursor, msg, dlen);
			init_sgs_cursor(&cur_sg, &cursor, data_pad);
		}
	}

	WARN_ON(!sg_is_last(cur_sg));
	sg_set_buf(cur_sg, epilogue,
		   CEPH_GCM_BLOCK_LEN + (add_tag ? CEPH_GCM_TAG_LEN : 0));
	return 0;
}

static int decrypt_preamble(struct ceph_connection *con)
{
	struct scatterlist sg;

	sg_init_one(&sg, con->v2.in_buf, CEPH_PREAMBLE_SECURE_LEN);
	return gcm_crypt(con, false, &sg, &sg, CEPH_PREAMBLE_SECURE_LEN);
}

static int decrypt_control_remainder(struct ceph_connection *con)
{
	int ctrl_len = con->v2.in_desc.fd_lens[0];
	int rem_len = ctrl_len - CEPH_PREAMBLE_INLINE_LEN;
	int pt_len = padding_len(rem_len) + CEPH_GCM_TAG_LEN;
	struct scatterlist sgs[2];

	WARN_ON(con->v2.in_kvecs[0].iov_len != rem_len);
	WARN_ON(con->v2.in_kvecs[1].iov_len != pt_len);

	sg_init_table(sgs, 2);
	sg_set_buf(&sgs[0], con->v2.in_kvecs[0].iov_base, rem_len);
	sg_set_buf(&sgs[1], con->v2.in_buf, pt_len);

	return gcm_crypt(con, false, sgs, sgs,
			 padded_len(rem_len) + CEPH_GCM_TAG_LEN);
}

/* Process sparse read data that lives in a buffer */
static int process_v2_sparse_read(struct ceph_connection *con,
				  struct page **pages, int spos)
{
	struct ceph_msg_data_cursor *cursor = &con->v2.in_cursor;
	int ret;

	for (;;) {
		char *buf = NULL;

		ret = con->ops->sparse_read(con, cursor, &buf);
		if (ret <= 0)
			return ret;

		dout("%s: sparse_read return %x buf %p\n", __func__, ret, buf);

		do {
			int idx = spos >> PAGE_SHIFT;
			int soff = offset_in_page(spos);
			struct page *spage = con->v2.in_enc_pages[idx];
			int len = min_t(int, ret, PAGE_SIZE - soff);

			if (buf) {
				memcpy_from_page(buf, spage, soff, len);
				buf += len;
			} else {
				struct bio_vec bv;

				get_bvec_at(cursor, &bv);
				len = min_t(int, len, bv.bv_len);
				memcpy_page(bv.bv_page, bv.bv_offset,
					    spage, soff, len);
				ceph_msg_data_advance(cursor, len);
			}
			spos += len;
			ret -= len;
		} while (ret);
	}
}

static int decrypt_tail(struct ceph_connection *con)
{
	struct sg_table enc_sgt = {};
	struct sg_table sgt = {};
	struct page **pages = NULL;
	bool sparse = con->in_msg->sparse_read;
	int dpos = 0;
	int tail_len;
	int ret;

	tail_len = tail_onwire_len(con->in_msg, true);
	ret = sg_alloc_table_from_pages(&enc_sgt, con->v2.in_enc_pages,
					con->v2.in_enc_page_cnt, 0, tail_len,
					GFP_NOIO);
	if (ret)
		goto out;

	if (sparse) {
		dpos = padded_len(front_len(con->in_msg) + padded_len(middle_len(con->in_msg)));
		pages = con->v2.in_enc_pages;
	}

	ret = setup_message_sgs(&sgt, con->in_msg, FRONT_PAD(con->v2.in_buf),
				MIDDLE_PAD(con->v2.in_buf), DATA_PAD(con->v2.in_buf),
				con->v2.in_buf, pages, dpos, true);
	if (ret)
		goto out;

	dout("%s con %p msg %p enc_page_cnt %d sg_cnt %d\n", __func__, con,
	     con->in_msg, con->v2.in_enc_page_cnt, sgt.orig_nents);
	ret = gcm_crypt(con, false, enc_sgt.sgl, sgt.sgl, tail_len);
	if (ret)
		goto out;

	if (sparse && data_len(con->in_msg)) {
		ret = process_v2_sparse_read(con, con->v2.in_enc_pages, dpos);
		if (ret)
			goto out;
	}

	WARN_ON(!con->v2.in_enc_page_cnt);
	ceph_release_page_vector(con->v2.in_enc_pages,
				 con->v2.in_enc_page_cnt);
	con->v2.in_enc_pages = NULL;
	con->v2.in_enc_page_cnt = 0;

out:
	sg_free_table(&sgt);
	sg_free_table(&enc_sgt);
	return ret;
}

static int prepare_banner(struct ceph_connection *con)
{
	int buf_len = CEPH_BANNER_V2_LEN + 2 + 8 + 8;
	void *buf, *p;

	buf = alloc_conn_buf(con, buf_len);
	if (!buf)
		return -ENOMEM;

	p = buf;
	ceph_encode_copy(&p, CEPH_BANNER_V2, CEPH_BANNER_V2_LEN);
	ceph_encode_16(&p, sizeof(u64) + sizeof(u64));
	ceph_encode_64(&p, CEPH_MSGR2_SUPPORTED_FEATURES);
	ceph_encode_64(&p, CEPH_MSGR2_REQUIRED_FEATURES);
	WARN_ON(p != buf + buf_len);

	add_out_kvec(con, buf, buf_len);
	add_out_sign_kvec(con, buf, buf_len);
	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
	return 0;
}

/*
 * base:
 *   preamble
 *   control body (ctrl_len bytes)
 *   space for control crc
 *
 * extdata (optional):
 *   control body (extdata_len bytes)
 *
 * Compute control crc and gather base and extdata into:
 *
 *   preamble
 *   control body (ctrl_len + extdata_len bytes)
 *   control crc
 *
 * Preamble should already be encoded at the start of base.
 */
static void prepare_head_plain(struct ceph_connection *con, void *base,
			       int ctrl_len, void *extdata, int extdata_len,
			       bool to_be_signed)
{
	int base_len = CEPH_PREAMBLE_LEN + ctrl_len + CEPH_CRC_LEN;
	void *crcp = base + base_len - CEPH_CRC_LEN;
	u32 crc;

	crc = crc32c(-1, CTRL_BODY(base), ctrl_len);
	if (extdata_len)
		crc = crc32c(crc, extdata, extdata_len);
	put_unaligned_le32(crc, crcp);

	if (!extdata_len) {
		add_out_kvec(con, base, base_len);
		if (to_be_signed)
			add_out_sign_kvec(con, base, base_len);
		return;
	}

	add_out_kvec(con, base, crcp - base);
	add_out_kvec(con, extdata, extdata_len);
	add_out_kvec(con, crcp, CEPH_CRC_LEN);
	if (to_be_signed) {
		add_out_sign_kvec(con, base, crcp - base);
		add_out_sign_kvec(con, extdata, extdata_len);
		add_out_sign_kvec(con, crcp, CEPH_CRC_LEN);
	}
}

static int prepare_head_secure_small(struct ceph_connection *con,
				     void *base, int ctrl_len)
{
	struct scatterlist sg;
	int ret;

	/* inline buffer padding? */
	if (ctrl_len < CEPH_PREAMBLE_INLINE_LEN)
		memset(CTRL_BODY(base) + ctrl_len, 0,
		       CEPH_PREAMBLE_INLINE_LEN - ctrl_len);

	sg_init_one(&sg, base, CEPH_PREAMBLE_SECURE_LEN);
	ret = gcm_crypt(con, true, &sg, &sg,
			CEPH_PREAMBLE_SECURE_LEN - CEPH_GCM_TAG_LEN);
	if (ret)
		return ret;

	add_out_kvec(con, base, CEPH_PREAMBLE_SECURE_LEN);
	return 0;
}

/*
 * base:
 *   preamble
 *   control body (ctrl_len bytes)
 *   space for padding, if needed
 *   space for control remainder auth tag
 *   space for preamble auth tag
 *
 * Encrypt preamble and the inline portion, then encrypt the remainder
 * and gather into:
 *
 *   preamble
 *   control body (48 bytes)
 *   preamble auth tag
 *   control body (ctrl_len - 48 bytes)
 *   zero padding, if needed
 *   control remainder auth tag
 *
 * Preamble should already be encoded at the start of base.
 */
static int prepare_head_secure_big(struct ceph_connection *con,
				   void *base, int ctrl_len)
{
	int rem_len = ctrl_len - CEPH_PREAMBLE_INLINE_LEN;
	void *rem = CTRL_BODY(base) + CEPH_PREAMBLE_INLINE_LEN;
	void *rem_tag = rem + padded_len(rem_len);
	void *pmbl_tag = rem_tag + CEPH_GCM_TAG_LEN;
	struct scatterlist sgs[2];
	int ret;

	sg_init_table(sgs, 2);
	sg_set_buf(&sgs[0], base, rem - base);
	sg_set_buf(&sgs[1], pmbl_tag, CEPH_GCM_TAG_LEN);
	ret = gcm_crypt(con, true, sgs, sgs, rem - base);
	if (ret)
		return ret;

	/* control remainder padding? */
	if (need_padding(rem_len))
		memset(rem + rem_len, 0, padding_len(rem_len));

	sg_init_one(&sgs[0], rem, pmbl_tag - rem);
	ret = gcm_crypt(con, true, sgs, sgs, rem_tag - rem);
	if (ret)
		return ret;

	add_out_kvec(con, base, rem - base);
	add_out_kvec(con, pmbl_tag, CEPH_GCM_TAG_LEN);
	add_out_kvec(con, rem, pmbl_tag - rem);
	return 0;
}

static int __prepare_control(struct ceph_connection *con, int tag,
			     void *base, int ctrl_len, void *extdata,
			     int extdata_len, bool to_be_signed)
{
	int total_len = ctrl_len + extdata_len;
	struct ceph_frame_desc desc;
	int ret;

	dout("%s con %p tag %d len %d (%d+%d)\n", __func__, con, tag,
	     total_len, ctrl_len, extdata_len);

	/* extdata may be vmalloc'ed but not base */
	if (WARN_ON(is_vmalloc_addr(base) || !ctrl_len))
		return -EINVAL;

	init_frame_desc(&desc, tag, &total_len, 1);
	encode_preamble(&desc, base);

	if (con_secure(con)) {
		if (WARN_ON(extdata_len || to_be_signed))
			return -EINVAL;

		if (ctrl_len <= CEPH_PREAMBLE_INLINE_LEN)
			/* fully inlined, inline buffer may need padding */
			ret = prepare_head_secure_small(con, base, ctrl_len);
		else
			/* partially inlined, inline buffer is full */
			ret = prepare_head_secure_big(con, base, ctrl_len);
		if (ret)
			return ret;
	} else {
		prepare_head_plain(con, base, ctrl_len, extdata, extdata_len,
				   to_be_signed);
	}

	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
	return 0;
}

static int prepare_control(struct ceph_connection *con, int tag,
			   void *base, int ctrl_len)
{
	return __prepare_control(con, tag, base, ctrl_len, NULL, 0, false);
}

static int prepare_hello(struct ceph_connection *con)
{
	void *buf, *p;
	int ctrl_len;

	ctrl_len = 1 + ceph_entity_addr_encoding_len(&con->peer_addr);
	buf = alloc_conn_buf(con, head_onwire_len(ctrl_len, false));
	if (!buf)
		return -ENOMEM;

	p = CTRL_BODY(buf);
	ceph_encode_8(&p, CEPH_ENTITY_TYPE_CLIENT);
	ceph_encode_entity_addr(&p, &con->peer_addr);
	WARN_ON(p != CTRL_BODY(buf) + ctrl_len);

	return __prepare_control(con, FRAME_TAG_HELLO, buf, ctrl_len,
				 NULL, 0, true);
}

/* so that head_onwire_len(AUTH_BUF_LEN, false) is 512 */
#define AUTH_BUF_LEN	(512 - CEPH_CRC_LEN - CEPH_PREAMBLE_PLAIN_LEN)

static int prepare_auth_request(struct ceph_connection *con)
{
	void *authorizer, *authorizer_copy;
	int ctrl_len, authorizer_len;
	void *buf;
	int ret;

	ctrl_len = AUTH_BUF_LEN;
	buf = alloc_conn_buf(con, head_onwire_len(ctrl_len, false));
	if (!buf)
		return -ENOMEM;

	mutex_unlock(&con->mutex);
	ret = con->ops->get_auth_request(con, CTRL_BODY(buf), &ctrl_len,
					 &authorizer, &authorizer_len);
	mutex_lock(&con->mutex);
	if (con->state != CEPH_CON_S_V2_HELLO) {
		dout("%s con %p state changed to %d\n", __func__, con,
		     con->state);
		return -EAGAIN;
	}

	dout("%s con %p get_auth_request ret %d\n", __func__, con, ret);
	if (ret)
		return ret;

	authorizer_copy = alloc_conn_buf(con, authorizer_len);
	if (!authorizer_copy)
		return -ENOMEM;

	memcpy(authorizer_copy, authorizer, authorizer_len);

	return __prepare_control(con, FRAME_TAG_AUTH_REQUEST, buf, ctrl_len,
				 authorizer_copy, authorizer_len, true);
}

static int prepare_auth_request_more(struct ceph_connection *con,
				     void *reply, int reply_len)
{
	int ctrl_len, authorizer_len;
	void *authorizer;
	void *buf;
	int ret;

	ctrl_len = AUTH_BUF_LEN;
	buf = alloc_conn_buf(con, head_onwire_len(ctrl_len, false));
	if (!buf)
		return -ENOMEM;

	mutex_unlock(&con->mutex);
	ret = con->ops->handle_auth_reply_more(con, reply, reply_len,
					       CTRL_BODY(buf), &ctrl_len,
					       &authorizer, &authorizer_len);
	mutex_lock(&con->mutex);
	if (con->state != CEPH_CON_S_V2_AUTH) {
		dout("%s con %p state changed to %d\n", __func__, con,
		     con->state);
		return -EAGAIN;
	}

	dout("%s con %p handle_auth_reply_more ret %d\n", __func__, con, ret);
	if (ret)
		return ret;

	return __prepare_control(con, FRAME_TAG_AUTH_REQUEST_MORE, buf,
				 ctrl_len, authorizer, authorizer_len, true);
}

static int prepare_auth_signature(struct ceph_connection *con)
{
	void *buf;
	int ret;

	buf = alloc_conn_buf(con, head_onwire_len(SHA256_DIGEST_SIZE,
						  con_secure(con)));
	if (!buf)
		return -ENOMEM;

	ret = hmac_sha256(con, con->v2.in_sign_kvecs, con->v2.in_sign_kvec_cnt,
			  CTRL_BODY(buf));
	if (ret)
		return ret;

	return prepare_control(con, FRAME_TAG_AUTH_SIGNATURE, buf,
			       SHA256_DIGEST_SIZE);
}

static int prepare_client_ident(struct ceph_connection *con)
{
	struct ceph_entity_addr *my_addr = &con->msgr->inst.addr;
	struct ceph_client *client = from_msgr(con->msgr);
	u64 global_id = ceph_client_gid(client);
	void *buf, *p;
	int ctrl_len;

	WARN_ON(con->v2.server_cookie);
	WARN_ON(con->v2.connect_seq);
	WARN_ON(con->v2.peer_global_seq);

	if (!con->v2.client_cookie) {
		do {
			get_random_bytes(&con->v2.client_cookie,
					 sizeof(con->v2.client_cookie));
		} while (!con->v2.client_cookie);
		dout("%s con %p generated cookie 0x%llx\n", __func__, con,
		     con->v2.client_cookie);
	} else {
		dout("%s con %p cookie already set 0x%llx\n", __func__, con,
		     con->v2.client_cookie);
	}

	dout("%s con %p my_addr %s/%u peer_addr %s/%u global_id %llu global_seq %llu features 0x%llx required_features 0x%llx cookie 0x%llx\n",
	     __func__, con, ceph_pr_addr(my_addr), le32_to_cpu(my_addr->nonce),
	     ceph_pr_addr(&con->peer_addr), le32_to_cpu(con->peer_addr.nonce),
	     global_id, con->v2.global_seq, client->supported_features,
	     client->required_features, con->v2.client_cookie);

	ctrl_len = 1 + 4 + ceph_entity_addr_encoding_len(my_addr) +
		   ceph_entity_addr_encoding_len(&con->peer_addr) + 6 * 8;
	buf = alloc_conn_buf(con, head_onwire_len(ctrl_len, con_secure(con)));
	if (!buf)
		return -ENOMEM;

	p = CTRL_BODY(buf);
	ceph_encode_8(&p, 2);  /* addrvec marker */
	ceph_encode_32(&p, 1);  /* addr_cnt */
	ceph_encode_entity_addr(&p, my_addr);
	ceph_encode_entity_addr(&p, &con->peer_addr);
	ceph_encode_64(&p, global_id);
	ceph_encode_64(&p, con->v2.global_seq);
	ceph_encode_64(&p, client->supported_features);
	ceph_encode_64(&p, client->required_features);
	ceph_encode_64(&p, 0);  /* flags */
	ceph_encode_64(&p, con->v2.client_cookie);
	WARN_ON(p != CTRL_BODY(buf) + ctrl_len);

	return prepare_control(con, FRAME_TAG_CLIENT_IDENT, buf, ctrl_len);
}

static int prepare_session_reconnect(struct ceph_connection *con)
{
	struct ceph_entity_addr *my_addr = &con->msgr->inst.addr;
	void *buf, *p;
	int ctrl_len;

	WARN_ON(!con->v2.client_cookie);
	WARN_ON(!con->v2.server_cookie);
	WARN_ON(!con->v2.connect_seq);
	WARN_ON(!con->v2.peer_global_seq);

	dout("%s con %p my_addr %s/%u client_cookie 0x%llx server_cookie 0x%llx global_seq %llu connect_seq %llu in_seq %llu\n",
	     __func__, con, ceph_pr_addr(my_addr), le32_to_cpu(my_addr->nonce),
	     con->v2.client_cookie, con->v2.server_cookie, con->v2.global_seq,
	     con->v2.connect_seq, con->in_seq);

	ctrl_len = 1 + 4 + ceph_entity_addr_encoding_len(my_addr) + 5 * 8;
	buf = alloc_conn_buf(con, head_onwire_len(ctrl_len, con_secure(con)));
	if (!buf)
		return -ENOMEM;

	p = CTRL_BODY(buf);
	ceph_encode_8(&p, 2);  /* entity_addrvec_t marker */
	ceph_encode_32(&p, 1);  /* my_addrs len */
	ceph_encode_entity_addr(&p, my_addr);
	ceph_encode_64(&p, con->v2.client_cookie);
	ceph_encode_64(&p, con->v2.server_cookie);
	ceph_encode_64(&p, con->v2.global_seq);
	ceph_encode_64(&p, con->v2.connect_seq);
	ceph_encode_64(&p, con->in_seq);
	WARN_ON(p != CTRL_BODY(buf) + ctrl_len);

	return prepare_control(con, FRAME_TAG_SESSION_RECONNECT, buf, ctrl_len);
}

static int prepare_keepalive2(struct ceph_connection *con)
{
	struct ceph_timespec *ts = CTRL_BODY(con->v2.out_buf);
	struct timespec64 now;

	ktime_get_real_ts64(&now);
	dout("%s con %p timestamp %lld.%09ld\n", __func__, con, now.tv_sec,
	     now.tv_nsec);

	ceph_encode_timespec64(ts, &now);

	reset_out_kvecs(con);
	return prepare_control(con, FRAME_TAG_KEEPALIVE2, con->v2.out_buf,
			       sizeof(struct ceph_timespec));
}

static int prepare_ack(struct ceph_connection *con)
{
	void *p;

	dout("%s con %p in_seq_acked %llu -> %llu\n", __func__, con,
	     con->in_seq_acked, con->in_seq);
	con->in_seq_acked = con->in_seq;

	p = CTRL_BODY(con->v2.out_buf);
	ceph_encode_64(&p, con->in_seq_acked);

	reset_out_kvecs(con);
	return prepare_control(con, FRAME_TAG_ACK, con->v2.out_buf, 8);
}

static void prepare_epilogue_plain(struct ceph_connection *con, bool aborted)
{
	dout("%s con %p msg %p aborted %d crcs %u %u %u\n", __func__, con,
	     con->out_msg, aborted, con->v2.out_epil.front_crc,
	     con->v2.out_epil.middle_crc, con->v2.out_epil.data_crc);

	encode_epilogue_plain(con, aborted);
	add_out_kvec(con, &con->v2.out_epil, CEPH_EPILOGUE_PLAIN_LEN);
}

/*
 * For "used" empty segments, crc is -1.  For unused (trailing)
 * segments, crc is 0.
 */
static void prepare_message_plain(struct ceph_connection *con)
{
	struct ceph_msg *msg = con->out_msg;

	prepare_head_plain(con, con->v2.out_buf,
			   sizeof(struct ceph_msg_header2), NULL, 0, false);

	if (!front_len(msg) && !middle_len(msg)) {
		if (!data_len(msg)) {
			/*
			 * Empty message: once the head is written,
			 * we are done -- there is no epilogue.
			 */
			con->v2.out_state = OUT_S_FINISH_MESSAGE;
			return;
		}

		con->v2.out_epil.front_crc = -1;
		con->v2.out_epil.middle_crc = -1;
		con->v2.out_state = OUT_S_QUEUE_DATA;
		return;
	}

	if (front_len(msg)) {
		con->v2.out_epil.front_crc = crc32c(-1, msg->front.iov_base,
						    front_len(msg));
		add_out_kvec(con, msg->front.iov_base, front_len(msg));
	} else {
		/* middle (at least) is there, checked above */
		con->v2.out_epil.front_crc = -1;
	}

	if (middle_len(msg)) {
		con->v2.out_epil.middle_crc =
			crc32c(-1, msg->middle->vec.iov_base, middle_len(msg));
		add_out_kvec(con, msg->middle->vec.iov_base, middle_len(msg));
	} else {
		con->v2.out_epil.middle_crc = data_len(msg) ? -1 : 0;
	}

	if (data_len(msg)) {
		con->v2.out_state = OUT_S_QUEUE_DATA;
	} else {
		con->v2.out_epil.data_crc = 0;
		prepare_epilogue_plain(con, false);
		con->v2.out_state = OUT_S_FINISH_MESSAGE;
	}
}

/*
 * Unfortunately the kernel crypto API doesn't support streaming
 * (piecewise) operation for AEAD algorithms, so we can't get away
 * with a fixed size buffer and a couple sgs.  Instead, we have to
 * allocate pages for the entire tail of the message (currently up
 * to ~32M) and two sgs arrays (up to ~256K each)...
 */
static int prepare_message_secure(struct ceph_connection *con)
{
	void *zerop = page_address(ceph_zero_page);
	struct sg_table enc_sgt = {};
	struct sg_table sgt = {};
	struct page **enc_pages;
	int enc_page_cnt;
	int tail_len;
	int ret;

	ret = prepare_head_secure_small(con, con->v2.out_buf,
					sizeof(struct ceph_msg_header2));
	if (ret)
		return ret;

	tail_len = tail_onwire_len(con->out_msg, true);
	if (!tail_len) {
		/*
		 * Empty message: once the head is written,
		 * we are done -- there is no epilogue.
		 */
		con->v2.out_state = OUT_S_FINISH_MESSAGE;
		return 0;
	}

	encode_epilogue_secure(con, false);
	ret = setup_message_sgs(&sgt, con->out_msg, zerop, zerop, zerop,
				&con->v2.out_epil, NULL, 0, false);
	if (ret)
		goto out;

	enc_page_cnt = calc_pages_for(0, tail_len);
	enc_pages = ceph_alloc_page_vector(enc_page_cnt, GFP_NOIO);
	if (IS_ERR(enc_pages)) {
		ret = PTR_ERR(enc_pages);
		goto out;
	}

	WARN_ON(con->v2.out_enc_pages || con->v2.out_enc_page_cnt);
	con->v2.out_enc_pages = enc_pages;
	con->v2.out_enc_page_cnt = enc_page_cnt;
	con->v2.out_enc_resid = tail_len;
	con->v2.out_enc_i = 0;

	ret = sg_alloc_table_from_pages(&enc_sgt, enc_pages, enc_page_cnt,
					0, tail_len, GFP_NOIO);
	if (ret)
		goto out;

	ret = gcm_crypt(con, true, sgt.sgl, enc_sgt.sgl,
			tail_len - CEPH_GCM_TAG_LEN);
	if (ret)
		goto out;

	dout("%s con %p msg %p sg_cnt %d enc_page_cnt %d\n", __func__, con,
	     con->out_msg, sgt.orig_nents, enc_page_cnt);
	con->v2.out_state = OUT_S_QUEUE_ENC_PAGE;

out:
	sg_free_table(&sgt);
	sg_free_table(&enc_sgt);
	return ret;
}

static int prepare_message(struct ceph_connection *con)
{
	int lens[] = {
		sizeof(struct ceph_msg_header2),
		front_len(con->out_msg),
		middle_len(con->out_msg),
		data_len(con->out_msg)
	};
	struct ceph_frame_desc desc;
	int ret;

	dout("%s con %p msg %p logical %d+%d+%d+%d\n", __func__, con,
	     con->out_msg, lens[0], lens[1], lens[2], lens[3]);

	if (con->in_seq > con->in_seq_acked) {
		dout("%s con %p in_seq_acked %llu -> %llu\n", __func__, con,
		     con->in_seq_acked, con->in_seq);
		con->in_seq_acked = con->in_seq;
	}

	reset_out_kvecs(con);
	init_frame_desc(&desc, FRAME_TAG_MESSAGE, lens, 4);
	encode_preamble(&desc, con->v2.out_buf);
	fill_header2(CTRL_BODY(con->v2.out_buf), &con->out_msg->hdr,
		     con->in_seq_acked);

	if (con_secure(con)) {
		ret = prepare_message_secure(con);
		if (ret)
			return ret;
	} else {
		prepare_message_plain(con);
	}

	ceph_con_flag_set(con, CEPH_CON_F_WRITE_PENDING);
	return 0;
}

static int prepare_read_banner_prefix(struct ceph_connection *con)
{
	void *buf;

	buf = alloc_conn_buf(con, CEPH_BANNER_V2_PREFIX_LEN);
	if (!buf)
		return -ENOMEM;

	reset_in_kvecs(con);
	add_in_kvec(con, buf, CEPH_BANNER_V2_PREFIX_LEN);
	add_in_sign_kvec(con, buf, CEPH_BANNER_V2_PREFIX_LEN);
	con->state = CEPH_CON_S_V2_BANNER_PREFIX;
	return 0;
}

static int prepare_read_banner_payload(struct ceph_connection *con,
				       int payload_len)
{
	void *buf;

	buf = alloc_conn_buf(con, payload_len);
	if (!buf)
		return -ENOMEM;

	reset_in_kvecs(con);
	add_in_kvec(con, buf, payload_len);
	add_in_sign_kvec(con, buf, payload_len);
	con->state = CEPH_CON_S_V2_BANNER_PAYLOAD;
	return 0;
}

static void prepare_read_preamble(struct ceph_connection *con)
{
	reset_in_kvecs(con);
	add_in_kvec(con, con->v2.in_buf,
		    con_secure(con) ? CEPH_PREAMBLE_SECURE_LEN :
				      CEPH_PREAMBLE_PLAIN_LEN);
	con->v2.in_state = IN_S_HANDLE_PREAMBLE;
}

static int prepare_read_control(struct ceph_connection *con)
{
	int ctrl_len = con->v2.in_desc.fd_lens[0];
	int head_len;
	void *buf;

	reset_in_kvecs(con);
	if (con->state == CEPH_CON_S_V2_HELLO ||
	    con->state == CEPH_CON_S_V2_AUTH) {
		head_len = head_onwire_len(ctrl_len, false);
		buf = alloc_conn_buf(con, head_len);
		if (!buf)
			return -ENOMEM;

		/* preserve preamble */
		memcpy(buf, con->v2.in_buf, CEPH_PREAMBLE_LEN);

		add_in_kvec(con, CTRL_BODY(buf), ctrl_len);
		add_in_kvec(con, CTRL_BODY(buf) + ctrl_len, CEPH_CRC_LEN);
		add_in_sign_kvec(con, buf, head_len);
	} else {
		if (ctrl_len > CEPH_PREAMBLE_INLINE_LEN) {
			buf = alloc_conn_buf(con, ctrl_len);
			if (!buf)
				return -ENOMEM;

			add_in_kvec(con, buf, ctrl_len);
		} else {
			add_in_kvec(con, CTRL_BODY(con->v2.in_buf), ctrl_len);
		}
		add_in_kvec(con, con->v2.in_buf, CEPH_CRC_LEN);
	}
	con->v2.in_state = IN_S_HANDLE_CONTROL;
	return 0;
}

static int prepare_read_control_remainder(struct ceph_connection *con)
{
	int ctrl_len = con->v2.in_desc.fd_lens[0];
	int rem_len = ctrl_len - CEPH_PREAMBLE_INLINE_LEN;
	void *buf;

	buf = alloc_conn_buf(con, ctrl_len);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, CTRL_BODY(con->v2.in_buf), CEPH_PREAMBLE_INLINE_LEN);

	reset_in_kvecs(con);
	add_in_kvec(con, buf + CEPH_PREAMBLE_INLINE_LEN, rem_len);
	add_in_kvec(con, con->v2.in_buf,
		    padding_len(rem_len) + CEPH_GCM_TAG_LEN);
	con->v2.in_state = IN_S_HANDLE_CONTROL_REMAINDER;
	return 0;
}

static int prepare_read_data(struct ceph_connection *con)
{
	struct bio_vec bv;

	con->in_data_crc = -1;
	ceph_msg_data_cursor_init(&con->v2.in_cursor, con->in_msg,
				  data_len(con->in_msg));

	get_bvec_at(&con->v2.in_cursor, &bv);
	if (ceph_test_opt(from_msgr(con->msgr), RXBOUNCE)) {
		if (unlikely(!con->bounce_page)) {
			con->bounce_page = alloc_page(GFP_NOIO);
			if (!con->bounce_page) {
				pr_err("failed to allocate bounce page\n");
				return -ENOMEM;
			}
		}

		bv.bv_page = con->bounce_page;
		bv.bv_offset = 0;
	}
	set_in_bvec(con, &bv);
	con->v2.in_state = IN_S_PREPARE_READ_DATA_CONT;
	return 0;
}

static void prepare_read_data_cont(struct ceph_connection *con)
{
	struct bio_vec bv;

	if (ceph_test_opt(from_msgr(con->msgr), RXBOUNCE)) {
		con->in_data_crc = crc32c(con->in_data_crc,
					  page_address(con->bounce_page),
					  con->v2.in_bvec.bv_len);

		get_bvec_at(&con->v2.in_cursor, &bv);
		memcpy_to_page(bv.bv_page, bv.bv_offset,
			       page_address(con->bounce_page),
			       con->v2.in_bvec.bv_len);
	} else {
		con->in_data_crc = ceph_crc32c_page(con->in_data_crc,
						    con->v2.in_bvec.bv_page,
						    con->v2.in_bvec.bv_offset,
						    con->v2.in_bvec.bv_len);
	}

	ceph_msg_data_advance(&con->v2.in_cursor, con->v2.in_bvec.bv_len);
	if (con->v2.in_cursor.total_resid) {
		get_bvec_at(&con->v2.in_cursor, &bv);
		if (ceph_test_opt(from_msgr(con->msgr), RXBOUNCE)) {
			bv.bv_page = con->bounce_page;
			bv.bv_offset = 0;
		}
		set_in_bvec(con, &bv);
		WARN_ON(con->v2.in_state != IN_S_PREPARE_READ_DATA_CONT);
		return;
	}

	/*
	 * We've read all data.  Prepare to read epilogue.
	 */
	reset_in_kvecs(con);
	add_in_kvec(con, con->v2.in_buf, CEPH_EPILOGUE_PLAIN_LEN);
	con->v2.in_state = IN_S_HANDLE_EPILOGUE;
}

static int prepare_sparse_read_cont(struct ceph_connection *con)
{
	int ret;
	struct bio_vec bv;
	char *buf = NULL;
	struct ceph_msg_data_cursor *cursor = &con->v2.in_cursor;

	WARN_ON(con->v2.in_state != IN_S_PREPARE_SPARSE_DATA_CONT);

	if (iov_iter_is_bvec(&con->v2.in_iter)) {
		if (ceph_test_opt(from_msgr(con->msgr), RXBOUNCE)) {
			con->in_data_crc = crc32c(con->in_data_crc,
						  page_address(con->bounce_page),
						  con->v2.in_bvec.bv_len);
			get_bvec_at(cursor, &bv);
			memcpy_to_page(bv.bv_page, bv.bv_offset,
				       page_address(con->bounce_page),
				       con->v2.in_bvec.bv_len);
		} else {
			con->in_data_crc = ceph_crc32c_page(con->in_data_crc,
							    con->v2.in_bvec.bv_page,
							    con->v2.in_bvec.bv_offset,
							    con->v2.in_bvec.bv_len);
		}

		ceph_msg_data_advance(cursor, con->v2.in_bvec.bv_len);
		cursor->sr_resid -= con->v2.in_bvec.bv_len;
		dout("%s: advance by 0x%x sr_resid 0x%x\n", __func__,
		     con->v2.in_bvec.bv_len, cursor->sr_resid);
		WARN_ON_ONCE(cursor->sr_resid > cursor->total_resid);
		if (cursor->sr_resid) {
			get_bvec_at(cursor, &bv);
			if (bv.bv_len > cursor->sr_resid)
				bv.bv_len = cursor->sr_resid;
			if (ceph_test_opt(from_msgr(con->msgr), RXBOUNCE)) {
				bv.bv_page = con->bounce_page;
				bv.bv_offset = 0;
			}
			set_in_bvec(con, &bv);
			con->v2.data_len_remain -= bv.bv_len;
			return 0;
		}
	} else if (iov_iter_is_kvec(&con->v2.in_iter)) {
		/* On first call, we have no kvec so don't compute crc */
		if (con->v2.in_kvec_cnt) {
			WARN_ON_ONCE(con->v2.in_kvec_cnt > 1);
			con->in_data_crc = crc32c(con->in_data_crc,
						  con->v2.in_kvecs[0].iov_base,
						  con->v2.in_kvecs[0].iov_len);
		}
	} else {
		return -EIO;
	}

	/* get next extent */
	ret = con->ops->sparse_read(con, cursor, &buf);
	if (ret <= 0) {
		if (ret < 0)
			return ret;

		reset_in_kvecs(con);
		add_in_kvec(con, con->v2.in_buf, CEPH_EPILOGUE_PLAIN_LEN);
		con->v2.in_state = IN_S_HANDLE_EPILOGUE;
		return 0;
	}

	if (buf) {
		/* receive into buffer */
		reset_in_kvecs(con);
		add_in_kvec(con, buf, ret);
		con->v2.data_len_remain -= ret;
		return 0;
	}

	if (ret > cursor->total_resid) {
		pr_warn("%s: ret 0x%x total_resid 0x%zx resid 0x%zx\n",
			__func__, ret, cursor->total_resid, cursor->resid);
		return -EIO;
	}
	get_bvec_at(cursor, &bv);
	if (bv.bv_len > cursor->sr_resid)
		bv.bv_len = cursor->sr_resid;
	if (ceph_test_opt(from_msgr(con->msgr), RXBOUNCE)) {
		if (unlikely(!con->bounce_page)) {
			con->bounce_page = alloc_page(GFP_NOIO);
			if (!con->bounce_page) {
				pr_err("failed to allocate bounce page\n");
				return -ENOMEM;
			}
		}

		bv.bv_page = con->bounce_page;
		bv.bv_offset = 0;
	}
	set_in_bvec(con, &bv);
	con->v2.data_len_remain -= ret;
	return ret;
}

static int prepare_sparse_read_data(struct ceph_connection *con)
{
	struct ceph_msg *msg = con->in_msg;

	dout("%s: starting sparse read\n", __func__);

	if (WARN_ON_ONCE(!con->ops->sparse_read))
		return -EOPNOTSUPP;

	if (!con_secure(con))
		con->in_data_crc = -1;

	reset_in_kvecs(con);
	con->v2.in_state = IN_S_PREPARE_SPARSE_DATA_CONT;
	con->v2.data_len_remain = data_len(msg);
	return prepare_sparse_read_cont(con);
}

static int prepare_read_tail_plain(struct ceph_connection *con)
{
	struct ceph_msg *msg = con->in_msg;

	if (!front_len(msg) && !middle_len(msg)) {
		WARN_ON(!data_len(msg));
		return prepare_read_data(con);
	}

	reset_in_kvecs(con);
	if (front_len(msg)) {
		add_in_kvec(con, msg->front.iov_base, front_len(msg));
		WARN_ON(msg->front.iov_len != front_len(msg));
	}
	if (middle_len(msg)) {
		add_in_kvec(con, msg->middle->vec.iov_base, middle_len(msg));
		WARN_ON(msg->middle->vec.iov_len != middle_len(msg));
	}

	if (data_len(msg)) {
		if (msg->sparse_read)
			con->v2.in_state = IN_S_PREPARE_SPARSE_DATA;
		else
			con->v2.in_state = IN_S_PREPARE_READ_DATA;
	} else {
		add_in_kvec(con, con->v2.in_buf, CEPH_EPILOGUE_PLAIN_LEN);
		con->v2.in_state = IN_S_HANDLE_EPILOGUE;
	}
	return 0;
}

static void prepare_read_enc_page(struct ceph_connection *con)
{
	struct bio_vec bv;

	dout("%s con %p i %d resid %d\n", __func__, con, con->v2.in_enc_i,
	     con->v2.in_enc_resid);
	WARN_ON(!con->v2.in_enc_resid);

	bvec_set_page(&bv, con->v2.in_enc_pages[con->v2.in_enc_i],
		      min(con->v2.in_enc_resid, (int)PAGE_SIZE), 0);

	set_in_bvec(con, &bv);
	con->v2.in_enc_i++;
	con->v2.in_enc_resid -= bv.bv_len;

	if (con->v2.in_enc_resid) {
		con->v2.in_state = IN_S_PREPARE_READ_ENC_PAGE;
		return;
	}

	/*
	 * We are set to read the last piece of ciphertext (ending
	 * with epilogue) + auth tag.
	 */
	WARN_ON(con->v2.in_enc_i != con->v2.in_enc_page_cnt);
	con->v2.in_state = IN_S_HANDLE_EPILOGUE;
}

static int prepare_read_tail_secure(struct ceph_connection *con)
{
	struct page **enc_pages;
	int enc_page_cnt;
	int tail_len;

	tail_len = tail_onwire_len(con->in_msg, true);
	WARN_ON(!tail_len);

	enc_page_cnt = calc_pages_for(0, tail_len);
	enc_pages = ceph_alloc_page_vector(enc_page_cnt, GFP_NOIO);
	if (IS_ERR(enc_pages))
		return PTR_ERR(enc_pages);

	WARN_ON(con->v2.in_enc_pages || con->v2.in_enc_page_cnt);
	con->v2.in_enc_pages = enc_pages;
	con->v2.in_enc_page_cnt = enc_page_cnt;
	con->v2.in_enc_resid = tail_len;
	con->v2.in_enc_i = 0;

	prepare_read_enc_page(con);
	return 0;
}

static void __finish_skip(struct ceph_connection *con)
{
	con->in_seq++;
	prepare_read_preamble(con);
}

static void prepare_skip_message(struct ceph_connection *con)
{
	struct ceph_frame_desc *desc = &con->v2.in_desc;
	int tail_len;

	dout("%s con %p %d+%d+%d\n", __func__, con, desc->fd_lens[1],
	     desc->fd_lens[2], desc->fd_lens[3]);

	tail_len = __tail_onwire_len(desc->fd_lens[1], desc->fd_lens[2],
				     desc->fd_lens[3], con_secure(con));
	if (!tail_len) {
		__finish_skip(con);
	} else {
		set_in_skip(con, tail_len);
		con->v2.in_state = IN_S_FINISH_SKIP;
	}
}

static int process_banner_prefix(struct ceph_connection *con)
{
	int payload_len;
	void *p;

	WARN_ON(con->v2.in_kvecs[0].iov_len != CEPH_BANNER_V2_PREFIX_LEN);

	p = con->v2.in_kvecs[0].iov_base;
	if (memcmp(p, CEPH_BANNER_V2, CEPH_BANNER_V2_LEN)) {
		if (!memcmp(p, CEPH_BANNER, CEPH_BANNER_LEN))
			con->error_msg = "server is speaking msgr1 protocol";
		else
			con->error_msg = "protocol error, bad banner";
		return -EINVAL;
	}

	p += CEPH_BANNER_V2_LEN;
	payload_len = ceph_decode_16(&p);
	dout("%s con %p payload_len %d\n", __func__, con, payload_len);

	return prepare_read_banner_payload(con, payload_len);
}

static int process_banner_payload(struct ceph_connection *con)
{
	void *end = con->v2.in_kvecs[0].iov_base + con->v2.in_kvecs[0].iov_len;
	u64 feat = CEPH_MSGR2_SUPPORTED_FEATURES;
	u64 req_feat = CEPH_MSGR2_REQUIRED_FEATURES;
	u64 server_feat, server_req_feat;
	void *p;
	int ret;

	p = con->v2.in_kvecs[0].iov_base;
	ceph_decode_64_safe(&p, end, server_feat, bad);
	ceph_decode_64_safe(&p, end, server_req_feat, bad);

	dout("%s con %p server_feat 0x%llx server_req_feat 0x%llx\n",
	     __func__, con, server_feat, server_req_feat);

	if (req_feat & ~server_feat) {
		pr_err("msgr2 feature set mismatch: my required > server's supported 0x%llx, need 0x%llx\n",
		       server_feat, req_feat & ~server_feat);
		con->error_msg = "missing required protocol features";
		return -EINVAL;
	}
	if (server_req_feat & ~feat) {
		pr_err("msgr2 feature set mismatch: server's required > my supported 0x%llx, missing 0x%llx\n",
		       feat, server_req_feat & ~feat);
		con->error_msg = "missing required protocol features";
		return -EINVAL;
	}

	/* no reset_out_kvecs() as our banner may still be pending */
	ret = prepare_hello(con);
	if (ret) {
		pr_err("prepare_hello failed: %d\n", ret);
		return ret;
	}

	con->state = CEPH_CON_S_V2_HELLO;
	prepare_read_preamble(con);
	return 0;

bad:
	pr_err("failed to decode banner payload\n");
	return -EINVAL;
}

static int process_hello(struct ceph_connection *con, void *p, void *end)
{
	struct ceph_entity_addr *my_addr = &con->msgr->inst.addr;
	struct ceph_entity_addr addr_for_me;
	u8 entity_type;
	int ret;

	if (con->state != CEPH_CON_S_V2_HELLO) {
		con->error_msg = "protocol error, unexpected hello";
		return -EINVAL;
	}

	ceph_decode_8_safe(&p, end, entity_type, bad);
	ret = ceph_decode_entity_addr(&p, end, &addr_for_me);
	if (ret) {
		pr_err("failed to decode addr_for_me: %d\n", ret);
		return ret;
	}

	dout("%s con %p entity_type %d addr_for_me %s\n", __func__, con,
	     entity_type, ceph_pr_addr(&addr_for_me));

	if (entity_type != con->peer_name.type) {
		pr_err("bad peer type, want %d, got %d\n",
		       con->peer_name.type, entity_type);
		con->error_msg = "wrong peer at address";
		return -EINVAL;
	}

	/*
	 * Set our address to the address our first peer (i.e. monitor)
	 * sees that we are connecting from.  If we are behind some sort
	 * of NAT and want to be identified by some private (not NATed)
	 * address, ip option should be used.
	 */
	if (ceph_addr_is_blank(my_addr)) {
		memcpy(&my_addr->in_addr, &addr_for_me.in_addr,
		       sizeof(my_addr->in_addr));
		ceph_addr_set_port(my_addr, 0);
		dout("%s con %p set my addr %s, as seen by peer %s\n",
		     __func__, con, ceph_pr_addr(my_addr),
		     ceph_pr_addr(&con->peer_addr));
	} else {
		dout("%s con %p my addr already set %s\n",
		     __func__, con, ceph_pr_addr(my_addr));
	}

	WARN_ON(ceph_addr_is_blank(my_addr) || ceph_addr_port(my_addr));
	WARN_ON(my_addr->type != CEPH_ENTITY_ADDR_TYPE_ANY);
	WARN_ON(!my_addr->nonce);

	/* no reset_out_kvecs() as our hello may still be pending */
	ret = prepare_auth_request(con);
	if (ret) {
		if (ret != -EAGAIN)
			pr_err("prepare_auth_request failed: %d\n", ret);
		return ret;
	}

	con->state = CEPH_CON_S_V2_AUTH;
	return 0;

bad:
	pr_err("failed to decode hello\n");
	return -EINVAL;
}

static int process_auth_bad_method(struct ceph_connection *con,
				   void *p, void *end)
{
	int allowed_protos[8], allowed_modes[8];
	int allowed_proto_cnt, allowed_mode_cnt;
	int used_proto, result;
	int ret;
	int i;

	if (con->state != CEPH_CON_S_V2_AUTH) {
		con->error_msg = "protocol error, unexpected auth_bad_method";
		return -EINVAL;
	}

	ceph_decode_32_safe(&p, end, used_proto, bad);
	ceph_decode_32_safe(&p, end, result, bad);
	dout("%s con %p used_proto %d result %d\n", __func__, con, used_proto,
	     result);

	ceph_decode_32_safe(&p, end, allowed_proto_cnt, bad);
	if (allowed_proto_cnt > ARRAY_SIZE(allowed_protos)) {
		pr_err("allowed_protos too big %d\n", allowed_proto_cnt);
		return -EINVAL;
	}
	for (i = 0; i < allowed_proto_cnt; i++) {
		ceph_decode_32_safe(&p, end, allowed_protos[i], bad);
		dout("%s con %p allowed_protos[%d] %d\n", __func__, con,
		     i, allowed_protos[i]);
	}

	ceph_decode_32_safe(&p, end, allowed_mode_cnt, bad);
	if (allowed_mode_cnt > ARRAY_SIZE(allowed_modes)) {
		pr_err("allowed_modes too big %d\n", allowed_mode_cnt);
		return -EINVAL;
	}
	for (i = 0; i < allowed_mode_cnt; i++) {
		ceph_decode_32_safe(&p, end, allowed_modes[i], bad);
		dout("%s con %p allowed_modes[%d] %d\n", __func__, con,
		     i, allowed_modes[i]);
	}

	mutex_unlock(&con->mutex);
	ret = con->ops->handle_auth_bad_method(con, used_proto, result,
					       allowed_protos,
					       allowed_proto_cnt,
					       allowed_modes,
					       allowed_mode_cnt);
	mutex_lock(&con->mutex);
	if (con->state != CEPH_CON_S_V2_AUTH) {
		dout("%s con %p state changed to %d\n", __func__, con,
		     con->state);
		return -EAGAIN;
	}

	dout("%s con %p handle_auth_bad_method ret %d\n", __func__, con, ret);
	return ret;

bad:
	pr_err("failed to decode auth_bad_method\n");
	return -EINVAL;
}

static int process_auth_reply_more(struct ceph_connection *con,
				   void *p, void *end)
{
	int payload_len;
	int ret;

	if (con->state != CEPH_CON_S_V2_AUTH) {
		con->error_msg = "protocol error, unexpected auth_reply_more";
		return -EINVAL;
	}

	ceph_decode_32_safe(&p, end, payload_len, bad);
	ceph_decode_need(&p, end, payload_len, bad);

	dout("%s con %p payload_len %d\n", __func__, con, payload_len);

	reset_out_kvecs(con);
	ret = prepare_auth_request_more(con, p, payload_len);
	if (ret) {
		if (ret != -EAGAIN)
			pr_err("prepare_auth_request_more failed: %d\n", ret);
		return ret;
	}

	return 0;

bad:
	pr_err("failed to decode auth_reply_more\n");
	return -EINVAL;
}

/*
 * Align session_key and con_secret to avoid GFP_ATOMIC allocation
 * inside crypto_shash_setkey() and crypto_aead_setkey() called from
 * setup_crypto().  __aligned(16) isn't guaranteed to work for stack
 * objects, so do it by hand.
 */
static int process_auth_done(struct ceph_connection *con, void *p, void *end)
{
	u8 session_key_buf[CEPH_KEY_LEN + 16];
	u8 con_secret_buf[CEPH_MAX_CON_SECRET_LEN + 16];
	u8 *session_key = PTR_ALIGN(&session_key_buf[0], 16);
	u8 *con_secret = PTR_ALIGN(&con_secret_buf[0], 16);
	int session_key_len, con_secret_len;
	int payload_len;
	u64 global_id;
	int ret;

	if (con->state != CEPH_CON_S_V2_AUTH) {
		con->error_msg = "protocol error, unexpected auth_done";
		return -EINVAL;
	}

	ceph_decode_64_safe(&p, end, global_id, bad);
	ceph_decode_32_safe(&p, end, con->v2.con_mode, bad);
	ceph_decode_32_safe(&p, end, payload_len, bad);

	dout("%s con %p global_id %llu con_mode %d payload_len %d\n",
	     __func__, con, global_id, con->v2.con_mode, payload_len);

	mutex_unlock(&con->mutex);
	session_key_len = 0;
	con_secret_len = 0;
	ret = con->ops->handle_auth_done(con, global_id, p, payload_len,
					 session_key, &session_key_len,
					 con_secret, &con_secret_len);
	mutex_lock(&con->mutex);
	if (con->state != CEPH_CON_S_V2_AUTH) {
		dout("%s con %p state changed to %d\n", __func__, con,
		     con->state);
		ret = -EAGAIN;
		goto out;
	}

	dout("%s con %p handle_auth_done ret %d\n", __func__, con, ret);
	if (ret)
		goto out;

	ret = setup_crypto(con, session_key, session_key_len, con_secret,
			   con_secret_len);
	if (ret)
		goto out;

	reset_out_kvecs(con);
	ret = prepare_auth_signature(con);
	if (ret) {
		pr_err("prepare_auth_signature failed: %d\n", ret);
		goto out;
	}

	con->state = CEPH_CON_S_V2_AUTH_SIGNATURE;

out:
	memzero_explicit(session_key_buf, sizeof(session_key_buf));
	memzero_explicit(con_secret_buf, sizeof(con_secret_buf));
	return ret;

bad:
	pr_err("failed to decode auth_done\n");
	return -EINVAL;
}

static int process_auth_signature(struct ceph_connection *con,
				  void *p, void *end)
{
	u8 hmac[SHA256_DIGEST_SIZE];
	int ret;

	if (con->state != CEPH_CON_S_V2_AUTH_SIGNATURE) {
		con->error_msg = "protocol error, unexpected auth_signature";
		return -EINVAL;
	}

	ret = hmac_sha256(con, con->v2.out_sign_kvecs,
			  con->v2.out_sign_kvec_cnt, hmac);
	if (ret)
		return ret;

	ceph_decode_need(&p, end, SHA256_DIGEST_SIZE, bad);
	if (crypto_memneq(p, hmac, SHA256_DIGEST_SIZE)) {
		con->error_msg = "integrity error, bad auth signature";
		return -EBADMSG;
	}

	dout("%s con %p auth signature ok\n", __func__, con);

	/* no reset_out_kvecs() as our auth_signature may still be pending */
	if (!con->v2.server_cookie) {
		ret = prepare_client_ident(con);
		if (ret) {
			pr_err("prepare_client_ident failed: %d\n", ret);
			return ret;
		}

		con->state = CEPH_CON_S_V2_SESSION_CONNECT;
	} else {
		ret = prepare_session_reconnect(con);
		if (ret) {
			pr_err("prepare_session_reconnect failed: %d\n", ret);
			return ret;
		}

		con->state = CEPH_CON_S_V2_SESSION_RECONNECT;
	}

	return 0;

bad:
	pr_err("failed to decode auth_signature\n");
	return -EINVAL;
}

static int process_server_ident(struct ceph_connection *con,
				void *p, void *end)
{
	struct ceph_client *client = from_msgr(con->msgr);
	u64 features, required_features;
	struct ceph_entity_addr addr;
	u64 global_seq;
	u64 global_id;
	u64 cookie;
	u64 flags;
	int ret;

	if (con->state != CEPH_CON_S_V2_SESSION_CONNECT) {
		con->error_msg = "protocol error, unexpected server_ident";
		return -EINVAL;
	}

	ret = ceph_decode_entity_addrvec(&p, end, true, &addr);
	if (ret) {
		pr_err("failed to decode server addrs: %d\n", ret);
		return ret;
	}

	ceph_decode_64_safe(&p, end, global_id, bad);
	ceph_decode_64_safe(&p, end, global_seq, bad);
	ceph_decode_64_safe(&p, end, features, bad);
	ceph_decode_64_safe(&p, end, required_features, bad);
	ceph_decode_64_safe(&p, end, flags, bad);
	ceph_decode_64_safe(&p, end, cookie, bad);

	dout("%s con %p addr %s/%u global_id %llu global_seq %llu features 0x%llx required_features 0x%llx flags 0x%llx cookie 0x%llx\n",
	     __func__, con, ceph_pr_addr(&addr), le32_to_cpu(addr.nonce),
	     global_id, global_seq, features, required_features, flags, cookie);

	/* is this who we intended to talk to? */
	if (memcmp(&addr, &con->peer_addr, sizeof(con->peer_addr))) {
		pr_err("bad peer addr/nonce, want %s/%u, got %s/%u\n",
		       ceph_pr_addr(&con->peer_addr),
		       le32_to_cpu(con->peer_addr.nonce),
		       ceph_pr_addr(&addr), le32_to_cpu(addr.nonce));
		con->error_msg = "wrong peer at address";
		return -EINVAL;
	}

	if (client->required_features & ~features) {
		pr_err("RADOS feature set mismatch: my required > server's supported 0x%llx, need 0x%llx\n",
		       features, client->required_features & ~features);
		con->error_msg = "missing required protocol features";
		return -EINVAL;
	}

	/*
	 * Both name->type and name->num are set in ceph_con_open() but
	 * name->num may be bogus in the initial monmap.  name->type is
	 * verified in handle_hello().
	 */
	WARN_ON(!con->peer_name.type);
	con->peer_name.num = cpu_to_le64(global_id);
	con->v2.peer_global_seq = global_seq;
	con->peer_features = features;
	WARN_ON(required_features & ~client->supported_features);
	con->v2.server_cookie = cookie;

	if (flags & CEPH_MSG_CONNECT_LOSSY) {
		ceph_con_flag_set(con, CEPH_CON_F_LOSSYTX);
		WARN_ON(con->v2.server_cookie);
	} else {
		WARN_ON(!con->v2.server_cookie);
	}

	clear_in_sign_kvecs(con);
	clear_out_sign_kvecs(con);
	free_conn_bufs(con);
	con->delay = 0;  /* reset backoff memory */

	con->state = CEPH_CON_S_OPEN;
	con->v2.out_state = OUT_S_GET_NEXT;
	return 0;

bad:
	pr_err("failed to decode server_ident\n");
	return -EINVAL;
}

static int process_ident_missing_features(struct ceph_connection *con,
					  void *p, void *end)
{
	struct ceph_client *client = from_msgr(con->msgr);
	u64 missing_features;

	if (con->state != CEPH_CON_S_V2_SESSION_CONNECT) {
		con->error_msg = "protocol error, unexpected ident_missing_features";
		return -EINVAL;
	}

	ceph_decode_64_safe(&p, end, missing_features, bad);
	pr_err("RADOS feature set mismatch: server's required > my supported 0x%llx, missing 0x%llx\n",
	       client->supported_features, missing_features);
	con->error_msg = "missing required protocol features";
	return -EINVAL;

bad:
	pr_err("failed to decode ident_missing_features\n");
	return -EINVAL;
}

static int process_session_reconnect_ok(struct ceph_connection *con,
					void *p, void *end)
{
	u64 seq;

	if (con->state != CEPH_CON_S_V2_SESSION_RECONNECT) {
		con->error_msg = "protocol error, unexpected session_reconnect_ok";
		return -EINVAL;
	}

	ceph_decode_64_safe(&p, end, seq, bad);

	dout("%s con %p seq %llu\n", __func__, con, seq);
	ceph_con_discard_requeued(con, seq);

	clear_in_sign_kvecs(con);
	clear_out_sign_kvecs(con);
	free_conn_bufs(con);
	con->delay = 0;  /* reset backoff memory */

	con->state = CEPH_CON_S_OPEN;
	con->v2.out_state = OUT_S_GET_NEXT;
	return 0;

bad:
	pr_err("failed to decode session_reconnect_ok\n");
	return -EINVAL;
}

static int process_session_retry(struct ceph_connection *con,
				 void *p, void *end)
{
	u64 connect_seq;
	int ret;

	if (con->state != CEPH_CON_S_V2_SESSION_RECONNECT) {
		con->error_msg = "protocol error, unexpected session_retry";
		return -EINVAL;
	}

	ceph_decode_64_safe(&p, end, connect_seq, bad);

	dout("%s con %p connect_seq %llu\n", __func__, con, connect_seq);
	WARN_ON(connect_seq <= con->v2.connect_seq);
	con->v2.connect_seq = connect_seq + 1;

	free_conn_bufs(con);

	reset_out_kvecs(con);
	ret = prepare_session_reconnect(con);
	if (ret) {
		pr_err("prepare_session_reconnect (cseq) failed: %d\n", ret);
		return ret;
	}

	return 0;

bad:
	pr_err("failed to decode session_retry\n");
	return -EINVAL;
}

static int process_session_retry_global(struct ceph_connection *con,
					void *p, void *end)
{
	u64 global_seq;
	int ret;

	if (con->state != CEPH_CON_S_V2_SESSION_RECONNECT) {
		con->error_msg = "protocol error, unexpected session_retry_global";
		return -EINVAL;
	}

	ceph_decode_64_safe(&p, end, global_seq, bad);

	dout("%s con %p global_seq %llu\n", __func__, con, global_seq);
	WARN_ON(global_seq <= con->v2.global_seq);
	con->v2.global_seq = ceph_get_global_seq(con->msgr, global_seq);

	free_conn_bufs(con);

	reset_out_kvecs(con);
	ret = prepare_session_reconnect(con);
	if (ret) {
		pr_err("prepare_session_reconnect (gseq) failed: %d\n", ret);
		return ret;
	}

	return 0;

bad:
	pr_err("failed to decode session_retry_global\n");
	return -EINVAL;
}

static int process_session_reset(struct ceph_connection *con,
				 void *p, void *end)
{
	bool full;
	int ret;

	if (con->state != CEPH_CON_S_V2_SESSION_RECONNECT) {
		con->error_msg = "protocol error, unexpected session_reset";
		return -EINVAL;
	}

	ceph_decode_8_safe(&p, end, full, bad);
	if (!full) {
		con->error_msg = "protocol error, bad session_reset";
		return -EINVAL;
	}

	pr_info("%s%lld %s session reset\n", ENTITY_NAME(con->peer_name),
		ceph_pr_addr(&con->peer_addr));
	ceph_con_reset_session(con);

	mutex_unlock(&con->mutex);
	if (con->ops->peer_reset)
		con->ops->peer_reset(con);
	mutex_lock(&con->mutex);
	if (con->state != CEPH_CON_S_V2_SESSION_RECONNECT) {
		dout("%s con %p state changed to %d\n", __func__, con,
		     con->state);
		return -EAGAIN;
	}

	free_conn_bufs(con);

	reset_out_kvecs(con);
	ret = prepare_client_ident(con);
	if (ret) {
		pr_err("prepare_client_ident (rst) failed: %d\n", ret);
		return ret;
	}

	con->state = CEPH_CON_S_V2_SESSION_CONNECT;
	return 0;

bad:
	pr_err("failed to decode session_reset\n");
	return -EINVAL;
}

static int process_keepalive2_ack(struct ceph_connection *con,
				  void *p, void *end)
{
	if (con->state != CEPH_CON_S_OPEN) {
		con->error_msg = "protocol error, unexpected keepalive2_ack";
		return -EINVAL;
	}

	ceph_decode_need(&p, end, sizeof(struct ceph_timespec), bad);
	ceph_decode_timespec64(&con->last_keepalive_ack, p);

	dout("%s con %p timestamp %lld.%09ld\n", __func__, con,
	     con->last_keepalive_ack.tv_sec, con->last_keepalive_ack.tv_nsec);

	return 0;

bad:
	pr_err("failed to decode keepalive2_ack\n");
	return -EINVAL;
}

static int process_ack(struct ceph_connection *con, void *p, void *end)
{
	u64 seq;

	if (con->state != CEPH_CON_S_OPEN) {
		con->error_msg = "protocol error, unexpected ack";
		return -EINVAL;
	}

	ceph_decode_64_safe(&p, end, seq, bad);

	dout("%s con %p seq %llu\n", __func__, con, seq);
	ceph_con_discard_sent(con, seq);
	return 0;

bad:
	pr_err("failed to decode ack\n");
	return -EINVAL;
}

static int process_control(struct ceph_connection *con, void *p, void *end)
{
	int tag = con->v2.in_desc.fd_tag;
	int ret;

	dout("%s con %p tag %d len %d\n", __func__, con, tag, (int)(end - p));

	switch (tag) {
	case FRAME_TAG_HELLO:
		ret = process_hello(con, p, end);
		break;
	case FRAME_TAG_AUTH_BAD_METHOD:
		ret = process_auth_bad_method(con, p, end);
		break;
	case FRAME_TAG_AUTH_REPLY_MORE:
		ret = process_auth_reply_more(con, p, end);
		break;
	case FRAME_TAG_AUTH_DONE:
		ret = process_auth_done(con, p, end);
		break;
	case FRAME_TAG_AUTH_SIGNATURE:
		ret = process_auth_signature(con, p, end);
		break;
	case FRAME_TAG_SERVER_IDENT:
		ret = process_server_ident(con, p, end);
		break;
	case FRAME_TAG_IDENT_MISSING_FEATURES:
		ret = process_ident_missing_features(con, p, end);
		break;
	case FRAME_TAG_SESSION_RECONNECT_OK:
		ret = process_session_reconnect_ok(con, p, end);
		break;
	case FRAME_TAG_SESSION_RETRY:
		ret = process_session_retry(con, p, end);
		break;
	case FRAME_TAG_SESSION_RETRY_GLOBAL:
		ret = process_session_retry_global(con, p, end);
		break;
	case FRAME_TAG_SESSION_RESET:
		ret = process_session_reset(con, p, end);
		break;
	case FRAME_TAG_KEEPALIVE2_ACK:
		ret = process_keepalive2_ack(con, p, end);
		break;
	case FRAME_TAG_ACK:
		ret = process_ack(con, p, end);
		break;
	default:
		pr_err("bad tag %d\n", tag);
		con->error_msg = "protocol error, bad tag";
		return -EINVAL;
	}
	if (ret) {
		dout("%s con %p error %d\n", __func__, con, ret);
		return ret;
	}

	prepare_read_preamble(con);
	return 0;
}

/*
 * Return:
 *   1 - con->in_msg set, read message
 *   0 - skip message
 *  <0 - error
 */
static int process_message_header(struct ceph_connection *con,
				  void *p, void *end)
{
	struct ceph_frame_desc *desc = &con->v2.in_desc;
	struct ceph_msg_header2 *hdr2 = p;
	struct ceph_msg_header hdr;
	int skip;
	int ret;
	u64 seq;

	/* verify seq# */
	seq = le64_to_cpu(hdr2->seq);
	if ((s64)seq - (s64)con->in_seq < 1) {
		pr_info("%s%lld %s skipping old message: seq %llu, expected %llu\n",
			ENTITY_NAME(con->peer_name),
			ceph_pr_addr(&con->peer_addr),
			seq, con->in_seq + 1);
		return 0;
	}
	if ((s64)seq - (s64)con->in_seq > 1) {
		pr_err("bad seq %llu, expected %llu\n", seq, con->in_seq + 1);
		con->error_msg = "bad message sequence # for incoming message";
		return -EBADE;
	}

	ceph_con_discard_sent(con, le64_to_cpu(hdr2->ack_seq));

	fill_header(&hdr, hdr2, desc->fd_lens[1], desc->fd_lens[2],
		    desc->fd_lens[3], &con->peer_name);
	ret = ceph_con_in_msg_alloc(con, &hdr, &skip);
	if (ret)
		return ret;

	WARN_ON(!con->in_msg ^ skip);
	if (skip)
		return 0;

	WARN_ON(!con->in_msg);
	WARN_ON(con->in_msg->con != con);
	return 1;
}

static int process_message(struct ceph_connection *con)
{
	ceph_con_process_message(con);

	/*
	 * We could have been closed by ceph_con_close() because
	 * ceph_con_process_message() temporarily drops con->mutex.
	 */
	if (con->state != CEPH_CON_S_OPEN) {
		dout("%s con %p state changed to %d\n", __func__, con,
		     con->state);
		return -EAGAIN;
	}

	prepare_read_preamble(con);
	return 0;
}

static int __handle_control(struct ceph_connection *con, void *p)
{
	void *end = p + con->v2.in_desc.fd_lens[0];
	struct ceph_msg *msg;
	int ret;

	if (con->v2.in_desc.fd_tag != FRAME_TAG_MESSAGE)
		return process_control(con, p, end);

	ret = process_message_header(con, p, end);
	if (ret < 0)
		return ret;
	if (ret == 0) {
		prepare_skip_message(con);
		return 0;
	}

	msg = con->in_msg;  /* set in process_message_header() */
	if (front_len(msg)) {
		WARN_ON(front_len(msg) > msg->front_alloc_len);
		msg->front.iov_len = front_len(msg);
	} else {
		msg->front.iov_len = 0;
	}
	if (middle_len(msg)) {
		WARN_ON(middle_len(msg) > msg->middle->alloc_len);
		msg->middle->vec.iov_len = middle_len(msg);
	} else if (msg->middle) {
		msg->middle->vec.iov_len = 0;
	}

	if (!front_len(msg) && !middle_len(msg) && !data_len(msg))
		return process_message(con);

	if (con_secure(con))
		return prepare_read_tail_secure(con);

	return prepare_read_tail_plain(con);
}

static int handle_preamble(struct ceph_connection *con)
{
	struct ceph_frame_desc *desc = &con->v2.in_desc;
	int ret;

	if (con_secure(con)) {
		ret = decrypt_preamble(con);
		if (ret) {
			if (ret == -EBADMSG)
				con->error_msg = "integrity error, bad preamble auth tag";
			return ret;
		}
	}

	ret = decode_preamble(con->v2.in_buf, desc);
	if (ret) {
		if (ret == -EBADMSG)
			con->error_msg = "integrity error, bad crc";
		else
			con->error_msg = "protocol error, bad preamble";
		return ret;
	}

	dout("%s con %p tag %d seg_cnt %d %d+%d+%d+%d\n", __func__,
	     con, desc->fd_tag, desc->fd_seg_cnt, desc->fd_lens[0],
	     desc->fd_lens[1], desc->fd_lens[2], desc->fd_lens[3]);

	if (!con_secure(con))
		return prepare_read_control(con);

	if (desc->fd_lens[0] > CEPH_PREAMBLE_INLINE_LEN)
		return prepare_read_control_remainder(con);

	return __handle_control(con, CTRL_BODY(con->v2.in_buf));
}

static int handle_control(struct ceph_connection *con)
{
	int ctrl_len = con->v2.in_desc.fd_lens[0];
	void *buf;
	int ret;

	WARN_ON(con_secure(con));

	ret = verify_control_crc(con);
	if (ret) {
		con->error_msg = "integrity error, bad crc";
		return ret;
	}

	if (con->state == CEPH_CON_S_V2_AUTH) {
		buf = alloc_conn_buf(con, ctrl_len);
		if (!buf)
			return -ENOMEM;

		memcpy(buf, con->v2.in_kvecs[0].iov_base, ctrl_len);
		return __handle_control(con, buf);
	}

	return __handle_control(con, con->v2.in_kvecs[0].iov_base);
}

static int handle_control_remainder(struct ceph_connection *con)
{
	int ret;

	WARN_ON(!con_secure(con));

	ret = decrypt_control_remainder(con);
	if (ret) {
		if (ret == -EBADMSG)
			con->error_msg = "integrity error, bad control remainder auth tag";
		return ret;
	}

	return __handle_control(con, con->v2.in_kvecs[0].iov_base -
				     CEPH_PREAMBLE_INLINE_LEN);
}

static int handle_epilogue(struct ceph_connection *con)
{
	u32 front_crc, middle_crc, data_crc;
	int ret;

	if (con_secure(con)) {
		ret = decrypt_tail(con);
		if (ret) {
			if (ret == -EBADMSG)
				con->error_msg = "integrity error, bad epilogue auth tag";
			return ret;
		}

		/* just late_status */
		ret = decode_epilogue(con->v2.in_buf, NULL, NULL, NULL);
		if (ret) {
			con->error_msg = "protocol error, bad epilogue";
			return ret;
		}
	} else {
		ret = decode_epilogue(con->v2.in_buf, &front_crc,
				      &middle_crc, &data_crc);
		if (ret) {
			con->error_msg = "protocol error, bad epilogue";
			return ret;
		}

		ret = verify_epilogue_crcs(con, front_crc, middle_crc,
					   data_crc);
		if (ret) {
			con->error_msg = "integrity error, bad crc";
			return ret;
		}
	}

	return process_message(con);
}

static void finish_skip(struct ceph_connection *con)
{
	dout("%s con %p\n", __func__, con);

	if (con_secure(con))
		gcm_inc_nonce(&con->v2.in_gcm_nonce);

	__finish_skip(con);
}

static int populate_in_iter(struct ceph_connection *con)
{
	int ret;

	dout("%s con %p state %d in_state %d\n", __func__, con, con->state,
	     con->v2.in_state);
	WARN_ON(iov_iter_count(&con->v2.in_iter));

	if (con->state == CEPH_CON_S_V2_BANNER_PREFIX) {
		ret = process_banner_prefix(con);
	} else if (con->state == CEPH_CON_S_V2_BANNER_PAYLOAD) {
		ret = process_banner_payload(con);
	} else if ((con->state >= CEPH_CON_S_V2_HELLO &&
		    con->state <= CEPH_CON_S_V2_SESSION_RECONNECT) ||
		   con->state == CEPH_CON_S_OPEN) {
		switch (con->v2.in_state) {
		case IN_S_HANDLE_PREAMBLE:
			ret = handle_preamble(con);
			break;
		case IN_S_HANDLE_CONTROL:
			ret = handle_control(con);
			break;
		case IN_S_HANDLE_CONTROL_REMAINDER:
			ret = handle_control_remainder(con);
			break;
		case IN_S_PREPARE_READ_DATA:
			ret = prepare_read_data(con);
			break;
		case IN_S_PREPARE_READ_DATA_CONT:
			prepare_read_data_cont(con);
			ret = 0;
			break;
		case IN_S_PREPARE_READ_ENC_PAGE:
			prepare_read_enc_page(con);
			ret = 0;
			break;
		case IN_S_PREPARE_SPARSE_DATA:
			ret = prepare_sparse_read_data(con);
			break;
		case IN_S_PREPARE_SPARSE_DATA_CONT:
			ret = prepare_sparse_read_cont(con);
			break;
		case IN_S_HANDLE_EPILOGUE:
			ret = handle_epilogue(con);
			break;
		case IN_S_FINISH_SKIP:
			finish_skip(con);
			ret = 0;
			break;
		default:
			WARN(1, "bad in_state %d", con->v2.in_state);
			return -EINVAL;
		}
	} else {
		WARN(1, "bad state %d", con->state);
		return -EINVAL;
	}
	if (ret) {
		dout("%s con %p error %d\n", __func__, con, ret);
		return ret;
	}

	if (WARN_ON(!iov_iter_count(&con->v2.in_iter)))
		return -ENODATA;
	dout("%s con %p populated %zu\n", __func__, con,
	     iov_iter_count(&con->v2.in_iter));
	return 1;
}

int ceph_con_v2_try_read(struct ceph_connection *con)
{
	int ret;

	dout("%s con %p state %d need %zu\n", __func__, con, con->state,
	     iov_iter_count(&con->v2.in_iter));

	if (con->state == CEPH_CON_S_PREOPEN)
		return 0;

	/*
	 * We should always have something pending here.  If not,
	 * avoid calling populate_in_iter() as if we read something
	 * (ceph_tcp_recv() would immediately return 1).
	 */
	if (WARN_ON(!iov_iter_count(&con->v2.in_iter)))
		return -ENODATA;

	for (;;) {
		ret = ceph_tcp_recv(con);
		if (ret <= 0)
			return ret;

		ret = populate_in_iter(con);
		if (ret <= 0) {
			if (ret && ret != -EAGAIN && !con->error_msg)
				con->error_msg = "read processing error";
			return ret;
		}
	}
}

static void queue_data(struct ceph_connection *con)
{
	struct bio_vec bv;

	con->v2.out_epil.data_crc = -1;
	ceph_msg_data_cursor_init(&con->v2.out_cursor, con->out_msg,
				  data_len(con->out_msg));

	get_bvec_at(&con->v2.out_cursor, &bv);
	set_out_bvec(con, &bv, true);
	con->v2.out_state = OUT_S_QUEUE_DATA_CONT;
}

static void queue_data_cont(struct ceph_connection *con)
{
	struct bio_vec bv;

	con->v2.out_epil.data_crc = ceph_crc32c_page(
		con->v2.out_epil.data_crc, con->v2.out_bvec.bv_page,
		con->v2.out_bvec.bv_offset, con->v2.out_bvec.bv_len);

	ceph_msg_data_advance(&con->v2.out_cursor, con->v2.out_bvec.bv_len);
	if (con->v2.out_cursor.total_resid) {
		get_bvec_at(&con->v2.out_cursor, &bv);
		set_out_bvec(con, &bv, true);
		WARN_ON(con->v2.out_state != OUT_S_QUEUE_DATA_CONT);
		return;
	}

	/*
	 * We've written all data.  Queue epilogue.  Once it's written,
	 * we are done.
	 */
	reset_out_kvecs(con);
	prepare_epilogue_plain(con, false);
	con->v2.out_state = OUT_S_FINISH_MESSAGE;
}

static void queue_enc_page(struct ceph_connection *con)
{
	struct bio_vec bv;

	dout("%s con %p i %d resid %d\n", __func__, con, con->v2.out_enc_i,
	     con->v2.out_enc_resid);
	WARN_ON(!con->v2.out_enc_resid);

	bvec_set_page(&bv, con->v2.out_enc_pages[con->v2.out_enc_i],
		      min(con->v2.out_enc_resid, (int)PAGE_SIZE), 0);

	set_out_bvec(con, &bv, false);
	con->v2.out_enc_i++;
	con->v2.out_enc_resid -= bv.bv_len;

	if (con->v2.out_enc_resid) {
		WARN_ON(con->v2.out_state != OUT_S_QUEUE_ENC_PAGE);
		return;
	}

	/*
	 * We've queued the last piece of ciphertext (ending with
	 * epilogue) + auth tag.  Once it's written, we are done.
	 */
	WARN_ON(con->v2.out_enc_i != con->v2.out_enc_page_cnt);
	con->v2.out_state = OUT_S_FINISH_MESSAGE;
}

static void queue_zeros(struct ceph_connection *con)
{
	dout("%s con %p out_zero %d\n", __func__, con, con->v2.out_zero);

	if (con->v2.out_zero) {
		set_out_bvec_zero(con);
		con->v2.out_zero -= con->v2.out_bvec.bv_len;
		con->v2.out_state = OUT_S_QUEUE_ZEROS;
		return;
	}

	/*
	 * We've zero-filled everything up to epilogue.  Queue epilogue
	 * with late_status set to ABORTED and crcs adjusted for zeros.
	 * Once it's written, we are done patching up for the revoke.
	 */
	reset_out_kvecs(con);
	prepare_epilogue_plain(con, true);
	con->v2.out_state = OUT_S_FINISH_MESSAGE;
}

static void finish_message(struct ceph_connection *con)
{
	dout("%s con %p msg %p\n", __func__, con, con->out_msg);

	/* we end up here both plain and secure modes */
	if (con->v2.out_enc_pages) {
		WARN_ON(!con->v2.out_enc_page_cnt);
		ceph_release_page_vector(con->v2.out_enc_pages,
					 con->v2.out_enc_page_cnt);
		con->v2.out_enc_pages = NULL;
		con->v2.out_enc_page_cnt = 0;
	}
	/* message may have been revoked */
	if (con->out_msg) {
		ceph_msg_put(con->out_msg);
		con->out_msg = NULL;
	}

	con->v2.out_state = OUT_S_GET_NEXT;
}

static int populate_out_iter(struct ceph_connection *con)
{
	int ret;

	dout("%s con %p state %d out_state %d\n", __func__, con, con->state,
	     con->v2.out_state);
	WARN_ON(iov_iter_count(&con->v2.out_iter));

	if (con->state != CEPH_CON_S_OPEN) {
		WARN_ON(con->state < CEPH_CON_S_V2_BANNER_PREFIX ||
			con->state > CEPH_CON_S_V2_SESSION_RECONNECT);
		goto nothing_pending;
	}

	switch (con->v2.out_state) {
	case OUT_S_QUEUE_DATA:
		WARN_ON(!con->out_msg);
		queue_data(con);
		goto populated;
	case OUT_S_QUEUE_DATA_CONT:
		WARN_ON(!con->out_msg);
		queue_data_cont(con);
		goto populated;
	case OUT_S_QUEUE_ENC_PAGE:
		queue_enc_page(con);
		goto populated;
	case OUT_S_QUEUE_ZEROS:
		WARN_ON(con->out_msg);  /* revoked */
		queue_zeros(con);
		goto populated;
	case OUT_S_FINISH_MESSAGE:
		finish_message(con);
		break;
	case OUT_S_GET_NEXT:
		break;
	default:
		WARN(1, "bad out_state %d", con->v2.out_state);
		return -EINVAL;
	}

	WARN_ON(con->v2.out_state != OUT_S_GET_NEXT);
	if (ceph_con_flag_test_and_clear(con, CEPH_CON_F_KEEPALIVE_PENDING)) {
		ret = prepare_keepalive2(con);
		if (ret) {
			pr_err("prepare_keepalive2 failed: %d\n", ret);
			return ret;
		}
	} else if (!list_empty(&con->out_queue)) {
		ceph_con_get_out_msg(con);
		ret = prepare_message(con);
		if (ret) {
			pr_err("prepare_message failed: %d\n", ret);
			return ret;
		}
	} else if (con->in_seq > con->in_seq_acked) {
		ret = prepare_ack(con);
		if (ret) {
			pr_err("prepare_ack failed: %d\n", ret);
			return ret;
		}
	} else {
		goto nothing_pending;
	}

populated:
	if (WARN_ON(!iov_iter_count(&con->v2.out_iter)))
		return -ENODATA;
	dout("%s con %p populated %zu\n", __func__, con,
	     iov_iter_count(&con->v2.out_iter));
	return 1;

nothing_pending:
	WARN_ON(iov_iter_count(&con->v2.out_iter));
	dout("%s con %p nothing pending\n", __func__, con);
	ceph_con_flag_clear(con, CEPH_CON_F_WRITE_PENDING);
	return 0;
}

int ceph_con_v2_try_write(struct ceph_connection *con)
{
	int ret;

	dout("%s con %p state %d have %zu\n", __func__, con, con->state,
	     iov_iter_count(&con->v2.out_iter));

	/* open the socket first? */
	if (con->state == CEPH_CON_S_PREOPEN) {
		WARN_ON(con->peer_addr.type != CEPH_ENTITY_ADDR_TYPE_MSGR2);

		/*
		 * Always bump global_seq.  Bump connect_seq only if
		 * there is a session (i.e. we are reconnecting and will
		 * send session_reconnect instead of client_ident).
		 */
		con->v2.global_seq = ceph_get_global_seq(con->msgr, 0);
		if (con->v2.server_cookie)
			con->v2.connect_seq++;

		ret = prepare_read_banner_prefix(con);
		if (ret) {
			pr_err("prepare_read_banner_prefix failed: %d\n", ret);
			con->error_msg = "connect error";
			return ret;
		}

		reset_out_kvecs(con);
		ret = prepare_banner(con);
		if (ret) {
			pr_err("prepare_banner failed: %d\n", ret);
			con->error_msg = "connect error";
			return ret;
		}

		ret = ceph_tcp_connect(con);
		if (ret) {
			pr_err("ceph_tcp_connect failed: %d\n", ret);
			con->error_msg = "connect error";
			return ret;
		}
	}

	if (!iov_iter_count(&con->v2.out_iter)) {
		ret = populate_out_iter(con);
		if (ret <= 0) {
			if (ret && ret != -EAGAIN && !con->error_msg)
				con->error_msg = "write processing error";
			return ret;
		}
	}

	tcp_sock_set_cork(con->sock->sk, true);
	for (;;) {
		ret = ceph_tcp_send(con);
		if (ret <= 0)
			break;

		ret = populate_out_iter(con);
		if (ret <= 0) {
			if (ret && ret != -EAGAIN && !con->error_msg)
				con->error_msg = "write processing error";
			break;
		}
	}

	tcp_sock_set_cork(con->sock->sk, false);
	return ret;
}

static u32 crc32c_zeros(u32 crc, int zero_len)
{
	int len;

	while (zero_len) {
		len = min(zero_len, (int)PAGE_SIZE);
		crc = crc32c(crc, page_address(ceph_zero_page), len);
		zero_len -= len;
	}

	return crc;
}

static void prepare_zero_front(struct ceph_connection *con, int resid)
{
	int sent;

	WARN_ON(!resid || resid > front_len(con->out_msg));
	sent = front_len(con->out_msg) - resid;
	dout("%s con %p sent %d resid %d\n", __func__, con, sent, resid);

	if (sent) {
		con->v2.out_epil.front_crc =
			crc32c(-1, con->out_msg->front.iov_base, sent);
		con->v2.out_epil.front_crc =
			crc32c_zeros(con->v2.out_epil.front_crc, resid);
	} else {
		con->v2.out_epil.front_crc = crc32c_zeros(-1, resid);
	}

	con->v2.out_iter.count -= resid;
	out_zero_add(con, resid);
}

static void prepare_zero_middle(struct ceph_connection *con, int resid)
{
	int sent;

	WARN_ON(!resid || resid > middle_len(con->out_msg));
	sent = middle_len(con->out_msg) - resid;
	dout("%s con %p sent %d resid %d\n", __func__, con, sent, resid);

	if (sent) {
		con->v2.out_epil.middle_crc =
			crc32c(-1, con->out_msg->middle->vec.iov_base, sent);
		con->v2.out_epil.middle_crc =
			crc32c_zeros(con->v2.out_epil.middle_crc, resid);
	} else {
		con->v2.out_epil.middle_crc = crc32c_zeros(-1, resid);
	}

	con->v2.out_iter.count -= resid;
	out_zero_add(con, resid);
}

static void prepare_zero_data(struct ceph_connection *con)
{
	dout("%s con %p\n", __func__, con);
	con->v2.out_epil.data_crc = crc32c_zeros(-1, data_len(con->out_msg));
	out_zero_add(con, data_len(con->out_msg));
}

static void revoke_at_queue_data(struct ceph_connection *con)
{
	int boundary;
	int resid;

	WARN_ON(!data_len(con->out_msg));
	WARN_ON(!iov_iter_is_kvec(&con->v2.out_iter));
	resid = iov_iter_count(&con->v2.out_iter);

	boundary = front_len(con->out_msg) + middle_len(con->out_msg);
	if (resid > boundary) {
		resid -= boundary;
		WARN_ON(resid > MESSAGE_HEAD_PLAIN_LEN);
		dout("%s con %p was sending head\n", __func__, con);
		if (front_len(con->out_msg))
			prepare_zero_front(con, front_len(con->out_msg));
		if (middle_len(con->out_msg))
			prepare_zero_middle(con, middle_len(con->out_msg));
		prepare_zero_data(con);
		WARN_ON(iov_iter_count(&con->v2.out_iter) != resid);
		con->v2.out_state = OUT_S_QUEUE_ZEROS;
		return;
	}

	boundary = middle_len(con->out_msg);
	if (resid > boundary) {
		resid -= boundary;
		dout("%s con %p was sending front\n", __func__, con);
		prepare_zero_front(con, resid);
		if (middle_len(con->out_msg))
			prepare_zero_middle(con, middle_len(con->out_msg));
		prepare_zero_data(con);
		queue_zeros(con);
		return;
	}

	WARN_ON(!resid);
	dout("%s con %p was sending middle\n", __func__, con);
	prepare_zero_middle(con, resid);
	prepare_zero_data(con);
	queue_zeros(con);
}

static void revoke_at_queue_data_cont(struct ceph_connection *con)
{
	int sent, resid;  /* current piece of data */

	WARN_ON(!data_len(con->out_msg));
	WARN_ON(!iov_iter_is_bvec(&con->v2.out_iter));
	resid = iov_iter_count(&con->v2.out_iter);
	WARN_ON(!resid || resid > con->v2.out_bvec.bv_len);
	sent = con->v2.out_bvec.bv_len - resid;
	dout("%s con %p sent %d resid %d\n", __func__, con, sent, resid);

	if (sent) {
		con->v2.out_epil.data_crc = ceph_crc32c_page(
			con->v2.out_epil.data_crc, con->v2.out_bvec.bv_page,
			con->v2.out_bvec.bv_offset, sent);
		ceph_msg_data_advance(&con->v2.out_cursor, sent);
	}
	WARN_ON(resid > con->v2.out_cursor.total_resid);
	con->v2.out_epil.data_crc = crc32c_zeros(con->v2.out_epil.data_crc,
						con->v2.out_cursor.total_resid);

	con->v2.out_iter.count -= resid;
	out_zero_add(con, con->v2.out_cursor.total_resid);
	queue_zeros(con);
}

static void revoke_at_finish_message(struct ceph_connection *con)
{
	int boundary;
	int resid;

	WARN_ON(!iov_iter_is_kvec(&con->v2.out_iter));
	resid = iov_iter_count(&con->v2.out_iter);

	if (!front_len(con->out_msg) && !middle_len(con->out_msg) &&
	    !data_len(con->out_msg)) {
		WARN_ON(!resid || resid > MESSAGE_HEAD_PLAIN_LEN);
		dout("%s con %p was sending head (empty message) - noop\n",
		     __func__, con);
		return;
	}

	boundary = front_len(con->out_msg) + middle_len(con->out_msg) +
		   CEPH_EPILOGUE_PLAIN_LEN;
	if (resid > boundary) {
		resid -= boundary;
		WARN_ON(resid > MESSAGE_HEAD_PLAIN_LEN);
		dout("%s con %p was sending head\n", __func__, con);
		if (front_len(con->out_msg))
			prepare_zero_front(con, front_len(con->out_msg));
		if (middle_len(con->out_msg))
			prepare_zero_middle(con, middle_len(con->out_msg));
		con->v2.out_iter.count -= CEPH_EPILOGUE_PLAIN_LEN;
		WARN_ON(iov_iter_count(&con->v2.out_iter) != resid);
		con->v2.out_state = OUT_S_QUEUE_ZEROS;
		return;
	}

	boundary = middle_len(con->out_msg) + CEPH_EPILOGUE_PLAIN_LEN;
	if (resid > boundary) {
		resid -= boundary;
		dout("%s con %p was sending front\n", __func__, con);
		prepare_zero_front(con, resid);
		if (middle_len(con->out_msg))
			prepare_zero_middle(con, middle_len(con->out_msg));
		con->v2.out_iter.count -= CEPH_EPILOGUE_PLAIN_LEN;
		queue_zeros(con);
		return;
	}

	boundary = CEPH_EPILOGUE_PLAIN_LEN;
	if (resid > boundary) {
		resid -= boundary;
		dout("%s con %p was sending middle\n", __func__, con);
		prepare_zero_middle(con, resid);
		con->v2.out_iter.count -= CEPH_EPILOGUE_PLAIN_LEN;
		queue_zeros(con);
		return;
	}

	WARN_ON(!resid);
	dout("%s con %p was sending epilogue - noop\n", __func__, con);
}

void ceph_con_v2_revoke(struct ceph_connection *con)
{
	WARN_ON(con->v2.out_zero);

	if (con_secure(con)) {
		WARN_ON(con->v2.out_state != OUT_S_QUEUE_ENC_PAGE &&
			con->v2.out_state != OUT_S_FINISH_MESSAGE);
		dout("%s con %p secure - noop\n", __func__, con);
		return;
	}

	switch (con->v2.out_state) {
	case OUT_S_QUEUE_DATA:
		revoke_at_queue_data(con);
		break;
	case OUT_S_QUEUE_DATA_CONT:
		revoke_at_queue_data_cont(con);
		break;
	case OUT_S_FINISH_MESSAGE:
		revoke_at_finish_message(con);
		break;
	default:
		WARN(1, "bad out_state %d", con->v2.out_state);
		break;
	}
}

static void revoke_at_prepare_read_data(struct ceph_connection *con)
{
	int remaining;
	int resid;

	WARN_ON(con_secure(con));
	WARN_ON(!data_len(con->in_msg));
	WARN_ON(!iov_iter_is_kvec(&con->v2.in_iter));
	resid = iov_iter_count(&con->v2.in_iter);
	WARN_ON(!resid);

	remaining = data_len(con->in_msg) + CEPH_EPILOGUE_PLAIN_LEN;
	dout("%s con %p resid %d remaining %d\n", __func__, con, resid,
	     remaining);
	con->v2.in_iter.count -= resid;
	set_in_skip(con, resid + remaining);
	con->v2.in_state = IN_S_FINISH_SKIP;
}

static void revoke_at_prepare_read_data_cont(struct ceph_connection *con)
{
	int recved, resid;  /* current piece of data */
	int remaining;

	WARN_ON(con_secure(con));
	WARN_ON(!data_len(con->in_msg));
	WARN_ON(!iov_iter_is_bvec(&con->v2.in_iter));
	resid = iov_iter_count(&con->v2.in_iter);
	WARN_ON(!resid || resid > con->v2.in_bvec.bv_len);
	recved = con->v2.in_bvec.bv_len - resid;
	dout("%s con %p recved %d resid %d\n", __func__, con, recved, resid);

	if (recved)
		ceph_msg_data_advance(&con->v2.in_cursor, recved);
	WARN_ON(resid > con->v2.in_cursor.total_resid);

	remaining = CEPH_EPILOGUE_PLAIN_LEN;
	dout("%s con %p total_resid %zu remaining %d\n", __func__, con,
	     con->v2.in_cursor.total_resid, remaining);
	con->v2.in_iter.count -= resid;
	set_in_skip(con, con->v2.in_cursor.total_resid + remaining);
	con->v2.in_state = IN_S_FINISH_SKIP;
}

static void revoke_at_prepare_read_enc_page(struct ceph_connection *con)
{
	int resid;  /* current enc page (not necessarily data) */

	WARN_ON(!con_secure(con));
	WARN_ON(!iov_iter_is_bvec(&con->v2.in_iter));
	resid = iov_iter_count(&con->v2.in_iter);
	WARN_ON(!resid || resid > con->v2.in_bvec.bv_len);

	dout("%s con %p resid %d enc_resid %d\n", __func__, con, resid,
	     con->v2.in_enc_resid);
	con->v2.in_iter.count -= resid;
	set_in_skip(con, resid + con->v2.in_enc_resid);
	con->v2.in_state = IN_S_FINISH_SKIP;
}

static void revoke_at_prepare_sparse_data(struct ceph_connection *con)
{
	int resid;  /* current piece of data */
	int remaining;

	WARN_ON(con_secure(con));
	WARN_ON(!data_len(con->in_msg));
	WARN_ON(!iov_iter_is_bvec(&con->v2.in_iter));
	resid = iov_iter_count(&con->v2.in_iter);
	dout("%s con %p resid %d\n", __func__, con, resid);

	remaining = CEPH_EPILOGUE_PLAIN_LEN + con->v2.data_len_remain;
	con->v2.in_iter.count -= resid;
	set_in_skip(con, resid + remaining);
	con->v2.in_state = IN_S_FINISH_SKIP;
}

static void revoke_at_handle_epilogue(struct ceph_connection *con)
{
	int resid;

	resid = iov_iter_count(&con->v2.in_iter);
	WARN_ON(!resid);

	dout("%s con %p resid %d\n", __func__, con, resid);
	con->v2.in_iter.count -= resid;
	set_in_skip(con, resid);
	con->v2.in_state = IN_S_FINISH_SKIP;
}

void ceph_con_v2_revoke_incoming(struct ceph_connection *con)
{
	switch (con->v2.in_state) {
	case IN_S_PREPARE_SPARSE_DATA:
	case IN_S_PREPARE_READ_DATA:
		revoke_at_prepare_read_data(con);
		break;
	case IN_S_PREPARE_READ_DATA_CONT:
		revoke_at_prepare_read_data_cont(con);
		break;
	case IN_S_PREPARE_READ_ENC_PAGE:
		revoke_at_prepare_read_enc_page(con);
		break;
	case IN_S_PREPARE_SPARSE_DATA_CONT:
		revoke_at_prepare_sparse_data(con);
		break;
	case IN_S_HANDLE_EPILOGUE:
		revoke_at_handle_epilogue(con);
		break;
	default:
		WARN(1, "bad in_state %d", con->v2.in_state);
		break;
	}
}

bool ceph_con_v2_opened(struct ceph_connection *con)
{
	return con->v2.peer_global_seq;
}

void ceph_con_v2_reset_session(struct ceph_connection *con)
{
	con->v2.client_cookie = 0;
	con->v2.server_cookie = 0;
	con->v2.global_seq = 0;
	con->v2.connect_seq = 0;
	con->v2.peer_global_seq = 0;
}

void ceph_con_v2_reset_protocol(struct ceph_connection *con)
{
	iov_iter_truncate(&con->v2.in_iter, 0);
	iov_iter_truncate(&con->v2.out_iter, 0);
	con->v2.out_zero = 0;

	clear_in_sign_kvecs(con);
	clear_out_sign_kvecs(con);
	free_conn_bufs(con);

	if (con->v2.in_enc_pages) {
		WARN_ON(!con->v2.in_enc_page_cnt);
		ceph_release_page_vector(con->v2.in_enc_pages,
					 con->v2.in_enc_page_cnt);
		con->v2.in_enc_pages = NULL;
		con->v2.in_enc_page_cnt = 0;
	}
	if (con->v2.out_enc_pages) {
		WARN_ON(!con->v2.out_enc_page_cnt);
		ceph_release_page_vector(con->v2.out_enc_pages,
					 con->v2.out_enc_page_cnt);
		con->v2.out_enc_pages = NULL;
		con->v2.out_enc_page_cnt = 0;
	}

	con->v2.con_mode = CEPH_CON_MODE_UNKNOWN;
	memzero_explicit(&con->v2.in_gcm_nonce, CEPH_GCM_IV_LEN);
	memzero_explicit(&con->v2.out_gcm_nonce, CEPH_GCM_IV_LEN);

	if (con->v2.hmac_tfm) {
		crypto_free_shash(con->v2.hmac_tfm);
		con->v2.hmac_tfm = NULL;
	}
	if (con->v2.gcm_req) {
		aead_request_free(con->v2.gcm_req);
		con->v2.gcm_req = NULL;
	}
	if (con->v2.gcm_tfm) {
		crypto_free_aead(con->v2.gcm_tfm);
		con->v2.gcm_tfm = NULL;
	}
}
