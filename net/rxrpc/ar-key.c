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
	const struct rxkad_key *tsec;
	struct rxrpc_key_payload *upayload;
	size_t plen;
	u32 kver;
	int ret;

	_enter("{%x},,%zu", key_serial(key), datalen);

	/* handle a no-security key */
	if (!data && datalen == 0)
		return 0;

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
	if (datalen < sizeof(*tsec))
		goto error;

	tsec = data;
	if (datalen != sizeof(*tsec) + tsec->ticket_len)
		goto error;

	_debug("SCIX: %u", tsec->security_index);
	_debug("TLEN: %u", tsec->ticket_len);
	_debug("EXPY: %x", tsec->expiry);
	_debug("KVNO: %u", tsec->kvno);
	_debug("SKEY: %02x%02x%02x%02x%02x%02x%02x%02x",
	       tsec->session_key[0], tsec->session_key[1],
	       tsec->session_key[2], tsec->session_key[3],
	       tsec->session_key[4], tsec->session_key[5],
	       tsec->session_key[6], tsec->session_key[7]);
	if (tsec->ticket_len >= 8)
		_debug("TCKT: %02x%02x%02x%02x%02x%02x%02x%02x",
		       tsec->ticket[0], tsec->ticket[1],
		       tsec->ticket[2], tsec->ticket[3],
		       tsec->ticket[4], tsec->ticket[5],
		       tsec->ticket[6], tsec->ticket[7]);

	ret = -EPROTONOSUPPORT;
	if (tsec->security_index != 2)
		goto error;

	key->type_data.x[0] = tsec->security_index;

	plen = sizeof(*upayload) + tsec->ticket_len;
	ret = key_payload_reserve(key, plen);
	if (ret < 0)
		goto error;

	ret = -ENOMEM;
	upayload = kmalloc(plen, GFP_KERNEL);
	if (!upayload)
		goto error;

	/* attach the data */
	memcpy(&upayload->k, tsec, sizeof(*tsec));
	memcpy(&upayload->k.ticket, (void *)tsec + sizeof(*tsec),
	       tsec->ticket_len);
	key->payload.data = upayload;
	key->expiry = tsec->expiry;
	ret = 0;

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
	kfree(key->payload.data);
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
		struct rxkad_key tsec;
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
	data.tsec.security_index = 2;
	data.tsec.ticket_len = 0;
	data.tsec.expiry = expiry;
	data.tsec.kvno = 0;

	memcpy(&data.tsec.session_key, session_key,
	       sizeof(data.tsec.session_key));

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
