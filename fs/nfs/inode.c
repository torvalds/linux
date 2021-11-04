// SPDX-License-Identifier: GPL-2.0-only
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
#include <linux/sched/signal.h>
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
#include <linux/uaccess.h>
#include <linux/iversion.h>

#include "nfs4_fs.h"
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
#include "fscache.h"
#include "pnfs.h"
#include "nfs.h"
#include "netns.h"
#include "sysfs.h"

#include "nfstrace.h"

#define NFSDBG_FACILITY		NFSDBG_VFS

#define NFS_64_BIT_INODE_NUMBERS_ENABLED	1

/* Default is to see 64-bit inode numbers */
static bool enable_ino64 = NFS_64_BIT_INODE_NUMBERS_ENABLED;

static int nfs_update_inode(struct inode *, struct nfs_fattr *);

static struct kmem_cache * nfs_inode_cachep;

static inline unsigned long
nfs_fattr_to_ino_t(struct nfs_fattr *fattr)
{
	return nfs_fileid_to_ino_t(fattr->fileid);
}

static int nfs_wait_killable(int mode)
{
	freezable_schedule_unsafe();
	if (signal_pending_state(mode, current))
		return -ERESTARTSYS;
	return 0;
}

int nfs_wait_bit_killable(struct wait_bit_key *key, int mode)
{
	return nfs_wait_killable(mode);
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

int nfs_sync_inode(struct inode *inode)
{
	inode_dio_wait(inode);
	return nfs_wb_all(inode);
}
EXPORT_SYMBOL_GPL(nfs_sync_inode);

/**
 * nfs_sync_mapping - helper to flush all mmapped dirty data to disk
 * @mapping: pointer to struct address_space
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

static int nfs_attribute_timeout(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);

	return !time_in_range_open(jiffies, nfsi->read_cache_jiffies, nfsi->read_cache_jiffies + nfsi->attrtimeo);
}

static bool nfs_check_cache_flags_invalid(struct inode *inode,
					  unsigned long flags)
{
	unsigned long cache_validity = READ_ONCE(NFS_I(inode)->cache_validity);

	return (cache_validity & flags) != 0;
}

bool nfs_check_cache_invalid(struct inode *inode, unsigned long flags)
{
	if (nfs_check_cache_flags_invalid(inode, flags))
		return true;
	return nfs_attribute_cache_expired(inode);
}
EXPORT_SYMBOL_GPL(nfs_check_cache_invalid);

#ifdef CONFIG_NFS_V4_2
static bool nfs_has_xattr_cache(const struct nfs_inode *nfsi)
{
	return nfsi->xattr_cache != NULL;
}
#else
static bool nfs_has_xattr_cache(const struct nfs_inode *nfsi)
{
	return false;
}
#endif

void nfs_set_cache_invalid(struct inode *inode, unsigned long flags)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	bool have_delegation = NFS_PROTO(inode)->have_delegation(inode, FMODE_READ);

	if (have_delegation) {
		if (!(flags & NFS_INO_REVAL_FORCED))
			flags &= ~(NFS_INO_INVALID_MODE |
				   NFS_INO_INVALID_OTHER |
				   NFS_INO_INVALID_XATTR);
		flags &= ~(NFS_INO_INVALID_CHANGE | NFS_INO_INVALID_SIZE);
	} else if (flags & NFS_INO_REVAL_PAGECACHE)
		flags |= NFS_INO_INVALID_CHANGE | NFS_INO_INVALID_SIZE;

	if (!nfs_has_xattr_cache(nfsi))
		flags &= ~NFS_INO_INVALID_XATTR;
	if (flags & NFS_INO_INVALID_DATA)
		nfs_fscache_invalidate(inode);
	flags &= ~(NFS_INO_REVAL_PAGECACHE | NFS_INO_REVAL_FORCED);

	nfsi->cache_validity |= flags;

	if (inode->i_mapping->nrpages == 0)
		nfsi->cache_validity &= ~(NFS_INO_INVALID_DATA |
					  NFS_INO_DATA_INVAL_DEFER);
	else if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
		nfsi->cache_validity &= ~NFS_INO_DATA_INVAL_DEFER;
}
EXPORT_SYMBOL_GPL(nfs_set_cache_invalid);

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

	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)) {
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR
					| NFS_INO_INVALID_DATA
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL
					| NFS_INO_INVALID_XATTR
					| NFS_INO_REVAL_PAGECACHE);
	} else
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATTR
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL
					| NFS_INO_INVALID_XATTR
					| NFS_INO_REVAL_PAGECACHE);
	nfs_zap_label_cache_locked(nfsi);
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
static void nfs_set_inode_stale_locked(struct inode *inode)
{
	set_bit(NFS_INO_STALE, &NFS_I(inode)->flags);
	nfs_zap_caches_locked(inode);
	trace_nfs_set_inode_stale(inode);
}

void nfs_set_inode_stale(struct inode *inode)
{
	spin_lock(&inode->i_lock);
	nfs_set_inode_stale_locked(inode);
	spin_unlock(&inode->i_lock);
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
	if (inode_wrong_type(inode, fattr->mode))
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
	inode->i_mode = fattr->mode;
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

/* Search for inode identified by fh, fileid and i_mode in inode cache. */
struct inode *
nfs_ilookup(struct super_block *sb, struct nfs_fattr *fattr, struct nfs_fh *fh)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr,
	};
	struct inode *inode;
	unsigned long hash;

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID) ||
	    !(fattr->valid & NFS_ATTR_FATTR_TYPE))
		return NULL;

	hash = nfs_fattr_to_ino_t(fattr);
	inode = ilookup5(sb, hash, nfs_find_actor, &desc);

	dprintk("%s: returning %p\n", __func__, inode);
	return inode;
}

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
	u64 fattr_supported = NFS_SB(sb)->fattr_valid;
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
		nfsi->cache_validity = 0;
		if ((fattr->valid & NFS_ATTR_FATTR_MODE) == 0
				&& (fattr_supported & NFS_ATTR_FATTR_MODE))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_MODE);
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
		} else if (S_ISLNK(inode->i_mode)) {
			inode->i_op = &nfs_symlink_inode_operations;
			inode_nohighmem(inode);
		} else
			init_special_inode(inode, inode->i_mode, fattr->rdev);

		memset(&inode->i_atime, 0, sizeof(inode->i_atime));
		memset(&inode->i_mtime, 0, sizeof(inode->i_mtime));
		memset(&inode->i_ctime, 0, sizeof(inode->i_ctime));
		inode_set_iversion_raw(inode, 0);
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
		else if (fattr_supported & NFS_ATTR_FATTR_ATIME)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATIME);
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			inode->i_mtime = fattr->mtime;
		else if (fattr_supported & NFS_ATTR_FATTR_MTIME)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_MTIME);
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			inode->i_ctime = fattr->ctime;
		else if (fattr_supported & NFS_ATTR_FATTR_CTIME)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			inode_set_iversion_raw(inode, fattr->change_attr);
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_CHANGE);
		if (fattr->valid & NFS_ATTR_FATTR_SIZE)
			inode->i_size = nfs_size_to_loff_t(fattr->size);
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_SIZE);
		if (fattr->valid & NFS_ATTR_FATTR_NLINK)
			set_nlink(inode, fattr->nlink);
		else if (fattr_supported & NFS_ATTR_FATTR_NLINK)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_NLINK);
		if (fattr->valid & NFS_ATTR_FATTR_OWNER)
			inode->i_uid = fattr->uid;
		else if (fattr_supported & NFS_ATTR_FATTR_OWNER)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_OTHER);
		if (fattr->valid & NFS_ATTR_FATTR_GROUP)
			inode->i_gid = fattr->gid;
		else if (fattr_supported & NFS_ATTR_FATTR_GROUP)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_OTHER);
		if (nfs_server_capable(inode, NFS_CAP_XATTR))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_XATTR);
		if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
			inode->i_blocks = fattr->du.nfs2.blocks;
		else if (fattr_supported & NFS_ATTR_FATTR_BLOCKS_USED &&
			 fattr->size != 0)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_BLOCKS);
		if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
			/*
			 * report the blocks in 512byte units
			 */
			inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		} else if (fattr_supported & NFS_ATTR_FATTR_SPACE_USED &&
			   fattr->size != 0)
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_BLOCKS);

		nfs_setsecurity(inode, fattr, label);

		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = now;
		nfsi->access_cache = RB_ROOT;

		nfs_fscache_init_inode(inode);

		unlock_new_inode(inode);
	} else {
		int err = nfs_refresh_inode(inode, fattr);
		if (err < 0) {
			iput(inode);
			inode = ERR_PTR(err);
			goto out_no_inode;
		}
	}
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
nfs_setattr(struct user_namespace *mnt_userns, struct dentry *dentry,
	    struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct nfs_fattr *fattr;
	int error = 0;

	nfs_inc_stats(inode, NFSIOS_VFSSETATTR);

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attr->ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		attr->ia_valid &= ~ATTR_MODE;

	if (attr->ia_valid & ATTR_SIZE) {
		BUG_ON(!S_ISREG(inode->i_mode));

		error = inode_newsize_ok(inode, attr->ia_size);
		if (error)
			return error;

		if (attr->ia_size == i_size_read(inode))
			attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Optimization: if the end result is no change, don't RPC */
	if (((attr->ia_valid & NFS_VALID_ATTRS) & ~(ATTR_FILE|ATTR_OPEN)) == 0)
		return 0;

	trace_nfs_setattr_enter(inode);

	/* Write all dirty data */
	if (S_ISREG(inode->i_mode))
		nfs_sync_inode(inode);

	fattr = nfs_alloc_fattr();
	if (fattr == NULL) {
		error = -ENOMEM;
		goto out;
	}

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
 * Note: must be called with inode->i_lock held!
 */
static int nfs_vmtruncate(struct inode * inode, loff_t offset)
{
	int err;

	err = inode_newsize_ok(inode, offset);
	if (err)
		goto out;

	i_size_write(inode, offset);
	/* Optimisation */
	if (offset == 0)
		NFS_I(inode)->cache_validity &= ~(NFS_INO_INVALID_DATA |
				NFS_INO_DATA_INVAL_DEFER);
	NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_SIZE;

	spin_unlock(&inode->i_lock);
	truncate_pagecache(inode, offset);
	spin_lock(&inode->i_lock);
out:
	return err;
}

/**
 * nfs_setattr_update_inode - Update inode metadata after a setattr call.
 * @inode: pointer to struct inode
 * @attr: pointer to struct iattr
 * @fattr: pointer to struct nfs_fattr
 *
 * Note: we do this in the *proc.c in order to ensure that
 *       it works for things like exclusive creates too.
 */
void nfs_setattr_update_inode(struct inode *inode, struct iattr *attr,
		struct nfs_fattr *fattr)
{
	/* Barrier: bump the attribute generation count. */
	nfs_fattr_set_barrier(fattr);

	spin_lock(&inode->i_lock);
	NFS_I(inode)->attr_gencount = fattr->gencount;
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_MTIME |
						     NFS_INO_INVALID_BLOCKS);
		nfs_inc_stats(inode, NFSIOS_SETATTRTRUNC);
		nfs_vmtruncate(inode, attr->ia_size);
	}
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0) {
		NFS_I(inode)->cache_validity &= ~NFS_INO_INVALID_CTIME;
		if ((attr->ia_valid & ATTR_KILL_SUID) != 0 &&
		    inode->i_mode & S_ISUID)
			inode->i_mode &= ~S_ISUID;
		if ((attr->ia_valid & ATTR_KILL_SGID) != 0 &&
		    (inode->i_mode & (S_ISGID | S_IXGRP)) ==
		     (S_ISGID | S_IXGRP))
			inode->i_mode &= ~S_ISGID;
		if ((attr->ia_valid & ATTR_MODE) != 0) {
			int mode = attr->ia_mode & S_IALLUGO;
			mode |= inode->i_mode & ~S_IALLUGO;
			inode->i_mode = mode;
		}
		if ((attr->ia_valid & ATTR_UID) != 0)
			inode->i_uid = attr->ia_uid;
		if ((attr->ia_valid & ATTR_GID) != 0)
			inode->i_gid = attr->ia_gid;
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			inode->i_ctime = fattr->ctime;
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_CHANGE
					| NFS_INO_INVALID_CTIME);
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL);
	}
	if (attr->ia_valid & (ATTR_ATIME_SET|ATTR_ATIME)) {
		NFS_I(inode)->cache_validity &= ~(NFS_INO_INVALID_ATIME
				| NFS_INO_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_ATIME)
			inode->i_atime = fattr->atime;
		else if (attr->ia_valid & ATTR_ATIME_SET)
			inode->i_atime = attr->ia_atime;
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_ATIME);

		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			inode->i_ctime = fattr->ctime;
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_CHANGE
					| NFS_INO_INVALID_CTIME);
	}
	if (attr->ia_valid & (ATTR_MTIME_SET|ATTR_MTIME)) {
		NFS_I(inode)->cache_validity &= ~(NFS_INO_INVALID_MTIME
				| NFS_INO_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			inode->i_mtime = fattr->mtime;
		else if (attr->ia_valid & ATTR_MTIME_SET)
			inode->i_mtime = attr->ia_mtime;
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_MTIME);

		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			inode->i_ctime = fattr->ctime;
		else
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_CHANGE
					| NFS_INO_INVALID_CTIME);
	}
	if (fattr->valid)
		nfs_update_inode(inode, fattr);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_setattr_update_inode);

