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
#include <linux/vfs.h>
#include <linux/inet.h>
#include <linux/nfs_xdr.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/freezer.h>

#include <asm/uaccess.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"
#include "pnfs.h"
#include "nfs.h"
#include "netns.h"

#include "nfstrace.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

#define NFS_64_BIT_INODE_NUMBERS_ENABLED	1

/* Default is to see 64-bit inode numbers */
static bool enable_ino64 = NFS_64_BIT_INODE_NUMBERS_ENABLED;

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
int nfs_wait_bit_killable(struct wait_bit_key *key)
{
	if (fatal_signal_pending(current))
		return -ERESTARTSYS;
	freezable_schedule_unsafe();
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_wait_bit_killable);

/**
 * nfs_compat_user_ino64 - returns the user-visible inode number
 * @fileid: 64-bit fileid
 *
 * This function returns a 32-bit inode number if the boot parameter
 * nfs.enable_ino64 is zero.
 */
u64 nfs_compat_user_ino64(u64 fileid)
{
#ifdef CONFIG_COMPAT
	compat_ulong_t ino;
#else	
	unsigned long ino;
#endif

	if (enable_ino64)
		return fileid;
	ino = fileid;
	if (sizeof(ino) < sizeof(fileid))
		ino ^= fileid >> (sizeof(fileid)-sizeof(ino)) * 8;
	return ino;
}

int nfs_drop_inode(struct inode *inode)
{
	return NFS_STALE(inode) || generic_drop_inode(inode);
}
EXPORT_SYMBOL_GPL(nfs_drop_inode);

void nfs_clear_inode(struct inode *inode)
{
	/*
	 * The following should never happen...
	 */
	WARN_ON_ONCE(nfs_have_writebacks(inode));
	WARN_ON_ONCE(!list_empty(&NFS_I(inode)->open_files));
	nfs_zap_acl_cache(inode);
	nfs_access_zap_cache(inode);
	nfs_fscache_clear_inode(inode);
}
EXPORT_SYMBOL_GPL(nfs_clear_inode);

void nfs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
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

static void nfs_set_cache_invalid(struct inode *inode, unsigned long flags)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	if (inode->i_mapping->nrpages == 0)
		flags &= ~NFS_INO_INVALID_DATA;
	nfsi->cache_validity |= flags;
	if (flags & NFS_INO_INVALID_DATA)
		nfs_fscache_invalidate(inode);
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

	memset(NFS_I(inode)->cookieverf, 0, sizeof(NFS_I(inode)->cookieverf));
	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)) {
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR
					| NFS_INO_INVALID_DATA
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL
					| NFS_INO_REVAL_PAGECACHE);
	} else
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL
					| NFS_INO_REVAL_PAGECACHE);
	nfs_zap_label_cache_locked(nfsi);
}

void nfs_zap_caches(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	nfs_zap_caches_locked(inode);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_zap_caches);

void nfs_zap_mapping(struct inode *inode, struct address_space *mapping)
{
	if (mapping->nrpages != 0) {
		spin_lock(&inode->i_lock);
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_DATA);
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
EXPORT_SYMBOL_GPL(nfs_zap_acl_cache);

void nfs_invalidate_atime(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATIME);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_invalidate_atime);

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
	if ((S_IFMT & inode->i_mode) != (S_IFMT & fattr->mode))
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

#ifdef CONFIG_NFS_V4_SECURITY_LABEL
static void nfs_clear_label_invalid(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_LABEL;
	spin_unlock(&inode->i_lock);
}

void nfs_setsecurity(struct inode *inode, struct nfs_fattr *fattr,
					struct nfs4_label *label)
{
	int error;

	if (label == NULL)
		return;

	if ((fattr->valid & NFS_ATTR_FATTR_V4_SECURITY_LABEL) && inode->i_security) {
		error = security_inode_notifysecctx(inode, label->label,
				label->len);
		if (error)
			printk(KERN_ERR "%s() %s %d "
					"security_inode_notifysecctx() %d\n",
					__func__,
					(char *)label->label,
					label->len, error);
		nfs_clear_label_invalid(inode);
	}
}

struct nfs4_label *nfs4_label_alloc(struct nfs_server *server, gfp_t flags)
{
	struct nfs4_label *label = NULL;
	int minor_version = server->nfs_client->cl_minorversion;

	if (minor_version < 2)
		return label;

	if (!(server->caps & NFS_CAP_SECURITY_LABEL))
		return label;

	label = kzalloc(sizeof(struct nfs4_label), flags);
	if (label == NULL)
		return ERR_PTR(-ENOMEM);

	label->label = kzalloc(NFS4_MAXLABELLEN, flags);
	if (label->label == NULL) {
		kfree(label);
		return ERR_PTR(-ENOMEM);
	}
	label->len = NFS4_MAXLABELLEN;

	return label;
}
EXPORT_SYMBOL_GPL(nfs4_label_alloc);
#else
void nfs_setsecurity(struct inode *inode, struct nfs_fattr *fattr,
					struct nfs4_label *label)
{
}
#endif
EXPORT_SYMBOL_GPL(nfs_setsecurity);

/*
 * This is our front-end to iget that looks up inodes by file handle
 * instead of inode number.
 */
