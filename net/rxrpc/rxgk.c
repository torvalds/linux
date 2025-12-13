// SPDX-License-Identifier: GPL-2.0-or-later
/* GSSAPI-based RxRPC security
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/key-type.h>
#include "ar-internal.h"
#include "rxgk_common.h"

/*
 * Parse the information from a server key
 */
static int rxgk_preparse_server_key(struct key_preparsed_payload *prep)
{
	const struct krb5_enctype *krb5;
	struct krb5_buffer *server_key = (void *)&prep->payload.data[2];
	unsigned int service, sec_class, kvno, enctype;
	int n = 0;

	_enter("%zu", prep->datalen);

	if (sscanf(prep->orig_description, "%u:%u:%u:%u%n",
		   &service, &sec_class, &kvno, &enctype, &n) != 4)
		return -EINVAL;

	if (prep->orig_description[n])
		return -EINVAL;

	krb5 = crypto_krb5_find_enctype(enctype);
	if (!krb5)
		return -ENOPKG;

	prep->payload.data[0] = (struct krb5_enctype *)krb5;

	if (prep->datalen != krb5->key_len)
		return -EKEYREJECTED;

	server_key->len = prep->datalen;
	server_key->data = kmemdup(prep->data, prep->datalen, GFP_KERNEL);
	if (!server_key->data)
		return -ENOMEM;

	_leave(" = 0");
	return 0;
}

static void rxgk_free_server_key(union key_payload *payload)
{
	struct krb5_buffer *server_key = (void *)&payload->data[2];

	kfree_sensitive(server_key->data);
}

static void rxgk_free_preparse_server_key(struct key_preparsed_payload *prep)
{
	rxgk_free_server_key(&prep->payload);
}

static void rxgk_destroy_server_key(struct key *key)
{
	rxgk_free_server_key(&key->payload);
}

static void rxgk_describe_server_key(const struct key *key, struct seq_file *m)
{
	const struct krb5_enctype *krb5 = key->payload.data[0];

	if (krb5)
		seq_printf(m, ": %s", krb5->name);
}

/*
 * Handle rekeying the connection when we see our limits overrun or when the
 * far side decided to rekey.
 *
 * Returns a ref on the context if successful or -ESTALE if the key is out of
 * date.
 */
static struct rxgk_context *rxgk_rekey(struct rxrpc_connection *conn,
				       const u16 *specific_key_number)
{
	struct rxgk_context *gk, *dead = NULL;
	unsigned int key_number, current_key, mask = ARRAY_SIZE(conn->rxgk.keys) - 1;
	bool crank = false;

	_enter("%d", specific_key_number ? *specific_key_number : -1);

	mutex_lock(&conn->security_lock);

	current_key = conn->rxgk.key_number;
	if (!specific_key_number) {
		key_number = current_key;
	} else {
		if (*specific_key_number == (u16)current_key)
			key_number = current_key;
		else if (*specific_key_number == (u16)(current_key - 1))
			key_number = current_key - 1;
		else if (*specific_key_number == (u16)(current_key + 1))
			goto crank_window;
		else
			goto bad_key;
	}

	gk = conn->rxgk.keys[key_number & mask];
	if (!gk)
		goto generate_key;
	if (!specific_key_number &&
	    test_bit(RXGK_TK_NEEDS_REKEY, &gk->flags))
		goto crank_window;

grab:
	refcount_inc(&gk->usage);
	mutex_unlock(&conn->security_lock);
	rxgk_put(dead);
	return gk;

crank_window:
	trace_rxrpc_rxgk_rekey(conn, current_key,
			       specific_key_number ? *specific_key_number : -1);
	if (current_key == UINT_MAX)
		goto bad_key;
	if (current_key + 1 == UINT_MAX)
		set_bit(RXRPC_CONN_DONT_REUSE, &conn->flags);

	key_number = current_key + 1;
	if (WARN_ON(conn->rxgk.keys[key_number & mask]))
		goto bad_key;
	crank = true;

generate_key:
	gk = conn->rxgk.keys[current_key & mask];
	gk = rxgk_generate_transport_key(conn, gk->key, key_number, GFP_NOFS);
	if (IS_ERR(gk)) {
		mutex_unlock(&conn->security_lock);
		return gk;
	}

	write_lock(&conn->security_use_lock);
	if (crank) {
		current_key++;
		conn->rxgk.key_number = current_key;
		dead = conn->rxgk.keys[(current_key - 2) & mask];
		conn->rxgk.keys[(current_key - 2) & mask] = NULL;
	}
	conn->rxgk.keys[current_key & mask] = gk;
	write_unlock(&conn->security_use_lock);
	goto grab;

bad_key:
	mutex_unlock(&conn->security_lock);
	return ERR_PTR(-ESTALE);
}

/*
 * Get the specified keying context.
 *
 * Returns a ref on the context if successful or -ESTALE if the key is out of
 * date.
 */
