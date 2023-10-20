/*
 * Copyright (c) 2016-2017, Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016-2017, Dave Watson <davejwatson@fb.com>. All rights reserved.
 * Copyright (c) 2016-2017, Lance Chao <lancerchao@fb.com>. All rights reserved.
 * Copyright (c) 2016, Fridolin Pokorny <fridolin.pokorny@gmail.com>. All rights reserved.
 * Copyright (c) 2016, Nikos Mavrogiannopoulos <nmav@gnutls.org>. All rights reserved.
 * Copyright (c) 2018, Covalent IO, Inc. http://covalent.io
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

#include <linux/bug.h>
#include <linux/sched/signal.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/splice.h>
#include <crypto/aead.h>

#include <net/strparser.h>
#include <net/tls.h>
#include <trace/events/sock.h>

#include "tls.h"

struct tls_decrypt_arg {
	struct_group(inargs,
	bool zc;
	bool async;
	u8 tail;
	);

	struct sk_buff *skb;
};

struct tls_decrypt_ctx {
	struct sock *sk;
	u8 iv[TLS_MAX_IV_SIZE];
	u8 aad[TLS_MAX_AAD_SIZE];
	u8 tail;
	struct scatterlist sg[];
};

noinline void tls_err_abort(struct sock *sk, int err)
{
	WARN_ON_ONCE(err >= 0);
	/* sk->sk_err should contain a positive error code. */
	WRITE_ONCE(sk->sk_err, -err);
	/* Paired with smp_rmb() in tcp_poll() */
	smp_wmb();
	sk_error_report(sk);
}

static int __skb_nsg(struct sk_buff *skb, int offset, int len,
                     unsigned int recursion_level)
{
        int start = skb_headlen(skb);
        int i, chunk = start - offset;
        struct sk_buff *frag_iter;
        int elt = 0;

        if (unlikely(recursion_level >= 24))
                return -EMSGSIZE;

        if (chunk > 0) {
                if (chunk > len)
                        chunk = len;
                elt++;
                len -= chunk;
                if (len == 0)
                        return elt;
                offset += chunk;
        }

        for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
                int end;

                WARN_ON(start > offset + len);

                end = start + skb_frag_size(&skb_shinfo(skb)->frags[i]);
                chunk = end - offset;
                if (chunk > 0) {
                        if (chunk > len)
                                chunk = len;
                        elt++;
                        len -= chunk;
                        if (len == 0)
                                return elt;
                        offset += chunk;
                }
                start = end;
        }

        if (unlikely(skb_has_frag_list(skb))) {
                skb_walk_frags(skb, frag_iter) {
                        int end, ret;

                        WARN_ON(start > offset + len);

                        end = start + frag_iter->len;
                        chunk = end - offset;
                        if (chunk > 0) {
                                if (chunk > len)
                                        chunk = len;
                                ret = __skb_nsg(frag_iter, offset - start, chunk,
                                                recursion_level + 1);
                                if (unlikely(ret < 0))
                                        return ret;
                                elt += ret;
                                len -= chunk;
                                if (len == 0)
                                        return elt;
                                offset += chunk;
                        }
                        start = end;
                }
        }
        BUG_ON(len);
        return elt;
}

/* Return the number of scatterlist elements required to completely map the
 * skb, or -EMSGSIZE if the recursion depth is exceeded.
 */
static int skb_nsg(struct sk_buff *skb, int offset, int len)
{
        return __skb_nsg(skb, offset, len, 0);
}

static int tls_padding_length(struct tls_prot_info *prot, struct sk_buff *skb,
			      struct tls_decrypt_arg *darg)
{
	struct strp_msg *rxm = strp_msg(skb);
	struct tls_msg *tlm = tls_msg(skb);
	int sub = 0;

	/* Determine zero-padding length */
	if (prot->version == TLS_1_3_VERSION) {
		int offset = rxm->full_len - TLS_TAG_SIZE - 1;
		char content_type = darg->zc ? darg->tail : 0;
		int err;

		while (content_type == 0) {
			if (offset < prot->prepend_size)
				return -EBADMSG;
			err = skb_copy_bits(skb, rxm->offset + offset,
					    &content_type, 1);
			if (err)
				return err;
			if (content_type)
				break;
			sub++;
			offset--;
		}
		tlm->control = content_type;
	}
	return sub;
}

static void tls_decrypt_done(void *data, int err)
{
	struct aead_request *aead_req = data;
	struct crypto_aead *aead = crypto_aead_reqtfm(aead_req);
	struct scatterlist *sgout = aead_req->dst;
	struct scatterlist *sgin = aead_req->src;
	struct tls_sw_context_rx *ctx;
	struct tls_decrypt_ctx *dctx;
	struct tls_context *tls_ctx;
	struct scatterlist *sg;
	unsigned int pages;
	struct sock *sk;
	int aead_size;

	aead_size = sizeof(*aead_req) + crypto_aead_reqsize(aead);
	aead_size = ALIGN(aead_size, __alignof__(*dctx));
	dctx = (void *)((u8 *)aead_req + aead_size);

	sk = dctx->sk;
	tls_ctx = tls_get_ctx(sk);
	ctx = tls_sw_ctx_rx(tls_ctx);

	/* Propagate if there was an err */
	if (err) {
		if (err == -EBADMSG)
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSDECRYPTERROR);
		ctx->async_wait.err = err;
		tls_err_abort(sk, err);
	}

	/* Free the destination pages if skb was not decrypted inplace */
	if (sgout != sgin) {
		/* Skip the first S/G entry as it points to AAD */
		for_each_sg(sg_next(sgout), sg, UINT_MAX, pages) {
			if (!sg)
				break;
			put_page(sg_page(sg));
		}
	}

	kfree(aead_req);

	spin_lock_bh(&ctx->decrypt_compl_lock);
	if (!atomic_dec_return(&ctx->decrypt_pending))
		complete(&ctx->async_wait.completion);
	spin_unlock_bh(&ctx->decrypt_compl_lock);
}

static int tls_do_decryption(struct sock *sk,
			     struct scatterlist *sgin,
			     struct scatterlist *sgout,
			     char *iv_recv,
			     size_t data_len,
			     struct aead_request *aead_req,
			     struct tls_decrypt_arg *darg)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	int ret;

	aead_request_set_tfm(aead_req, ctx->aead_recv);
	aead_request_set_ad(aead_req, prot->aad_size);
	aead_request_set_crypt(aead_req, sgin, sgout,
			       data_len + prot->tag_size,
			       (u8 *)iv_recv);

	if (darg->async) {
		aead_request_set_callback(aead_req,
					  CRYPTO_TFM_REQ_MAY_BACKLOG,
					  tls_decrypt_done, aead_req);
		atomic_inc(&ctx->decrypt_pending);
	} else {
		aead_request_set_callback(aead_req,
					  CRYPTO_TFM_REQ_MAY_BACKLOG,
					  crypto_req_done, &ctx->async_wait);
	}

	ret = crypto_aead_decrypt(aead_req);
	if (ret == -EINPROGRESS) {
		if (darg->async)
			return 0;

		ret = crypto_wait_req(ret, &ctx->async_wait);
	}
	darg->async = false;

	return ret;
}

static void tls_trim_both_msgs(struct sock *sk, int target_size)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec = ctx->open_rec;

	sk_msg_trim(sk, &rec->msg_plaintext, target_size);
	if (target_size > 0)
		target_size += prot->overhead_size;
	sk_msg_trim(sk, &rec->msg_encrypted, target_size);
}

static int tls_alloc_encrypted_msg(struct sock *sk, int len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec = ctx->open_rec;
	struct sk_msg *msg_en = &rec->msg_encrypted;

	return sk_msg_alloc(sk, msg_en, len, 0);
}

static int tls_clone_plaintext_msg(struct sock *sk, int required)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec = ctx->open_rec;
	struct sk_msg *msg_pl = &rec->msg_plaintext;
	struct sk_msg *msg_en = &rec->msg_encrypted;
	int skip, len;

	/* We add page references worth len bytes from encrypted sg
	 * at the end of plaintext sg. It is guaranteed that msg_en
	 * has enough required room (ensured by caller).
	 */
	len = required - msg_pl->sg.size;

	/* Skip initial bytes in msg_en's data to be able to use
	 * same offset of both plain and encrypted data.
	 */
	skip = prot->prepend_size + msg_pl->sg.size;

	return sk_msg_clone(sk, msg_pl, msg_en, skip, len);
}

static struct tls_rec *tls_get_rec(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct sk_msg *msg_pl, *msg_en;
	struct tls_rec *rec;
	int mem_size;

	mem_size = sizeof(struct tls_rec) + crypto_aead_reqsize(ctx->aead_send);

	rec = kzalloc(mem_size, sk->sk_allocation);
	if (!rec)
		return NULL;

	msg_pl = &rec->msg_plaintext;
	msg_en = &rec->msg_encrypted;

	sk_msg_init(msg_pl);
	sk_msg_init(msg_en);

	sg_init_table(rec->sg_aead_in, 2);
	sg_set_buf(&rec->sg_aead_in[0], rec->aad_space, prot->aad_size);
	sg_unmark_end(&rec->sg_aead_in[1]);

	sg_init_table(rec->sg_aead_out, 2);
	sg_set_buf(&rec->sg_aead_out[0], rec->aad_space, prot->aad_size);
	sg_unmark_end(&rec->sg_aead_out[1]);

	rec->sk = sk;

	return rec;
}

static void tls_free_rec(struct sock *sk, struct tls_rec *rec)
{
	sk_msg_free(sk, &rec->msg_encrypted);
	sk_msg_free(sk, &rec->msg_plaintext);
	kfree(rec);
}

static void tls_free_open_rec(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec = ctx->open_rec;

	if (rec) {
		tls_free_rec(sk, rec);
		ctx->open_rec = NULL;
	}
}

