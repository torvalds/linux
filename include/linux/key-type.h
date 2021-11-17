/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Definitions for key type implementations
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#ifndef _LINUX_KEY_TYPE_H
#define _LINUX_KEY_TYPE_H

#include <linux/key.h>
#include <linux/errno.h>

#ifdef CONFIG_KEYS

struct kernel_pkey_query;
struct kernel_pkey_params;

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
	const char	*orig_description; /* Actual or proposed description (maybe NULL) */
	char		*description;	/* Proposed key description (or NULL) */
	union key_payload payload;	/* Proposed payload */
	const void	*data;		/* Raw data */
	size_t		datalen;	/* Raw datalen */
	size_t		quotalen;	/* Quota length for proposed payload */
	time64_t	expiry;		/* Expiry time of key */
} __randomize_layout;

typedef int (*request_key_actor_t)(struct key *auth_key, void *aux);

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

	unsigned int flags;
#define KEY_TYPE_NET_DOMAIN	0x00000001 /* Keys of this type have a net namespace domain */

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
	long (*read)(const struct key *key, char *buffer, size_t buflen);

	/* handle request_key() for this type instead of invoking
	 * /sbin/request-key (optional)
	 * - key is the key to instantiate
	 * - authkey is the authority to assume when instantiating this key
	 * - op is the operation to be done, usually "create"
	 * - the call must not return until the instantiation process has run
	 *   its course
	 */
	request_key_actor_t request_key;

	/* Look up a keyring access restriction (optional)
	 *
	 * - NULL is a valid return value (meaning the requested restriction
	 *   is known but will never block addition of a key)
	 * - should return -EINVAL if the restriction is unknown
	 */
	struct key_restriction *(*lookup_restriction)(const char *params);

	/* Asymmetric key accessor functions. */
	int (*asym_query)(const struct kernel_pkey_params *params,
			  struct kernel_pkey_query *info);
	int (*asym_eds_op)(struct kernel_pkey_params *params,
			   const void *in, void *out);
	int (*asym_verify_signature)(struct kernel_pkey_params *params,
				     const void *in, const void *in2);

	/* internal fields */
	struct list_head	link;		/* link in types list */
	struct lock_class_key	lock_class;	/* key->sem lock class */
} __randomize_layout;

extern struct key_type key_type_keyring;

extern int register_key_type(struct key_type *ktype);
extern void unregister_key_type(struct key_type *ktype);

extern int key_payload_reserve(struct key *key, size_t datalen);
extern int key_instantiate_and_link(struct key *key,
				    const void *data,
				    size_t datalen,
				    struct key *keyring,
				    struct key *authkey);
extern int key_reject_and_link(struct key *key,
			       unsigned timeout,
			       unsigned error,
			       struct key *keyring,
			       struct key *authkey);
extern void complete_request_key(struct key *authkey, int error);

static inline int key_negate_and_link(struct key *key,
				      unsigned timeout,
				      struct key *keyring,
				      struct key *authkey)
{
	return key_reject_and_link(key, timeout, ENOKEY, keyring, authkey);
}

extern int generic_key_instantiate(struct key *key, struct key_preparsed_payload *prep);

#endif /* CONFIG_KEYS */
#endif /* _LINUX_KEY_TYPE_H */
