// SPDX-License-Identifier: GPL-2.0-only
/*
 * Landlock LSM - Security framework setup
 *
 * Copyright © 2016-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2018-2020 ANSSI
 */

#include <linux/init.h>
#include <linux/lsm_hooks.h>
#include <uapi/linux/lsm.h>

#include "common.h"
#include "cred.h"
#include "fs.h"
#include "net.h"
#include "setup.h"
#include "task.h"

bool landlock_initialized __ro_after_init = false;

struct lsm_blob_sizes landlock_blob_sizes __ro_after_init = {
	.lbs_cred = sizeof(struct landlock_cred_security),
	.lbs_file = sizeof(struct landlock_file_security),
	.lbs_inode = sizeof(struct landlock_inode_security),
	.lbs_superblock = sizeof(struct landlock_superblock_security),
};

const struct lsm_id landlock_lsmid = {
	.name = LANDLOCK_NAME,
	.id = LSM_ID_LANDLOCK,
};

static int __init landlock_init(void)
{
	landlock_add_cred_hooks();
	landlock_add_task_hooks();
	landlock_add_fs_hooks();
	landlock_add_net_hooks();
	landlock_initialized = true;
	pr_info("Up and running.\n");
	return 0;
}

DEFINE_LSM(LANDLOCK_NAME) = {
	.name = LANDLOCK_NAME,
	.init = landlock_init,
	.blobs = &landlock_blob_sizes,
};