int tls_tx_records(struct sock *sk, int flags)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec, *tmp;
	struct sk_msg *msg_en;
	int tx_flags, rc = 0;

	if (tls_is_partially_sent_record(tls_ctx)) {
		rec = list_first_entry(&ctx->tx_list,
				       struct tls_rec, list);

		if (flags == -1)
			tx_flags = rec->tx_flags;
		else
			tx_flags = flags;

		rc = tls_push_partial_record(sk, tls_ctx, tx_flags);
		if (rc)
			goto tx_err;

		/* Full record has been transmitted.
		 * Remove the head of tx_list
		 */
		list_del(&rec->list);
		sk_msg_free(sk, &rec->msg_plaintext);
		kfree(rec);
	}

	/* Tx all ready records */
	list_for_each_entry_safe(rec, tmp, &ctx->tx_list, list) {
		if (READ_ONCE(rec->tx_ready)) {
			if (flags == -1)
				tx_flags = rec->tx_flags;
			else
				tx_flags = flags;

			msg_en = &rec->msg_encrypted;
			rc = tls_push_sg(sk, tls_ctx,
					 &msg_en->sg.data[msg_en->sg.curr],
					 0, tx_flags);
			if (rc)
				goto tx_err;

			list_del(&rec->list);
			sk_msg_free(sk, &rec->msg_plaintext);
			kfree(rec);
		} else {
			break;
		}
	}

tx_err:
	if (rc < 0 && rc != -EAGAIN)
		tls_err_abort(sk, -EBADMSG);

	return rc;
}

static void tls_encrypt_done(void *data, int err)
{
	struct tls_sw_context_tx *ctx;
	struct tls_context *tls_ctx;
	struct tls_prot_info *prot;
	struct tls_rec *rec = data;
	struct scatterlist *sge;
	struct sk_msg *msg_en;
	bool ready = false;
	struct sock *sk;
	int pending;

	msg_en = &rec->msg_encrypted;

	sk = rec->sk;
	tls_ctx = tls_get_ctx(sk);
	prot = &tls_ctx->prot_info;
	ctx = tls_sw_ctx_tx(tls_ctx);

	sge = sk_msg_elem(msg_en, msg_en->sg.curr);
	sge->offset -= prot->prepend_size;
	sge->length += prot->prepend_size;

	/* Check if error is previously set on socket */
	if (err || sk->sk_err) {
		rec = NULL;

		/* If err is already set on socket, return the same code */
		if (sk->sk_err) {
			ctx->async_wait.err = -sk->sk_err;
		} else {
			ctx->async_wait.err = err;
			tls_err_abort(sk, err);
		}
	}

	if (rec) {
		struct tls_rec *first_rec;

		/* Mark the record as ready for transmission */
		smp_store_mb(rec->tx_ready, true);

		/* If received record is at head of tx_list, schedule tx */
		first_rec = list_first_entry(&ctx->tx_list,
					     struct tls_rec, list);
		if (rec == first_rec)
			ready = true;
	}

	spin_lock_bh(&ctx->encrypt_compl_lock);
	pending = atomic_dec_return(&ctx->encrypt_pending);

	if (!pending && ctx->async_notify)
		complete(&ctx->async_wait.completion);
	spin_unlock_bh(&ctx->encrypt_compl_lock);

	if (!ready)
		return;

	/* Schedule the transmission */
	if (!test_and_set_bit(BIT_TX_SCHEDULED, &ctx->tx_bitmask))
		schedule_delayed_work(&ctx->tx_work.work, 1);
}

static int tls_do_encryption(struct sock *sk,
			     struct tls_context *tls_ctx,
			     struct tls_sw_context_tx *ctx,
			     struct aead_request *aead_req,
			     size_t data_len, u32 start)
{
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_rec *rec = ctx->open_rec;
	struct sk_msg *msg_en = &rec->msg_encrypted;
	struct scatterlist *sge = sk_msg_elem(msg_en, start);
	int rc, iv_offset = 0;

	/* For CCM based ciphers, first byte of IV is a constant */
	switch (prot->cipher_type) {
	case TLS_CIPHER_AES_CCM_128:
		rec->iv_data[0] = TLS_AES_CCM_IV_B0_BYTE;
		iv_offset = 1;
		break;
	case TLS_CIPHER_SM4_CCM:
		rec->iv_data[0] = TLS_SM4_CCM_IV_B0_BYTE;
		iv_offset = 1;
		break;
	}

	memcpy(&rec->iv_data[iv_offset], tls_ctx->tx.iv,
	       prot->iv_size + prot->salt_size);

	tls_xor_iv_with_seq(prot, rec->iv_data + iv_offset,
			    tls_ctx->tx.rec_seq);

	sge->offset += prot->prepend_size;
	sge->length -= prot->prepend_size;

	msg_en->sg.curr = start;

	aead_request_set_tfm(aead_req, ctx->aead_send);
	aead_request_set_ad(aead_req, prot->aad_size);
	aead_request_set_crypt(aead_req, rec->sg_aead_in,
			       rec->sg_aead_out,
			       data_len, rec->iv_data);

	aead_request_set_callback(aead_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  tls_encrypt_done, rec);

	/* Add the record in tx_list */
	list_add_tail((struct list_head *)&rec->list, &ctx->tx_list);
	atomic_inc(&ctx->encrypt_pending);

	rc = crypto_aead_encrypt(aead_req);
	if (!rc || rc != -EINPROGRESS) {
		atomic_dec(&ctx->encrypt_pending);
		sge->offset -= prot->prepend_size;
		sge->length += prot->prepend_size;
	}

	if (!rc) {
		WRITE_ONCE(rec->tx_ready, true);
	} else if (rc != -EINPROGRESS) {
		list_del(&rec->list);
		return rc;
	}

	/* Unhook the record from context if encryption is not failure */
	ctx->open_rec = NULL;
	tls_advance_record_sn(sk, prot, &tls_ctx->tx);
	return rc;
}

static int tls_split_open_record(struct sock *sk, struct tls_rec *from,
				 struct tls_rec **to, struct sk_msg *msg_opl,
				 struct sk_msg *msg_oen, u32 split_point,
				 u32 tx_overhead_size, u32 *orig_end)
{
	u32 i, j, bytes = 0, apply = msg_opl->apply_bytes;
	struct scatterlist *sge, *osge, *nsge;
	u32 orig_size = msg_opl->sg.size;
	struct scatterlist tmp = { };
	struct sk_msg *msg_npl;
	struct tls_rec *new;
	int ret;

	new = tls_get_rec(sk);
	if (!new)
		return -ENOMEM;
	ret = sk_msg_alloc(sk, &new->msg_encrypted, msg_opl->sg.size +
			   tx_overhead_size, 0);
	if (ret < 0) {
		tls_free_rec(sk, new);
		return ret;
	}

	*orig_end = msg_opl->sg.end;
	i = msg_opl->sg.start;
	sge = sk_msg_elem(msg_opl, i);
	while (apply && sge->length) {
		if (sge->length > apply) {
			u32 len = sge->length - apply;

			get_page(sg_page(sge));
			sg_set_page(&tmp, sg_page(sge), len,
				    sge->offset + apply);
			sge->length = apply;
			bytes += apply;
			apply = 0;
		} else {
			apply -= sge->length;
			bytes += sge->length;
		}

		sk_msg_iter_var_next(i);
		if (i == msg_opl->sg.end)
			break;
		sge = sk_msg_elem(msg_opl, i);
	}

	msg_opl->sg.end = i;
	msg_opl->sg.curr = i;
	msg_opl->sg.copybreak = 0;
	msg_opl->apply_bytes = 0;
	msg_opl->sg.size = bytes;

	msg_npl = &new->msg_plaintext;
	msg_npl->apply_bytes = apply;
	msg_npl->sg.size = orig_size - bytes;

	j = msg_npl->sg.start;
	nsge = sk_msg_elem(msg_npl, j);
	if (tmp.length) {
		memcpy(nsge, &tmp, sizeof(*nsge));
		sk_msg_iter_var_next(j);
		nsge = sk_msg_elem(msg_npl, j);
	}

	osge = sk_msg_elem(msg_opl, i);
	while (osge->length) {
		memcpy(nsge, osge, sizeof(*nsge));
		sg_unmark_end(nsge);
		sk_msg_iter_var_next(i);
		sk_msg_iter_var_next(j);
		if (i == *orig_end)
			break;
		osge = sk_msg_elem(msg_opl, i);
		nsge = sk_msg_elem(msg_npl, j);
	}

	msg_npl->sg.end = j;
	msg_npl->sg.curr = j;
	msg_npl->sg.copybreak = 0;

	*to = new;
	return 0;
}

static void tls_merge_open_record(struct sock *sk, struct tls_rec *to,
				  struct tls_rec *from, u32 orig_end)
{
	struct sk_msg *msg_npl = &from->msg_plaintext;
	struct sk_msg *msg_opl = &to->msg_plaintext;
	struct scatterlist *osge, *nsge;
	u32 i, j;

	i = msg_opl->sg.end;
	sk_msg_iter_var_prev(i);
	j = msg_npl->sg.start;

	osge = sk_msg_elem(msg_opl, i);
	nsge = sk_msg_elem(msg_npl, j);

	if (sg_page(osge) == sg_page(nsge) &&
	    osge->offset + osge->length == nsge->offset) {
		osge->length += nsge->length;
		put_page(sg_page(nsge));
	}

	msg_opl->sg.end = orig_end;
	msg_opl->sg.curr = orig_end;
	msg_opl->sg.copybreak = 0;
	msg_opl->apply_bytes = msg_opl->sg.size + msg_npl->sg.size;
	msg_opl->sg.size += msg_npl->sg.size;

	sk_msg_free(sk, &to->msg_encrypted);
	sk_msg_xfer_full(&to->msg_encrypted, &from->msg_encrypted);

	kfree(from);
}

