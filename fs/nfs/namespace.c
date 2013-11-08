/*
 * linux/fs/nfs/namespace.c
 *
 * Copyright (C) 2005 Trond Myklebust <Trond.Myklebust@netapp.com>
 * - Modified by David Howells <dhowells@redhat.com>
 *
 * NFS namespace
 */

#include <linux/dcache.h>
#include <linux/gfp.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/nfs_fs.h>
#include <linux/string.h>
#include <linux/sunrpc/clnt.h>
#include <linux/vfs.h>
#include <linux/sunrpc/gss_api.h>
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

static void nfs_expire_automounts(struct work_struct *work);

static LIST_HEAD(nfs_automount_list);
static DECLARE_DELAYED_WORK(nfs_automount_task, nfs_expire_automounts);
int nfs_mountpoint_expiry_timeout = 500 * HZ;

static struct vfsmount *nfs_do_submount(struct dentry *dentry,
					struct nfs_fh *fh,
					struct nfs_fattr *fattr,
					rpc_authflavor_t authflavor);

/*
 * nfs_path - reconstruct the path given an arbitrary dentry
 * @base - used to return pointer to the end of devname part of path
 * @dentry - pointer to dentry
 * @buffer - result buffer
 * @buflen - length of buffer
 *
 * Helper function for constructing the server pathname
 * by arbitrary hashed dentry.
 *
 * This is mainly for use in figuring out the path on the
 * server side when automounting on top of an existing partition
 * and in generating /proc/mounts and friends.
 */
char *nfs_path(char **p, struct dentry *dentry, char *buffer, ssize_t buflen)
{
	char *end;
	int namelen;
	unsigned seq;
	const char *base;

rename_retry:
	end = buffer+buflen;
	*--end = '\0';
	buflen--;

	seq = read_seqbegin(&rename_lock);
	rcu_read_lock();
	while (1) {
		spin_lock(&dentry->d_lock);
		if (IS_ROOT(dentry))
			break;
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0)
			goto Elong_unlock;
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		spin_unlock(&dentry->d_lock);
		dentry = dentry->d_parent;
	}
	if (read_seqretry(&rename_lock, seq)) {
		spin_unlock(&dentry->d_lock);
		rcu_read_unlock();
		goto rename_retry;
	}
	if (*end != '/') {
		if (--buflen < 0) {
			spin_unlock(&dentry->d_lock);
			rcu_read_unlock();
			goto Elong;
		}
		*--end = '/';
	}
	*p = end;
	base = dentry->d_fsdata;
	if (!base) {
		spin_unlock(&dentry->d_lock);
		rcu_read_unlock();
		WARN_ON(1);
		return end;
	}
	namelen = strlen(base);
	/* Strip off excess slashes in base string */
	while (namelen > 0 && base[namelen - 1] == '/')
		namelen--;
	buflen -= namelen;
	if (buflen < 0) {
		spin_unlock(&dentry->d_lock);
		rcu_read_unlock();
		goto Elong;
	}
	end -= namelen;
	memcpy(end, base, namelen);
	spin_unlock(&dentry->d_lock);
	rcu_read_unlock();
	return end;
Elong_unlock:
	spin_unlock(&dentry->d_lock);
	rcu_read_unlock();
	if (read_seqretry(&rename_lock, seq))
		goto rename_retry;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}

#ifdef CONFIG_NFS_V4
static rpc_authflavor_t nfs_find_best_sec(struct nfs4_secinfo_flavors *flavors)
{
	struct gss_api_mech *mech;
	struct xdr_netobj oid;
	int i;
	rpc_authflavor_t pseudoflavor = RPC_AUTH_UNIX;

	for (i = 0; i < flavors->num_flavors; i++) {
		struct nfs4_secinfo_flavor *flavor;
		flavor = &flavors->flavors[i];

		if (flavor->flavor == RPC_AUTH_NULL || flavor->flavor == RPC_AUTH_UNIX) {
			pseudoflavor = flavor->flavor;
			break;
		} else if (flavor->flavor == RPC_AUTH_GSS) {
			oid.len  = flavor->gss.sec_oid4.len;
			oid.data = flavor->gss.sec_oid4.data;
			mech = gss_mech_get_by_OID(&oid);
			if (!mech)
				continue;
			pseudoflavor = gss_svc_to_pseudoflavor(mech, flavor->gss.service);
			gss_mech_put(mech);
			break;
		}
	}

	return pseudoflavor;
}

