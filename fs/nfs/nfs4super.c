/*
 * Copyright (c) 2012 Bryan Schumaker <bjschuma@netapp.com>
 */
#include <linux/init.h>
#include <linux/nfs_idmap.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"

int __init init_nfs_v4(void)
{
	int err;

	err = nfs_idmap_init();
	if (err)
		goto out;

	err = nfs4_register_sysctl();
	if (err)
		goto out1;

	return 0;
out1:
	nfs_idmap_quit();
out:
	return err;
}

void __exit exit_nfs_v4(void)
{
	nfs4_unregister_sysctl();
	nfs_idmap_quit();
}
