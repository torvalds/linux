/*
 * Copyright (C) 2011 Intel Corporation
 *
 * Author:
 * Dmitry Kasatkin <dmitry.kasatkin@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/sched.h>
#include <linux/rbtree.h>
#include <linux/cred.h>
#include <linux/key-type.h>
#include <linux/digsig.h>

#include "integrity.h"

static struct key *keyring[INTEGRITY_KEYRING_MAX];

#ifdef CONFIG_IMA_TRUSTED_KEYRING
static const char *keyring_name[INTEGRITY_KEYRING_MAX] = {
	".evm",
	".module",
	".ima",
};
#else
static const char *keyring_name[INTEGRITY_KEYRING_MAX] = {
	"_evm",
	"_module",
	"_ima",
};
#endif

int integrity_digsig_verify(const unsigned int id, const char *sig, int siglen,
			    const char *digest, int digestlen)
{
	if (id >= INTEGRITY_KEYRING_MAX)
		return -EINVAL;

	if (!keyring[id]) {
		keyring[id] =
		    request_key(&key_type_keyring, keyring_name[id], NULL);
		if (IS_ERR(keyring[id])) {
			int err = PTR_ERR(keyring[id]);
			pr_err("no %s keyring: %d\n", keyring_name[id], err);
			keyring[id] = NULL;
			return err;
		}
	}

	switch (sig[1]) {
	case 1:
		/* v1 API expect signature without xattr type */
		return digsig_verify(keyring[id], sig + 1, siglen - 1,
				     digest, digestlen);
	case 2:
		return asymmetric_verify(keyring[id], sig, siglen,
					 digest, digestlen);
	}

	return -EOPNOTSUPP;
}

int integrity_init_keyring(const unsigned int id)
{
	const struct cred *cred = current_cred();
	const struct user_struct *user = cred->user;

	keyring[id] = keyring_alloc(keyring_name[id], KUIDT_INIT(0),
				    KGIDT_INIT(0), cred,
				    ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
				     KEY_USR_VIEW | KEY_USR_READ),
				    KEY_ALLOC_NOT_IN_QUOTA, user->uid_keyring);
	if (!IS_ERR(keyring[id]))
		set_bit(KEY_FLAG_TRUSTED_ONLY, &keyring[id]->flags);
	else
		pr_info("Can't allocate %s keyring (%ld)\n",
			keyring_name[id], PTR_ERR(keyring[id]));
	return 0;
}
