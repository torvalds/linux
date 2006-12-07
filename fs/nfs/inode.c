/*
 *  linux/fs/nfs/inode.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs inode and superblock handling functions
 *
 *  Modularised by Alan Cox <Alan.Cox@linux.org>, while hacking some
 *  experimental NFS changes. Modularisation taken straight from SYS5 fs.
 *
 *  Change to nfs_read_super() to permit NFS mounts to multi-homed hosts.
 *  J.S.Peatfield@damtp.cam.ac.uk
 *
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/stats.h>
#include <linux/sunrpc/metrics.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/lockd/bind.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/nfs_xdr.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"

#define NFSDBG_FACILITY		NFSDBG_VFS
#define NFS_PARANOIA 1

static void nfs_invalidate_inode(struct inode *);
static int nfs_update_inode(struct inode *, struct nfs_fattr *);

static void nfs_zap_acl_cache(struct inode *);

static struct kmem_cache * nfs_inode_cachep;

static inline unsigned long
nfs_fattr_to_ino_t(struct nfs_fattr *fattr)
{
	return nfs_fileid_to_ino_t(fattr->fileid);
}

int nfs_write_inode(struct inode *inode, int sync)
{
	int flags = sync ? FLUSH_SYNC : 0;
	int ret;

	ret = nfs_commit_inode(inode, flags);
	if (ret < 0)
		return ret;
	return 0;
}

void nfs_clear_inode(struct inode *inode)
{
	/*
	 * The following should never happen...
	 */
	BUG_ON(nfs_have_writebacks(inode));
	BUG_ON(!list_empty(&NFS_I(inode)->open_files));
	BUG_ON(atomic_read(&NFS_I(inode)->data_updates) != 0);
	nfs_zap_acl_cache(inode);
	nfs_access_zap_cache(inode);
}

/**
 * nfs_sync_mapping - helper to flush all mmapped dirty data to disk
 */
int nfs_sync_mapping(struct address_space *mapping)
{
	int ret;

	if (mapping->nrpages == 0)
		return 0;
	unmap_mapping_range(mapping, 0, 0, 0);
	ret = filemap_write_and_wait(mapping);
	if (ret != 0)
		goto out;
	ret = nfs_wb_all(mapping->host);
out:
	return ret;
}

/*
 * Invalidate the local caches
 */
static void nfs_zap_caches_locked(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int mode = inode->i_mode;

	nfs_inc_stats(inode, NFSIOS_ATTRINVALIDATE);

	NFS_ATTRTIMEO(inode) = NFS_MINATTRTIMEO(inode);
	NFS_ATTRTIMEO_UPDATE(inode) = jiffies;

	memset(NFS_COOKIEVERF(inode), 0, sizeof(NFS_COOKIEVERF(inode)));
	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode))
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL|NFS_INO_REVAL_PAGECACHE;
	else
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL|NFS_INO_REVAL_PAGECACHE;
}

void nfs_zap_caches(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	nfs_zap_caches_locked(inode);
	spin_unlock(&inode->i_lock);
}

void nfs_zap_mapping(struct inode *inode, struct address_space *mapping)
{
	if (mapping->nrpages != 0) {
		spin_lock(&inode->i_lock);
		NFS_I(inode)->cache_validity |= NFS_INO_INVALID_DATA;
		spin_unlock(&inode->i_lock);
	}
}

static void nfs_zap_acl_cache(struct inode *inode)
{
	void (*clear_acl_cache)(struct inode *);

	clear_acl_cache = NFS_PROTO(inode)->clear_acl_cache;
	if (clear_acl_cache != NULL)
		clear_acl_cache(inode);
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_ACL;
	spin_unlock(&inode->i_lock);
}

/*
 * Invalidate, but do not unhash, the inode.
 * NB: must be called with inode->i_lock held!
 */
static void nfs_invalidate_inode(struct inode *inode)
{
	set_bit(NFS_INO_STALE, &NFS_FLAGS(inode));
	nfs_zap_caches_locked(inode);
}

struct nfs_find_desc {
	struct nfs_fh		*fh;
	struct nfs_fattr	*fattr;
};

/*
 * In NFSv3 we can have 64bit inode numbers. In order to support
 * this, and re-exported directories (also seen in NFSv2)
 * we are forced to allow 2 different inodes to have the same
 * i_ino.
 */
