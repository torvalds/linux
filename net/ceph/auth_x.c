// SPDX-License-Identifier: GPL-2.0

#include <linux/ceph/ceph_debug.h>

#include <linux/err.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>

#include <linux/ceph/decode.h>
#include <linux/ceph/auth.h>
#include <linux/ceph/libceph.h>
#include <linux/ceph/messenger.h>

#include "crypto.h"
#include "auth_x.h"
#include "auth_x_protocol.h"

static void ceph_x_validate_tickets(struct ceph_auth_client *ac, int *pneed);

static int ceph_x_is_authenticated(struct ceph_auth_client *ac)
{
	struct ceph_x_info *xi = ac->private;
	int need;

	ceph_x_validate_tickets(ac, &need);
	dout("ceph_x_is_authenticated want=%d need=%d have=%d\n",
	     ac->want_keys, need, xi->have_keys);
	return (ac->want_keys & xi->have_keys) == ac->want_keys;
}

static int ceph_x_should_authenticate(struct ceph_auth_client *ac)
{
	struct ceph_x_info *xi = ac->private;
	int need;

	ceph_x_validate_tickets(ac, &need);
	dout("ceph_x_should_authenticate want=%d need=%d have=%d\n",
	     ac->want_keys, need, xi->have_keys);
	return need != 0;
}

static int ceph_x_encrypt_offset(void)
{
	return sizeof(u32) + sizeof(struct ceph_x_encrypt_header);
}

static int ceph_x_encrypt_buflen(int ilen)
{
	return ceph_x_encrypt_offset() + ilen + 16;
}

static int ceph_x_encrypt(struct ceph_crypto_key *secret, void *buf,
			  int buf_len, int plaintext_len)
{
	struct ceph_x_encrypt_header *hdr = buf + sizeof(u32);
	int ciphertext_len;
	int ret;

	hdr->struct_v = 1;
	hdr->magic = cpu_to_le64(CEPHX_ENC_MAGIC);

	ret = ceph_crypt(secret, true, buf + sizeof(u32), buf_len - sizeof(u32),
			 plaintext_len + sizeof(struct ceph_x_encrypt_header),
			 &ciphertext_len);
	if (ret)
		return ret;

	ceph_encode_32(&buf, ciphertext_len);
	return sizeof(u32) + ciphertext_len;
}

static int ceph_x_decrypt(struct ceph_crypto_key *secret, void **p, void *end)
{
	struct ceph_x_encrypt_header *hdr = *p + sizeof(u32);
	int ciphertext_len, plaintext_len;
	int ret;

	ceph_decode_32_safe(p, end, ciphertext_len, e_inval);
	ceph_decode_need(p, end, ciphertext_len, e_inval);

	ret = ceph_crypt(secret, false, *p, end - *p, ciphertext_len,
			 &plaintext_len);
	if (ret)
		return ret;

	if (hdr->struct_v != 1 || le64_to_cpu(hdr->magic) != CEPHX_ENC_MAGIC)
		return -EPERM;

	*p += ciphertext_len;
	return plaintext_len - sizeof(struct ceph_x_encrypt_header);

e_inval:
	return -EINVAL;
}

/*
 * get existing (or insert new) ticket handler
 */
static struct ceph_x_ticket_handler *
get_ticket_handler(struct ceph_auth_client *ac, int service)
{
	struct ceph_x_ticket_handler *th;
	struct ceph_x_info *xi = ac->private;
	struct rb_node *parent = NULL, **p = &xi->ticket_handlers.rb_node;

	while (*p) {
		parent = *p;
		th = rb_entry(parent, struct ceph_x_ticket_handler, node);
		if (service < th->service)
			p = &(*p)->rb_left;
		else if (service > th->service)
			p = &(*p)->rb_right;
		else
			return th;
	}

	/* add it */
	th = kzalloc(sizeof(*th), GFP_NOFS);
	if (!th)
		return ERR_PTR(-ENOMEM);
	th->service = service;
	rb_link_node(&th->node, parent, p);
	rb_insert_color(&th->node, &xi->ticket_handlers);
	return th;
}

