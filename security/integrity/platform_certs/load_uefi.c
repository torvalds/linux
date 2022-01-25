// SPDX-License-Identifier: GPL-2.0

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/efi.h>
#include <linux/slab.h>
#include <linux/ima.h>
#include <keys/asymmetric-type.h>
#include <keys/system_keyring.h>
#include "../integrity.h"
#include "keyring_handler.h"

/*
 * Look to see if a UEFI variable called MokIgnoreDB exists and return true if
 * it does.
 *
 * This UEFI variable is set by the shim if a user tells the shim to not use
 * the certs/hashes in the UEFI db variable for verification purposes.  If it
 * is set, we should ignore the db variable also and the true return indicates
 * this.
 */
static __init bool uefi_check_ignore_db(void)
{
	efi_status_t status;
	unsigned int db = 0;
	unsigned long size = sizeof(db);
	efi_guid_t guid = EFI_SHIM_LOCK_GUID;

	status = efi.get_variable(L"MokIgnoreDB", &guid, NULL, &size, &db);
	return status == EFI_SUCCESS;
}

/*
 * Get a certificate list blob from the named EFI variable.
 */
static __init void *get_cert_list(efi_char16_t *name, efi_guid_t *guid,
				  unsigned long *size, efi_status_t *status)
{
	unsigned long lsize = 4;
	unsigned long tmpdb[4];
	void *db;

	*status = efi.get_variable(name, guid, NULL, &lsize, &tmpdb);
	if (*status == EFI_NOT_FOUND)
		return NULL;

	if (*status != EFI_BUFFER_TOO_SMALL) {
		pr_err("Couldn't get size: 0x%lx\n", *status);
		return NULL;
	}

	db = kmalloc(lsize, GFP_KERNEL);
	if (!db)
		return NULL;

	*status = efi.get_variable(name, guid, NULL, &lsize, db);
	if (*status != EFI_SUCCESS) {
		kfree(db);
		pr_err("Error reading db var: 0x%lx\n", *status);
		return NULL;
	}

	*size = lsize;
	return db;
}

/*
 * load_moklist_certs() - Load MokList certs
 *
 * Load the certs contained in the UEFI MokListRT database into the
 * platform trusted keyring.
 *
 * This routine checks the EFI MOK config table first. If and only if
 * that fails, this routine uses the MokListRT ordinary UEFI variable.
 *
 * Return:	Status
 */
static int __init load_moklist_certs(void)
{
	struct efi_mokvar_table_entry *mokvar_entry;
	efi_guid_t mok_var = EFI_SHIM_LOCK_GUID;
	void *mok;
	unsigned long moksize;
	efi_status_t status;
	int rc;

	/* First try to load certs from the EFI MOKvar config table.
	 * It's not an error if the MOKvar config table doesn't exist
	 * or the MokListRT entry is not found in it.
	 */
	mokvar_entry = efi_mokvar_entry_find("MokListRT");
	if (mokvar_entry) {
		rc = parse_efi_signature_list("UEFI:MokListRT (MOKvar table)",
					      mokvar_entry->data,
					      mokvar_entry->data_size,
					      get_handler_for_db);
		/* All done if that worked. */
		if (!rc)
			return rc;

		pr_err("Couldn't parse MokListRT signatures from EFI MOKvar config table: %d\n",
		       rc);
	}

	/* Get MokListRT. It might not exist, so it isn't an error
	 * if we can't get it.
	 */
	mok = get_cert_list(L"MokListRT", &mok_var, &moksize, &status);
	if (mok) {
		rc = parse_efi_signature_list("UEFI:MokListRT",
					      mok, moksize, get_handler_for_db);
		kfree(mok);
		if (rc)
			pr_err("Couldn't parse MokListRT signatures: %d\n", rc);
		return rc;
	}
	if (status == EFI_NOT_FOUND)
		pr_debug("MokListRT variable wasn't found\n");
	else
		pr_info("Couldn't get UEFI MokListRT\n");
	return 0;
}

/*
 * load_uefi_certs() - Load certs from UEFI sources
 *
 * Load the certs contained in the UEFI databases into the platform trusted
 * keyring and the UEFI blacklisted X.509 cert SHA256 hashes into the blacklist
 * keyring.
 */
static int __init load_uefi_certs(void)
{
	efi_guid_t secure_var = EFI_IMAGE_SECURITY_DATABASE_GUID;
	efi_guid_t mok_var = EFI_SHIM_LOCK_GUID;
	void *db = NULL, *dbx = NULL, *mokx = NULL;
	unsigned long dbsize = 0, dbxsize = 0, mokxsize = 0;
	efi_status_t status;
	int rc = 0;

	if (!efi_rt_services_supported(EFI_RT_SUPPORTED_GET_VARIABLE))
		return false;

	/* Get db and dbx.  They might not exist, so it isn't an error
	 * if we can't get them.
	 */
	if (!uefi_check_ignore_db()) {
		db = get_cert_list(L"db", &secure_var, &dbsize, &status);
		if (!db) {
			if (status == EFI_NOT_FOUND)
				pr_debug("MODSIGN: db variable wasn't found\n");
			else
				pr_err("MODSIGN: Couldn't get UEFI db list\n");
		} else {
			rc = parse_efi_signature_list("UEFI:db",
					db, dbsize, get_handler_for_db);
			if (rc)
				pr_err("Couldn't parse db signatures: %d\n",
				       rc);
			kfree(db);
		}
	}

	dbx = get_cert_list(L"dbx", &secure_var, &dbxsize, &status);
	if (!dbx) {
		if (status == EFI_NOT_FOUND)
			pr_debug("dbx variable wasn't found\n");
		else
			pr_info("Couldn't get UEFI dbx list\n");
	} else {
		rc = parse_efi_signature_list("UEFI:dbx",
					      dbx, dbxsize,
					      get_handler_for_dbx);
		if (rc)
			pr_err("Couldn't parse dbx signatures: %d\n", rc);
		kfree(dbx);
	}

	/* the MOK/MOKx can not be trusted when secure boot is disabled */
	if (!arch_ima_get_secureboot())
		return 0;

	mokx = get_cert_list(L"MokListXRT", &mok_var, &mokxsize, &status);
	if (!mokx) {
		if (status == EFI_NOT_FOUND)
			pr_debug("mokx variable wasn't found\n");
		else
			pr_info("Couldn't get mokx list\n");
	} else {
		rc = parse_efi_signature_list("UEFI:MokListXRT",
					      mokx, mokxsize,
					      get_handler_for_dbx);
		if (rc)
			pr_err("Couldn't parse mokx signatures %d\n", rc);
		kfree(mokx);
	}

	/* Load the MokListRT certs */
	rc = load_moklist_certs();

	return rc;
}
late_initcall(load_uefi_certs);