static void nfs_readdirplus_parent_cache_miss(struct dentry *dentry)
{
	struct dentry *parent;

	if (!nfs_server_capable(d_inode(dentry), NFS_CAP_READDIRPLUS))
		return;
	parent = dget_parent(dentry);
	nfs_force_use_readdirplus(d_inode(parent));
	dput(parent);
}

static void nfs_readdirplus_parent_cache_hit(struct dentry *dentry)
{
	struct dentry *parent;

	if (!nfs_server_capable(d_inode(dentry), NFS_CAP_READDIRPLUS))
		return;
	parent = dget_parent(dentry);
	nfs_advise_use_readdirplus(d_inode(parent));
	dput(parent);
}

static u32 nfs_get_valid_attrmask(struct inode *inode)
{
	unsigned long cache_validity = READ_ONCE(NFS_I(inode)->cache_validity);
	u32 reply_mask = STATX_INO | STATX_TYPE;

	if (!(cache_validity & NFS_INO_INVALID_ATIME))
		reply_mask |= STATX_ATIME;
	if (!(cache_validity & NFS_INO_INVALID_CTIME))
		reply_mask |= STATX_CTIME;
	if (!(cache_validity & NFS_INO_INVALID_MTIME))
		reply_mask |= STATX_MTIME;
	if (!(cache_validity & NFS_INO_INVALID_SIZE))
		reply_mask |= STATX_SIZE;
	if (!(cache_validity & NFS_INO_INVALID_NLINK))
		reply_mask |= STATX_NLINK;
	if (!(cache_validity & NFS_INO_INVALID_MODE))
		reply_mask |= STATX_MODE;
	if (!(cache_validity & NFS_INO_INVALID_OTHER))
		reply_mask |= STATX_UID | STATX_GID;
	if (!(cache_validity & NFS_INO_INVALID_BLOCKS))
		reply_mask |= STATX_BLOCKS;
	return reply_mask;
}