static void remove_ticket_handler(struct ceph_auth_client *ac,
				  struct ceph_x_ticket_handler *th)
{
	struct ceph_x_info *xi = ac->private;

	dout("remove_ticket_handler %p %d\n", th, th->service);
	rb_erase(&th->node, &xi->ticket_handlers);
	ceph_crypto_key_destroy(&th->session_key);
	if (th->ticket_blob)
		ceph_buffer_put(th->ticket_blob);
	kfree(th);
}

static int process_one_ticket(struct ceph_auth_client *ac,
			      struct ceph_crypto_key *secret,
			      void **p, void *end)
{
	struct ceph_x_info *xi = ac->private;
	int type;
	u8 tkt_struct_v, blob_struct_v;
	struct ceph_x_ticket_handler *th;
	void *dp, *dend;
	int dlen;
	char is_enc;
	struct timespec validity;
	void *tp, *tpend;
	void **ptp;
	struct ceph_crypto_key new_session_key = { 0 };
	struct ceph_buffer *new_ticket_blob;
	unsigned long new_expires, new_renew_after;
	u64 new_secret_id;
	int ret;

	ceph_decode_need(p, end, sizeof(u32) + 1, bad);

	type = ceph_decode_32(p);
	dout(" ticket type %d %s\n", type, ceph_entity_type_name(type));

	tkt_struct_v = ceph_decode_8(p);
	if (tkt_struct_v != 1)
		goto bad;

	th = get_ticket_handler(ac, type);
	if (IS_ERR(th)) {
		ret = PTR_ERR(th);
		goto out;
	}

	/* blob for me */
	dp = *p + ceph_x_encrypt_offset();
	ret = ceph_x_decrypt(secret, p, end);
	if (ret < 0)
		goto out;
	dout(" decrypted %d bytes\n", ret);
	dend = dp + ret;

	tkt_struct_v = ceph_decode_8(&dp);
	if (tkt_struct_v != 1)
		goto bad;

	ret = ceph_crypto_key_decode(&new_session_key, &dp, dend);
	if (ret)
		goto out;

	ceph_decode_timespec(&validity, dp);
	dp += sizeof(struct ceph_timespec);
	new_expires = get_seconds() + validity.tv_sec;
	new_renew_after = new_expires - (validity.tv_sec / 4);
	dout(" expires=%lu renew_after=%lu\n", new_expires,
	     new_renew_after);

	/* ticket blob for service */
	ceph_decode_8_safe(p, end, is_enc, bad);
	if (is_enc) {
		/* encrypted */
		tp = *p + ceph_x_encrypt_offset();
		ret = ceph_x_decrypt(&th->session_key, p, end);
		if (ret < 0)
			goto out;
		dout(" encrypted ticket, decrypted %d bytes\n", ret);
		ptp = &tp;
		tpend = tp + ret;
	} else {
		/* unencrypted */
		ptp = p;
		tpend = end;
	}
	ceph_decode_32_safe(ptp, tpend, dlen, bad);
	dout(" ticket blob is %d bytes\n", dlen);
	ceph_decode_need(ptp, tpend, 1 + sizeof(u64), bad);
	blob_struct_v = ceph_decode_8(ptp);
	if (blob_struct_v != 1)
		goto bad;

	new_secret_id = ceph_decode_64(ptp);
	ret = ceph_decode_buffer(&new_ticket_blob, ptp, tpend);
	if (ret)
		goto out;

	/* all is well, update our ticket */
	ceph_crypto_key_destroy(&th->session_key);
	if (th->ticket_blob)
		ceph_buffer_put(th->ticket_blob);
	th->session_key = new_session_key;
	th->ticket_blob = new_ticket_blob;
	th->secret_id = new_secret_id;
	th->expires = new_expires;
	th->renew_after = new_renew_after;
	th->have_key = true;
	dout(" got ticket service %d (%s) secret_id %lld len %d\n",
	     type, ceph_entity_type_name(type), th->secret_id,
	     (int)th->ticket_blob->vec.iov_len);
	xi->have_keys |= th->service;
	return 0;

bad:
	ret = -EINVAL;
out:
	ceph_crypto_key_destroy(&new_session_key);
	return ret;
}

