// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC key management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * RxRPC keys should have a description of describing their purpose:
 *	"afs@example.com"
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <crypto/skcipher.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/key-type.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <keys/rxrpc-type.h>
#include <keys/user-type.h>
#include "ar-internal.h"

static int rxrpc_preparse(struct key_preparsed_payload *);
static void rxrpc_free_preparse(struct key_preparsed_payload *);
static void rxrpc_destroy(struct key *);
static void rxrpc_describe(const struct key *, struct seq_file *);
static long rxrpc_read(const struct key *, char *, size_t);

/*
 * rxrpc defined keys take an arbitrary string as the description and an
 * arbitrary blob of data as the payload
 */
struct key_type key_type_rxrpc = {
	.name		= "rxrpc",
	.flags		= KEY_TYPE_NET_DOMAIN,
	.preparse	= rxrpc_preparse,
	.free_preparse	= rxrpc_free_preparse,
	.instantiate	= generic_key_instantiate,
	.destroy	= rxrpc_destroy,
	.describe	= rxrpc_describe,
	.read		= rxrpc_read,
};
EXPORT_SYMBOL(key_type_rxrpc);

/*
 * parse an RxKAD type XDR format token
 * - the caller guarantees we have at least 4 words
 */
static int rxrpc_preparse_xdr_rxkad(struct key_preparsed_payload *prep,
				    size_t datalen,
				    const __be32 *xdr, unsigned int toklen)
{
	struct rxrpc_key_token *token, **pptoken;
	time64_t expiry;
	size_t plen;
	u32 tktlen;

	_enter(",{%x,%x,%x,%x},%u",
	       ntohl(xdr[0]), ntohl(xdr[1]), ntohl(xdr[2]), ntohl(xdr[3]),
	       toklen);

	if (toklen <= 8 * 4)
		return -EKEYREJECTED;
	tktlen = ntohl(xdr[7]);
	_debug("tktlen: %x", tktlen);
	if (tktlen > AFSTOKEN_RK_TIX_MAX)
		return -EKEYREJECTED;
	if (toklen < 8 * 4 + tktlen)
		return -EKEYREJECTED;

	plen = sizeof(*token) + sizeof(*token->kad) + tktlen;
	prep->quotalen = datalen + plen;

	plen -= sizeof(*token);
	token = kzalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		return -ENOMEM;

	token->kad = kzalloc(plen, GFP_KERNEL);
	if (!token->kad) {
		kfree(token);
		return -ENOMEM;
	}

	token->security_index	= RXRPC_SECURITY_RXKAD;
	token->kad->ticket_len	= tktlen;
	token->kad->vice_id	= ntohl(xdr[0]);
	token->kad->kvno	= ntohl(xdr[1]);
	token->kad->start	= ntohl(xdr[4]);
	token->kad->expiry	= ntohl(xdr[5]);
	token->kad->primary_flag = ntohl(xdr[6]);
	memcpy(&token->kad->session_key, &xdr[2], 8);
	memcpy(&token->kad->ticket, &xdr[8], tktlen);

	_debug("SCIX: %u", token->security_index);
	_debug("TLEN: %u", token->kad->ticket_len);
	_debug("EXPY: %x", token->kad->expiry);
	_debug("KVNO: %u", token->kad->kvno);
	_debug("PRIM: %u", token->kad->primary_flag);
	_debug("SKEY: %02x%02x%02x%02x%02x%02x%02x%02x",
	       token->kad->session_key[0], token->kad->session_key[1],
	       token->kad->session_key[2], token->kad->session_key[3],
	       token->kad->session_key[4], token->kad->session_key[5],
	       token->kad->session_key[6], token->kad->session_key[7]);
	if (token->kad->ticket_len >= 8)
		_debug("TCKT: %02x%02x%02x%02x%02x%02x%02x%02x",
		       token->kad->ticket[0], token->kad->ticket[1],
		       token->kad->ticket[2], token->kad->ticket[3],
		       token->kad->ticket[4], token->kad->ticket[5],
		       token->kad->ticket[6], token->kad->ticket[7]);

	/* count the number of tokens attached */
	prep->payload.data[1] = (void *)((unsigned long)prep->payload.data[1] + 1);

	/* attach the data */
	for (pptoken = (struct rxrpc_key_token **)&prep->payload.data[0];
	     *pptoken;
	     pptoken = &(*pptoken)->next)
		continue;
	*pptoken = token;
	expiry = rxrpc_u32_to_time64(token->kad->expiry);
	if (expiry < prep->expiry)
		prep->expiry = expiry;

	_leave(" = 0");
	return 0;
}