static int tls_push_record(struct sock *sk, int flags,
			   unsigned char record_type)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec = ctx->open_rec, *tmp = NULL;
	u32 i, split_point, orig_end;
	struct sk_msg *msg_pl, *msg_en;
	struct aead_request *req;
	bool split;
	int rc;

	if (!rec)
		return 0;

	msg_pl = &rec->msg_plaintext;
	msg_en = &rec->msg_encrypted;

	split_point = msg_pl->apply_bytes;
	split = split_point && split_point < msg_pl->sg.size;
	if (unlikely((!split &&
		      msg_pl->sg.size +
		      prot->overhead_size > msg_en->sg.size) ||
		     (split &&
		      split_point +
		      prot->overhead_size > msg_en->sg.size))) {
		split = true;
		split_point = msg_en->sg.size;
	}
	if (split) {
		rc = tls_split_open_record(sk, rec, &tmp, msg_pl, msg_en,
					   split_point, prot->overhead_size,
					   &orig_end);
		if (rc < 0)
			return rc;
		/* This can happen if above tls_split_open_record allocates
		 * a single large encryption buffer instead of two smaller
		 * ones. In this case adjust pointers and continue without
		 * split.
		 */
		if (!msg_pl->sg.size) {
			tls_merge_open_record(sk, rec, tmp, orig_end);
			msg_pl = &rec->msg_plaintext;
			msg_en = &rec->msg_encrypted;
			split = false;
		}
		sk_msg_trim(sk, msg_en, msg_pl->sg.size +
			    prot->overhead_size);
	}

	rec->tx_flags = flags;
	req = &rec->aead_req;

	i = msg_pl->sg.end;
	sk_msg_iter_var_prev(i);

	rec->content_type = record_type;
	if (prot->version == TLS_1_3_VERSION) {
		/* Add content type to end of message.  No padding added */
		sg_set_buf(&rec->sg_content_type, &rec->content_type, 1);
		sg_mark_end(&rec->sg_content_type);
		sg_chain(msg_pl->sg.data, msg_pl->sg.end + 1,
			 &rec->sg_content_type);
	} else {
		sg_mark_end(sk_msg_elem(msg_pl, i));
	}

	if (msg_pl->sg.end < msg_pl->sg.start) {
		sg_chain(&msg_pl->sg.data[msg_pl->sg.start],
			 MAX_SKB_FRAGS - msg_pl->sg.start + 1,
			 msg_pl->sg.data);
	}

	i = msg_pl->sg.start;
	sg_chain(rec->sg_aead_in, 2, &msg_pl->sg.data[i]);

	i = msg_en->sg.end;
	sk_msg_iter_var_prev(i);
	sg_mark_end(sk_msg_elem(msg_en, i));

	i = msg_en->sg.start;
	sg_chain(rec->sg_aead_out, 2, &msg_en->sg.data[i]);

	tls_make_aad(rec->aad_space, msg_pl->sg.size + prot->tail_size,
		     tls_ctx->tx.rec_seq, record_type, prot);

	tls_fill_prepend(tls_ctx,
			 page_address(sg_page(&msg_en->sg.data[i])) +
			 msg_en->sg.data[i].offset,
			 msg_pl->sg.size + prot->tail_size,
			 record_type);

	tls_ctx->pending_open_record_frags = false;

	rc = tls_do_encryption(sk, tls_ctx, ctx, req,
			       msg_pl->sg.size + prot->tail_size, i);
	if (rc < 0) {
		if (rc != -EINPROGRESS) {
			tls_err_abort(sk, -EBADMSG);
			if (split) {
				tls_ctx->pending_open_record_frags = true;
				tls_merge_open_record(sk, rec, tmp, orig_end);
			}
		}
		ctx->async_capable = 1;
		return rc;
	} else if (split) {
		msg_pl = &tmp->msg_plaintext;
		msg_en = &tmp->msg_encrypted;
		sk_msg_trim(sk, msg_en, msg_pl->sg.size + prot->overhead_size);
		tls_ctx->pending_open_record_frags = true;
		ctx->open_rec = tmp;
	}

	return tls_tx_records(sk, flags);
}

static int bpf_exec_tx_verdict(struct sk_msg *msg, struct sock *sk,
			       bool full_record, u8 record_type,
			       ssize_t *copied, int flags)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct sk_msg msg_redir = { };
	struct sk_psock *psock;
	struct sock *sk_redir;
	struct tls_rec *rec;
	bool enospc, policy, redir_ingress;
	int err = 0, send;
	u32 delta = 0;

	policy = !(flags & MSG_SENDPAGE_NOPOLICY);
	psock = sk_psock_get(sk);
	if (!psock || !policy) {
		err = tls_push_record(sk, flags, record_type);
		if (err && err != -EINPROGRESS && sk->sk_err == EBADMSG) {
			*copied -= sk_msg_free(sk, msg);
			tls_free_open_rec(sk);
			err = -sk->sk_err;
		}
		if (psock)
			sk_psock_put(sk, psock);
		return err;
	}
more_data:
	enospc = sk_msg_full(msg);
	if (psock->eval == __SK_NONE) {
		delta = msg->sg.size;
		psock->eval = sk_psock_msg_verdict(sk, psock, msg);
		delta -= msg->sg.size;
	}
	if (msg->cork_bytes && msg->cork_bytes > msg->sg.size &&
	    !enospc && !full_record) {
		err = -ENOSPC;
		goto out_err;
	}
	msg->cork_bytes = 0;
	send = msg->sg.size;
	if (msg->apply_bytes && msg->apply_bytes < send)
		send = msg->apply_bytes;

	switch (psock->eval) {
	case __SK_PASS:
		err = tls_push_record(sk, flags, record_type);
		if (err && err != -EINPROGRESS && sk->sk_err == EBADMSG) {
			*copied -= sk_msg_free(sk, msg);
			tls_free_open_rec(sk);
			err = -sk->sk_err;
			goto out_err;
		}
		break;
	case __SK_REDIRECT:
		redir_ingress = psock->redir_ingress;
		sk_redir = psock->sk_redir;
		memcpy(&msg_redir, msg, sizeof(*msg));
		if (msg->apply_bytes < send)
			msg->apply_bytes = 0;
		else
			msg->apply_bytes -= send;
		sk_msg_return_zero(sk, msg, send);
		msg->sg.size -= send;
		release_sock(sk);
		err = tcp_bpf_sendmsg_redir(sk_redir, redir_ingress,
					    &msg_redir, send, flags);
		lock_sock(sk);
		if (err < 0) {
			*copied -= sk_msg_free_nocharge(sk, &msg_redir);
			msg->sg.size = 0;
		}
		if (msg->sg.size == 0)
			tls_free_open_rec(sk);
		break;
	case __SK_DROP:
	default:
		sk_msg_free_partial(sk, msg, send);
		if (msg->apply_bytes < send)
			msg->apply_bytes = 0;
		else
			msg->apply_bytes -= send;
		if (msg->sg.size == 0)
			tls_free_open_rec(sk);
		*copied -= (send + delta);
		err = -EACCES;
	}

	if (likely(!err)) {
		bool reset_eval = !ctx->open_rec;

		rec = ctx->open_rec;
		if (rec) {
			msg = &rec->msg_plaintext;
			if (!msg->apply_bytes)
				reset_eval = true;
		}
		if (reset_eval) {
			psock->eval = __SK_NONE;
			if (psock->sk_redir) {
				sock_put(psock->sk_redir);
				psock->sk_redir = NULL;
			}
		}
		if (rec)
			goto more_data;
	}
 out_err:
	sk_psock_put(sk, psock);
	return err;
}

static int tls_sw_push_pending_record(struct sock *sk, int flags)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec = ctx->open_rec;
	struct sk_msg *msg_pl;
	size_t copied;

	if (!rec)
		return 0;

	msg_pl = &rec->msg_plaintext;
	copied = msg_pl->sg.size;
	if (!copied)
		return 0;

	return bpf_exec_tx_verdict(msg_pl, sk, true, TLS_RECORD_TYPE_DATA,
				   &copied, flags);
}

static int tls_sw_sendmsg_splice(struct sock *sk, struct msghdr *msg,
				 struct sk_msg *msg_pl, size_t try_to_copy,
				 ssize_t *copied)
{
	struct page *page = NULL, **pages = &page;

	do {
		ssize_t part;
		size_t off;

		part = iov_iter_extract_pages(&msg->msg_iter, &pages,
					      try_to_copy, 1, 0, &off);
		if (part <= 0)
			return part ?: -EIO;

		if (WARN_ON_ONCE(!sendpage_ok(page))) {
			iov_iter_revert(&msg->msg_iter, part);
			return -EIO;
		}

		sk_msg_page_add(msg_pl, page, part, off);
		sk_mem_charge(sk, part);
		*copied += part;
		try_to_copy -= part;
	} while (try_to_copy && !sk_msg_full(msg_pl));

	return 0;
}