static int
nfs_find_actor(struct inode *inode, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fh		*fh = desc->fh;
	struct nfs_fattr	*fattr = desc->fattr;

	if (NFS_FILEID(inode) != fattr->fileid)
		return 0;
	if (nfs_compare_fh(NFS_FH(inode), fh))
		return 0;
	if (is_bad_inode(inode) || NFS_STALE(inode))
		return 0;
	return 1;
}

static int
nfs_init_locked(struct inode *inode, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fattr	*fattr = desc->fattr;

	NFS_FILEID(inode) = fattr->fileid;
	nfs_copy_fh(NFS_FH(inode), desc->fh);
	return 0;
}

/* Don't use READDIRPLUS on directories that we believe are too large */
#define NFS_LIMIT_READDIRPLUS (8*PAGE_SIZE)

/*
 * This is our front-end to iget that looks up inodes by file handle
 * instead of inode number.
 */
struct inode *
nfs_fhget(struct super_block *sb, struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr
	};
	struct inode *inode = ERR_PTR(-ENOENT);
	unsigned long hash;

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		goto out_no_inode;

	if (!fattr->nlink) {
		printk("NFS: Buggy server - nlink == 0!\n");
		goto out_no_inode;
	}

	hash = nfs_fattr_to_ino_t(fattr);

	inode = iget5_locked(sb, hash, nfs_find_actor, nfs_init_locked, &desc);
	if (inode == NULL) {
		inode = ERR_PTR(-ENOMEM);
		goto out_no_inode;
	}

	if (inode->i_state & I_NEW) {
		struct nfs_inode *nfsi = NFS_I(inode);

		/* We set i_ino for the few things that still rely on it,
		 * such as stat(2) */
		inode->i_ino = hash;

		/* We can't support update_atime(), since the server will reset it */
		inode->i_flags |= S_NOATIME|S_NOCMTIME;
		inode->i_mode = fattr->mode;
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_inode_operations.
		 */
		inode->i_op = NFS_SB(sb)->nfs_client->rpc_ops->file_inode_ops;
		if (S_ISREG(inode->i_mode)) {
			inode->i_fop = &nfs_file_operations;
			inode->i_data.a_ops = &nfs_file_aops;
			inode->i_data.backing_dev_info = &NFS_SB(sb)->backing_dev_info;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op = NFS_SB(sb)->nfs_client->rpc_ops->dir_inode_ops;
			inode->i_fop = &nfs_dir_operations;
			if (nfs_server_capable(inode, NFS_CAP_READDIRPLUS)
			    && fattr->size <= NFS_LIMIT_READDIRPLUS)
				set_bit(NFS_INO_ADVISE_RDPLUS, &NFS_FLAGS(inode));
			/* Deal with crossing mountpoints */
			if (!nfs_fsid_equal(&NFS_SB(sb)->fsid, &fattr->fsid)) {
				if (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL)
					inode->i_op = &nfs_referral_inode_operations;
				else
					inode->i_op = &nfs_mountpoint_inode_operations;
				inode->i_fop = NULL;
			}
		} else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else
			init_special_inode(inode, inode->i_mode, fattr->rdev);

		nfsi->read_cache_jiffies = fattr->time_start;
		nfsi->last_updated = jiffies;
		inode->i_atime = fattr->atime;
		inode->i_mtime = fattr->mtime;
		inode->i_ctime = fattr->ctime;
		if (fattr->valid & NFS_ATTR_FATTR_V4)
			nfsi->change_attr = fattr->change_attr;
		inode->i_size = nfs_size_to_loff_t(fattr->size);
		inode->i_nlink = fattr->nlink;
		inode->i_uid = fattr->uid;
		inode->i_gid = fattr->gid;
		if (fattr->valid & (NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4)) {
			/*
			 * report the blocks in 512byte units
			 */
			inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		} else {
			inode->i_blocks = fattr->du.nfs2.blocks;
		}
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
		nfsi->access_cache = RB_ROOT;

		unlock_new_inode(inode);
	} else
		nfs_refresh_inode(inode, fattr);
	dprintk("NFS: nfs_fhget(%s/%Ld ct=%d)\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode),
		atomic_read(&inode->i_count));

out:
	return inode;

out_no_inode:
	dprintk("nfs_fhget: iget failed with error %ld\n", PTR_ERR(inode));
	goto out;
}

#define NFS_VALID_ATTRS (ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_SIZE|ATTR_ATIME|ATTR_ATIME_SET|ATTR_MTIME|ATTR_MTIME_SET)

