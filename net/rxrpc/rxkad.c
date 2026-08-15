// SPDX-License-Identifier: GPL-2.0-or-later
/* Kerberos-based RxRPC security
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/des.h>
#include <kunit/visibility.h>
#include <linux/export.h>
#include <linux/fips.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/udp.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/key-type.h>
#include <linux/unaligned.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <keys/rxrpc-type.h>
#include "ar-internal.h"

#define RXKAD_VERSION			2
#define MAXKRB5TICKETLEN		1024
#define RXKAD_TKT_TYPE_KERBEROS_V5	256
#define ANAME_SZ			40	/* size of authentication name */
#define INST_SZ				40	/* size of principal's instance */
#define REALM_SZ			40	/* size of principal's auth domain */
#define SNAME_SZ			40	/* size of service name */
#define RXKAD_ALIGN			8

static const u8 zero_iv[FCRYPT_BSIZE];

struct rxkad_level1_hdr {
	__be32	data_size;	/* true data size (excluding padding) */
};

struct rxkad_level2_hdr {
	__be32	data_size;	/* true data size (excluding padding) */
	__be32	checksum;	/* decrypted data checksum */
};

static void rxkad_prime_packet_security(struct rxrpc_connection *conn,
					const struct fcrypt_key *cipher);

/*
 * Parse the information from a server key
 *
 * The data should be the 8-byte secret key.
 */
static int rxkad_preparse_server_key(struct key_preparsed_payload *prep)
{
	struct des_ctx *des_key;
	int err;

	if (prep->datalen != 8)
		return -EINVAL;

	memcpy(&prep->payload.data[2], prep->data, 8);

	des_key = kmalloc_obj(*des_key);
	if (!des_key) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	err = des_expand_key(des_key, prep->data, 8);
	if (err) {
		kfree_sensitive(des_key);
		_leave(" = %d", err);
		return err;
	}

	prep->payload.data[0] = des_key;
	_leave(" = 0");
	return 0;
}

static void rxkad_free_preparse_server_key(struct key_preparsed_payload *prep)
{
	kfree_sensitive(prep->payload.data[0]);
}

static void rxkad_destroy_server_key(struct key *key)
{
	kfree_sensitive(key->payload.data[0]);
	key->payload.data[0] = NULL;
}

/*
 * initialise connection security
 */
static int rxkad_init_connection_security(struct rxrpc_connection *conn,
					  struct rxrpc_key_token *token)
{
	struct fcrypt_key *ci;
	int ret;

	_enter("{%d},{%x}", conn->debug_id, key_serial(conn->key));

	conn->security_ix = token->security_index;

	ci = kmalloc_obj(*ci);
	if (!ci) {
		ret = -ENOMEM;
		goto error;
	}
	fcrypt_preparekey(ci, token->kad->session_key);

	switch (conn->security_level) {
	case RXRPC_SECURITY_PLAIN:
	case RXRPC_SECURITY_AUTH:
	case RXRPC_SECURITY_ENCRYPT:
		break;
	default:
		ret = -EKEYREJECTED;
		goto error_ci;
	}

	rxkad_prime_packet_security(conn, ci);

	conn->rxkad.cipher = ci;
	return 0;

error_ci:
	kfree_sensitive(ci);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Work out how much data we can put in a packet.
 */
static struct rxrpc_txbuf *rxkad_alloc_txbuf(struct rxrpc_call *call, size_t remain, gfp_t gfp)
{
	struct rxrpc_txbuf *txb;
	size_t shdr, alloc, limit, part;

	remain = umin(remain, 65535 - sizeof(struct rxrpc_wire_header));

	switch (call->conn->security_level) {
	default:
		alloc = umin(remain, RXRPC_JUMBO_DATALEN);
		return rxrpc_alloc_data_txbuf(call, alloc, 1, gfp);
	case RXRPC_SECURITY_AUTH:
		shdr = sizeof(struct rxkad_level1_hdr);
		break;
	case RXRPC_SECURITY_ENCRYPT:
		shdr = sizeof(struct rxkad_level2_hdr);
		break;
	}

	limit = round_down(RXRPC_JUMBO_DATALEN, RXKAD_ALIGN) - shdr;
	if (remain < limit) {
		part = remain;
		alloc = round_up(shdr + part, RXKAD_ALIGN);
	} else {
		part = limit;
		alloc = RXRPC_JUMBO_DATALEN;
	}

	txb = rxrpc_alloc_data_txbuf(call, alloc, RXKAD_ALIGN, gfp);
	if (!txb)
		return NULL;