struct inode *
nfs_fhget(struct super_block *sb, struct nfs_fh *fh, struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr
	};
	struct inode *inode = ERR_PTR(-ENOENT);
	unsigned long hash;

	nfs_attr_check_mountpoint(sb, fattr);

	if (nfs_attr_use_mounted_on_fileid(fattr))
		fattr->fileid = fattr->mounted_on_fileid;
	else if ((fattr->valid & NFS_ATTR_FATTR_FILEID) == 0)
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
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_inode_operations.
		 */
		inode->i_op = NFS_SB(sb)->nfs_client->rpc_ops->file_inode_ops;
		if (S_ISREG(inode->i_mode)) {
			inode->i_fop = NFS_SB(sb)->nfs_client->rpc_ops->file_ops;
			inode->i_data.a_ops = &nfs_file_aops;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op = NFS_SB(sb)->nfs_client->rpc_ops->dir_inode_ops;
			inode->i_fop = &nfs_dir_operations;
			inode->i_data.a_ops = &nfs_dir_aops;
			/* Deal with crossing mountpoints */
			if (fattr->valid & NFS_ATTR_FATTR_MOUNTPOINT ||
					fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL) {
				if (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL)
					inode->i_op = &nfs_referral_inode_operations;
				else
					inode->i_op = &nfs_mountpoint_inode_operations;
				inode->i_fop = NULL;
				inode->i_flags |= S_AUTOMOUNT;
			}
		} else if (S_ISLNK(inode->i_mode))
			inode->i_op = &nfs_symlink_inode_operations;
		else
			init_special_inode(inode, inode->i_mode, fattr->rdev);

		memset(&inode->i_atime, 0, sizeof(inode->i_atime));
		memset(&inode->i_mtime, 0, sizeof(inode->i_mtime));
		memset(&inode->i_ctime, 0, sizeof(inode->i_ctime));
		inode->i_version = 0;
		inode->i_size = 0;
		clear_nlink(inode);
		inode->i_uid = make_kuid(&init_user_ns, -2);
		inode->i_gid = make_kgid(&init_user_ns, -2);
		inode->i_blocks = 0;
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
		nfsi->write_io = 0;
		nfsi->read_io = 0;

		nfsi->read_cache_jiffies = fattr->time_start;
		nfsi->attr_gencount = fattr->gencount;
		if (fattr->valid & NFS_ATTR_FATTR_ATIME)
			inode->i_atime = fattr->atime;
		else if (nfs_server_capable(inode, NFS_CAP_ATIME))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			inode->i_mtime = fattr->mtime;
		else if (nfs_server_capable(inode, NFS_CAP_MTIME))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			inode->i_ctime = fattr->ctime;
		else if (nfs_server_capable(inode, NFS_CAP_CTIME))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		if (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			inode->i_version = fattr->change_attr;
		else if (nfs_server_capable(inode, NFS_CAP_CHANGE_ATTR))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		if (fattr->valid & NFS_ATTR_FATTR_SIZE)
			inode->i_size = nfs_size_to_loff_t(fattr->size);
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR
				| NFS_INO_REVAL_PAGECACHE);
		if (fattr->valid & NFS_ATTR_FATTR_NLINK)
			set_nlink(inode, fattr->nlink);
		else if (nfs_server_capable(inode, NFS_CAP_NLINK))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		if (fattr->valid & NFS_ATTR_FATTR_OWNER)
			inode->i_uid = fattr->uid;
		else if (nfs_server_capable(inode, NFS_CAP_OWNER))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		if (fattr->valid & NFS_ATTR_FATTR_GROUP)
			inode->i_gid = fattr->gid;
		else if (nfs_server_capable(inode, NFS_CAP_OWNER_GROUP))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR);
		if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
			inode->i_blocks = fattr->du.nfs2.blocks;
		if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
			/*
			 * report the blocks in 512byte units
			 */
			inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		}

		nfs_setsecurity(inode, fattr, label);

		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = now;
		nfsi->access_cache = RB_ROOT;

		nfs_fscache_init_inode(inode);

		unlock_new_inode(inode);
	} else
		nfs_refresh_inode(inode, fattr);
	dprintk("NFS: nfs_fhget(%s/%Lu fh_crc=0x%08x ct=%d)\n",
		inode->i_sb->s_id,
		(unsigned long long)NFS_FILEID(inode),
		nfs_display_fhandle_hash(fh),
		atomic_read(&inode->i_count));

out:
	return inode;

out_no_inode:
	dprintk("nfs_fhget: iget failed with error %ld\n", PTR_ERR(inode));
	goto out;
}
EXPORT_SYMBOL_GPL(nfs_fhget);

#define NFS_VALID_ATTRS (ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_SIZE|ATTR_ATIME|ATTR_ATIME_SET|ATTR_MTIME|ATTR_MTIME_SET|ATTR_FILE|ATTR_OPEN)

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
		loff_t i_size;

		BUG_ON(!S_ISREG(inode->i_mode));

		i_size = i_size_read(inode);
		if (attr->ia_size == i_size)
			attr->ia_valid &= ~ATTR_SIZE;
		else if (attr->ia_size < i_size && IS_SWAPFILE(inode))
			return -ETXTBSY;
	}

	/* Optimization: if the end result is no change, don't RPC */
	attr->ia_valid &= NFS_VALID_ATTRS;
	if ((attr->ia_valid & ~(ATTR_FILE|ATTR_OPEN)) == 0)
		return 0;

	trace_nfs_setattr_enter(inode);

	/* Write all dirty data */
	if (S_ISREG(inode->i_mode)) {
		nfs_inode_dio_wait(inode);
		nfs_wb_all(inode);
	}

	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		goto out;
	/*
	 * Return any delegations if we're going to change ACLs
	 */
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0)
		NFS_PROTO(inode)->return_delegation(inode);
	error = NFS_PROTO(inode)->setattr(dentry, fattr, attr);
	if (error == 0)
		error = nfs_refresh_inode(inode, fattr);
	nfs_free_fattr(fattr);