int
nfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct nfs_fattr fattr;
	int error;

	nfs_inc_stats(inode, NFSIOS_VFSSETATTR);

	if (attr->ia_valid & ATTR_SIZE) {
		if (!S_ISREG(inode->i_mode) || attr->ia_size == i_size_read(inode))
			attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Optimization: if the end result is no change, don't RPC */
	attr->ia_valid &= NFS_VALID_ATTRS;
	if (attr->ia_valid == 0)
		return 0;

	lock_kernel();
	nfs_begin_data_update(inode);
	/* Write all dirty data */
	filemap_write_and_wait(inode->i_mapping);
	nfs_wb_all(inode);
	/*
	 * Return any delegations if we're going to change ACLs
	 */
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0)
		nfs_inode_return_delegation(inode);
	error = NFS_PROTO(inode)->setattr(dentry, &fattr, attr);
	if (error == 0)
		nfs_refresh_inode(inode, &fattr);
	nfs_end_data_update(inode);
	unlock_kernel();
	return error;
}

/**
 * nfs_setattr_update_inode - Update inode metadata after a setattr call.
 * @inode: pointer to struct inode
 * @attr: pointer to struct iattr
 *
 * Note: we do this in the *proc.c in order to ensure that
 *       it works for things like exclusive creates too.
 */
void nfs_setattr_update_inode(struct inode *inode, struct iattr *attr)
{
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0) {
		if ((attr->ia_valid & ATTR_MODE) != 0) {
			int mode = attr->ia_mode & S_IALLUGO;
			mode |= inode->i_mode & ~S_IALLUGO;
			inode->i_mode = mode;
		}
		if ((attr->ia_valid & ATTR_UID) != 0)
			inode->i_uid = attr->ia_uid;
		if ((attr->ia_valid & ATTR_GID) != 0)
			inode->i_gid = attr->ia_gid;
		spin_lock(&inode->i_lock);
		NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		spin_unlock(&inode->i_lock);
	}
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		nfs_inc_stats(inode, NFSIOS_SETATTRTRUNC);
		inode->i_size = attr->ia_size;
		vmtruncate(inode, attr->ia_size);
	}
}

static int nfs_wait_schedule(void *word)
{
	if (signal_pending(current))
		return -ERESTARTSYS;
	schedule();
	return 0;
}

/*
 * Wait for the inode to get unlocked.
 */
static int nfs_wait_on_inode(struct inode *inode)
{
	struct rpc_clnt	*clnt = NFS_CLIENT(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	sigset_t oldmask;
	int error;

	rpc_clnt_sigmask(clnt, &oldmask);
	error = wait_on_bit_lock(&nfsi->flags, NFS_INO_REVALIDATING,
					nfs_wait_schedule, TASK_INTERRUPTIBLE);
	rpc_clnt_sigunmask(clnt, &oldmask);

	return error;
}

static void nfs_wake_up_inode(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	clear_bit(NFS_INO_REVALIDATING, &nfsi->flags);
	smp_mb__after_clear_bit();
	wake_up_bit(&nfsi->flags, NFS_INO_REVALIDATING);
}

int nfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int need_atime = NFS_I(inode)->cache_validity & NFS_INO_INVALID_ATIME;
	int err;

	/* Flush out writes to the server in order to update c/mtime */
	nfs_sync_inode_wait(inode, 0, 0, FLUSH_NOCOMMIT);

	/*
	 * We may force a getattr if the user cares about atime.
	 *
	 * Note that we only have to check the vfsmount flags here:
	 *  - NFS always sets S_NOATIME by so checking it would give a
	 *    bogus result
	 *  - NFS never sets MS_NOATIME or MS_NODIRATIME so there is
	 *    no point in checking those.
	 */
 	if ((mnt->mnt_flags & MNT_NOATIME) ||
 	    ((mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(inode->i_mode)))
		need_atime = 0;

	if (need_atime)
		err = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
	else
		err = nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (!err)
		generic_fillattr(inode, stat);
	return err;
}

static struct nfs_open_context *alloc_nfs_open_context(struct vfsmount *mnt, struct dentry *dentry, struct rpc_cred *cred)
{
	struct nfs_open_context *ctx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx != NULL) {
		atomic_set(&ctx->count, 1);
		ctx->dentry = dget(dentry);
		ctx->vfsmnt = mntget(mnt);
		ctx->cred = get_rpccred(cred);
		ctx->state = NULL;
		ctx->lockowner = current->files;
		ctx->error = 0;
		ctx->dir_cookie = 0;
	}
	return ctx;
}

struct nfs_open_context *get_nfs_open_context(struct nfs_open_context *ctx)
{
	if (ctx != NULL)
		atomic_inc(&ctx->count);
	return ctx;
}

