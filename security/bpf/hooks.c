// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2020 Google LLC.
 */
#include <linux/lsm_hooks.h>
#include <linux/bpf_lsm.h>

static struct security_hook_list bpf_lsm_hooks[] __lsm_ro_after_init = {
	#define LSM_HOOK(RET, DEFAULT, NAME, ...) \
	LSM_HOOK_INIT(NAME, bpf_lsm_##NAME),
	#include <linux/lsm_hook_defs.h>
	#undef LSM_HOOK
	LSM_HOOK_INIT(inode_free_security, bpf_inode_storage_free),
};

static int __init bpf_lsm_init(void)
{
	security_add_hooks(bpf_lsm_hooks, ARRAY_SIZE(bpf_lsm_hooks), "bpf");
	pr_info("LSM support for eBPF active\n");
	return 0;
}

struct lsm_blob_sizes bpf_lsm_blob_sizes __lsm_ro_after_init = {
	.lbs_inode = sizeof(struct bpf_storage_blob),
};

DEFINE_LSM(bpf) = {
	.name = "bpf",
	.init = bpf_lsm_init,
	.blobs = &bpf_lsm_blob_sizes
};