out:
	trace_nfs_setattr_exit(inode, error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_setattr);

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
	int err;

	err = inode_newsize_ok(inode, offset);
	if (err)
		goto out;

	spin_lock(&inode->i_lock);
	i_size_write(inode, offset);
	/* Optimisation */
	if (offset == 0)
		NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_DATA;
	spin_unlock(&inode->i_lock);

	truncate_pagecache(inode, offset);
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
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL);
		spin_unlock(&inode->i_lock);
	}
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		nfs_inc_stats(inode, NFSIOS_SETATTRTRUNC);
		nfs_vmtruncate(inode, attr->ia_size);
	}
}
EXPORT_SYMBOL_GPL(nfs_setattr_update_inode);

static void nfs_request_parent_use_readdirplus(struct dentry *dentry)
{
	struct dentry *parent;

	parent = dget_parent(dentry);
	nfs_force_use_readdirplus(parent->d_inode);
	dput(parent);
}

static bool nfs_need_revalidate_inode(struct inode *inode)
{
	if (NFS_I(inode)->cache_validity &
			(NFS_INO_INVALID_ATTR|NFS_INO_INVALID_LABEL))
		return true;
	if (nfs_attribute_cache_expired(inode))
		return true;
	return false;
}

int nfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int need_atime = NFS_I(inode)->cache_validity & NFS_INO_INVALID_ATIME;
	int err = 0;

	trace_nfs_getattr_enter(inode);
	/* Flush out writes to the server in order to update c/mtime.  */
	if (S_ISREG(inode->i_mode)) {
		nfs_inode_dio_wait(inode);
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

	if (need_atime || nfs_need_revalidate_inode(inode)) {
		struct nfs_server *server = NFS_SERVER(inode);

		if (server->caps & NFS_CAP_READDIRPLUS)
			nfs_request_parent_use_readdirplus(dentry);
		err = __nfs_revalidate_inode(server, inode);
	}
	if (!err) {
		generic_fillattr(inode, stat);
		stat->ino = nfs_compat_user_ino64(NFS_FILEID(inode));
	}
out:
	trace_nfs_getattr_exit(inode, err);
	return err;
}
EXPORT_SYMBOL_GPL(nfs_getattr);

static void nfs_init_lock_context(struct nfs_lock_context *l_ctx)
{
	atomic_set(&l_ctx->count, 1);
	l_ctx->lockowner.l_owner = current->files;
	l_ctx->lockowner.l_pid = current->tgid;
	INIT_LIST_HEAD(&l_ctx->list);
	nfs_iocounter_init(&l_ctx->io_count);
}

static struct nfs_lock_context *__nfs_find_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *head = &ctx->lock_context;
	struct nfs_lock_context *pos = head;

	do {
		if (pos->lockowner.l_owner != current->files)
			continue;
		if (pos->lockowner.l_pid != current->tgid)
			continue;
		atomic_inc(&pos->count);
		return pos;
	} while ((pos = list_entry(pos->list.next, typeof(*pos), list)) != head);
	return NULL;
}

struct nfs_lock_context *nfs_get_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *res, *new = NULL;
	struct inode *inode = ctx->dentry->d_inode;

	spin_lock(&inode->i_lock);
	res = __nfs_find_lock_context(ctx);
	if (res == NULL) {
		spin_unlock(&inode->i_lock);
		new = kmalloc(sizeof(*new), GFP_KERNEL);
		if (new == NULL)
			return ERR_PTR(-ENOMEM);
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
EXPORT_SYMBOL_GPL(nfs_get_lock_context);

void nfs_put_lock_context(struct nfs_lock_context *l_ctx)
{
	struct nfs_open_context *ctx = l_ctx->open_context;
	struct inode *inode = ctx->dentry->d_inode;

	if (!atomic_dec_and_lock(&l_ctx->count, &inode->i_lock))
		return;
	list_del(&l_ctx->list);
	spin_unlock(&inode->i_lock);
	kfree(l_ctx);
}
EXPORT_SYMBOL_GPL(nfs_put_lock_context);

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
	inode = ctx->dentry->d_inode;
	if (!list_empty(&NFS_I(inode)->open_files))
		return;
	server = NFS_SERVER(inode);
	if (server->flags & NFS_MOUNT_NOCTO)
		return;
	nfs_revalidate_inode(server, inode);
}
EXPORT_SYMBOL_GPL(nfs_close_context);

struct nfs_open_context *alloc_nfs_open_context(struct dentry *dentry, fmode_t f_mode)
{
	struct nfs_open_context *ctx;
	struct rpc_cred *cred = rpc_lookup_cred();
	if (IS_ERR(cred))
		return ERR_CAST(cred);

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		put_rpccred(cred);
		return ERR_PTR(-ENOMEM);
	}
	nfs_sb_active(dentry->d_sb);
	ctx->dentry = dget(dentry);
	ctx->cred = cred;
	ctx->state = NULL;
	ctx->mode = f_mode;
	ctx->flags = 0;
	ctx->error = 0;
	nfs_init_lock_context(&ctx->lock_context);
	ctx->lock_context.open_context = ctx;
	INIT_LIST_HEAD(&ctx->list);
	ctx->mdsthreshold = NULL;
	return ctx;
}
EXPORT_SYMBOL_GPL(alloc_nfs_open_context);