void put_nfs_open_context(struct nfs_open_context *ctx)
{
	if (atomic_dec_and_test(&ctx->count)) {
		if (!list_empty(&ctx->list)) {
			struct inode *inode = ctx->dentry->d_inode;
			spin_lock(&inode->i_lock);
			list_del(&ctx->list);
			spin_unlock(&inode->i_lock);
		}
		if (ctx->state != NULL)
			nfs4_close_state(ctx->state, ctx->mode);
		if (ctx->cred != NULL)
			put_rpccred(ctx->cred);
		dput(ctx->dentry);
		mntput(ctx->vfsmnt);
		kfree(ctx);
	}
}

/*
 * Ensure that mmap has a recent RPC credential for use when writing out
 * shared pages
 */
static void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	filp->private_data = get_nfs_open_context(ctx);
	spin_lock(&inode->i_lock);
	list_add(&ctx->list, &nfsi->open_files);
	spin_unlock(&inode->i_lock);
}

/*
 * Given an inode, search for an open context with the desired characteristics
 */
struct nfs_open_context *nfs_find_open_context(struct inode *inode, struct rpc_cred *cred, int mode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *pos, *ctx = NULL;

	spin_lock(&inode->i_lock);
	list_for_each_entry(pos, &nfsi->open_files, list) {
		if (cred != NULL && pos->cred != cred)
			continue;
		if ((pos->mode & mode) == mode) {
			ctx = get_nfs_open_context(pos);
			break;
		}
	}
	spin_unlock(&inode->i_lock);
	return ctx;
}

static void nfs_file_clear_open_context(struct file *filp)
{
	struct inode *inode = filp->f_dentry->d_inode;
	struct nfs_open_context *ctx = (struct nfs_open_context *)filp->private_data;

	if (ctx) {
		filp->private_data = NULL;
		spin_lock(&inode->i_lock);
		list_move_tail(&ctx->list, &NFS_I(inode)->open_files);
		spin_unlock(&inode->i_lock);
		put_nfs_open_context(ctx);
	}
}

/*
 * These allocate and release file read/write context information.
 */
int nfs_open(struct inode *inode, struct file *filp)
{
	struct nfs_open_context *ctx;
	struct rpc_cred *cred;

	cred = rpcauth_lookupcred(NFS_CLIENT(inode)->cl_auth, 0);
	if (IS_ERR(cred))
		return PTR_ERR(cred);
	ctx = alloc_nfs_open_context(filp->f_vfsmnt, filp->f_dentry, cred);
	put_rpccred(cred);
	if (ctx == NULL)
		return -ENOMEM;
	ctx->mode = filp->f_mode;
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	return 0;
}

int nfs_release(struct inode *inode, struct file *filp)
{
	nfs_file_clear_open_context(filp);
	return 0;
}

/*
 * This function is called whenever some part of NFS notices that
 * the cached attributes have to be refreshed.
 */
int
__nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	int		 status = -ESTALE;
	struct nfs_fattr fattr;
	struct nfs_inode *nfsi = NFS_I(inode);

	dfprintk(PAGECACHE, "NFS: revalidating (%s/%Ld)\n",
		inode->i_sb->s_id, (long long)NFS_FILEID(inode));

	nfs_inc_stats(inode, NFSIOS_INODEREVALIDATE);
	lock_kernel();
	if (is_bad_inode(inode))
 		goto out_nowait;
	if (NFS_STALE(inode))
 		goto out_nowait;

	status = nfs_wait_on_inode(inode);
	if (status < 0)
		goto out;
	if (NFS_STALE(inode)) {
		status = -ESTALE;
		/* Do we trust the cached ESTALE? */
		if (NFS_ATTRTIMEO(inode) != 0) {
			if (nfsi->cache_validity & (NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ATIME)) {
				/* no */
			} else
				goto out;
		}
	}

	status = NFS_PROTO(inode)->getattr(server, NFS_FH(inode), &fattr);
	if (status != 0) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) getattr failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		if (status == -ESTALE) {
			nfs_zap_caches(inode);
			if (!S_ISDIR(inode->i_mode))
				set_bit(NFS_INO_STALE, &NFS_FLAGS(inode));
		}
		goto out;
	}

	spin_lock(&inode->i_lock);
	status = nfs_update_inode(inode, &fattr);
	if (status) {
		spin_unlock(&inode->i_lock);
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) refresh failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		goto out;
	}
	spin_unlock(&inode->i_lock);

	if (nfsi->cache_validity & NFS_INO_INVALID_ACL)
		nfs_zap_acl_cache(inode);

	dfprintk(PAGECACHE, "NFS: (%s/%Ld) revalidation complete\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode));

 out:
	nfs_wake_up_inode(inode);

 out_nowait:
	unlock_kernel();
	return status;
}

