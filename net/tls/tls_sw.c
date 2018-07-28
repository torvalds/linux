/*
 * Copyright (c) 2016-2017, Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016-2017, Dave Watson <davejwatson@fb.com>. All rights reserved.
 * Copyright (c) 2016-2017, Lance Chao <lancerchao@fb.com>. All rights reserved.
 * Copyright (c) 2016, Fridolin Pokorny <fridolin.pokorny@gmail.com>. All rights reserved.
 * Copyright (c) 2016, Nikos Mavrogiannopoulos <nmav@gnutls.org>. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <crypto/aead.h>

#include <net/tls.h>

static inline void tls_make_aad(int recv,
				char *buf,
				size_t size,
				char *record_sequence,
				int record_sequence_size,
				unsigned char record_type)
{
	memcpy(buf, record_sequence, record_sequence_size);

	buf[8] = record_type;
	buf[9] = TLS_1_2_VERSION_MAJOR;
	buf[10] = TLS_1_2_VERSION_MINOR;
	buf[11] = size >> 8;
	buf[12] = size & 0xFF;
}

static void trim_sg(struct sock *sk, struct scatterlist *sg,
		    int *sg_num_elem, unsigned int *sg_size, int target_size)
{
	int i = *sg_num_elem - 1;
	int trim = *sg_size - target_size;

	if (trim <= 0) {
		WARN_ON(trim < 0);
		return;
	}

	*sg_size = target_size;
	while (trim >= sg[i].length) {
		trim -= sg[i].length;
		sk_mem_uncharge(sk, sg[i].length);
		put_page(sg_page(&sg[i]));
		i--;

		if (i < 0)
			goto out;
	}

	sg[i].length -= trim;
	sk_mem_uncharge(sk, trim);

out:
	*sg_num_elem = i + 1;
}

static void trim_both_sgl(struct sock *sk, int target_size)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);

	trim_sg(sk, ctx->sg_plaintext_data,
		&ctx->sg_plaintext_num_elem,
		&ctx->sg_plaintext_size,
		target_size);

	if (target_size > 0)
		target_size += tls_ctx->overhead_size;

	trim_sg(sk, ctx->sg_encrypted_data,
		&ctx->sg_encrypted_num_elem,
		&ctx->sg_encrypted_size,
		target_size);
}

static int alloc_sg(struct sock *sk, int len, struct scatterlist *sg,
		    int *sg_num_elem, unsigned int *sg_size,
		    int first_coalesce)
{
	struct page_frag *pfrag;
	unsigned int size = *sg_size;
	int num_elem = *sg_num_elem, use = 0, rc = 0;
	struct scatterlist *sge;
	unsigned int orig_offset;

	len -= size;
	pfrag = sk_page_frag(sk);

	while (len > 0) {
		if (!sk_page_frag_refill(sk, pfrag)) {
			rc = -ENOMEM;
			goto out;
		}

		use = min_t(int, len, pfrag->size - pfrag->offset);

		if (!sk_wmem_schedule(sk, use)) {
			rc = -ENOMEM;
			goto out;
		}

		sk_mem_charge(sk, use);
		size += use;
		orig_offset = pfrag->offset;
		pfrag->offset += use;

		sge = sg + num_elem - 1;

		if (num_elem > first_coalesce && sg_page(sge) == pfrag->page &&
		    sge->offset + sge->length == orig_offset) {
			sge->length += use;
		} else {
			sge++;
			sg_unmark_end(sge);
			sg_set_page(sge, pfrag->page, use, orig_offset);
			get_page(pfrag->page);
			++num_elem;
			if (num_elem == MAX_SKB_FRAGS) {
				rc = -ENOSPC;
				break;
			}
		}

		len -= use;
	}
	goto out;

out:
	*sg_size = size;
	*sg_num_elem = num_elem;
	return rc;
}

static int alloc_encrypted_sg(struct sock *sk, int len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);
	int rc = 0;

	rc = alloc_sg(sk, len, ctx->sg_encrypted_data,
		      &ctx->sg_encrypted_num_elem, &ctx->sg_encrypted_size, 0);

	return rc;
}

static int alloc_plaintext_sg(struct sock *sk, int len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);
	int rc = 0;

	rc = alloc_sg(sk, len, ctx->sg_plaintext_data,
		      &ctx->sg_plaintext_num_elem, &ctx->sg_plaintext_size,
		      tls_ctx->pending_open_record_frags);

	return rc;
}

static void free_sg(struct sock *sk, struct scatterlist *sg,
		    int *sg_num_elem, unsigned int *sg_size)
{
	int i, n = *sg_num_elem;

	for (i = 0; i < n; ++i) {
		sk_mem_uncharge(sk, sg[i].length);
		put_page(sg_page(&sg[i]));
	}
	*sg_num_elem = 0;
	*sg_size = 0;
}

static void tls_free_both_sg(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);

	free_sg(sk, ctx->sg_encrypted_data, &ctx->sg_encrypted_num_elem,
		&ctx->sg_encrypted_size);

	free_sg(sk, ctx->sg_plaintext_data, &ctx->sg_plaintext_num_elem,
		&ctx->sg_plaintext_size);
}

static int tls_do_encryption(struct tls_context *tls_ctx,
			     struct tls_sw_context *ctx,
			     struct aead_request *aead_req,
			     size_t data_len)
{
	int rc;

	ctx->sg_encrypted_data[0].offset += tls_ctx->prepend_size;
	ctx->sg_encrypted_data[0].length -= tls_ctx->prepend_size;

	aead_request_set_tfm(aead_req, ctx->aead_send);
	aead_request_set_ad(aead_req, TLS_AAD_SPACE_SIZE);
	aead_request_set_crypt(aead_req, ctx->sg_aead_in, ctx->sg_aead_out,
			       data_len, tls_ctx->iv);
	rc = crypto_aead_encrypt(aead_req);

	ctx->sg_encrypted_data[0].offset -= tls_ctx->prepend_size;
	ctx->sg_encrypted_data[0].length += tls_ctx->prepend_size;

	return rc;
}

static int tls_push_record(struct sock *sk, int flags,
			   unsigned char record_type)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);
	struct aead_request *req;
	int rc;

	req = kzalloc(sizeof(struct aead_request) +
		      crypto_aead_reqsize(ctx->aead_send), sk->sk_allocation);
	if (!req)
		return -ENOMEM;

	sg_mark_end(ctx->sg_plaintext_data + ctx->sg_plaintext_num_elem - 1);
	sg_mark_end(ctx->sg_encrypted_data + ctx->sg_encrypted_num_elem - 1);

	tls_make_aad(0, ctx->aad_space, ctx->sg_plaintext_size,
		     tls_ctx->rec_seq, tls_ctx->rec_seq_size,
		     record_type);

	tls_fill_prepend(tls_ctx,
			 page_address(sg_page(&ctx->sg_encrypted_data[0])) +
			 ctx->sg_encrypted_data[0].offset,
			 ctx->sg_plaintext_size, record_type);

	tls_ctx->pending_open_record_frags = 0;
	set_bit(TLS_PENDING_CLOSED_RECORD, &tls_ctx->flags);

	rc = tls_do_encryption(tls_ctx, ctx, req, ctx->sg_plaintext_size);
	if (rc < 0) {
		/* If we are called from write_space and
		 * we fail, we need to set this SOCK_NOSPACE
		 * to trigger another write_space in the future.
		 */
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
		goto out_req;
	}

	free_sg(sk, ctx->sg_plaintext_data, &ctx->sg_plaintext_num_elem,
		&ctx->sg_plaintext_size);

	ctx->sg_encrypted_num_elem = 0;
	ctx->sg_encrypted_size = 0;

	/* Only pass through MSG_DONTWAIT and MSG_NOSIGNAL flags */
	rc = tls_push_sg(sk, tls_ctx, ctx->sg_encrypted_data, 0, flags);
	if (rc < 0 && rc != -EAGAIN)
		tls_err_abort(sk);

	tls_advance_record_sn(sk, tls_ctx);
