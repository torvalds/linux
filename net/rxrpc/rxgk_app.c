// SPDX-License-Identifier: GPL-2.0-or-later
/* Application-specific bits for GSSAPI-based RxRPC security
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
 * Decode a default-style YFS ticket in a response and turn it into an
 * rxrpc-type key.
 *
 * struct rxgk_key {
 *	afs_uint32	enctype;
 *	opaque		key<>;
 * };
 *
 * struct RXGK_AuthName {
 *	afs_int32	kind;
 *	opaque		data<AUTHDATAMAX>;
 *	opaque		display<AUTHPRINTABLEMAX>;
 * };
 *
 * struct RXGK_Token {
 *	rxgk_key		K0;
 *	RXGK_Level		level;
 *	rxgkTime		starttime;
 *	afs_int32		lifetime;
 *	afs_int32		bytelife;
 *	rxgkTime		expirationtime;
 *	struct RXGK_AuthName	identities<>;
 * };
 */
int rxgk_yfs_decode_ticket(struct rxrpc_connection *conn, struct sk_buff *skb,
			   unsigned int ticket_offset, unsigned int ticket_len,
			   struct key **_key)
{
	struct rxrpc_key_token *token;
	const struct cred *cred = current_cred(); // TODO - use socket creds
	struct key *key;
	size_t pre_ticket_len, payload_len;
	unsigned int klen, enctype;
	void *payload, *ticket;
	__be32 *t, *p, *q, tmp[2];
	int ret;

	_enter("");

	if (ticket_len < 10 * sizeof(__be32))
		return rxrpc_abort_conn(conn, skb, RXGK_INCONSISTENCY, -EPROTO,
					rxgk_abort_resp_short_yfs_tkt);

	/* Get the session key length */
	ret = skb_copy_bits(skb, ticket_offset, tmp, sizeof(tmp));
	if (ret < 0)
		return rxrpc_abort_conn(conn, skb, RXGK_INCONSISTENCY, -EPROTO,
					rxgk_abort_resp_short_yfs_klen);
	enctype = ntohl(tmp[0]);
	klen = ntohl(tmp[1]);

	if (klen > ticket_len - 10 * sizeof(__be32))
		return rxrpc_abort_conn(conn, skb, RXGK_INCONSISTENCY, -EPROTO,
					rxgk_abort_resp_short_yfs_key);

	pre_ticket_len = ((5 + 14) * sizeof(__be32) +
			  xdr_round_up(klen) +
			  sizeof(__be32));
	payload_len = pre_ticket_len + xdr_round_up(ticket_len);

	payload = kzalloc(payload_len, GFP_NOFS);
	if (!payload)
		return -ENOMEM;

	/* We need to fill out the XDR form for a key payload that we can pass
	 * to add_key().  Start by copying in the ticket so that we can parse
	 * it.
	 */
	ticket = payload + pre_ticket_len;
	ret = skb_copy_bits(skb, ticket_offset, ticket, ticket_len);
	if (ret < 0) {
		ret = rxrpc_abort_conn(conn, skb, RXGK_INCONSISTENCY, -EPROTO,
				       rxgk_abort_resp_short_yfs_tkt);
		goto error;
	}

	/* Fill out the form header. */
	p = payload;
	p[0] = htonl(0); /* Flags */
	p[1] = htonl(1); /* len(cellname) */
	p[2] = htonl(0x20000000); /* Cellname " " */
	p[3] = htonl(1); /* #tokens */
	p[4] = htonl(15 * sizeof(__be32) + xdr_round_up(klen) +
		     xdr_round_up(ticket_len)); /* Token len */

	/* Now fill in the body.  Most of this we can just scrape directly from
	 * the ticket.
	 */
	t = ticket + sizeof(__be32) * 2 + xdr_round_up(klen);
	q = payload + 5 * sizeof(__be32);
	q[0]  = htonl(RXRPC_SECURITY_YFS_RXGK);
	q[1]  = t[1];		/* begintime - msw */
	q[2]  = t[2];		/* - lsw */
	q[3]  = t[5];		/* endtime - msw */
	q[4]  = t[6];		/* - lsw */
	q[5]  = 0;		/* level - msw */
	q[6]  = t[0];		/* - lsw */
	q[7]  = 0;		/* lifetime - msw */
	q[8]  = t[3];		/* - lsw */
	q[9]  = 0;		/* bytelife - msw */
	q[10] = t[4];		/* - lsw */
	q[11] = 0;		/* enctype - msw */
	q[12] = htonl(enctype);	/* - lsw */
	q[13] = htonl(klen);	/* Key length */

	q += 14;

	memcpy(q, ticket + sizeof(__be32) * 2, klen);
	q += xdr_round_up(klen) / 4;
	q[0] = htonl(ticket_len);
	q++;
	if (WARN_ON((unsigned long)q != (unsigned long)ticket)) {
		ret = -EIO;
		goto error;
	}

	/* Ticket read in with skb_copy_bits above */
	q += xdr_round_up(ticket_len) / 4;
	if (WARN_ON((unsigned long)q - (unsigned long)payload != payload_len)) {
		ret = -EIO;
		goto error;
	}