static u64 xdr_dec64(const __be32 *xdr)
{
	return (u64)ntohl(xdr[0]) << 32 | (u64)ntohl(xdr[1]);
}

static time64_t rxrpc_s64_to_time64(s64 time_in_100ns)
{
	bool neg = false;
	u64 tmp = time_in_100ns;

	if (time_in_100ns < 0) {
		tmp = -time_in_100ns;
		neg = true;
	}
	do_div(tmp, 10000000);
	return neg ? -tmp : tmp;
}

/*
 * Parse a YFS-RxGK type XDR format token
 * - the caller guarantees we have at least 4 words
 *
 * struct token_rxgk {
 *	opr_time begintime;
 *	opr_time endtime;
 *	afs_int64 level;
 *	afs_int64 lifetime;
 *	afs_int64 bytelife;
 *	afs_int64 enctype;
 *	opaque key<>;
 *	opaque ticket<>;
 * };
 */
static int rxrpc_preparse_xdr_yfs_rxgk(struct key_preparsed_payload *prep,
				       size_t datalen,
				       const __be32 *xdr, unsigned int toklen)
{
	struct rxrpc_key_token *token, **pptoken;
	time64_t expiry;
	size_t plen;
	const __be32 *ticket, *key;
	s64 tmp;
	u32 tktlen, keylen;

	_enter(",{%x,%x,%x,%x},%x",
	       ntohl(xdr[0]), ntohl(xdr[1]), ntohl(xdr[2]), ntohl(xdr[3]),
	       toklen);

	if (6 * 2 + 2 > toklen / 4)
		goto reject;

	key = xdr + (6 * 2 + 1);
	keylen = ntohl(key[-1]);
	_debug("keylen: %x", keylen);
	keylen = round_up(keylen, 4);
	if ((6 * 2 + 2) * 4 + keylen > toklen)
		goto reject;

	ticket = xdr + (6 * 2 + 1 + (keylen / 4) + 1);
	tktlen = ntohl(ticket[-1]);
	_debug("tktlen: %x", tktlen);
	tktlen = round_up(tktlen, 4);
	if ((6 * 2 + 2) * 4 + keylen + tktlen != toklen) {
		kleave(" = -EKEYREJECTED [%x!=%x, %x,%x]",
		       (6 * 2 + 2) * 4 + keylen + tktlen, toklen,
		       keylen, tktlen);
		goto reject;
	}

	plen = sizeof(*token) + sizeof(*token->rxgk) + tktlen + keylen;
	prep->quotalen = datalen + plen;

	plen -= sizeof(*token);
	token = kzalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		goto nomem;

	token->rxgk = kzalloc(sizeof(*token->rxgk) + keylen, GFP_KERNEL);
	if (!token->rxgk)
		goto nomem_token;

	token->security_index	= RXRPC_SECURITY_YFS_RXGK;
	token->rxgk->begintime	= xdr_dec64(xdr + 0 * 2);
	token->rxgk->endtime	= xdr_dec64(xdr + 1 * 2);
	token->rxgk->level	= tmp = xdr_dec64(xdr + 2 * 2);
	if (tmp < -1LL || tmp > RXRPC_SECURITY_ENCRYPT)
		goto reject_token;
	token->rxgk->lifetime	= xdr_dec64(xdr + 3 * 2);
	token->rxgk->bytelife	= xdr_dec64(xdr + 4 * 2);
	token->rxgk->enctype	= tmp = xdr_dec64(xdr + 5 * 2);
	if (tmp < 0 || tmp > UINT_MAX)
		goto reject_token;
	token->rxgk->key.len	= ntohl(key[-1]);
	token->rxgk->key.data	= token->rxgk->_key;
	token->rxgk->ticket.len = ntohl(ticket[-1]);

	if (token->rxgk->endtime != 0) {
		expiry = rxrpc_s64_to_time64(token->rxgk->endtime);
		if (expiry < 0)
			goto expired;
		if (expiry < prep->expiry)
			prep->expiry = expiry;
	}

	memcpy(token->rxgk->key.data, key, token->rxgk->key.len);

	/* Pad the ticket so that we can use it directly in XDR */
	token->rxgk->ticket.data = kzalloc(round_up(token->rxgk->ticket.len, 4),
					   GFP_KERNEL);
	if (!token->rxgk->ticket.data)
		goto nomem_yrxgk;
	memcpy(token->rxgk->ticket.data, ticket, token->rxgk->ticket.len);

	_debug("SCIX: %u",	token->security_index);
	_debug("EXPY: %llx",	token->rxgk->endtime);
	_debug("LIFE: %llx",	token->rxgk->lifetime);
	_debug("BYTE: %llx",	token->rxgk->bytelife);
	_debug("ENC : %u",	token->rxgk->enctype);
	_debug("LEVL: %u",	token->rxgk->level);
	_debug("KLEN: %u",	token->rxgk->key.len);
	_debug("TLEN: %u",	token->rxgk->ticket.len);
	_debug("KEY0: %*phN",	token->rxgk->key.len, token->rxgk->key.data);
	_debug("TICK: %*phN",
	       min_t(u32, token->rxgk->ticket.len, 32), token->rxgk->ticket.data);

	/* count the number of tokens attached */
	prep->payload.data[1] = (void *)((unsigned long)prep->payload.data[1] + 1);

	/* attach the data */
	for (pptoken = (struct rxrpc_key_token **)&prep->payload.data[0];
	     *pptoken;
	     pptoken = &(*pptoken)->next)
		continue;
	*pptoken = token;

	_leave(" = 0");
	return 0;

nomem_yrxgk:
	kfree(token->rxgk);
nomem_token:
	kfree(token);
nomem:
	return -ENOMEM;
reject_token:
	kfree(token);
reject:
	return -EKEYREJECTED;
expired:
	kfree(token->rxgk);
	kfree(token);
	return -EKEYEXPIRED;
}

