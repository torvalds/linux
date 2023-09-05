// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 IBM Corporation
 * Author: Nayna Jain
 *
 *      - loads keys and hashes stored and controlled by the firmware.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <asm/secure_boot.h>
#include <asm/secvar.h>
#include "keyring_handler.h"
#include "../integrity.h"

#define extract_esl(db, data, size, offset)	\
	do { db = data + offset; size = size - offset; } while (0)

/*
 * Get a certificate list blob from the named secure variable.
 *
 * Returns:
 *  - a pointer to a kmalloc'd buffer containing the cert list on success
 *  - NULL if the key does not exist
 *  - an ERR_PTR on error
 */
static __init void *get_cert_list(u8 *key, unsigned long keylen, u64 *size)
{
	int rc;
	void *db;

	rc = secvar_ops->get(key, keylen, NULL, size);
	if (rc) {
		if (rc == -ENOENT)
			return NULL;
		return ERR_PTR(rc);
	}

	db = kmalloc(*size, GFP_KERNEL);
	if (!db)
		return ERR_PTR(-ENOMEM);

	rc = secvar_ops->get(key, keylen, db, size);
	if (rc) {
		kfree(db);
		return ERR_PTR(rc);
	}

	return db;
}

/*
 * Load the certs contained in the keys databases into the platform trusted
 * keyring and the blacklisted X.509 cert SHA256 hashes into the blacklist
 * keyring.
 */
static int __init load_powerpc_certs(void)
{
	void *db = NULL, *dbx = NULL, *data = NULL;
	u64 dsize = 0;
	u64 offset = 0;
	int rc = 0;
	ssize_t len;
	char buf[32];

	if (!secvar_ops)
		return -ENODEV;

	len = secvar_ops->format(buf, sizeof(buf));
	if (len <= 0)
		return -ENODEV;

	// Check for known secure boot implementations from OPAL or PLPKS
	if (strcmp("ibm,edk2-compat-v1", buf) && strcmp("ibm,plpks-sb-v1", buf)) {
		pr_err("Unsupported secvar implementation \"%s\", not loading certs\n", buf);
		return -ENODEV;
	}

	if (strcmp("ibm,plpks-sb-v1", buf) == 0)
		/* PLPKS authenticated variables ESL data is prefixed with 8 bytes of timestamp */
		offset = 8;

	/*
	 * Get db, and dbx. They might not exist, so it isn't an error if we
	 * can't get them.
	 */
	data = get_cert_list("db", 3, &dsize);
	if (!data) {
		pr_info("Couldn't get db list from firmware\n");
	} else if (IS_ERR(data)) {
		rc = PTR_ERR(data);
		pr_err("Error reading db from firmware: %d\n", rc);
		return rc;
	} else {
		extract_esl(db, data, dsize, offset);

		rc = parse_efi_signature_list("powerpc:db", db, dsize,
					      get_handler_for_db);
		if (rc)
			pr_err("Couldn't parse db signatures: %d\n", rc);
		kfree(data);
	}

	data = get_cert_list("dbx", 4,  &dsize);
	if (!data) {
		pr_info("Couldn't get dbx list from firmware\n");
	} else if (IS_ERR(data)) {
		rc = PTR_ERR(data);
		pr_err("Error reading dbx from firmware: %d\n", rc);
		return rc;
	} else {
		extract_esl(dbx, data, dsize, offset);

		rc = parse_efi_signature_list("powerpc:dbx", dbx, dsize,
					      get_handler_for_dbx);
		if (rc)
			pr_err("Couldn't parse dbx signatures: %d\n", rc);
		kfree(data);
	}

	return rc;
}
late_initcall(load_powerpc_certs);
