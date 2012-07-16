/*
 * Copyright (c) 2012 Bryan Schumaker <bjschuma@netapp.com>
 */
#include <linux/init.h>
#include <linux/nfs_idmap.h>

int __init init_nfs_v4(void)
{
	int err;

	err = nfs_idmap_init();
	if (err)
		goto out;

	return 0;
out:
	return err;
}

void __exit exit_nfs_v4(void)
{
	nfs_idmap_quit();
}