/*
 * attempt to parse the data as the XDR format
 * - the caller guarantees we have more than 7 words
 */
static int rxrpc_preparse_xdr(struct key_preparsed_payload *prep)
{
	const __be32 *xdr = prep->data, *token, *p;
	const char *cp;
	unsigned int len, paddedlen, loop, ntoken, toklen, sec_ix;
	size_t datalen = prep->datalen;
	int ret, ret2;

	_enter(",{%x,%x,%x,%x},%zu",
	       ntohl(xdr[0]), ntohl(xdr[1]), ntohl(xdr[2]), ntohl(xdr[3]),
	       prep->datalen);

	if (datalen > AFSTOKEN_LENGTH_MAX)
		goto not_xdr;

	/* XDR is an array of __be32's */
	if (datalen & 3)
		goto not_xdr;

	/* the flags should be 0 (the setpag bit must be handled by
	 * userspace) */
	if (ntohl(*xdr++) != 0)
		goto not_xdr;
	datalen -= 4;

	/* check the cell name */
	len = ntohl(*xdr++);
	if (len < 1 || len > AFSTOKEN_CELL_MAX)
		goto not_xdr;
	datalen -= 4;
	paddedlen = (len + 3) & ~3;
	if (paddedlen > datalen)
		goto not_xdr;

	cp = (const char *) xdr;
	for (loop = 0; loop < len; loop++)
		if (!isprint(cp[loop]))
			goto not_xdr;
	for (; loop < paddedlen; loop++)
		if (cp[loop])
			goto not_xdr;
	_debug("cellname: [%u/%u] '%*.*s'",
	       len, paddedlen, len, len, (const char *) xdr);
	datalen -= paddedlen;
	xdr += paddedlen >> 2;

	/* get the token count */
	if (datalen < 12)
		goto not_xdr;
	ntoken = ntohl(*xdr++);
	datalen -= 4;
	_debug("ntoken: %x", ntoken);
	if (ntoken < 1 || ntoken > AFSTOKEN_MAX)
		goto not_xdr;

	/* check each token wrapper */
	p = xdr;
	loop = ntoken;
	do {
		if (datalen < 8)
			goto not_xdr;
		toklen = ntohl(*p++);
		sec_ix = ntohl(*p);
		datalen -= 4;
		_debug("token: [%x/%zx] %x", toklen, datalen, sec_ix);
		paddedlen = (toklen + 3) & ~3;
		if (toklen < 20 || toklen > datalen || paddedlen > datalen)
			goto not_xdr;
		datalen -= paddedlen;
		p += paddedlen >> 2;

	} while (--loop > 0);

	_debug("remainder: %zu", datalen);
	if (datalen != 0)
		goto not_xdr;

	/* okay: we're going to assume it's valid XDR format
	 * - we ignore the cellname, relying on the key to be correctly named
	 */
	ret = -EPROTONOSUPPORT;
	do {
		toklen = ntohl(*xdr++);
		token = xdr;
		xdr += (toklen + 3) / 4;

		sec_ix = ntohl(*token++);
		toklen -= 4;

		_debug("TOKEN type=%x len=%x", sec_ix, toklen);

		switch (sec_ix) {
		case RXRPC_SECURITY_RXKAD:
			ret2 = rxrpc_preparse_xdr_rxkad(prep, datalen, token, toklen);
			break;
		case RXRPC_SECURITY_YFS_RXGK:
			ret2 = rxrpc_preparse_xdr_yfs_rxgk(prep, datalen, token, toklen);
			break;
		default:
			ret2 = -EPROTONOSUPPORT;
			break;
		}

		switch (ret2) {
		case 0:
			ret = 0;
			break;
		case -EPROTONOSUPPORT:
			break;
		case -ENOPKG:
			if (ret != 0)
				ret = -ENOPKG;
			break;
		default:
			ret = ret2;
			goto error;
		}

	} while (--ntoken > 0);

error:
	_leave(" = %d", ret);
	return ret;

not_xdr:
	_leave(" = -EPROTO");
	return -EPROTO;
}

