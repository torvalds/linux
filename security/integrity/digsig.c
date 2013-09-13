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
#include <linux/rbtree.h>
#include <linux/key-type.h>
#include <linux/digsig.h>

#include "integrity.h"

static struct key *keyring[INTEGRITY_KEYRING_MAX];

static const char *keyring_name[INTEGRITY_KEYRING_MAX] = {
	"_evm",
	"_module",
	"_ima",
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

	switch (sig[0]) {
	case 1:
		return digsig_verify(keyring[id], sig, siglen,
				     digest, digestlen);
	case 2:
		return asymmetric_verify(keyring[id], sig, siglen,
					 digest, digestlen);
	}

	return -EOPNOTSUPP;
}
