/*
 *  linux/fs/nfs/inode.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs inode and superblock handling functions
 *
 *  Modularised by Alan Cox <alan@lxorguk.ukuu.org.uk>, while hacking some
 *  experimental NFS changes. Modularisation taken straight from SYS5 fs.
 *
 *  Change to nfs_read_super() to permit NFS mounts to multi-homed hosts.
 *  J.S.Peatfield@damtp.cam.ac.uk
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
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
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/nfs_idmap.h>
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/nfs_xdr.h>
#include <linux/slab.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"
#include "dns_resolve.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

#define NFS_64_BIT_INODE_NUMBERS_ENABLED	1

/* Default is to see 64-bit inode numbers */
static int enable_ino64 = NFS_64_BIT_INODE_NUMBERS_ENABLED;

static void nfs_invalidate_inode(struct inode *);
static int nfs_update_inode(struct inode *, struct nfs_fattr *);

static struct kmem_cache * nfs_inode_cachep;

static inline unsigned long
nfs_fattr_to_ino_t(struct nfs_fattr *fattr)
{
	return nfs_fileid_to_ino_t(fattr->fileid);
}

/**
 * nfs_wait_bit_killable - helper for functions that are sleeping on bit locks
 * @word: long word containing the bit lock
 */
int nfs_wait_bit_killable(void *word)
{
	if (fatal_signal_pending(current))
		return -ERESTARTSYS;
	schedule();
	return 0;
}

/**
 * nfs_compat_user_ino64 - returns the user-visible inode number
 * @fileid: 64-bit fileid
 *
 * This function returns a 32-bit inode number if the boot parameter
 * nfs.enable_ino64 is zero.
 */
u64 nfs_compat_user_ino64(u64 fileid)
{
	int ino;

	if (enable_ino64)
		return fileid;
	ino = fileid;
	if (sizeof(ino) < sizeof(fileid))
		ino ^= fileid >> (sizeof(fileid)-sizeof(ino)) * 8;
	return ino;
}

static void nfs_clear_inode(struct inode *inode)
{
	/*
	 * The following should never happen...
	 */
	BUG_ON(nfs_have_writebacks(inode));
	BUG_ON(!list_empty(&NFS_I(inode)->open_files));
	nfs_zap_acl_cache(inode);
	nfs_access_zap_cache(inode);
	nfs_fscache_release_inode_cookie(inode);
}

void nfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	end_writeback(inode);
	nfs_clear_inode(inode);
}

/**
 * nfs_sync_mapping - helper to flush all mmapped dirty data to disk
 */
