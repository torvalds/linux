// SPDX-License-Identifier: GPL-2.0+
/*
 * Platform keyring for firmware/platform keys
 *
 * Copyright IBM Corporation, 2018
 * Author(s): Nayna Jain <nayna@linux.ibm.com>
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "../integrity.h"

/*
 * Create the trusted keyrings.
 */
static __init int platform_keyring_init(void)
{
	int rc;

	rc = integrity_init_keyring(INTEGRITY_KEYRING_PLATFORM);
	if (rc)
		return rc;

	pr_notice("Platform Keyring initialized\n");
	return 0;
}

/*
 * Must be initialised before we try and load the keys into the keyring.
 */
device_initcall(platform_keyring_init);
