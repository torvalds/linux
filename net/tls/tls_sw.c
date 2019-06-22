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

#include <linux/sched/signal.h>
#include <linux/module.h>
#include <crypto/aead.h>

#include <net/strparser.h>
#include <net/tls.h>

#define MAX_IV_SIZE	TLS_CIPHER_AES_GCM_128_IV_SIZE

static int tls_do_decryption(struct sock *sk,
			     struct scatterlist *sgin,
			     struct scatterlist *sgout,
			     char *iv_recv,
			     size_t data_len,
			     struct aead_request *aead_req)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	int ret;

	aead_request_set_tfm(aead_req, ctx->aead_recv);
	aead_request_set_ad(aead_req, TLS_AAD_SPACE_SIZE);
	aead_request_set_crypt(aead_req, sgin, sgout,
			       data_len + tls_ctx->rx.tag_size,
			       (u8 *)iv_recv);
	aead_request_set_callback(aead_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  crypto_req_done, &ctx->async_wait);

	ret = crypto_wait_req(crypto_aead_decrypt(aead_req), &ctx->async_wait);
	return ret;
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
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);

	trim_sg(sk, ctx->sg_plaintext_data,
		&ctx->sg_plaintext_num_elem,
		&ctx->sg_plaintext_size,
		target_size);

	if (target_size > 0)
		target_size += tls_ctx->tx.overhead_size;

	trim_sg(sk, ctx->sg_encrypted_data,
		&ctx->sg_encrypted_num_elem,
		&ctx->sg_encrypted_size,
		target_size);
}

static int alloc_encrypted_sg(struct sock *sk, int len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	int rc = 0;

	rc = sk_alloc_sg(sk, len,
			 ctx->sg_encrypted_data, 0,
			 &ctx->sg_encrypted_num_elem,
			 &ctx->sg_encrypted_size, 0);

	if (rc == -ENOSPC)
		ctx->sg_encrypted_num_elem = ARRAY_SIZE(ctx->sg_encrypted_data);

	return rc;
}

static int alloc_plaintext_sg(struct sock *sk, int len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	int rc = 0;

	rc = sk_alloc_sg(sk, len, ctx->sg_plaintext_data, 0,
			 &ctx->sg_plaintext_num_elem, &ctx->sg_plaintext_size,
			 tls_ctx->pending_open_record_frags);

	if (rc == -ENOSPC)
		ctx->sg_plaintext_num_elem = ARRAY_SIZE(ctx->sg_plaintext_data);

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
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);

	free_sg(sk, ctx->sg_encrypted_data, &ctx->sg_encrypted_num_elem,
		&ctx->sg_encrypted_size);

	free_sg(sk, ctx->sg_plaintext_data, &ctx->sg_plaintext_num_elem,
		&ctx->sg_plaintext_size);
}

static int tls_do_encryption(struct tls_context *tls_ctx,
			     struct tls_sw_context_tx *ctx,
			     struct aead_request *aead_req,
			     size_t data_len)
{
	int rc;

	ctx->sg_encrypted_data[0].offset += tls_ctx->tx.prepend_size;
	ctx->sg_encrypted_data[0].length -= tls_ctx->tx.prepend_size;

	aead_request_set_tfm(aead_req, ctx->aead_send);
	aead_request_set_ad(aead_req, TLS_AAD_SPACE_SIZE);
	aead_request_set_crypt(aead_req, ctx->sg_aead_in, ctx->sg_aead_out,
			       data_len, tls_ctx->tx.iv);

	aead_request_set_callback(aead_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  crypto_req_done, &ctx->async_wait);

	rc = crypto_wait_req(crypto_aead_encrypt(aead_req), &ctx->async_wait);

	ctx->sg_encrypted_data[0].offset -= tls_ctx->tx.prepend_size;
	ctx->sg_encrypted_data[0].length += tls_ctx->tx.prepend_size;

	return rc;
}

