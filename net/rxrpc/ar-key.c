/* RxRPC key management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * RxRPC keys should have a description of describing their purpose:
 *	"afs@CAMBRIDGE.REDHAT.COM>
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/key-type.h>
#include <linux/crypto.h>
#include <linux/ctype.h>
#include <net/sock.h>
#include <net/af_rxrpc.h>
#include <keys/rxrpc-type.h>
#include <keys/user-type.h>
#include "ar-internal.h"

static int rxrpc_instantiate(struct key *, const void *, size_t);
static int rxrpc_instantiate_s(struct key *, const void *, size_t);
static void rxrpc_destroy(struct key *);
static void rxrpc_destroy_s(struct key *);
static void rxrpc_describe(const struct key *, struct seq_file *);
static long rxrpc_read(const struct key *, char __user *, size_t);

/*
 * rxrpc defined keys take an arbitrary string as the description and an
 * arbitrary blob of data as the payload
 */
struct key_type key_type_rxrpc = {
	.name		= "rxrpc",
	.instantiate	= rxrpc_instantiate,
	.match		= user_match,
	.destroy	= rxrpc_destroy,
	.describe	= rxrpc_describe,
	.read		= rxrpc_read,
};
EXPORT_SYMBOL(key_type_rxrpc);

/*
 * rxrpc server defined keys take "<serviceId>:<securityIndex>" as the
 * description and an 8-byte decryption key as the payload
 */
struct key_type key_type_rxrpc_s = {
	.name		= "rxrpc_s",
	.instantiate	= rxrpc_instantiate_s,
	.match		= user_match,
	.destroy	= rxrpc_destroy_s,
	.describe	= rxrpc_describe,
};

/*
 * parse an RxKAD type XDR format token
 * - the caller guarantees we have at least 4 words
 */
static int rxrpc_instantiate_xdr_rxkad(struct key *key, const __be32 *xdr,
				       unsigned toklen)
{
	struct rxrpc_key_token *token;
	size_t plen;
	u32 tktlen;
	int ret;

	_enter(",{%x,%x,%x,%x},%u",
	       ntohl(xdr[0]), ntohl(xdr[1]), ntohl(xdr[2]), ntohl(xdr[3]),
	       toklen);

	if (toklen <= 8 * 4)
		return -EKEYREJECTED;
	tktlen = ntohl(xdr[7]);
	_debug("tktlen: %x", tktlen);
	if (tktlen > AFSTOKEN_RK_TIX_MAX)
		return -EKEYREJECTED;
	if (8 * 4 + tktlen != toklen)
		return -EKEYREJECTED;

	plen = sizeof(*token) + sizeof(*token->kad) + tktlen;
	ret = key_payload_reserve(key, key->datalen + plen);
	if (ret < 0)
		return ret;

	plen -= sizeof(*token);
	token = kmalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		return -ENOMEM;

	token->kad = kmalloc(plen, GFP_KERNEL);
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
	key->type_data.x[0]++;

	/* attach the data */
	token->next = key->payload.data;
	key->payload.data = token;
	if (token->kad->expiry < key->expiry)
		key->expiry = token->kad->expiry;

	_leave(" = 0");
	return 0;
}

/*
 * attempt to parse the data as the XDR format
 * - the caller guarantees we have more than 7 words
 */