int nfs_attribute_timeout(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if (nfs_have_delegation(inode, FMODE_READ))
		return 0;
	return time_after(jiffies, nfsi->read_cache_jiffies+nfsi->attrtimeo);
}

/**
 * nfs_revalidate_inode - Revalidate the inode attributes
 * @server - pointer to nfs_server struct
 * @inode - pointer to inode struct
 *
 * Updates inode attribute information by retrieving the data from the server.
 */
int nfs_revalidate_inode(struct nfs_server *server, struct inode *inode)
{
	if (!(NFS_I(inode)->cache_validity & NFS_INO_INVALID_ATTR)
			&& !nfs_attribute_timeout(inode))
		return NFS_STALE(inode) ? -ESTALE : 0;
	return __nfs_revalidate_inode(server, inode);
}

/**
 * nfs_revalidate_mapping - Revalidate the pagecache
 * @inode - pointer to host inode
 * @mapping - pointer to mapping
 */
int nfs_revalidate_mapping(struct inode *inode, struct address_space *mapping)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int ret = 0;

	if (NFS_STALE(inode))
		ret = -ESTALE;
	if ((nfsi->cache_validity & NFS_INO_REVAL_PAGECACHE)
			|| nfs_attribute_timeout(inode))
		ret = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
	if (ret < 0)
		goto out;

	if (nfsi->cache_validity & NFS_INO_INVALID_DATA) {
		if (mapping->nrpages != 0) {
			if (S_ISREG(inode->i_mode)) {
				ret = nfs_sync_mapping(mapping);
				if (ret < 0)
					goto out;
			}
			ret = invalidate_inode_pages2(mapping);
			if (ret < 0)
				goto out;
		}
		spin_lock(&inode->i_lock);
		nfsi->cache_validity &= ~NFS_INO_INVALID_DATA;
		if (S_ISDIR(inode->i_mode)) {
			memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
			/* This ensures we revalidate child dentries */
			nfsi->cache_change_attribute = jiffies;
		}
		spin_unlock(&inode->i_lock);

		nfs_inc_stats(inode, NFSIOS_DATAINVALIDATE);
		dfprintk(PAGECACHE, "NFS: (%s/%Ld) data cache invalidated\n",
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode));
	}
out:
	return ret;
}

/**
 * nfs_begin_data_update
 * @inode - pointer to inode
 * Declare that a set of operations will update file data on the server
 */
void nfs_begin_data_update(struct inode *inode)
{
	atomic_inc(&NFS_I(inode)->data_updates);
}

/**
 * nfs_end_data_update
 * @inode - pointer to inode
 * Declare end of the operations that will update file data
 * This will mark the inode as immediately needing revalidation
 * of its attribute cache.
 */
void nfs_end_data_update(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	/* Directories: invalidate page cache */
	if (S_ISDIR(inode->i_mode)) {
		spin_lock(&inode->i_lock);
		nfsi->cache_validity |= NFS_INO_INVALID_DATA;
		spin_unlock(&inode->i_lock);
	}
	nfsi->cache_change_attribute = jiffies;
	atomic_dec(&nfsi->data_updates);
}

static void nfs_wcc_update_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	/* If we have atomic WCC data, we may update some attributes */
	if ((fattr->valid & NFS_ATTR_WCC) != 0) {
		if (timespec_equal(&inode->i_ctime, &fattr->pre_ctime)) {
			memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));
			nfsi->cache_change_attribute = jiffies;
		}
		if (timespec_equal(&inode->i_mtime, &fattr->pre_mtime)) {
			memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
			nfsi->cache_change_attribute = jiffies;
		}
		if (inode->i_size == fattr->pre_size && nfsi->npages == 0) {
			inode->i_size = fattr->size;
			nfsi->cache_change_attribute = jiffies;
		}
	}
}

/**
 * nfs_check_inode_attributes - verify consistency of the inode attribute cache
 * @inode - pointer to inode
 * @fattr - updated attributes
 *
 * Verifies the attribute cache. If we have just changed the attributes,
 * so that fattr carries weak cache consistency data, then it may
 * also update the ctime/mtime/change_attribute.
 */
