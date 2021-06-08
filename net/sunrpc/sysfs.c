// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Anna Schumaker <Anna.Schumaker@Netapp.com>
 */
#include <linux/kobject.h>

static struct kset *rpc_sunrpc_kset;

int rpc_sysfs_init(void)
{
	rpc_sunrpc_kset = kset_create_and_add("sunrpc", NULL, kernel_kobj);
	if (!rpc_sunrpc_kset)
		return -ENOMEM;
	return 0;
}

void rpc_sysfs_exit(void)
{
	kset_unregister(rpc_sunrpc_kset);
}