static int rxrpc_instantiate_xdr(struct key *key, const void *data, size_t datalen)
{
	const __be32 *xdr = data, *token;
	const char *cp;
	unsigned len, tmp, loop, ntoken, toklen, sec_ix;
	int ret;

	_enter(",{%x,%x,%x,%x},%zu",
	       ntohl(xdr[0]), ntohl(xdr[1]), ntohl(xdr[2]), ntohl(xdr[3]),
	       datalen);

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
	tmp = (len + 3) & ~3;
	if (tmp > datalen)
		goto not_xdr;

	cp = (const char *) xdr;
	for (loop = 0; loop < len; loop++)
		if (!isprint(cp[loop]))
			goto not_xdr;
	if (len < tmp)
		for (; loop < tmp; loop++)
			if (cp[loop])
				goto not_xdr;
	_debug("cellname: [%u/%u] '%*.*s'",
	       len, tmp, len, len, (const char *) xdr);
	datalen -= tmp;
	xdr += tmp >> 2;

	/* get the token count */
	if (datalen < 12)
		goto not_xdr;
	ntoken = ntohl(*xdr++);
	datalen -= 4;
	_debug("ntoken: %x", ntoken);
	if (ntoken < 1 || ntoken > AFSTOKEN_MAX)
		goto not_xdr;

	/* check each token wrapper */
	token = xdr;
	loop = ntoken;
	do {
		if (datalen < 8)
			goto not_xdr;
		toklen = ntohl(*xdr++);
		sec_ix = ntohl(*xdr);
		datalen -= 4;
		_debug("token: [%x/%zx] %x", toklen, datalen, sec_ix);
		if (toklen < 20 || toklen > datalen)
			goto not_xdr;
		datalen -= (toklen + 3) & ~3;
		xdr += (toklen + 3) >> 2;

	} while (--loop > 0);

	_debug("remainder: %zu", datalen);
	if (datalen != 0)
		goto not_xdr;

	/* okay: we're going to assume it's valid XDR format
	 * - we ignore the cellname, relying on the key to be correctly named
	 */
	do {
		xdr = token;
		toklen = ntohl(*xdr++);
		token = xdr + ((toklen + 3) >> 2);
		sec_ix = ntohl(*xdr++);
		toklen -= 4;

		switch (sec_ix) {
		case RXRPC_SECURITY_RXKAD:
			ret = rxrpc_instantiate_xdr_rxkad(key, xdr, toklen);
			if (ret != 0)
				goto error;
			break;

		default:
			ret = -EPROTONOSUPPORT;
			goto error;
		}

	} while (--ntoken > 0);

	_leave(" = 0");
	return 0;

not_xdr:
	_leave(" = -EPROTO");
	return -EPROTO;
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * instantiate an rxrpc defined key
 * data should be of the form:
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
static int rxrpc_instantiate(struct key *key, const void *data, size_t datalen)
{
	const struct rxrpc_key_data_v1 *v1;
	struct rxrpc_key_token *token, **pp;
	size_t plen;
	u32 kver;
	int ret;

	_enter("{%x},,%zu", key_serial(key), datalen);

	/* handle a no-security key */
	if (!data && datalen == 0)
		return 0;

	/* determine if the XDR payload format is being used */
	if (datalen > 7 * 4) {
		ret = rxrpc_instantiate_xdr(key, data, datalen);
		if (ret != -EPROTO)
			return ret;
	}

	/* get the key interface version number */
	ret = -EINVAL;
	if (datalen <= 4 || !data)
		goto error;
	memcpy(&kver, data, sizeof(kver));
	data += sizeof(kver);
	datalen -= sizeof(kver);

	_debug("KEY I/F VERSION: %u", kver);

	ret = -EKEYREJECTED;
	if (kver != 1)
		goto error;

	/* deal with a version 1 key */
	ret = -EINVAL;
	if (datalen < sizeof(*v1))
		goto error;

	v1 = data;
	if (datalen != sizeof(*v1) + v1->ticket_length)
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
	ret = key_payload_reserve(key, plen + sizeof(*token));
	if (ret < 0)
		goto error;

	ret = -ENOMEM;
	token = kmalloc(sizeof(*token), GFP_KERNEL);
	if (!token)
		goto error;
	token->kad = kmalloc(plen, GFP_KERNEL);
	if (!token->kad)
		goto error_free;

	token->security_index		= RXRPC_SECURITY_RXKAD;
	token->kad->ticket_len		= v1->ticket_length;
	token->kad->expiry		= v1->expiry;
	token->kad->kvno		= v1->kvno;
	memcpy(&token->kad->session_key, &v1->session_key, 8);
	memcpy(&token->kad->ticket, v1->ticket, v1->ticket_length);

	/* attach the data */
	key->type_data.x[0]++;

	pp = (struct rxrpc_key_token **)&key->payload.data;
	while (*pp)
		pp = &(*pp)->next;
	*pp = token;
	if (token->kad->expiry < key->expiry)
		key->expiry = token->kad->expiry;
	token = NULL;
	ret = 0;

error_free:
	kfree(token);
error:
	return ret;
}

/*
 * instantiate a server secret key
 * data should be a pointer to the 8-byte secret key
 */
static int rxrpc_instantiate_s(struct key *key, const void *data,
			       size_t datalen)
{
	struct crypto_blkcipher *ci;

	_enter("{%x},,%zu", key_serial(key), datalen);

	if (datalen != 8)
		return -EINVAL;

	memcpy(&key->type_data, data, 8);

	ci = crypto_alloc_blkcipher("pcbc(des)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(ci)) {
		_leave(" = %ld", PTR_ERR(ci));
		return PTR_ERR(ci);
	}

	if (crypto_blkcipher_setkey(ci, data, 8) < 0)
		BUG();

	key->payload.data = ci;
	_leave(" = 0");
	return 0;
}

/*
 * dispose of the data dangling from the corpse of a rxrpc key
 */
static void rxrpc_destroy(struct key *key)
{
	struct rxrpc_key_token *token;

	while ((token = key->payload.data)) {
		key->payload.data = token->next;
		switch (token->security_index) {
		case RXRPC_SECURITY_RXKAD:
			kfree(token->kad);
			break;
		default:
			printk(KERN_ERR "Unknown token type %x on rxrpc key\n",
			       token->security_index);
			BUG();
		}

		kfree(token);
	}
}

/*
 * dispose of the data dangling from the corpse of a rxrpc key
 */
static void rxrpc_destroy_s(struct key *key)
{
	if (key->payload.data) {
		crypto_free_blkcipher(key->payload.data);
		key->payload.data = NULL;
	}
}

/*
 * describe the rxrpc key
 */
static void rxrpc_describe(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);
}