static struct rxgk_context *rxgk_get_key(struct rxrpc_connection *conn,
					 const u16 *specific_key_number)
{
	struct rxgk_context *gk;
	unsigned int key_number, current_key, mask = ARRAY_SIZE(conn->rxgk.keys) - 1;

	_enter("{%u},%d",
	       conn->rxgk.key_number, specific_key_number ? *specific_key_number : -1);

	read_lock(&conn->security_use_lock);

	current_key = conn->rxgk.key_number;
	if (!specific_key_number) {
		key_number = current_key;
	} else {
		/* Only the bottom 16 bits of the key number are exposed in the
		 * header, so we try and keep the upper 16 bits in step.  The
		 * whole 32 bits are used to generate the TK.
		 */
		if (*specific_key_number == (u16)current_key)
			key_number = current_key;
		else if (*specific_key_number == (u16)(current_key - 1))
			key_number = current_key - 1;
		else if (*specific_key_number == (u16)(current_key + 1))
			goto rekey;
		else
			goto bad_key;
	}

	gk = conn->rxgk.keys[key_number & mask];
	if (!gk)
		goto slow_path;
	if (!specific_key_number &&
	    key_number < UINT_MAX) {
		if (time_after(jiffies, gk->expiry) ||
		    gk->bytes_remaining < 0) {
			set_bit(RXGK_TK_NEEDS_REKEY, &gk->flags);
			goto slow_path;
		}

		if (test_bit(RXGK_TK_NEEDS_REKEY, &gk->flags))
			goto slow_path;
	}

	refcount_inc(&gk->usage);
	read_unlock(&conn->security_use_lock);
	return gk;

rekey:
	_debug("rekey");
	if (current_key == UINT_MAX)
		goto bad_key;
	gk = conn->rxgk.keys[current_key & mask];
	if (gk)
		set_bit(RXGK_TK_NEEDS_REKEY, &gk->flags);
slow_path:
	read_unlock(&conn->security_use_lock);
	return rxgk_rekey(conn, specific_key_number);
bad_key:
	read_unlock(&conn->security_use_lock);
	return ERR_PTR(-ESTALE);
}

/*
 * initialise connection security
 */
static int rxgk_init_connection_security(struct rxrpc_connection *conn,
					 struct rxrpc_key_token *token)
{
	struct rxgk_context *gk;
	int ret;

	_enter("{%d,%u},{%x}",
	       conn->debug_id, conn->rxgk.key_number, key_serial(conn->key));

	conn->security_ix = token->security_index;
	conn->security_level = token->rxgk->level;

	if (rxrpc_conn_is_client(conn)) {
		conn->rxgk.start_time = ktime_get();
		do_div(conn->rxgk.start_time, 100);
	}

	gk = rxgk_generate_transport_key(conn, token->rxgk, conn->rxgk.key_number,
					 GFP_NOFS);
	if (IS_ERR(gk))
		return PTR_ERR(gk);
	conn->rxgk.enctype = gk->krb5->etype;
	conn->rxgk.keys[gk->key_number & 3] = gk;

	switch (conn->security_level) {
	case RXRPC_SECURITY_PLAIN:
	case RXRPC_SECURITY_AUTH:
	case RXRPC_SECURITY_ENCRYPT:
		break;
	default:
		ret = -EKEYREJECTED;
		goto error;
	}

	ret = 0;
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Clean up the crypto on a call.
 */
static void rxgk_free_call_crypto(struct rxrpc_call *call)
{
}

/*
 * Work out how much data we can put in a packet.
 */
static struct rxrpc_txbuf *rxgk_alloc_txbuf(struct rxrpc_call *call, size_t remain, gfp_t gfp)
{
	enum krb5_crypto_mode mode;
	struct rxgk_context *gk;
	struct rxrpc_txbuf *txb;
	size_t shdr, alloc, limit, part, offset, gap;

	switch (call->conn->security_level) {
	default:
		alloc = umin(remain, RXRPC_JUMBO_DATALEN);
		return rxrpc_alloc_data_txbuf(call, alloc, 1, gfp);
	case RXRPC_SECURITY_AUTH:
		shdr = 0;
		mode = KRB5_CHECKSUM_MODE;
		break;
	case RXRPC_SECURITY_ENCRYPT:
		shdr = sizeof(struct rxgk_header);
		mode = KRB5_ENCRYPT_MODE;
		break;
	}

	gk = rxgk_get_key(call->conn, NULL);
	if (IS_ERR(gk))
		return NULL;

	/* Work out the maximum amount of data that will fit. */
	alloc = RXRPC_JUMBO_DATALEN;
	limit = crypto_krb5_how_much_data(gk->krb5, mode, &alloc, &offset);

	if (remain < limit - shdr) {
		part = remain;
		alloc = crypto_krb5_how_much_buffer(gk->krb5, mode,
						    shdr + part, &offset);
		gap = 0;
	} else {
		part = limit - shdr;
		gap = RXRPC_JUMBO_DATALEN - alloc;
		alloc = RXRPC_JUMBO_DATALEN;
	}

	rxgk_put(gk);

	txb = rxrpc_alloc_data_txbuf(call, alloc, 16, gfp);
	if (!txb)
		return NULL;

	txb->crypto_header	= offset;
	txb->sec_header		= shdr;
	txb->offset		+= offset + shdr;
	txb->space		= part;

	/* Clear excess space in the packet */
	if (gap)
		memset(txb->data + alloc - gap, 0, gap);
	return txb;
}

/*
 * Integrity mode (sign a packet - level 1 security)
 */
static int rxgk_secure_packet_integrity(const struct rxrpc_call *call,
					struct rxgk_context *gk,
					struct rxrpc_txbuf *txb)
{
	struct rxgk_header *hdr;
	struct scatterlist sg[1];
	struct krb5_buffer metadata;
	int ret = -ENOMEM;

	_enter("");

	hdr = kzalloc(sizeof(*hdr), GFP_NOFS);
	if (!hdr)
		goto error_gk;

	hdr->epoch	= htonl(call->conn->proto.epoch);
	hdr->cid	= htonl(call->cid);
	hdr->call_number = htonl(call->call_id);
	hdr->seq	= htonl(txb->seq);
	hdr->sec_index	= htonl(call->security_ix);
	hdr->data_len	= htonl(txb->len);
	metadata.len = sizeof(*hdr);
	metadata.data = hdr;