struct nfs_open_context *get_nfs_open_context(struct nfs_open_context *ctx)
{
	if (ctx != NULL)
		atomic_inc(&ctx->lock_context.count);
	return ctx;
}
EXPORT_SYMBOL_GPL(get_nfs_open_context);

static void __put_nfs_open_context(struct nfs_open_context *ctx, int is_sync)
{
	struct inode *inode = ctx->dentry->d_inode;
	struct super_block *sb = ctx->dentry->d_sb;

	if (!list_empty(&ctx->list)) {
		if (!atomic_dec_and_lock(&ctx->lock_context.count, &inode->i_lock))
			return;
		list_del(&ctx->list);
		spin_unlock(&inode->i_lock);
	} else if (!atomic_dec_and_test(&ctx->lock_context.count))
		return;
	if (inode != NULL)
		NFS_PROTO(inode)->close_context(ctx, is_sync);
	if (ctx->cred != NULL)
		put_rpccred(ctx->cred);
	dput(ctx->dentry);
	nfs_sb_deactive(sb);
	kfree(ctx->mdsthreshold);
	kfree(ctx);
}

void put_nfs_open_context(struct nfs_open_context *ctx)
{
	__put_nfs_open_context(ctx, 0);
}
EXPORT_SYMBOL_GPL(put_nfs_open_context);

/*
 * Ensure that mmap has a recent RPC credential for use when writing out
 * shared pages
 */
void nfs_inode_attach_open_context(struct nfs_open_context *ctx)
{
	struct inode *inode = ctx->dentry->d_inode;
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	list_add(&ctx->list, &nfsi->open_files);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_inode_attach_open_context);

void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx)
{
	filp->private_data = get_nfs_open_context(ctx);
	if (list_empty(&ctx->list))
		nfs_inode_attach_open_context(ctx);
}
EXPORT_SYMBOL_GPL(nfs_file_set_open_context);

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
	struct nfs_open_context *ctx = nfs_file_open_context(filp);

	if (ctx) {
		struct inode *inode = ctx->dentry->d_inode;

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

	ctx = alloc_nfs_open_context(filp->f_path.dentry, filp->f_mode);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	nfs_fscache_open_file(inode, filp);
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
	struct nfs4_label *label = NULL;
	struct nfs_fattr *fattr = NULL;
	struct nfs_inode *nfsi = NFS_I(inode);

	dfprintk(PAGECACHE, "NFS: revalidating (%s/%Lu)\n",
		inode->i_sb->s_id, (unsigned long long)NFS_FILEID(inode));

	trace_nfs_revalidate_inode_enter(inode);

	if (is_bad_inode(inode))
		goto out;
	if (NFS_STALE(inode))
		goto out;

	status = -ENOMEM;
	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		goto out;

	nfs_inc_stats(inode, NFSIOS_INODEREVALIDATE);

	label = nfs4_label_alloc(NFS_SERVER(inode), GFP_KERNEL);
	if (IS_ERR(label)) {
		status = PTR_ERR(label);
		goto out;
	}

	status = NFS_PROTO(inode)->getattr(server, NFS_FH(inode), fattr, label);
	if (status != 0) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Lu) getattr failed, error=%d\n",
			 inode->i_sb->s_id,
			 (unsigned long long)NFS_FILEID(inode), status);
		if (status == -ESTALE) {
			nfs_zap_caches(inode);
			if (!S_ISDIR(inode->i_mode))
				set_bit(NFS_INO_STALE, &NFS_I(inode)->flags);
		}
		goto err_out;
	}

	status = nfs_refresh_inode(inode, fattr);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Lu) refresh failed, error=%d\n",
			 inode->i_sb->s_id,
			 (unsigned long long)NFS_FILEID(inode), status);
		goto err_out;
	}

	if (nfsi->cache_validity & NFS_INO_INVALID_ACL)
		nfs_zap_acl_cache(inode);

	nfs_setsecurity(inode, fattr, label);

	dfprintk(PAGECACHE, "NFS: (%s/%Lu) revalidation complete\n",
		inode->i_sb->s_id,
		(unsigned long long)NFS_FILEID(inode));

err_out:
	nfs4_label_free(label);
out:
	nfs_free_fattr(fattr);
	trace_nfs_revalidate_inode_exit(inode, status);
	return status;
}

int nfs_attribute_timeout(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	return !time_in_range_open(jiffies, nfsi->read_cache_jiffies, nfsi->read_cache_jiffies + nfsi->attrtimeo);
}

int nfs_attribute_cache_expired(struct inode *inode)
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
	if (!nfs_need_revalidate_inode(inode))
		return NFS_STALE(inode) ? -ESTALE : 0;
	return __nfs_revalidate_inode(server, inode);
}
EXPORT_SYMBOL_GPL(nfs_revalidate_inode);

int nfs_revalidate_inode_rcu(struct nfs_server *server, struct inode *inode)
{
	if (!(NFS_I(inode)->cache_validity &
			(NFS_INO_INVALID_ATTR|NFS_INO_INVALID_LABEL))
			&& !nfs_attribute_cache_expired(inode))
		return NFS_STALE(inode) ? -ESTALE : 0;
	return -ECHILD;
}