/*
 * Preparse an rxrpc defined key.
 *
 * Data should be of the form:
 *	OFFSET	LEN	CONTENT
 *	0	4	key interface version number
 *	4	2	security index (type)
 *	6	2	ticket length
 *	8	4	key expiry time (time_t)
 *	12	4	kvno
 *	16	8	session key
 *	24	[len]	ticket
 *
 * if no data is provided, then a no-security key is made
 */
static int rxrpc_preparse(struct key_preparsed_payload *prep)
{
	const struct rxrpc_key_data_v1 *v1;
	struct rxrpc_key_token *token, **pp;
	time64_t expiry;
	size_t plen;
	u32 kver;
	int ret;

	_enter("%zu", prep->datalen);

	/* handle a no-security key */
	if (!prep->data && prep->datalen == 0)
		return 0;

	/* determine if the XDR payload format is being used */
	if (prep->datalen > 7 * 4) {
		ret = rxrpc_preparse_xdr(prep);
		if (ret != -EPROTO)
			return ret;
	}

	/* get the key interface version number */
	ret = -EINVAL;
	if (prep->datalen <= 4 || !prep->data)
		goto error;
	memcpy(&kver, prep->data, sizeof(kver));
	prep->data += sizeof(kver);
	prep->datalen -= sizeof(kver);

	_debug("KEY I/F VERSION: %u", kver);

	ret = -EKEYREJECTED;
	if (kver != 1)
		goto error;

	/* deal with a version 1 key */
	ret = -EINVAL;
	if (prep->datalen < sizeof(*v1))
		goto error;

	v1 = prep->data;
	if (prep->datalen != sizeof(*v1) + v1->ticket_length)
		goto error;

	_debug("SCIX: %u", v1->security_index);
	_debug("TLEN: %u", v1->ticket_length);
	_debug("EXPY: %x", v1->expiry);
	_debug("KVNO: %u", v1->kvno);
	_debug("SKEY: %02x%02x%02x%02x%02x%02x%02x%02x",
	       v1->session_key[0], v1->session_key[1],
	       v1->session_key[2], v1->session_key[3],
	       v1->session_key[4], v1->session_key[5],
	       v1->session_key[6], v1->session_key[7]);
	if (v1->ticket_length >= 8)
		_debug("TCKT: %02x%02x%02x%02x%02x%02x%02x%02x",
		       v1->ticket[0], v1->ticket[1],
		       v1->ticket[2], v1->ticket[3],
		       v1->ticket[4], v1->ticket[5],
		       v1->ticket[6], v1->ticket[7]);

	ret = -EPROTONOSUPPORT;
	if (v1->security_index != RXRPC_SECURITY_RXKAD)
		goto error;

	plen = sizeof(*token->kad) + v1->ticket_length;
	prep->quotalen = plen + sizeof(*token);

	ret = -ENOMEM;
	token = kzalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		goto error;
	token->kad = kzalloc(plen, GFP_KERNEL);
	if (!token->kad)
		goto error_free;

	token->security_index		= RXRPC_SECURITY_RXKAD;
	token->kad->ticket_len		= v1->ticket_length;
	token->kad->expiry		= v1->expiry;
	token->kad->kvno		= v1->kvno;
	memcpy(&token->kad->session_key, &v1->session_key, 8);
	memcpy(&token->kad->ticket, v1->ticket, v1->ticket_length);

	/* count the number of tokens attached */
	prep->payload.data[1] = (void *)((unsigned long)prep->payload.data[1] + 1);

	/* attach the data */
	pp = (struct rxrpc_key_token **)&prep->payload.data[0];
	while (*pp)
		pp = &(*pp)->next;
	*pp = token;
	expiry = rxrpc_u32_to_time64(token->kad->expiry);
	if (expiry < prep->expiry)
		prep->expiry = expiry;
	token = NULL;
	ret = 0;

error_free:
	kfree(token);
error:
	return ret;
}