int nfs_sync_mapping(struct address_space *mapping)
{
	int ret = 0;

	if (mapping->nrpages != 0) {
		unmap_mapping_range(mapping, 0, 0, 0);
		ret = nfs_wb_all(mapping->host);
	}
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

	nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
	nfsi->attrtimeo_timestamp = jiffies;

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

void nfs_zap_acl_cache(struct inode *inode)
{
	void (*clear_acl_cache)(struct inode *);

	clear_acl_cache = NFS_PROTO(inode)->clear_acl_cache;
	if (clear_acl_cache != NULL)
		clear_acl_cache(inode);
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_ACL;
	spin_unlock(&inode->i_lock);
}

void nfs_invalidate_atime(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ATIME;
	spin_unlock(&inode->i_lock);
}

/*
 * Invalidate, but do not unhash, the inode.
 * NB: must be called with inode->i_lock held!
 */
static void nfs_invalidate_inode(struct inode *inode)
{
	set_bit(NFS_INO_STALE, &NFS_I(inode)->flags);
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

	set_nfs_fileid(inode, fattr->fileid);
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

	if ((fattr->valid & NFS_ATTR_FATTR_FILEID) == 0)
		goto out_no_inode;
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) == 0)
		goto out_no_inode;

	hash = nfs_fattr_to_ino_t(fattr);

	inode = iget5_locked(sb, hash, nfs_find_actor, nfs_init_locked, &desc);
	if (inode == NULL) {
		inode = ERR_PTR(-ENOMEM);
		goto out_no_inode;
	}

	if (inode->i_state & I_NEW) {
		struct nfs_inode *nfsi = NFS_I(inode);
		unsigned long now = jiffies;

		/* We set i_ino for the few things that still rely on it,
		 * such as stat(2) */
		inode->i_ino = hash;

		/* We can't support update_atime(), since the server will reset it */
		inode->i_flags |= S_NOATIME|S_NOCMTIME;
		inode->i_mode = fattr->mode;
		if ((fattr->valid & NFS_ATTR_FATTR_MODE) == 0
				&& nfs_server_capable(inode, NFS_CAP_MODE))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
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
				set_bit(NFS_INO_ADVISE_RDPLUS, &NFS_I(inode)->flags);
			/* Deal with crossing mountpoints */
			if ((fattr->valid & NFS_ATTR_FATTR_FSID)
					&& !nfs_fsid_equal(&NFS_SB(sb)->fsid, &fattr->fsid)) {
				if (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL)
					inode->i_op = &nfs_referral_inode_operations;
				else
					inode->i_op = &nfs_mountpoint_inode_operations;
				inode->i_fop = NULL;
				set_bit(NFS_INO_MOUNTPOINT, &nfsi->flags);
			}
		} else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else
			init_special_inode(inode, inode->i_mode, fattr->rdev);

		memset(&inode->i_atime, 0, sizeof(inode->i_atime));
		memset(&inode->i_mtime, 0, sizeof(inode->i_mtime));
		memset(&inode->i_ctime, 0, sizeof(inode->i_ctime));
		nfsi->change_attr = 0;
		inode->i_size = 0;
		inode->i_nlink = 0;
		inode->i_uid = -2;
		inode->i_gid = -2;
		inode->i_blocks = 0;
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));

		nfsi->read_cache_jiffies = fattr->time_start;
		nfsi->attr_gencount = fattr->gencount;
		if (fattr->valid & NFS_ATTR_FATTR_ATIME)
			inode->i_atime = fattr->atime;
		else if (nfs_server_capable(inode, NFS_CAP_ATIME))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR;
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			inode->i_mtime = fattr->mtime;
		else if (nfs_server_capable(inode, NFS_CAP_MTIME))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_DATA;
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			inode->i_ctime = fattr->ctime;
		else if (nfs_server_capable(inode, NFS_CAP_CTIME))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
		if (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			nfsi->change_attr = fattr->change_attr;
		else if (nfs_server_capable(inode, NFS_CAP_CHANGE_ATTR))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_DATA;
		if (fattr->valid & NFS_ATTR_FATTR_SIZE)
			inode->i_size = nfs_size_to_loff_t(fattr->size);
		else
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_DATA
				| NFS_INO_REVAL_PAGECACHE;
		if (fattr->valid & NFS_ATTR_FATTR_NLINK)
			inode->i_nlink = fattr->nlink;
		else if (nfs_server_capable(inode, NFS_CAP_NLINK))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR;
		if (fattr->valid & NFS_ATTR_FATTR_OWNER)
			inode->i_uid = fattr->uid;
		else if (nfs_server_capable(inode, NFS_CAP_OWNER))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
		if (fattr->valid & NFS_ATTR_FATTR_GROUP)
			inode->i_gid = fattr->gid;
		else if (nfs_server_capable(inode, NFS_CAP_OWNER_GROUP))
			nfsi->cache_validity |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
		if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
			inode->i_blocks = fattr->du.nfs2.blocks;
		if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
			/*
			 * report the blocks in 512byte units
			 */
			inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		}
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = now;
		nfsi->access_cache = RB_ROOT;

		nfs_fscache_init_inode_cookie(inode);

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

#define NFS_VALID_ATTRS (ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_SIZE|ATTR_ATIME|ATTR_ATIME_SET|ATTR_MTIME|ATTR_MTIME_SET|ATTR_FILE)

int
nfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct nfs_fattr *fattr;
	int error = -ENOMEM;

	nfs_inc_stats(inode, NFSIOS_VFSSETATTR);

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attr->ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		attr->ia_valid &= ~ATTR_MODE;

	if (attr->ia_valid & ATTR_SIZE) {
		if (!S_ISREG(inode->i_mode) || attr->ia_size == i_size_read(inode))
			attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Optimization: if the end result is no change, don't RPC */
	attr->ia_valid &= NFS_VALID_ATTRS;
	if ((attr->ia_valid & ~ATTR_FILE) == 0)
		return 0;

	/* Write all dirty data */
	if (S_ISREG(inode->i_mode))
		nfs_wb_all(inode);

	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		goto out;
	/*
	 * Return any delegations if we're going to change ACLs
	 */
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0)
		nfs_inode_return_delegation(inode);
	error = NFS_PROTO(inode)->setattr(dentry, fattr, attr);
	if (error == 0)
		nfs_refresh_inode(inode, fattr);
	nfs_free_fattr(fattr);
out:
	return error;
}

/**
 * nfs_vmtruncate - unmap mappings "freed" by truncate() syscall
 * @inode: inode of the file used
 * @offset: file offset to start truncating
 *
 * This is a copy of the common vmtruncate, but with the locking
 * corrected to take into account the fact that NFS requires
 * inode->i_size to be updated under the inode->i_lock.
 */