static int ceph_x_proc_ticket_reply(struct ceph_auth_client *ac,
				    struct ceph_crypto_key *secret,
				    void *buf, void *end)
{
	void *p = buf;
	u8 reply_struct_v;
	u32 num;
	int ret;

	ceph_decode_8_safe(&p, end, reply_struct_v, bad);
	if (reply_struct_v != 1)
		return -EINVAL;

	ceph_decode_32_safe(&p, end, num, bad);
	dout("%d tickets\n", num);

	while (num--) {
		ret = process_one_ticket(ac, secret, &p, end);
		if (ret)
			return ret;
	}

	return 0;

bad:
	return -EINVAL;
}

static void ceph_x_authorizer_cleanup(struct ceph_x_authorizer *au)
{
	ceph_crypto_key_destroy(&au->session_key);
	if (au->buf) {
		ceph_buffer_put(au->buf);
		au->buf = NULL;
	}
}

static int ceph_x_build_authorizer(struct ceph_auth_client *ac,
				   struct ceph_x_ticket_handler *th,
				   struct ceph_x_authorizer *au)
{
	int maxlen;
	struct ceph_x_authorize_a *msg_a;
	struct ceph_x_authorize_b *msg_b;
	void *p, *end;
	int ret;
	int ticket_blob_len =
		(th->ticket_blob ? th->ticket_blob->vec.iov_len : 0);

	dout("build_authorizer for %s %p\n",
	     ceph_entity_type_name(th->service), au);

	ceph_crypto_key_destroy(&au->session_key);
	ret = ceph_crypto_key_clone(&au->session_key, &th->session_key);
	if (ret)
		goto out_au;

	maxlen = sizeof(*msg_a) + ticket_blob_len +
		ceph_x_encrypt_buflen(sizeof(*msg_b));
	dout("  need len %d\n", maxlen);
	if (au->buf && au->buf->alloc_len < maxlen) {
		ceph_buffer_put(au->buf);
		au->buf = NULL;
	}
	if (!au->buf) {
		au->buf = ceph_buffer_new(maxlen, GFP_NOFS);
		if (!au->buf) {
			ret = -ENOMEM;
			goto out_au;
		}
	}
	au->service = th->service;
	au->secret_id = th->secret_id;

	msg_a = au->buf->vec.iov_base;
	msg_a->struct_v = 1;
	msg_a->global_id = cpu_to_le64(ac->global_id);
	msg_a->service_id = cpu_to_le32(th->service);
	msg_a->ticket_blob.struct_v = 1;
	msg_a->ticket_blob.secret_id = cpu_to_le64(th->secret_id);
	msg_a->ticket_blob.blob_len = cpu_to_le32(ticket_blob_len);
	if (ticket_blob_len) {
		memcpy(msg_a->ticket_blob.blob, th->ticket_blob->vec.iov_base,
		       th->ticket_blob->vec.iov_len);
	}
	dout(" th %p secret_id %lld %lld\n", th, th->secret_id,
	     le64_to_cpu(msg_a->ticket_blob.secret_id));

	p = msg_a + 1;
	p += ticket_blob_len;
	end = au->buf->vec.iov_base + au->buf->vec.iov_len;

	msg_b = p + ceph_x_encrypt_offset();
	msg_b->struct_v = 1;
	get_random_bytes(&au->nonce, sizeof(au->nonce));
	msg_b->nonce = cpu_to_le64(au->nonce);
	ret = ceph_x_encrypt(&au->session_key, p, end - p, sizeof(*msg_b));
	if (ret < 0)
		goto out_au;