/*
 * grab the security key for a socket
 */
int rxrpc_request_key(struct rxrpc_sock *rx, char __user *optval, int optlen)
{
	struct key *key;
	char *description;

	_enter("");

	if (optlen <= 0 || optlen > PAGE_SIZE - 1)
		return -EINVAL;

	description = kmalloc(optlen + 1, GFP_KERNEL);
	if (!description)
		return -ENOMEM;

	if (copy_from_user(description, optval, optlen)) {
		kfree(description);
		return -EFAULT;
	}
	description[optlen] = 0;

	key = request_key(&key_type_rxrpc, description, NULL);
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
 * grab the security keyring for a server socket
 */
int rxrpc_server_keyring(struct rxrpc_sock *rx, char __user *optval,
			 int optlen)
{
	struct key *key;
	char *description;

	_enter("");

	if (optlen <= 0 || optlen > PAGE_SIZE - 1)
		return -EINVAL;

	description = kmalloc(optlen + 1, GFP_KERNEL);
	if (!description)
		return -ENOMEM;

	if (copy_from_user(description, optval, optlen)) {
		kfree(description);
		return -EFAULT;
	}
	description[optlen] = 0;

	key = request_key(&key_type_keyring, description, NULL);
	if (IS_ERR(key)) {
		kfree(description);
		_leave(" = %ld", PTR_ERR(key));
		return PTR_ERR(key);
	}

	rx->securities = key;
	kfree(description);
	_leave(" = 0 [key %x]", key->serial);
	return 0;
}

/*
 * generate a server data key
 */
int rxrpc_get_server_data_key(struct rxrpc_connection *conn,
			      const void *session_key,
			      time_t expiry,
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

	key = key_alloc(&key_type_rxrpc, "x", 0, 0, cred, 0,
			KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(key)) {
		_leave(" = -ENOMEM [alloc %ld]", PTR_ERR(key));
		return -ENOMEM;
	}

	_debug("key %d", key_serial(key));

	data.kver = 1;
	data.v1.security_index = RXRPC_SECURITY_RXKAD;
	data.v1.ticket_length = 0;
	data.v1.expiry = expiry;
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
 */
struct key *rxrpc_get_null_key(const char *keyname)
{
	const struct cred *cred = current_cred();
	struct key *key;
	int ret;

	key = key_alloc(&key_type_rxrpc, keyname, 0, 0, cred,
			KEY_POS_SEARCH, KEY_ALLOC_NOT_IN_QUOTA);
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
		       char __user *buffer, size_t buflen)
{
	struct rxrpc_key_token *token;
	size_t size, toksize;
	__be32 __user *xdr;
	u32 cnlen, tktlen, ntoks, zero;

	_enter("");

	/* we don't know what form we should return non-AFS keys in */
	if (memcmp(key->description, "afs@", 4) != 0)
		return -EOPNOTSUPP;
	cnlen = strlen(key->description + 4);

	/* AFS keys we return in XDR form, so we need to work out the size of
	 * the XDR */
	size = 2 * 4;	/* flags, cellname len */
	size += (cnlen + 3) & ~3;	/* cellname */
	size += 1 * 4;	/* token count */

	ntoks = 0;
	for (token = key->payload.data; token; token = token->next) {
		switch (token->security_index) {
		case RXRPC_SECURITY_RXKAD:
			size += 2 * 4;	/* length, security index (switch ID) */
			size += 8 * 4;	/* viceid, kvno, key*2, begin, end,
					 * primary, tktlen */
			size += (token->kad->ticket_len + 3) & ~3; /* ticket */
			ntoks++;
			break;

		default: /* can't encode */
			break;
		}
	}

	if (!buffer || buflen < size)
		return size;

	xdr = (__be32 __user *) buffer;
	zero = 0;
#define ENCODE(x)				\
	do {					\
		__be32 y = htonl(x);		\
		if (put_user(y, xdr++) < 0)	\
			goto fault;		\
	} while(0)

	ENCODE(0);	/* flags */
	ENCODE(cnlen);	/* cellname length */
	if (copy_to_user(xdr, key->description + 4, cnlen) != 0)
		goto fault;
	if (cnlen & 3 &&
	    copy_to_user((u8 *)xdr + cnlen, &zero, 4 - (cnlen & 3)) != 0)
		goto fault;
	xdr += (cnlen + 3) >> 2;
	ENCODE(ntoks);	/* token count */

	for (token = key->payload.data; token; token = token->next) {
		toksize = 1 * 4;	/* sec index */

		switch (token->security_index) {
		case RXRPC_SECURITY_RXKAD:
			toksize += 8 * 4;
			toksize += (token->kad->ticket_len + 3) & ~3;
			ENCODE(toksize);
			ENCODE(token->security_index);
			ENCODE(token->kad->vice_id);
			ENCODE(token->kad->kvno);
			if (copy_to_user(xdr, token->kad->session_key, 8) != 0)
				goto fault;
			xdr += 8 >> 2;
			ENCODE(token->kad->start);
			ENCODE(token->kad->expiry);
			ENCODE(token->kad->primary_flag);
			tktlen = token->kad->ticket_len;
			ENCODE(tktlen);
			if (copy_to_user(xdr, token->kad->ticket, tktlen) != 0)
				goto fault;
			if (tktlen & 3 &&
			    copy_to_user((u8 *)xdr + tktlen, &zero,
					 4 - (tktlen & 3)) != 0)
				goto fault;
			xdr += (tktlen + 3) >> 2;
			break;

		default:
			break;
		}
	}

#undef ENCODE

	ASSERTCMP((char __user *) xdr - buffer, ==, size);
	_leave(" = %zu", size);
	return size;

fault:
	_leave(" = -EFAULT");
	return -EFAULT;
}