static int tls_push_record(struct sock *sk, int flags,
			   unsigned char record_type)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct aead_request *req;
	int rc;

	req = aead_request_alloc(ctx->aead_send, sk->sk_allocation);
	if (!req)
		return -ENOMEM;

	sg_mark_end(ctx->sg_plaintext_data + ctx->sg_plaintext_num_elem - 1);
	sg_mark_end(ctx->sg_encrypted_data + ctx->sg_encrypted_num_elem - 1);

	tls_make_aad(ctx->aad_space, ctx->sg_plaintext_size,
		     tls_ctx->tx.rec_seq, tls_ctx->tx.rec_seq_size,
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
		tls_err_abort(sk, EBADMSG);

	tls_advance_record_sn(sk, &tls_ctx->tx);
out_req:
	aead_request_free(req);
	return rc;
}

static int tls_sw_push_pending_record(struct sock *sk, int flags)
{
	return tls_push_record(sk, flags, TLS_RECORD_TYPE_DATA);
}

static int zerocopy_from_iter(struct sock *sk, struct iov_iter *from,
			      int length, int *pages_used,
			      unsigned int *size_used,
			      struct scatterlist *to, int to_max_pages,
			      bool charge)
{
	struct page *pages[MAX_SKB_FRAGS];

	size_t offset;
	ssize_t copied, use;
	int i = 0;
	unsigned int size = *size_used;
	int num_elem = *pages_used;
	int rc = 0;
	int maxpages;

	while (length > 0) {
		i = 0;
		maxpages = to_max_pages - num_elem;
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

			sg_set_page(&to[num_elem],
				    pages[i], use, offset);
			sg_unmark_end(&to[num_elem]);
			if (charge)
				sk_mem_charge(sk, use);

			offset = 0;
			copied -= use;

			++i;
			++num_elem;
		}
	}

	/* Mark the end in the last sg entry if newly added */
	if (num_elem > *pages_used)
		sg_mark_end(&to[num_elem - 1]);
out:
	if (rc)
		iov_iter_revert(from, size - *size_used);
	*size_used = size;
	*pages_used = num_elem;

	return rc;
}

static int memcopy_from_iter(struct sock *sk, struct iov_iter *from,
			     int bytes)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
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
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	int ret = 0;
	int required_size;
	long timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	bool eor = !(msg->msg_flags & MSG_MORE);
	size_t try_to_copy, copied = 0;
	unsigned char record_type = TLS_RECORD_TYPE_DATA;
	int record_room;
	bool full_record;
	int orig_size;
	bool is_kvec = msg->msg_iter.type & ITER_KVEC;

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
				tls_ctx->tx.overhead_size;

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
		if (!is_kvec && (full_record || eor)) {
			ret = zerocopy_from_iter(sk, &msg->msg_iter,
				try_to_copy, &ctx->sg_plaintext_num_elem,
				&ctx->sg_plaintext_size,
				ctx->sg_plaintext_data,
				ARRAY_SIZE(ctx->sg_plaintext_data),
				true);
			if (ret)
				goto fallback_to_reg_send;

			copied += try_to_copy;
			ret = tls_push_record(sk, msg->msg_flags, record_type);
			if (ret)
				goto send_end;
			continue;

fallback_to_reg_send:
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
				tls_ctx->tx.overhead_size);
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
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
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
			      tls_ctx->tx.overhead_size;

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
		sg_unmark_end(sg);

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

static struct sk_buff *tls_wait_data(struct sock *sk, int flags,
				     long timeo, int *err)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct sk_buff *skb;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	while (!(skb = ctx->recv_pkt)) {
		if (sk->sk_err) {
			*err = sock_error(sk);
			return NULL;
		}

		if (sk->sk_shutdown & RCV_SHUTDOWN)
			return NULL;

		if (sock_flag(sk, SOCK_DONE))
			return NULL;

		if ((flags & MSG_DONTWAIT) || !timeo) {
			*err = -EAGAIN;
			return NULL;
		}

		add_wait_queue(sk_sleep(sk), &wait);
		sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
		sk_wait_event(sk, &timeo, ctx->recv_pkt != skb, &wait);
		sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
		remove_wait_queue(sk_sleep(sk), &wait);

		/* Handle signals */
		if (signal_pending(current)) {
			*err = sock_intr_errno(timeo);
			return NULL;
		}
	}

	return skb;
}

