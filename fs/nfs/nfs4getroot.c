// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (C) 2006 Red Hat, Inc. All Rights Reserved.
* Written by David Howells (dhowells@redhat.com)
*/

#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_CLIENT

int nfs4_get_rootfh(struct nfs_server *server, struct nfs_fh *mntfh, bool auth_probe)
{
	struct nfs_fattr *fattr = nfs_alloc_fattr();
	int ret = -ENOMEM;

	if (fattr == NULL)
		goto out;

	/* Start by getting the root filehandle from the server */
	ret = nfs4_proc_get_rootfh(server, mntfh, fattr, auth_probe);
	if (ret < 0) {
		dprintk("nfs4_get_rootfh: getroot error = %d\n", -ret);
		goto out;
	}

	if (!(fattr->valid & NFS_ATTR_FATTR_TYPE) || !S_ISDIR(fattr->mode)) {
		printk(KERN_ERR "nfs4_get_rootfh:"
		       " getroot encountered non-directory\n");
		ret = -ENOTDIR;
		goto out;
	}

	memcpy(&server->fsid, &fattr->fsid, sizeof(server->fsid));
out:
	nfs_free_fattr(fattr);
	return ret;
}