	p += ret;
	WARN_ON(p > end);
	au->buf->vec.iov_len = p - au->buf->vec.iov_base;
	dout(" built authorizer nonce %llx len %d\n", au->nonce,
	     (int)au->buf->vec.iov_len);
	return 0;

out_au:
	ceph_x_authorizer_cleanup(au);
	return ret;
}

static int ceph_x_encode_ticket(struct ceph_x_ticket_handler *th,
				void **p, void *end)
{
	ceph_decode_need(p, end, 1 + sizeof(u64), bad);
	ceph_encode_8(p, 1);
	ceph_encode_64(p, th->secret_id);
	if (th->ticket_blob) {
		const char *buf = th->ticket_blob->vec.iov_base;
		u32 len = th->ticket_blob->vec.iov_len;

		ceph_encode_32_safe(p, end, len, bad);
		ceph_encode_copy_safe(p, end, buf, len, bad);
	} else {
		ceph_encode_32_safe(p, end, 0, bad);
	}

	return 0;
bad:
	return -ERANGE;
}

static bool need_key(struct ceph_x_ticket_handler *th)
{
	if (!th->have_key)
		return true;

	return get_seconds() >= th->renew_after;
}

static bool have_key(struct ceph_x_ticket_handler *th)
{
	if (th->have_key) {
		if (get_seconds() >= th->expires)
			th->have_key = false;
	}

	return th->have_key;
}

static void ceph_x_validate_tickets(struct ceph_auth_client *ac, int *pneed)
{
	int want = ac->want_keys;
	struct ceph_x_info *xi = ac->private;
	int service;

	*pneed = ac->want_keys & ~(xi->have_keys);

	for (service = 1; service <= want; service <<= 1) {
		struct ceph_x_ticket_handler *th;

		if (!(ac->want_keys & service))
			continue;

		if (*pneed & service)
			continue;

		th = get_ticket_handler(ac, service);
		if (IS_ERR(th)) {
			*pneed |= service;
			continue;
		}

		if (need_key(th))
			*pneed |= service;
		if (!have_key(th))
			xi->have_keys &= ~service;
	}
}

static int ceph_x_build_request(struct ceph_auth_client *ac,
				void *buf, void *end)
{
	struct ceph_x_info *xi = ac->private;
	int need;
	struct ceph_x_request_header *head = buf;
	int ret;
	struct ceph_x_ticket_handler *th =
		get_ticket_handler(ac, CEPH_ENTITY_TYPE_AUTH);

	if (IS_ERR(th))
		return PTR_ERR(th);

	ceph_x_validate_tickets(ac, &need);

	dout("build_request want %x have %x need %x\n",
	     ac->want_keys, xi->have_keys, need);

	if (need & CEPH_ENTITY_TYPE_AUTH) {
		struct ceph_x_authenticate *auth = (void *)(head + 1);
		void *p = auth + 1;
		void *enc_buf = xi->auth_authorizer.enc_buf;
		struct ceph_x_challenge_blob *blob = enc_buf +
							ceph_x_encrypt_offset();
		u64 *u;

		if (p > end)
			return -ERANGE;

		dout(" get_auth_session_key\n");
		head->op = cpu_to_le16(CEPHX_GET_AUTH_SESSION_KEY);

		/* encrypt and hash */
		get_random_bytes(&auth->client_challenge, sizeof(u64));
		blob->client_challenge = auth->client_challenge;
		blob->server_challenge = cpu_to_le64(xi->server_challenge);
		ret = ceph_x_encrypt(&xi->secret, enc_buf, CEPHX_AU_ENC_BUF_LEN,
				     sizeof(*blob));
		if (ret < 0)
			return ret;

		auth->struct_v = 1;
		auth->key = 0;
		for (u = (u64 *)enc_buf; u + 1 <= (u64 *)(enc_buf + ret); u++)
			auth->key ^= *(__le64 *)u;
		dout(" server_challenge %llx client_challenge %llx key %llx\n",
		     xi->server_challenge, le64_to_cpu(auth->client_challenge),
		     le64_to_cpu(auth->key));

		/* now encode the old ticket if exists */
		ret = ceph_x_encode_ticket(th, &p, end);
		if (ret < 0)
			return ret;

		return p - buf;
	}

	if (need) {
		void *p = head + 1;
		struct ceph_x_service_ticket_request *req;

		if (p > end)
			return -ERANGE;
		head->op = cpu_to_le16(CEPHX_GET_PRINCIPAL_SESSION_KEY);

		ret = ceph_x_build_authorizer(ac, th, &xi->auth_authorizer);
		if (ret)
			return ret;
		ceph_encode_copy(&p, xi->auth_authorizer.buf->vec.iov_base,
				 xi->auth_authorizer.buf->vec.iov_len);

		req = p;
		req->keys = cpu_to_le32(need);
		p += sizeof(*req);
		return p - buf;
	}

	return 0;
}