/* This function decrypts the input skb into either out_iov or in out_sg
 * or in skb buffers itself. The input parameter 'zc' indicates if
 * zero-copy mode needs to be tried or not. With zero-copy mode, either
 * out_iov or out_sg must be non-NULL. In case both out_iov and out_sg are
 * NULL, then the decryption happens inside skb buffers itself, i.e.
 * zero-copy gets disabled and 'zc' is updated.
 */

static int decrypt_internal(struct sock *sk, struct sk_buff *skb,
			    struct iov_iter *out_iov,
			    struct scatterlist *out_sg,
			    int *chunk, bool *zc)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct strp_msg *rxm = strp_msg(skb);
	int n_sgin, n_sgout, nsg, mem_size, aead_size, err, pages = 0;
	struct aead_request *aead_req;
	struct sk_buff *unused;
	u8 *aad, *iv, *mem = NULL;
	struct scatterlist *sgin = NULL;
	struct scatterlist *sgout = NULL;
	const int data_len = rxm->full_len - tls_ctx->rx.overhead_size;

	if (*zc && (out_iov || out_sg)) {
		if (out_iov)
			n_sgout = iov_iter_npages(out_iov, INT_MAX) + 1;
		else
			n_sgout = sg_nents(out_sg);
	} else {
		n_sgout = 0;
		*zc = false;
	}

	n_sgin = skb_cow_data(skb, 0, &unused);
	if (n_sgin < 1)
		return -EBADMSG;

	/* Increment to accommodate AAD */
	n_sgin = n_sgin + 1;

	nsg = n_sgin + n_sgout;

	aead_size = sizeof(*aead_req) + crypto_aead_reqsize(ctx->aead_recv);
	mem_size = aead_size + (nsg * sizeof(struct scatterlist));
	mem_size = mem_size + TLS_AAD_SPACE_SIZE;
	mem_size = mem_size + crypto_aead_ivsize(ctx->aead_recv);

	/* Allocate a single block of memory which contains
	 * aead_req || sgin[] || sgout[] || aad || iv.
	 * This order achieves correct alignment for aead_req, sgin, sgout.
	 */
	mem = kmalloc(mem_size, sk->sk_allocation);
	if (!mem)
		return -ENOMEM;

	/* Segment the allocated memory */
	aead_req = (struct aead_request *)mem;
	sgin = (struct scatterlist *)(mem + aead_size);
	sgout = sgin + n_sgin;
	aad = (u8 *)(sgout + n_sgout);
	iv = aad + TLS_AAD_SPACE_SIZE;

	/* Prepare IV */
	err = skb_copy_bits(skb, rxm->offset + TLS_HEADER_SIZE,
			    iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE,
			    tls_ctx->rx.iv_size);
	if (err < 0) {
		kfree(mem);
		return err;
	}
	memcpy(iv, tls_ctx->rx.iv, TLS_CIPHER_AES_GCM_128_SALT_SIZE);

	/* Prepare AAD */
	tls_make_aad(aad, rxm->full_len - tls_ctx->rx.overhead_size,
		     tls_ctx->rx.rec_seq, tls_ctx->rx.rec_seq_size,
		     ctx->control);

	/* Prepare sgin */
	sg_init_table(sgin, n_sgin);
	sg_set_buf(&sgin[0], aad, TLS_AAD_SPACE_SIZE);
	err = skb_to_sgvec(skb, &sgin[1],
			   rxm->offset + tls_ctx->rx.prepend_size,
			   rxm->full_len - tls_ctx->rx.prepend_size);
	if (err < 0) {
		kfree(mem);
		return err;
	}

	if (n_sgout) {
		if (out_iov) {
			sg_init_table(sgout, n_sgout);
			sg_set_buf(&sgout[0], aad, TLS_AAD_SPACE_SIZE);

			*chunk = 0;
			err = zerocopy_from_iter(sk, out_iov, data_len, &pages,
						 chunk, &sgout[1],
						 (n_sgout - 1), false);
			if (err < 0)
				goto fallback_to_reg_recv;
		} else if (out_sg) {
			memcpy(sgout, out_sg, n_sgout * sizeof(*sgout));
		} else {
			goto fallback_to_reg_recv;
		}
	} else {
fallback_to_reg_recv:
		sgout = sgin;
		pages = 0;
		*chunk = 0;
		*zc = false;
	}

	/* Prepare and submit AEAD request */
	err = tls_do_decryption(sk, sgin, sgout, iv, data_len, aead_req);

	/* Release the pages in case iov was mapped to pages */
	for (; pages > 0; pages--)
		put_page(sg_page(&sgout[pages]));

	kfree(mem);
	return err;
}