	sg_init_table(sg, 1);
	sg_set_buf(&sg[0], txb->data, txb->alloc_size);

	ret = crypto_krb5_get_mic(gk->krb5, gk->tx_Kc, &metadata,
				  sg, 1, txb->alloc_size,
				  txb->crypto_header, txb->sec_header + txb->len);
	if (ret >= 0) {
		txb->pkt_len = ret;
		if (txb->alloc_size == RXRPC_JUMBO_DATALEN)
			txb->jumboable = true;
		gk->bytes_remaining -= ret;
	}
	kfree(hdr);
error_gk:
	rxgk_put(gk);
	_leave(" = %d", ret);
	return ret;
}

/*
 * wholly encrypt a packet (level 2 security)
 */
static int rxgk_secure_packet_encrypted(const struct rxrpc_call *call,
					struct rxgk_context *gk,
					struct rxrpc_txbuf *txb)
{
	struct rxgk_header *hdr;
	struct scatterlist sg[1];
	int ret;

	_enter("%x", txb->len);

	/* Insert the header into the buffer. */
	hdr = txb->data + txb->crypto_header;
	hdr->epoch	 = htonl(call->conn->proto.epoch);
	hdr->cid	 = htonl(call->cid);
	hdr->call_number = htonl(call->call_id);
	hdr->seq	 = htonl(txb->seq);
	hdr->sec_index	 = htonl(call->security_ix);
	hdr->data_len	 = htonl(txb->len);

	sg_init_table(sg, 1);
	sg_set_buf(&sg[0], txb->data, txb->alloc_size);

	ret = crypto_krb5_encrypt(gk->krb5, gk->tx_enc,
				  sg, 1, txb->alloc_size,
				  txb->crypto_header, txb->sec_header + txb->len,
				  false);
	if (ret >= 0) {
		txb->pkt_len = ret;
		if (txb->alloc_size == RXRPC_JUMBO_DATALEN)
			txb->jumboable = true;
		gk->bytes_remaining -= ret;
	}

	rxgk_put(gk);
	_leave(" = %d", ret);
	return ret;
}

/*
 * checksum an RxRPC packet header
 */
static int rxgk_secure_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	struct rxgk_context *gk;
	int ret;

	_enter("{%d{%x}},{#%u},%u,",
	       call->debug_id, key_serial(call->conn->key), txb->seq, txb->len);

	gk = rxgk_get_key(call->conn, NULL);
	if (IS_ERR(gk))
		return PTR_ERR(gk) == -ESTALE ? -EKEYREJECTED : PTR_ERR(gk);

	ret = key_validate(call->conn->key);
	if (ret < 0) {
		rxgk_put(gk);
		return ret;
	}

	call->security_enctype = gk->krb5->etype;
	txb->cksum = htons(gk->key_number);

	switch (call->conn->security_level) {
	case RXRPC_SECURITY_PLAIN:
		rxgk_put(gk);
		txb->pkt_len = txb->len;
		return 0;
	case RXRPC_SECURITY_AUTH:
		return rxgk_secure_packet_integrity(call, gk, txb);
	case RXRPC_SECURITY_ENCRYPT:
		return rxgk_secure_packet_encrypted(call, gk, txb);
	default:
		rxgk_put(gk);
		return -EPERM;
	}
}

/*
 * Integrity mode (check the signature on a packet - level 1 security)
 */