	txb->crypto_header	= 0;
	txb->sec_header		= shdr;
	txb->offset		+= shdr;
	txb->space		= part;
	return txb;
}

/*
 * prime the encryption state with the invariant parts of a connection's
 * description
 */
static void rxkad_prime_packet_security(struct rxrpc_connection *conn,
					const struct fcrypt_key *cipher)
{
	struct rxrpc_key_token *token;
	__be32 tmpbuf[4];

	_enter("");

	if (!conn->key)
		return;
	token = conn->key->payload.data[0];

	tmpbuf[0] = htonl(conn->proto.epoch);
	tmpbuf[1] = htonl(conn->proto.cid);
	tmpbuf[2] = 0;
	tmpbuf[3] = htonl(conn->security_ix);

	static_assert(sizeof(tmpbuf) % FCRYPT_BSIZE == 0);
	fcrypt_pcbc_encrypt(cipher, /* iv= */ token->kad->session_key, tmpbuf,
			    tmpbuf, sizeof(tmpbuf) / FCRYPT_BSIZE);
	memcpy(&conn->rxkad.csum_iv, &tmpbuf[2], sizeof(conn->rxkad.csum_iv));
	_leave("");
}

/*
 * Clean up the crypto on a call.
 */
static void rxkad_free_call_crypto(struct rxrpc_call *call)
{
}

/*
 * partially encrypt a packet (level 1 security)
 */
static void rxkad_secure_packet_auth(const struct rxrpc_call *call,
				     struct rxrpc_txbuf *txb)
{
	struct rxkad_level1_hdr *hdr = txb->data;
	size_t pad;
	u16 check;

	_enter("");

	check = txb->seq ^ call->call_id;
	hdr->data_size = htonl((u32)check << 16 | txb->len);

	txb->pkt_len = sizeof(struct rxkad_level1_hdr) + txb->len;
	pad = txb->pkt_len;
	pad = RXKAD_ALIGN - pad;
	pad &= RXKAD_ALIGN - 1;
	if (pad) {
		memset(txb->data + txb->offset, 0, pad);
		txb->pkt_len += pad;
	}

	/* start the encryption afresh */
	fcrypt_pcbc_encrypt(call->conn->rxkad.cipher, zero_iv, hdr, hdr, 1);
	_leave("");
}

/*
 * wholly encrypt a packet (level 2 security)
 */
static void rxkad_secure_packet_encrypt(const struct rxrpc_call *call,
					struct rxrpc_txbuf *txb)
{
	const struct rxrpc_key_token *token;
	struct rxkad_level2_hdr *rxkhdr = txb->data;
	size_t content, pad;
	u16 check;

	_enter("");

	check = txb->seq ^ call->call_id;

	rxkhdr->data_size = htonl(txb->len | (u32)check << 16);
	rxkhdr->checksum = 0;

	content = sizeof(struct rxkad_level2_hdr) + txb->len;
	static_assert(RXKAD_ALIGN == FCRYPT_BSIZE);
	txb->pkt_len = round_up(content, RXKAD_ALIGN);
	pad = txb->pkt_len - content;
	if (pad)
		memset(txb->data + txb->offset, 0, pad);
	/* Now txb->pkt_len % FCRYPT_BSIZE == 0. */