/*
 * Free token list.
 */
static void rxrpc_free_token_list(struct rxrpc_key_token *token)
{
	struct rxrpc_key_token *next;

	for (; token; token = next) {
		next = token->next;
		switch (token->security_index) {
		case RXRPC_SECURITY_RXKAD:
			kfree(token->kad);
			break;
		case RXRPC_SECURITY_YFS_RXGK:
			kfree(token->rxgk->ticket.data);
			kfree(token->rxgk);
			break;
		default:
			pr_err("Unknown token type %x on rxrpc key\n",
			       token->security_index);
			BUG();
		}

		kfree(token);
	}
}

/*
 * Clean up preparse data.
 */
static void rxrpc_free_preparse(struct key_preparsed_payload *prep)
{
	rxrpc_free_token_list(prep->payload.data[0]);
}

/*
 * dispose of the data dangling from the corpse of a rxrpc key
 */
static void rxrpc_destroy(struct key *key)
{
	rxrpc_free_token_list(key->payload.data[0]);
}

/*
 * describe the rxrpc key
 */
static void rxrpc_describe(const struct key *key, struct seq_file *m)
{
	const struct rxrpc_key_token *token;
	const char *sep = ": ";

	seq_puts(m, key->description);

	for (token = key->payload.data[0]; token; token = token->next) {
		seq_puts(m, sep);

		switch (token->security_index) {
		case RXRPC_SECURITY_RXKAD:
			seq_puts(m, "ka");
			break;
		case RXRPC_SECURITY_YFS_RXGK:
			seq_puts(m, "ygk");
			break;
		default: /* we have a ticket we can't encode */
			seq_printf(m, "%u", token->security_index);
			break;
		}

		sep = " ";
	}
}

/*
 * grab the security key for a socket
 */
int rxrpc_request_key(struct rxrpc_sock *rx, sockptr_t optval, int optlen)
{
	struct key *key;
	char *description;

	_enter("");

	if (optlen <= 0 || optlen > PAGE_SIZE - 1 || rx->securities)
		return -EINVAL;

	description = memdup_sockptr_nul(optval, optlen);
	if (IS_ERR(description))
		return PTR_ERR(description);

	key = request_key_net(&key_type_rxrpc, description, sock_net(&rx->sk), NULL);
	if (IS_ERR(key)) {
		kfree(description);
		_leave(" = %ld", PTR_ERR(key));
		return PTR_ERR(key);
	}

	rx->key = key;
	kfree(description);
	_leave(" = 0 [key %x]", key->serial);
	return 0;
}