out_req:
	kfree(req);
	return rc;
}

static int tls_sw_push_pending_record(struct sock *sk, int flags)
{
	return tls_push_record(sk, flags, TLS_RECORD_TYPE_DATA);
}

static int zerocopy_from_iter(struct sock *sk, struct iov_iter *from,
			      int length)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);
	struct page *pages[MAX_SKB_FRAGS];

	size_t offset;
	ssize_t copied, use;
	int i = 0;
	unsigned int size = ctx->sg_plaintext_size;
	int num_elem = ctx->sg_plaintext_num_elem;
	int rc = 0;
	int maxpages;

	while (length > 0) {
		i = 0;
		maxpages = ARRAY_SIZE(ctx->sg_plaintext_data) - num_elem;
		if (maxpages == 0) {
			rc = -EFAULT;
			goto out;
		}
		copied = iov_iter_get_pages(from, pages,
					    length,
					    maxpages, &offset);
		if (copied <= 0) {
			rc = -EFAULT;
			goto out;
		}

		iov_iter_advance(from, copied);

		length -= copied;
		size += copied;
		while (copied) {
			use = min_t(int, copied, PAGE_SIZE - offset);

			sg_set_page(&ctx->sg_plaintext_data[num_elem],
				    pages[i], use, offset);
			sg_unmark_end(&ctx->sg_plaintext_data[num_elem]);
			sk_mem_charge(sk, use);

			offset = 0;
			copied -= use;

			++i;
			++num_elem;
		}
	}