static int decrypt_skb_update(struct sock *sk, struct sk_buff *skb,
			      struct iov_iter *dest, int *chunk, bool *zc)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct strp_msg *rxm = strp_msg(skb);
	int err = 0;

#ifdef CONFIG_TLS_DEVICE
	err = tls_device_decrypted(sk, skb);
	if (err < 0)
		return err;
#endif
	if (!ctx->decrypted) {
		err = decrypt_internal(sk, skb, dest, NULL, chunk, zc);
		if (err < 0)
			return err;
	} else {
		*zc = false;
	}

	rxm->offset += tls_ctx->rx.prepend_size;
	rxm->full_len -= tls_ctx->rx.overhead_size;
	tls_advance_record_sn(sk, &tls_ctx->rx);
	ctx->decrypted = true;
	ctx->saved_data_ready(sk);

	return err;
}

int decrypt_skb(struct sock *sk, struct sk_buff *skb,
		struct scatterlist *sgout)
{
	bool zc = true;
	int chunk;

	return decrypt_internal(sk, skb, NULL, sgout, &chunk, &zc);
}

static bool tls_sw_advance_skb(struct sock *sk, struct sk_buff *skb,
			       unsigned int len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct strp_msg *rxm = strp_msg(skb);

	if (len < rxm->full_len) {
		rxm->offset += len;
		rxm->full_len -= len;

		return false;
	}

	/* Finished with message */
	ctx->recv_pkt = NULL;
	kfree_skb(skb);
	__strp_unpause(&ctx->strp);

	return true;
}

int tls_sw_recvmsg(struct sock *sk,
		   struct msghdr *msg,
		   size_t len,
		   int nonblock,
		   int flags,
		   int *addr_len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	unsigned char control;
	struct strp_msg *rxm;
	struct sk_buff *skb;
	ssize_t copied = 0;
	bool cmsg = false;
	int target, err = 0;
	long timeo;
	bool is_kvec = msg->msg_iter.type & ITER_KVEC;

	flags |= nonblock;

	if (unlikely(flags & MSG_ERRQUEUE))
		return sock_recv_errqueue(sk, msg, len, SOL_IP, IP_RECVERR);

	lock_sock(sk);

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);
	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);
	do {
		bool zc = false;
		int chunk = 0;

		skb = tls_wait_data(sk, flags, timeo, &err);
		if (!skb)
			goto recv_end;

		rxm = strp_msg(skb);
		if (!cmsg) {
			int cerr;

			cerr = put_cmsg(msg, SOL_TLS, TLS_GET_RECORD_TYPE,
					sizeof(ctx->control), &ctx->control);
			cmsg = true;
			control = ctx->control;
			if (ctx->control != TLS_RECORD_TYPE_DATA) {
				if (cerr || msg->msg_flags & MSG_CTRUNC) {
					err = -EIO;
					goto recv_end;
				}
			}
		} else if (control != ctx->control) {
			goto recv_end;
		}

		if (!ctx->decrypted) {
			int to_copy = rxm->full_len - tls_ctx->rx.overhead_size;

			if (!is_kvec && to_copy <= len &&
			    likely(!(flags & MSG_PEEK)))
				zc = true;

			err = decrypt_skb_update(sk, skb, &msg->msg_iter,
						 &chunk, &zc);
			if (err < 0) {
				tls_err_abort(sk, EBADMSG);
				goto recv_end;
			}
			ctx->decrypted = true;
		}

		if (!zc) {
			chunk = min_t(unsigned int, rxm->full_len, len);
			err = skb_copy_datagram_msg(skb, rxm->offset, msg,
						    chunk);
			if (err < 0)
				goto recv_end;
		}

		copied += chunk;
		len -= chunk;
		if (likely(!(flags & MSG_PEEK))) {
			u8 control = ctx->control;

			if (tls_sw_advance_skb(sk, skb, chunk)) {
				/* Return full control message to
				 * userspace before trying to parse
				 * another message type
				 */
				msg->msg_flags |= MSG_EOR;
				if (control != TLS_RECORD_TYPE_DATA)
					goto recv_end;
			}
		} else {
			/* MSG_PEEK right now cannot look beyond current skb
			 * from strparser, meaning we cannot advance skb here
			 * and thus unpause strparser since we'd loose original
			 * one.
			 */
			break;
		}

		/* If we have a new message from strparser, continue now. */
		if (copied >= target && !ctx->recv_pkt)
			break;
	} while (len);

