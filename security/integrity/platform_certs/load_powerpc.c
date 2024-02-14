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
#include <linux/of.h>
#include <asm/secure_boot.h>
#include <asm/secvar.h>
#include "keyring_handler.h"

/*
 * Get a certificate list blob from the named secure variable.
 */
static __init void *get_cert_list(u8 *key, unsigned long keylen, uint64_t *size)
{
	int rc;
	void *db;

	rc = secvar_ops->get(key, keylen, NULL, size);
	if (rc) {
		pr_err("Couldn't get size: %d\n", rc);
		return NULL;
	}

	db = kmalloc(*size, GFP_KERNEL);
	if (!db)
		return NULL;

	rc = secvar_ops->get(key, keylen, db, size);
	if (rc) {
		kfree(db);
		pr_err("Error reading %s var: %d\n", key, rc);
		return NULL;
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
	void *db = NULL, *dbx = NULL;
	uint64_t dbsize = 0, dbxsize = 0;
	int rc = 0;
	struct device_node *node;

	if (!secvar_ops)
		return -ENODEV;

	/* The following only applies for the edk2-compat backend. */
	node = of_find_compatible_node(NULL, NULL, "ibm,edk2-compat-v1");
	if (!node)
		return -ENODEV;

	/*
	 * Get db, and dbx. They might not exist, so it isn't an error if we
	 * can't get them.
	 */
	db = get_cert_list("db", 3, &dbsize);
	if (!db) {
		pr_err("Couldn't get db list from firmware\n");
	} else {
		rc = parse_efi_signature_list("powerpc:db", db, dbsize,
					      get_handler_for_db);
		if (rc)
			pr_err("Couldn't parse db signatures: %d\n", rc);
		kfree(db);
	}

	dbx = get_cert_list("dbx", 4,  &dbxsize);
	if (!dbx) {
		pr_info("Couldn't get dbx list from firmware\n");
	} else {
		rc = parse_efi_signature_list("powerpc:dbx", dbx, dbxsize,
					      get_handler_for_dbx);
		if (rc)
			pr_err("Couldn't parse dbx signatures: %d\n", rc);
		kfree(dbx);
	}

	of_node_put(node);

	return rc;
}
late_initcall(load_powerpc_certs);