static int rxgk_verify_packet_integrity(struct rxrpc_call *call,
					struct rxgk_context *gk,
					struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxgk_header *hdr;
	struct krb5_buffer metadata;
	unsigned int offset = sp->offset, len = sp->len;
	size_t data_offset = 0, data_len = len;
	u32 ac = 0;
	int ret = -ENOMEM;

	_enter("");

	crypto_krb5_where_is_the_data(gk->krb5, KRB5_CHECKSUM_MODE,
				      &data_offset, &data_len);

	hdr = kzalloc(sizeof(*hdr), GFP_NOFS);
	if (!hdr)
		goto put_gk;

	hdr->epoch	= htonl(call->conn->proto.epoch);
	hdr->cid	= htonl(call->cid);
	hdr->call_number = htonl(call->call_id);
	hdr->seq	= htonl(sp->hdr.seq);
	hdr->sec_index	= htonl(call->security_ix);
	hdr->data_len	= htonl(data_len);

	metadata.len = sizeof(*hdr);
	metadata.data = hdr;
	ret = rxgk_verify_mic_skb(gk->krb5, gk->rx_Kc, &metadata,
				  skb, &offset, &len, &ac);
	kfree(hdr);
	if (ret < 0) {
		if (ret != -ENOMEM)
			rxrpc_abort_eproto(call, skb, ac,
					   rxgk_abort_1_verify_mic_eproto);
	} else {
		sp->offset = offset;
		sp->len = len;
	}

put_gk:
	rxgk_put(gk);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Decrypt an encrypted packet (level 2 security).
 */
static int rxgk_verify_packet_encrypted(struct rxrpc_call *call,
					struct rxgk_context *gk,
					struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxgk_header hdr;
	unsigned int offset = sp->offset, len = sp->len;
	int ret;
	u32 ac = 0;

	_enter("");

	ret = rxgk_decrypt_skb(gk->krb5, gk->rx_enc, skb, &offset, &len, &ac);
	if (ret < 0) {
		if (ret != -ENOMEM)
			rxrpc_abort_eproto(call, skb, ac, rxgk_abort_2_decrypt_eproto);
		goto error;
	}

	if (len < sizeof(hdr)) {
		ret = rxrpc_abort_eproto(call, skb, RXGK_PACKETSHORT,
					 rxgk_abort_2_short_header);
		goto error;
	}

	/* Extract the header from the skb */
	ret = skb_copy_bits(skb, offset, &hdr, sizeof(hdr));
	if (ret < 0) {
		ret = rxrpc_abort_eproto(call, skb, RXGK_PACKETSHORT,
					 rxgk_abort_2_short_encdata);
		goto error;
	}
	offset += sizeof(hdr);
	len -= sizeof(hdr);

	if (ntohl(hdr.epoch)		!= call->conn->proto.epoch ||
	    ntohl(hdr.cid)		!= call->cid ||
	    ntohl(hdr.call_number)	!= call->call_id ||
	    ntohl(hdr.seq)		!= sp->hdr.seq ||
	    ntohl(hdr.sec_index)	!= call->security_ix ||
	    ntohl(hdr.data_len)		> len) {
		ret = rxrpc_abort_eproto(call, skb, RXGK_SEALEDINCON,
					 rxgk_abort_2_short_data);
		goto error;
	}

	sp->offset = offset;
	sp->len = ntohl(hdr.data_len);
	ret = 0;
error:
	rxgk_put(gk);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Verify the security on a received packet or subpacket (if part of a
 * jumbo packet).
 */
static int rxgk_verify_packet(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxgk_context *gk;
	u16 key_number = sp->hdr.cksum;

	_enter("{%d{%x}},{#%u}",
	       call->debug_id, key_serial(call->conn->key), sp->hdr.seq);

	gk = rxgk_get_key(call->conn, &key_number);
	if (IS_ERR(gk)) {
		switch (PTR_ERR(gk)) {
		case -ESTALE:
			return rxrpc_abort_eproto(call, skb, RXGK_BADKEYNO,
						  rxgk_abort_bad_key_number);
		default:
			return PTR_ERR(gk);
		}
	}

	call->security_enctype = gk->krb5->etype;
	switch (call->conn->security_level) {
	case RXRPC_SECURITY_PLAIN:
		rxgk_put(gk);
		return 0;
	case RXRPC_SECURITY_AUTH:
		return rxgk_verify_packet_integrity(call, gk, skb);
	case RXRPC_SECURITY_ENCRYPT:
		return rxgk_verify_packet_encrypted(call, gk, skb);
	default:
		rxgk_put(gk);
		return -ENOANO;
	}
}

/*
 * Allocate memory to hold a challenge or a response packet.  We're not running
 * in the io_thread, so we can't use ->tx_alloc.
 */
static struct page *rxgk_alloc_packet(size_t total_len)
{
	gfp_t gfp = GFP_NOFS;
	int order;

	order = get_order(total_len);
	if (order > 0)
		gfp |= __GFP_COMP;
	return alloc_pages(gfp, order);
}

/*
 * Issue a challenge.
 */
static int rxgk_issue_challenge(struct rxrpc_connection *conn)
{
	struct rxrpc_wire_header *whdr;
	struct bio_vec bvec[1];
	struct msghdr msg;
	struct page *page;
	size_t len = sizeof(*whdr) + sizeof(conn->rxgk.nonce);
	u32 serial;
	int ret;

	_enter("{%d}", conn->debug_id);

	get_random_bytes(&conn->rxgk.nonce, sizeof(conn->rxgk.nonce));

	/* We can't use conn->tx_alloc without a lock */
	page = rxgk_alloc_packet(sizeof(*whdr) + sizeof(conn->rxgk.nonce));
	if (!page)
		return -ENOMEM;

	bvec_set_page(&bvec[0], page, len, 0);
	iov_iter_bvec(&msg.msg_iter, WRITE, bvec, 1, len);

	msg.msg_name	= &conn->peer->srx.transport;
	msg.msg_namelen	= conn->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= MSG_SPLICE_PAGES;

	whdr = page_address(page);
	whdr->epoch	= htonl(conn->proto.epoch);
	whdr->cid	= htonl(conn->proto.cid);
	whdr->callNumber = 0;
	whdr->seq	= 0;
	whdr->type	= RXRPC_PACKET_TYPE_CHALLENGE;
	whdr->flags	= conn->out_clientflag;
	whdr->userStatus = 0;
	whdr->securityIndex = conn->security_ix;
	whdr->_rsvd	= 0;
	whdr->serviceId	= htons(conn->service_id);

	memcpy(whdr + 1, conn->rxgk.nonce, sizeof(conn->rxgk.nonce));

	serial = rxrpc_get_next_serials(conn, 1);
	whdr->serial = htonl(serial);

	trace_rxrpc_tx_challenge(conn, serial, 0, *(u32 *)&conn->rxgk.nonce);

	ret = do_udp_sendmsg(conn->local->socket, &msg, len);
	if (ret > 0)
		conn->peer->last_tx_at = ktime_get_seconds();
	__free_page(page);

	if (ret < 0) {
		trace_rxrpc_tx_fail(conn->debug_id, serial, ret,
				    rxrpc_tx_point_rxgk_challenge);
		return -EAGAIN;
	}

	trace_rxrpc_tx_packet(conn->debug_id, whdr,
			      rxrpc_tx_point_rxgk_challenge);
	_leave(" = 0");
	return 0;
}

/*
 * Validate a challenge packet.
 */
static bool rxgk_validate_challenge(struct rxrpc_connection *conn,
				    struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	u8 nonce[20];

	if (!conn->key) {
		rxrpc_abort_conn(conn, skb, RX_PROTOCOL_ERROR, -EPROTO,
				 rxgk_abort_chall_no_key);
		return false;
	}

	if (key_validate(conn->key) < 0) {
		rxrpc_abort_conn(conn, skb, RXGK_EXPIRED, -EPROTO,
				 rxgk_abort_chall_key_expired);
		return false;
	}

	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
			  nonce, sizeof(nonce)) < 0) {
		rxrpc_abort_conn(conn, skb, RXGK_PACKETSHORT, -EPROTO,
				 rxgk_abort_chall_short);
		return false;
	}

	trace_rxrpc_rx_challenge(conn, sp->hdr.serial, 0, *(u32 *)nonce, 0);
	return true;
}