static int nfs_check_inode_attributes(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t cur_size, new_isize;
	int data_unstable;


	/* Has the inode gone and changed behind our back? */
	if (nfsi->fileid != fattr->fileid
			|| (inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT)) {
		return -EIO;
	}

	/* Are we in the process of updating data on the server? */
	data_unstable = nfs_caches_unstable(inode);

	/* Do atomic weak cache consistency updates */
	nfs_wcc_update_inode(inode, fattr);

	if ((fattr->valid & NFS_ATTR_FATTR_V4) != 0 &&
			nfsi->change_attr != fattr->change_attr)
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;

	/* Verify a few of the more important attributes */
	if (!timespec_equal(&inode->i_mtime, &fattr->mtime))
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;

	cur_size = i_size_read(inode);
 	new_isize = nfs_size_to_loff_t(fattr->size);
	if (cur_size != new_isize && nfsi->npages == 0)
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;

	/* Have any file permissions changed? */
	if ((inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO)
			|| inode->i_uid != fattr->uid
			|| inode->i_gid != fattr->gid)
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;

	/* Has the link count changed? */
	if (inode->i_nlink != fattr->nlink)
		nfsi->cache_validity |= NFS_INO_INVALID_ATTR;

	if (!timespec_equal(&inode->i_atime, &fattr->atime))
		nfsi->cache_validity |= NFS_INO_INVALID_ATIME;

	nfsi->read_cache_jiffies = fattr->time_start;
	return 0;
}

/**
 * nfs_refresh_inode - try to update the inode attribute cache
 * @inode - pointer to inode
 * @fattr - updated attributes
 *
 * Check that an RPC call that returned attributes has not overlapped with
 * other recent updates of the inode metadata, then decide whether it is
 * safe to do a full update of the inode attributes, or whether just to
 * call nfs_check_inode_attributes.
 */
int nfs_refresh_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int status;

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	spin_lock(&inode->i_lock);
	if (time_after(fattr->time_start, nfsi->last_updated))
		status = nfs_update_inode(inode, fattr);
	else
		status = nfs_check_inode_attributes(inode, fattr);

	spin_unlock(&inode->i_lock);
	return status;
}

/**
 * nfs_post_op_update_inode - try to update the inode attribute cache
 * @inode - pointer to inode
 * @fattr - updated attributes
 *
 * After an operation that has changed the inode metadata, mark the
 * attribute cache as being invalid, then try to update it.
 *
 * NB: if the server didn't return any post op attributes, this
 * function will force the retrieval of attributes before the next
 * NFS request.  Thus it should be used only for operations that
 * are expected to change one or more attributes, to avoid
 * unnecessary NFS requests and trips through nfs_update_inode().
 */
int nfs_post_op_update_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int status = 0;

	spin_lock(&inode->i_lock);
	if (unlikely((fattr->valid & NFS_ATTR_FATTR) == 0)) {
		nfsi->cache_validity |= NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;
		goto out;
	}
	status = nfs_update_inode(inode, fattr);
out:
	spin_unlock(&inode->i_lock);
	return status;
}