	/* encrypt from the session key */
	token = call->conn->key->payload.data[0];
	fcrypt_pcbc_encrypt(call->conn->rxkad.cipher, token->kad->session_key,
			    rxkhdr, rxkhdr, txb->pkt_len / FCRYPT_BSIZE);
	_leave("");
}

/*
 * checksum an RxRPC packet header
 */
static int rxkad_secure_packet(struct rxrpc_call *call, struct rxrpc_txbuf *txb)
{
	union {
		__be32 buf[2];
	} crypto __aligned(8);
	u32 x, y = 0;
	int ret;

	_enter("{%d{%x}},{#%u},%u,",
	       call->debug_id, key_serial(call->conn->key),
	       txb->seq, txb->len);

	if (!call->conn->rxkad.cipher)
		return 0;

	ret = key_validate(call->conn->key);
	if (ret < 0)
		return ret;

	/* calculate the security checksum */
	x = (call->cid & RXRPC_CHANNELMASK) << (32 - RXRPC_CIDSHIFT);
	x |= txb->seq & 0x3fffffff;
	crypto.buf[0] = htonl(call->call_id);
	crypto.buf[1] = htonl(x);

	/* continue encrypting from where we left off */
	fcrypt_pcbc_encrypt(call->conn->rxkad.cipher,
			    call->conn->rxkad.csum_iv.x, crypto.buf, crypto.buf,
			    1);

	y = ntohl(crypto.buf[1]);
	y = (y >> 16) & 0xffff;
	if (y == 0)
		y = 1; /* zero checksums are not permitted */
	txb->cksum = htons(y);

	switch (call->conn->security_level) {
	case RXRPC_SECURITY_PLAIN:
		txb->pkt_len = txb->len;
		ret = 0;
		break;
	case RXRPC_SECURITY_AUTH:
		rxkad_secure_packet_auth(call, txb);
		if (txb->alloc_size == RXRPC_JUMBO_DATALEN)
			txb->jumboable = true;
		ret = 0;
		break;
	case RXRPC_SECURITY_ENCRYPT:
		rxkad_secure_packet_encrypt(call, txb);
		if (txb->alloc_size == RXRPC_JUMBO_DATALEN)
			txb->jumboable = true;
		ret = 0;
		break;
	default:
		ret = -EPERM;
		break;
	}

	/* Clear excess space in the packet */
	if (txb->pkt_len < txb->alloc_size) {
		size_t gap = txb->alloc_size - txb->pkt_len;
		void *p = txb->data;

		memset(p + txb->pkt_len, 0, gap);
	}

	_leave(" = %d [set %x]", ret, y);
	return ret;
}

/*
 * decrypt partial encryption on a packet (level 1 security)
 */
static int rxkad_verify_packet_1(struct rxrpc_call *call, struct sk_buff *skb,
				 rxrpc_seq_t seq)
{
	struct rxkad_level1_hdr *sechdr;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	void *data = call->rx_dec_buffer;
	u32 len = sp->len, data_size, buf;
	u16 check;

	_enter("");

	if (len < 8)
		return rxrpc_abort_eproto(call, skb, RXKADSEALEDINCON,
					  rxkad_abort_1_short_header);

	/* Decrypt the first 8-byte block of the packet, using the zero IV. */
	fcrypt_pcbc_decrypt(call->conn->rxkad.cipher, zero_iv, data, data, 1);

	/* Extract the decrypted packet length */
	sechdr = data;
	call->rx_dec_offset = sizeof(*sechdr);
	len -= sizeof(*sechdr);

	buf = ntohl(sechdr->data_size);
	data_size = buf & 0xffff;

	check = buf >> 16;
	check ^= seq ^ call->call_id;
	check &= 0xffff;
	if (check != 0)
		return rxrpc_abort_eproto(call, skb, RXKADSEALEDINCON,
					  rxkad_abort_1_short_check);
	if (data_size > len)
		return rxrpc_abort_eproto(call, skb, RXKADDATALEN,
					  rxkad_abort_1_short_data);
	call->rx_dec_len = data_size;

	_leave(" = 0 [dlen=%x]", data_size);
	return 0;
}

/*
 * wholly decrypt a packet (level 2 security)
 */
static int rxkad_verify_packet_2(struct rxrpc_call *call, struct sk_buff *skb,
				 rxrpc_seq_t seq)
{
	const struct rxrpc_key_token *token;
	struct rxkad_level2_hdr *sechdr;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	void *data = call->rx_dec_buffer;
	u32 len = sp->len, data_size, buf;
	u16 check;

	_enter(",{%d}", len);

	if (len < 8)
		return rxrpc_abort_eproto(call, skb, RXKADSEALEDINCON,
					  rxkad_abort_2_short_header);

	/* Don't let the crypto algo see a misaligned length. */
	len = round_down(len, 8);

	/* decrypt from the session key */
	token = call->conn->key->payload.data[0];
	fcrypt_pcbc_decrypt(call->conn->rxkad.cipher, token->kad->session_key,
			    data, data, len / FCRYPT_BSIZE);

	/* Extract the decrypted packet length */
	sechdr = data;
	call->rx_dec_offset = sizeof(*sechdr);
	len -= sizeof(*sechdr);

	buf = ntohl(sechdr->data_size);
	data_size = buf & 0xffff;

	check = buf >> 16;
	check ^= seq ^ call->call_id;
	check &= 0xffff;
	if (check != 0)
		return rxrpc_abort_eproto(call, skb, RXKADSEALEDINCON,
					  rxkad_abort_2_short_check);

	if (data_size > len)
		return rxrpc_abort_eproto(call, skb, RXKADDATALEN,
					  rxkad_abort_2_short_data);

	call->rx_dec_len = data_size;
	_leave(" = 0 [dlen=%x]", data_size);
	return 0;
}

/*
 * Verify the security on a received (sub)packet.  If the packet needs
 * modifying (e.g. decrypting), it must be copied.
 */
static int rxkad_verify_packet(struct rxrpc_call *call, struct sk_buff *skb)
{
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	union {
		__be32 buf[2];
	} crypto __aligned(8);
	rxrpc_seq_t seq = sp->hdr.seq;
	int ret;
	u16 cksum;
	u32 x, y;

	_enter("{%d{%x}},{#%u}",
	       call->debug_id, key_serial(call->conn->key), seq);

	if (!call->conn->rxkad.cipher)
		return 0;

	/* validate the security checksum */
	x = (call->cid & RXRPC_CHANNELMASK) << (32 - RXRPC_CIDSHIFT);
	x |= seq & 0x3fffffff;
	crypto.buf[0] = htonl(call->call_id);
	crypto.buf[1] = htonl(x);

	/* continue encrypting from where we left off */
	fcrypt_pcbc_encrypt(call->conn->rxkad.cipher,
			    call->conn->rxkad.csum_iv.x, crypto.buf, crypto.buf,
			    1);

	y = ntohl(crypto.buf[1]);
	cksum = (y >> 16) & 0xffff;
	if (cksum == 0)
		cksum = 1; /* zero checksums are not permitted */

	if (cksum != sp->hdr.cksum) {
		ret = rxrpc_abort_eproto(call, skb, RXKADSEALEDINCON,
					 rxkad_abort_bad_checksum);
		goto out;
	}

	switch (call->conn->security_level) {
	case RXRPC_SECURITY_PLAIN:
		ret = 0;
		break;
	case RXRPC_SECURITY_AUTH:
		ret = rxkad_verify_packet_1(call, skb, seq);
		break;
	case RXRPC_SECURITY_ENCRYPT:
		ret = rxkad_verify_packet_2(call, skb, seq);
		break;
	default:
		ret = -ENOANO;
		break;
	}
out:
	return ret;
}

/*
 * issue a challenge
 */
static int rxkad_issue_challenge(struct rxrpc_connection *conn)
{
	struct rxkad_challenge challenge;
	struct rxrpc_wire_header whdr;
	struct msghdr msg;
	struct kvec iov[2];
	size_t len;
	u32 serial;
	int ret;

	_enter("{%d}", conn->debug_id);

	get_random_bytes(&conn->rxkad.nonce, sizeof(conn->rxkad.nonce));

	challenge.version	= htonl(2);
	challenge.nonce		= htonl(conn->rxkad.nonce);
	challenge.min_level	= htonl(0);
	challenge.__padding	= 0;

	msg.msg_name	= &conn->peer->srx.transport;
	msg.msg_namelen	= conn->peer->srx.transport_len;
	msg.msg_control	= NULL;
	msg.msg_controllen = 0;
	msg.msg_flags	= 0;

	whdr.epoch	= htonl(conn->proto.epoch);
	whdr.cid	= htonl(conn->proto.cid);
	whdr.callNumber	= 0;
	whdr.seq	= 0;
	whdr.type	= RXRPC_PACKET_TYPE_CHALLENGE;
	whdr.flags	= conn->out_clientflag;
	whdr.userStatus	= 0;
	whdr.securityIndex = conn->security_ix;
	whdr._rsvd	= 0;
	whdr.serviceId	= htons(conn->service_id);

	iov[0].iov_base	= &whdr;
	iov[0].iov_len	= sizeof(whdr);
	iov[1].iov_base	= &challenge;
	iov[1].iov_len	= sizeof(challenge);

	len = iov[0].iov_len + iov[1].iov_len;

	serial = rxrpc_get_next_serial(conn);
	whdr.serial = htonl(serial);

	trace_rxrpc_tx_challenge(conn, serial, 0, conn->rxkad.nonce);

	ret = kernel_sendmsg(conn->local->socket, &msg, iov, 2, len);
	if (ret < 0) {
		trace_rxrpc_tx_fail(conn->debug_id, serial, ret,
				    rxrpc_tx_point_rxkad_challenge);
		return -EAGAIN;
	}

	rxrpc_peer_mark_tx(conn->peer);
	trace_rxrpc_tx_packet(conn->debug_id, &whdr,
			      rxrpc_tx_point_rxkad_challenge);
	_leave(" = 0");
	return 0;
}

/*
 * calculate the response checksum
 */
static void rxkad_calc_response_checksum(struct rxkad_response *response)
{
	u32 csum = 1000003;
	int loop;
	u8 *p = (u8 *) response;

	for (loop = sizeof(*response); loop > 0; loop--)
		csum = csum * 0x10204081 + *p++;

	response->encrypted.checksum = htonl(csum);
}

/*
 * Validate a challenge packet.
 */
static bool rxkad_validate_challenge(struct rxrpc_connection *conn,
				     struct sk_buff *skb)
{
	struct rxkad_challenge challenge;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	u32 version, min_level;
	int ret;