/**
 * rxgk_kernel_query_challenge - Query RxGK-specific challenge parameters
 * @challenge: The challenge packet to query
 *
 * Return: The Kerberos 5 encoding type for the challenged connection.
 */
u32 rxgk_kernel_query_challenge(struct sk_buff *challenge)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(challenge);

	return sp->chall.conn->rxgk.enctype;
}
EXPORT_SYMBOL(rxgk_kernel_query_challenge);

/*
 * Fill out the control message to pass to userspace to inform about the
 * challenge.
 */
static int rxgk_challenge_to_recvmsg(struct rxrpc_connection *conn,
				     struct sk_buff *challenge,
				     struct msghdr *msg)
{
	struct rxgk_challenge chall;

	chall.base.service_id		= conn->service_id;
	chall.base.security_index	= conn->security_ix;
	chall.enctype			= conn->rxgk.enctype;

	return put_cmsg(msg, SOL_RXRPC, RXRPC_CHALLENGED, sizeof(chall), &chall);
}

/*
 * Insert the requisite amount of XDR padding for the length given.
 */
static int rxgk_pad_out(struct sk_buff *response, size_t len, size_t offset)
{
	__be32 zero = 0;
	size_t pad = xdr_round_up(len) - len;
	int ret;

	if (!pad)
		return 0;

	ret = skb_store_bits(response, offset, &zero, pad);
	if (ret < 0)
		return ret;
	return pad;
}

/*
 * Insert the header into the response.
 */
static noinline ssize_t rxgk_insert_response_header(struct rxrpc_connection *conn,
						    struct rxgk_context *gk,
						    struct sk_buff *response,
						    size_t offset)
{
	struct rxrpc_skb_priv *rsp = rxrpc_skb(response);

	struct {
		struct rxrpc_wire_header whdr;
		__be32 start_time_msw;
		__be32 start_time_lsw;
		__be32 ticket_len;
	} h;
	int ret;

	rsp->resp.kvno		= gk->key_number;
	rsp->resp.version	= gk->krb5->etype;

	h.whdr.epoch		= htonl(conn->proto.epoch);
	h.whdr.cid		= htonl(conn->proto.cid);
	h.whdr.callNumber	= 0;
	h.whdr.serial		= 0;
	h.whdr.seq		= 0;
	h.whdr.type		= RXRPC_PACKET_TYPE_RESPONSE;
	h.whdr.flags		= conn->out_clientflag;
	h.whdr.userStatus	= 0;
	h.whdr.securityIndex	= conn->security_ix;
	h.whdr.cksum		= htons(gk->key_number);
	h.whdr.serviceId	= htons(conn->service_id);
	h.start_time_msw	= htonl(upper_32_bits(conn->rxgk.start_time));
	h.start_time_lsw	= htonl(lower_32_bits(conn->rxgk.start_time));
	h.ticket_len		= htonl(gk->key->ticket.len);

	ret = skb_store_bits(response, offset, &h, sizeof(h));
	return ret < 0 ? ret : sizeof(h);
}

/*
 * Construct the authenticator to go in the response packet
 *
 * struct RXGK_Authenticator {
 *	opaque nonce[20];
 *	opaque appdata<>;
 *	RXGK_Level level;
 *	unsigned int epoch;
 *	unsigned int cid;
 *	unsigned int call_numbers<>;
 * };
 */
static ssize_t rxgk_construct_authenticator(struct rxrpc_connection *conn,
					    struct sk_buff *challenge,
					    const struct krb5_buffer *appdata,
					    struct sk_buff *response,
					    size_t offset)
{
	struct {
		u8	nonce[20];
		__be32	appdata_len;
	} a;
	struct {
		__be32	level;
		__be32	epoch;
		__be32	cid;
		__be32	call_numbers_count;
		__be32	call_numbers[4];
	} b;
	int ret;

	ret = skb_copy_bits(challenge, sizeof(struct rxrpc_wire_header),
			    a.nonce, sizeof(a.nonce));
	if (ret < 0)
		return -EPROTO;

	a.appdata_len = htonl(appdata->len);

	ret = skb_store_bits(response, offset, &a, sizeof(a));
	if (ret < 0)
		return ret;
	offset += sizeof(a);

	if (appdata->len) {
		ret = skb_store_bits(response, offset, appdata->data, appdata->len);
		if (ret < 0)
			return ret;
		offset += appdata->len;

		ret = rxgk_pad_out(response, appdata->len, offset);
		if (ret < 0)
			return ret;
		offset += ret;
	}

	b.level			= htonl(conn->security_level);
	b.epoch			= htonl(conn->proto.epoch);
	b.cid			= htonl(conn->proto.cid);
	b.call_numbers_count	= htonl(4);
	b.call_numbers[0]	= htonl(conn->channels[0].call_counter);
	b.call_numbers[1]	= htonl(conn->channels[1].call_counter);
	b.call_numbers[2]	= htonl(conn->channels[2].call_counter);
	b.call_numbers[3]	= htonl(conn->channels[3].call_counter);

	ret = skb_store_bits(response, offset, &b, sizeof(b));
	if (ret < 0)
		return ret;
	return sizeof(a) + xdr_round_up(appdata->len) + sizeof(b);
}