int nfs_getattr(struct user_namespace *mnt_userns, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int query_flags)
{
	struct inode *inode = d_inode(path->dentry);
	struct nfs_server *server = NFS_SERVER(inode);
	unsigned long cache_validity;
	int err = 0;
	bool force_sync = query_flags & AT_STATX_FORCE_SYNC;
	bool do_update = false;

	trace_nfs_getattr_enter(inode);

	request_mask &= STATX_TYPE | STATX_MODE | STATX_NLINK | STATX_UID |
			STATX_GID | STATX_ATIME | STATX_MTIME | STATX_CTIME |
			STATX_INO | STATX_SIZE | STATX_BLOCKS;

	if ((query_flags & AT_STATX_DONT_SYNC) && !force_sync) {
		nfs_readdirplus_parent_cache_hit(path->dentry);
		goto out_no_revalidate;
	}

	/* Flush out writes to the server in order to update c/mtime.  */
	if ((request_mask & (STATX_CTIME | STATX_MTIME)) &&
	    S_ISREG(inode->i_mode))
		filemap_write_and_wait(inode->i_mapping);

	/*
	 * We may force a getattr if the user cares about atime.
	 *
	 * Note that we only have to check the vfsmount flags here:
	 *  - NFS always sets S_NOATIME by so checking it would give a
	 *    bogus result
	 *  - NFS never sets SB_NOATIME or SB_NODIRATIME so there is
	 *    no point in checking those.
	 */
	if ((path->mnt->mnt_flags & MNT_NOATIME) ||
	    ((path->mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(inode->i_mode)))
		request_mask &= ~STATX_ATIME;

	/* Is the user requesting attributes that might need revalidation? */
	if (!(request_mask & (STATX_MODE|STATX_NLINK|STATX_ATIME|STATX_CTIME|
					STATX_MTIME|STATX_UID|STATX_GID|
					STATX_SIZE|STATX_BLOCKS)))
		goto out_no_revalidate;

	/* Check whether the cached attributes are stale */
	do_update |= force_sync || nfs_attribute_cache_expired(inode);
	cache_validity = READ_ONCE(NFS_I(inode)->cache_validity);
	do_update |= cache_validity & NFS_INO_INVALID_CHANGE;
	if (request_mask & STATX_ATIME)
		do_update |= cache_validity & NFS_INO_INVALID_ATIME;
	if (request_mask & STATX_CTIME)
		do_update |= cache_validity & NFS_INO_INVALID_CTIME;
	if (request_mask & STATX_MTIME)
		do_update |= cache_validity & NFS_INO_INVALID_MTIME;
	if (request_mask & STATX_SIZE)
		do_update |= cache_validity & NFS_INO_INVALID_SIZE;
	if (request_mask & STATX_NLINK)
		do_update |= cache_validity & NFS_INO_INVALID_NLINK;
	if (request_mask & STATX_MODE)
		do_update |= cache_validity & NFS_INO_INVALID_MODE;
	if (request_mask & (STATX_UID | STATX_GID))
		do_update |= cache_validity & NFS_INO_INVALID_OTHER;
	if (request_mask & STATX_BLOCKS)
		do_update |= cache_validity & NFS_INO_INVALID_BLOCKS;

	if (do_update) {
		/* Update the attribute cache */
		if (!(server->flags & NFS_MOUNT_NOAC))
			nfs_readdirplus_parent_cache_miss(path->dentry);
		else
			nfs_readdirplus_parent_cache_hit(path->dentry);
		err = __nfs_revalidate_inode(server, inode);
		if (err)
			goto out;
	} else
		nfs_readdirplus_parent_cache_hit(path->dentry);
out_no_revalidate:
	/* Only return attributes that were revalidated. */
	stat->result_mask = nfs_get_valid_attrmask(inode) | request_mask;

	generic_fillattr(&init_user_ns, inode, stat);
	stat->ino = nfs_compat_user_ino64(NFS_FILEID(inode));
	if (S_ISDIR(inode->i_mode))
		stat->blksize = NFS_SERVER(inode)->dtsize;
out:
	trace_nfs_getattr_exit(inode, err);
	return err;
}
EXPORT_SYMBOL_GPL(nfs_getattr);

static void nfs_init_lock_context(struct nfs_lock_context *l_ctx)
{
	refcount_set(&l_ctx->count, 1);
	l_ctx->lockowner = current->files;
	INIT_LIST_HEAD(&l_ctx->list);
	atomic_set(&l_ctx->io_count, 0);
}

static struct nfs_lock_context *__nfs_find_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *pos;

	list_for_each_entry_rcu(pos, &ctx->lock_context.list, list) {
		if (pos->lockowner != current->files)
			continue;
		if (refcount_inc_not_zero(&pos->count))
			return pos;
	}
	return NULL;
}

struct nfs_lock_context *nfs_get_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *res, *new = NULL;
	struct inode *inode = d_inode(ctx->dentry);

	rcu_read_lock();
	res = __nfs_find_lock_context(ctx);
	rcu_read_unlock();
	if (res == NULL) {
		new = kmalloc(sizeof(*new), GFP_KERNEL);
		if (new == NULL)
			return ERR_PTR(-ENOMEM);
		nfs_init_lock_context(new);
		spin_lock(&inode->i_lock);
		res = __nfs_find_lock_context(ctx);
		if (res == NULL) {
			new->open_context = get_nfs_open_context(ctx);
			if (new->open_context) {
				list_add_tail_rcu(&new->list,
						&ctx->lock_context.list);
				res = new;
				new = NULL;
			} else
				res = ERR_PTR(-EBADF);
		}
		spin_unlock(&inode->i_lock);
		kfree(new);
	}
	return res;
}
EXPORT_SYMBOL_GPL(nfs_get_lock_context);

void nfs_put_lock_context(struct nfs_lock_context *l_ctx)
{
	struct nfs_open_context *ctx = l_ctx->open_context;
	struct inode *inode = d_inode(ctx->dentry);

	if (!refcount_dec_and_lock(&l_ctx->count, &inode->i_lock))
		return;
	list_del_rcu(&l_ctx->list);
	spin_unlock(&inode->i_lock);
	put_nfs_open_context(ctx);
	kfree_rcu(l_ctx, rcu_head);
}
EXPORT_SYMBOL_GPL(nfs_put_lock_context);

/**
 * nfs_close_context - Common close_context() routine NFSv2/v3
 * @ctx: pointer to context
 * @is_sync: is this a synchronous close
 *
 * Ensure that the attributes are up to date if we're mounted
 * with close-to-open semantics and we have cached data that will
 * need to be revalidated on open.
 */
