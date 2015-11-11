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
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/key-type.h>
#include <linux/digsig.h>

#include "integrity.h"

static struct key *keyring[INTEGRITY_KEYRING_MAX];

static const char *keyring_name[INTEGRITY_KEYRING_MAX] = {
	"_evm",
	"_module",
#ifndef CONFIG_IMA_TRUSTED_KEYRING
	"_ima",
#else
	".ima",
#endif
};

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

int __init integrity_init_keyring(const unsigned int id)
{
	const struct cred *cred = current_cred();
	int err = 0;

	keyring[id] = keyring_alloc(keyring_name[id], KUIDT_INIT(0),
				    KGIDT_INIT(0), cred,
				    ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
				     KEY_USR_VIEW | KEY_USR_READ |
				     KEY_USR_WRITE | KEY_USR_SEARCH),
				    KEY_ALLOC_NOT_IN_QUOTA, NULL);
	if (!IS_ERR(keyring[id]))
		set_bit(KEY_FLAG_TRUSTED_ONLY, &keyring[id]->flags);
	else {
		err = PTR_ERR(keyring[id]);
		pr_info("Can't allocate %s keyring (%d)\n",
			keyring_name[id], err);
		keyring[id] = NULL;
	}
	return err;
}

int __init integrity_load_x509(const unsigned int id, const char *path)
{
	key_ref_t key;
	char *data;
	int rc;

	if (!keyring[id])
		return -EINVAL;

	rc = integrity_read_file(path, &data);
	if (rc < 0)
		return rc;

	key = key_create_or_update(make_key_ref(keyring[id], 1),
				   "asymmetric",
				   NULL,
				   data,
				   rc,
				   ((KEY_POS_ALL & ~KEY_POS_SETATTR) |
				    KEY_USR_VIEW | KEY_USR_READ),
				   KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(key)) {
		rc = PTR_ERR(key);
		pr_err("Problem loading X.509 certificate (%d): %s\n",
		       rc, path);
	} else {
		pr_notice("Loaded X.509 cert '%s': %s\n",
			  key_ref_to_ptr(key)->description, path);
		key_ref_put(key);
	}
	kfree(data);
	return 0;
}