static ssize_t rxgk_encrypt_authenticator(struct rxrpc_connection *conn,
					  struct rxgk_context *gk,
					  struct sk_buff *response,
					  size_t offset,
					  size_t alloc_len,
					  size_t auth_offset,
					  size_t auth_len)
{
	struct scatterlist sg[16];
	int nr_sg;

	sg_init_table(sg, ARRAY_SIZE(sg));
	nr_sg = skb_to_sgvec(response, sg, offset, alloc_len);
	if (unlikely(nr_sg < 0))
		return nr_sg;
	return crypto_krb5_encrypt(gk->krb5, gk->resp_enc, sg, nr_sg, alloc_len,
				   auth_offset, auth_len, false);
}

/*
 * Construct the response.
 *
 * struct RXGK_Response {
 *	rxgkTime start_time;
 *	RXGK_Data token;
 *	opaque authenticator<RXGK_MAXAUTHENTICATOR>
 * };
 */
static int rxgk_construct_response(struct rxrpc_connection *conn,
				   struct sk_buff *challenge,
				   struct krb5_buffer *appdata)
{
	struct rxrpc_skb_priv *csp, *rsp;
	struct rxgk_context *gk;
	struct sk_buff *response;
	size_t len, auth_len, authx_len, offset, auth_offset, authx_offset;
	__be32 tmp;
	int ret;

	gk = rxgk_get_key(conn, NULL);
	if (IS_ERR(gk))
		return PTR_ERR(gk);

	auth_len = 20 + (4 + appdata->len) + 12 + (1 + 4) * 4;
	authx_len = crypto_krb5_how_much_buffer(gk->krb5, KRB5_ENCRYPT_MODE,
						auth_len, &auth_offset);
	len = sizeof(struct rxrpc_wire_header) +
		8 + (4 + xdr_round_up(gk->key->ticket.len)) + (4 + authx_len);

	response = alloc_skb_with_frags(0, len, 0, &ret, GFP_NOFS);
	if (!response)
		goto error;
	rxrpc_new_skb(response, rxrpc_skb_new_response_rxgk);
	response->len = len;
	response->data_len = len;

	ret = rxgk_insert_response_header(conn, gk, response, 0);
	if (ret < 0)
		goto error;
	offset = ret;

	ret = skb_store_bits(response, offset, gk->key->ticket.data, gk->key->ticket.len);
	if (ret < 0)
		goto error;
	offset += gk->key->ticket.len;
	ret = rxgk_pad_out(response, gk->key->ticket.len, offset);
	if (ret < 0)
		goto error;

	authx_offset = offset + ret + 4; /* Leave a gap for the length. */

	ret = rxgk_construct_authenticator(conn, challenge, appdata, response,
					   authx_offset + auth_offset);
	if (ret < 0)
		goto error;
	auth_len = ret;

	ret = rxgk_encrypt_authenticator(conn, gk, response,
					 authx_offset, authx_len,
					 auth_offset, auth_len);
	if (ret < 0)
		goto error;
	authx_len = ret;

	tmp = htonl(authx_len);
	ret = skb_store_bits(response, authx_offset - 4, &tmp, 4);
	if (ret < 0)
		goto error;

	ret = rxgk_pad_out(response, authx_len, authx_offset + authx_len);
	if (ret < 0)
		goto error;
	len = authx_offset + authx_len + ret;

	if (len != response->len) {
		response->len = len;
		response->data_len = len;
	}

	csp = rxrpc_skb(challenge);
	rsp = rxrpc_skb(response);
	rsp->resp.len = len;
	rsp->resp.challenge_serial = csp->hdr.serial;
	rxrpc_post_response(conn, response);
	response = NULL;
	ret = 0;

error:
	rxrpc_free_skb(response, rxrpc_skb_put_response);
	rxgk_put(gk);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Respond to a challenge packet.
 */
static int rxgk_respond_to_challenge(struct rxrpc_connection *conn,
				     struct sk_buff *challenge,
				     struct krb5_buffer *appdata)
{
	_enter("{%d,%x}", conn->debug_id, key_serial(conn->key));

	if (key_validate(conn->key) < 0)
		return rxrpc_abort_conn(conn, NULL, RXGK_EXPIRED, -EPROTO,
					rxgk_abort_chall_key_expired);

	return rxgk_construct_response(conn, challenge, appdata);
}

static int rxgk_respond_to_challenge_no_appdata(struct rxrpc_connection *conn,
						struct sk_buff *challenge)
{
	struct krb5_buffer appdata = {};

	return rxgk_respond_to_challenge(conn, challenge, &appdata);
}

/**
 * rxgk_kernel_respond_to_challenge - Respond to a challenge with appdata
 * @challenge: The challenge to respond to
 * @appdata: The application data to include in the RESPONSE authenticator
 *
 * Allow a kernel application to respond to a CHALLENGE with application data
 * to be included in the RxGK RESPONSE Authenticator.
 *
 * Return: %0 if successful and a negative error code otherwise.
 */
int rxgk_kernel_respond_to_challenge(struct sk_buff *challenge,
				     struct krb5_buffer *appdata)
{
	struct rxrpc_skb_priv *csp = rxrpc_skb(challenge);