	_enter("{%d,%x}", conn->debug_id, key_serial(conn->key));

	if (!conn->key) {
		rxrpc_abort_conn(conn, skb, RX_PROTOCOL_ERROR, -EPROTO,
				 rxkad_abort_chall_no_key);
		return false;
	}

	ret = key_validate(conn->key);
	if (ret < 0) {
		rxrpc_abort_conn(conn, skb, RXKADEXPIRED, ret,
				 rxkad_abort_chall_key_expired);
		return false;
	}

	if (skb_copy_bits(skb, sizeof(struct rxrpc_wire_header),
			  &challenge, sizeof(challenge)) < 0) {
		rxrpc_abort_conn(conn, skb, RXKADPACKETSHORT, -EPROTO,
				 rxkad_abort_chall_short);
		return false;
	}

	version = ntohl(challenge.version);
	sp->chall.rxkad_nonce = ntohl(challenge.nonce);
	min_level = ntohl(challenge.min_level);

	trace_rxrpc_rx_challenge(conn, sp->hdr.serial, version,
				 sp->chall.rxkad_nonce, min_level);

	if (version != RXKAD_VERSION) {
		rxrpc_abort_conn(conn, skb, RXKADINCONSISTENCY, -EPROTO,
				 rxkad_abort_chall_version);
		return false;
	}