out:
	ctx->sg_plaintext_size = size;
	ctx->sg_plaintext_num_elem = num_elem;
	return rc;
}

static int memcopy_from_iter(struct sock *sk, struct iov_iter *from,
			     int bytes)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);
	struct scatterlist *sg = ctx->sg_plaintext_data;
	int copy, i, rc = 0;

	for (i = tls_ctx->pending_open_record_frags;
	     i < ctx->sg_plaintext_num_elem; ++i) {
		copy = sg[i].length;
		if (copy_from_iter(
				page_address(sg_page(&sg[i])) + sg[i].offset,
				copy, from) != copy) {
			rc = -EFAULT;
			goto out;
		}
		bytes -= copy;

		++tls_ctx->pending_open_record_frags;

		if (!bytes)
			break;
	}

out:
	return rc;
}

int tls_sw_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);
	int ret = 0;
	int required_size;
	long timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	bool eor = !(msg->msg_flags & MSG_MORE);
	size_t try_to_copy, copied = 0;
	unsigned char record_type = TLS_RECORD_TYPE_DATA;
	int record_room;
	bool full_record;
	int orig_size;

	if (msg->msg_flags & ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL))
		return -ENOTSUPP;

	lock_sock(sk);

	if (tls_complete_pending_work(sk, tls_ctx, msg->msg_flags, &timeo))
		goto send_end;

	if (unlikely(msg->msg_controllen)) {
		ret = tls_proccess_cmsg(sk, msg, &record_type);
		if (ret)
			goto send_end;
	}

	while (msg_data_left(msg)) {
		if (sk->sk_err) {
			ret = -sk->sk_err;
			goto send_end;
		}

		orig_size = ctx->sg_plaintext_size;
		full_record = false;
		try_to_copy = msg_data_left(msg);
		record_room = TLS_MAX_PAYLOAD_SIZE - ctx->sg_plaintext_size;
		if (try_to_copy >= record_room) {
			try_to_copy = record_room;
			full_record = true;
		}

		required_size = ctx->sg_plaintext_size + try_to_copy +
				tls_ctx->overhead_size;

		if (!sk_stream_memory_free(sk))
			goto wait_for_sndbuf;
alloc_encrypted:
		ret = alloc_encrypted_sg(sk, required_size);
		if (ret) {
			if (ret != -ENOSPC)
				goto wait_for_memory;

			/* Adjust try_to_copy according to the amount that was
			 * actually allocated. The difference is due
			 * to max sg elements limit
			 */
			try_to_copy -= required_size - ctx->sg_encrypted_size;
			full_record = true;
		}

		if (full_record || eor) {
			ret = zerocopy_from_iter(sk, &msg->msg_iter,
						 try_to_copy);
			if (ret)
				goto fallback_to_reg_send;

			copied += try_to_copy;
			ret = tls_push_record(sk, msg->msg_flags, record_type);
			if (!ret)
				continue;
			if (ret < 0)
				goto send_end;

			copied -= try_to_copy;
fallback_to_reg_send:
			iov_iter_revert(&msg->msg_iter,
					ctx->sg_plaintext_size - orig_size);
			trim_sg(sk, ctx->sg_plaintext_data,
				&ctx->sg_plaintext_num_elem,
				&ctx->sg_plaintext_size,
				orig_size);
		}

		required_size = ctx->sg_plaintext_size + try_to_copy;
alloc_plaintext:
		ret = alloc_plaintext_sg(sk, required_size);
		if (ret) {
			if (ret != -ENOSPC)
				goto wait_for_memory;

			/* Adjust try_to_copy according to the amount that was
			 * actually allocated. The difference is due
			 * to max sg elements limit
			 */
			try_to_copy -= required_size - ctx->sg_plaintext_size;
			full_record = true;

			trim_sg(sk, ctx->sg_encrypted_data,
				&ctx->sg_encrypted_num_elem,
				&ctx->sg_encrypted_size,
				ctx->sg_plaintext_size +
				tls_ctx->overhead_size);
		}

		ret = memcopy_from_iter(sk, &msg->msg_iter, try_to_copy);
		if (ret)
			goto trim_sgl;

		copied += try_to_copy;
		if (full_record || eor) {
push_record:
			ret = tls_push_record(sk, msg->msg_flags, record_type);
			if (ret) {
				if (ret == -ENOMEM)
					goto wait_for_memory;

				goto send_end;
			}
		}

		continue;

wait_for_sndbuf:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
wait_for_memory:
		ret = sk_stream_wait_memory(sk, &timeo);
		if (ret) {
trim_sgl:
			trim_both_sgl(sk, orig_size);
			goto send_end;
		}

		if (tls_is_pending_closed_record(tls_ctx))
			goto push_record;

		if (ctx->sg_encrypted_size < required_size)
			goto alloc_encrypted;

		goto alloc_plaintext;
	}