static int nfs_negotiate_security(const struct dentry *parent,
				  const struct dentry *dentry,
				  rpc_authflavor_t *flavor)
{
	struct page *page;
	struct nfs4_secinfo_flavors *flavors;
	int (*secinfo)(struct inode *, const struct qstr *, struct nfs4_secinfo_flavors *);
	int ret = -EPERM;

	secinfo = NFS_PROTO(parent->d_inode)->secinfo;
	if (secinfo != NULL) {
		page = alloc_page(GFP_KERNEL);
		if (!page) {
			ret = -ENOMEM;
			goto out;
		}
		flavors = page_address(page);
		ret = secinfo(parent->d_inode, &dentry->d_name, flavors);
		*flavor = nfs_find_best_sec(flavors);
		put_page(page);
	}

out:
	return ret;
}

static int nfs_lookup_with_sec(struct nfs_server *server, struct dentry *parent,
			       struct dentry *dentry, struct path *path,
			       struct nfs_fh *fh, struct nfs_fattr *fattr,
			       rpc_authflavor_t *flavor)
{
	struct rpc_clnt *clone;
	struct rpc_auth *auth;
	int err;

	err = nfs_negotiate_security(parent, path->dentry, flavor);
	if (err < 0)
		goto out;
	clone  = rpc_clone_client(server->client);
	auth   = rpcauth_create(*flavor, clone);
	if (!auth) {
		err = -EIO;
		goto out_shutdown;
	}
	err = server->nfs_client->rpc_ops->lookup(clone, parent->d_inode,
						  &path->dentry->d_name,
						  fh, fattr);
out_shutdown:
	rpc_shutdown_client(clone);
out:
	return err;
}
#else /* CONFIG_NFS_V4 */
static inline int nfs_lookup_with_sec(struct nfs_server *server,
				      struct dentry *parent, struct dentry *dentry,
				      struct path *path, struct nfs_fh *fh,
				      struct nfs_fattr *fattr,
				      rpc_authflavor_t *flavor)
{
	return -EPERM;
}
#endif /* CONFIG_NFS_V4 */

/*
 * nfs_d_automount - Handle crossing a mountpoint on the server
 * @path - The mountpoint
 *
 * When we encounter a mountpoint on the server, we want to set up
 * a mountpoint on the client too, to prevent inode numbers from
 * colliding, and to allow "df" to work properly.
 * On NFSv4, we also want to allow for the fact that different
 * filesystems may be migrated to different servers in a failover
 * situation, and that different filesystems may want to use
 * different security flavours.
 */
struct vfsmount *nfs_d_automount(struct path *path)
{
	struct vfsmount *mnt;
	struct nfs_server *server = NFS_SERVER(path->dentry->d_inode);
	struct dentry *parent;
	struct nfs_fh *fh = NULL;
	struct nfs_fattr *fattr = NULL;
	int err;
	rpc_authflavor_t flavor = RPC_AUTH_UNIX;

	dprintk("--> nfs_d_automount()\n");

	mnt = ERR_PTR(-ESTALE);
	if (IS_ROOT(path->dentry))
		goto out_nofree;

	mnt = ERR_PTR(-ENOMEM);
	fh = nfs_alloc_fhandle();
	fattr = nfs_alloc_fattr();
	if (fh == NULL || fattr == NULL)
		goto out;

	dprintk("%s: enter\n", __func__);