	/* Now turn that into a key. */
	key = key_alloc(&key_type_rxrpc, "x",
			GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, cred, // TODO: Use socket owner
			KEY_USR_VIEW,
			KEY_ALLOC_NOT_IN_QUOTA, NULL);
	if (IS_ERR(key)) {
		_leave(" = -ENOMEM [alloc %ld]", PTR_ERR(key));
		ret = PTR_ERR(key);
		goto error;
	}

	_debug("key %d", key_serial(key));

	ret = key_instantiate_and_link(key, payload, payload_len, NULL, NULL);
	if (ret < 0)
		goto error_key;

	token = key->payload.data[0];
	token->no_leak_key = true;
	*_key = key;
	key = NULL;
	ret = 0;
	goto error;

error_key:
	key_put(key);
error:
	kfree_sensitive(payload);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Extract the token and set up a session key from the details.
 *
 * struct RXGK_TokenContainer {
 *	afs_int32	kvno;
 *	afs_int32	enctype;
 *	opaque		encrypted_token<>;
 * };
 *
 * [tools.ietf.org/html/draft-wilkinson-afs3-rxgk-afs-08 sec 6.1]
 */
int rxgk_extract_token(struct rxrpc_connection *conn, struct sk_buff *skb,
		       unsigned int token_offset, unsigned int token_len,
		       struct key **_key)
{
	const struct krb5_enctype *krb5;
	const struct krb5_buffer *server_secret;
	struct crypto_aead *token_enc = NULL;
	struct key *server_key;
	unsigned int ticket_offset, ticket_len;
	u32 kvno, enctype;
	int ret, ec = 0;

	struct {
		__be32 kvno;
		__be32 enctype;
		__be32 token_len;
	} container;

	if (token_len < sizeof(container))
		goto short_packet;

	/* Decode the RXGK_TokenContainer object.  This tells us which server
	 * key we should be using.  We can then fetch the key, get the secret
	 * and set up the crypto to extract the token.
	 */
	if (skb_copy_bits(skb, token_offset, &container, sizeof(container)) < 0)
		goto short_packet;

	kvno		= ntohl(container.kvno);
	enctype		= ntohl(container.enctype);
	ticket_len	= ntohl(container.token_len);
	ticket_offset	= token_offset + sizeof(container);

	if (xdr_round_up(ticket_len) > token_len - sizeof(container))
		goto short_packet;

	_debug("KVNO %u", kvno);
	_debug("ENC  %u", enctype);
	_debug("TLEN %u", ticket_len);

	server_key = rxrpc_look_up_server_security(conn, skb, kvno, enctype);
	if (IS_ERR(server_key))
		goto cant_get_server_key;

	down_read(&server_key->sem);
	server_secret = (const void *)&server_key->payload.data[2];
	ret = rxgk_set_up_token_cipher(server_secret, &token_enc, enctype, &krb5, GFP_NOFS);
	up_read(&server_key->sem);
	key_put(server_key);
	if (ret < 0)
		goto cant_get_token;

	/* We can now decrypt and parse the token/ticket.  This allows us to
	 * gain access to K0, from which we can derive the transport key and
	 * thence decode the authenticator.
	 */
	ret = rxgk_decrypt_skb(krb5, token_enc, skb,
			       &ticket_offset, &ticket_len, &ec);
	crypto_free_aead(token_enc);
	token_enc = NULL;
	if (ret < 0) {
		if (ret != -ENOMEM)
			return rxrpc_abort_conn(conn, skb, ec, ret,
						rxgk_abort_resp_tok_dec);
	}

	ret = conn->security->default_decode_ticket(conn, skb, ticket_offset,
						    ticket_len, _key);
	if (ret < 0)
		goto cant_get_token;

	_leave(" = 0");
	return ret;

cant_get_server_key:
	ret = PTR_ERR(server_key);
	switch (ret) {
	case -ENOMEM:
		goto temporary_error;
	case -ENOKEY:
	case -EKEYREJECTED:
	case -EKEYEXPIRED:
	case -EKEYREVOKED:
	case -EPERM:
		return rxrpc_abort_conn(conn, skb, RXGK_BADKEYNO, -EKEYREJECTED,
					rxgk_abort_resp_tok_nokey);
	default:
		return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EKEYREJECTED,
					rxgk_abort_resp_tok_keyerr);
	}

cant_get_token:
	switch (ret) {
	case -ENOMEM:
		goto temporary_error;
	case -EINVAL:
		return rxrpc_abort_conn(conn, skb, RXGK_NOTAUTH, -EKEYREJECTED,
					rxgk_abort_resp_tok_internal_error);
	case -ENOPKG:
		return rxrpc_abort_conn(conn, skb, KRB5_PROG_KEYTYPE_NOSUPP,
					-EKEYREJECTED, rxgk_abort_resp_tok_nopkg);
	}

temporary_error:
	/* Ignore the response packet if we got a temporary error such as
	 * ENOMEM.  We just want to send the challenge again.  Note that we
	 * also come out this way if the ticket decryption fails.
	 */
	return ret;

short_packet:
	return rxrpc_abort_conn(conn, skb, RXGK_PACKETSHORT, -EPROTO,
				rxgk_abort_resp_tok_short);
}