static int nfs_invalidate_mapping(struct inode *inode, struct address_space *mapping)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	int ret;

	if (mapping->nrpages != 0) {
		if (S_ISREG(inode->i_mode)) {
			ret = nfs_sync_mapping(mapping);
			if (ret < 0)
				return ret;
		}
		ret = invalidate_inode_pages2(mapping);
		if (ret < 0)
			return ret;
	}
	if (S_ISDIR(inode->i_mode)) {
		spin_lock(&inode->i_lock);
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
		spin_unlock(&inode->i_lock);
	}
	nfs_inc_stats(inode, NFSIOS_DATAINVALIDATE);
	nfs_fscache_wait_on_invalidate(inode);

	dfprintk(PAGECACHE, "NFS: (%s/%Lu) data cache invalidated\n",
			inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(inode));
	return 0;
}

static bool nfs_mapping_need_revalidate_inode(struct inode *inode)
{
	if (nfs_have_delegated_attributes(inode))
		return false;
	return (NFS_I(inode)->cache_validity & NFS_INO_REVAL_PAGECACHE)
		|| nfs_attribute_timeout(inode)
		|| NFS_STALE(inode);
}

/**
 * nfs_revalidate_mapping - Revalidate the pagecache
 * @inode - pointer to host inode
 * @mapping - pointer to mapping
 */
int nfs_revalidate_mapping(struct inode *inode, struct address_space *mapping)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	unsigned long *bitlock = &nfsi->flags;
	int ret = 0;

	/* swapfiles are not supposed to be shared. */
	if (IS_SWAPFILE(inode))
		goto out;

	if (nfs_mapping_need_revalidate_inode(inode)) {
		ret = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
		if (ret < 0)
			goto out;
	}

	/*
	 * We must clear NFS_INO_INVALID_DATA first to ensure that
	 * invalidations that come in while we're shooting down the mappings
	 * are respected. But, that leaves a race window where one revalidator
	 * can clear the flag, and then another checks it before the mapping
	 * gets invalidated. Fix that by serializing access to this part of
	 * the function.
	 *
	 * At the same time, we need to allow other tasks to see whether we
	 * might be in the middle of invalidating the pages, so we only set
	 * the bit lock here if it looks like we're going to be doing that.
	 */
	for (;;) {
		ret = wait_on_bit_action(bitlock, NFS_INO_INVALIDATING,
					 nfs_wait_bit_killable, TASK_KILLABLE);
		if (ret)
			goto out;
		spin_lock(&inode->i_lock);
		if (test_bit(NFS_INO_INVALIDATING, bitlock)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
			break;
		spin_unlock(&inode->i_lock);
		goto out;
	}

	set_bit(NFS_INO_INVALIDATING, bitlock);
	smp_wmb();
	nfsi->cache_validity &= ~NFS_INO_INVALID_DATA;
	spin_unlock(&inode->i_lock);
	trace_nfs_invalidate_mapping_enter(inode);
	ret = nfs_invalidate_mapping(inode, mapping);
	trace_nfs_invalidate_mapping_exit(inode, ret);

	clear_bit_unlock(NFS_INO_INVALIDATING, bitlock);
	smp_mb__after_atomic();
	wake_up_bit(bitlock, NFS_INO_INVALIDATING);
out:
	return ret;
}