	return rxgk_respond_to_challenge(csp->chall.conn, challenge, appdata);
}
EXPORT_SYMBOL(rxgk_kernel_respond_to_challenge);

/*
 * Parse sendmsg() control message and respond to challenge.  We need to see if
 * there's an appdata to fish out.
 */
static int rxgk_sendmsg_respond_to_challenge(struct sk_buff *challenge,
					     struct msghdr *msg)
{
	struct krb5_buffer appdata = {};
	struct cmsghdr *cmsg;

	for_each_cmsghdr(cmsg, msg) {
		if (cmsg->cmsg_level != SOL_RXRPC ||
		    cmsg->cmsg_type != RXRPC_RESP_RXGK_APPDATA)
			continue;
		if (appdata.data)
			return -EINVAL;
		appdata.data = CMSG_DATA(cmsg);
		appdata.len = cmsg->cmsg_len - sizeof(struct cmsghdr);
	}

	return rxgk_kernel_respond_to_challenge(challenge, &appdata);
}

/*
 * Verify the authenticator.
 *
 * struct RXGK_Authenticator {
 *	opaque nonce[20];
 *	opaque appdata<>;
 *	RXGK_Level level;
 *	unsigned int epoch;
 *	unsigned int cid;
 *	unsigned int call_numbers<>;
 * };
 */
static int rxgk_do_verify_authenticator(struct rxrpc_connection *conn,
					const struct krb5_enctype *krb5,
					struct sk_buff *skb,
					__be32 *p, __be32 *end)
{
	u32 app_len, call_count, level, epoch, cid, i;

	_enter("");

	if (memcmp(p, conn->rxgk.nonce, 20) != 0)
		return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
					rxgk_abort_resp_bad_nonce);
	p += 20 / sizeof(__be32);

	app_len	= ntohl(*p++);
	if (app_len > (end - p) * sizeof(__be32))
		return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
					rxgk_abort_resp_short_applen);

	p += xdr_round_up(app_len) / sizeof(__be32);
	if (end - p < 4)
		return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
					rxgk_abort_resp_short_applen);

	level	= ntohl(*p++);
	epoch	= ntohl(*p++);
	cid	= ntohl(*p++);
	call_count = ntohl(*p++);

	if (level	!= conn->security_level ||
	    epoch	!= conn->proto.epoch ||
	    cid		!= conn->proto.cid ||
	    call_count	> 4)
		return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
					rxgk_abort_resp_bad_param);

	if (end - p < call_count)
		return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
					rxgk_abort_resp_short_call_list);

	for (i = 0; i < call_count; i++) {
		u32 call_id = ntohl(*p++);

		if (call_id > INT_MAX)
			return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
						rxgk_abort_resp_bad_callid);

		if (call_id < conn->channels[i].call_counter)
			return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
						rxgk_abort_resp_call_ctr);

		if (call_id > conn->channels[i].call_counter) {
			if (conn->channels[i].call)
				return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
							rxgk_abort_resp_call_state);

			conn->channels[i].call_counter = call_id;
		}
	}

	_leave(" = 0");
	return 0;
}

/*
 * Extract the authenticator and verify it.
 */
static int rxgk_verify_authenticator(struct rxrpc_connection *conn,
				     const struct krb5_enctype *krb5,
				     struct sk_buff *skb,
				     unsigned int auth_offset, unsigned int auth_len)
{
	void *auth;
	__be32 *p;
	int ret;

	auth = kmalloc(auth_len, GFP_NOFS);
	if (!auth)
		return -ENOMEM;

	ret = skb_copy_bits(skb, auth_offset, auth, auth_len);
	if (ret < 0) {
		ret = rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EPROTO,
				       rxgk_abort_resp_short_auth);
		goto error;
	}

	p = auth;
	ret = rxgk_do_verify_authenticator(conn, krb5, skb, p, p + auth_len);
error:
	kfree(auth);
	return ret;
}

/*
 * Verify a response.
 *
 * struct RXGK_Response {
 *	rxgkTime	start_time;
 *	RXGK_Data	token;
 *	opaque		authenticator<RXGK_MAXAUTHENTICATOR>
 * };
 */
static int rxgk_verify_response(struct rxrpc_connection *conn,
				struct sk_buff *skb)
{
	const struct krb5_enctype *krb5;
	struct rxrpc_key_token *token;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxgk_response rhdr;
	struct rxgk_context *gk;
	struct key *key = NULL;
	unsigned int offset = sizeof(struct rxrpc_wire_header);
	unsigned int len = skb->len - sizeof(struct rxrpc_wire_header);
	unsigned int token_offset, token_len;
	unsigned int auth_offset, auth_len;
	__be32 xauth_len;
	int ret, ec;

	_enter("{%d}", conn->debug_id);

	/* Parse the RXGK_Response object */
	if (sizeof(rhdr) + sizeof(__be32) > len)
		goto short_packet;

	if (skb_copy_bits(skb, offset, &rhdr, sizeof(rhdr)) < 0)
		goto short_packet;
	offset	+= sizeof(rhdr);
	len	-= sizeof(rhdr);

	token_offset	= offset;
	token_len	= ntohl(rhdr.token_len);
	if (xdr_round_up(token_len) + sizeof(__be32) > len)
		goto short_packet;

	trace_rxrpc_rx_response(conn, sp->hdr.serial, 0, sp->hdr.cksum, token_len);

	offset	+= xdr_round_up(token_len);
	len	-= xdr_round_up(token_len);