static int nfs_vmtruncate(struct inode * inode, loff_t offset)
{
	loff_t oldsize;
	int err;

	err = inode_newsize_ok(inode, offset);
	if (err)
		goto out;

	spin_lock(&inode->i_lock);
	oldsize = inode->i_size;
	i_size_write(inode, offset);
	spin_unlock(&inode->i_lock);

	truncate_pagecache(inode, oldsize, offset);
out:
	return err;
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
		spin_lock(&inode->i_lock);
		if ((attr->ia_valid & ATTR_MODE) != 0) {
			int mode = attr->ia_mode & S_IALLUGO;
			mode |= inode->i_mode & ~S_IALLUGO;
			inode->i_mode = mode;
		}
		if ((attr->ia_valid & ATTR_UID) != 0)
			inode->i_uid = attr->ia_uid;
		if ((attr->ia_valid & ATTR_GID) != 0)
			inode->i_gid = attr->ia_gid;
		NFS_I(inode)->cache_validity |= NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		spin_unlock(&inode->i_lock);
	}
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		nfs_inc_stats(inode, NFSIOS_SETATTRTRUNC);
		nfs_vmtruncate(inode, attr->ia_size);
	}
}

int nfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int need_atime = NFS_I(inode)->cache_validity & NFS_INO_INVALID_ATIME;
	int err;

	/* Flush out writes to the server in order to update c/mtime.  */
	if (S_ISREG(inode->i_mode)) {
		err = filemap_write_and_wait(inode->i_mapping);
		if (err)
			goto out;
	}

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
	if (!err) {
		generic_fillattr(inode, stat);
		stat->ino = nfs_compat_user_ino64(NFS_FILEID(inode));
	}
out:
	return err;
}

static void nfs_init_lock_context(struct nfs_lock_context *l_ctx)
{
	atomic_set(&l_ctx->count, 1);
	l_ctx->lockowner = current->files;
	l_ctx->pid = current->tgid;
	INIT_LIST_HEAD(&l_ctx->list);
}

static struct nfs_lock_context *__nfs_find_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *pos;

	list_for_each_entry(pos, &ctx->lock_context.list, list) {
		if (pos->lockowner != current->files)
			continue;
		if (pos->pid != current->tgid)
			continue;
		atomic_inc(&pos->count);
		return pos;
	}
	return NULL;
}

struct nfs_lock_context *nfs_get_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *res, *new = NULL;
	struct inode *inode = ctx->path.dentry->d_inode;

	spin_lock(&inode->i_lock);
	res = __nfs_find_lock_context(ctx);
	if (res == NULL) {
		spin_unlock(&inode->i_lock);
		new = kmalloc(sizeof(*new), GFP_KERNEL);
		if (new == NULL)
			return NULL;
		nfs_init_lock_context(new);
		spin_lock(&inode->i_lock);
		res = __nfs_find_lock_context(ctx);
		if (res == NULL) {
			list_add_tail(&new->list, &ctx->lock_context.list);
			new->open_context = ctx;
			res = new;
			new = NULL;
		}
	}
	spin_unlock(&inode->i_lock);
	kfree(new);
	return res;
}

void nfs_put_lock_context(struct nfs_lock_context *l_ctx)
{
	struct nfs_open_context *ctx = l_ctx->open_context;
	struct inode *inode = ctx->path.dentry->d_inode;

	if (!atomic_dec_and_lock(&l_ctx->count, &inode->i_lock))
		return;
	list_del(&l_ctx->list);
	spin_unlock(&inode->i_lock);
	kfree(l_ctx);
}

/**
 * nfs_close_context - Common close_context() routine NFSv2/v3
 * @ctx: pointer to context
 * @is_sync: is this a synchronous close
 *
 * always ensure that the attributes are up to date if we're mounted
 * with close-to-open semantics
 */
void nfs_close_context(struct nfs_open_context *ctx, int is_sync)
{
	struct inode *inode;
	struct nfs_server *server;

	if (!(ctx->mode & FMODE_WRITE))
		return;
	if (!is_sync)
		return;
	inode = ctx->path.dentry->d_inode;
	if (!list_empty(&NFS_I(inode)->open_files))
		return;
	server = NFS_SERVER(inode);
	if (server->flags & NFS_MOUNT_NOCTO)
		return;
	nfs_revalidate_inode(server, inode);
}

static struct nfs_open_context *alloc_nfs_open_context(struct path *path, struct rpc_cred *cred)
{
	struct nfs_open_context *ctx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx != NULL) {
		ctx->path = *path;
		path_get(&ctx->path);
		ctx->cred = get_rpccred(cred);
		ctx->state = NULL;
		ctx->flags = 0;
		ctx->error = 0;
		ctx->dir_cookie = 0;
		nfs_init_lock_context(&ctx->lock_context);
		ctx->lock_context.open_context = ctx;
	}
	return ctx;
}

struct nfs_open_context *get_nfs_open_context(struct nfs_open_context *ctx)
{
	if (ctx != NULL)
		atomic_inc(&ctx->lock_context.count);
	return ctx;
}

