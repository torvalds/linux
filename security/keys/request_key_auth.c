/* request_key_auth.c: request key authorisation controlling key def
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * See Documentation/keys-request-key.txt
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/seq_file.h>
#include "internal.h"

static int request_key_auth_instantiate(struct key *, const void *, size_t);
static void request_key_auth_describe(const struct key *, struct seq_file *);
static void request_key_auth_destroy(struct key *);

/*
 * the request-key authorisation key type definition
 */
struct key_type key_type_request_key_auth = {
	.name		= ".request_key_auth",
	.def_datalen	= sizeof(struct request_key_auth),
	.instantiate	= request_key_auth_instantiate,
	.describe	= request_key_auth_describe,
	.destroy	= request_key_auth_destroy,
};

/*****************************************************************************/
/*
 * instantiate a request-key authorisation record
 */
static int request_key_auth_instantiate(struct key *key,
					const void *data,
					size_t datalen)
{
	struct request_key_auth *rka, *irka;
	struct key *instkey;
	int ret;

	ret = -ENOMEM;
	rka = kmalloc(sizeof(*rka), GFP_KERNEL);
	if (rka) {
		/* see if the calling process is already servicing the key
		 * request of another process */
		instkey = key_get_instantiation_authkey(0);
		if (!IS_ERR(instkey)) {
			/* it is - use that instantiation context here too */
			irka = instkey->payload.data;
			rka->context = irka->context;
			rka->pid = irka->pid;
			key_put(instkey);
		}
		else {
			/* it isn't - use this process as the context */
			rka->context = current;
			rka->pid = current->pid;
		}

		rka->target_key = key_get((struct key *) data);
		key->payload.data = rka;
		ret = 0;
	}

	return ret;

} /* end request_key_auth_instantiate() */

/*****************************************************************************/
/*
 *
 */
static void request_key_auth_describe(const struct key *key,
				      struct seq_file *m)
{
	struct request_key_auth *rka = key->payload.data;

	seq_puts(m, "key:");
	seq_puts(m, key->description);
	seq_printf(m, " pid:%d", rka->pid);

} /* end request_key_auth_describe() */

/*****************************************************************************/
/*
 * destroy an instantiation authorisation token key
 */
static void request_key_auth_destroy(struct key *key)
{
	struct request_key_auth *rka = key->payload.data;

	kenter("{%d}", key->serial);

	key_put(rka->target_key);
	kfree(rka);

} /* end request_key_auth_destroy() */

/*****************************************************************************/
/*
 * create a session keyring to be for the invokation of /sbin/request-key and
 * stick an authorisation token in it
 */
struct key *request_key_auth_new(struct key *target, struct key **_rkakey)
{
	struct key *keyring, *rkakey = NULL;
	char desc[20];
	int ret;

	kenter("%d,", target->serial);

	/* allocate a new session keyring */
	sprintf(desc, "_req.%u", target->serial);

	keyring = keyring_alloc(desc, current->fsuid, current->fsgid, 1, NULL);
	if (IS_ERR(keyring)) {
		kleave("= %ld", PTR_ERR(keyring));
		return keyring;
	}

	/* allocate the auth key */
	sprintf(desc, "%x", target->serial);

	rkakey = key_alloc(&key_type_request_key_auth, desc,
			   current->fsuid, current->fsgid,
			   KEY_POS_VIEW | KEY_USR_VIEW, 1);
	if (IS_ERR(rkakey)) {
		key_put(keyring);
		kleave("= %ld", PTR_ERR(rkakey));
		return rkakey;
	}

	/* construct and attach to the keyring */
	ret = key_instantiate_and_link(rkakey, target, 0, keyring, NULL);
	if (ret < 0) {
		key_revoke(rkakey);
		key_put(rkakey);
		key_put(keyring);
		kleave("= %d", ret);
		return ERR_PTR(ret);
	}

	*_rkakey = rkakey;
	kleave(" = {%d} ({%d})", keyring->serial, rkakey->serial);
	return keyring;

} /* end request_key_auth_new() */

/*****************************************************************************/
/*
 * get the authorisation key for instantiation of a specific key if attached to
 * the current process's keyrings
 * - this key is inserted into a keyring and that is set as /sbin/request-key's
 *   session keyring
 * - a target_id of zero specifies any valid token
 */
struct key *key_get_instantiation_authkey(key_serial_t target_id)
{
	struct task_struct *tsk = current;
	struct key *instkey;

	/* we must have our own personal session keyring */
	if (!tsk->signal->session_keyring)
		return ERR_PTR(-EACCES);

	/* and it must contain a suitable request authorisation key
	 * - lock RCU against session keyring changing
	 */
	rcu_read_lock();

	instkey = keyring_search_instkey(
		rcu_dereference(tsk->signal->session_keyring), target_id);

	rcu_read_unlock();
	return instkey;

} /* end key_get_instantiation_authkey() */
