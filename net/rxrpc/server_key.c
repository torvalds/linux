// SPDX-License-Identifier: GPL-2.0-or-later
/* RxRPC key management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * RxRPC keys should have a description of describing their purpose:
 *	"afs@CAMBRIDGE.REDHAT.COM>
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

static int rxrpc_vet_description_s(const char *);
static int rxrpc_preparse_s(struct key_preparsed_payload *);
static void rxrpc_free_preparse_s(struct key_preparsed_payload *);
static void rxrpc_destroy_s(struct key *);
static void rxrpc_describe_s(const struct key *, struct seq_file *);

/*
 * rxrpc server defined keys take "<serviceId>:<securityIndex>" as the
 * description and an 8-byte decryption key as the payload
 */
struct key_type key_type_rxrpc_s = {
	.name		= "rxrpc_s",
	.flags		= KEY_TYPE_NET_DOMAIN,
	.vet_description = rxrpc_vet_description_s,
	.preparse	= rxrpc_preparse_s,
	.free_preparse	= rxrpc_free_preparse_s,
	.instantiate	= generic_key_instantiate,
	.destroy	= rxrpc_destroy_s,
	.describe	= rxrpc_describe_s,
};

/*
 * Vet the description for an RxRPC server key
 */
static int rxrpc_vet_description_s(const char *desc)
{
	unsigned long num;
	char *p;

	num = simple_strtoul(desc, &p, 10);
	if (*p != ':' || num > 65535)
		return -EINVAL;
	num = simple_strtoul(p + 1, &p, 10);
	if (*p || num < 1 || num > 255)
		return -EINVAL;
	return 0;
}

/*
 * Preparse a server secret key.
 *
 * The data should be the 8-byte secret key.
 */
static int rxrpc_preparse_s(struct key_preparsed_payload *prep)
{
	struct crypto_skcipher *ci;

	_enter("%zu", prep->datalen);

	if (prep->datalen != 8)
		return -EINVAL;

	memcpy(&prep->payload.data[2], prep->data, 8);

	ci = crypto_alloc_skcipher("pcbc(des)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(ci)) {
		_leave(" = %ld", PTR_ERR(ci));
		return PTR_ERR(ci);
	}

	if (crypto_skcipher_setkey(ci, prep->data, 8) < 0)
		BUG();

	prep->payload.data[0] = ci;
	_leave(" = 0");
	return 0;
}

static void rxrpc_free_preparse_s(struct key_preparsed_payload *prep)
{
	if (prep->payload.data[0])
		crypto_free_skcipher(prep->payload.data[0]);
}

static void rxrpc_destroy_s(struct key *key)
{
	if (key->payload.data[0]) {
		crypto_free_skcipher(key->payload.data[0]);
		key->payload.data[0] = NULL;
	}
}

static void rxrpc_describe_s(const struct key *key, struct seq_file *m)
{
	seq_puts(m, key->description);
}

/*
 * grab the security keyring for a server socket
 */
int rxrpc_server_keyring(struct rxrpc_sock *rx, sockptr_t optval, int optlen)
{
	struct key *key;
	char *description;

	_enter("");

	if (optlen <= 0 || optlen > PAGE_SIZE - 1)
		return -EINVAL;

	description = memdup_sockptr_nul(optval, optlen);
	if (IS_ERR(description))
		return PTR_ERR(description);

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