recv_end:
	release_sock(sk);
	return copied ? : err;
}

ssize_t tls_sw_splice_read(struct socket *sock,  loff_t *ppos,
			   struct pipe_inode_info *pipe,
			   size_t len, unsigned int flags)
{
	struct tls_context *tls_ctx = tls_get_ctx(sock->sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct strp_msg *rxm = NULL;
	struct sock *sk = sock->sk;
	struct sk_buff *skb;
	ssize_t copied = 0;
	int err = 0;
	long timeo;
	int chunk;
	bool zc = false;

	lock_sock(sk);

	timeo = sock_rcvtimeo(sk, flags & MSG_DONTWAIT);

	skb = tls_wait_data(sk, flags, timeo, &err);
	if (!skb)
		goto splice_read_end;

	/* splice does not support reading control messages */
	if (ctx->control != TLS_RECORD_TYPE_DATA) {
		err = -ENOTSUPP;
		goto splice_read_end;
	}

	if (!ctx->decrypted) {
		err = decrypt_skb_update(sk, skb, NULL, &chunk, &zc);

		if (err < 0) {
			tls_err_abort(sk, EBADMSG);
			goto splice_read_end;
		}
		ctx->decrypted = true;
	}
	rxm = strp_msg(skb);

	chunk = min_t(unsigned int, rxm->full_len, len);
	copied = skb_splice_bits(skb, sk, rxm->offset, pipe, chunk, flags);
	if (copied < 0)
		goto splice_read_end;

	if (likely(!(flags & MSG_PEEK)))
		tls_sw_advance_skb(sk, skb, copied);

splice_read_end:
	release_sock(sk);
	return copied ? : err;
}

unsigned int tls_sw_poll(struct file *file, struct socket *sock,
			 struct poll_table_struct *wait)
{
	unsigned int ret;
	struct sock *sk = sock->sk;
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	/* Grab POLLOUT and POLLHUP from the underlying socket */
	ret = ctx->sk_poll(file, sock, wait);

	/* Clear POLLIN bits, and set based on recv_pkt */
	ret &= ~(POLLIN | POLLRDNORM);
	if (ctx->recv_pkt)
		ret |= POLLIN | POLLRDNORM;

	return ret;
}

static int tls_read_size(struct strparser *strp, struct sk_buff *skb)
{
	struct tls_context *tls_ctx = tls_get_ctx(strp->sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	char header[TLS_HEADER_SIZE + MAX_IV_SIZE];
	struct strp_msg *rxm = strp_msg(skb);
	size_t cipher_overhead;
	size_t data_len = 0;
	int ret;

	/* Verify that we have a full TLS header, or wait for more data */
	if (rxm->offset + tls_ctx->rx.prepend_size > skb->len)
		return 0;

	/* Sanity-check size of on-stack buffer. */
	if (WARN_ON(tls_ctx->rx.prepend_size > sizeof(header))) {
		ret = -EINVAL;
		goto read_failure;
	}

	/* Linearize header to local buffer */
	ret = skb_copy_bits(skb, rxm->offset, header, tls_ctx->rx.prepend_size);

	if (ret < 0)
		goto read_failure;

	ctx->control = header[0];

	data_len = ((header[4] & 0xFF) | (header[3] << 8));

	cipher_overhead = tls_ctx->rx.tag_size + tls_ctx->rx.iv_size;

	if (data_len > TLS_MAX_PAYLOAD_SIZE + cipher_overhead) {
		ret = -EMSGSIZE;
		goto read_failure;
	}
	if (data_len < cipher_overhead) {
		ret = -EBADMSG;
		goto read_failure;
	}

	if (header[1] != TLS_VERSION_MINOR(tls_ctx->crypto_recv.info.version) ||
	    header[2] != TLS_VERSION_MAJOR(tls_ctx->crypto_recv.info.version)) {
		ret = -EINVAL;
		goto read_failure;
	}

#ifdef CONFIG_TLS_DEVICE
	handle_device_resync(strp->sk, TCP_SKB_CB(skb)->seq + rxm->offset,
			     *(u64*)tls_ctx->rx.rec_seq);
#endif
	return data_len + TLS_HEADER_SIZE;

read_failure:
	tls_err_abort(strp->sk, ret);

	return ret;
}

static void tls_queue(struct strparser *strp, struct sk_buff *skb)
{
	struct tls_context *tls_ctx = tls_get_ctx(strp->sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	ctx->decrypted = false;

	ctx->recv_pkt = skb;
	strp_pause(strp);

	ctx->saved_data_ready(strp->sk);
}

static void tls_data_ready(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	strp_data_ready(&ctx->strp);
}

void tls_sw_free_resources_tx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);

	crypto_free_aead(ctx->aead_send);
	tls_free_both_sg(sk);

	kfree(ctx);
}

void tls_sw_release_resources_rx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	kfree(tls_ctx->rx.rec_seq);
	kfree(tls_ctx->rx.iv);

	if (ctx->aead_recv) {
		kfree_skb(ctx->recv_pkt);
		ctx->recv_pkt = NULL;
		crypto_free_aead(ctx->aead_recv);
		strp_stop(&ctx->strp);
		write_lock_bh(&sk->sk_callback_lock);
		sk->sk_data_ready = ctx->saved_data_ready;
		write_unlock_bh(&sk->sk_callback_lock);
		release_sock(sk);
		strp_done(&ctx->strp);
		lock_sock(sk);
	}
}