static unsigned long nfs_wcc_update_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	unsigned long ret = 0;

	if ((fattr->valid & NFS_ATTR_FATTR_PRECHANGE)
			&& (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			&& inode->i_version == fattr->pre_change_attr) {
		inode->i_version = fattr->change_attr;
		if (S_ISDIR(inode->i_mode))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_DATA);
		ret |= NFS_INO_INVALID_ATTR;
	}
	/* If we have atomic WCC data, we may update some attributes */
	if ((fattr->valid & NFS_ATTR_FATTR_PRECTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_CTIME)
			&& timespec_equal(&inode->i_ctime, &fattr->pre_ctime)) {
		memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));
		ret |= NFS_INO_INVALID_ATTR;
	}

	if ((fattr->valid & NFS_ATTR_FATTR_PREMTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_MTIME)
			&& timespec_equal(&inode->i_mtime, &fattr->pre_mtime)) {
		memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
		if (S_ISDIR(inode->i_mode))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_DATA);
		ret |= NFS_INO_INVALID_ATTR;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_PRESIZE)
			&& (fattr->valid & NFS_ATTR_FATTR_SIZE)
			&& i_size_read(inode) == nfs_size_to_loff_t(fattr->pre_size)
			&& nfsi->nrequests == 0) {
		i_size_write(inode, nfs_size_to_loff_t(fattr->size));
		ret |= NFS_INO_INVALID_ATTR;
	}

	return ret;
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


	if (nfs_have_delegated_attributes(inode))
		return 0;
	/* Has the inode gone and changed behind our back? */
	if ((fattr->valid & NFS_ATTR_FATTR_FILEID) && nfsi->fileid != fattr->fileid)
		return -EIO;
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && (inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		return -EIO;

	if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
			inode->i_version != fattr->change_attr)
		invalid |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;

	/* Verify a few of the more important attributes */
	if ((fattr->valid & NFS_ATTR_FATTR_MTIME) && !timespec_equal(&inode->i_mtime, &fattr->mtime))
		invalid |= NFS_INO_INVALID_ATTR;

	if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
		cur_size = i_size_read(inode);
		new_isize = nfs_size_to_loff_t(fattr->size);
		if (cur_size != new_isize && nfsi->nrequests == 0)
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;
	}

	/* Have any file permissions changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_MODE) && (inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO))
		invalid |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;
	if ((fattr->valid & NFS_ATTR_FATTR_OWNER) && !uid_eq(inode->i_uid, fattr->uid))
		invalid |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;
	if ((fattr->valid & NFS_ATTR_FATTR_GROUP) && !gid_eq(inode->i_gid, fattr->gid))
		invalid |= NFS_INO_INVALID_ATTR | NFS_INO_INVALID_ACCESS | NFS_INO_INVALID_ACL;

	/* Has the link count changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_NLINK) && inode->i_nlink != fattr->nlink)
		invalid |= NFS_INO_INVALID_ATTR;

	if ((fattr->valid & NFS_ATTR_FATTR_ATIME) && !timespec_equal(&inode->i_atime, &fattr->atime))
		invalid |= NFS_INO_INVALID_ATIME;

	if (invalid != 0)
		nfs_set_cache_invalid(inode, invalid);

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
	fattr->owner_name = NULL;
	fattr->group_name = NULL;
}
EXPORT_SYMBOL_GPL(nfs_fattr_init);

struct nfs_fattr *nfs_alloc_fattr(void)
{
	struct nfs_fattr *fattr;

	fattr = kmalloc(sizeof(*fattr), GFP_NOFS);
	if (fattr != NULL)
		nfs_fattr_init(fattr);
	return fattr;
}
EXPORT_SYMBOL_GPL(nfs_alloc_fattr);

struct nfs_fh *nfs_alloc_fhandle(void)
{
	struct nfs_fh *fh;

	fh = kmalloc(sizeof(struct nfs_fh), GFP_NOFS);
	if (fh != NULL)
		fh->size = 0;
	return fh;
}
EXPORT_SYMBOL_GPL(nfs_alloc_fhandle);

#ifdef NFS_DEBUG
/*
 * _nfs_display_fhandle_hash - calculate the crc32 hash for the filehandle
 *                             in the same way that wireshark does
 *
 * @fh: file handle
 *
 * For debugging only.
 */
u32 _nfs_display_fhandle_hash(const struct nfs_fh *fh)
{
	/* wireshark uses 32-bit AUTODIN crc and does a bitwise
	 * not on the result */
	return nfs_fhandle_hash(fh);
}
EXPORT_SYMBOL_GPL(_nfs_display_fhandle_hash);

/*
 * _nfs_display_fhandle - display an NFS file handle on the console
 *
 * @fh: file handle to display
 * @caption: display caption
 *
 * For debugging only.
 */
void _nfs_display_fhandle(const struct nfs_fh *fh, const char *caption)
{
	unsigned short i;

	if (fh == NULL || fh->size == 0) {
		printk(KERN_DEFAULT "%s at %p is empty\n", caption, fh);
		return;
	}

	printk(KERN_DEFAULT "%s at %p is %u bytes, crc: 0x%08x:\n",
	       caption, fh, fh->size, _nfs_display_fhandle_hash(fh));
	for (i = 0; i < fh->size; i += 16) {
		__be32 *pos = (__be32 *)&fh->data[i];

		switch ((fh->size - i - 1) >> 2) {
		case 0:
			printk(KERN_DEFAULT " %08x\n",
				be32_to_cpup(pos));
			break;
		case 1:
			printk(KERN_DEFAULT " %08x %08x\n",
				be32_to_cpup(pos), be32_to_cpup(pos + 1));
			break;
		case 2:
			printk(KERN_DEFAULT " %08x %08x %08x\n",
				be32_to_cpup(pos), be32_to_cpup(pos + 1),
				be32_to_cpup(pos + 2));
			break;
		default:
			printk(KERN_DEFAULT " %08x %08x %08x %08x\n",
				be32_to_cpup(pos), be32_to_cpup(pos + 1),
				be32_to_cpup(pos + 2), be32_to_cpup(pos + 3));
		}
	}
}
EXPORT_SYMBOL_GPL(_nfs_display_fhandle);
#endif

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

/*
 * Don't trust the change_attribute, mtime, ctime or size if
 * a pnfs LAYOUTCOMMIT is outstanding
 */
static void nfs_inode_attrs_handle_layoutcommit(struct inode *inode,
		struct nfs_fattr *fattr)
{
	if (pnfs_layoutcommit_outstanding(inode))
		fattr->valid &= ~(NFS_ATTR_FATTR_CHANGE |
				NFS_ATTR_FATTR_MTIME |
				NFS_ATTR_FATTR_CTIME |
				NFS_ATTR_FATTR_SIZE);
}