	if (conn->security_level < min_level) {
		rxrpc_abort_conn(conn, skb, RXKADLEVELFAIL, -EACCES,
				 rxkad_abort_chall_level);
		return false;
	}
	return true;
}

/*
 * Insert the header into the response.
 */
static noinline
int rxkad_insert_response_header(struct rxrpc_connection *conn,
				 const struct rxrpc_key_token *token,
				 struct sk_buff *challenge,
				 struct sk_buff *response,
				 size_t *offset)
{
	struct rxrpc_skb_priv *csp = rxrpc_skb(challenge);
	struct {
		struct rxrpc_wire_header whdr;
		struct rxkad_response	resp;
	} h;
	int ret;

	h.whdr.epoch			= htonl(conn->proto.epoch);
	h.whdr.cid			= htonl(conn->proto.cid);
	h.whdr.callNumber		= 0;
	h.whdr.serial			= 0;
	h.whdr.seq			= 0;
	h.whdr.type			= RXRPC_PACKET_TYPE_RESPONSE;
	h.whdr.flags			= conn->out_clientflag;
	h.whdr.userStatus		= 0;
	h.whdr.securityIndex		= conn->security_ix;
	h.whdr.cksum			= 0;
	h.whdr.serviceId		= htons(conn->service_id);
	h.resp.version			= htonl(RXKAD_VERSION);
	h.resp.__pad			= 0;
	h.resp.encrypted.epoch		= htonl(conn->proto.epoch);
	h.resp.encrypted.cid		= htonl(conn->proto.cid);
	h.resp.encrypted.checksum	= 0;
	h.resp.encrypted.securityIndex	= htonl(conn->security_ix);
	h.resp.encrypted.call_id[0]	= htonl(conn->channels[0].call_counter);
	h.resp.encrypted.call_id[1]	= htonl(conn->channels[1].call_counter);
	h.resp.encrypted.call_id[2]	= htonl(conn->channels[2].call_counter);
	h.resp.encrypted.call_id[3]	= htonl(conn->channels[3].call_counter);
	h.resp.encrypted.inc_nonce	= htonl(csp->chall.rxkad_nonce + 1);
	h.resp.encrypted.level		= htonl(conn->security_level);
	h.resp.kvno			= htonl(token->kad->kvno);
	h.resp.ticket_len		= htonl(token->kad->ticket_len);

	rxkad_calc_response_checksum(&h.resp);

	/* encrypt the response packet */
	static_assert(sizeof(h.resp.encrypted) % FCRYPT_BSIZE == 0);
	fcrypt_pcbc_encrypt(conn->rxkad.cipher, token->kad->session_key,
			    &h.resp.encrypted, &h.resp.encrypted,
			    sizeof(h.resp.encrypted) / FCRYPT_BSIZE);

	ret = skb_store_bits(response, *offset, &h, sizeof(h));
	*offset += sizeof(h);
	return ret;
}

/*
 * respond to a challenge packet
 */
static int rxkad_respond_to_challenge(struct rxrpc_connection *conn,
				      struct sk_buff *challenge)
{
	const struct rxrpc_key_token *token;
	struct rxrpc_skb_priv *csp, *rsp;
	struct sk_buff *response;
	size_t len, offset = 0;
	int ret = -EPROTO;

	_enter("{%d,%x}", conn->debug_id, key_serial(conn->key));

	ret = key_validate(conn->key);
	if (ret < 0)
		return rxrpc_abort_conn(conn, challenge, RXKADEXPIRED, ret,
					rxkad_abort_chall_key_expired);

	token = conn->key->payload.data[0];

	/* build the response packet */
	len = sizeof(struct rxrpc_wire_header) +
		sizeof(struct rxkad_response) +
		token->kad->ticket_len;

	response = alloc_skb_with_frags(0, len, 0, &ret, GFP_NOFS);
	if (!response)
		goto error;
	rxrpc_new_skb(response, rxrpc_skb_new_response_rxkad);
	response->len = len;
	response->data_len = len;

	offset = 0;
	ret = rxkad_insert_response_header(conn, token, challenge, response,
					   &offset);
	if (ret < 0)
		goto error;

	ret = skb_store_bits(response, offset, token->kad->ticket,
			     token->kad->ticket_len);
	if (ret < 0)
		goto error;