static int tls_sw_sendmsg_locked(struct sock *sk, struct msghdr *msg,
				 size_t size)
{
	long timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	bool async_capable = ctx->async_capable;
	unsigned char record_type = TLS_RECORD_TYPE_DATA;
	bool is_kvec = iov_iter_is_kvec(&msg->msg_iter);
	bool eor = !(msg->msg_flags & MSG_MORE);
	size_t try_to_copy;
	ssize_t copied = 0;
	struct sk_msg *msg_pl, *msg_en;
	struct tls_rec *rec;
	int required_size;
	int num_async = 0;
	bool full_record;
	int record_room;
	int num_zc = 0;
	int orig_size;
	int ret = 0;
	int pending;

	if (!eor && (msg->msg_flags & MSG_EOR))
		return -EINVAL;

	if (unlikely(msg->msg_controllen)) {
		ret = tls_process_cmsg(sk, msg, &record_type);
		if (ret) {
			if (ret == -EINPROGRESS)
				num_async++;
			else if (ret != -EAGAIN)
				goto send_end;
		}
	}

	while (msg_data_left(msg)) {
		if (sk->sk_err) {
			ret = -sk->sk_err;
			goto send_end;
		}

		if (ctx->open_rec)
			rec = ctx->open_rec;
		else
			rec = ctx->open_rec = tls_get_rec(sk);
		if (!rec) {
			ret = -ENOMEM;
			goto send_end;
		}

		msg_pl = &rec->msg_plaintext;
		msg_en = &rec->msg_encrypted;

		orig_size = msg_pl->sg.size;
		full_record = false;
		try_to_copy = msg_data_left(msg);
		record_room = TLS_MAX_PAYLOAD_SIZE - msg_pl->sg.size;
		if (try_to_copy >= record_room) {
			try_to_copy = record_room;
			full_record = true;
		}

		required_size = msg_pl->sg.size + try_to_copy +
				prot->overhead_size;

		if (!sk_stream_memory_free(sk))
			goto wait_for_sndbuf;

alloc_encrypted:
		ret = tls_alloc_encrypted_msg(sk, required_size);
		if (ret) {
			if (ret != -ENOSPC)
				goto wait_for_memory;

			/* Adjust try_to_copy according to the amount that was
			 * actually allocated. The difference is due
			 * to max sg elements limit
			 */
			try_to_copy -= required_size - msg_en->sg.size;
			full_record = true;
		}

		if (try_to_copy && (msg->msg_flags & MSG_SPLICE_PAGES)) {
			ret = tls_sw_sendmsg_splice(sk, msg, msg_pl,
						    try_to_copy, &copied);
			if (ret < 0)
				goto send_end;
			tls_ctx->pending_open_record_frags = true;
			if (full_record || eor || sk_msg_full(msg_pl))
				goto copied;
			continue;
		}

		if (!is_kvec && (full_record || eor) && !async_capable) {
			u32 first = msg_pl->sg.end;

			ret = sk_msg_zerocopy_from_iter(sk, &msg->msg_iter,
							msg_pl, try_to_copy);
			if (ret)
				goto fallback_to_reg_send;

			num_zc++;
			copied += try_to_copy;

			sk_msg_sg_copy_set(msg_pl, first);
			ret = bpf_exec_tx_verdict(msg_pl, sk, full_record,
						  record_type, &copied,
						  msg->msg_flags);
			if (ret) {
				if (ret == -EINPROGRESS)
					num_async++;
				else if (ret == -ENOMEM)
					goto wait_for_memory;
				else if (ctx->open_rec && ret == -ENOSPC)
					goto rollback_iter;
				else if (ret != -EAGAIN)
					goto send_end;
			}
			continue;
rollback_iter:
			copied -= try_to_copy;
			sk_msg_sg_copy_clear(msg_pl, first);
			iov_iter_revert(&msg->msg_iter,
					msg_pl->sg.size - orig_size);
fallback_to_reg_send:
			sk_msg_trim(sk, msg_pl, orig_size);
		}

		required_size = msg_pl->sg.size + try_to_copy;

		ret = tls_clone_plaintext_msg(sk, required_size);
		if (ret) {
			if (ret != -ENOSPC)
				goto send_end;

			/* Adjust try_to_copy according to the amount that was
			 * actually allocated. The difference is due
			 * to max sg elements limit
			 */
			try_to_copy -= required_size - msg_pl->sg.size;
			full_record = true;
			sk_msg_trim(sk, msg_en,
				    msg_pl->sg.size + prot->overhead_size);
		}

		if (try_to_copy) {
			ret = sk_msg_memcopy_from_iter(sk, &msg->msg_iter,
						       msg_pl, try_to_copy);
			if (ret < 0)
				goto trim_sgl;
		}

		/* Open records defined only if successfully copied, otherwise
		 * we would trim the sg but not reset the open record frags.
		 */
		tls_ctx->pending_open_record_frags = true;
		copied += try_to_copy;
copied:
		if (full_record || eor) {
			ret = bpf_exec_tx_verdict(msg_pl, sk, full_record,
						  record_type, &copied,
						  msg->msg_flags);
			if (ret) {
				if (ret == -EINPROGRESS)
					num_async++;
				else if (ret == -ENOMEM)
					goto wait_for_memory;
				else if (ret != -EAGAIN) {
					if (ret == -ENOSPC)
						ret = 0;
					goto send_end;
				}
			}
		}

		continue;

wait_for_sndbuf:
		set_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
wait_for_memory:
		ret = sk_stream_wait_memory(sk, &timeo);
		if (ret) {
trim_sgl:
			if (ctx->open_rec)
				tls_trim_both_msgs(sk, orig_size);
			goto send_end;
		}

		if (ctx->open_rec && msg_en->sg.size < required_size)
			goto alloc_encrypted;
	}

	if (!num_async) {
		goto send_end;
	} else if (num_zc) {
		/* Wait for pending encryptions to get completed */
		spin_lock_bh(&ctx->encrypt_compl_lock);
		ctx->async_notify = true;

		pending = atomic_read(&ctx->encrypt_pending);
		spin_unlock_bh(&ctx->encrypt_compl_lock);
		if (pending)
			crypto_wait_req(-EINPROGRESS, &ctx->async_wait);
		else
			reinit_completion(&ctx->async_wait.completion);

		/* There can be no concurrent accesses, since we have no
		 * pending encrypt operations
		 */
		WRITE_ONCE(ctx->async_notify, false);

		if (ctx->async_wait.err) {
			ret = ctx->async_wait.err;
			copied = 0;
		}
	}

	/* Transmit if any encryptions have completed */
	if (test_and_clear_bit(BIT_TX_SCHEDULED, &ctx->tx_bitmask)) {
		cancel_delayed_work(&ctx->tx_work.work);
		tls_tx_records(sk, msg->msg_flags);
	}

send_end:
	ret = sk_stream_error(sk, msg->msg_flags, ret);
	return copied > 0 ? copied : ret;
}

int tls_sw_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	int ret;

	if (msg->msg_flags & ~(MSG_MORE | MSG_DONTWAIT | MSG_NOSIGNAL |
			       MSG_CMSG_COMPAT | MSG_SPLICE_PAGES | MSG_EOR |
			       MSG_SENDPAGE_NOPOLICY))
		return -EOPNOTSUPP;

	ret = mutex_lock_interruptible(&tls_ctx->tx_lock);
	if (ret)
		return ret;
	lock_sock(sk);
	ret = tls_sw_sendmsg_locked(sk, msg, size);
	release_sock(sk);
	mutex_unlock(&tls_ctx->tx_lock);
	return ret;
}

/*
 * Handle unexpected EOF during splice without SPLICE_F_MORE set.
 */
void tls_sw_splice_eof(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec;
	struct sk_msg *msg_pl;
	ssize_t copied = 0;
	bool retrying = false;
	int ret = 0;
	int pending;

	if (!ctx->open_rec)
		return;

	mutex_lock(&tls_ctx->tx_lock);
	lock_sock(sk);

retry:
	rec = ctx->open_rec;
	if (!rec)
		goto unlock;

	msg_pl = &rec->msg_plaintext;

	/* Check the BPF advisor and perform transmission. */
	ret = bpf_exec_tx_verdict(msg_pl, sk, false, TLS_RECORD_TYPE_DATA,
				  &copied, 0);
	switch (ret) {
	case 0:
	case -EAGAIN:
		if (retrying)
			goto unlock;
		retrying = true;
		goto retry;
	case -EINPROGRESS:
		break;
	default:
		goto unlock;
	}

	/* Wait for pending encryptions to get completed */
	spin_lock_bh(&ctx->encrypt_compl_lock);
	ctx->async_notify = true;

	pending = atomic_read(&ctx->encrypt_pending);
	spin_unlock_bh(&ctx->encrypt_compl_lock);
	if (pending)
		crypto_wait_req(-EINPROGRESS, &ctx->async_wait);
	else
		reinit_completion(&ctx->async_wait.completion);

	/* There can be no concurrent accesses, since we have no pending
	 * encrypt operations
	 */
	WRITE_ONCE(ctx->async_notify, false);

	if (ctx->async_wait.err)
		goto unlock;

	/* Transmit if any encryptions have completed */
	if (test_and_clear_bit(BIT_TX_SCHEDULED, &ctx->tx_bitmask)) {
		cancel_delayed_work(&ctx->tx_work.work);
		tls_tx_records(sk, 0);
	}

unlock:
	release_sock(sk);
	mutex_unlock(&tls_ctx->tx_lock);
}

static int
tls_rx_rec_wait(struct sock *sk, struct sk_psock *psock, bool nonblock,
		bool released)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int ret = 0;
	long timeo;

	timeo = sock_rcvtimeo(sk, nonblock);

	while (!tls_strp_msg_ready(ctx)) {
		if (!sk_psock_queue_empty(psock))
			return 0;

		if (sk->sk_err)
			return sock_error(sk);

		if (ret < 0)
			return ret;

		if (!skb_queue_empty(&sk->sk_receive_queue)) {
			tls_strp_check_rcv(&ctx->strp);
			if (tls_strp_msg_ready(ctx))
				break;
		}

		if (sk->sk_shutdown & RCV_SHUTDOWN)
			return 0;

		if (sock_flag(sk, SOCK_DONE))
			return 0;

		if (!timeo)
			return -EAGAIN;

		released = true;
		add_wait_queue(sk_sleep(sk), &wait);
		sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
		ret = sk_wait_event(sk, &timeo,
				    tls_strp_msg_ready(ctx) ||
				    !sk_psock_queue_empty(psock),
				    &wait);
		sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);
		remove_wait_queue(sk_sleep(sk), &wait);

		/* Handle signals */
		if (signal_pending(current))
			return sock_intr_errno(timeo);
	}

	tls_strp_msg_load(&ctx->strp, released);

	return 1;
}

static int tls_setup_from_iter(struct iov_iter *from,
			       int length, int *pages_used,
			       struct scatterlist *to,
			       int to_max_pages)
{
	int rc = 0, i = 0, num_elem = *pages_used, maxpages;
	struct page *pages[MAX_SKB_FRAGS];
	unsigned int size = 0;
	ssize_t copied, use;
	size_t offset;

	while (length > 0) {
		i = 0;
		maxpages = to_max_pages - num_elem;
		if (maxpages == 0) {
			rc = -EFAULT;
			goto out;
		}
		copied = iov_iter_get_pages2(from, pages,
					    length,
					    maxpages, &offset);
		if (copied <= 0) {
			rc = -EFAULT;
			goto out;
		}

		length -= copied;
		size += copied;
		while (copied) {
			use = min_t(int, copied, PAGE_SIZE - offset);

			sg_set_page(&to[num_elem],
				    pages[i], use, offset);
			sg_unmark_end(&to[num_elem]);
			/* We do not uncharge memory from this API */

			offset = 0;
			copied -= use;

			i++;
			num_elem++;
		}
	}
	/* Mark the end in the last sg entry if newly added */
	if (num_elem > *pages_used)
		sg_mark_end(&to[num_elem - 1]);
out:
	if (rc)
		iov_iter_revert(from, size);
	*pages_used = num_elem;

	return rc;
}

static struct sk_buff *
tls_alloc_clrtxt_skb(struct sock *sk, struct sk_buff *skb,
		     unsigned int full_len)
{
	struct strp_msg *clr_rxm;
	struct sk_buff *clr_skb;
	int err;

	clr_skb = alloc_skb_with_frags(0, full_len, TLS_PAGE_ORDER,
				       &err, sk->sk_allocation);
	if (!clr_skb)
		return NULL;

	skb_copy_header(clr_skb, skb);
	clr_skb->len = full_len;
	clr_skb->data_len = full_len;

	clr_rxm = strp_msg(clr_skb);
	clr_rxm->offset = 0;

	return clr_skb;
}

/* Decrypt handlers
 *
 * tls_decrypt_sw() and tls_decrypt_device() are decrypt handlers.
 * They must transform the darg in/out argument are as follows:
 *       |          Input            |         Output
 * -------------------------------------------------------------------
 *    zc | Zero-copy decrypt allowed | Zero-copy performed
 * async | Async decrypt allowed     | Async crypto used / in progress
 *   skb |            *              | Output skb
 *
 * If ZC decryption was performed darg.skb will point to the input skb.
 */