static void __put_nfs_open_context(struct nfs_open_context *ctx, int is_sync)
{
	struct inode *inode = ctx->path.dentry->d_inode;

	if (!atomic_dec_and_lock(&ctx->lock_context.count, &inode->i_lock))
		return;
	list_del(&ctx->list);
	spin_unlock(&inode->i_lock);
	NFS_PROTO(inode)->close_context(ctx, is_sync);
	if (ctx->cred != NULL)
		put_rpccred(ctx->cred);
	path_put(&ctx->path);
	kfree(ctx);
}

void put_nfs_open_context(struct nfs_open_context *ctx)
{
	__put_nfs_open_context(ctx, 0);
}

/*
 * Ensure that mmap has a recent RPC credential for use when writing out
 * shared pages
 */
static void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	filp->private_data = get_nfs_open_context(ctx);
	spin_lock(&inode->i_lock);
	list_add(&ctx->list, &nfsi->open_files);
	spin_unlock(&inode->i_lock);
}

/*
 * Given an inode, search for an open context with the desired characteristics
 */
struct nfs_open_context *nfs_find_open_context(struct inode *inode, struct rpc_cred *cred, fmode_t mode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *pos, *ctx = NULL;

	spin_lock(&inode->i_lock);
	list_for_each_entry(pos, &nfsi->open_files, list) {
		if (cred != NULL && pos->cred != cred)
			continue;
		if ((pos->mode & (FMODE_READ|FMODE_WRITE)) != mode)
			continue;
		ctx = get_nfs_open_context(pos);
		break;
	}
	spin_unlock(&inode->i_lock);
	return ctx;
}

static void nfs_file_clear_open_context(struct file *filp)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct nfs_open_context *ctx = nfs_file_open_context(filp);

	if (ctx) {
		filp->private_data = NULL;
		spin_lock(&inode->i_lock);
		list_move_tail(&ctx->list, &NFS_I(inode)->open_files);
		spin_unlock(&inode->i_lock);
		__put_nfs_open_context(ctx, filp->f_flags & O_DIRECT ? 0 : 1);
	}
}

/*
 * These allocate and release file read/write context information.
 */
int nfs_open(struct inode *inode, struct file *filp)
{
	struct nfs_open_context *ctx;
	struct rpc_cred *cred;

	cred = rpc_lookup_cred();
	if (IS_ERR(cred))
		return PTR_ERR(cred);
	ctx = alloc_nfs_open_context(&filp->f_path, cred);
	put_rpccred(cred);
	if (ctx == NULL)
		return -ENOMEM;
	ctx->mode = filp->f_mode;
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	nfs_fscache_set_inode_cookie(inode, filp);
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
	struct nfs_fattr *fattr = NULL;
	struct nfs_inode *nfsi = NFS_I(inode);

	dfprintk(PAGECACHE, "NFS: revalidating (%s/%Ld)\n",
		inode->i_sb->s_id, (long long)NFS_FILEID(inode));

	if (is_bad_inode(inode))
		goto out;
	if (NFS_STALE(inode))
		goto out;

	status = -ENOMEM;
	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		goto out;

	nfs_inc_stats(inode, NFSIOS_INODEREVALIDATE);
	status = NFS_PROTO(inode)->getattr(server, NFS_FH(inode), fattr);
	if (status != 0) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) getattr failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		if (status == -ESTALE) {
			nfs_zap_caches(inode);
			if (!S_ISDIR(inode->i_mode))
				set_bit(NFS_INO_STALE, &NFS_I(inode)->flags);
		}
		goto out;
	}

	status = nfs_refresh_inode(inode, fattr);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Ld) refresh failed, error=%d\n",
			 inode->i_sb->s_id,
			 (long long)NFS_FILEID(inode), status);
		goto out;
	}

	if (nfsi->cache_validity & NFS_INO_INVALID_ACL)
		nfs_zap_acl_cache(inode);

	dfprintk(PAGECACHE, "NFS: (%s/%Ld) revalidation complete\n",
		inode->i_sb->s_id,
		(long long)NFS_FILEID(inode));

 out:
	nfs_free_fattr(fattr);
	return status;
}

int nfs_attribute_timeout(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	return !time_in_range_open(jiffies, nfsi->read_cache_jiffies, nfsi->read_cache_jiffies + nfsi->attrtimeo);
}

static int nfs_attribute_cache_expired(struct inode *inode)
{
	if (nfs_have_delegated_attributes(inode))
		return 0;
	return nfs_attribute_timeout(inode);
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
			&& !nfs_attribute_cache_expired(inode))
		return NFS_STALE(inode) ? -ESTALE : 0;
	return __nfs_revalidate_inode(server, inode);
}