send_end:
	ret = sk_stream_error(sk, msg->msg_flags, ret);

	release_sock(sk);
	return copied ? copied : ret;
}

int tls_sw_sendpage(struct sock *sk, struct page *page,
		    int offset, size_t size, int flags)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);
	int ret = 0;
	long timeo = sock_sndtimeo(sk, flags & MSG_DONTWAIT);
	bool eor;
	size_t orig_size = size;
	unsigned char record_type = TLS_RECORD_TYPE_DATA;
	struct scatterlist *sg;
	bool full_record;
	int record_room;

	if (flags & ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL |
		      MSG_SENDPAGE_NOTLAST))
		return -ENOTSUPP;

	/* No MSG_EOR from splice, only look at MSG_MORE */
	eor = !(flags & (MSG_MORE | MSG_SENDPAGE_NOTLAST));

	lock_sock(sk);

	sk_clear_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	if (tls_complete_pending_work(sk, tls_ctx, flags, &timeo))
		goto sendpage_end;

	/* Call the sk_stream functions to manage the sndbuf mem. */
	while (size > 0) {
		size_t copy, required_size;

		if (sk->sk_err) {
			ret = -sk->sk_err;
			goto sendpage_end;
		}

		full_record = false;
		record_room = TLS_MAX_PAYLOAD_SIZE - ctx->sg_plaintext_size;
		copy = size;
		if (copy >= record_room) {
			copy = record_room;
			full_record = true;
		}
		required_size = ctx->sg_plaintext_size + copy +
			      tls_ctx->overhead_size;

		if (!sk_stream_memory_free(sk))
			goto wait_for_sndbuf;
alloc_payload:
		ret = alloc_encrypted_sg(sk, required_size);
		if (ret) {
			if (ret != -ENOSPC)
				goto wait_for_memory;

			/* Adjust copy according to the amount that was
			 * actually allocated. The difference is due
			 * to max sg elements limit
			 */
			copy -= required_size - ctx->sg_plaintext_size;
			full_record = true;
		}

		get_page(page);
		sg = ctx->sg_plaintext_data + ctx->sg_plaintext_num_elem;
		sg_set_page(sg, page, copy, offset);
		ctx->sg_plaintext_num_elem++;

		sk_mem_charge(sk, copy);
		offset += copy;
		size -= copy;
		ctx->sg_plaintext_size += copy;
		tls_ctx->pending_open_record_frags = ctx->sg_plaintext_num_elem;

		if (full_record || eor ||
		    ctx->sg_plaintext_num_elem ==
		    ARRAY_SIZE(ctx->sg_plaintext_data)) {
push_record:
			ret = tls_push_record(sk, flags, record_type);
			if (ret) {
				if (ret == -ENOMEM)
					goto wait_for_memory;

				goto sendpage_end;
			}
		}
		continue;
wait_for_sndbuf:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
wait_for_memory:
		ret = sk_stream_wait_memory(sk, &timeo);
		if (ret) {
			trim_both_sgl(sk, ctx->sg_plaintext_size);
			goto sendpage_end;
		}

		if (tls_is_pending_closed_record(tls_ctx))
			goto push_record;

		goto alloc_payload;
	}

