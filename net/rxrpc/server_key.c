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

	if (!sec->preparse_server_key)
		return -EINVAL;

	return sec->preparse_server_key(prep);
}

static void rxrpc_free_preparse_s(struct key_preparsed_payload *prep)
{
	const struct rxrpc_security *sec = prep->payload.data[1];

	if (sec && sec->free_preparse_server_key)
		sec->free_preparse_server_key(prep);
}

static void rxrpc_destroy_s(struct key *key)
{
	const struct rxrpc_security *sec = key->payload.data[1];

	if (sec && sec->destroy_server_key)
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

/**
 * rxrpc_sock_set_security_keyring - Set the security keyring for a kernel service
 * @sk: The socket to set the keyring on
 * @keyring: The keyring to set
 *
 * Set the server security keyring on an rxrpc socket.  This is used to provide
 * the encryption keys for a kernel service.
 *
 * Return: %0 if successful and a negative error code otherwise.
 */
int rxrpc_sock_set_security_keyring(struct sock *sk, struct key *keyring)
{
	struct rxrpc_sock *rx = rxrpc_sk(sk);
	int ret = 0;

	lock_sock(sk);
	if (rx->securities)
		ret = -EINVAL;
	else if (rx->sk.sk_state != RXRPC_UNBOUND)
		ret = -EISCONN;
	else
		rx->securities = key_get(keyring);
	release_sock(sk);
	return ret;
}
EXPORT_SYMBOL(rxrpc_sock_set_security_keyring);

/**
 * rxrpc_sock_set_manage_response - Set the manage-response flag for a kernel service
 * @sk: The socket to set the keyring on
 * @set: True to set, false to clear the flag
 *
 * Set the flag on an rxrpc socket to say that the caller wants to manage the
 * RESPONSE packet and the user-defined data it may contain.  Setting this
 * means that recvmsg() will return messages with RXRPC_CHALLENGED in the
 * control message buffer containing information about the challenge.
 *
 * The user should respond to the challenge by passing RXRPC_RESPOND or
 * RXRPC_RESPOND_ABORT control messages with sendmsg() to the same call.
 * Supplementary control messages, such as RXRPC_RESP_RXGK_APPDATA, may be
 * included to indicate the parts the user wants to supply.
 *
 * The server will be passed the response data with a RXRPC_RESPONDED control
 * message when it gets the first data from each call.
 *
 * Note that this is only honoured by security classes that need auxiliary data
 * (e.g. RxGK).  Those that don't offer the facility (e.g. RxKAD) respond
 * without consulting userspace.
 *
 * Return: The previous setting.
 */
int rxrpc_sock_set_manage_response(struct sock *sk, bool set)
{
	struct rxrpc_sock *rx = rxrpc_sk(sk);
	int ret;

	lock_sock(sk);
	ret = !!test_bit(RXRPC_SOCK_MANAGE_RESPONSE, &rx->flags);
	if (set)
		set_bit(RXRPC_SOCK_MANAGE_RESPONSE, &rx->flags);
	else
		clear_bit(RXRPC_SOCK_MANAGE_RESPONSE, &rx->flags);
	release_sock(sk);
	return ret;
}
EXPORT_SYMBOL(rxrpc_sock_set_manage_response);