static int nfs_invalidate_mapping(struct inode *inode, struct address_space *mapping)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	
	if (mapping->nrpages != 0) {
		int ret = invalidate_inode_pages2(mapping);
		if (ret < 0)
			return ret;
	}
	spin_lock(&inode->i_lock);
	nfsi->cache_validity &= ~NFS_INO_INVALID_DATA;
	if (S_ISDIR(inode->i_mode))
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
	spin_unlock(&inode->i_lock);
	nfs_inc_stats(inode, NFSIOS_DATAINVALIDATE);
	nfs_fscache_reset_inode_cookie(inode);
	dfprintk(PAGECACHE, "NFS: (%s/%Ld) data cache invalidated\n",
			inode->i_sb->s_id, (long long)NFS_FILEID(inode));
	return 0;
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

	if ((nfsi->cache_validity & NFS_INO_REVAL_PAGECACHE)
			|| nfs_attribute_cache_expired(inode)
			|| NFS_STALE(inode)) {
		ret = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
		if (ret < 0)
			goto out;
	}
	if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
		ret = nfs_invalidate_mapping(inode, mapping);
out:
	return ret;
}

static void nfs_wcc_update_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if ((fattr->valid & NFS_ATTR_FATTR_PRECHANGE)
			&& (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			&& nfsi->change_attr == fattr->pre_change_attr) {
		nfsi->change_attr = fattr->change_attr;
		if (S_ISDIR(inode->i_mode))
			nfsi->cache_validity |= NFS_INO_INVALID_DATA;
	}
	/* If we have atomic WCC data, we may update some attributes */
	if ((fattr->valid & NFS_ATTR_FATTR_PRECTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_CTIME)
			&& timespec_equal(&inode->i_ctime, &fattr->pre_ctime))
			memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));

	if ((fattr->valid & NFS_ATTR_FATTR_PREMTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_MTIME)
			&& timespec_equal(&inode->i_mtime, &fattr->pre_mtime)) {
			memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
			if (S_ISDIR(inode->i_mode))
				nfsi->cache_validity |= NFS_INO_INVALID_DATA;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_PRESIZE)
			&& (fattr->valid & NFS_ATTR_FATTR_SIZE)
			&& i_size_read(inode) == nfs_size_to_loff_t(fattr->pre_size)
			&& nfsi->npages == 0)
			i_size_write(inode, nfs_size_to_loff_t(fattr->size));
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
	unsigned long invalid = 0;


	/* Has the inode gone and changed behind our back? */
	if ((fattr->valid & NFS_ATTR_FATTR_FILEID) && nfsi->fileid != fattr->fileid)
		return -EIO;
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && (inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		return -EIO;

	if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
			nfsi->change_attr != fattr->change_attr)
		invalid |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;

	/* Verify a few of the more important attributes */
	if ((fattr->valid & NFS_ATTR_FATTR_MTIME) && !timespec_equal(&inode->i_mtime, &fattr->mtime))
		invalid |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;

	if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
		cur_size = i_size_read(inode);
		new_isize = nfs_size_to_loff_t(fattr->size);
		if (cur_size != new_isize && nfsi->npages == 0)
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;
	}

	/* Have any file permissions changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_MODE) && (inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO))
		invalid |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;
	if ((fattr->valid & NFS_ATTR_FATTR_OWNER) && inode->i_uid != fattr->uid)
		invalid |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;
	if ((fattr->valid & NFS_ATTR_FATTR_GROUP) && inode->i_gid != fattr->gid)
		invalid |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;

	/* Has the link count changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_NLINK) && inode->i_nlink != fattr->nlink)
		invalid |= NFS_INO_INVALID_ATTR;

	if ((fattr->valid & NFS_ATTR_FATTR_ATIME) && !timespec_equal(&inode->i_atime, &fattr->atime))
		invalid |= NFS_INO_INVALID_ATIME;

	if (invalid != 0)
		nfsi->cache_validity |= invalid;

	nfsi->read_cache_jiffies = fattr->time_start;
	return 0;
}

static int nfs_ctime_need_update(const struct inode *inode, const struct nfs_fattr *fattr)
{
	if (!(fattr->valid & NFS_ATTR_FATTR_CTIME))
		return 0;
	return timespec_compare(&fattr->ctime, &inode->i_ctime) > 0;
}

static int nfs_size_need_update(const struct inode *inode, const struct nfs_fattr *fattr)
{
	if (!(fattr->valid & NFS_ATTR_FATTR_SIZE))
		return 0;
	return nfs_size_to_loff_t(fattr->size) > i_size_read(inode);
}

static atomic_long_t nfs_attr_generation_counter;

static unsigned long nfs_read_attr_generation_counter(void)
{
	return atomic_long_read(&nfs_attr_generation_counter);
}

unsigned long nfs_inc_attr_generation_counter(void)
{
	return atomic_long_inc_return(&nfs_attr_generation_counter);
}

void nfs_fattr_init(struct nfs_fattr *fattr)
{
	fattr->valid = 0;
	fattr->time_start = jiffies;
	fattr->gencount = nfs_inc_attr_generation_counter();
}

struct nfs_fattr *nfs_alloc_fattr(void)
{
	struct nfs_fattr *fattr;

	fattr = kmalloc(sizeof(*fattr), GFP_NOFS);
	if (fattr != NULL)
		nfs_fattr_init(fattr);
	return fattr;
}

struct nfs_fh *nfs_alloc_fhandle(void)
{
	struct nfs_fh *fh;

	fh = kmalloc(sizeof(struct nfs_fh), GFP_NOFS);
	if (fh != NULL)
		fh->size = 0;
	return fh;
}

/**
 * nfs_inode_attrs_need_update - check if the inode attributes need updating
 * @inode - pointer to inode
 * @fattr - attributes
 *
 * Attempt to divine whether or not an RPC call reply carrying stale
 * attributes got scheduled after another call carrying updated ones.
 *
 * To do so, the function first assumes that a more recent ctime means
 * that the attributes in fattr are newer, however it also attempt to
 * catch the case where ctime either didn't change, or went backwards
 * (if someone reset the clock on the server) by looking at whether
 * or not this RPC call was started after the inode was last updated.
 * Note also the check for wraparound of 'attr_gencount'
 *
 * The function returns 'true' if it thinks the attributes in 'fattr' are
 * more recent than the ones cached in the inode.
 *
 */