	/* Look it up again to get its attributes */
	parent = dget_parent(path->dentry);
	err = server->nfs_client->rpc_ops->lookup(server->client, parent->d_inode,
						  &path->dentry->d_name,
						  fh, fattr);
	if (err == -EPERM && NFS_PROTO(parent->d_inode)->secinfo != NULL)
		err = nfs_lookup_with_sec(server, parent, path->dentry, path, fh, fattr, &flavor);
	dput(parent);
	if (err != 0) {
		mnt = ERR_PTR(err);
		goto out;
	}

	if (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL)
		mnt = nfs_do_refmount(path->dentry);
	else
		mnt = nfs_do_submount(path->dentry, fh, fattr, flavor);
	if (IS_ERR(mnt))
		goto out;

	dprintk("%s: done, success\n", __func__);
	mntget(mnt); /* prevent immediate expiration */
	mnt_set_expiry(mnt, &nfs_automount_list);
	schedule_delayed_work(&nfs_automount_task, nfs_mountpoint_expiry_timeout);

out:
	nfs_free_fattr(fattr);
	nfs_free_fhandle(fh);
out_nofree:
	dprintk("<-- nfs_follow_mountpoint() = %p\n", mnt);
	return mnt;
}

const struct inode_operations nfs_mountpoint_inode_operations = {
	.getattr	= nfs_getattr,
};

const struct inode_operations nfs_referral_inode_operations = {
};

static void nfs_expire_automounts(struct work_struct *work)
{
	struct list_head *list = &nfs_automount_list;

	mark_mounts_for_expiry(list);
	if (!list_empty(list))
		schedule_delayed_work(&nfs_automount_task, nfs_mountpoint_expiry_timeout);
}

void nfs_release_automount_timer(void)
{
	if (list_empty(&nfs_automount_list))
		cancel_delayed_work(&nfs_automount_task);
}

/*
 * Clone a mountpoint of the appropriate type
 */
static struct vfsmount *nfs_do_clone_mount(struct nfs_server *server,
					   const char *devname,
					   struct nfs_clone_mount *mountdata)
{
#ifdef CONFIG_NFS_V4
	struct vfsmount *mnt = ERR_PTR(-EINVAL);
	switch (server->nfs_client->rpc_ops->version) {
		case 2:
		case 3:
			mnt = vfs_kern_mount(&nfs_xdev_fs_type, 0, devname, mountdata);
			break;
		case 4:
			mnt = vfs_kern_mount(&nfs4_xdev_fs_type, 0, devname, mountdata);
	}
	return mnt;
#else
	return vfs_kern_mount(&nfs_xdev_fs_type, 0, devname, mountdata);
#endif
}

/**
 * nfs_do_submount - set up mountpoint when crossing a filesystem boundary
 * @dentry - parent directory
 * @fh - filehandle for new root dentry
 * @fattr - attributes for new root inode
 * @authflavor - security flavor to use when performing the mount
 *
 */
static struct vfsmount *nfs_do_submount(struct dentry *dentry,
					struct nfs_fh *fh,
					struct nfs_fattr *fattr,
					rpc_authflavor_t authflavor)
{
	struct nfs_clone_mount mountdata = {
		.sb = dentry->d_sb,
		.dentry = dentry,
		.fh = fh,
		.fattr = fattr,
		.authflavor = authflavor,
	};
	struct vfsmount *mnt = ERR_PTR(-ENOMEM);
	char *page = (char *) __get_free_page(GFP_USER);
	char *devname;

	dprintk("--> nfs_do_submount()\n");

	dprintk("%s: submounting on %s/%s\n", __func__,
			dentry->d_parent->d_name.name,
			dentry->d_name.name);
	if (page == NULL)
		goto out;
	devname = nfs_devname(dentry, page, PAGE_SIZE);
	mnt = (struct vfsmount *)devname;
	if (IS_ERR(devname))
		goto free_page;
	mnt = nfs_do_clone_mount(NFS_SB(dentry->d_sb), devname, &mountdata);
free_page:
	free_page((unsigned long)page);
out:
	dprintk("%s: done\n", __func__);

	dprintk("<-- nfs_do_submount() = %p\n", mnt);
	return mnt;
}