sendpage_end:
	if (orig_size > size)
		ret = orig_size - size;
	else
		ret = sk_stream_error(sk, flags, ret);

	release_sock(sk);
	return ret;
}

static void tls_sw_free_resources(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context *ctx = tls_sw_ctx(tls_ctx);

	if (ctx->aead_send)
		crypto_free_aead(ctx->aead_send);

	tls_free_both_sg(sk);

	kfree(ctx);
}

int tls_set_sw_offload(struct sock *sk, struct tls_context *ctx)
{
	char keyval[TLS_CIPHER_AES_GCM_128_KEY_SIZE];
	struct tls_crypto_info *crypto_info;
	struct tls12_crypto_info_aes_gcm_128 *gcm_128_info;
	struct tls_sw_context *sw_ctx;
	u16 nonce_size, tag_size, iv_size, rec_seq_size;
	char *iv, *rec_seq;
	int rc = 0;

	if (!ctx) {
		rc = -EINVAL;
		goto out;
	}

	if (ctx->priv_ctx) {
		rc = -EEXIST;
		goto out;
	}

	sw_ctx = kzalloc(sizeof(*sw_ctx), GFP_KERNEL);
	if (!sw_ctx) {
		rc = -ENOMEM;
		goto out;
	}

	ctx->priv_ctx = (struct tls_offload_context *)sw_ctx;
	ctx->free_resources = tls_sw_free_resources;

	crypto_info = &ctx->crypto_send;
	switch (crypto_info->cipher_type) {
	case TLS_CIPHER_AES_GCM_128: {
		nonce_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
		tag_size = TLS_CIPHER_AES_GCM_128_TAG_SIZE;
		iv_size = TLS_CIPHER_AES_GCM_128_IV_SIZE;
		iv = ((struct tls12_crypto_info_aes_gcm_128 *)crypto_info)->iv;
		rec_seq_size = TLS_CIPHER_AES_GCM_128_REC_SEQ_SIZE;
		rec_seq =
		 ((struct tls12_crypto_info_aes_gcm_128 *)crypto_info)->rec_seq;
		gcm_128_info =
			(struct tls12_crypto_info_aes_gcm_128 *)crypto_info;
		break;
	}
	default:
		rc = -EINVAL;
		goto free_priv;
	}

	ctx->prepend_size = TLS_HEADER_SIZE + nonce_size;
	ctx->tag_size = tag_size;
	ctx->overhead_size = ctx->prepend_size + ctx->tag_size;
	ctx->iv_size = iv_size;
	ctx->iv = kmalloc(iv_size + TLS_CIPHER_AES_GCM_128_SALT_SIZE, GFP_KERNEL);
	if (!ctx->iv) {
		rc = -ENOMEM;
		goto free_priv;
	}
	memcpy(ctx->iv, gcm_128_info->salt, TLS_CIPHER_AES_GCM_128_SALT_SIZE);
	memcpy(ctx->iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE, iv, iv_size);
	ctx->rec_seq_size = rec_seq_size;
	ctx->rec_seq = kmalloc(rec_seq_size, GFP_KERNEL);
	if (!ctx->rec_seq) {
		rc = -ENOMEM;
		goto free_iv;
	}
	memcpy(ctx->rec_seq, rec_seq, rec_seq_size);

	sg_init_table(sw_ctx->sg_encrypted_data,
		      ARRAY_SIZE(sw_ctx->sg_encrypted_data));
	sg_init_table(sw_ctx->sg_plaintext_data,
		      ARRAY_SIZE(sw_ctx->sg_plaintext_data));

	sg_init_table(sw_ctx->sg_aead_in, 2);
	sg_set_buf(&sw_ctx->sg_aead_in[0], sw_ctx->aad_space,
		   sizeof(sw_ctx->aad_space));
	sg_unmark_end(&sw_ctx->sg_aead_in[1]);
	sg_chain(sw_ctx->sg_aead_in, 2, sw_ctx->sg_plaintext_data);
	sg_init_table(sw_ctx->sg_aead_out, 2);
	sg_set_buf(&sw_ctx->sg_aead_out[0], sw_ctx->aad_space,
		   sizeof(sw_ctx->aad_space));
	sg_unmark_end(&sw_ctx->sg_aead_out[1]);
	sg_chain(sw_ctx->sg_aead_out, 2, sw_ctx->sg_encrypted_data);

	if (!sw_ctx->aead_send) {
		sw_ctx->aead_send = crypto_alloc_aead("gcm(aes)", 0, 0);
		if (IS_ERR(sw_ctx->aead_send)) {
			rc = PTR_ERR(sw_ctx->aead_send);
			sw_ctx->aead_send = NULL;
			goto free_rec_seq;
		}
	}

	ctx->push_pending_record = tls_sw_push_pending_record;

	memcpy(keyval, gcm_128_info->key, TLS_CIPHER_AES_GCM_128_KEY_SIZE);

	rc = crypto_aead_setkey(sw_ctx->aead_send, keyval,
				TLS_CIPHER_AES_GCM_128_KEY_SIZE);
	if (rc)
		goto free_aead;

	rc = crypto_aead_setauthsize(sw_ctx->aead_send, ctx->tag_size);
	if (!rc)
		return 0;

free_aead:
	crypto_free_aead(sw_ctx->aead_send);
	sw_ctx->aead_send = NULL;
free_rec_seq:
	kfree(ctx->rec_seq);
	ctx->rec_seq = NULL;
free_iv:
	kfree(ctx->iv);
	ctx->iv = NULL;
free_priv:
	kfree(ctx->priv_ctx);
	ctx->priv_ctx = NULL;
out:
	return rc;
}
