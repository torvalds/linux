// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2024 Microsoft Corporation. All rights reserved.
 */
#include <uapi/linux/lsm.h>

#include "ipe.h"
#include "hooks.h"

static struct lsm_blob_sizes ipe_blobs __ro_after_init = {
};

static const struct lsm_id ipe_lsmid = {
	.name = "ipe",
	.id = LSM_ID_IPE,
};

static struct security_hook_list ipe_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(bprm_check_security, ipe_bprm_check_security),
	LSM_HOOK_INIT(mmap_file, ipe_mmap_file),
	LSM_HOOK_INIT(file_mprotect, ipe_file_mprotect),
	LSM_HOOK_INIT(kernel_read_file, ipe_kernel_read_file),
	LSM_HOOK_INIT(kernel_load_data, ipe_kernel_load_data),
};

/**
 * ipe_init() - Entry point of IPE.
 *
 * This is called at LSM init, which happens occurs early during kernel
 * start up. During this phase, IPE registers its hooks and loads the
 * builtin boot policy.
 *
 * Return:
 * * %0		- OK
 * * %-ENOMEM	- Out of memory (OOM)
 */
static int __init ipe_init(void)
{
	security_add_hooks(ipe_hooks, ARRAY_SIZE(ipe_hooks), &ipe_lsmid);

	return 0;
}

DEFINE_LSM(ipe) = {
	.name = "ipe",
	.init = ipe_init,
	.blobs = &ipe_blobs,
};