void tls_sw_free_resources_rx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	tls_sw_release_resources_rx(sk);

	kfree(ctx);
}

int tls_set_sw_offload(struct sock *sk, struct tls_context *ctx, int tx)
{
	struct tls_crypto_info *crypto_info;
	struct tls12_crypto_info_aes_gcm_128 *gcm_128_info;
	struct tls_sw_context_tx *sw_ctx_tx = NULL;
	struct tls_sw_context_rx *sw_ctx_rx = NULL;
	struct cipher_context *cctx;
	struct crypto_aead **aead;
	struct strp_callbacks cb;
	u16 nonce_size, tag_size, iv_size, rec_seq_size;
	char *iv, *rec_seq;
	int rc = 0;

	if (!ctx) {
		rc = -EINVAL;
		goto out;
	}

	if (tx) {
		if (!ctx->priv_ctx_tx) {
			sw_ctx_tx = kzalloc(sizeof(*sw_ctx_tx), GFP_KERNEL);
			if (!sw_ctx_tx) {
				rc = -ENOMEM;
				goto out;
			}
			ctx->priv_ctx_tx = sw_ctx_tx;
		} else {
			sw_ctx_tx =
				(struct tls_sw_context_tx *)ctx->priv_ctx_tx;
		}
	} else {
		if (!ctx->priv_ctx_rx) {
			sw_ctx_rx = kzalloc(sizeof(*sw_ctx_rx), GFP_KERNEL);
			if (!sw_ctx_rx) {
				rc = -ENOMEM;
				goto out;
			}
			ctx->priv_ctx_rx = sw_ctx_rx;
		} else {
			sw_ctx_rx =
				(struct tls_sw_context_rx *)ctx->priv_ctx_rx;
		}
	}

	if (tx) {
		crypto_init_wait(&sw_ctx_tx->async_wait);
		crypto_info = &ctx->crypto_send.info;
		cctx = &ctx->tx;
		aead = &sw_ctx_tx->aead_send;
	} else {
		crypto_init_wait(&sw_ctx_rx->async_wait);
		crypto_info = &ctx->crypto_recv.info;
		cctx = &ctx->rx;
		aead = &sw_ctx_rx->aead_recv;
	}

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

	/* Sanity-check the IV size for stack allocations. */
	if (iv_size > MAX_IV_SIZE || nonce_size > MAX_IV_SIZE) {
		rc = -EINVAL;
		goto free_priv;
	}

	cctx->prepend_size = TLS_HEADER_SIZE + nonce_size;
	cctx->tag_size = tag_size;
	cctx->overhead_size = cctx->prepend_size + cctx->tag_size;
	cctx->iv_size = iv_size;
	cctx->iv = kmalloc(iv_size + TLS_CIPHER_AES_GCM_128_SALT_SIZE,
			   GFP_KERNEL);
	if (!cctx->iv) {
		rc = -ENOMEM;
		goto free_priv;
	}
	memcpy(cctx->iv, gcm_128_info->salt, TLS_CIPHER_AES_GCM_128_SALT_SIZE);
	memcpy(cctx->iv + TLS_CIPHER_AES_GCM_128_SALT_SIZE, iv, iv_size);
	cctx->rec_seq_size = rec_seq_size;
	cctx->rec_seq = kmemdup(rec_seq, rec_seq_size, GFP_KERNEL);
	if (!cctx->rec_seq) {
		rc = -ENOMEM;
		goto free_iv;
	}

	if (sw_ctx_tx) {
		sg_init_table(sw_ctx_tx->sg_encrypted_data,
			      ARRAY_SIZE(sw_ctx_tx->sg_encrypted_data));
		sg_init_table(sw_ctx_tx->sg_plaintext_data,
			      ARRAY_SIZE(sw_ctx_tx->sg_plaintext_data));

		sg_init_table(sw_ctx_tx->sg_aead_in, 2);
		sg_set_buf(&sw_ctx_tx->sg_aead_in[0], sw_ctx_tx->aad_space,
			   sizeof(sw_ctx_tx->aad_space));
		sg_unmark_end(&sw_ctx_tx->sg_aead_in[1]);
		sg_chain(sw_ctx_tx->sg_aead_in, 2,
			 sw_ctx_tx->sg_plaintext_data);
		sg_init_table(sw_ctx_tx->sg_aead_out, 2);
		sg_set_buf(&sw_ctx_tx->sg_aead_out[0], sw_ctx_tx->aad_space,
			   sizeof(sw_ctx_tx->aad_space));
		sg_unmark_end(&sw_ctx_tx->sg_aead_out[1]);
		sg_chain(sw_ctx_tx->sg_aead_out, 2,
			 sw_ctx_tx->sg_encrypted_data);
	}

	if (!*aead) {
		*aead = crypto_alloc_aead("gcm(aes)", 0, 0);
		if (IS_ERR(*aead)) {
			rc = PTR_ERR(*aead);
			*aead = NULL;
			goto free_rec_seq;
		}
	}

	ctx->push_pending_record = tls_sw_push_pending_record;

	rc = crypto_aead_setkey(*aead, gcm_128_info->key,
				TLS_CIPHER_AES_GCM_128_KEY_SIZE);
	if (rc)
		goto free_aead;

	rc = crypto_aead_setauthsize(*aead, cctx->tag_size);
	if (rc)
		goto free_aead;

	if (sw_ctx_rx) {
		/* Set up strparser */
		memset(&cb, 0, sizeof(cb));
		cb.rcv_msg = tls_queue;
		cb.parse_msg = tls_read_size;

		strp_init(&sw_ctx_rx->strp, sk, &cb);

		write_lock_bh(&sk->sk_callback_lock);
		sw_ctx_rx->saved_data_ready = sk->sk_data_ready;
		sk->sk_data_ready = tls_data_ready;
		write_unlock_bh(&sk->sk_callback_lock);

		sw_ctx_rx->sk_poll = sk->sk_socket->ops->poll;

		strp_check_rcv(&sw_ctx_rx->strp);
	}

	goto out;

free_aead:
	crypto_free_aead(*aead);
	*aead = NULL;
free_rec_seq:
	kfree(cctx->rec_seq);
	cctx->rec_seq = NULL;
free_iv:
	kfree(cctx->iv);
	cctx->iv = NULL;
free_priv:
	if (tx) {
		kfree(ctx->priv_ctx_tx);
		ctx->priv_ctx_tx = NULL;
	} else {
		kfree(ctx->priv_ctx_rx);
		ctx->priv_ctx_rx = NULL;
	}
out:
	return rc;
}
