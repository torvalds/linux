// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 Isovalent */

#include <linux/bpf.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/key.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "bpf."

static struct key *bpf_keyring;

static bool bpf_keyring_unsealed __ro_after_init;
module_param_named(keyring_unsealed, bpf_keyring_unsealed, bool, 0444);
MODULE_PARM_DESC(keyring_unsealed, "Leave the bpf keyring unsealed");

bool bpf_keyring_enforced(void)
{
	return bpf_keyring_unsealed;
}

struct bpf_key *bpf_lookup_keyring(void)
{
	struct bpf_key *bkey;

	if (!bpf_keyring)
		return NULL;
	if (!READ_ONCE(bpf_keyring->keys.nr_leaves_on_tree) ||
	    !READ_ONCE(bpf_keyring->restrict_link))
		return NULL;

	bkey = kmalloc_obj(*bkey);
	if (!bkey)
		return NULL;

	bkey->key = bpf_keyring;
	bkey->has_ref = false;
	return bkey;
}

static int __init bpf_keyring_init(void)
{
	struct key *keyring;

	keyring = keyring_alloc(".bpf",
				GLOBAL_ROOT_UID, GLOBAL_ROOT_GID,
				current_cred(), KEY_POS_SEARCH |
				KEY_USR_VIEW | KEY_USR_READ |
				KEY_USR_WRITE | KEY_USR_SEARCH |
				KEY_USR_SETATTR, KEY_ALLOC_NOT_IN_QUOTA,
				NULL, NULL);
	if (IS_ERR(keyring)) {
		pr_err("bpf: cannot allocate bpf keyring: %ld\n",
		       PTR_ERR(keyring));
		return 0;
	}
	if (!bpf_keyring_unsealed &&
	    keyring_restrict(make_key_ref(keyring, true), NULL, NULL)) {
		pr_err("bpf: cannot seal bpf keyring\n");
		key_revoke(keyring);
		key_put(keyring);
		return 0;
	}

	bpf_keyring = keyring;
	key_register_bpf_keyring(keyring);
	return 0;
}
late_initcall(bpf_keyring_init);
