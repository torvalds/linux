// SPDX-License-Identifier: GPL-2.0

#include <linux/ceph/ceph_debug.h>

#include <linux/err.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>

#include <linux/ceph/decode.h>
#include <linux/ceph/auth.h>
#include <linux/ceph/ceph_features.h>
#include <linux/ceph/libceph.h>
#include <linux/ceph/messenger.h>

#include "crypto.h"
#include "auth_x.h"
#include "auth_x_protocol.h"

static void ceph_x_validate_tickets(struct ceph_auth_client *ac, int *pneed);

static int ceph_x_is_authenticated(struct ceph_auth_client *ac)
{
	struct ceph_x_info *xi = ac->private;
	int missing;
	int need;  /* missing + need renewal */

	ceph_x_validate_tickets(ac, &need);
	missing = ac->want_keys & ~xi->have_keys;
	WARN_ON((need & missing) != missing);
	dout("%s want 0x%x have 0x%x missing 0x%x -> %d\n", __func__,
	     ac->want_keys, xi->have_keys, missing, !missing);
	return !missing;
}

static int ceph_x_should_authenticate(struct ceph_auth_client *ac)
{
	struct ceph_x_info *xi = ac->private;
	int need;

	ceph_x_validate_tickets(ac, &need);
	dout("%s want 0x%x have 0x%x need 0x%x -> %d\n", __func__,
	     ac->want_keys, xi->have_keys, need, !!need);
	return !!need;
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

static int __ceph_x_decrypt(struct ceph_crypto_key *secret, void *p,
			    int ciphertext_len)
{
	struct ceph_x_encrypt_header *hdr = p;
	int plaintext_len;
	int ret;

	ret = ceph_crypt(secret, false, p, ciphertext_len, ciphertext_len,
			 &plaintext_len);
	if (ret)
		return ret;

	if (le64_to_cpu(hdr->magic) != CEPHX_ENC_MAGIC) {
		pr_err("%s bad magic\n", __func__);
		return -EINVAL;
	}

	return plaintext_len - sizeof(*hdr);
}

static int ceph_x_decrypt(struct ceph_crypto_key *secret, void **p, void *end)
{
	int ciphertext_len;
	int ret;

	ceph_decode_32_safe(p, end, ciphertext_len, e_inval);
	ceph_decode_need(p, end, ciphertext_len, e_inval);

	ret = __ceph_x_decrypt(secret, *p, ciphertext_len);
	if (ret < 0)
		return ret;

	*p += ciphertext_len;
	return ret;

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
	struct timespec64 validity;
	void *tp, *tpend;
	void **ptp;
	struct ceph_crypto_key new_session_key = { 0 };
	struct ceph_buffer *new_ticket_blob;
	time64_t new_expires, new_renew_after;
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

	ceph_decode_8_safe(&dp, dend, tkt_struct_v, bad);
	if (tkt_struct_v != 1)
		goto bad;

	ret = ceph_crypto_key_decode(&new_session_key, &dp, dend);
	if (ret)
		goto out;

	ceph_decode_need(&dp, dend, sizeof(struct ceph_timespec), bad);
	ceph_decode_timespec64(&validity, dp);
	dp += sizeof(struct ceph_timespec);
	new_expires = ktime_get_real_seconds() + validity.tv_sec;
	new_renew_after = new_expires - (validity.tv_sec / 4);
	dout(" expires=%llu renew_after=%llu\n", new_expires,
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
				    void **p, void *end)
{
	u8 reply_struct_v;
	u32 num;
	int ret;

	ceph_decode_8_safe(p, end, reply_struct_v, bad);
	if (reply_struct_v != 1)
		return -EINVAL;

	ceph_decode_32_safe(p, end, num, bad);
	dout("%d tickets\n", num);

	while (num--) {
		ret = process_one_ticket(ac, secret, p, end);
		if (ret)
			return ret;
	}

	return 0;

bad:
	return -EINVAL;
}

/*
 * Encode and encrypt the second part (ceph_x_authorize_b) of the
 * authorizer.  The first part (ceph_x_authorize_a) should already be
 * encoded.
 */
static int encrypt_authorizer(struct ceph_x_authorizer *au,
			      u64 *server_challenge)
{
	struct ceph_x_authorize_a *msg_a;
	struct ceph_x_authorize_b *msg_b;
	void *p, *end;
	int ret;

	msg_a = au->buf->vec.iov_base;
	WARN_ON(msg_a->ticket_blob.secret_id != cpu_to_le64(au->secret_id));
	p = (void *)(msg_a + 1) + le32_to_cpu(msg_a->ticket_blob.blob_len);
	end = au->buf->vec.iov_base + au->buf->vec.iov_len;

	msg_b = p + ceph_x_encrypt_offset();
	msg_b->struct_v = 2;
	msg_b->nonce = cpu_to_le64(au->nonce);
	if (server_challenge) {
		msg_b->have_challenge = 1;
		msg_b->server_challenge_plus_one =
		    cpu_to_le64(*server_challenge + 1);
	} else {
		msg_b->have_challenge = 0;
		msg_b->server_challenge_plus_one = 0;
	}

	ret = ceph_x_encrypt(&au->session_key, p, end - p, sizeof(*msg_b));
	if (ret < 0)
		return ret;

	p += ret;
	if (server_challenge) {
		WARN_ON(p != end);
	} else {
		WARN_ON(p > end);
		au->buf->vec.iov_len = p - au->buf->vec.iov_base;
	}

	return 0;
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
	WARN_ON(!th->secret_id);
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

	get_random_bytes(&au->nonce, sizeof(au->nonce));
	ret = encrypt_authorizer(au, NULL);
	if (ret) {
		pr_err("failed to encrypt authorizer: %d", ret);
		goto out_au;
	}

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

	return ktime_get_real_seconds() >= th->renew_after;
}

static bool have_key(struct ceph_x_ticket_handler *th)
{
	if (th->have_key && ktime_get_real_seconds() >= th->expires) {
		dout("ticket %d (%s) secret_id %llu expired\n", th->service,
		     ceph_entity_type_name(th->service), th->secret_id);
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
	void *p;
	int ret;
	struct ceph_x_ticket_handler *th =
		get_ticket_handler(ac, CEPH_ENTITY_TYPE_AUTH);

	if (IS_ERR(th))
		return PTR_ERR(th);

	ceph_x_validate_tickets(ac, &need);
	dout("%s want 0x%x have 0x%x need 0x%x\n", __func__, ac->want_keys,
	     xi->have_keys, need);

	if (need & CEPH_ENTITY_TYPE_AUTH) {
		struct ceph_x_authenticate *auth = (void *)(head + 1);
		void *enc_buf = xi->auth_authorizer.enc_buf;
		struct ceph_x_challenge_blob *blob = enc_buf +
							ceph_x_encrypt_offset();
		u64 *u;

		p = auth + 1;
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

		auth->struct_v = 2;  /* nautilus+ */
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

		/* nautilus+: request service tickets at the same time */
		need = ac->want_keys & ~CEPH_ENTITY_TYPE_AUTH;
		WARN_ON(!need);
		ceph_encode_32_safe(&p, end, need, e_range);
		return p - buf;
	}

	if (need) {
		dout(" get_principal_session_key\n");
		ret = ceph_x_build_authorizer(ac, th, &xi->auth_authorizer);
		if (ret)
			return ret;

		p = buf;
		ceph_encode_16_safe(&p, end, CEPHX_GET_PRINCIPAL_SESSION_KEY,
				    e_range);
		ceph_encode_copy_safe(&p, end,
			xi->auth_authorizer.buf->vec.iov_base,
			xi->auth_authorizer.buf->vec.iov_len, e_range);
		ceph_encode_8_safe(&p, end, 1, e_range);
		ceph_encode_32_safe(&p, end, need, e_range);
		return p - buf;
	}

	return 0;

e_range:
	return -ERANGE;
}

static int decode_con_secret(void **p, void *end, u8 *con_secret,
			     int *con_secret_len)
{
	int len;

	ceph_decode_32_safe(p, end, len, bad);
	ceph_decode_need(p, end, len, bad);

	dout("%s len %d\n", __func__, len);
	if (con_secret) {
		if (len > CEPH_MAX_CON_SECRET_LEN) {
			pr_err("connection secret too big %d\n", len);
			goto bad_memzero;
		}
		memcpy(con_secret, *p, len);
		*con_secret_len = len;
	}
	memzero_explicit(*p, len);
	*p += len;
	return 0;

bad_memzero:
	memzero_explicit(*p, len);
bad:
	pr_err("failed to decode connection secret\n");
	return -EINVAL;
}

static int handle_auth_session_key(struct ceph_auth_client *ac,
				   void **p, void *end,
				   u8 *session_key, int *session_key_len,
				   u8 *con_secret, int *con_secret_len)
{
	struct ceph_x_info *xi = ac->private;
	struct ceph_x_ticket_handler *th;
	void *dp, *dend;
	int len;
	int ret;

	/* AUTH ticket */
	ret = ceph_x_proc_ticket_reply(ac, &xi->secret, p, end);
	if (ret)
		return ret;

	if (*p == end) {
		/* pre-nautilus (or didn't request service tickets!) */
		WARN_ON(session_key || con_secret);
		return 0;
	}

	th = get_ticket_handler(ac, CEPH_ENTITY_TYPE_AUTH);
	if (IS_ERR(th))
		return PTR_ERR(th);

	if (session_key) {
		memcpy(session_key, th->session_key.key, th->session_key.len);
		*session_key_len = th->session_key.len;
	}

	/* connection secret */
	ceph_decode_32_safe(p, end, len, e_inval);
	dout("%s connection secret blob len %d\n", __func__, len);
	if (len > 0) {
		dp = *p + ceph_x_encrypt_offset();
		ret = ceph_x_decrypt(&th->session_key, p, *p + len);
		if (ret < 0)
			return ret;

		dout("%s decrypted %d bytes\n", __func__, ret);
		dend = dp + ret;

		ret = decode_con_secret(&dp, dend, con_secret, con_secret_len);
		if (ret)
			return ret;
	}

	/* service tickets */
	ceph_decode_32_safe(p, end, len, e_inval);
	dout("%s service tickets blob len %d\n", __func__, len);
	if (len > 0) {
		ret = ceph_x_proc_ticket_reply(ac, &th->session_key,
					       p, *p + len);
		if (ret)
			return ret;
	}

	return 0;

e_inval:
	return -EINVAL;
}

static int ceph_x_handle_reply(struct ceph_auth_client *ac, int result,
			       void *buf, void *end,
			       u8 *session_key, int *session_key_len,
			       u8 *con_secret, int *con_secret_len)
{
	struct ceph_x_info *xi = ac->private;
	struct ceph_x_ticket_handler *th;
	int len = end - buf;
	void *p;
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

	p = buf;
	ceph_decode_16_safe(&p, end, op, e_inval);
	ceph_decode_32_safe(&p, end, result, e_inval);
	dout("handle_reply op %d result %d\n", op, result);
	switch (op) {
	case CEPHX_GET_AUTH_SESSION_KEY:
		/* AUTH ticket + [connection secret] + service tickets */
		ret = handle_auth_session_key(ac, &p, end, session_key,
					      session_key_len, con_secret,
					      con_secret_len);
		break;

	case CEPHX_GET_PRINCIPAL_SESSION_KEY:
		th = get_ticket_handler(ac, CEPH_ENTITY_TYPE_AUTH);
		if (IS_ERR(th))
			return PTR_ERR(th);

		/* service tickets */
		ret = ceph_x_proc_ticket_reply(ac, &th->session_key, &p, end);
		break;

	default:
		return -EINVAL;
	}
	if (ret)
		return ret;
	if (ac->want_keys == xi->have_keys)
		return 0;
	return -EAGAIN;

e_inval:
	return -EINVAL;
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

/*
 * CephXAuthorizeChallenge
 */
static int decrypt_authorizer_challenge(struct ceph_crypto_key *secret,
					void *challenge, int challenge_len,
					u64 *server_challenge)
{
	void *dp, *dend;
	int ret;

	/* no leading len */
	ret = __ceph_x_decrypt(secret, challenge, challenge_len);
	if (ret < 0)
		return ret;

	dout("%s decrypted %d bytes\n", __func__, ret);
	dp = challenge + sizeof(struct ceph_x_encrypt_header);
	dend = dp + ret;

	ceph_decode_skip_8(&dp, dend, e_inval);  /* struct_v */
	ceph_decode_64_safe(&dp, dend, *server_challenge, e_inval);
	dout("%s server_challenge %llu\n", __func__, *server_challenge);
	return 0;

e_inval:
	return -EINVAL;
}

static int ceph_x_add_authorizer_challenge(struct ceph_auth_client *ac,
					   struct ceph_authorizer *a,
					   void *challenge, int challenge_len)
{
	struct ceph_x_authorizer *au = (void *)a;
	u64 server_challenge;
	int ret;

	ret = decrypt_authorizer_challenge(&au->session_key, challenge,
					   challenge_len, &server_challenge);
	if (ret) {
		pr_err("failed to decrypt authorize challenge: %d", ret);
		return ret;
	}

	ret = encrypt_authorizer(au, &server_challenge);
	if (ret) {
		pr_err("failed to encrypt authorizer w/ challenge: %d", ret);
		return ret;
	}

	return 0;
}

/*
 * CephXAuthorizeReply
 */
static int decrypt_authorizer_reply(struct ceph_crypto_key *secret,
				    void **p, void *end, u64 *nonce_plus_one,
				    u8 *con_secret, int *con_secret_len)
{
	void *dp, *dend;
	u8 struct_v;
	int ret;

	dp = *p + ceph_x_encrypt_offset();
	ret = ceph_x_decrypt(secret, p, end);
	if (ret < 0)
		return ret;

	dout("%s decrypted %d bytes\n", __func__, ret);
	dend = dp + ret;

	ceph_decode_8_safe(&dp, dend, struct_v, e_inval);
	ceph_decode_64_safe(&dp, dend, *nonce_plus_one, e_inval);
	dout("%s nonce_plus_one %llu\n", __func__, *nonce_plus_one);
	if (struct_v >= 2) {
		ret = decode_con_secret(&dp, dend, con_secret, con_secret_len);
		if (ret)
			return ret;
	}

	return 0;

e_inval:
	return -EINVAL;
}

static int ceph_x_verify_authorizer_reply(struct ceph_auth_client *ac,
					  struct ceph_authorizer *a,
					  void *reply, int reply_len,
					  u8 *session_key, int *session_key_len,
					  u8 *con_secret, int *con_secret_len)
{
	struct ceph_x_authorizer *au = (void *)a;
	u64 nonce_plus_one;
	int ret;

	if (session_key) {
		memcpy(session_key, au->session_key.key, au->session_key.len);
		*session_key_len = au->session_key.len;
	}

	ret = decrypt_authorizer_reply(&au->session_key, &reply,
				       reply + reply_len, &nonce_plus_one,
				       con_secret, con_secret_len);
	if (ret)
		return ret;

	if (nonce_plus_one != au->nonce + 1) {
		pr_err("failed to authenticate server\n");
		return -EPERM;
	}

	return 0;
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
	if (IS_ERR(th))
		return;

	if (th->have_key) {
		dout("ticket %d (%s) secret_id %llu invalidated\n",
		     th->service, ceph_entity_type_name(th->service),
		     th->secret_id);
		th->have_key = false;
	}
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
	int ret;

	if (!CEPH_HAVE_FEATURE(msg->con->peer_features, CEPHX_V2)) {
		struct {
			__le32 len;
			__le32 header_crc;
			__le32 front_crc;
			__le32 middle_crc;
			__le32 data_crc;
		} __packed *sigblock = enc_buf + ceph_x_encrypt_offset();

		sigblock->len = cpu_to_le32(4*sizeof(u32));
		sigblock->header_crc = msg->hdr.crc;
		sigblock->front_crc = msg->footer.front_crc;
		sigblock->middle_crc = msg->footer.middle_crc;
		sigblock->data_crc =  msg->footer.data_crc;

		ret = ceph_x_encrypt(&au->session_key, enc_buf,
				     CEPHX_AU_ENC_BUF_LEN, sizeof(*sigblock));
		if (ret < 0)
			return ret;

		*psig = *(__le64 *)(enc_buf + sizeof(u32));
	} else {
		struct {
			__le32 header_crc;
			__le32 front_crc;
			__le32 front_len;
			__le32 middle_crc;
			__le32 middle_len;
			__le32 data_crc;
			__le32 data_len;
			__le32 seq_lower_word;
		} __packed *sigblock = enc_buf;
		struct {
			__le64 a, b, c, d;
		} __packed *penc = enc_buf;
		int ciphertext_len;

		sigblock->header_crc = msg->hdr.crc;
		sigblock->front_crc = msg->footer.front_crc;
		sigblock->front_len = msg->hdr.front_len;
		sigblock->middle_crc = msg->footer.middle_crc;
		sigblock->middle_len = msg->hdr.middle_len;
		sigblock->data_crc =  msg->footer.data_crc;
		sigblock->data_len = msg->hdr.data_len;
		sigblock->seq_lower_word = *(__le32 *)&msg->hdr.seq;

		/* no leading len, no ceph_x_encrypt_header */
		ret = ceph_crypt(&au->session_key, true, enc_buf,
				 CEPHX_AU_ENC_BUF_LEN, sizeof(*sigblock),
				 &ciphertext_len);
		if (ret)
			return ret;

		*psig = penc->a ^ penc->b ^ penc->c ^ penc->d;
	}

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
	.is_authenticated = ceph_x_is_authenticated,
	.should_authenticate = ceph_x_should_authenticate,
	.build_request = ceph_x_build_request,
	.handle_reply = ceph_x_handle_reply,
	.create_authorizer = ceph_x_create_authorizer,
	.update_authorizer = ceph_x_update_authorizer,
	.add_authorizer_challenge = ceph_x_add_authorizer_challenge,
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