/*
 * generate a server data key
 */
int rxrpc_get_server_data_key(struct rxrpc_connection *conn,
			      const void *session_key,
			      time64_t expiry,
			      u32 kvno)
{
	const struct cred *cred = current_cred();
	struct key *key;
	int ret;

	struct {
		u32 kver;
		struct rxrpc_key_data_v1 v1;
	} data;

	_enter("");

	key = key_alloc(&key_type_rxrpc, "x",
			GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, cred, 0,
			KEY_ALLOC_NOT_IN_QUOTA, NULL);
	if (IS_ERR(key)) {
		_leave(" = -ENOMEM [alloc %ld]", PTR_ERR(key));
		return -ENOMEM;
	}

	_debug("key %d", key_serial(key));

	data.kver = 1;
	data.v1.security_index = RXRPC_SECURITY_RXKAD;
	data.v1.ticket_length = 0;
	data.v1.expiry = rxrpc_time64_to_u32(expiry);
	data.v1.kvno = 0;

	memcpy(&data.v1.session_key, session_key, sizeof(data.v1.session_key));

	ret = key_instantiate_and_link(key, &data, sizeof(data), NULL, NULL);
	if (ret < 0)
		goto error;

	conn->key = key;
	_leave(" = 0 [%d]", key_serial(key));
	return 0;

error:
	key_revoke(key);
	key_put(key);
	_leave(" = -ENOMEM [ins %d]", ret);
	return -ENOMEM;
}
EXPORT_SYMBOL(rxrpc_get_server_data_key);

/**
 * rxrpc_get_null_key - Generate a null RxRPC key
 * @keyname: The name to give the key.
 *
 * Generate a null RxRPC key that can be used to indicate anonymous security is
 * required for a particular domain.
 *
 * Return: The new key or a negative error code.
 */
struct key *rxrpc_get_null_key(const char *keyname)
{
	const struct cred *cred = current_cred();
	struct key *key;
	int ret;

	key = key_alloc(&key_type_rxrpc, keyname,
			GLOBAL_ROOT_UID, GLOBAL_ROOT_GID, cred,
			KEY_POS_SEARCH, KEY_ALLOC_NOT_IN_QUOTA, NULL);
	if (IS_ERR(key))
		return key;

	ret = key_instantiate_and_link(key, NULL, 0, NULL, NULL);
	if (ret < 0) {
		key_revoke(key);
		key_put(key);
		return ERR_PTR(ret);
	}

	return key;
}
EXPORT_SYMBOL(rxrpc_get_null_key);

/*
 * read the contents of an rxrpc key
 * - this returns the result in XDR form
 */
