// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Intel Corporation
 *
 * Author:
 * Dmitry Kasatkin <dmitry.kasatkin@intel.com>
 */

#include <linux/err.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/cred.h>
#include <linux/kernel_read_file.h>
#include <linux/key-type.h>
#include <linux/digsig.h>
#include <linux/vmalloc.h>
#include <crypto/public_key.h>
#include <keys/system_keyring.h>

#include "integrity.h"

static struct key *keyring[INTEGRITY_KEYRING_MAX];

static const char * const keyring_name[INTEGRITY_KEYRING_MAX] = {
#ifndef CONFIG_INTEGRITY_TRUSTED_KEYRING
	"_evm",
	"_ima",
#else
	".evm",
	".ima",
#endif
	".platform",
	".machine",
};

#ifdef CONFIG_IMA_KEYRINGS_PERMIT_SIGNED_BY_BUILTIN_OR_SECONDARY
#define restrict_link_to_ima restrict_link_by_builtin_and_secondary_trusted
#else
#define restrict_link_to_ima restrict_link_by_builtin_trusted
#endif

static struct key *integrity_keyring_from_id(const unsigned int id)
{
	if (id >= INTEGRITY_KEYRING_MAX)
		return ERR_PTR(-EINVAL);

	if (!keyring[id]) {
		keyring[id] =
			request_key(&key_type_keyring, keyring_name[id], NULL);
		if (IS_ERR(keyring[id])) {
			int err = PTR_ERR(keyring[id]);
			pr_err("no %s keyring: %d\n", keyring_name[id], err);
			keyring[id] = NULL;
			return ERR_PTR(err);
		}
	}

	return keyring[id];
}

int integrity_digsig_verify(const unsigned int id, const char *sig, int siglen,
			    const char *digest, int digestlen)
{
	struct key *keyring;

	if (siglen < 2)
		return -EINVAL;

	keyring = integrity_keyring_from_id(id);
	if (IS_ERR(keyring))
		return PTR_ERR(keyring);

	switch (sig[1]) {
	case 1:
		/* v1 API expect signature without xattr type */
		return digsig_verify(keyring, sig + 1, siglen - 1, digest,
				     digestlen);
	case 2: /* regular file data hash based signature */
	case 3: /* struct ima_file_id data based signature */
		return asymmetric_verify(keyring, sig, siglen, digest,
					 digestlen);
	}

	return -EOPNOTSUPP;
}

int integrity_modsig_verify(const unsigned int id, const struct modsig *modsig)
{
	struct key *keyring;

	keyring = integrity_keyring_from_id(id);
	if (IS_ERR(keyring))
		return PTR_ERR(keyring);

	return ima_modsig_verify(keyring, modsig);
}

static int __init __integrity_init_keyring(const unsigned int id,
					   key_perm_t perm,
					   struct key_restriction *restriction)
{
	const struct cred *cred = current_cred();
	int err = 0;

	keyring[id] = keyring_alloc(keyring_name[id], KUIDT_INIT(0),
				    KGIDT_INIT(0), cred, perm,
				    KEY_ALLOC_NOT_IN_QUOTA, restriction, NULL);
	if (IS_ERR(keyring[id])) {
		err = PTR_ERR(keyring[id]);
		pr_info("Can't allocate %s keyring (%d)\n",
			keyring_name[id], err);
		keyring[id] = NULL;
	} else {
		if (id == INTEGRITY_KEYRING_PLATFORM)
			set_platform_trusted_keys(keyring[id]);
		if (id == INTEGRITY_KEYRING_MACHINE && trust_moklist())
			set_machine_trusted_keys(keyring[id]);
		if (id == INTEGRITY_KEYRING_IMA)
			load_module_cert(keyring[id]);
	}

	return err;
}

int __init integrity_init_keyring(const unsigned int id)
{
	struct key_restriction *restriction;
	key_perm_t perm;
	int ret;

	perm = (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_VIEW
		| KEY_USR_READ | KEY_USR_SEARCH;

	if (id == INTEGRITY_KEYRING_PLATFORM ||
	    id == INTEGRITY_KEYRING_MACHINE) {
		restriction = NULL;
		goto out;
	}

	if (!IS_ENABLED(CONFIG_INTEGRITY_TRUSTED_KEYRING))
		return 0;

	restriction = kzalloc(sizeof(struct key_restriction), GFP_KERNEL);
	if (!restriction)
		return -ENOMEM;

	restriction->check = restrict_link_to_ima;

	/*
	 * MOK keys can only be added through a read-only runtime services
	 * UEFI variable during boot. No additional keys shall be allowed to
	 * load into the machine keyring following init from userspace.
	 */
	if (id != INTEGRITY_KEYRING_MACHINE)
		perm |= KEY_USR_WRITE;

out:
	ret = __integrity_init_keyring(id, perm, restriction);
	if (ret)
		kfree(restriction);
	return ret;
}

static int __init integrity_add_key(const unsigned int id, const void *data,
				    off_t size, key_perm_t perm)
{
	key_ref_t key;
	int rc = 0;

	if (!keyring[id])
		return -EINVAL;

	key = key_create_or_update(make_key_ref(keyring[id], 1), "asymmetric",
				   NULL, data, size, perm,
				   KEY_ALLOC_NOT_IN_QUOTA);
	if (IS_ERR(key)) {
		rc = PTR_ERR(key);
		pr_err("Problem loading X.509 certificate %d\n", rc);
	} else {
		pr_notice("Loaded X.509 cert '%s'\n",
			  key_ref_to_ptr(key)->description);
		key_ref_put(key);
	}

	return rc;

}

int __init integrity_load_x509(const unsigned int id, const char *path)
{
	void *data = NULL;
	size_t size;
	int rc;
	key_perm_t perm;

	rc = kernel_read_file_from_path(path, 0, &data, INT_MAX, NULL,
					READING_X509_CERTIFICATE);
	if (rc < 0) {
		pr_err("Unable to open file: %s (%d)", path, rc);
		return rc;
	}
	size = rc;

	perm = (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_VIEW | KEY_USR_READ;

	pr_info("Loading X.509 certificate: %s\n", path);
	rc = integrity_add_key(id, (const void *)data, size, perm);

	vfree(data);
	return rc;
}

int __init integrity_load_cert(const unsigned int id, const char *source,
			       const void *data, size_t len, key_perm_t perm)
{
	if (!data)
		return -EINVAL;

	pr_info("Loading X.509 certificate: %s\n", source);
	return integrity_add_key(id, data, len, perm);
}