void nfs_close_context(struct nfs_open_context *ctx, int is_sync)
{
	struct nfs_inode *nfsi;
	struct inode *inode;

	if (!(ctx->mode & FMODE_WRITE))
		return;
	if (!is_sync)
		return;
	inode = d_inode(ctx->dentry);
	if (NFS_PROTO(inode)->have_delegation(inode, FMODE_READ))
		return;
	nfsi = NFS_I(inode);
	if (inode->i_mapping->nrpages == 0)
		return;
	if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
		return;
	if (!list_empty(&nfsi->open_files))
		return;
	if (NFS_SERVER(inode)->flags & NFS_MOUNT_NOCTO)
		return;
	nfs_revalidate_inode(inode,
			     NFS_INO_INVALID_CHANGE | NFS_INO_INVALID_SIZE);
}
EXPORT_SYMBOL_GPL(nfs_close_context);

struct nfs_open_context *alloc_nfs_open_context(struct dentry *dentry,
						fmode_t f_mode,
						struct file *filp)
{
	struct nfs_open_context *ctx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);
	nfs_sb_active(dentry->d_sb);
	ctx->dentry = dget(dentry);
	if (filp)
		ctx->cred = get_cred(filp->f_cred);
	else
		ctx->cred = get_current_cred();
	ctx->ll_cred = NULL;
	ctx->state = NULL;
	ctx->mode = f_mode;
	ctx->flags = 0;
	ctx->error = 0;
	ctx->flock_owner = (fl_owner_t)filp;
	nfs_init_lock_context(&ctx->lock_context);
	ctx->lock_context.open_context = ctx;
	INIT_LIST_HEAD(&ctx->list);
	ctx->mdsthreshold = NULL;
	return ctx;
}
EXPORT_SYMBOL_GPL(alloc_nfs_open_context);

struct nfs_open_context *get_nfs_open_context(struct nfs_open_context *ctx)
{
	if (ctx != NULL && refcount_inc_not_zero(&ctx->lock_context.count))
		return ctx;
	return NULL;
}
EXPORT_SYMBOL_GPL(get_nfs_open_context);

static void __put_nfs_open_context(struct nfs_open_context *ctx, int is_sync)
{
	struct inode *inode = d_inode(ctx->dentry);
	struct super_block *sb = ctx->dentry->d_sb;

	if (!refcount_dec_and_test(&ctx->lock_context.count))
		return;
	if (!list_empty(&ctx->list)) {
		spin_lock(&inode->i_lock);
		list_del_rcu(&ctx->list);
		spin_unlock(&inode->i_lock);
	}
	if (inode != NULL)
		NFS_PROTO(inode)->close_context(ctx, is_sync);
	put_cred(ctx->cred);
	dput(ctx->dentry);
	nfs_sb_deactive(sb);
	put_rpccred(ctx->ll_cred);
	kfree(ctx->mdsthreshold);
	kfree_rcu(ctx, rcu_head);
}

void put_nfs_open_context(struct nfs_open_context *ctx)
{
	__put_nfs_open_context(ctx, 0);
}
EXPORT_SYMBOL_GPL(put_nfs_open_context);

static void put_nfs_open_context_sync(struct nfs_open_context *ctx)
{
	__put_nfs_open_context(ctx, 1);
}

/*
 * Ensure that mmap has a recent RPC credential for use when writing out
 * shared pages
 */
void nfs_inode_attach_open_context(struct nfs_open_context *ctx)
{
	struct inode *inode = d_inode(ctx->dentry);
	struct nfs_inode *nfsi = NFS_I(inode);

	spin_lock(&inode->i_lock);
	if (list_empty(&nfsi->open_files) &&
	    (nfsi->cache_validity & NFS_INO_DATA_INVAL_DEFER))
		nfs_set_cache_invalid(inode, NFS_INO_INVALID_DATA |
						     NFS_INO_REVAL_FORCED);
	list_add_tail_rcu(&ctx->list, &nfsi->open_files);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_inode_attach_open_context);

void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx)
{
	filp->private_data = get_nfs_open_context(ctx);
	set_bit(NFS_CONTEXT_FILE_OPEN, &ctx->flags);
	if (list_empty(&ctx->list))
		nfs_inode_attach_open_context(ctx);
}
EXPORT_SYMBOL_GPL(nfs_file_set_open_context);

/*
 * Given an inode, search for an open context with the desired characteristics
 */
struct nfs_open_context *nfs_find_open_context(struct inode *inode, const struct cred *cred, fmode_t mode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	struct nfs_open_context *pos, *ctx = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(pos, &nfsi->open_files, list) {
		if (cred != NULL && cred_fscmp(pos->cred, cred) != 0)
			continue;
		if ((pos->mode & (FMODE_READ|FMODE_WRITE)) != mode)
			continue;
		if (!test_bit(NFS_CONTEXT_FILE_OPEN, &pos->flags))
			continue;
		ctx = get_nfs_open_context(pos);
		if (ctx)
			break;
	}
	rcu_read_unlock();
	return ctx;
}

void nfs_file_clear_open_context(struct file *filp)
{
	struct nfs_open_context *ctx = nfs_file_open_context(filp);

	if (ctx) {
		struct inode *inode = d_inode(ctx->dentry);

		clear_bit(NFS_CONTEXT_FILE_OPEN, &ctx->flags);
		/*
		 * We fatal error on write before. Try to writeback
		 * every page again.
		 */
		if (ctx->error < 0)
			invalidate_inode_pages2(inode->i_mapping);
		filp->private_data = NULL;
		put_nfs_open_context_sync(ctx);
	}
}

/*
 * These allocate and release file read/write context information.
 */