static int nfs_inode_attrs_need_update(const struct inode *inode, const struct nfs_fattr *fattr)
{
	const struct nfs_inode *nfsi = NFS_I(inode);

	return ((long)fattr->gencount - (long)nfsi->attr_gencount) > 0 ||
		nfs_ctime_need_update(inode, fattr) ||
		nfs_size_need_update(inode, fattr) ||
		((long)nfsi->attr_gencount - (long)nfs_read_attr_generation_counter() > 0);
}

static int nfs_refresh_inode_locked(struct inode *inode, struct nfs_fattr *fattr)
{
	if (nfs_inode_attrs_need_update(inode, fattr))
		return nfs_update_inode(inode, fattr);
	return nfs_check_inode_attributes(inode, fattr);
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
	int status;

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	spin_lock(&inode->i_lock);
	status = nfs_refresh_inode_locked(inode, fattr);
	spin_unlock(&inode->i_lock);

	return status;
}

static int nfs_post_op_update_inode_locked(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	nfsi->cache_validity |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;
	if (S_ISDIR(inode->i_mode))
		nfsi->cache_validity |= NFS_INO_INVALID_DATA;
	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	return nfs_refresh_inode_locked(inode, fattr);
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
	int status;

	spin_lock(&inode->i_lock);
	status = nfs_post_op_update_inode_locked(inode, fattr);
	spin_unlock(&inode->i_lock);
	return status;
}

/**
 * nfs_post_op_update_inode_force_wcc - try to update the inode attribute cache
 * @inode - pointer to inode
 * @fattr - updated attributes
 *
 * After an operation that has changed the inode metadata, mark the
 * attribute cache as being invalid, then try to update it. Fake up
 * weak cache consistency data, if none exist.
 *
 * This function is mainly designed to be used by the ->write_done() functions.
 */
int nfs_post_op_update_inode_force_wcc(struct inode *inode, struct nfs_fattr *fattr)
{
	int status;

	spin_lock(&inode->i_lock);
	/* Don't do a WCC update if these attributes are already stale */
	if ((fattr->valid & NFS_ATTR_FATTR) == 0 ||
			!nfs_inode_attrs_need_update(inode, fattr)) {
		fattr->valid &= ~(NFS_ATTR_FATTR_PRECHANGE
				| NFS_ATTR_FATTR_PRESIZE
				| NFS_ATTR_FATTR_PREMTIME
				| NFS_ATTR_FATTR_PRECTIME);
		goto out_noforce;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECHANGE) == 0) {
		fattr->pre_change_attr = NFS_I(inode)->change_attr;
		fattr->valid |= NFS_ATTR_FATTR_PRECHANGE;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECTIME) == 0) {
		memcpy(&fattr->pre_ctime, &inode->i_ctime, sizeof(fattr->pre_ctime));
		fattr->valid |= NFS_ATTR_FATTR_PRECTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_MTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PREMTIME) == 0) {
		memcpy(&fattr->pre_mtime, &inode->i_mtime, sizeof(fattr->pre_mtime));
		fattr->valid |= NFS_ATTR_FATTR_PREMTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_SIZE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRESIZE) == 0) {
		fattr->pre_size = i_size_read(inode);
		fattr->valid |= NFS_ATTR_FATTR_PRESIZE;
	}