	if (skb_copy_bits(skb, offset, &xauth_len, sizeof(xauth_len)) < 0)
		goto short_packet;
	offset	+= sizeof(xauth_len);
	len	-= sizeof(xauth_len);

	auth_offset	= offset;
	auth_len	= ntohl(xauth_len);
	if (auth_len < len)
		goto short_packet;
	if (auth_len & 3)
		goto inconsistent;
	if (auth_len < 20 + 9 * 4)
		goto auth_too_short;

	/* We need to extract and decrypt the token and instantiate a session
	 * key for it.  This bit, however, is application-specific.  If
	 * possible, we use a default parser, but we might end up bumping this
	 * to the app to deal with - which might mean a round trip to
	 * userspace.
	 */
	ret = rxgk_extract_token(conn, skb, token_offset, token_len, &key);
	if (ret < 0)
		goto out;

	/* We now have a key instantiated from the decrypted ticket.  We can
	 * pass this to the application so that they can parse the ticket
	 * content and we can use the session key it contains to derive the
	 * keys we need.
	 *
	 * Note that we have to switch enctype at this point as the enctype of
	 * the ticket doesn't necessarily match that of the transport.
	 */
	token = key->payload.data[0];
	conn->security_level = token->rxgk->level;
	conn->rxgk.start_time = __be64_to_cpu(rhdr.start_time);

	gk = rxgk_generate_transport_key(conn, token->rxgk, sp->hdr.cksum, GFP_NOFS);
	if (IS_ERR(gk)) {
		ret = PTR_ERR(gk);
		goto cant_get_token;
	}

	krb5 = gk->krb5;

	trace_rxrpc_rx_response(conn, sp->hdr.serial, krb5->etype, sp->hdr.cksum, token_len);

	/* Decrypt, parse and verify the authenticator. */
	ret = rxgk_decrypt_skb(krb5, gk->resp_enc, skb,
			       &auth_offset, &auth_len, &ec);
	if (ret < 0) {
		rxrpc_abort_conn(conn, skb, RXGK_SEALEDINCON, ret,
				 rxgk_abort_resp_auth_dec);
		goto out;
	}

	ret = rxgk_verify_authenticator(conn, krb5, skb, auth_offset, auth_len);
	if (ret < 0)
		goto out;

	conn->key = key;
	key = NULL;
	ret = 0;
out:
	key_put(key);
	_leave(" = %d", ret);
	return ret;

inconsistent:
	ret = rxrpc_abort_conn(conn, skb, RXGK_INCONSISTENCY, -EPROTO,
			       rxgk_abort_resp_xdr_align);
	goto out;
auth_too_short:
	ret = rxrpc_abort_conn(conn, skb, RXGK_PACKETSHORT, -EPROTO,
			       rxgk_abort_resp_short_auth);
	goto out;
short_packet:
	ret = rxrpc_abort_conn(conn, skb, RXGK_PACKETSHORT, -EPROTO,
			       rxgk_abort_resp_short_packet);
	goto out;

cant_get_token:
	switch (ret) {
	case -ENOMEM:
		goto temporary_error;
	case -EINVAL:
		ret = rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EKEYREJECTED,
				       rxgk_abort_resp_internal_error);
		goto out;
	case -ENOPKG:
		ret = rxrpc_abort_conn(conn, skb, KRB5_PROG_KEYTYPE_NOSUPP,
				       -EKEYREJECTED, rxgk_abort_resp_nopkg);
		goto out;
	}

temporary_error:
	/* Ignore the response packet if we got a temporary error such as
	 * ENOMEM.  We just want to send the challenge again.  Note that we
	 * also come out this way if the ticket decryption fails.
	 */
	goto out;
}

/*
 * clear the connection security
 */
static void rxgk_clear(struct rxrpc_connection *conn)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(conn->rxgk.keys); i++)
		rxgk_put(conn->rxgk.keys[i]);
}

/*
 * Initialise the RxGK security service.
 */
static int rxgk_init(void)
{
	return 0;
}

/*
 * Clean up the RxGK security service.
 */
static void rxgk_exit(void)
{
}

/*
 * RxRPC YFS GSSAPI-based security
 */
const struct rxrpc_security rxgk_yfs = {
	.name				= "yfs-rxgk",
	.security_index			= RXRPC_SECURITY_YFS_RXGK,
	.no_key_abort			= RXGK_NOTAUTH,
	.init				= rxgk_init,
	.exit				= rxgk_exit,
	.preparse_server_key		= rxgk_preparse_server_key,
	.free_preparse_server_key	= rxgk_free_preparse_server_key,
	.destroy_server_key		= rxgk_destroy_server_key,
	.describe_server_key		= rxgk_describe_server_key,
	.init_connection_security	= rxgk_init_connection_security,
	.alloc_txbuf			= rxgk_alloc_txbuf,
	.secure_packet			= rxgk_secure_packet,
	.verify_packet			= rxgk_verify_packet,
	.free_call_crypto		= rxgk_free_call_crypto,
	.issue_challenge		= rxgk_issue_challenge,
	.validate_challenge		= rxgk_validate_challenge,
	.challenge_to_recvmsg		= rxgk_challenge_to_recvmsg,
	.sendmsg_respond_to_challenge	= rxgk_sendmsg_respond_to_challenge,
	.respond_to_challenge		= rxgk_respond_to_challenge_no_appdata,
	.verify_response		= rxgk_verify_response,
	.clear				= rxgk_clear,
	.default_decode_ticket		= rxgk_yfs_decode_ticket,
};