int nfs_open(struct inode *inode, struct file *filp)
{
	struct nfs_open_context *ctx;

	ctx = alloc_nfs_open_context(file_dentry(filp), filp->f_mode, filp);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	nfs_fscache_open_file(inode, filp);
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

	/* pNFS: Attributes aren't updated until we layoutcommit */
	if (S_ISREG(inode->i_mode)) {
		status = pnfs_sync_inode(inode, false);
		if (status)
			goto out;
	}

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

	status = NFS_PROTO(inode)->getattr(server, NFS_FH(inode), fattr,
			label, inode);
	if (status != 0) {
		dfprintk(PAGECACHE, "nfs_revalidate_inode: (%s/%Lu) getattr failed, error=%d\n",
			 inode->i_sb->s_id,
			 (unsigned long long)NFS_FILEID(inode), status);
		switch (status) {
		case -ETIMEDOUT:
			/* A soft timeout occurred. Use cached information? */
			if (server->flags & NFS_MOUNT_SOFTREVAL)
				status = 0;
			break;
		case -ESTALE:
			if (!S_ISDIR(inode->i_mode))
				nfs_set_inode_stale(inode);
			else
				nfs_zap_caches(inode);
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

int nfs_attribute_cache_expired(struct inode *inode)
{
	if (nfs_have_delegated_attributes(inode))
		return 0;
	return nfs_attribute_timeout(inode);
}

/**
 * nfs_revalidate_inode - Revalidate the inode attributes
 * @inode: pointer to inode struct
 * @flags: cache flags to check
 *
 * Updates inode attribute information by retrieving the data from the server.
 */
int nfs_revalidate_inode(struct inode *inode, unsigned long flags)
{
	if (!nfs_check_cache_invalid(inode, flags))
		return NFS_STALE(inode) ? -ESTALE : 0;
	return __nfs_revalidate_inode(NFS_SERVER(inode), inode);
}
EXPORT_SYMBOL_GPL(nfs_revalidate_inode);

static int nfs_invalidate_mapping(struct inode *inode, struct address_space *mapping)
{
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
	nfs_inc_stats(inode, NFSIOS_DATAINVALIDATE);
	nfs_fscache_wait_on_invalidate(inode);

	dfprintk(PAGECACHE, "NFS: (%s/%Lu) data cache invalidated\n",
			inode->i_sb->s_id,
			(unsigned long long)NFS_FILEID(inode));
	return 0;
}

/**
 * nfs_clear_invalid_mapping - Conditionally clear a mapping
 * @mapping: pointer to mapping
 *
 * If the NFS_INO_INVALID_DATA inode flag is set, clear the mapping.
 */
int nfs_clear_invalid_mapping(struct address_space *mapping)
{
	struct inode *inode = mapping->host;
	struct nfs_inode *nfsi = NFS_I(inode);
	unsigned long *bitlock = &nfsi->flags;
	int ret = 0;

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
	nfsi->cache_validity &=
		~(NFS_INO_INVALID_DATA | NFS_INO_DATA_INVAL_DEFER);
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

bool nfs_mapping_need_revalidate_inode(struct inode *inode)
{
	return nfs_check_cache_invalid(inode, NFS_INO_INVALID_CHANGE) ||
		NFS_STALE(inode);
}

int nfs_revalidate_mapping_rcu(struct inode *inode)
{
	struct nfs_inode *nfsi = NFS_I(inode);
	unsigned long *bitlock = &nfsi->flags;
	int ret = 0;

	if (IS_SWAPFILE(inode))
		goto out;
	if (nfs_mapping_need_revalidate_inode(inode)) {
		ret = -ECHILD;
		goto out;
	}
	spin_lock(&inode->i_lock);
	if (test_bit(NFS_INO_INVALIDATING, bitlock) ||
	    (nfsi->cache_validity & NFS_INO_INVALID_DATA))
		ret = -ECHILD;
	spin_unlock(&inode->i_lock);
out:
	return ret;
}

/**
 * nfs_revalidate_mapping - Revalidate the pagecache
 * @inode: pointer to host inode
 * @mapping: pointer to mapping
 */
int nfs_revalidate_mapping(struct inode *inode, struct address_space *mapping)
{
	/* swapfiles are not supposed to be shared. */
	if (IS_SWAPFILE(inode))
		return 0;

	if (nfs_mapping_need_revalidate_inode(inode)) {
		int ret = __nfs_revalidate_inode(NFS_SERVER(inode), inode);
		if (ret < 0)
			return ret;
	}

	return nfs_clear_invalid_mapping(mapping);
}

static bool nfs_file_has_writers(struct nfs_inode *nfsi)
{
	struct inode *inode = &nfsi->vfs_inode;

	if (!S_ISREG(inode->i_mode))
		return false;
	if (list_empty(&nfsi->open_files))
		return false;
	return inode_is_open_for_write(inode);
}

static bool nfs_file_has_buffered_writers(struct nfs_inode *nfsi)
{
	return nfs_file_has_writers(nfsi) && nfs_file_io_is_buffered(nfsi);
}

static void nfs_wcc_update_inode(struct inode *inode, struct nfs_fattr *fattr)
{
	struct timespec64 ts;

	if ((fattr->valid & NFS_ATTR_FATTR_PRECHANGE)
			&& (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			&& inode_eq_iversion_raw(inode, fattr->pre_change_attr)) {
		inode_set_iversion_raw(inode, fattr->change_attr);
		if (S_ISDIR(inode->i_mode))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_DATA);
		else if (nfs_server_capable(inode, NFS_CAP_XATTR))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_XATTR);
	}
	/* If we have atomic WCC data, we may update some attributes */
	ts = inode->i_ctime;
	if ((fattr->valid & NFS_ATTR_FATTR_PRECTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_CTIME)
			&& timespec64_equal(&ts, &fattr->pre_ctime)) {
		inode->i_ctime = fattr->ctime;
	}

	ts = inode->i_mtime;
	if ((fattr->valid & NFS_ATTR_FATTR_PREMTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_MTIME)
			&& timespec64_equal(&ts, &fattr->pre_mtime)) {
		inode->i_mtime = fattr->mtime;
		if (S_ISDIR(inode->i_mode))
			nfs_set_cache_invalid(inode, NFS_INO_INVALID_DATA);
	}
	if ((fattr->valid & NFS_ATTR_FATTR_PRESIZE)
			&& (fattr->valid & NFS_ATTR_FATTR_SIZE)
			&& i_size_read(inode) == nfs_size_to_loff_t(fattr->pre_size)
			&& !nfs_have_writebacks(inode)) {
		i_size_write(inode, nfs_size_to_loff_t(fattr->size));
	}
}

/**
 * nfs_check_inode_attributes - verify consistency of the inode attribute cache
 * @inode: pointer to inode
 * @fattr: updated attributes
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
	struct timespec64 ts;

	if (NFS_PROTO(inode)->have_delegation(inode, FMODE_READ))
		return 0;

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID)) {
		/* Only a mounted-on-fileid? Just exit */
		if (fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID)
			return 0;
	/* Has the inode gone and changed behind our back? */
	} else if (nfsi->fileid != fattr->fileid) {
		/* Is this perhaps the mounted-on fileid? */
		if ((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) &&
		    nfsi->fileid == fattr->mounted_on_fileid)
			return 0;
		return -ESTALE;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && inode_wrong_type(inode, fattr->mode))
		return -ESTALE;


	if (!nfs_file_has_buffered_writers(nfsi)) {
		/* Verify a few of the more important attributes */
		if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 && !inode_eq_iversion_raw(inode, fattr->change_attr))
			invalid |= NFS_INO_INVALID_CHANGE;

		ts = inode->i_mtime;
		if ((fattr->valid & NFS_ATTR_FATTR_MTIME) && !timespec64_equal(&ts, &fattr->mtime))
			invalid |= NFS_INO_INVALID_MTIME;

		ts = inode->i_ctime;
		if ((fattr->valid & NFS_ATTR_FATTR_CTIME) && !timespec64_equal(&ts, &fattr->ctime))
			invalid |= NFS_INO_INVALID_CTIME;

		if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
			cur_size = i_size_read(inode);
			new_isize = nfs_size_to_loff_t(fattr->size);
			if (cur_size != new_isize)
				invalid |= NFS_INO_INVALID_SIZE;
		}
	}

	/* Have any file permissions changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_MODE) && (inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO))
		invalid |= NFS_INO_INVALID_MODE;
	if ((fattr->valid & NFS_ATTR_FATTR_OWNER) && !uid_eq(inode->i_uid, fattr->uid))
		invalid |= NFS_INO_INVALID_OTHER;
	if ((fattr->valid & NFS_ATTR_FATTR_GROUP) && !gid_eq(inode->i_gid, fattr->gid))
		invalid |= NFS_INO_INVALID_OTHER;

	/* Has the link count changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_NLINK) && inode->i_nlink != fattr->nlink)
		invalid |= NFS_INO_INVALID_NLINK;

	ts = inode->i_atime;
	if ((fattr->valid & NFS_ATTR_FATTR_ATIME) && !timespec64_equal(&ts, &fattr->atime))
		invalid |= NFS_INO_INVALID_ATIME;

	if (invalid != 0)
		nfs_set_cache_invalid(inode, invalid);

	nfsi->read_cache_jiffies = fattr->time_start;
	return 0;
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
EXPORT_SYMBOL_GPL(nfs_inc_attr_generation_counter);

void nfs_fattr_init(struct nfs_fattr *fattr)
{
	fattr->valid = 0;
	fattr->time_start = jiffies;
	fattr->gencount = nfs_inc_attr_generation_counter();
	fattr->owner_name = NULL;
	fattr->group_name = NULL;
}
EXPORT_SYMBOL_GPL(nfs_fattr_init);

/**
 * nfs_fattr_set_barrier
 * @fattr: attributes
 *
 * Used to set a barrier after an attribute was updated. This
 * barrier ensures that older attributes from RPC calls that may
 * have raced with our update cannot clobber these new values.
 * Note that you are still responsible for ensuring that other
 * operations which change the attribute on the server do not
 * collide.
 */