static int ceph_x_handle_reply(struct ceph_auth_client *ac, int result,
			       void *buf, void *end)
{
	struct ceph_x_info *xi = ac->private;
	struct ceph_x_reply_header *head = buf;
	struct ceph_x_ticket_handler *th;
	int len = end - buf;
	int op;
	int ret;

	if (result)
		return result;  /* XXX hmm? */

	if (xi->starting) {
		/* it's a hello */
		struct ceph_x_server_challenge *sc = buf;

		if (len != sizeof(*sc))
			return -EINVAL;
		xi->server_challenge = le64_to_cpu(sc->server_challenge);
		dout("handle_reply got server challenge %llx\n",
		     xi->server_challenge);
		xi->starting = false;
		xi->have_keys &= ~CEPH_ENTITY_TYPE_AUTH;
		return -EAGAIN;
	}

	op = le16_to_cpu(head->op);
	result = le32_to_cpu(head->result);
	dout("handle_reply op %d result %d\n", op, result);
	switch (op) {
	case CEPHX_GET_AUTH_SESSION_KEY:
		/* verify auth key */
		ret = ceph_x_proc_ticket_reply(ac, &xi->secret,
					       buf + sizeof(*head), end);
		break;

	case CEPHX_GET_PRINCIPAL_SESSION_KEY:
		th = get_ticket_handler(ac, CEPH_ENTITY_TYPE_AUTH);
		if (IS_ERR(th))
			return PTR_ERR(th);
		ret = ceph_x_proc_ticket_reply(ac, &th->session_key,
					       buf + sizeof(*head), end);
		break;

	default:
		return -EINVAL;
	}
	if (ret)
		return ret;
	if (ac->want_keys == xi->have_keys)
		return 0;
	return -EAGAIN;
}

static void ceph_x_destroy_authorizer(struct ceph_authorizer *a)
{
	struct ceph_x_authorizer *au = (void *)a;

	ceph_x_authorizer_cleanup(au);
	kfree(au);
}

static int ceph_x_create_authorizer(
	struct ceph_auth_client *ac, int peer_type,
	struct ceph_auth_handshake *auth)
{
	struct ceph_x_authorizer *au;
	struct ceph_x_ticket_handler *th;
	int ret;

	th = get_ticket_handler(ac, peer_type);
	if (IS_ERR(th))
		return PTR_ERR(th);

	au = kzalloc(sizeof(*au), GFP_NOFS);
	if (!au)
		return -ENOMEM;

	au->base.destroy = ceph_x_destroy_authorizer;

	ret = ceph_x_build_authorizer(ac, th, au);
	if (ret) {
		kfree(au);
		return ret;
	}