static long rxrpc_read(const struct key *key,
		       char *buffer, size_t buflen)
{
	const struct rxrpc_key_token *token;
	size_t size;
	__be32 *xdr, *oldxdr;
	u32 cnlen, toksize, ntoks, tok, zero;
	u16 toksizes[AFSTOKEN_MAX];

	_enter("");

	/* we don't know what form we should return non-AFS keys in */
	if (memcmp(key->description, "afs@", 4) != 0)
		return -EOPNOTSUPP;
	cnlen = strlen(key->description + 4);

#define RND(X) (((X) + 3) & ~3)

	/* AFS keys we return in XDR form, so we need to work out the size of
	 * the XDR */
	size = 2 * 4;	/* flags, cellname len */
	size += RND(cnlen);	/* cellname */
	size += 1 * 4;	/* token count */

	ntoks = 0;
	for (token = key->payload.data[0]; token; token = token->next) {
		toksize = 4;	/* sec index */

		switch (token->security_index) {
		case RXRPC_SECURITY_RXKAD:
			toksize += 8 * 4;	/* viceid, kvno, key*2, begin,
						 * end, primary, tktlen */
			if (!token->no_leak_key)
				toksize += RND(token->kad->ticket_len);
			break;

		case RXRPC_SECURITY_YFS_RXGK:
			toksize += 6 * 8 + 2 * 4;
			if (!token->no_leak_key)
				toksize += RND(token->rxgk->key.len);
			toksize += RND(token->rxgk->ticket.len);
			break;

		default: /* we have a ticket we can't encode */
			pr_err("Unsupported key token type (%u)\n",
			       token->security_index);
			return -ENOPKG;
		}

		_debug("token[%u]: toksize=%u", ntoks, toksize);
		if (WARN_ON(toksize > AFSTOKEN_LENGTH_MAX))
			return -EIO;

		toksizes[ntoks++] = toksize;
		size += toksize + 4; /* each token has a length word */
	}

#undef RND

	if (!buffer || buflen < size)
		return size;

	xdr = (__be32 *)buffer;
	zero = 0;
#define ENCODE(x)				\
	do {					\
		*xdr++ = htonl(x);		\
	} while(0)
#define ENCODE_DATA(l, s)						\
	do {								\
		u32 _l = (l);						\
		ENCODE(l);						\
		memcpy(xdr, (s), _l);					\
		if (_l & 3)						\
			memcpy((u8 *)xdr + _l, &zero, 4 - (_l & 3));	\
		xdr += (_l + 3) >> 2;					\
	} while(0)
#define ENCODE_BYTES(l, s)						\
	do {								\
		u32 _l = (l);						\
		memcpy(xdr, (s), _l);					\
		if (_l & 3)						\
			memcpy((u8 *)xdr + _l, &zero, 4 - (_l & 3));	\
		xdr += (_l + 3) >> 2;					\
	} while(0)
#define ENCODE64(x)					\
	do {						\
		__be64 y = cpu_to_be64(x);		\
		memcpy(xdr, &y, 8);			\
		xdr += 8 >> 2;				\
	} while(0)
#define ENCODE_STR(s)				\
	do {					\
		const char *_s = (s);		\
		ENCODE_DATA(strlen(_s), _s);	\
	} while(0)

	ENCODE(0);					/* flags */
	ENCODE_DATA(cnlen, key->description + 4);	/* cellname */
	ENCODE(ntoks);

	tok = 0;
	for (token = key->payload.data[0]; token; token = token->next) {
		toksize = toksizes[tok++];
		ENCODE(toksize);
		oldxdr = xdr;
		ENCODE(token->security_index);

		switch (token->security_index) {
		case RXRPC_SECURITY_RXKAD:
			ENCODE(token->kad->vice_id);
			ENCODE(token->kad->kvno);
			ENCODE_BYTES(8, token->kad->session_key);
			ENCODE(token->kad->start);
			ENCODE(token->kad->expiry);
			ENCODE(token->kad->primary_flag);
			if (token->no_leak_key)
				ENCODE(0);
			else
				ENCODE_DATA(token->kad->ticket_len, token->kad->ticket);
			break;

		case RXRPC_SECURITY_YFS_RXGK:
			ENCODE64(token->rxgk->begintime);
			ENCODE64(token->rxgk->endtime);
			ENCODE64(token->rxgk->level);
			ENCODE64(token->rxgk->lifetime);
			ENCODE64(token->rxgk->bytelife);
			ENCODE64(token->rxgk->enctype);
			if (token->no_leak_key)
				ENCODE(0);
			else
				ENCODE_DATA(token->rxgk->key.len, token->rxgk->key.data);
			ENCODE_DATA(token->rxgk->ticket.len, token->rxgk->ticket.data);
			break;

		default:
			pr_err("Unsupported key token type (%u)\n",
			       token->security_index);
			return -ENOPKG;
		}

		if (WARN_ON((unsigned long)xdr - (unsigned long)oldxdr !=
			    toksize))
			return -EIO;
	}

#undef ENCODE_STR
#undef ENCODE_DATA
#undef ENCODE64
#undef ENCODE

	if (WARN_ON(tok != ntoks))
		return -EIO;
	if (WARN_ON((unsigned long)xdr - (unsigned long)buffer != size))
		return -EIO;
	_leave(" = %zu", size);
	return size;
}