void nfs_fattr_set_barrier(struct nfs_fattr *fattr)
{
	fattr->gencount = nfs_inc_attr_generation_counter();
}

struct nfs_fattr *nfs_alloc_fattr(void)
{
	struct nfs_fattr *fattr;

	fattr = kmalloc(sizeof(*fattr), GFP_NOFS);
	if (fattr != NULL) {
		nfs_fattr_init(fattr);
		fattr->label = NULL;
	}
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
 * nfs_inode_attrs_cmp_generic - compare attributes
 * @fattr: attributes
 * @inode: pointer to inode
 *
 * Attempt to divine whether or not an RPC call reply carrying stale
 * attributes got scheduled after another call carrying updated ones.
 * Note also the check for wraparound of 'attr_gencount'
 *
 * The function returns '1' if it thinks the attributes in @fattr are
 * more recent than the ones cached in @inode. Otherwise it returns
 * the value '0'.
 */
static int nfs_inode_attrs_cmp_generic(const struct nfs_fattr *fattr,
				       const struct inode *inode)
{
	unsigned long attr_gencount = NFS_I(inode)->attr_gencount;

	return (long)(fattr->gencount - attr_gencount) > 0 ||
	       (long)(attr_gencount - nfs_read_attr_generation_counter()) > 0;
}

/**
 * nfs_inode_attrs_cmp_monotonic - compare attributes
 * @fattr: attributes
 * @inode: pointer to inode
 *
 * Attempt to divine whether or not an RPC call reply carrying stale
 * attributes got scheduled after another call carrying updated ones.
 *
 * We assume that the server observes monotonic semantics for
 * the change attribute, so a larger value means that the attributes in
 * @fattr are more recent, in which case the function returns the
 * value '1'.
 * A return value of '0' indicates no measurable change
 * A return value of '-1' means that the attributes in @inode are
 * more recent.
 */
static int nfs_inode_attrs_cmp_monotonic(const struct nfs_fattr *fattr,
					 const struct inode *inode)
{
	s64 diff = fattr->change_attr - inode_peek_iversion_raw(inode);
	if (diff > 0)
		return 1;
	return diff == 0 ? 0 : -1;
}

/**
 * nfs_inode_attrs_cmp_strict_monotonic - compare attributes
 * @fattr: attributes
 * @inode: pointer to inode
 *
 * Attempt to divine whether or not an RPC call reply carrying stale
 * attributes got scheduled after another call carrying updated ones.
 *
 * We assume that the server observes strictly monotonic semantics for
 * the change attribute, so a larger value means that the attributes in
 * @fattr are more recent, in which case the function returns the
 * value '1'.
 * A return value of '-1' means that the attributes in @inode are
 * more recent or unchanged.
 */
static int nfs_inode_attrs_cmp_strict_monotonic(const struct nfs_fattr *fattr,
						const struct inode *inode)
{
	return  nfs_inode_attrs_cmp_monotonic(fattr, inode) > 0 ? 1 : -1;
}

/**
 * nfs_inode_attrs_cmp - compare attributes
 * @fattr: attributes
 * @inode: pointer to inode
 *
 * This function returns '1' if it thinks the attributes in @fattr are
 * more recent than the ones cached in @inode. It returns '-1' if
 * the attributes in @inode are more recent than the ones in @fattr,
 * and it returns 0 if not sure.
 */
static int nfs_inode_attrs_cmp(const struct nfs_fattr *fattr,
			       const struct inode *inode)
{
	if (nfs_inode_attrs_cmp_generic(fattr, inode) > 0)
		return 1;
	switch (NFS_SERVER(inode)->change_attr_type) {
	case NFS4_CHANGE_TYPE_IS_UNDEFINED:
		break;
	case NFS4_CHANGE_TYPE_IS_TIME_METADATA:
		if (!(fattr->valid & NFS_ATTR_FATTR_CHANGE))
			break;
		return nfs_inode_attrs_cmp_monotonic(fattr, inode);
	default:
		if (!(fattr->valid & NFS_ATTR_FATTR_CHANGE))
			break;
		return nfs_inode_attrs_cmp_strict_monotonic(fattr, inode);
	}
	return 0;
}

/**
 * nfs_inode_finish_partial_attr_update - complete a previous inode update
 * @fattr: attributes
 * @inode: pointer to inode
 *
 * Returns '1' if the last attribute update left the inode cached
 * attributes in a partially unrevalidated state, and @fattr
 * matches the change attribute of that partial update.
 * Otherwise returns '0'.
 */
static int nfs_inode_finish_partial_attr_update(const struct nfs_fattr *fattr,
						const struct inode *inode)
{
	const unsigned long check_valid =
		NFS_INO_INVALID_ATIME | NFS_INO_INVALID_CTIME |
		NFS_INO_INVALID_MTIME | NFS_INO_INVALID_SIZE |
		NFS_INO_INVALID_BLOCKS | NFS_INO_INVALID_OTHER |
		NFS_INO_INVALID_NLINK;
	unsigned long cache_validity = NFS_I(inode)->cache_validity;
	enum nfs4_change_attr_type ctype = NFS_SERVER(inode)->change_attr_type;

	if (ctype != NFS4_CHANGE_TYPE_IS_UNDEFINED &&
	    !(cache_validity & NFS_INO_INVALID_CHANGE) &&
	    (cache_validity & check_valid) != 0 &&
	    (fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
	    nfs_inode_attrs_cmp_monotonic(fattr, inode) == 0)
		return 1;
	return 0;
}

static int nfs_refresh_inode_locked(struct inode *inode,
				    struct nfs_fattr *fattr)
{
	int attr_cmp = nfs_inode_attrs_cmp(fattr, inode);
	int ret = 0;

	trace_nfs_refresh_inode_enter(inode);

	if (attr_cmp > 0 || nfs_inode_finish_partial_attr_update(fattr, inode))
		ret = nfs_update_inode(inode, fattr);
	else if (attr_cmp == 0)
		ret = nfs_check_inode_attributes(inode, fattr);

	trace_nfs_refresh_inode_exit(inode, ret);
	return ret;
}

/**
 * nfs_refresh_inode - try to update the inode attribute cache
 * @inode: pointer to inode
 * @fattr: updated attributes
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

static int nfs_post_op_update_inode_locked(struct inode *inode,
		struct nfs_fattr *fattr, unsigned int invalid)
{
	if (S_ISDIR(inode->i_mode))
		invalid |= NFS_INO_INVALID_DATA;
	nfs_set_cache_invalid(inode, invalid);
	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	return nfs_refresh_inode_locked(inode, fattr);
}

/**
 * nfs_post_op_update_inode - try to update the inode attribute cache
 * @inode: pointer to inode
 * @fattr: updated attributes
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
	nfs_fattr_set_barrier(fattr);
	status = nfs_post_op_update_inode_locked(inode, fattr,
			NFS_INO_INVALID_CHANGE
			| NFS_INO_INVALID_CTIME
			| NFS_INO_REVAL_FORCED);
	spin_unlock(&inode->i_lock);

	return status;
}
EXPORT_SYMBOL_GPL(nfs_post_op_update_inode);

/**
 * nfs_post_op_update_inode_force_wcc_locked - update the inode attribute cache
 * @inode: pointer to inode
 * @fattr: updated attributes
 *
 * After an operation that has changed the inode metadata, mark the
 * attribute cache as being invalid, then try to update it. Fake up
 * weak cache consistency data, if none exist.
 *
 * This function is mainly designed to be used by the ->write_done() functions.
 */
int nfs_post_op_update_inode_force_wcc_locked(struct inode *inode, struct nfs_fattr *fattr)
{
	int attr_cmp = nfs_inode_attrs_cmp(fattr, inode);
	int status;

	/* Don't do a WCC update if these attributes are already stale */
	if (attr_cmp < 0)
		return 0;
	if ((fattr->valid & NFS_ATTR_FATTR) == 0 || !attr_cmp) {
		fattr->valid &= ~(NFS_ATTR_FATTR_PRECHANGE
				| NFS_ATTR_FATTR_PRESIZE
				| NFS_ATTR_FATTR_PREMTIME
				| NFS_ATTR_FATTR_PRECTIME);
		goto out_noforce;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECHANGE) == 0) {
		fattr->pre_change_attr = inode_peek_iversion_raw(inode);
		fattr->valid |= NFS_ATTR_FATTR_PRECHANGE;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECTIME) == 0) {
		fattr->pre_ctime = inode->i_ctime;
		fattr->valid |= NFS_ATTR_FATTR_PRECTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_MTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PREMTIME) == 0) {
		fattr->pre_mtime = inode->i_mtime;
		fattr->valid |= NFS_ATTR_FATTR_PREMTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_SIZE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRESIZE) == 0) {
		fattr->pre_size = i_size_read(inode);
		fattr->valid |= NFS_ATTR_FATTR_PRESIZE;
	}