/* This function decrypts the input skb into either out_iov or in out_sg
 * or in skb buffers itself. The input parameter 'darg->zc' indicates if
 * zero-copy mode needs to be tried or not. With zero-copy mode, either
 * out_iov or out_sg must be non-NULL. In case both out_iov and out_sg are
 * NULL, then the decryption happens inside skb buffers itself, i.e.
 * zero-copy gets disabled and 'darg->zc' is updated.
 */
static int tls_decrypt_sg(struct sock *sk, struct iov_iter *out_iov,
			  struct scatterlist *out_sg,
			  struct tls_decrypt_arg *darg)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	int n_sgin, n_sgout, aead_size, err, pages = 0;
	struct sk_buff *skb = tls_strp_msg(ctx);
	const struct strp_msg *rxm = strp_msg(skb);
	const struct tls_msg *tlm = tls_msg(skb);
	struct aead_request *aead_req;
	struct scatterlist *sgin = NULL;
	struct scatterlist *sgout = NULL;
	const int data_len = rxm->full_len - prot->overhead_size;
	int tail_pages = !!prot->tail_size;
	struct tls_decrypt_ctx *dctx;
	struct sk_buff *clear_skb;
	int iv_offset = 0;
	u8 *mem;

	n_sgin = skb_nsg(skb, rxm->offset + prot->prepend_size,
			 rxm->full_len - prot->prepend_size);
	if (n_sgin < 1)
		return n_sgin ?: -EBADMSG;

	if (darg->zc && (out_iov || out_sg)) {
		clear_skb = NULL;

		if (out_iov)
			n_sgout = 1 + tail_pages +
				iov_iter_npages_cap(out_iov, INT_MAX, data_len);
		else
			n_sgout = sg_nents(out_sg);
	} else {
		darg->zc = false;

		clear_skb = tls_alloc_clrtxt_skb(sk, skb, rxm->full_len);
		if (!clear_skb)
			return -ENOMEM;

		n_sgout = 1 + skb_shinfo(clear_skb)->nr_frags;
	}

	/* Increment to accommodate AAD */
	n_sgin = n_sgin + 1;

	/* Allocate a single block of memory which contains
	 *   aead_req || tls_decrypt_ctx.
	 * Both structs are variable length.
	 */
	aead_size = sizeof(*aead_req) + crypto_aead_reqsize(ctx->aead_recv);
	aead_size = ALIGN(aead_size, __alignof__(*dctx));
	mem = kmalloc(aead_size + struct_size(dctx, sg, size_add(n_sgin, n_sgout)),
		      sk->sk_allocation);
	if (!mem) {
		err = -ENOMEM;
		goto exit_free_skb;
	}

	/* Segment the allocated memory */
	aead_req = (struct aead_request *)mem;
	dctx = (struct tls_decrypt_ctx *)(mem + aead_size);
	dctx->sk = sk;
	sgin = &dctx->sg[0];
	sgout = &dctx->sg[n_sgin];

	/* For CCM based ciphers, first byte of nonce+iv is a constant */
	switch (prot->cipher_type) {
	case TLS_CIPHER_AES_CCM_128:
		dctx->iv[0] = TLS_AES_CCM_IV_B0_BYTE;
		iv_offset = 1;
		break;
	case TLS_CIPHER_SM4_CCM:
		dctx->iv[0] = TLS_SM4_CCM_IV_B0_BYTE;
		iv_offset = 1;
		break;
	}

	/* Prepare IV */
	if (prot->version == TLS_1_3_VERSION ||
	    prot->cipher_type == TLS_CIPHER_CHACHA20_POLY1305) {
		memcpy(&dctx->iv[iv_offset], tls_ctx->rx.iv,
		       prot->iv_size + prot->salt_size);
	} else {
		err = skb_copy_bits(skb, rxm->offset + TLS_HEADER_SIZE,
				    &dctx->iv[iv_offset] + prot->salt_size,
				    prot->iv_size);
		if (err < 0)
			goto exit_free;
		memcpy(&dctx->iv[iv_offset], tls_ctx->rx.iv, prot->salt_size);
	}
	tls_xor_iv_with_seq(prot, &dctx->iv[iv_offset], tls_ctx->rx.rec_seq);

	/* Prepare AAD */
	tls_make_aad(dctx->aad, rxm->full_len - prot->overhead_size +
		     prot->tail_size,
		     tls_ctx->rx.rec_seq, tlm->control, prot);

	/* Prepare sgin */
	sg_init_table(sgin, n_sgin);
	sg_set_buf(&sgin[0], dctx->aad, prot->aad_size);
	err = skb_to_sgvec(skb, &sgin[1],
			   rxm->offset + prot->prepend_size,
			   rxm->full_len - prot->prepend_size);
	if (err < 0)
		goto exit_free;

	if (clear_skb) {
		sg_init_table(sgout, n_sgout);
		sg_set_buf(&sgout[0], dctx->aad, prot->aad_size);

		err = skb_to_sgvec(clear_skb, &sgout[1], prot->prepend_size,
				   data_len + prot->tail_size);
		if (err < 0)
			goto exit_free;
	} else if (out_iov) {
		sg_init_table(sgout, n_sgout);
		sg_set_buf(&sgout[0], dctx->aad, prot->aad_size);

		err = tls_setup_from_iter(out_iov, data_len, &pages, &sgout[1],
					  (n_sgout - 1 - tail_pages));
		if (err < 0)
			goto exit_free_pages;

		if (prot->tail_size) {
			sg_unmark_end(&sgout[pages]);
			sg_set_buf(&sgout[pages + 1], &dctx->tail,
				   prot->tail_size);
			sg_mark_end(&sgout[pages + 1]);
		}
	} else if (out_sg) {
		memcpy(sgout, out_sg, n_sgout * sizeof(*sgout));
	}

	/* Prepare and submit AEAD request */
	err = tls_do_decryption(sk, sgin, sgout, dctx->iv,
				data_len + prot->tail_size, aead_req, darg);
	if (err)
		goto exit_free_pages;

	darg->skb = clear_skb ?: tls_strp_msg(ctx);
	clear_skb = NULL;

	if (unlikely(darg->async)) {
		err = tls_strp_msg_hold(&ctx->strp, &ctx->async_hold);
		if (err)
			__skb_queue_tail(&ctx->async_hold, darg->skb);
		return err;
	}

	if (prot->tail_size)
		darg->tail = dctx->tail;

exit_free_pages:
	/* Release the pages in case iov was mapped to pages */
	for (; pages > 0; pages--)
		put_page(sg_page(&sgout[pages]));
exit_free:
	kfree(mem);
exit_free_skb:
	consume_skb(clear_skb);
	return err;
}

static int
tls_decrypt_sw(struct sock *sk, struct tls_context *tls_ctx,
	       struct msghdr *msg, struct tls_decrypt_arg *darg)
{
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct strp_msg *rxm;
	int pad, err;

	err = tls_decrypt_sg(sk, &msg->msg_iter, NULL, darg);
	if (err < 0) {
		if (err == -EBADMSG)
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSDECRYPTERROR);
		return err;
	}
	/* keep going even for ->async, the code below is TLS 1.3 */

	/* If opportunistic TLS 1.3 ZC failed retry without ZC */
	if (unlikely(darg->zc && prot->version == TLS_1_3_VERSION &&
		     darg->tail != TLS_RECORD_TYPE_DATA)) {
		darg->zc = false;
		if (!darg->tail)
			TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSRXNOPADVIOL);
		TLS_INC_STATS(sock_net(sk), LINUX_MIB_TLSDECRYPTRETRY);
		return tls_decrypt_sw(sk, tls_ctx, msg, darg);
	}

	pad = tls_padding_length(prot, darg->skb, darg);
	if (pad < 0) {
		if (darg->skb != tls_strp_msg(ctx))
			consume_skb(darg->skb);
		return pad;
	}

	rxm = strp_msg(darg->skb);
	rxm->full_len -= pad;

	return 0;
}

static int
tls_decrypt_device(struct sock *sk, struct msghdr *msg,
		   struct tls_context *tls_ctx, struct tls_decrypt_arg *darg)
{
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct strp_msg *rxm;
	int pad, err;

	if (tls_ctx->rx_conf != TLS_HW)
		return 0;

	err = tls_device_decrypted(sk, tls_ctx);
	if (err <= 0)
		return err;

	pad = tls_padding_length(prot, tls_strp_msg(ctx), darg);
	if (pad < 0)
		return pad;

	darg->async = false;
	darg->skb = tls_strp_msg(ctx);
	/* ->zc downgrade check, in case TLS 1.3 gets here */
	darg->zc &= !(prot->version == TLS_1_3_VERSION &&
		      tls_msg(darg->skb)->control != TLS_RECORD_TYPE_DATA);

	rxm = strp_msg(darg->skb);
	rxm->full_len -= pad;

	if (!darg->zc) {
		/* Non-ZC case needs a real skb */
		darg->skb = tls_strp_msg_detach(ctx);
		if (!darg->skb)
			return -ENOMEM;
	} else {
		unsigned int off, len;

		/* In ZC case nobody cares about the output skb.
		 * Just copy the data here. Note the skb is not fully trimmed.
		 */
		off = rxm->offset + prot->prepend_size;
		len = rxm->full_len - prot->overhead_size;

		err = skb_copy_datagram_msg(darg->skb, off, msg, len);
		if (err)
			return err;
	}
	return 1;
}

static int tls_rx_one_record(struct sock *sk, struct msghdr *msg,
			     struct tls_decrypt_arg *darg)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct strp_msg *rxm;
	int err;

	err = tls_decrypt_device(sk, msg, tls_ctx, darg);
	if (!err)
		err = tls_decrypt_sw(sk, tls_ctx, msg, darg);
	if (err < 0)
		return err;

	rxm = strp_msg(darg->skb);
	rxm->offset += prot->prepend_size;
	rxm->full_len -= prot->overhead_size;
	tls_advance_record_sn(sk, prot, &tls_ctx->rx);

	return 0;
}

int decrypt_skb(struct sock *sk, struct scatterlist *sgout)
{
	struct tls_decrypt_arg darg = { .zc = true, };

	return tls_decrypt_sg(sk, NULL, sgout, &darg);
}