static int nfs_refresh_inode_locked(struct inode *inode, struct nfs_fattr *fattr)
{
	int ret;

	trace_nfs_refresh_inode_enter(inode);

	nfs_inode_attrs_handle_layoutcommit(inode, fattr);

	if (nfs_inode_attrs_need_update(inode, fattr))
		ret = nfs_update_inode(inode, fattr);
	else
		ret = nfs_check_inode_attributes(inode, fattr);

	trace_nfs_refresh_inode_exit(inode, ret);
	return ret;
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
EXPORT_SYMBOL_GPL(nfs_refresh_inode);

static int nfs_post_op_update_inode_locked(struct inode *inode, struct nfs_fattr *fattr)
{
	unsigned long invalid = NFS_INO_INVALID_ATTR|NFS_INO_REVAL_PAGECACHE;

	if (S_ISDIR(inode->i_mode))
		invalid |= NFS_INO_INVALID_DATA;
	nfs_set_cache_invalid(inode, invalid);
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
EXPORT_SYMBOL_GPL(nfs_post_op_update_inode);

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
		fattr->pre_change_attr = inode->i_version;
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
EXPORT_SYMBOL_GPL(nfs_post_op_update_inode_force_wcc);

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

	dfprintk(VFS, "NFS: %s(%s/%lu fh_crc=0x%08x ct=%d info=0x%x)\n",
			__func__, inode->i_sb->s_id, inode->i_ino,
			nfs_display_fhandle_hash(NFS_FH(inode)),
			atomic_read(&inode->i_count), fattr->valid);

	if ((fattr->valid & NFS_ATTR_FATTR_FILEID) && nfsi->fileid != fattr->fileid) {
		printk(KERN_ERR "NFS: server %s error: fileid changed\n"
			"fsid %s: expected fileid 0x%Lx, got 0x%Lx\n",
			NFS_SERVER(inode)->nfs_client->cl_hostname,
			inode->i_sb->s_id, (long long)nfsi->fileid,
			(long long)fattr->fileid);
		goto out_err;
	}

	/*
	 * Make sure the inode's type hasn't changed.
	 */
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && (inode->i_mode & S_IFMT) != (fattr->mode & S_IFMT)) {
		/*
		* Big trouble! The inode has become a different object.
		*/
		printk(KERN_DEBUG "NFS: %s: inode %lu mode changed, %07o to %07o\n",
				__func__, inode->i_ino, inode->i_mode, fattr->mode);
		goto out_err;
	}

	server = NFS_SERVER(inode);
	/* Update the fsid? */
	if (S_ISDIR(inode->i_mode) && (fattr->valid & NFS_ATTR_FATTR_FSID) &&
			!nfs_fsid_equal(&server->fsid, &fattr->fsid) &&
			!IS_AUTOMOUNT(inode))
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
	invalid |= nfs_wcc_update_inode(inode, fattr);

	/* More cache consistency checks */
	if (fattr->valid & NFS_ATTR_FATTR_CHANGE) {
		if (inode->i_version != fattr->change_attr) {
			dprintk("NFS: change_attr change on server for file %s/%ld\n",
					inode->i_sb->s_id, inode->i_ino);
			invalid |= NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_DATA
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_PAGECACHE;
			if (S_ISDIR(inode->i_mode))
				nfs_force_lookup_revalidate(inode);
			inode->i_version = fattr->change_attr;
		}
	} else if (server->caps & NFS_CAP_CHANGE_ATTR)
		nfsi->cache_validity |= save_cache_validity;

	if (fattr->valid & NFS_ATTR_FATTR_MTIME) {
		memcpy(&inode->i_mtime, &fattr->mtime, sizeof(inode->i_mtime));
	} else if (server->caps & NFS_CAP_MTIME)
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATTR
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_CTIME) {
		memcpy(&inode->i_ctime, &fattr->ctime, sizeof(inode->i_ctime));
	} else if (server->caps & NFS_CAP_CTIME)
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATTR
				| NFS_INO_REVAL_FORCED);

	/* Check if our cached file size is stale */
	if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
		new_isize = nfs_size_to_loff_t(fattr->size);
		cur_isize = i_size_read(inode);
		if (new_isize != cur_isize) {
			/* Do we perhaps have any outstanding writes, or has
			 * the file grown beyond our last write? */
			if ((nfsi->nrequests == 0) || new_isize > cur_isize) {
				i_size_write(inode, new_isize);
				invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_DATA;
				invalid &= ~NFS_INO_REVAL_PAGECACHE;
			}
			dprintk("NFS: isize change on server for file %s/%ld "
					"(%Ld to %Ld)\n",
					inode->i_sb->s_id,
					inode->i_ino,
					(long long)cur_isize,
					(long long)new_isize);
		}
	} else
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATTR
				| NFS_INO_REVAL_PAGECACHE
				| NFS_INO_REVAL_FORCED);


	if (fattr->valid & NFS_ATTR_FATTR_ATIME)
		memcpy(&inode->i_atime, &fattr->atime, sizeof(inode->i_atime));
	else if (server->caps & NFS_CAP_ATIME)
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATIME
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_MODE) {
		if ((inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO)) {
			umode_t newmode = inode->i_mode & S_IFMT;
			newmode |= fattr->mode & S_IALLUGO;
			inode->i_mode = newmode;
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
		}
	} else if (server->caps & NFS_CAP_MODE)
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_OWNER) {
		if (!uid_eq(inode->i_uid, fattr->uid)) {
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
			inode->i_uid = fattr->uid;
		}
	} else if (server->caps & NFS_CAP_OWNER)
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_GROUP) {
		if (!gid_eq(inode->i_gid, fattr->gid)) {
			invalid |= NFS_INO_INVALID_ATTR|NFS_INO_INVALID_ACCESS|NFS_INO_INVALID_ACL;
			inode->i_gid = fattr->gid;
		}
	} else if (server->caps & NFS_CAP_OWNER_GROUP)
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATTR
				| NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL
				| NFS_INO_REVAL_FORCED);

	if (fattr->valid & NFS_ATTR_FATTR_NLINK) {
		if (inode->i_nlink != fattr->nlink) {
			invalid |= NFS_INO_INVALID_ATTR;
			if (S_ISDIR(inode->i_mode))
				invalid |= NFS_INO_INVALID_DATA;
			set_nlink(inode, fattr->nlink);
		}
	} else if (server->caps & NFS_CAP_NLINK)
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATTR
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
	if (!NFS_PROTO(inode)->have_delegation(inode, FMODE_READ) ||
			(save_cache_validity & NFS_INO_REVAL_FORCED))
		nfs_set_cache_invalid(inode, invalid);

	return 0;
 out_err:
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 * (But we fall through to invalidate the caches.)
	 */
	nfs_invalidate_inode(inode);
	return -ESTALE;
}