out_noforce:
	status = nfs_post_op_update_inode_locked(inode, fattr,
			NFS_INO_INVALID_CHANGE
			| NFS_INO_INVALID_CTIME
			| NFS_INO_INVALID_MTIME
			| NFS_INO_INVALID_BLOCKS);
	return status;
}

/**
 * nfs_post_op_update_inode_force_wcc - try to update the inode attribute cache
 * @inode: pointer to inode
 * @fattr: updated attributes
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
	nfs_fattr_set_barrier(fattr);
	status = nfs_post_op_update_inode_force_wcc_locked(inode, fattr);
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
	struct nfs_server *server = NFS_SERVER(inode);
	struct nfs_inode *nfsi = NFS_I(inode);
	loff_t cur_isize, new_isize;
	u64 fattr_supported = server->fattr_valid;
	unsigned long invalid = 0;
	unsigned long now = jiffies;
	unsigned long save_cache_validity;
	bool have_writers = nfs_file_has_buffered_writers(nfsi);
	bool cache_revalidated = true;
	bool attr_changed = false;
	bool have_delegation;

	dfprintk(VFS, "NFS: %s(%s/%lu fh_crc=0x%08x ct=%d info=0x%x)\n",
			__func__, inode->i_sb->s_id, inode->i_ino,
			nfs_display_fhandle_hash(NFS_FH(inode)),
			atomic_read(&inode->i_count), fattr->valid);

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID)) {
		/* Only a mounted-on-fileid? Just exit */
		if (fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID)
			return 0;
	/* Has the inode gone and changed behind our back? */
	} else if (nfsi->fileid != fattr->fileid) {
		/* Is this perhaps the mounted-on fileid? */
		if ((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) &&
		    nfsi->fileid == fattr->mounted_on_fileid)
			return 0;
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
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && inode_wrong_type(inode, fattr->mode)) {
		/*
		* Big trouble! The inode has become a different object.
		*/
		printk(KERN_DEBUG "NFS: %s: inode %lu mode changed, %07o to %07o\n",
				__func__, inode->i_ino, inode->i_mode, fattr->mode);
		goto out_err;
	}

	/* Update the fsid? */
	if (S_ISDIR(inode->i_mode) && (fattr->valid & NFS_ATTR_FATTR_FSID) &&
			!nfs_fsid_equal(&server->fsid, &fattr->fsid) &&
			!IS_AUTOMOUNT(inode))
		server->fsid = fattr->fsid;

	/* Save the delegation state before clearing cache_validity */
	have_delegation = nfs_have_delegated_attributes(inode);

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	nfsi->read_cache_jiffies = fattr->time_start;

	save_cache_validity = nfsi->cache_validity;
	nfsi->cache_validity &= ~(NFS_INO_INVALID_ATTR
			| NFS_INO_INVALID_ATIME
			| NFS_INO_REVAL_FORCED
			| NFS_INO_INVALID_BLOCKS);

	/* Do atomic weak cache consistency updates */
	nfs_wcc_update_inode(inode, fattr);

	if (pnfs_layoutcommit_outstanding(inode)) {
		nfsi->cache_validity |=
			save_cache_validity &
			(NFS_INO_INVALID_CHANGE | NFS_INO_INVALID_CTIME |
			 NFS_INO_INVALID_MTIME | NFS_INO_INVALID_SIZE |
			 NFS_INO_INVALID_BLOCKS);
		cache_revalidated = false;
	}

	/* More cache consistency checks */
	if (fattr->valid & NFS_ATTR_FATTR_CHANGE) {
		if (!inode_eq_iversion_raw(inode, fattr->change_attr)) {
			/* Could it be a race with writeback? */
			if (!(have_writers || have_delegation)) {
				invalid |= NFS_INO_INVALID_DATA
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL
					| NFS_INO_INVALID_XATTR;
				/* Force revalidate of all attributes */
				save_cache_validity |= NFS_INO_INVALID_CTIME
					| NFS_INO_INVALID_MTIME
					| NFS_INO_INVALID_SIZE
					| NFS_INO_INVALID_BLOCKS
					| NFS_INO_INVALID_NLINK
					| NFS_INO_INVALID_MODE
					| NFS_INO_INVALID_OTHER;
				if (S_ISDIR(inode->i_mode))
					nfs_force_lookup_revalidate(inode);
				attr_changed = true;
				dprintk("NFS: change_attr change on server for file %s/%ld\n",
						inode->i_sb->s_id,
						inode->i_ino);
			} else if (!have_delegation)
				nfsi->cache_validity |= NFS_INO_DATA_INVAL_DEFER;
			inode_set_iversion_raw(inode, fattr->change_attr);
		}
	} else {
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_CHANGE;
		if (!have_delegation ||
		    (nfsi->cache_validity & NFS_INO_INVALID_CHANGE) != 0)
			cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_MTIME)
		inode->i_mtime = fattr->mtime;
	else if (fattr_supported & NFS_ATTR_FATTR_MTIME)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_MTIME;

	if (fattr->valid & NFS_ATTR_FATTR_CTIME)
		inode->i_ctime = fattr->ctime;
	else if (fattr_supported & NFS_ATTR_FATTR_CTIME)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_CTIME;

	/* Check if our cached file size is stale */
	if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
		new_isize = nfs_size_to_loff_t(fattr->size);
		cur_isize = i_size_read(inode);
		if (new_isize != cur_isize && !have_delegation) {
			/* Do we perhaps have any outstanding writes, or has
			 * the file grown beyond our last write? */
			if (!nfs_have_writebacks(inode) || new_isize > cur_isize) {
				i_size_write(inode, new_isize);
				if (!have_writers)
					invalid |= NFS_INO_INVALID_DATA;
			}
			dprintk("NFS: isize change on server for file %s/%ld "
					"(%Ld to %Ld)\n",
					inode->i_sb->s_id,
					inode->i_ino,
					(long long)cur_isize,
					(long long)new_isize);
		}
		if (new_isize == 0 &&
		    !(fattr->valid & (NFS_ATTR_FATTR_SPACE_USED |
				      NFS_ATTR_FATTR_BLOCKS_USED))) {
			fattr->du.nfs3.used = 0;
			fattr->valid |= NFS_ATTR_FATTR_SPACE_USED;
		}
	} else
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_SIZE;

	if (fattr->valid & NFS_ATTR_FATTR_ATIME)
		inode->i_atime = fattr->atime;
	else if (fattr_supported & NFS_ATTR_FATTR_ATIME)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_ATIME;

	if (fattr->valid & NFS_ATTR_FATTR_MODE) {
		if ((inode->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO)) {
			umode_t newmode = inode->i_mode & S_IFMT;
			newmode |= fattr->mode & S_IALLUGO;
			inode->i_mode = newmode;
			invalid |= NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
		}
	} else if (fattr_supported & NFS_ATTR_FATTR_MODE)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_MODE;

	if (fattr->valid & NFS_ATTR_FATTR_OWNER) {
		if (!uid_eq(inode->i_uid, fattr->uid)) {
			invalid |= NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
			inode->i_uid = fattr->uid;
		}
	} else if (fattr_supported & NFS_ATTR_FATTR_OWNER)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_OTHER;

	if (fattr->valid & NFS_ATTR_FATTR_GROUP) {
		if (!gid_eq(inode->i_gid, fattr->gid)) {
			invalid |= NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
			inode->i_gid = fattr->gid;
		}
	} else if (fattr_supported & NFS_ATTR_FATTR_GROUP)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_OTHER;

	if (fattr->valid & NFS_ATTR_FATTR_NLINK) {
		if (inode->i_nlink != fattr->nlink) {
			if (S_ISDIR(inode->i_mode))
				invalid |= NFS_INO_INVALID_DATA;
			set_nlink(inode, fattr->nlink);
		}
	} else if (fattr_supported & NFS_ATTR_FATTR_NLINK)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_NLINK;

	if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
		/*
		 * report the blocks in 512byte units
		 */
		inode->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
	} else if (fattr_supported & NFS_ATTR_FATTR_SPACE_USED)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_BLOCKS;

	if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
		inode->i_blocks = fattr->du.nfs2.blocks;
	else if (fattr_supported & NFS_ATTR_FATTR_BLOCKS_USED)
		nfsi->cache_validity |=
			save_cache_validity & NFS_INO_INVALID_BLOCKS;

	/* Update attrtimeo value if we're out of the unstable period */
	if (attr_changed) {
		nfs_inc_stats(inode, NFSIOS_ATTRINVALIDATE);
		nfsi->attrtimeo = NFS_MINATTRTIMEO(inode);
		nfsi->attrtimeo_timestamp = now;
		/* Set barrier to be more recent than all outstanding updates */
		nfsi->attr_gencount = nfs_inc_attr_generation_counter();
	} else {
		if (cache_revalidated) {
			if (!time_in_range_open(now, nfsi->attrtimeo_timestamp,
				nfsi->attrtimeo_timestamp + nfsi->attrtimeo)) {
				nfsi->attrtimeo <<= 1;
				if (nfsi->attrtimeo > NFS_MAXATTRTIMEO(inode))
					nfsi->attrtimeo = NFS_MAXATTRTIMEO(inode);
			}
			nfsi->attrtimeo_timestamp = now;
		}
		/* Set the barrier to be more recent than this fattr */
		if ((long)(fattr->gencount - nfsi->attr_gencount) > 0)
			nfsi->attr_gencount = fattr->gencount;
	}

	/* Don't invalidate the data if we were to blame */
	if (!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)
				|| S_ISLNK(inode->i_mode)))
		invalid &= ~NFS_INO_INVALID_DATA;
	nfs_set_cache_invalid(inode, invalid);

	return 0;
 out_err:
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will know that the inode is bad.
	 * (But we fall through to invalidate the caches.)
	 */
	nfs_set_inode_stale_locked(inode);
	return -ESTALE;
}