static int tls_record_content_type(struct msghdr *msg, struct tls_msg *tlm,
				   u8 *control)
{
	int err;

	if (!*control) {
		*control = tlm->control;
		if (!*control)
			return -EBADMSG;

		err = put_cmsg(msg, SOL_TLS, TLS_GET_RECORD_TYPE,
			       sizeof(*control), control);
		if (*control != TLS_RECORD_TYPE_DATA) {
			if (err || msg->msg_flags & MSG_CTRUNC)
				return -EIO;
		}
	} else if (*control != tlm->control) {
		return 0;
	}

	return 1;
}

static void tls_rx_rec_done(struct tls_sw_context_rx *ctx)
{
	tls_strp_msg_done(&ctx->strp);
}

/* This function traverses the rx_list in tls receive context to copies the
 * decrypted records into the buffer provided by caller zero copy is not
 * true. Further, the records are removed from the rx_list if it is not a peek
 * case and the record has been consumed completely.
 */
static int process_rx_list(struct tls_sw_context_rx *ctx,
			   struct msghdr *msg,
			   u8 *control,
			   size_t skip,
			   size_t len,
			   bool is_peek)
{
	struct sk_buff *skb = skb_peek(&ctx->rx_list);
	struct tls_msg *tlm;
	ssize_t copied = 0;
	int err;

	while (skip && skb) {
		struct strp_msg *rxm = strp_msg(skb);
		tlm = tls_msg(skb);

		err = tls_record_content_type(msg, tlm, control);
		if (err <= 0)
			goto out;

		if (skip < rxm->full_len)
			break;

		skip = skip - rxm->full_len;
		skb = skb_peek_next(skb, &ctx->rx_list);
	}

	while (len && skb) {
		struct sk_buff *next_skb;
		struct strp_msg *rxm = strp_msg(skb);
		int chunk = min_t(unsigned int, rxm->full_len - skip, len);

		tlm = tls_msg(skb);

		err = tls_record_content_type(msg, tlm, control);
		if (err <= 0)
			goto out;

		err = skb_copy_datagram_msg(skb, rxm->offset + skip,
					    msg, chunk);
		if (err < 0)
			goto out;

		len = len - chunk;
		copied = copied + chunk;

		/* Consume the data from record if it is non-peek case*/
		if (!is_peek) {
			rxm->offset = rxm->offset + chunk;
			rxm->full_len = rxm->full_len - chunk;

			/* Return if there is unconsumed data in the record */
			if (rxm->full_len - skip)
				break;
		}

		/* The remaining skip-bytes must lie in 1st record in rx_list.
		 * So from the 2nd record, 'skip' should be 0.
		 */
		skip = 0;

		if (msg)
			msg->msg_flags |= MSG_EOR;

		next_skb = skb_peek_next(skb, &ctx->rx_list);

		if (!is_peek) {
			__skb_unlink(skb, &ctx->rx_list);
			consume_skb(skb);
		}

		skb = next_skb;
	}
	err = 0;

out:
	return copied ? : err;
}

static bool
tls_read_flush_backlog(struct sock *sk, struct tls_prot_info *prot,
		       size_t len_left, size_t decrypted, ssize_t done,
		       size_t *flushed_at)
{
	size_t max_rec;

	if (len_left <= decrypted)
		return false;

	max_rec = prot->overhead_size - prot->tail_size + TLS_MAX_PAYLOAD_SIZE;
	if (done - *flushed_at < SZ_128K && tcp_inq(sk) > max_rec)
		return false;

	*flushed_at = done;
	return sk_flush_backlog(sk);
}

static int tls_rx_reader_acquire(struct sock *sk, struct tls_sw_context_rx *ctx,
				 bool nonblock)
{
	long timeo;
	int ret;

	timeo = sock_rcvtimeo(sk, nonblock);

	while (unlikely(ctx->reader_present)) {
		DEFINE_WAIT_FUNC(wait, woken_wake_function);

		ctx->reader_contended = 1;

		add_wait_queue(&ctx->wq, &wait);
		ret = sk_wait_event(sk, &timeo,
				    !READ_ONCE(ctx->reader_present), &wait);
		remove_wait_queue(&ctx->wq, &wait);

		if (timeo <= 0)
			return -EAGAIN;
		if (signal_pending(current))
			return sock_intr_errno(timeo);
		if (ret < 0)
			return ret;
	}

	WRITE_ONCE(ctx->reader_present, 1);

	return 0;
}

static int tls_rx_reader_lock(struct sock *sk, struct tls_sw_context_rx *ctx,
			      bool nonblock)
{
	int err;

	lock_sock(sk);
	err = tls_rx_reader_acquire(sk, ctx, nonblock);
	if (err)
		release_sock(sk);
	return err;
}

static void tls_rx_reader_release(struct sock *sk, struct tls_sw_context_rx *ctx)
{
	if (unlikely(ctx->reader_contended)) {
		if (wq_has_sleeper(&ctx->wq))
			wake_up(&ctx->wq);
		else
			ctx->reader_contended = 0;

		WARN_ON_ONCE(!ctx->reader_present);
	}

	WRITE_ONCE(ctx->reader_present, 0);
}

static void tls_rx_reader_unlock(struct sock *sk, struct tls_sw_context_rx *ctx)
{
	tls_rx_reader_release(sk, ctx);
	release_sock(sk);
}

int tls_sw_recvmsg(struct sock *sk,
		   struct msghdr *msg,
		   size_t len,
		   int flags,
		   int *addr_len)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	ssize_t decrypted = 0, async_copy_bytes = 0;
	struct sk_psock *psock;
	unsigned char control = 0;
	size_t flushed_at = 0;
	struct strp_msg *rxm;
	struct tls_msg *tlm;
	ssize_t copied = 0;
	bool async = false;
	int target, err;
	bool is_kvec = iov_iter_is_kvec(&msg->msg_iter);
	bool is_peek = flags & MSG_PEEK;
	bool released = true;
	bool bpf_strp_enabled;
	bool zc_capable;

	if (unlikely(flags & MSG_ERRQUEUE))
		return sock_recv_errqueue(sk, msg, len, SOL_IP, IP_RECVERR);

	psock = sk_psock_get(sk);
	err = tls_rx_reader_lock(sk, ctx, flags & MSG_DONTWAIT);
	if (err < 0)
		return err;
	bpf_strp_enabled = sk_psock_strp_enabled(psock);

	/* If crypto failed the connection is broken */
	err = ctx->async_wait.err;
	if (err)
		goto end;

	/* Process pending decrypted records. It must be non-zero-copy */
	err = process_rx_list(ctx, msg, &control, 0, len, is_peek);
	if (err < 0)
		goto end;

	copied = err;
	if (len <= copied)
		goto end;

	target = sock_rcvlowat(sk, flags & MSG_WAITALL, len);
	len = len - copied;

	zc_capable = !bpf_strp_enabled && !is_kvec && !is_peek &&
		ctx->zc_capable;
	decrypted = 0;
	while (len && (decrypted + copied < target || tls_strp_msg_ready(ctx))) {
		struct tls_decrypt_arg darg;
		int to_decrypt, chunk;

		err = tls_rx_rec_wait(sk, psock, flags & MSG_DONTWAIT,
				      released);
		if (err <= 0) {
			if (psock) {
				chunk = sk_msg_recvmsg(sk, psock, msg, len,
						       flags);
				if (chunk > 0) {
					decrypted += chunk;
					len -= chunk;
					continue;
				}
			}
			goto recv_end;
		}

		memset(&darg.inargs, 0, sizeof(darg.inargs));

		rxm = strp_msg(tls_strp_msg(ctx));
		tlm = tls_msg(tls_strp_msg(ctx));

		to_decrypt = rxm->full_len - prot->overhead_size;

		if (zc_capable && to_decrypt <= len &&
		    tlm->control == TLS_RECORD_TYPE_DATA)
			darg.zc = true;

		/* Do not use async mode if record is non-data */
		if (tlm->control == TLS_RECORD_TYPE_DATA && !bpf_strp_enabled)
			darg.async = ctx->async_capable;
		else
			darg.async = false;

		err = tls_rx_one_record(sk, msg, &darg);
		if (err < 0) {
			tls_err_abort(sk, -EBADMSG);
			goto recv_end;
		}

		async |= darg.async;

		/* If the type of records being processed is not known yet,
		 * set it to record type just dequeued. If it is already known,
		 * but does not match the record type just dequeued, go to end.
		 * We always get record type here since for tls1.2, record type
		 * is known just after record is dequeued from stream parser.
		 * For tls1.3, we disable async.
		 */
		err = tls_record_content_type(msg, tls_msg(darg.skb), &control);
		if (err <= 0) {
			DEBUG_NET_WARN_ON_ONCE(darg.zc);
			tls_rx_rec_done(ctx);
put_on_rx_list_err:
			__skb_queue_tail(&ctx->rx_list, darg.skb);
			goto recv_end;
		}

		/* periodically flush backlog, and feed strparser */
		released = tls_read_flush_backlog(sk, prot, len, to_decrypt,
						  decrypted + copied,
						  &flushed_at);

		/* TLS 1.3 may have updated the length by more than overhead */
		rxm = strp_msg(darg.skb);
		chunk = rxm->full_len;
		tls_rx_rec_done(ctx);

		if (!darg.zc) {
			bool partially_consumed = chunk > len;
			struct sk_buff *skb = darg.skb;

			DEBUG_NET_WARN_ON_ONCE(darg.skb == ctx->strp.anchor);

			if (async) {
				/* TLS 1.2-only, to_decrypt must be text len */
				chunk = min_t(int, to_decrypt, len);
				async_copy_bytes += chunk;
put_on_rx_list:
				decrypted += chunk;
				len -= chunk;
				__skb_queue_tail(&ctx->rx_list, skb);
				continue;
			}

			if (bpf_strp_enabled) {
				released = true;
				err = sk_psock_tls_strp_read(psock, skb);
				if (err != __SK_PASS) {
					rxm->offset = rxm->offset + rxm->full_len;
					rxm->full_len = 0;
					if (err == __SK_DROP)
						consume_skb(skb);
					continue;
				}
			}

			if (partially_consumed)
				chunk = len;

			err = skb_copy_datagram_msg(skb, rxm->offset,
						    msg, chunk);
			if (err < 0)
				goto put_on_rx_list_err;

			if (is_peek)
				goto put_on_rx_list;

			if (partially_consumed) {
				rxm->offset += chunk;
				rxm->full_len -= chunk;
				goto put_on_rx_list;
			}

			consume_skb(skb);
		}

		decrypted += chunk;
		len -= chunk;

		/* Return full control message to userspace before trying
		 * to parse another message type
		 */
		msg->msg_flags |= MSG_EOR;
		if (control != TLS_RECORD_TYPE_DATA)
			break;
	}

