// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Security framework setup
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#include <linux/init.h>
#include <linux/lsm_hooks.h>

#include "common.h"
#include "cred.h"
#include "setup.h"

struct lsm_blob_sizes landlock_blob_sizes __lsm_ro_after_init = {
	.lbs_cred = sizeof(struct landlock_cred_security),
};

static int __init landlock_init(void)
{
	landlock_add_cred_hooks();
	pr_info("Up and running.\n");
	return 0;
}

DEFINE_LSM(LANDLOCK_NAME) = {
	.name = LANDLOCK_NAME,
	.init = landlock_init,
	.blobs = &landlock_blob_sizes,
};