	auth->authorizer = (struct ceph_authorizer *) au;
	auth->authorizer_buf = au->buf->vec.iov_base;
	auth->authorizer_buf_len = au->buf->vec.iov_len;
	auth->authorizer_reply_buf = au->enc_buf;
	auth->authorizer_reply_buf_len = CEPHX_AU_ENC_BUF_LEN;
	auth->sign_message = ac->ops->sign_message;
	auth->check_message_signature = ac->ops->check_message_signature;

	return 0;
}

static int ceph_x_update_authorizer(
	struct ceph_auth_client *ac, int peer_type,
	struct ceph_auth_handshake *auth)
{
	struct ceph_x_authorizer *au;
	struct ceph_x_ticket_handler *th;

	th = get_ticket_handler(ac, peer_type);
	if (IS_ERR(th))
		return PTR_ERR(th);

	au = (struct ceph_x_authorizer *)auth->authorizer;
	if (au->secret_id < th->secret_id) {
		dout("ceph_x_update_authorizer service %u secret %llu < %llu\n",
		     au->service, au->secret_id, th->secret_id);
		return ceph_x_build_authorizer(ac, th, au);
	}
	return 0;
}

static int ceph_x_verify_authorizer_reply(struct ceph_auth_client *ac,
					  struct ceph_authorizer *a)
{
	struct ceph_x_authorizer *au = (void *)a;
	void *p = au->enc_buf;
	struct ceph_x_authorize_reply *reply = p + ceph_x_encrypt_offset();
	int ret;

	ret = ceph_x_decrypt(&au->session_key, &p, p + CEPHX_AU_ENC_BUF_LEN);
	if (ret < 0)
		return ret;
	if (ret != sizeof(*reply))
		return -EPERM;

	if (au->nonce + 1 != le64_to_cpu(reply->nonce_plus_one))
		ret = -EPERM;
	else
		ret = 0;
	dout("verify_authorizer_reply nonce %llx got %llx ret %d\n",
	     au->nonce, le64_to_cpu(reply->nonce_plus_one), ret);
	return ret;
}

static void ceph_x_reset(struct ceph_auth_client *ac)
{
	struct ceph_x_info *xi = ac->private;

	dout("reset\n");
	xi->starting = true;
	xi->server_challenge = 0;
}

static void ceph_x_destroy(struct ceph_auth_client *ac)
{
	struct ceph_x_info *xi = ac->private;
	struct rb_node *p;

	dout("ceph_x_destroy %p\n", ac);
	ceph_crypto_key_destroy(&xi->secret);

	while ((p = rb_first(&xi->ticket_handlers)) != NULL) {
		struct ceph_x_ticket_handler *th =
			rb_entry(p, struct ceph_x_ticket_handler, node);
		remove_ticket_handler(ac, th);
	}

	ceph_x_authorizer_cleanup(&xi->auth_authorizer);

	kfree(ac->private);
	ac->private = NULL;
}

static void invalidate_ticket(struct ceph_auth_client *ac, int peer_type)
{
	struct ceph_x_ticket_handler *th;

	th = get_ticket_handler(ac, peer_type);
	if (!IS_ERR(th))
		th->have_key = false;
}

static void ceph_x_invalidate_authorizer(struct ceph_auth_client *ac,
					 int peer_type)
{
	/*
	 * We are to invalidate a service ticket in the hopes of
	 * getting a new, hopefully more valid, one.  But, we won't get
	 * it unless our AUTH ticket is good, so invalidate AUTH ticket
	 * as well, just in case.
	 */
	invalidate_ticket(ac, peer_type);
	invalidate_ticket(ac, CEPH_ENTITY_TYPE_AUTH);
}