recv_end:
	if (async) {
		int ret, pending;

		/* Wait for all previously submitted records to be decrypted */
		spin_lock_bh(&ctx->decrypt_compl_lock);
		reinit_completion(&ctx->async_wait.completion);
		pending = atomic_read(&ctx->decrypt_pending);
		spin_unlock_bh(&ctx->decrypt_compl_lock);
		ret = 0;
		if (pending)
			ret = crypto_wait_req(-EINPROGRESS, &ctx->async_wait);
		__skb_queue_purge(&ctx->async_hold);

		if (ret) {
			if (err >= 0 || err == -EINPROGRESS)
				err = ret;
			decrypted = 0;
			goto end;
		}

		/* Drain records from the rx_list & copy if required */
		if (is_peek || is_kvec)
			err = process_rx_list(ctx, msg, &control, copied,
					      decrypted, is_peek);
		else
			err = process_rx_list(ctx, msg, &control, 0,
					      async_copy_bytes, is_peek);
		decrypted += max(err, 0);
	}

	copied += decrypted;

end:
	tls_rx_reader_unlock(sk, ctx);
	if (psock)
		sk_psock_put(sk, psock);
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
	struct tls_msg *tlm;
	struct sk_buff *skb;
	ssize_t copied = 0;
	int chunk;
	int err;

	err = tls_rx_reader_lock(sk, ctx, flags & SPLICE_F_NONBLOCK);
	if (err < 0)
		return err;

	if (!skb_queue_empty(&ctx->rx_list)) {
		skb = __skb_dequeue(&ctx->rx_list);
	} else {
		struct tls_decrypt_arg darg;

		err = tls_rx_rec_wait(sk, NULL, flags & SPLICE_F_NONBLOCK,
				      true);
		if (err <= 0)
			goto splice_read_end;

		memset(&darg.inargs, 0, sizeof(darg.inargs));

		err = tls_rx_one_record(sk, NULL, &darg);
		if (err < 0) {
			tls_err_abort(sk, -EBADMSG);
			goto splice_read_end;
		}

		tls_rx_rec_done(ctx);
		skb = darg.skb;
	}

	rxm = strp_msg(skb);
	tlm = tls_msg(skb);

	/* splice does not support reading control messages */
	if (tlm->control != TLS_RECORD_TYPE_DATA) {
		err = -EINVAL;
		goto splice_requeue;
	}

	chunk = min_t(unsigned int, rxm->full_len, len);
	copied = skb_splice_bits(skb, sk, rxm->offset, pipe, chunk, flags);
	if (copied < 0)
		goto splice_requeue;

	if (chunk < rxm->full_len) {
		rxm->offset += len;
		rxm->full_len -= len;
		goto splice_requeue;
	}

	consume_skb(skb);

splice_read_end:
	tls_rx_reader_unlock(sk, ctx);
	return copied ? : err;

splice_requeue:
	__skb_queue_head(&ctx->rx_list, skb);
	goto splice_read_end;
}

int tls_sw_read_sock(struct sock *sk, read_descriptor_t *desc,
		     sk_read_actor_t read_actor)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	struct strp_msg *rxm = NULL;
	struct sk_buff *skb = NULL;
	struct sk_psock *psock;
	size_t flushed_at = 0;
	bool released = true;
	struct tls_msg *tlm;
	ssize_t copied = 0;
	ssize_t decrypted;
	int err, used;

	psock = sk_psock_get(sk);
	if (psock) {
		sk_psock_put(sk, psock);
		return -EINVAL;
	}
	err = tls_rx_reader_acquire(sk, ctx, true);
	if (err < 0)
		return err;

	/* If crypto failed the connection is broken */
	err = ctx->async_wait.err;
	if (err)
		goto read_sock_end;

	decrypted = 0;
	do {
		if (!skb_queue_empty(&ctx->rx_list)) {
			skb = __skb_dequeue(&ctx->rx_list);
			rxm = strp_msg(skb);
			tlm = tls_msg(skb);
		} else {
			struct tls_decrypt_arg darg;

			err = tls_rx_rec_wait(sk, NULL, true, released);
			if (err <= 0)
				goto read_sock_end;

			memset(&darg.inargs, 0, sizeof(darg.inargs));

			err = tls_rx_one_record(sk, NULL, &darg);
			if (err < 0) {
				tls_err_abort(sk, -EBADMSG);
				goto read_sock_end;
			}

			released = tls_read_flush_backlog(sk, prot, INT_MAX,
							  0, decrypted,
							  &flushed_at);
			skb = darg.skb;
			rxm = strp_msg(skb);
			tlm = tls_msg(skb);
			decrypted += rxm->full_len;

			tls_rx_rec_done(ctx);
		}

		/* read_sock does not support reading control messages */
		if (tlm->control != TLS_RECORD_TYPE_DATA) {
			err = -EINVAL;
			goto read_sock_requeue;
		}

		used = read_actor(desc, skb, rxm->offset, rxm->full_len);
		if (used <= 0) {
			if (!copied)
				err = used;
			goto read_sock_requeue;
		}
		copied += used;
		if (used < rxm->full_len) {
			rxm->offset += used;
			rxm->full_len -= used;
			if (!desc->count)
				goto read_sock_requeue;
		} else {
			consume_skb(skb);
			if (!desc->count)
				skb = NULL;
		}
	} while (skb);

read_sock_end:
	tls_rx_reader_release(sk, ctx);
	return copied ? : err;

read_sock_requeue:
	__skb_queue_head(&ctx->rx_list, skb);
	goto read_sock_end;
}

bool tls_sw_sock_is_readable(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	bool ingress_empty = true;
	struct sk_psock *psock;

	rcu_read_lock();
	psock = sk_psock(sk);
	if (psock)
		ingress_empty = list_empty(&psock->ingress_msg);
	rcu_read_unlock();

	return !ingress_empty || tls_strp_msg_ready(ctx) ||
		!skb_queue_empty(&ctx->rx_list);
}

int tls_rx_msg_size(struct tls_strparser *strp, struct sk_buff *skb)
{
	struct tls_context *tls_ctx = tls_get_ctx(strp->sk);
	struct tls_prot_info *prot = &tls_ctx->prot_info;
	char header[TLS_HEADER_SIZE + TLS_MAX_IV_SIZE];
	size_t cipher_overhead;
	size_t data_len = 0;
	int ret;

	/* Verify that we have a full TLS header, or wait for more data */
	if (strp->stm.offset + prot->prepend_size > skb->len)
		return 0;

	/* Sanity-check size of on-stack buffer. */
	if (WARN_ON(prot->prepend_size > sizeof(header))) {
		ret = -EINVAL;
		goto read_failure;
	}

	/* Linearize header to local buffer */
	ret = skb_copy_bits(skb, strp->stm.offset, header, prot->prepend_size);
	if (ret < 0)
		goto read_failure;

	strp->mark = header[0];

	data_len = ((header[4] & 0xFF) | (header[3] << 8));

	cipher_overhead = prot->tag_size;
	if (prot->version != TLS_1_3_VERSION &&
	    prot->cipher_type != TLS_CIPHER_CHACHA20_POLY1305)
		cipher_overhead += prot->iv_size;

	if (data_len > TLS_MAX_PAYLOAD_SIZE + cipher_overhead +
	    prot->tail_size) {
		ret = -EMSGSIZE;
		goto read_failure;
	}
	if (data_len < cipher_overhead) {
		ret = -EBADMSG;
		goto read_failure;
	}

	/* Note that both TLS1.3 and TLS1.2 use TLS_1_2 version here */
	if (header[1] != TLS_1_2_VERSION_MINOR ||
	    header[2] != TLS_1_2_VERSION_MAJOR) {
		ret = -EINVAL;
		goto read_failure;
	}

	tls_device_rx_resync_new_rec(strp->sk, data_len + TLS_HEADER_SIZE,
				     TCP_SKB_CB(skb)->seq + strp->stm.offset);
	return data_len + TLS_HEADER_SIZE;

read_failure:
	tls_err_abort(strp->sk, ret);

	return ret;
}

void tls_rx_msg_ready(struct tls_strparser *strp)
{
	struct tls_sw_context_rx *ctx;

	ctx = container_of(strp, struct tls_sw_context_rx, strp);
	ctx->saved_data_ready(strp->sk);
}

static void tls_data_ready(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);
	struct sk_psock *psock;
	gfp_t alloc_save;

	trace_sk_data_ready(sk);

	alloc_save = sk->sk_allocation;
	sk->sk_allocation = GFP_ATOMIC;
	tls_strp_data_ready(&ctx->strp);
	sk->sk_allocation = alloc_save;

	psock = sk_psock_get(sk);
	if (psock) {
		if (!list_empty(&psock->ingress_msg))
			ctx->saved_data_ready(sk);
		sk_psock_put(sk, psock);
	}
}

void tls_sw_cancel_work_tx(struct tls_context *tls_ctx)
{
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);

	set_bit(BIT_TX_CLOSING, &ctx->tx_bitmask);
	set_bit(BIT_TX_SCHEDULED, &ctx->tx_bitmask);
	cancel_delayed_work_sync(&ctx->tx_work.work);
}

void tls_sw_release_resources_tx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);
	struct tls_rec *rec, *tmp;
	int pending;

	/* Wait for any pending async encryptions to complete */
	spin_lock_bh(&ctx->encrypt_compl_lock);
	ctx->async_notify = true;
	pending = atomic_read(&ctx->encrypt_pending);
	spin_unlock_bh(&ctx->encrypt_compl_lock);

	if (pending)
		crypto_wait_req(-EINPROGRESS, &ctx->async_wait);

	tls_tx_records(sk, -1);

	/* Free up un-sent records in tx_list. First, free
	 * the partially sent record if any at head of tx_list.
	 */
	if (tls_ctx->partially_sent_record) {
		tls_free_partial_record(sk, tls_ctx);
		rec = list_first_entry(&ctx->tx_list,
				       struct tls_rec, list);
		list_del(&rec->list);
		sk_msg_free(sk, &rec->msg_plaintext);
		kfree(rec);
	}

	list_for_each_entry_safe(rec, tmp, &ctx->tx_list, list) {
		list_del(&rec->list);
		sk_msg_free(sk, &rec->msg_encrypted);
		sk_msg_free(sk, &rec->msg_plaintext);
		kfree(rec);
	}

	crypto_free_aead(ctx->aead_send);
	tls_free_open_rec(sk);
}

