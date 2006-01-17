/*
 * linux/fs/nfs/callback_proc.c
 *
 * Copyright (C) 2004 Trond Myklebust
 *
 * NFSv4 callback procedures
 */
#include <linux/config.h>
#include <linux/nfs4.h>
#include <linux/nfs_fs.h>
#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"

#define NFSDBG_FACILITY NFSDBG_CALLBACK
 
unsigned nfs4_callback_getattr(struct cb_getattrargs *args, struct cb_getattrres *res)
{
	struct nfs4_client *clp;
	struct nfs_delegation *delegation;
	struct nfs_inode *nfsi;
	struct inode *inode;
	
	res->bitmap[0] = res->bitmap[1] = 0;
	res->status = htonl(NFS4ERR_BADHANDLE);
	clp = nfs4_find_client(&args->addr->sin_addr);
	if (clp == NULL)
		goto out;
	inode = nfs_delegation_find_inode(clp, &args->fh);
	if (inode == NULL)
		goto out_putclient;
	nfsi = NFS_I(inode);
	down_read(&nfsi->rwsem);
	delegation = nfsi->delegation;
	if (delegation == NULL || (delegation->type & FMODE_WRITE) == 0)
		goto out_iput;
	res->size = i_size_read(inode);
	res->change_attr = delegation->change_attr;
	if (nfsi->npages != 0)
		res->change_attr++;
	res->ctime = inode->i_ctime;
	res->mtime = inode->i_mtime;
	res->bitmap[0] = (FATTR4_WORD0_CHANGE|FATTR4_WORD0_SIZE) &
		args->bitmap[0];
	res->bitmap[1] = (FATTR4_WORD1_TIME_METADATA|FATTR4_WORD1_TIME_MODIFY) &
		args->bitmap[1];
	res->status = 0;
out_iput:
	up_read(&nfsi->rwsem);
	iput(inode);
out_putclient:
	nfs4_put_client(clp);
out:
	dprintk("%s: exit with status = %d\n", __FUNCTION__, ntohl(res->status));
	return res->status;
}

unsigned nfs4_callback_recall(struct cb_recallargs *args, void *dummy)
{
	struct nfs4_client *clp;
	struct inode *inode;
	unsigned res;
	
	res = htonl(NFS4ERR_BADHANDLE);
	clp = nfs4_find_client(&args->addr->sin_addr);
	if (clp == NULL)
		goto out;
	inode = nfs_delegation_find_inode(clp, &args->fh);
	if (inode == NULL)
		goto out_putclient;
	/* Set up a helper thread to actually return the delegation */
	switch(nfs_async_inode_return_delegation(inode, &args->stateid)) {
		case 0:
			res = 0;
			break;
		case -ENOENT:
			res = htonl(NFS4ERR_BAD_STATEID);
			break;
		default:
			res = htonl(NFS4ERR_RESOURCE);
	}
	iput(inode);
out_putclient:
	nfs4_put_client(clp);
out:
	dprintk("%s: exit with status = %d\n", __FUNCTION__, ntohl(res));
	return res;
}