/*
 * Many nfs protocol calls return the new file attributes after
 * an operation.  Here we update the inode to reflect the state
 * of the server's inode.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_inode_pages.
 * To make sure no other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
static int nfs_update_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_server *server;
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t cur_isize, new_isize;
	unsigned int	invalid = 0;
	int data_stable;

	dfprintk(VFS, "NFS: %s(%s/%ld ct=%d info=0x%x)\n",
			__FUNCTION__, inode->i_sb->s_id, inode->i_ino,
			atomic_read(&inode->i_count), fattr->valid);

	if (nfsi->fileid != fattr->fileid)
		goto out_fileid;

	/*
	 * Make sure the inode's type hasn't changed.
	 */
	if ((inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		goto out_changed;

	server = NFS_SERVER(inode);
	/* Update the fsid if and only if this is the root directory */
	if (inode == inode->i_sb->s_root->d_inode
			&& !nfs_fsid_equal(&server->fsid, &fattr->fsid))
		server->fsid = fattr->fsid;

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	nfsi->read_cache_jiffies = fattr->time_start;
	nfsi->last_updated = jiffies;

	/* Are we racing with known updates of the metadata on the server? */
	data_stable = nfs_verify_change_attribute(inode, fattr->time_start);
	if (data_stable)
		nfsi->cache_validity &= ~(NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE|NFS_INO_INVALID_ATIME);

	/* Do atomic weak cache consistency updates */
	nfs_wcc_update_inode(inode, fattr);

	/* Check if our cached file size is stale */
 	new_isize = nfs_size_to_loff_t(fattr->size);
	cur_isize = i_size_read(inode);
	if (new_isize != cur_isize) {
		/* Do we perhaps have any outstanding writes? */
		if (nfsi->npages == 0) {
			/* No, but did we race with nfs_end_data_update()? */
			if (data_stable) {
				inode->i_size = new_isize;
				invalid |= NFS_INO_INVALID_DATA;
			}
			invalid |= NFS_INO_INVALID_ATTR;
		} else if (new_isize > cur_isize) {
			inode->i_size = new_isize;
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
		}
		nfsi->cache_change_attribute = jiffies;
		dprintk("NFS: isize change on server for file %s/%ld\n",
				inode->i_sb->s_id, inode->i_ino);
	}

	/* Check if the mtime agrees */
	if (!timespec_equal(&inode->i_mtime, &fattr->mtime)) {
		memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
		dprintk("NFS: mtime change on server for file %s/%ld\n",
				inode->i_sb->s_id, inode->i_ino);
		invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
		nfsi->cache_change_attribute = jiffies;
	}

	/* If ctime has changed we should definitely clear access+acl caches */
	if (!timespec_equal(&inode->i_ctime, &fattr->ctime)) {
		invalid |= NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));
		nfsi->cache_change_attribute = jiffies;
	}
	memcpy(&inode->i_atime, &fattr->atime, sizeof(inode->i_atime));

	if ((inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO) ||
	    inode->i_uid != fattr->uid ||
	    inode->i_gid != fattr->gid)
		invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;

	inode->i_mode = fattr->mode;
	inode->i_nlink = fattr->nlink;
	inode->i_uid = fattr->uid;
	inode->i_gid = fattr->gid;

	if (fattr->valid & (NFS_ATTR_FATTR_V3 | NFS_ATTR_FATTR_V4)) {
		/*
		 * report the blocks in 512byte units
		 */
		inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
 	} else {
 		inode->i_blocks = fattr->du.nfs2.blocks;
 	}

	if ((fattr->valid & NFS_ATTR_FATTR_V4) != 0 &&
			nfsi->change_attr != fattr->change_attr) {
		dprintk("NFS: change_attr change on server for file %s/%ld\n",
				inode->i_sb->s_id, inode->i_ino);
		nfsi->change_attr = fattr->change_attr;
		invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		nfsi->cache_change_attribute = jiffies;
	}

	/* Update attrtimeo value if we're out of the unstable period */
	if (invalid & NFS_INO_INVALID_ATTR) {
		nfs_inc_stats(inode, NFSIOS_ATTRINVALIDATE);
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
	} else if (time_after(jiffies, nfsi->attrtimeo_timestamp+nfsi->attrtimeo)) {
		if ((nfsi->attrtimeo <<= 1) > NFS_MAXATTRTIMEO(inode))
			nfsi->attrtimeo = NFS_MAXATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = jiffies;
	}
	/* Don't invalidate the data if we were to blame */
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
				|| S_ISLNK(inode->i_mode)))
		invalid &= ~NFS_INO_INVALID_DATA;
	if (data_stable)
		invalid &= ~(NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ATIME|NFS_INO_REVAL_PAGECACHE);
	if (!nfs_have_delegation(inode, FMODE_READ))
		nfsi->cache_validity |= invalid;

	return 0;
 out_changed:
	/*
	 * Big trouble! The inode has become a different object.
	 */
#ifdef NFS_PARANOIA
	printk(KERN_DEBUG "%s: inode %ld mode changed, %07o to %07o\n",
			__FUNCTION__, inode->i_ino, inode->i_mode, fattr->mode);
#endif
 out_err:
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 * (But we fall through to invalidate the caches.)
	 */
	nfs_invalidate_inode(inode);
	return -ESTALE;

 out_fileid:
	printk(KERN_ERR "NFS: server %s error: fileid changed\n"
		"fsid %s: expected fileid 0x%Lx, got 0x%Lx\n",
		NFS_SERVER(inode)->nfs_client->cl_hostname, inode->i_sb->s_id,
		(long long)nfsi->fileid, (long long)fattr->fileid);
	goto out_err;
}


#ifdef CONFIG_NFS_V4

/*
 * Clean out any remaining NFSv4 state that might be left over due
 * to open() calls that passed nfs_atomic_lookup, but failed to call
 * nfs_open().
 */
