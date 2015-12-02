/*
 * Copyright (C) 2015 Juniper Networks, Inc.
 *
 * Author:
 * Petko Manolov <petko.manolov@konsulko.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 */

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/module.h>
#include <keys/asymmetric-type.h>


struct key *ima_mok_keyring;
struct key *ima_blacklist_keyring;

/*
 * Allocate the IMA MOK and blacklist keyrings
 */
__init int ima_mok_init(void)
{
	pr_notice("Allocating IMA MOK and blacklist keyrings.\n");

	ima_mok_keyring = keyring_alloc(".ima_mok",
			      KUIDT_INIT(0), KGIDT_INIT(0), current_cred(),
			      (KEY_POS_ALL & ~KEY_POS_SETATTR) |
			      KEY_USR_VIEW | KEY_USR_READ |
			      KEY_USR_WRITE | KEY_USR_SEARCH,
			      KEY_ALLOC_NOT_IN_QUOTA, NULL);

	ima_blacklist_keyring = keyring_alloc(".ima_blacklist",
				KUIDT_INIT(0), KGIDT_INIT(0), current_cred(),
				(KEY_POS_ALL & ~KEY_POS_SETATTR) |
				KEY_USR_VIEW | KEY_USR_READ |
				KEY_USR_WRITE | KEY_USR_SEARCH,
				KEY_ALLOC_NOT_IN_QUOTA, NULL);

	if (IS_ERR(ima_mok_keyring) || IS_ERR(ima_blacklist_keyring))
		panic("Can't allocate IMA MOK or blacklist keyrings.");
	set_bit(KEY_FLAG_TRUSTED_ONLY, &ima_mok_keyring->flags);
	set_bit(KEY_FLAG_TRUSTED_ONLY, &ima_blacklist_keyring->flags);
	return 0;
}

module_init(ima_mok_init);
