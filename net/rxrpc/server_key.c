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
 * rxrpc server keys take "<serviceId>:<securityIndex>[:<sec-specific>]" as the
 * description and the key material as the payload.
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
 * Vet the description for an RxRPC server key.
 */
static int rxrpc_vet_description_s(const char *desc)
{
	unsigned long service, sec_class;
	char *p;

	service = simple_strtoul(desc, &p, 10);
	if (*p != ':' || service > 65535)
		return -EINVAL;
	sec_class = simple_strtoul(p + 1, &p, 10);
	if ((*p && *p != ':') || sec_class < 1 || sec_class > 255)
		return -EINVAL;
	return 0;
}

/*
 * Preparse a server secret key.
 */
static int rxrpc_preparse_s(struct key_preparsed_payload *prep)
{
	const struct rxrpc_security *sec;
	unsigned int service, sec_class;
	int n;

	_enter("%zu", prep->datalen);

	if (!prep->orig_description)
		return -EINVAL;

	if (sscanf(prep->orig_description, "%u:%u%n", &service, &sec_class, &n) != 2)
		return -EINVAL;

	sec = rxrpc_security_lookup(sec_class);
	if (!sec)
		return -ENOPKG;

	prep->payload.data[1] = (struct rxrpc_security *)sec;

	return sec->preparse_server_key(prep);
}

static void rxrpc_free_preparse_s(struct key_preparsed_payload *prep)
{
	const struct rxrpc_security *sec = prep->payload.data[1];

	if (sec)
		sec->free_preparse_server_key(prep);
}

static void rxrpc_destroy_s(struct key *key)
{
	const struct rxrpc_security *sec = key->payload.data[1];

	if (sec)
		sec->destroy_server_key(key);
}

static void rxrpc_describe_s(const struct key *key, struct seq_file *m)
{
	const struct rxrpc_security *sec = key->payload.data[1];

	seq_puts(m, key->description);
	if (sec && sec->describe_server_key)
		sec->describe_server_key(key, m);
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