void nfs4_clear_inode(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	/* If we are holding a delegation, return it! */
	nfs_inode_return_delegation(inode);
	/* First call standard NFS clear_inode() code */
	nfs_clear_inode(inode);
	/* Now clear out any remaining state */
	while (!list_empty(&nfsi->open_states)) {
		struct nfs4_state *state;
		
		state = list_entry(nfsi->open_states.next,
				struct nfs4_state,
				inode_states);
		dprintk("%s(%s/%Ld): found unclaimed NFSv4 state %p\n",
				__FUNCTION__,
				inode->i_sb->s_id,
				(long long)NFS_FILEID(inode),
				state);
		BUG_ON(atomic_read(&state->count) != 1);
		nfs4_close_state(state, state->state);
	}
}
#endif

struct inode *nfs_alloc_inode(struct super_block *sb)
{
	struct nfs_inode *nfsi;
	nfsi = (struct nfs_inode *)kmem_cache_alloc(nfs_inode_cachep, GFP_KERNEL);
	if (!nfsi)
		return NULL;
	nfsi->flags = 0UL;
	nfsi->cache_validity = 0UL;
	nfsi->cache_change_attribute = jiffies;
#ifdef CONFIG_NFS_V3_ACL
	nfsi->acl_access = ERR_PTR(-EAGAIN);
	nfsi->acl_default = ERR_PTR(-EAGAIN);
#endif
#ifdef CONFIG_NFS_V4
	nfsi->nfs4_acl = NULL;
#endif /* CONFIG_NFS_V4 */
	return &nfsi->vfs_inode;
}

void nfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(nfs_inode_cachep, NFS_I(inode));
}

static inline void nfs4_init_once(struct nfs_inode *nfsi)
{
#ifdef CONFIG_NFS_V4
	INIT_LIST_HEAD(&nfsi->open_states);
	nfsi->delegation = NULL;
	nfsi->delegation_state = 0;
	init_rwsem(&nfsi->rwsem);
#endif
}

static void init_once(void * foo, struct kmem_cache * cachep, unsigned long flags)
{
	struct nfs_inode *nfsi = (struct nfs_inode *) foo;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		inode_init_once(&nfsi->vfs_inode);
		spin_lock_init(&nfsi->req_lock);
		INIT_LIST_HEAD(&nfsi->dirty);
		INIT_LIST_HEAD(&nfsi->commit);
		INIT_LIST_HEAD(&nfsi->open_files);
		INIT_LIST_HEAD(&nfsi->access_cache_entry_lru);
		INIT_LIST_HEAD(&nfsi->access_cache_inode_lru);
		INIT_RADIX_TREE(&nfsi->nfs_page_tree, GFP_ATOMIC);
		atomic_set(&nfsi->data_updates, 0);
		nfsi->ndirty = 0;
		nfsi->ncommit = 0;
		nfsi->npages = 0;
		nfs4_init_once(nfsi);
	}
}
 
static int __init nfs_init_inodecache(void)
{
	nfs_inode_cachep = kmem_cache_create("nfs_inode_cache",
					     sizeof(struct nfs_inode),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	if (nfs_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

static void nfs_destroy_inodecache(void)
{
	kmem_cache_destroy(nfs_inode_cachep);
}

/*
 * Initialize NFS
 */
static int __init init_nfs_fs(void)
{
	int err;

	err = nfs_fs_proc_init();
	if (err)
		goto out5;

	err = nfs_init_nfspagecache();
	if (err)
		goto out4;

	err = nfs_init_inodecache();
	if (err)
		goto out3;

	err = nfs_init_readpagecache();
	if (err)
		goto out2;

	err = nfs_init_writepagecache();
	if (err)
		goto out1;

	err = nfs_init_directcache();
	if (err)
		goto out0;

#ifdef CONFIG_PROC_FS
	rpc_proc_register(&nfs_rpcstat);
#endif
	if ((err = register_nfs_fs()) != 0)
		goto out;
	return 0;
out:
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	nfs_destroy_directcache();
out0:
	nfs_destroy_writepagecache();
out1:
	nfs_destroy_readpagecache();
out2:
	nfs_destroy_inodecache();
out3:
	nfs_destroy_nfspagecache();
out4:
	nfs_fs_proc_exit();
out5:
	return err;
}

static void __exit exit_nfs_fs(void)
{
	nfs_destroy_directcache();
	nfs_destroy_writepagecache();
	nfs_destroy_readpagecache();
	nfs_destroy_inodecache();
	nfs_destroy_nfspagecache();
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	unregister_nfs_fs();
	nfs_fs_proc_exit();
}

/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