	csp = rxrpc_skb(challenge);
	rsp = rxrpc_skb(response);
	rsp->resp.len = len;
	rsp->resp.challenge_serial = csp->hdr.serial;
	rxrpc_post_response(conn, response);
	response = NULL;
	ret = 0;

error:
	rxrpc_free_skb(response, rxrpc_skb_put_response);
	return ret;
}

/*
 * RxKAD does automatic response only as there's nothing to manage that isn't
 * already in the key.
 */
static int rxkad_sendmsg_respond_to_challenge(struct sk_buff *challenge,
					      struct msghdr *msg)
{
	return -EINVAL;
}

/**
 * rxkad_kernel_respond_to_challenge - Respond to a challenge with appdata
 * @challenge: The challenge to respond to
 *
 * Allow a kernel application to respond to a CHALLENGE.
 *
 * Return: %0 if successful and a negative error code otherwise.
 */
int rxkad_kernel_respond_to_challenge(struct sk_buff *challenge)
{
	struct rxrpc_skb_priv *csp = rxrpc_skb(challenge);

	return rxkad_respond_to_challenge(csp->chall.conn, challenge);
}
EXPORT_SYMBOL(rxkad_kernel_respond_to_challenge);

/* Decrypt data in-place using DES-PCBC.  @len must be a multiple of 8. */
VISIBLE_IF_KUNIT void des_pcbc_decrypt_inplace(const struct des_ctx *key,
					       __le64 iv, u8 *data, size_t len)
{
	for (size_t i = 0; i < len; i += DES_BLOCK_SIZE) {
		__le64 ctext, ptext;

		ctext = get_unaligned((const __le64 *)&data[i]);
		des_decrypt(key, (u8 *)&ptext, (const u8 *)&ctext);
		ptext ^= iv;
		put_unaligned(ptext, (__le64 *)&data[i]);
		iv = ptext ^ ctext;
	}
}
EXPORT_SYMBOL_IF_KUNIT(des_pcbc_decrypt_inplace);

/*
 * decrypt the kerberos IV ticket in the response
 */
static int rxkad_decrypt_ticket(struct rxrpc_connection *conn,
				struct key *server_key,
				struct sk_buff *skb,
				void *ticket, size_t ticket_len,
				struct rxrpc_crypt *_session_key,
				time64_t *_expiry)
{
	struct rxrpc_crypt key;
	struct in_addr addr;
	unsigned int life;
	time64_t issue, now;
	bool little_endian;
	u8 *p, *q, *name, *end;

	_enter("{%d},{%x}", conn->debug_id, key_serial(server_key));

	*_expiry = 0;

	ASSERT(server_key->payload.data[0] != NULL);

	if (ticket_len % DES_BLOCK_SIZE != 0)
		return rxrpc_abort_conn(conn, skb, RXKADBADTICKET, -EPROTO,
					rxkad_abort_resp_tkt_short);
	des_pcbc_decrypt_inplace(
		server_key->payload.data[0],
		get_unaligned((const __le64 *)&server_key->payload.data[2]),
		ticket, ticket_len);
	p = ticket;
	end = p + ticket_len;

#define Z(field, fieldl)						\
	({								\
		u8 *__str = p;						\
		q = memchr(p, 0, end - p);				\
		if (!q || q - p > field##_SZ)				\
			return rxrpc_abort_conn(			\
				conn, skb, RXKADBADTICKET, -EPROTO,	\
				rxkad_abort_resp_tkt_##fieldl);		\
		for (; p < q; p++)					\
			if (!isprint(*p))				\
				return rxrpc_abort_conn(		\
					conn, skb, RXKADBADTICKET, -EPROTO, \
					rxkad_abort_resp_tkt_##fieldl);	\
		p++;							\
		__str;							\
	})

	/* extract the ticket flags */
	_debug("KIV FLAGS: %x", *p);
	little_endian = *p & 1;
	p++;

	/* extract the authentication name */
	name = Z(ANAME, aname);
	_debug("KIV ANAME: %s", name);

	/* extract the principal's instance */
	name = Z(INST, inst);
	_debug("KIV INST : %s", name);

	/* extract the principal's authentication domain */
	name = Z(REALM, realm);
	_debug("KIV REALM: %s", name);

	if (end - p < 4 + 8 + 4 + 2)
		return rxrpc_abort_conn(conn, skb, RXKADBADTICKET, -EPROTO,
					rxkad_abort_resp_tkt_short);

	/* get the IPv4 address of the entity that requested the ticket */
	memcpy(&addr, p, sizeof(addr));
	p += 4;
	_debug("KIV ADDR : %pI4", &addr);

	/* get the session key from the ticket */
	memcpy(&key, p, sizeof(key));
	p += 8;
	_debug("KIV KEY  : %08x %08x", ntohl(key.n[0]), ntohl(key.n[1]));
	memcpy(_session_key, &key, sizeof(key));

	/* get the ticket's lifetime */
	life = *p++ * 5 * 60;
	_debug("KIV LIFE : %u", life);

	/* get the issue time of the ticket */
	if (little_endian) {
		__le32 stamp;
		memcpy(&stamp, p, 4);
		issue = rxrpc_u32_to_time64(le32_to_cpu(stamp));
	} else {
		__be32 stamp;
		memcpy(&stamp, p, 4);
		issue = rxrpc_u32_to_time64(be32_to_cpu(stamp));
	}
	p += 4;
	now = ktime_get_real_seconds();
	_debug("KIV ISSUE: %llx [%llx]", issue, now);

	/* check the ticket is in date */
	if (issue > now)
		return rxrpc_abort_conn(conn, skb, RXKADNOAUTH, -EKEYREJECTED,
					rxkad_abort_resp_tkt_future);
	if (issue < now - life)
		return rxrpc_abort_conn(conn, skb, RXKADEXPIRED, -EKEYEXPIRED,
					rxkad_abort_resp_tkt_expired);

	*_expiry = issue + life;

	/* get the service name */
	name = Z(SNAME, sname);
	_debug("KIV SNAME: %s", name);

	/* get the service instance name */
	name = Z(INST, sinst);
	_debug("KIV SINST: %s", name);
	return 0;
}

/*
 * decrypt the response packet
 */
static void rxkad_decrypt_response(struct rxrpc_connection *conn,
				   struct rxkad_response *resp,
				   const struct rxrpc_crypt *session_key)
{
	struct fcrypt_key cipher;

	_enter(",,%08x%08x",
	       ntohl(session_key->n[0]), ntohl(session_key->n[1]));

	fcrypt_preparekey(&cipher, session_key->x);

	static_assert(sizeof(resp->encrypted) % FCRYPT_BSIZE == 0);
	fcrypt_pcbc_decrypt(&cipher, session_key->x, &resp->encrypted,
			    &resp->encrypted,
			    sizeof(resp->encrypted) / FCRYPT_BSIZE);
	_leave("");
}

/*
 * verify a response
 */
static int rxkad_verify_response(struct rxrpc_connection *conn,
				 struct sk_buff *skb,
				 void *buffer, unsigned int len)
{
	struct rxkad_response *response;
	struct rxrpc_skb_priv *sp = rxrpc_skb(skb);
	struct rxrpc_crypt session_key;
	struct key *server_key;
	time64_t expiry;
	void *ticket;
	u32 version, kvno, ticket_len, level;
	__be32 csum;
	int ret, i;

	_enter("{%d}", conn->debug_id);

	server_key = rxrpc_look_up_server_security(conn, skb, 0, 0);
	if (IS_ERR(server_key)) {
		ret = PTR_ERR(server_key);
		switch (ret) {
		case -ENOKEY:
			return rxrpc_abort_conn(conn, skb, RXKADUNKNOWNKEY, ret,
						rxkad_abort_resp_nokey);
		case -EKEYEXPIRED:
			return rxrpc_abort_conn(conn, skb, RXKADEXPIRED, ret,
						rxkad_abort_resp_key_expired);
		default:
			return rxrpc_abort_conn(conn, skb, RXKADNOAUTH, ret,
						rxkad_abort_resp_key_rejected);
		}
	}

	response = buffer;
	if (len < sizeof(*response)) {
		ret = rxrpc_abort_conn(conn, skb, RXKADPACKETSHORT, -EPROTO,
				       rxkad_abort_resp_short);
		goto error;
	}

	version = ntohl(response->version);
	ticket_len = ntohl(response->ticket_len);
	kvno = ntohl(response->kvno);

	trace_rxrpc_rx_response(conn, sp->hdr.serial, version, kvno, ticket_len);

	buffer	+= sizeof(*response);
	len	-= sizeof(*response);

	if (version != RXKAD_VERSION) {
		ret = rxrpc_abort_conn(conn, skb, RXKADINCONSISTENCY, -EPROTO,
				       rxkad_abort_resp_version);
		goto error;
	}

	if (ticket_len < 4 || ticket_len > MAXKRB5TICKETLEN) {
		ret = rxrpc_abort_conn(conn, skb, RXKADTICKETLEN, -EPROTO,
				       rxkad_abort_resp_tkt_len);
		goto error;
	}

	if (kvno >= RXKAD_TKT_TYPE_KERBEROS_V5) {
		ret = rxrpc_abort_conn(conn, skb, RXKADUNKNOWNKEY, -EPROTO,
				       rxkad_abort_resp_unknown_tkt);
		goto error;
	}

	/* extract the kerberos ticket and decrypt and decode it */
	ticket = buffer;
	if (ticket_len > len) {
		ret = rxrpc_abort_conn(conn, skb, RXKADPACKETSHORT, -EPROTO,
				       rxkad_abort_resp_short_tkt);
		goto error;
	}

	ret = rxkad_decrypt_ticket(conn, server_key, skb, ticket, ticket_len,
				   &session_key, &expiry);
	if (ret < 0)
		goto error;

	/* use the session key from inside the ticket to decrypt the
	 * response */
	rxkad_decrypt_response(conn, response, &session_key);

	if (ntohl(response->encrypted.epoch) != conn->proto.epoch ||
	    ntohl(response->encrypted.cid) != conn->proto.cid ||
	    ntohl(response->encrypted.securityIndex) != conn->security_ix) {
		ret = rxrpc_abort_conn(conn, skb, RXKADSEALEDINCON, -EPROTO,
				       rxkad_abort_resp_bad_param);
		goto error;
	}

	csum = response->encrypted.checksum;
	response->encrypted.checksum = 0;
	rxkad_calc_response_checksum(response);
	if (response->encrypted.checksum != csum) {
		ret = rxrpc_abort_conn(conn, skb, RXKADSEALEDINCON, -EPROTO,
				       rxkad_abort_resp_bad_checksum);
		goto error;
	}

	for (i = 0; i < RXRPC_MAXCALLS; i++) {
		u32 call_id = ntohl(response->encrypted.call_id[i]);
		u32 counter = READ_ONCE(conn->channels[i].call_counter);

		if (call_id > INT_MAX) {
			ret = rxrpc_abort_conn(conn, skb, RXKADSEALEDINCON, -EPROTO,
					       rxkad_abort_resp_bad_callid);
			goto error;
		}

		if (call_id < counter) {
			ret = rxrpc_abort_conn(conn, skb, RXKADSEALEDINCON, -EPROTO,
					       rxkad_abort_resp_call_ctr);
			goto error;
		}

		if (call_id > counter) {
			if (conn->channels[i].call) {
				ret = rxrpc_abort_conn(conn, skb, RXKADSEALEDINCON, -EPROTO,
						 rxkad_abort_resp_call_state);
				goto error;
			}
			conn->channels[i].call_counter = call_id;
		}
	}

	if (ntohl(response->encrypted.inc_nonce) != conn->rxkad.nonce + 1) {
		ret = rxrpc_abort_conn(conn, skb, RXKADOUTOFSEQUENCE, -EPROTO,
				       rxkad_abort_resp_ooseq);
		goto error;
	}

	level = ntohl(response->encrypted.level);
	if (level > RXRPC_SECURITY_ENCRYPT) {
		ret = rxrpc_abort_conn(conn, skb, RXKADLEVELFAIL, -EPROTO,
				       rxkad_abort_resp_level);
		goto error;
	}
	conn->security_level = level;

	/* create a key to hold the security data and expiration time - after
	 * this the connection security can be handled in exactly the same way
	 * as for a client connection */
	ret = rxrpc_get_server_data_key(conn, &session_key, expiry, kvno);

error:
	key_put(server_key);
	_leave(" = %d", ret);
	return ret;
}

/*
 * clear the connection security
 */
static void rxkad_clear(struct rxrpc_connection *conn)
{
	_enter("");

	kfree_sensitive(conn->rxkad.cipher);
	conn->rxkad.cipher = NULL;
}

/*
 * Initialise the rxkad security service.
 */
static int rxkad_init(void)
{
	if (fips_enabled) {
		pr_warn("rxkad support is disabled due to FIPS\n");
		return -ENOENT;
	}
	return 0;
}

/*
 * Clean up the rxkad security service.
 */
static void rxkad_exit(void)
{
}

/*
 * RxRPC Kerberos-based security
 */
const struct rxrpc_security rxkad = {
	.name				= "rxkad",
	.security_index			= RXRPC_SECURITY_RXKAD,
	.no_key_abort			= RXKADUNKNOWNKEY,
	.init				= rxkad_init,
	.exit				= rxkad_exit,
	.preparse_server_key		= rxkad_preparse_server_key,
	.free_preparse_server_key	= rxkad_free_preparse_server_key,
	.destroy_server_key		= rxkad_destroy_server_key,
	.init_connection_security	= rxkad_init_connection_security,
	.alloc_txbuf			= rxkad_alloc_txbuf,
	.secure_packet			= rxkad_secure_packet,
	.verify_packet			= rxkad_verify_packet,
	.free_call_crypto		= rxkad_free_call_crypto,
	.issue_challenge		= rxkad_issue_challenge,
	.validate_challenge		= rxkad_validate_challenge,
	.sendmsg_respond_to_challenge	= rxkad_sendmsg_respond_to_challenge,
	.respond_to_challenge		= rxkad_respond_to_challenge,
	.verify_response		= rxkad_verify_response,
	.clear				= rxkad_clear,
};
