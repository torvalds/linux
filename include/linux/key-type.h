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
#include <linux/errno.h>

#ifdef CONFIG_KEYS

/*
 * key under-construction record
 * - passed to the request_key actor if supplied
 */
struct key_construction {
	struct key	*key;	/* key being constructed */
	struct key	*authkey;/* authorisation for key being constructed */
};

/*
 * Pre-parsed payload, used by key add, update and instantiate.
 *
 * This struct will be cleared and data and datalen will be set with the data
 * and length parameters from the caller and quotalen will be set from
 * def_datalen from the key type.  Then if the preparse() op is provided by the
 * key type, that will be called.  Then the struct will be passed to the
 * instantiate() or the update() op.
 *
 * If the preparse() op is given, the free_preparse() op will be called to
 * clear the contents.
 */
struct key_preparsed_payload {
	char		*description;	/* Proposed key description (or NULL) */
	void		*type_data[2];	/* Private key-type data */
	void		*payload[2];	/* Proposed payload */
	const void	*data;		/* Raw data */
	size_t		datalen;	/* Raw datalen */
	size_t		quotalen;	/* Quota length for proposed payload */
	time_t		expiry;		/* Expiry time of key */
	bool		trusted;	/* True if key is trusted */
};

typedef int (*request_key_actor_t)(struct key_construction *key,
				   const char *op, void *aux);

/*
 * Preparsed matching criterion.
 */
struct key_match_data {
	/* Comparison function, defaults to exact description match, but can be
	 * overridden by type->match_preparse().  Should return true if a match
	 * is found and false if not.
	 */
	bool (*cmp)(const struct key *key,
		    const struct key_match_data *match_data);

	const void	*raw_data;	/* Raw match data */
	void		*preparsed;	/* For ->match_preparse() to stash stuff */
	unsigned	lookup_type;	/* Type of lookup for this search. */
#define KEYRING_SEARCH_LOOKUP_DIRECT	0x0000	/* Direct lookup by description. */
#define KEYRING_SEARCH_LOOKUP_ITERATE	0x0001	/* Iterative search. */
};

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

	/* Preparse the data blob from userspace that is to be the payload,
	 * generating a proposed description and payload that will be handed to
	 * the instantiate() and update() ops.
	 */
	int (*preparse)(struct key_preparsed_payload *prep);

	/* Free a preparse data structure.
	 */
	void (*free_preparse)(struct key_preparsed_payload *prep);

	/* instantiate a key of this type
	 * - this method should call key_payload_reserve() to determine if the
	 *   user's quota will hold the payload
	 */
	int (*instantiate)(struct key *key, struct key_preparsed_payload *prep);

	/* update a key of this type (optional)
	 * - this method should call key_payload_reserve() to recalculate the
	 *   quota consumption
	 * - the key must be locked against read when modifying
	 */
	int (*update)(struct key *key, struct key_preparsed_payload *prep);

	/* Preparse the data supplied to ->match() (optional).  The
	 * data to be preparsed can be found in match_data->raw_data.
	 * The lookup type can also be set by this function.
	 */
	int (*match_preparse)(struct key_match_data *match_data);

	/* Free preparsed match data (optional).  This should be supplied it
	 * ->match_preparse() is supplied. */
	void (*match_free)(struct key_match_data *match_data);

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
	struct lock_class_key	lock_class;	/* key->sem lock class */
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

extern int generic_key_instantiate(struct key *key, struct key_preparsed_payload *prep);

#endif /* CONFIG_KEYS */
#endif /* _LINUX_KEY_TYPE_H */