void tls_sw_free_ctx_tx(struct tls_context *tls_ctx)
{
	struct tls_sw_context_tx *ctx = tls_sw_ctx_tx(tls_ctx);

	kfree(ctx);
}

void tls_sw_release_resources_rx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	if (ctx->aead_recv) {
		__skb_queue_purge(&ctx->rx_list);
		crypto_free_aead(ctx->aead_recv);
		tls_strp_stop(&ctx->strp);
		/* If tls_sw_strparser_arm() was not called (cleanup paths)
		 * we still want to tls_strp_stop(), but sk->sk_data_ready was
		 * never swapped.
		 */
		if (ctx->saved_data_ready) {
			write_lock_bh(&sk->sk_callback_lock);
			sk->sk_data_ready = ctx->saved_data_ready;
			write_unlock_bh(&sk->sk_callback_lock);
		}
	}
}

void tls_sw_strparser_done(struct tls_context *tls_ctx)
{
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	tls_strp_done(&ctx->strp);
}

void tls_sw_free_ctx_rx(struct tls_context *tls_ctx)
{
	struct tls_sw_context_rx *ctx = tls_sw_ctx_rx(tls_ctx);

	kfree(ctx);
}

void tls_sw_free_resources_rx(struct sock *sk)
{
	struct tls_context *tls_ctx = tls_get_ctx(sk);

	tls_sw_release_resources_rx(sk);
	tls_sw_free_ctx_rx(tls_ctx);
}

/* The work handler to transmitt the encrypted records in tx_list */
static void tx_work_handler(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct tx_work *tx_work = container_of(delayed_work,
					       struct tx_work, work);
	struct sock *sk = tx_work->sk;
	struct tls_context *tls_ctx = tls_get_ctx(sk);
	struct tls_sw_context_tx *ctx;

	if (unlikely(!tls_ctx))
		return;

	ctx = tls_sw_ctx_tx(tls_ctx);
	if (test_bit(BIT_TX_CLOSING, &ctx->tx_bitmask))
		return;

	if (!test_and_clear_bit(BIT_TX_SCHEDULED, &ctx->tx_bitmask))
		return;

	if (mutex_trylock(&tls_ctx->tx_lock)) {
		lock_sock(sk);
		tls_tx_records(sk, -1);
		release_sock(sk);
		mutex_unlock(&tls_ctx->tx_lock);
	} else if (!test_and_set_bit(BIT_TX_SCHEDULED, &ctx->tx_bitmask)) {
		/* Someone is holding the tx_lock, they will likely run Tx
		 * and cancel the work on their way out of the lock section.
		 * Schedule a long delay just in case.
		 */
		schedule_delayed_work(&ctx->tx_work.work, msecs_to_jiffies(10));
	}
}

static bool tls_is_tx_ready(struct tls_sw_context_tx *ctx)
{
	struct tls_rec *rec;

	rec = list_first_entry_or_null(&ctx->tx_list, struct tls_rec, list);
	if (!rec)
		return false;

	return READ_ONCE(rec->tx_ready);
}

void tls_sw_write_space(struct sock *sk, struct tls_context *ctx)
{
	struct tls_sw_context_tx *tx_ctx = tls_sw_ctx_tx(ctx);

	/* Schedule the transmission if tx list is ready */
	if (tls_is_tx_ready(tx_ctx) &&
	    !test_and_set_bit(BIT_TX_SCHEDULED, &tx_ctx->tx_bitmask))
		schedule_delayed_work(&tx_ctx->tx_work.work, 0);
}

void tls_sw_strparser_arm(struct sock *sk, struct tls_context *tls_ctx)
{
	struct tls_sw_context_rx *rx_ctx = tls_sw_ctx_rx(tls_ctx);

	write_lock_bh(&sk->sk_callback_lock);
	rx_ctx->saved_data_ready = sk->sk_data_ready;
	sk->sk_data_ready = tls_data_ready;
	write_unlock_bh(&sk->sk_callback_lock);
}

void tls_update_rx_zc_capable(struct tls_context *tls_ctx)
{
	struct tls_sw_context_rx *rx_ctx = tls_sw_ctx_rx(tls_ctx);

	rx_ctx->zc_capable = tls_ctx->rx_no_pad ||
		tls_ctx->prot_info.version != TLS_1_3_VERSION;
}

static struct tls_sw_context_tx *init_ctx_tx(struct tls_context *ctx, struct sock *sk)
{
	struct tls_sw_context_tx *sw_ctx_tx;

	if (!ctx->priv_ctx_tx) {
		sw_ctx_tx = kzalloc(sizeof(*sw_ctx_tx), GFP_KERNEL);
		if (!sw_ctx_tx)
			return NULL;
	} else {
		sw_ctx_tx = ctx->priv_ctx_tx;
	}

	crypto_init_wait(&sw_ctx_tx->async_wait);
	spin_lock_init(&sw_ctx_tx->encrypt_compl_lock);
	INIT_LIST_HEAD(&sw_ctx_tx->tx_list);
	INIT_DELAYED_WORK(&sw_ctx_tx->tx_work.work, tx_work_handler);
	sw_ctx_tx->tx_work.sk = sk;

	return sw_ctx_tx;
}

static struct tls_sw_context_rx *init_ctx_rx(struct tls_context *ctx)
{
	struct tls_sw_context_rx *sw_ctx_rx;

	if (!ctx->priv_ctx_rx) {
		sw_ctx_rx = kzalloc(sizeof(*sw_ctx_rx), GFP_KERNEL);
		if (!sw_ctx_rx)
			return NULL;
	} else {
		sw_ctx_rx = ctx->priv_ctx_rx;
	}

	crypto_init_wait(&sw_ctx_rx->async_wait);
	spin_lock_init(&sw_ctx_rx->decrypt_compl_lock);
	init_waitqueue_head(&sw_ctx_rx->wq);
	skb_queue_head_init(&sw_ctx_rx->rx_list);
	skb_queue_head_init(&sw_ctx_rx->async_hold);

	return sw_ctx_rx;
}

int init_prot_info(struct tls_prot_info *prot,
		   const struct tls_crypto_info *crypto_info,
		   const struct tls_cipher_desc *cipher_desc)
{
	u16 nonce_size = cipher_desc->nonce;

	if (crypto_info->version == TLS_1_3_VERSION) {
		nonce_size = 0;
		prot->aad_size = TLS_HEADER_SIZE;
		prot->tail_size = 1;
	} else {
		prot->aad_size = TLS_AAD_SPACE_SIZE;
		prot->tail_size = 0;
	}

	/* Sanity-check the sizes for stack allocations. */
	if (nonce_size > TLS_MAX_IV_SIZE || prot->aad_size > TLS_MAX_AAD_SIZE)
		return -EINVAL;

	prot->version = crypto_info->version;
	prot->cipher_type = crypto_info->cipher_type;
	prot->prepend_size = TLS_HEADER_SIZE + nonce_size;
	prot->tag_size = cipher_desc->tag;
	prot->overhead_size = prot->prepend_size + prot->tag_size + prot->tail_size;
	prot->iv_size = cipher_desc->iv;
	prot->salt_size = cipher_desc->salt;
	prot->rec_seq_size = cipher_desc->rec_seq;

	return 0;
}

int tls_set_sw_offload(struct sock *sk, int tx)
{
	struct tls_sw_context_tx *sw_ctx_tx = NULL;
	struct tls_sw_context_rx *sw_ctx_rx = NULL;
	const struct tls_cipher_desc *cipher_desc;
	struct tls_crypto_info *crypto_info;
	char *iv, *rec_seq, *key, *salt;
	struct cipher_context *cctx;
	struct tls_prot_info *prot;
	struct crypto_aead **aead;
	struct tls_context *ctx;
	struct crypto_tfm *tfm;
	int rc = 0;

	ctx = tls_get_ctx(sk);
	prot = &ctx->prot_info;

	if (tx) {
		ctx->priv_ctx_tx = init_ctx_tx(ctx, sk);
		if (!ctx->priv_ctx_tx)
			return -ENOMEM;

		sw_ctx_tx = ctx->priv_ctx_tx;
		crypto_info = &ctx->crypto_send.info;
		cctx = &ctx->tx;
		aead = &sw_ctx_tx->aead_send;
	} else {
		ctx->priv_ctx_rx = init_ctx_rx(ctx);
		if (!ctx->priv_ctx_rx)
			return -ENOMEM;

		sw_ctx_rx = ctx->priv_ctx_rx;
		crypto_info = &ctx->crypto_recv.info;
		cctx = &ctx->rx;
		aead = &sw_ctx_rx->aead_recv;
	}

	cipher_desc = get_cipher_desc(crypto_info->cipher_type);
	if (!cipher_desc) {
		rc = -EINVAL;
		goto free_priv;
	}

	rc = init_prot_info(prot, crypto_info, cipher_desc);
	if (rc)
		goto free_priv;

	iv = crypto_info_iv(crypto_info, cipher_desc);
	key = crypto_info_key(crypto_info, cipher_desc);
	salt = crypto_info_salt(crypto_info, cipher_desc);
	rec_seq = crypto_info_rec_seq(crypto_info, cipher_desc);

	memcpy(cctx->iv, salt, cipher_desc->salt);
	memcpy(cctx->iv + cipher_desc->salt, iv, cipher_desc->iv);
	memcpy(cctx->rec_seq, rec_seq, cipher_desc->rec_seq);

	if (!*aead) {
		*aead = crypto_alloc_aead(cipher_desc->cipher_name, 0, 0);
		if (IS_ERR(*aead)) {
			rc = PTR_ERR(*aead);
			*aead = NULL;
			goto free_priv;
		}
	}

	ctx->push_pending_record = tls_sw_push_pending_record;

	rc = crypto_aead_setkey(*aead, key, cipher_desc->key);
	if (rc)
		goto free_aead;

	rc = crypto_aead_setauthsize(*aead, prot->tag_size);
	if (rc)
		goto free_aead;

	if (sw_ctx_rx) {
		tfm = crypto_aead_tfm(sw_ctx_rx->aead_recv);

		tls_update_rx_zc_capable(ctx);
		sw_ctx_rx->async_capable =
			crypto_info->version != TLS_1_3_VERSION &&
			!!(tfm->__crt_alg->cra_flags & CRYPTO_ALG_ASYNC);

		rc = tls_strp_init(&sw_ctx_rx->strp, sk);
		if (rc)
			goto free_aead;
	}

	goto out;

free_aead:
	crypto_free_aead(*aead);
	*aead = NULL;
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