static int calc_signature(struct ceph_x_authorizer *au, struct ceph_msg *msg,
			  __le64 *psig)
{
	void *enc_buf = au->enc_buf;
	struct {
		__le32 len;
		__le32 header_crc;
		__le32 front_crc;
		__le32 middle_crc;
		__le32 data_crc;
	} __packed *sigblock = enc_buf + ceph_x_encrypt_offset();
	int ret;

	sigblock->len = cpu_to_le32(4*sizeof(u32));
	sigblock->header_crc = msg->hdr.crc;
	sigblock->front_crc = msg->footer.front_crc;
	sigblock->middle_crc = msg->footer.middle_crc;
	sigblock->data_crc =  msg->footer.data_crc;
	ret = ceph_x_encrypt(&au->session_key, enc_buf, CEPHX_AU_ENC_BUF_LEN,
			     sizeof(*sigblock));
	if (ret < 0)
		return ret;

	*psig = *(__le64 *)(enc_buf + sizeof(u32));
	return 0;
}

static int ceph_x_sign_message(struct ceph_auth_handshake *auth,
			       struct ceph_msg *msg)
{
	__le64 sig;
	int ret;

	if (ceph_test_opt(from_msgr(msg->con->msgr), NOMSGSIGN))
		return 0;

	ret = calc_signature((struct ceph_x_authorizer *)auth->authorizer,
			     msg, &sig);
	if (ret)
		return ret;

	msg->footer.sig = sig;
	msg->footer.flags |= CEPH_MSG_FOOTER_SIGNED;
	return 0;
}

static int ceph_x_check_message_signature(struct ceph_auth_handshake *auth,
					  struct ceph_msg *msg)
{
	__le64 sig_check;
	int ret;

	if (ceph_test_opt(from_msgr(msg->con->msgr), NOMSGSIGN))
		return 0;

	ret = calc_signature((struct ceph_x_authorizer *)auth->authorizer,
			     msg, &sig_check);
	if (ret)
		return ret;
	if (sig_check == msg->footer.sig)
		return 0;
	if (msg->footer.flags & CEPH_MSG_FOOTER_SIGNED)
		dout("ceph_x_check_message_signature %p has signature %llx "
		     "expect %llx\n", msg, msg->footer.sig, sig_check);
	else
		dout("ceph_x_check_message_signature %p sender did not set "
		     "CEPH_MSG_FOOTER_SIGNED\n", msg);
	return -EBADMSG;
}

static const struct ceph_auth_client_ops ceph_x_ops = {
	.name = "x",
	.is_authenticated = ceph_x_is_authenticated,
	.should_authenticate = ceph_x_should_authenticate,
	.build_request = ceph_x_build_request,
	.handle_reply = ceph_x_handle_reply,
	.create_authorizer = ceph_x_create_authorizer,
	.update_authorizer = ceph_x_update_authorizer,
	.verify_authorizer_reply = ceph_x_verify_authorizer_reply,
	.invalidate_authorizer = ceph_x_invalidate_authorizer,
	.reset =  ceph_x_reset,
	.destroy = ceph_x_destroy,
	.sign_message = ceph_x_sign_message,
	.check_message_signature = ceph_x_check_message_signature,
};


int ceph_x_init(struct ceph_auth_client *ac)
{
	struct ceph_x_info *xi;
	int ret;

	dout("ceph_x_init %p\n", ac);
	ret = -ENOMEM;
	xi = kzalloc(sizeof(*xi), GFP_NOFS);
	if (!xi)
		goto out;

	ret = -EINVAL;
	if (!ac->key) {
		pr_err("no secret set (for auth_x protocol)\n");
		goto out_nomem;
	}

	ret = ceph_crypto_key_clone(&xi->secret, ac->key);
	if (ret < 0) {
		pr_err("cannot clone key: %d\n", ret);
		goto out_nomem;
	}

	xi->starting = true;
	xi->ticket_handlers = RB_ROOT;

	ac->protocol = CEPH_AUTH_CEPHX;
	ac->private = xi;
	ac->ops = &ceph_x_ops;
	return 0;

out_nomem:
	kfree(xi);
out:
	return ret;
}


