// SPDX-License-Identifier: GPL-2.0
/*
 * Machine keyring routines.
 *
 * Copyright (c) 2021, Oracle and/or its affiliates.
 */

#include <linux/efi.h>
#include "../integrity.h"

static __init int machine_keyring_init(void)
{
	int rc;

	rc = integrity_init_keyring(INTEGRITY_KEYRING_MACHINE);
	if (rc)
		return rc;

	pr_notice("Machine keyring initialized\n");
	return 0;
}
device_initcall(machine_keyring_init);

void __init add_to_machine_keyring(const char *source, const void *data, size_t len)
{
	key_perm_t perm;
	int rc;

	perm = (KEY_POS_ALL & ~KEY_POS_SETATTR) | KEY_USR_VIEW;
	rc = integrity_load_cert(INTEGRITY_KEYRING_MACHINE, source, data, len, perm);

	/*
	 * Some MOKList keys may not pass the machine keyring restrictions.
	 * If the restriction check does not pass and the platform keyring
	 * is configured, try to add it into that keyring instead.
	 */
	if (rc && IS_ENABLED(CONFIG_INTEGRITY_PLATFORM_KEYRING))
		rc = integrity_load_cert(INTEGRITY_KEYRING_PLATFORM, source,
					 data, len, perm);

	if (rc)
		pr_info("Error adding keys to machine keyring %s\n", source);
}

/*
 * Try to load the MokListTrustedRT MOK variable to see if we should trust
 * the MOK keys within the kernel. It is not an error if this variable
 * does not exist.  If it does not exist, MOK keys should not be trusted
 * within the machine keyring.
 */
static __init bool uefi_check_trust_mok_keys(void)
{
	struct efi_mokvar_table_entry *mokvar_entry;

	mokvar_entry = efi_mokvar_entry_find("MokListTrustedRT");

	if (mokvar_entry)
		return true;

	return false;
}
