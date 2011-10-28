/* Definitions for key type implementations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_KEY_TYPE_H
#define _LINUX_KEY_TYPE_H

#include <linux/key.h>

#ifdef CONFIG_KEYS

/*
 * key under-construction record
 * - passed to the request_key actor if supplied
 */
struct key_construction {
	struct key	*key;	/* key being constructed */
	struct key	*authkey;/* authorisation for key being constructed */
};

typedef int (*request_key_actor_t)(struct key_construction *key,
				   const char *op, void *aux);

/*
 * kernel managed key type definition
 */
struct key_type {
	/* name of the type */
	const char *name;

	/* default payload length for quota precalculation (optional)
	 * - this can be used instead of calling key_payload_reserve(), that
	 *   function only needs to be called if the real datalen is different
	 */
	size_t def_datalen;

	/* vet a description */
	int (*vet_description)(const char *description);

	/* instantiate a key of this type
	 * - this method should call key_payload_reserve() to determine if the
	 *   user's quota will hold the payload
	 */
	int (*instantiate)(struct key *key, const void *data, size_t datalen);

	/* update a key of this type (optional)
	 * - this method should call key_payload_reserve() to recalculate the
	 *   quota consumption
	 * - the key must be locked against read when modifying
	 */
	int (*update)(struct key *key, const void *data, size_t datalen);

	/* match a key against a description */
	int (*match)(const struct key *key, const void *desc);

	/* clear some of the data from a key on revokation (optional)
	 * - the key's semaphore will be write-locked by the caller
	 */
	void (*revoke)(struct key *key);

	/* clear the data from a key (optional) */
	void (*destroy)(struct key *key);

	/* describe a key */
	void (*describe)(const struct key *key, struct seq_file *p);

	/* read a key's data (optional)
	 * - permission checks will be done by the caller
	 * - the key's semaphore will be readlocked by the caller
	 * - should return the amount of data that could be read, no matter how
	 *   much is copied into the buffer
	 * - shouldn't do the copy if the buffer is NULL
	 */
	long (*read)(const struct key *key, char __user *buffer, size_t buflen);

	/* handle request_key() for this type instead of invoking
	 * /sbin/request-key (optional)
	 * - key is the key to instantiate
	 * - authkey is the authority to assume when instantiating this key
	 * - op is the operation to be done, usually "create"
	 * - the call must not return until the instantiation process has run
	 *   its course
	 */
	request_key_actor_t request_key;

	/* internal fields */
	struct list_head	link;		/* link in types list */
};

extern struct key_type key_type_keyring;

extern int register_key_type(struct key_type *ktype);
extern void unregister_key_type(struct key_type *ktype);

extern int key_payload_reserve(struct key *key, size_t datalen);
extern int key_instantiate_and_link(struct key *key,
				    const void *data,
				    size_t datalen,
				    struct key *keyring,
				    struct key *instkey);
extern int key_reject_and_link(struct key *key,
			       unsigned timeout,
			       unsigned error,
			       struct key *keyring,
			       struct key *instkey);
extern void complete_request_key(struct key_construction *cons, int error);

static inline int key_negate_and_link(struct key *key,
				      unsigned timeout,
				      struct key *keyring,
				      struct key *instkey)
{
	return key_reject_and_link(key, timeout, ENOKEY, keyring, instkey);
}

#endif /* CONFIG_KEYS */
#endif /* _LINUX_KEY_TYPE_H */