struct inode *nfs_alloc_inode(struct super_block *sb)
{
	struct nfs_inode *nfsi;
	nfsi = kmem_cache_alloc(nfs_inode_cachep, GFP_KERNEL);
	if (!nfsi)
		return NULL;
	nfsi->flags = 0UL;
	nfsi->cache_validity = 0UL;
#if IS_ENABLED(CONFIG_NFS_V4)
	nfsi->nfs4_acl = NULL;
#endif /* CONFIG_NFS_V4 */
#ifdef CONFIG_NFS_V4_2
	nfsi->xattr_cache = NULL;
#endif
	return &nfsi->vfs_inode;
}
EXPORT_SYMBOL_GPL(nfs_alloc_inode);

void nfs_free_inode(struct inode *inode)
{
	kmem_cache_free(nfs_inode_cachep, NFS_I(inode));
}
EXPORT_SYMBOL_GPL(nfs_free_inode);

static inline void nfs4_init_once(struct nfs_inode *nfsi)
{
#if IS_ENABLED(CONFIG_NFS_V4)
	INIT_LIST_HEAD(&nfsi->open_states);
	nfsi->delegation = NULL;
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
	atomic_long_set(&nfsi->nrequests, 0);
	atomic_long_set(&nfsi->commit_info.ncommit, 0);
	atomic_set(&nfsi->commit_info.rpcs_out, 0);
	init_rwsem(&nfsi->rmdir_sem);
	mutex_init(&nfsi->commit_mutex);
	nfs4_init_once(nfsi);
	nfsi->cache_change_attribute = 0;
}

static int __init nfs_init_inodecache(void)
{
	nfs_inode_cachep = kmem_cache_create("nfs_inode_cache",
					     sizeof(struct nfs_inode),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
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
	wq = alloc_workqueue("nfsiod", WQ_MEM_RECLAIM | WQ_UNBOUND, 0);
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

unsigned int nfs_net_id;
EXPORT_SYMBOL_GPL(nfs_net_id);

static int nfs_net_init(struct net *net)
{
	nfs_clients_init(net);
	return nfs_fs_proc_net_init(net);
}

static void nfs_net_exit(struct net *net)
{
	nfs_fs_proc_net_exit(net);
	nfs_clients_exit(net);
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

	err = nfs_sysfs_init();
	if (err < 0)
		goto out10;

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

	rpc_proc_register(&init_net, &nfs_rpcstat);

	err = register_nfs_fs();
	if (err)
		goto out0;

	return 0;
out0:
	rpc_proc_unregister(&init_net, "nfs");
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
	nfs_sysfs_exit();
out10:
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
	rpc_proc_unregister(&init_net, "nfs");
	unregister_nfs_fs();
	nfs_fs_proc_exit();
	nfsiod_stop();
	nfs_sysfs_exit();
}

/* Not quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");
module_param(enable_ino64, bool, 0644);

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
