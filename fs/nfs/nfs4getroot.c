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
	struct nfs_fsinfo fsinfo;
	int ret = -ENOMEM;

	dprintk("--> nfs4_get_rootfh()\n");

	fsinfo.fattr = nfs_alloc_fattr();
	if (fsinfo.fattr == NULL)
		goto out;

	/* Start by getting the root filehandle from the server */
	ret = nfs4_proc_get_rootfh(server, mntfh, &fsinfo, auth_probe);
	if (ret < 0) {
		dprintk("nfs4_get_rootfh: getroot error = %d\n", -ret);
		goto out;
	}

	if (!(fsinfo.fattr->valid & NFS_ATTR_FATTR_TYPE)
			|| !S_ISDIR(fsinfo.fattr->mode)) {
		printk(KERN_ERR "nfs4_get_rootfh:"
		       " getroot encountered non-directory\n");
		ret = -ENOTDIR;
		goto out;
	}

	if (fsinfo.fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL) {
		printk(KERN_ERR "nfs4_get_rootfh:"
		       " getroot obtained referral\n");
		ret = -EREMOTE;
		goto out;
	}

	memcpy(&server->fsid, &fsinfo.fattr->fsid, sizeof(server->fsid));
out:
	nfs_free_fattr(fsinfo.fattr);
	dprintk("<-- nfs4_get_rootfh() = %d\n", ret);
	return ret;
}