struct inode *nfs_alloc_inode(struct super_block *sb)
{
	struct nfs_inode *nfsi;
	nfsi = (struct nfs_inode *)kmem_cache_alloc(nfs_inode_cachep, GFP_KERNEL);
	if (!nfsi)
		return NULL;
	nfsi->flags = 0UL;
	nfsi->cache_validity = 0UL;
#if IS_ENABLED(CONFIG_NFS_V4)
	nfsi->nfs4_acl = NULL;
#endif /* CONFIG_NFS_V4 */
	return &nfsi->vfs_inode;
}
EXPORT_SYMBOL_GPL(nfs_alloc_inode);

static void nfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(nfs_inode_cachep, NFS_I(inode));
}

void nfs_destroy_inode(struct inode *inode)
{
	call_rcu(&inode->i_rcu, nfs_i_callback);
}
EXPORT_SYMBOL_GPL(nfs_destroy_inode);

static inline void nfs4_init_once(struct nfs_inode *nfsi)
{
#if IS_ENABLED(CONFIG_NFS_V4)
	INIT_LIST_HEAD(&nfsi->open_states);
	nfsi->delegation = NULL;
	nfsi->delegation_state = 0;
	init_rwsem(&nfsi->rwsem);
	nfsi->layout = NULL;
#endif
}

static void init_once(void *foo)
{
	struct nfs_inode *nfsi = (struct nfs_inode *) foo;

	inode_init_once(&nfsi->vfs_inode);
	INIT_LIST_HEAD(&nfsi->open_files);
	INIT_LIST_HEAD(&nfsi->access_cache_entry_lru);
	INIT_LIST_HEAD(&nfsi->access_cache_inode_lru);
	INIT_LIST_HEAD(&nfsi->commit_info.list);
	nfsi->nrequests = 0;
	nfsi->commit_info.ncommit = 0;
	atomic_set(&nfsi->commit_info.rpcs_out, 0);
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
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(nfs_inode_cachep);
}

struct workqueue_struct *nfsiod_workqueue;
EXPORT_SYMBOL_GPL(nfsiod_workqueue);

/*
 * start up the nfsiod workqueue
 */
static int nfsiod_start(void)
{
	struct workqueue_struct *wq;
	dprintk("RPC:       creating workqueue nfsiod\n");
	wq = alloc_workqueue("nfsiod", WQ_MEM_RECLAIM, 0);
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

int nfs_net_id;
EXPORT_SYMBOL_GPL(nfs_net_id);

static int nfs_net_init(struct net *net)
{
	nfs_clients_init(net);
	return nfs_fs_proc_net_init(net);
}

static void nfs_net_exit(struct net *net)
{
	nfs_fs_proc_net_exit(net);
	nfs_cleanup_cb_ident_idr(net);
}

static struct pernet_operations nfs_net_ops = {
	.init = nfs_net_init,
	.exit = nfs_net_exit,
	.id   = &nfs_net_id,
	.size = sizeof(struct nfs_net),
};

/*
 * Initialize NFS
 */
static int __init init_nfs_fs(void)
{
	int err;

	err = register_pernet_subsys(&nfs_net_ops);
	if (err < 0)
		goto out9;

	err = nfs_fscache_register();
	if (err < 0)
		goto out8;

	err = nfsiod_start();
	if (err)
		goto out7;

	err = nfs_fs_proc_init();
	if (err)
		goto out6;

	err = nfs_init_nfspagecache();
	if (err)
		goto out5;

	err = nfs_init_inodecache();
	if (err)
		goto out4;

	err = nfs_init_readpagecache();
	if (err)
		goto out3;

	err = nfs_init_writepagecache();
	if (err)
		goto out2;

	err = nfs_init_directcache();
	if (err)
		goto out1;

#ifdef CONFIG_PROC_FS
	rpc_proc_register(&init_net, &nfs_rpcstat);
#endif
	if ((err = register_nfs_fs()) != 0)
		goto out0;

	return 0;
out0:
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister(&init_net, "nfs");
#endif
	nfs_destroy_directcache();
out1:
	nfs_destroy_writepagecache();
out2:
	nfs_destroy_readpagecache();
out3:
	nfs_destroy_inodecache();
out4:
	nfs_destroy_nfspagecache();
out5:
	nfs_fs_proc_exit();
out6:
	nfsiod_stop();
out7:
	nfs_fscache_unregister();
out8:
	unregister_pernet_subsys(&nfs_net_ops);
out9:
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
	unregister_pernet_subsys(&nfs_net_ops);
#ifdef CONFIG_PROC_FS
	rpc_proc_unregister(&init_net, "nfs");
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