out_noforce:
	status = nfs_post_op_update_inode_locked(inode, fattr);
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
	unsigned long invalid = 0;
	unsigned long now = jiffies;
	unsigned long save_cache_validity;

	dfprintk(VFS, "NFS: %s(%s/%ld ct=%d info=0x%x)\n",
			__func__, inode->i_sb->s_id, inode->i_ino,
			atomic_read(&inode->i_count), fattr->valid);

	if ((fattr->valid & NFS_ATTR_FATTR_FILEID) && nfsi->fileid != fattr->fileid)
		goto out_fileid;

	/*
	 * Make sure the inode's type hasn't changed.
	 */
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && (inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		goto out_changed;

	server = NFS_SERVER(inode);
	/* Update the fsid? */
	if (S_ISDIR(inode->i_mode) && (fattr->valid & NFS_ATTR_FATTR_FSID) &&
			!nfs_fsid_equal(&server->fsid, &fattr->fsid) &&
			!test_bit(NFS_INO_MOUNTPOINT, &nfsi->flags))
		server->fsid = fattr->fsid;

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	nfsi->read_cache_jiffies = fattr->time_start;

	save_cache_validity = nfsi->cache_validity;
	nfsi->cache_validity &= ~(NFS_INO_INVALID_ATTR
			| NFS_INO_INVALID_ATIME
			| NFS_INO_REVAL_FORCED
			| NFS_INO_REVAL_PAGECACHE);

	/* Do atomic weak cache consistency updates */
	nfs_wcc_update_inode(inode, fattr);

	/* More cache consistency checks */
	if (fattr->valid & NFS_ATTR_FATTR_CHANGE) {
		if (nfsi->change_attr != fattr->change_attr) {
			dprintk("NFS: change_attr change on server for file %s/%ld\n",
					inode->i_sb->s_id, inode->i_ino);
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
			if (S_ISDIR(inode->i_mode))
				nfs_force_lookup_revalidate(inode);
			nfsi->change_attr = fattr->change_attr;
		}
	} else if (server->caps & NFS_CAP_CHANGE_ATTR)
		invalid |= save_cache_validity;

	if (fattr->valid & NFS_ATTR_FATTR_MTIME) {
		/* NFSv2/v3: Check if the mtime agrees */
		if (!timespec_equal(&inode->i_mtime, &fattr->mtime)) {
			dprintk("NFS: mtime change on server for file %s/%ld\n",
					inode->i_sb->s_id, inode->i_ino);
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
			if (S_ISDIR(inode->i_mode))
				nfs_force_lookup_revalidate(inode);
			memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
		}
	} else if (server->caps & NFS_CAP_MTIME)
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_DATA
				| NFS_INO_REVAL_PAGECACHE
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_CTIME) {
		/* If ctime has changed we should definitely clear access+acl caches */
		if (!timespec_equal(&inode->i_ctime, &fattr->ctime)) {
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
			/* and probably clear data for a directory too as utimes can cause
			 * havoc with our cache.
			 */
			if (S_ISDIR(inode->i_mode)) {
				invalid |= NFS_INO_INVALID_DATA;
				nfs_force_lookup_revalidate(inode);
			}
			memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));
		}
	} else if (server->caps & NFS_CAP_CTIME)
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_FORCED);

	/* Check if our cached file size is stale */
	if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
		new_isize = nfs_size_to_loff_t(fattr->size);
		cur_isize = i_size_read(inode);
		if (new_isize != cur_isize) {
			/* Do we perhaps have any outstanding writes, or has
			 * the file grown beyond our last write? */
			if (nfsi->npages == 0 || new_isize > cur_isize) {
				i_size_write(inode, new_isize);
				invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
			}
			dprintk("NFS: isize change on server for file %s/%ld\n",
					inode->i_sb->s_id, inode->i_ino);
		}
	} else
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATTR
				| NFS_INO_REVAL_PAGECACHE
				| NFS_INO_REVAL_FORCED);


	if (fattr->valid & NFS_ATTR_FATTR_ATIME)
		memcpy(&inode->i_atime, &fattr->atime, sizeof(inode->i_atime));
	else if (server->caps & NFS_CAP_ATIME)
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATIME
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_MODE) {
		if ((inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO)) {
			umode_t newmode = inode->i_mode & S_IFMT;
			newmode |= fattr->mode & S_IALLUGO;
			inode->i_mode = newmode;
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		}
	} else if (server->caps & NFS_CAP_MODE)
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_OWNER) {
		if (inode->i_uid != fattr->uid) {
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
			inode->i_uid = fattr->uid;
		}
	} else if (server->caps & NFS_CAP_OWNER)
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_GROUP) {
		if (inode->i_gid != fattr->gid) {
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
			inode->i_gid = fattr->gid;
		}
	} else if (server->caps & NFS_CAP_OWNER_GROUP)
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_NLINK) {
		if (inode->i_nlink != fattr->nlink) {
			invalid |= NFS_INO_INVALID_ATTR;
			if (S_ISDIR(inode->i_mode))
				invalid |= NFS_INO_INVALID_DATA;
			inode->i_nlink = fattr->nlink;
		}
	} else if (server->caps & NFS_CAP_NLINK)
		invalid |= save_cache_validity & (NFS_INO_INVALID_ATTR
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
		/*
		 * report the blocks in 512byte units
		 */
		inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
 	}
	if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
		inode->i_blocks = fattr->du.nfs2.blocks;

	/* Update attrtimeo value if we're out of the unstable period */
	if (invalid & NFS_INO_INVALID_ATTR) {
		nfs_inc_stats(inode, NFSIOS_ATTRINVALIDATE);
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = now;
		nfsi->attr_gencount = nfs_inc_attr_generation_counter();
	} else {
		if (!time_in_range_open(now, nfsi->attrtimeo_timestamp, nfsi->attrtimeo_timestamp + nfsi->attrtimeo)) {
			if ((nfsi->attrtimeo <<= 1) > NFS_MAXATTRTIMEO(inode))
				nfsi->attrtimeo = NFS_MAXATTRTIMEO(inode);
			nfsi->attrtimeo_timestamp = now;
		}
	}
	invalid &= ~NFS_INO_INVALID_ATTR;
	/* Don't invalidate the data if we were to blame */
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
				|| S_ISLNK(inode->i_mode)))
		invalid &= ~NFS_INO_INVALID_DATA;
	if (!nfs_have_delegation(inode, FMODE_READ) ||
			(save_cache_validity & NFS_INO_REVAL_FORCED))
		nfsi->cache_validity |= invalid;

	return 0;
 out_changed:
	/*
	 * Big trouble! The inode has become a different object.
	 */
	printk(KERN_DEBUG "%s: inode %ld mode changed, %07o to %07o\n",
			__func__, inode->i_ino, inode->i_mode, fattr->mode);
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
void nfs4_evict_inode(struct inode *inode)
{
	truncate_inode_pages(&inode->i_data, 0);
	end_writeback(inode);
	/* If we are holding a delegation, return it! */
	nfs_inode_return_delegation_noreclaim(inode);
	/* First call standard NFS clear_inode() code */
	nfs_clear_inode(inode);
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

static void init_once(void *foo)
{
	struct nfs_inode *nfsi = (struct nfs_inode *) foo;

	inode_init_once(&nfsi->vfs_inode);
	INIT_LIST_HEAD(&nfsi->open_files);
	INIT_LIST_HEAD(&nfsi->access_cache_entry_lru);
	INIT_LIST_HEAD(&nfsi->access_cache_inode_lru);
	INIT_RADIX_TREE(&nfsi->nfs_page_tree, GFP_ATOMIC);
	nfsi->npages = 0;
	nfsi->ncommit = 0;
	atomic_set(&nfsi->silly_count, 1);
	INIT_HLIST_HEAD(&nfsi->silly_list);
	init_waitqueue_head(&nfsi->waitqueue);
	nfs4_init_once(nfsi);
}

static int __init nfs_init_inodecache(void)
{
	nfs_inode_cachep = kmem_cache_create("nfs_inode_cache",
					     sizeof(struct nfs_inode),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (nfs_inode_cachep == NULL)
		return -ENOMEM;

	return 0;
}

static void nfs_destroy_inodecache(void)
{
	kmem_cache_destroy(nfs_inode_cachep);
}

struct workqueue_struct *nfsiod_workqueue;

/*
 * start up the nfsiod workqueue
 */
static int nfsiod_start(void)
{
	struct workqueue_struct *wq;
	dprintk("RPC:       creating workqueue nfsiod\n");
	wq = create_singlethread_workqueue("nfsiod");
	if (wq == NULL)
		return -ENOMEM;
	nfsiod_workqueue = wq;
	return 0;
}

/*
 * Destroy the nfsiod workqueue
 */
static void nfsiod_stop(void)
{
	struct workqueue_struct *wq;

	wq = nfsiod_workqueue;
	if (wq == NULL)
		return;
	nfsiod_workqueue = NULL;
	destroy_workqueue(wq);
}

/*
 * Initialize NFS
 */
static int __init init_nfs_fs(void)
{
	int err;

	err = nfs_dns_resolver_init();
	if (err < 0)
		goto out8;

	err = nfs_fscache_register();
	if (err < 0)
		goto out7;

	err = nfsiod_start();
	if (err)
		goto out6;

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
	nfsiod_stop();
out6:
	nfs_fscache_unregister();
out7:
	nfs_dns_resolver_destroy();
out8:
	return err;
}

static void __exit exit_nfs_fs(void)
{
	nfs_destroy_directcache();
	nfs_destroy_writepagecache();
	nfs_destroy_readpagecache();
	nfs_destroy_inodecache();
	nfs_destroy_nfspagecache();
	nfs_fscache_unregister();
	nfs_dns_resolver_destroy();
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister("nfs");
#endif
	unregister_nfs_fs();
	nfs_fs_proc_exit();
	nfsiod_stop();
}

/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");
module_param(enable_ino64, bool, 0644);

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
