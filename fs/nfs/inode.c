// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/nfs/iyesde.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs iyesde and superblock handling functions
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
#include <linux/erryes.h>
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

/* Default is to see 64-bit iyesde numbers */
static bool enable_iyes64 = NFS_64_BIT_INODE_NUMBERS_ENABLED;

static void nfs_invalidate_iyesde(struct iyesde *);
static int nfs_update_iyesde(struct iyesde *, struct nfs_fattr *);

static struct kmem_cache * nfs_iyesde_cachep;

static inline unsigned long
nfs_fattr_to_iyes_t(struct nfs_fattr *fattr)
{
	return nfs_fileid_to_iyes_t(fattr->fileid);
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
 * nfs_compat_user_iyes64 - returns the user-visible iyesde number
 * @fileid: 64-bit fileid
 *
 * This function returns a 32-bit iyesde number if the boot parameter
 * nfs.enable_iyes64 is zero.
 */
u64 nfs_compat_user_iyes64(u64 fileid)
{
#ifdef CONFIG_COMPAT
	compat_ulong_t iyes;
#else	
	unsigned long iyes;
#endif

	if (enable_iyes64)
		return fileid;
	iyes = fileid;
	if (sizeof(iyes) < sizeof(fileid))
		iyes ^= fileid >> (sizeof(fileid)-sizeof(iyes)) * 8;
	return iyes;
}

int nfs_drop_iyesde(struct iyesde *iyesde)
{
	return NFS_STALE(iyesde) || generic_drop_iyesde(iyesde);
}
EXPORT_SYMBOL_GPL(nfs_drop_iyesde);

void nfs_clear_iyesde(struct iyesde *iyesde)
{
	/*
	 * The following should never happen...
	 */
	WARN_ON_ONCE(nfs_have_writebacks(iyesde));
	WARN_ON_ONCE(!list_empty(&NFS_I(iyesde)->open_files));
	nfs_zap_acl_cache(iyesde);
	nfs_access_zap_cache(iyesde);
	nfs_fscache_clear_iyesde(iyesde);
}
EXPORT_SYMBOL_GPL(nfs_clear_iyesde);

void nfs_evict_iyesde(struct iyesde *iyesde)
{
	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);
	nfs_clear_iyesde(iyesde);
}

int nfs_sync_iyesde(struct iyesde *iyesde)
{
	iyesde_dio_wait(iyesde);
	return nfs_wb_all(iyesde);
}
EXPORT_SYMBOL_GPL(nfs_sync_iyesde);

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

static int nfs_attribute_timeout(struct iyesde *iyesde)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);

	return !time_in_range_open(jiffies, nfsi->read_cache_jiffies, nfsi->read_cache_jiffies + nfsi->attrtimeo);
}

static bool nfs_check_cache_invalid_delegated(struct iyesde *iyesde, unsigned long flags)
{
	unsigned long cache_validity = READ_ONCE(NFS_I(iyesde)->cache_validity);

	/* Special case for the pagecache or access cache */
	if (flags == NFS_INO_REVAL_PAGECACHE &&
	    !(cache_validity & NFS_INO_REVAL_FORCED))
		return false;
	return (cache_validity & flags) != 0;
}

static bool nfs_check_cache_invalid_yest_delegated(struct iyesde *iyesde, unsigned long flags)
{
	unsigned long cache_validity = READ_ONCE(NFS_I(iyesde)->cache_validity);

	if ((cache_validity & flags) != 0)
		return true;
	if (nfs_attribute_timeout(iyesde))
		return true;
	return false;
}

bool nfs_check_cache_invalid(struct iyesde *iyesde, unsigned long flags)
{
	if (NFS_PROTO(iyesde)->have_delegation(iyesde, FMODE_READ))
		return nfs_check_cache_invalid_delegated(iyesde, flags);

	return nfs_check_cache_invalid_yest_delegated(iyesde, flags);
}

static void nfs_set_cache_invalid(struct iyesde *iyesde, unsigned long flags)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	bool have_delegation = NFS_PROTO(iyesde)->have_delegation(iyesde, FMODE_READ);

	if (have_delegation) {
		if (!(flags & NFS_INO_REVAL_FORCED))
			flags &= ~NFS_INO_INVALID_OTHER;
		flags &= ~(NFS_INO_INVALID_CHANGE
				| NFS_INO_INVALID_SIZE
				| NFS_INO_REVAL_PAGECACHE);
	}

	if (iyesde->i_mapping->nrpages == 0)
		flags &= ~(NFS_INO_INVALID_DATA|NFS_INO_DATA_INVAL_DEFER);
	nfsi->cache_validity |= flags;
	if (flags & NFS_INO_INVALID_DATA)
		nfs_fscache_invalidate(iyesde);
}

/*
 * Invalidate the local caches
 */
static void nfs_zap_caches_locked(struct iyesde *iyesde)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	int mode = iyesde->i_mode;

	nfs_inc_stats(iyesde, NFSIOS_ATTRINVALIDATE);

	nfsi->attrtimeo = NFS_MINATTRTIMEO(iyesde);
	nfsi->attrtimeo_timestamp = jiffies;

	memset(NFS_I(iyesde)->cookieverf, 0, sizeof(NFS_I(iyesde)->cookieverf));
	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)) {
		nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_ATTR
					| NFS_INO_INVALID_DATA
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL
					| NFS_INO_REVAL_PAGECACHE);
	} else
		nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_ATTR
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL
					| NFS_INO_REVAL_PAGECACHE);
	nfs_zap_label_cache_locked(nfsi);
}

void nfs_zap_caches(struct iyesde *iyesde)
{
	spin_lock(&iyesde->i_lock);
	nfs_zap_caches_locked(iyesde);
	spin_unlock(&iyesde->i_lock);
}

void nfs_zap_mapping(struct iyesde *iyesde, struct address_space *mapping)
{
	if (mapping->nrpages != 0) {
		spin_lock(&iyesde->i_lock);
		nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_DATA);
		spin_unlock(&iyesde->i_lock);
	}
}

void nfs_zap_acl_cache(struct iyesde *iyesde)
{
	void (*clear_acl_cache)(struct iyesde *);

	clear_acl_cache = NFS_PROTO(iyesde)->clear_acl_cache;
	if (clear_acl_cache != NULL)
		clear_acl_cache(iyesde);
	spin_lock(&iyesde->i_lock);
	NFS_I(iyesde)->cache_validity &= ~NFS_INO_INVALID_ACL;
	spin_unlock(&iyesde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_zap_acl_cache);

void nfs_invalidate_atime(struct iyesde *iyesde)
{
	spin_lock(&iyesde->i_lock);
	nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_ATIME);
	spin_unlock(&iyesde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_invalidate_atime);

/*
 * Invalidate, but do yest unhash, the iyesde.
 * NB: must be called with iyesde->i_lock held!
 */
static void nfs_invalidate_iyesde(struct iyesde *iyesde)
{
	set_bit(NFS_INO_STALE, &NFS_I(iyesde)->flags);
	nfs_zap_caches_locked(iyesde);
}

struct nfs_find_desc {
	struct nfs_fh		*fh;
	struct nfs_fattr	*fattr;
};

/*
 * In NFSv3 we can have 64bit iyesde numbers. In order to support
 * this, and re-exported directories (also seen in NFSv2)
 * we are forced to allow 2 different iyesdes to have the same
 * i_iyes.
 */
static int
nfs_find_actor(struct iyesde *iyesde, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fh		*fh = desc->fh;
	struct nfs_fattr	*fattr = desc->fattr;

	if (NFS_FILEID(iyesde) != fattr->fileid)
		return 0;
	if ((S_IFMT & iyesde->i_mode) != (S_IFMT & fattr->mode))
		return 0;
	if (nfs_compare_fh(NFS_FH(iyesde), fh))
		return 0;
	if (is_bad_iyesde(iyesde) || NFS_STALE(iyesde))
		return 0;
	return 1;
}

static int
nfs_init_locked(struct iyesde *iyesde, void *opaque)
{
	struct nfs_find_desc	*desc = (struct nfs_find_desc *)opaque;
	struct nfs_fattr	*fattr = desc->fattr;

	set_nfs_fileid(iyesde, fattr->fileid);
	iyesde->i_mode = fattr->mode;
	nfs_copy_fh(NFS_FH(iyesde), desc->fh);
	return 0;
}

#ifdef CONFIG_NFS_V4_SECURITY_LABEL
static void nfs_clear_label_invalid(struct iyesde *iyesde)
{
	spin_lock(&iyesde->i_lock);
	NFS_I(iyesde)->cache_validity &= ~NFS_INO_INVALID_LABEL;
	spin_unlock(&iyesde->i_lock);
}

void nfs_setsecurity(struct iyesde *iyesde, struct nfs_fattr *fattr,
					struct nfs4_label *label)
{
	int error;

	if (label == NULL)
		return;

	if ((fattr->valid & NFS_ATTR_FATTR_V4_SECURITY_LABEL) && iyesde->i_security) {
		error = security_iyesde_yestifysecctx(iyesde, label->label,
				label->len);
		if (error)
			printk(KERN_ERR "%s() %s %d "
					"security_iyesde_yestifysecctx() %d\n",
					__func__,
					(char *)label->label,
					label->len, error);
		nfs_clear_label_invalid(iyesde);
	}
}

struct nfs4_label *nfs4_label_alloc(struct nfs_server *server, gfp_t flags)
{
	struct nfs4_label *label = NULL;
	int miyesr_version = server->nfs_client->cl_miyesrversion;

	if (miyesr_version < 2)
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
void nfs_setsecurity(struct iyesde *iyesde, struct nfs_fattr *fattr,
					struct nfs4_label *label)
{
}
#endif
EXPORT_SYMBOL_GPL(nfs_setsecurity);

/* Search for iyesde identified by fh, fileid and i_mode in iyesde cache. */
struct iyesde *
nfs_ilookup(struct super_block *sb, struct nfs_fattr *fattr, struct nfs_fh *fh)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr,
	};
	struct iyesde *iyesde;
	unsigned long hash;

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID) ||
	    !(fattr->valid & NFS_ATTR_FATTR_TYPE))
		return NULL;

	hash = nfs_fattr_to_iyes_t(fattr);
	iyesde = ilookup5(sb, hash, nfs_find_actor, &desc);

	dprintk("%s: returning %p\n", __func__, iyesde);
	return iyesde;
}

/*
 * This is our front-end to iget that looks up iyesdes by file handle
 * instead of iyesde number.
 */
struct iyesde *
nfs_fhget(struct super_block *sb, struct nfs_fh *fh, struct nfs_fattr *fattr, struct nfs4_label *label)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr
	};
	struct iyesde *iyesde = ERR_PTR(-ENOENT);
	unsigned long hash;

	nfs_attr_check_mountpoint(sb, fattr);

	if (nfs_attr_use_mounted_on_fileid(fattr))
		fattr->fileid = fattr->mounted_on_fileid;
	else if ((fattr->valid & NFS_ATTR_FATTR_FILEID) == 0)
		goto out_yes_iyesde;
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) == 0)
		goto out_yes_iyesde;

	hash = nfs_fattr_to_iyes_t(fattr);

	iyesde = iget5_locked(sb, hash, nfs_find_actor, nfs_init_locked, &desc);
	if (iyesde == NULL) {
		iyesde = ERR_PTR(-ENOMEM);
		goto out_yes_iyesde;
	}

	if (iyesde->i_state & I_NEW) {
		struct nfs_iyesde *nfsi = NFS_I(iyesde);
		unsigned long yesw = jiffies;

		/* We set i_iyes for the few things that still rely on it,
		 * such as stat(2) */
		iyesde->i_iyes = hash;

		/* We can't support update_atime(), since the server will reset it */
		iyesde->i_flags |= S_NOATIME|S_NOCMTIME;
		iyesde->i_mode = fattr->mode;
		nfsi->cache_validity = 0;
		if ((fattr->valid & NFS_ATTR_FATTR_MODE) == 0
				&& nfs_server_capable(iyesde, NFS_CAP_MODE))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_OTHER);
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_iyesde_operations.
		 */
		iyesde->i_op = NFS_SB(sb)->nfs_client->rpc_ops->file_iyesde_ops;
		if (S_ISREG(iyesde->i_mode)) {
			iyesde->i_fop = NFS_SB(sb)->nfs_client->rpc_ops->file_ops;
			iyesde->i_data.a_ops = &nfs_file_aops;
		} else if (S_ISDIR(iyesde->i_mode)) {
			iyesde->i_op = NFS_SB(sb)->nfs_client->rpc_ops->dir_iyesde_ops;
			iyesde->i_fop = &nfs_dir_operations;
			iyesde->i_data.a_ops = &nfs_dir_aops;
			/* Deal with crossing mountpoints */
			if (fattr->valid & NFS_ATTR_FATTR_MOUNTPOINT ||
					fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL) {
				if (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL)
					iyesde->i_op = &nfs_referral_iyesde_operations;
				else
					iyesde->i_op = &nfs_mountpoint_iyesde_operations;
				iyesde->i_fop = NULL;
				iyesde->i_flags |= S_AUTOMOUNT;
			}
		} else if (S_ISLNK(iyesde->i_mode)) {
			iyesde->i_op = &nfs_symlink_iyesde_operations;
			iyesde_yeshighmem(iyesde);
		} else
			init_special_iyesde(iyesde, iyesde->i_mode, fattr->rdev);

		memset(&iyesde->i_atime, 0, sizeof(iyesde->i_atime));
		memset(&iyesde->i_mtime, 0, sizeof(iyesde->i_mtime));
		memset(&iyesde->i_ctime, 0, sizeof(iyesde->i_ctime));
		iyesde_set_iversion_raw(iyesde, 0);
		iyesde->i_size = 0;
		clear_nlink(iyesde);
		iyesde->i_uid = make_kuid(&init_user_ns, -2);
		iyesde->i_gid = make_kgid(&init_user_ns, -2);
		iyesde->i_blocks = 0;
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
		nfsi->write_io = 0;
		nfsi->read_io = 0;

		nfsi->read_cache_jiffies = fattr->time_start;
		nfsi->attr_gencount = fattr->gencount;
		if (fattr->valid & NFS_ATTR_FATTR_ATIME)
			iyesde->i_atime = fattr->atime;
		else if (nfs_server_capable(iyesde, NFS_CAP_ATIME))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_ATIME);
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			iyesde->i_mtime = fattr->mtime;
		else if (nfs_server_capable(iyesde, NFS_CAP_MTIME))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_MTIME);
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			iyesde->i_ctime = fattr->ctime;
		else if (nfs_server_capable(iyesde, NFS_CAP_CTIME))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			iyesde_set_iversion_raw(iyesde, fattr->change_attr);
		else
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_CHANGE);
		if (fattr->valid & NFS_ATTR_FATTR_SIZE)
			iyesde->i_size = nfs_size_to_loff_t(fattr->size);
		else
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_SIZE);
		if (fattr->valid & NFS_ATTR_FATTR_NLINK)
			set_nlink(iyesde, fattr->nlink);
		else if (nfs_server_capable(iyesde, NFS_CAP_NLINK))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_OTHER);
		if (fattr->valid & NFS_ATTR_FATTR_OWNER)
			iyesde->i_uid = fattr->uid;
		else if (nfs_server_capable(iyesde, NFS_CAP_OWNER))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_OTHER);
		if (fattr->valid & NFS_ATTR_FATTR_GROUP)
			iyesde->i_gid = fattr->gid;
		else if (nfs_server_capable(iyesde, NFS_CAP_OWNER_GROUP))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_OTHER);
		if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
			iyesde->i_blocks = fattr->du.nfs2.blocks;
		if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
			/*
			 * report the blocks in 512byte units
			 */
			iyesde->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		}

		if (nfsi->cache_validity != 0)
			nfsi->cache_validity |= NFS_INO_REVAL_FORCED;

		nfs_setsecurity(iyesde, fattr, label);

		nfsi->attrtimeo = NFS_MINATTRTIMEO(iyesde);
		nfsi->attrtimeo_timestamp = yesw;
		nfsi->access_cache = RB_ROOT;

		nfs_fscache_init_iyesde(iyesde);

		unlock_new_iyesde(iyesde);
	} else {
		int err = nfs_refresh_iyesde(iyesde, fattr);
		if (err < 0) {
			iput(iyesde);
			iyesde = ERR_PTR(err);
			goto out_yes_iyesde;
		}
	}
	dprintk("NFS: nfs_fhget(%s/%Lu fh_crc=0x%08x ct=%d)\n",
		iyesde->i_sb->s_id,
		(unsigned long long)NFS_FILEID(iyesde),
		nfs_display_fhandle_hash(fh),
		atomic_read(&iyesde->i_count));

out:
	return iyesde;

out_yes_iyesde:
	dprintk("nfs_fhget: iget failed with error %ld\n", PTR_ERR(iyesde));
	goto out;
}
EXPORT_SYMBOL_GPL(nfs_fhget);

#define NFS_VALID_ATTRS (ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_SIZE|ATTR_ATIME|ATTR_ATIME_SET|ATTR_MTIME|ATTR_MTIME_SET|ATTR_FILE|ATTR_OPEN)

int
nfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct nfs_fattr *fattr;
	int error = 0;

	nfs_inc_stats(iyesde, NFSIOS_VFSSETATTR);

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attr->ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		attr->ia_valid &= ~ATTR_MODE;

	if (attr->ia_valid & ATTR_SIZE) {
		BUG_ON(!S_ISREG(iyesde->i_mode));

		error = iyesde_newsize_ok(iyesde, attr->ia_size);
		if (error)
			return error;

		if (attr->ia_size == i_size_read(iyesde))
			attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Optimization: if the end result is yes change, don't RPC */
	attr->ia_valid &= NFS_VALID_ATTRS;
	if ((attr->ia_valid & ~(ATTR_FILE|ATTR_OPEN)) == 0)
		return 0;

	trace_nfs_setattr_enter(iyesde);

	/* Write all dirty data */
	if (S_ISREG(iyesde->i_mode))
		nfs_sync_iyesde(iyesde);

	fattr = nfs_alloc_fattr();
	if (fattr == NULL) {
		error = -ENOMEM;
		goto out;
	}

	error = NFS_PROTO(iyesde)->setattr(dentry, fattr, attr);
	if (error == 0)
		error = nfs_refresh_iyesde(iyesde, fattr);
	nfs_free_fattr(fattr);
out:
	trace_nfs_setattr_exit(iyesde, error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_setattr);

/**
 * nfs_vmtruncate - unmap mappings "freed" by truncate() syscall
 * @iyesde: iyesde of the file used
 * @offset: file offset to start truncating
 *
 * This is a copy of the common vmtruncate, but with the locking
 * corrected to take into account the fact that NFS requires
 * iyesde->i_size to be updated under the iyesde->i_lock.
 * Note: must be called with iyesde->i_lock held!
 */
static int nfs_vmtruncate(struct iyesde * iyesde, loff_t offset)
{
	int err;

	err = iyesde_newsize_ok(iyesde, offset);
	if (err)
		goto out;

	i_size_write(iyesde, offset);
	/* Optimisation */
	if (offset == 0)
		NFS_I(iyesde)->cache_validity &= ~(NFS_INO_INVALID_DATA |
				NFS_INO_DATA_INVAL_DEFER);
	NFS_I(iyesde)->cache_validity &= ~NFS_INO_INVALID_SIZE;

	spin_unlock(&iyesde->i_lock);
	truncate_pagecache(iyesde, offset);
	spin_lock(&iyesde->i_lock);
out:
	return err;
}

/**
 * nfs_setattr_update_iyesde - Update iyesde metadata after a setattr call.
 * @iyesde: pointer to struct iyesde
 * @attr: pointer to struct iattr
 * @fattr: pointer to struct nfs_fattr
 *
 * Note: we do this in the *proc.c in order to ensure that
 *       it works for things like exclusive creates too.
 */
void nfs_setattr_update_iyesde(struct iyesde *iyesde, struct iattr *attr,
		struct nfs_fattr *fattr)
{
	/* Barrier: bump the attribute generation count. */
	nfs_fattr_set_barrier(fattr);

	spin_lock(&iyesde->i_lock);
	NFS_I(iyesde)->attr_gencount = fattr->gencount;
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_MTIME);
		nfs_inc_stats(iyesde, NFSIOS_SETATTRTRUNC);
		nfs_vmtruncate(iyesde, attr->ia_size);
	}
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0) {
		NFS_I(iyesde)->cache_validity &= ~NFS_INO_INVALID_CTIME;
		if ((attr->ia_valid & ATTR_MODE) != 0) {
			int mode = attr->ia_mode & S_IALLUGO;
			mode |= iyesde->i_mode & ~S_IALLUGO;
			iyesde->i_mode = mode;
		}
		if ((attr->ia_valid & ATTR_UID) != 0)
			iyesde->i_uid = attr->ia_uid;
		if ((attr->ia_valid & ATTR_GID) != 0)
			iyesde->i_gid = attr->ia_gid;
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			iyesde->i_ctime = fattr->ctime;
		else
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_CHANGE
					| NFS_INO_INVALID_CTIME);
		nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL);
	}
	if (attr->ia_valid & (ATTR_ATIME_SET|ATTR_ATIME)) {
		NFS_I(iyesde)->cache_validity &= ~(NFS_INO_INVALID_ATIME
				| NFS_INO_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_ATIME)
			iyesde->i_atime = fattr->atime;
		else if (attr->ia_valid & ATTR_ATIME_SET)
			iyesde->i_atime = attr->ia_atime;
		else
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_ATIME);

		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			iyesde->i_ctime = fattr->ctime;
		else
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_CHANGE
					| NFS_INO_INVALID_CTIME);
	}
	if (attr->ia_valid & (ATTR_MTIME_SET|ATTR_MTIME)) {
		NFS_I(iyesde)->cache_validity &= ~(NFS_INO_INVALID_MTIME
				| NFS_INO_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			iyesde->i_mtime = fattr->mtime;
		else if (attr->ia_valid & ATTR_MTIME_SET)
			iyesde->i_mtime = attr->ia_mtime;
		else
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_MTIME);

		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			iyesde->i_ctime = fattr->ctime;
		else
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_CHANGE
					| NFS_INO_INVALID_CTIME);
	}
	if (fattr->valid)
		nfs_update_iyesde(iyesde, fattr);
	spin_unlock(&iyesde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_setattr_update_iyesde);

static void nfs_readdirplus_parent_cache_miss(struct dentry *dentry)
{
	struct dentry *parent;

	if (!nfs_server_capable(d_iyesde(dentry), NFS_CAP_READDIRPLUS))
		return;
	parent = dget_parent(dentry);
	nfs_force_use_readdirplus(d_iyesde(parent));
	dput(parent);
}

static void nfs_readdirplus_parent_cache_hit(struct dentry *dentry)
{
	struct dentry *parent;

	if (!nfs_server_capable(d_iyesde(dentry), NFS_CAP_READDIRPLUS))
		return;
	parent = dget_parent(dentry);
	nfs_advise_use_readdirplus(d_iyesde(parent));
	dput(parent);
}

static bool nfs_need_revalidate_iyesde(struct iyesde *iyesde)
{
	if (NFS_I(iyesde)->cache_validity &
			(NFS_INO_INVALID_ATTR|NFS_INO_INVALID_LABEL))
		return true;
	if (nfs_attribute_cache_expired(iyesde))
		return true;
	return false;
}

int nfs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags)
{
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct nfs_server *server = NFS_SERVER(iyesde);
	unsigned long cache_validity;
	int err = 0;
	bool force_sync = query_flags & AT_STATX_FORCE_SYNC;
	bool do_update = false;

	trace_nfs_getattr_enter(iyesde);

	if ((query_flags & AT_STATX_DONT_SYNC) && !force_sync)
		goto out_yes_update;

	/* Flush out writes to the server in order to update c/mtime.  */
	if ((request_mask & (STATX_CTIME|STATX_MTIME)) &&
			S_ISREG(iyesde->i_mode)) {
		err = filemap_write_and_wait(iyesde->i_mapping);
		if (err)
			goto out;
	}

	/*
	 * We may force a getattr if the user cares about atime.
	 *
	 * Note that we only have to check the vfsmount flags here:
	 *  - NFS always sets S_NOATIME by so checking it would give a
	 *    bogus result
	 *  - NFS never sets SB_NOATIME or SB_NODIRATIME so there is
	 *    yes point in checking those.
	 */
	if ((path->mnt->mnt_flags & MNT_NOATIME) ||
	    ((path->mnt->mnt_flags & MNT_NODIRATIME) && S_ISDIR(iyesde->i_mode)))
		request_mask &= ~STATX_ATIME;

	/* Is the user requesting attributes that might need revalidation? */
	if (!(request_mask & (STATX_MODE|STATX_NLINK|STATX_ATIME|STATX_CTIME|
					STATX_MTIME|STATX_UID|STATX_GID|
					STATX_SIZE|STATX_BLOCKS)))
		goto out_yes_revalidate;

	/* Check whether the cached attributes are stale */
	do_update |= force_sync || nfs_attribute_cache_expired(iyesde);
	cache_validity = READ_ONCE(NFS_I(iyesde)->cache_validity);
	do_update |= cache_validity &
		(NFS_INO_INVALID_ATTR|NFS_INO_INVALID_LABEL);
	if (request_mask & STATX_ATIME)
		do_update |= cache_validity & NFS_INO_INVALID_ATIME;
	if (request_mask & (STATX_CTIME|STATX_MTIME))
		do_update |= cache_validity & NFS_INO_REVAL_PAGECACHE;
	if (do_update) {
		/* Update the attribute cache */
		if (!(server->flags & NFS_MOUNT_NOAC))
			nfs_readdirplus_parent_cache_miss(path->dentry);
		else
			nfs_readdirplus_parent_cache_hit(path->dentry);
		err = __nfs_revalidate_iyesde(server, iyesde);
		if (err)
			goto out;
	} else
		nfs_readdirplus_parent_cache_hit(path->dentry);
out_yes_revalidate:
	/* Only return attributes that were revalidated. */
	stat->result_mask &= request_mask;
out_yes_update:
	generic_fillattr(iyesde, stat);
	stat->iyes = nfs_compat_user_iyes64(NFS_FILEID(iyesde));
	if (S_ISDIR(iyesde->i_mode))
		stat->blksize = NFS_SERVER(iyesde)->dtsize;
out:
	trace_nfs_getattr_exit(iyesde, err);
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
		if (refcount_inc_yest_zero(&pos->count))
			return pos;
	}
	return NULL;
}

struct nfs_lock_context *nfs_get_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *res, *new = NULL;
	struct iyesde *iyesde = d_iyesde(ctx->dentry);

	rcu_read_lock();
	res = __nfs_find_lock_context(ctx);
	rcu_read_unlock();
	if (res == NULL) {
		new = kmalloc(sizeof(*new), GFP_KERNEL);
		if (new == NULL)
			return ERR_PTR(-ENOMEM);
		nfs_init_lock_context(new);
		spin_lock(&iyesde->i_lock);
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
		spin_unlock(&iyesde->i_lock);
		kfree(new);
	}
	return res;
}
EXPORT_SYMBOL_GPL(nfs_get_lock_context);

void nfs_put_lock_context(struct nfs_lock_context *l_ctx)
{
	struct nfs_open_context *ctx = l_ctx->open_context;
	struct iyesde *iyesde = d_iyesde(ctx->dentry);

	if (!refcount_dec_and_lock(&l_ctx->count, &iyesde->i_lock))
		return;
	list_del_rcu(&l_ctx->list);
	spin_unlock(&iyesde->i_lock);
	put_nfs_open_context(ctx);
	kfree_rcu(l_ctx, rcu_head);
}
EXPORT_SYMBOL_GPL(nfs_put_lock_context);

/**
 * nfs_close_context - Common close_context() routine NFSv2/v3
 * @ctx: pointer to context
 * @is_sync: is this a synchroyesus close
 *
 * Ensure that the attributes are up to date if we're mounted
 * with close-to-open semantics and we have cached data that will
 * need to be revalidated on open.
 */
void nfs_close_context(struct nfs_open_context *ctx, int is_sync)
{
	struct nfs_iyesde *nfsi;
	struct iyesde *iyesde;
	struct nfs_server *server;

	if (!(ctx->mode & FMODE_WRITE))
		return;
	if (!is_sync)
		return;
	iyesde = d_iyesde(ctx->dentry);
	if (NFS_PROTO(iyesde)->have_delegation(iyesde, FMODE_READ))
		return;
	nfsi = NFS_I(iyesde);
	if (iyesde->i_mapping->nrpages == 0)
		return;
	if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
		return;
	if (!list_empty(&nfsi->open_files))
		return;
	server = NFS_SERVER(iyesde);
	if (server->flags & NFS_MOUNT_NOCTO)
		return;
	nfs_revalidate_iyesde(server, iyesde);
}
EXPORT_SYMBOL_GPL(nfs_close_context);

struct nfs_open_context *alloc_nfs_open_context(struct dentry *dentry,
						fmode_t f_mode,
						struct file *filp)
{
	struct nfs_open_context *ctx;
	const struct cred *cred = get_current_cred();

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		put_cred(cred);
		return ERR_PTR(-ENOMEM);
	}
	nfs_sb_active(dentry->d_sb);
	ctx->dentry = dget(dentry);
	ctx->cred = cred;
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
	if (ctx != NULL && refcount_inc_yest_zero(&ctx->lock_context.count))
		return ctx;
	return NULL;
}
EXPORT_SYMBOL_GPL(get_nfs_open_context);

static void __put_nfs_open_context(struct nfs_open_context *ctx, int is_sync)
{
	struct iyesde *iyesde = d_iyesde(ctx->dentry);
	struct super_block *sb = ctx->dentry->d_sb;

	if (!refcount_dec_and_test(&ctx->lock_context.count))
		return;
	if (!list_empty(&ctx->list)) {
		spin_lock(&iyesde->i_lock);
		list_del_rcu(&ctx->list);
		spin_unlock(&iyesde->i_lock);
	}
	if (iyesde != NULL)
		NFS_PROTO(iyesde)->close_context(ctx, is_sync);
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
void nfs_iyesde_attach_open_context(struct nfs_open_context *ctx)
{
	struct iyesde *iyesde = d_iyesde(ctx->dentry);
	struct nfs_iyesde *nfsi = NFS_I(iyesde);

	spin_lock(&iyesde->i_lock);
	if (list_empty(&nfsi->open_files) &&
	    (nfsi->cache_validity & NFS_INO_DATA_INVAL_DEFER))
		nfsi->cache_validity |= NFS_INO_INVALID_DATA |
			NFS_INO_REVAL_FORCED;
	list_add_tail_rcu(&ctx->list, &nfsi->open_files);
	spin_unlock(&iyesde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_iyesde_attach_open_context);

void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx)
{
	filp->private_data = get_nfs_open_context(ctx);
	if (list_empty(&ctx->list))
		nfs_iyesde_attach_open_context(ctx);
}
EXPORT_SYMBOL_GPL(nfs_file_set_open_context);

/*
 * Given an iyesde, search for an open context with the desired characteristics
 */
struct nfs_open_context *nfs_find_open_context(struct iyesde *iyesde, const struct cred *cred, fmode_t mode)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	struct nfs_open_context *pos, *ctx = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(pos, &nfsi->open_files, list) {
		if (cred != NULL && pos->cred != cred)
			continue;
		if ((pos->mode & (FMODE_READ|FMODE_WRITE)) != mode)
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
		struct iyesde *iyesde = d_iyesde(ctx->dentry);

		/*
		 * We fatal error on write before. Try to writeback
		 * every page again.
		 */
		if (ctx->error < 0)
			invalidate_iyesde_pages2(iyesde->i_mapping);
		filp->private_data = NULL;
		put_nfs_open_context_sync(ctx);
	}
}

/*
 * These allocate and release file read/write context information.
 */
int nfs_open(struct iyesde *iyesde, struct file *filp)
{
	struct nfs_open_context *ctx;

	ctx = alloc_nfs_open_context(file_dentry(filp), filp->f_mode, filp);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	nfs_fscache_open_file(iyesde, filp);
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_open);

/*
 * This function is called whenever some part of NFS yestices that
 * the cached attributes have to be refreshed.
 */
int
__nfs_revalidate_iyesde(struct nfs_server *server, struct iyesde *iyesde)
{
	int		 status = -ESTALE;
	struct nfs4_label *label = NULL;
	struct nfs_fattr *fattr = NULL;
	struct nfs_iyesde *nfsi = NFS_I(iyesde);

	dfprintk(PAGECACHE, "NFS: revalidating (%s/%Lu)\n",
		iyesde->i_sb->s_id, (unsigned long long)NFS_FILEID(iyesde));

	trace_nfs_revalidate_iyesde_enter(iyesde);

	if (is_bad_iyesde(iyesde))
		goto out;
	if (NFS_STALE(iyesde))
		goto out;

	/* pNFS: Attributes aren't updated until we layoutcommit */
	if (S_ISREG(iyesde->i_mode)) {
		status = pnfs_sync_iyesde(iyesde, false);
		if (status)
			goto out;
	}

	status = -ENOMEM;
	fattr = nfs_alloc_fattr();
	if (fattr == NULL)
		goto out;

	nfs_inc_stats(iyesde, NFSIOS_INODEREVALIDATE);

	label = nfs4_label_alloc(NFS_SERVER(iyesde), GFP_KERNEL);
	if (IS_ERR(label)) {
		status = PTR_ERR(label);
		goto out;
	}

	status = NFS_PROTO(iyesde)->getattr(server, NFS_FH(iyesde), fattr,
			label, iyesde);
	if (status != 0) {
		dfprintk(PAGECACHE, "nfs_revalidate_iyesde: (%s/%Lu) getattr failed, error=%d\n",
			 iyesde->i_sb->s_id,
			 (unsigned long long)NFS_FILEID(iyesde), status);
		if (status == -ESTALE) {
			nfs_zap_caches(iyesde);
			if (!S_ISDIR(iyesde->i_mode))
				set_bit(NFS_INO_STALE, &NFS_I(iyesde)->flags);
		}
		goto err_out;
	}

	status = nfs_refresh_iyesde(iyesde, fattr);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_iyesde: (%s/%Lu) refresh failed, error=%d\n",
			 iyesde->i_sb->s_id,
			 (unsigned long long)NFS_FILEID(iyesde), status);
		goto err_out;
	}

	if (nfsi->cache_validity & NFS_INO_INVALID_ACL)
		nfs_zap_acl_cache(iyesde);

	nfs_setsecurity(iyesde, fattr, label);

	dfprintk(PAGECACHE, "NFS: (%s/%Lu) revalidation complete\n",
		iyesde->i_sb->s_id,
		(unsigned long long)NFS_FILEID(iyesde));

err_out:
	nfs4_label_free(label);
out:
	nfs_free_fattr(fattr);
	trace_nfs_revalidate_iyesde_exit(iyesde, status);
	return status;
}

int nfs_attribute_cache_expired(struct iyesde *iyesde)
{
	if (nfs_have_delegated_attributes(iyesde))
		return 0;
	return nfs_attribute_timeout(iyesde);
}

/**
 * nfs_revalidate_iyesde - Revalidate the iyesde attributes
 * @server: pointer to nfs_server struct
 * @iyesde: pointer to iyesde struct
 *
 * Updates iyesde attribute information by retrieving the data from the server.
 */
int nfs_revalidate_iyesde(struct nfs_server *server, struct iyesde *iyesde)
{
	if (!nfs_need_revalidate_iyesde(iyesde))
		return NFS_STALE(iyesde) ? -ESTALE : 0;
	return __nfs_revalidate_iyesde(server, iyesde);
}
EXPORT_SYMBOL_GPL(nfs_revalidate_iyesde);

static int nfs_invalidate_mapping(struct iyesde *iyesde, struct address_space *mapping)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	int ret;

	if (mapping->nrpages != 0) {
		if (S_ISREG(iyesde->i_mode)) {
			ret = nfs_sync_mapping(mapping);
			if (ret < 0)
				return ret;
		}
		ret = invalidate_iyesde_pages2(mapping);
		if (ret < 0)
			return ret;
	}
	if (S_ISDIR(iyesde->i_mode)) {
		spin_lock(&iyesde->i_lock);
		memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
		spin_unlock(&iyesde->i_lock);
	}
	nfs_inc_stats(iyesde, NFSIOS_DATAINVALIDATE);
	nfs_fscache_wait_on_invalidate(iyesde);

	dfprintk(PAGECACHE, "NFS: (%s/%Lu) data cache invalidated\n",
			iyesde->i_sb->s_id,
			(unsigned long long)NFS_FILEID(iyesde));
	return 0;
}

bool nfs_mapping_need_revalidate_iyesde(struct iyesde *iyesde)
{
	return nfs_check_cache_invalid(iyesde, NFS_INO_REVAL_PAGECACHE) ||
		NFS_STALE(iyesde);
}

int nfs_revalidate_mapping_rcu(struct iyesde *iyesde)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	unsigned long *bitlock = &nfsi->flags;
	int ret = 0;

	if (IS_SWAPFILE(iyesde))
		goto out;
	if (nfs_mapping_need_revalidate_iyesde(iyesde)) {
		ret = -ECHILD;
		goto out;
	}
	spin_lock(&iyesde->i_lock);
	if (test_bit(NFS_INO_INVALIDATING, bitlock) ||
	    (nfsi->cache_validity & NFS_INO_INVALID_DATA))
		ret = -ECHILD;
	spin_unlock(&iyesde->i_lock);
out:
	return ret;
}

/**
 * nfs_revalidate_mapping - Revalidate the pagecache
 * @iyesde: pointer to host iyesde
 * @mapping: pointer to mapping
 */
int nfs_revalidate_mapping(struct iyesde *iyesde,
		struct address_space *mapping)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	unsigned long *bitlock = &nfsi->flags;
	int ret = 0;

	/* swapfiles are yest supposed to be shared. */
	if (IS_SWAPFILE(iyesde))
		goto out;

	if (nfs_mapping_need_revalidate_iyesde(iyesde)) {
		ret = __nfs_revalidate_iyesde(NFS_SERVER(iyesde), iyesde);
		if (ret < 0)
			goto out;
	}

	/*
	 * We must clear NFS_INO_INVALID_DATA first to ensure that
	 * invalidations that come in while we're shooting down the mappings
	 * are respected. But, that leaves a race window where one revalidator
	 * can clear the flag, and then ayesther checks it before the mapping
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
		spin_lock(&iyesde->i_lock);
		if (test_bit(NFS_INO_INVALIDATING, bitlock)) {
			spin_unlock(&iyesde->i_lock);
			continue;
		}
		if (nfsi->cache_validity & NFS_INO_INVALID_DATA)
			break;
		spin_unlock(&iyesde->i_lock);
		goto out;
	}

	set_bit(NFS_INO_INVALIDATING, bitlock);
	smp_wmb();
	nfsi->cache_validity &= ~(NFS_INO_INVALID_DATA|
			NFS_INO_DATA_INVAL_DEFER);
	spin_unlock(&iyesde->i_lock);
	trace_nfs_invalidate_mapping_enter(iyesde);
	ret = nfs_invalidate_mapping(iyesde, mapping);
	trace_nfs_invalidate_mapping_exit(iyesde, ret);

	clear_bit_unlock(NFS_INO_INVALIDATING, bitlock);
	smp_mb__after_atomic();
	wake_up_bit(bitlock, NFS_INO_INVALIDATING);
out:
	return ret;
}

static bool nfs_file_has_writers(struct nfs_iyesde *nfsi)
{
	struct iyesde *iyesde = &nfsi->vfs_iyesde;

	if (!S_ISREG(iyesde->i_mode))
		return false;
	if (list_empty(&nfsi->open_files))
		return false;
	return iyesde_is_open_for_write(iyesde);
}

static bool nfs_file_has_buffered_writers(struct nfs_iyesde *nfsi)
{
	return nfs_file_has_writers(nfsi) && nfs_file_io_is_buffered(nfsi);
}

static void nfs_wcc_update_iyesde(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	struct timespec64 ts;

	if ((fattr->valid & NFS_ATTR_FATTR_PRECHANGE)
			&& (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			&& iyesde_eq_iversion_raw(iyesde, fattr->pre_change_attr)) {
		iyesde_set_iversion_raw(iyesde, fattr->change_attr);
		if (S_ISDIR(iyesde->i_mode))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_DATA);
	}
	/* If we have atomic WCC data, we may update some attributes */
	ts = iyesde->i_ctime;
	if ((fattr->valid & NFS_ATTR_FATTR_PRECTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_CTIME)
			&& timespec64_equal(&ts, &fattr->pre_ctime)) {
		iyesde->i_ctime = fattr->ctime;
	}

	ts = iyesde->i_mtime;
	if ((fattr->valid & NFS_ATTR_FATTR_PREMTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_MTIME)
			&& timespec64_equal(&ts, &fattr->pre_mtime)) {
		iyesde->i_mtime = fattr->mtime;
		if (S_ISDIR(iyesde->i_mode))
			nfs_set_cache_invalid(iyesde, NFS_INO_INVALID_DATA);
	}
	if ((fattr->valid & NFS_ATTR_FATTR_PRESIZE)
			&& (fattr->valid & NFS_ATTR_FATTR_SIZE)
			&& i_size_read(iyesde) == nfs_size_to_loff_t(fattr->pre_size)
			&& !nfs_have_writebacks(iyesde)) {
		i_size_write(iyesde, nfs_size_to_loff_t(fattr->size));
	}
}

/**
 * nfs_check_iyesde_attributes - verify consistency of the iyesde attribute cache
 * @iyesde: pointer to iyesde
 * @fattr: updated attributes
 *
 * Verifies the attribute cache. If we have just changed the attributes,
 * so that fattr carries weak cache consistency data, then it may
 * also update the ctime/mtime/change_attribute.
 */
static int nfs_check_iyesde_attributes(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	loff_t cur_size, new_isize;
	unsigned long invalid = 0;
	struct timespec64 ts;

	if (NFS_PROTO(iyesde)->have_delegation(iyesde, FMODE_READ))
		return 0;

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID)) {
		/* Only a mounted-on-fileid? Just exit */
		if (fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID)
			return 0;
	/* Has the iyesde gone and changed behind our back? */
	} else if (nfsi->fileid != fattr->fileid) {
		/* Is this perhaps the mounted-on fileid? */
		if ((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) &&
		    nfsi->fileid == fattr->mounted_on_fileid)
			return 0;
		return -ESTALE;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && (iyesde->i_mode & S_IFMT) != (fattr->mode & S_IFMT))
		return -ESTALE;


	if (!nfs_file_has_buffered_writers(nfsi)) {
		/* Verify a few of the more important attributes */
		if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 && !iyesde_eq_iversion_raw(iyesde, fattr->change_attr))
			invalid |= NFS_INO_INVALID_CHANGE
				| NFS_INO_REVAL_PAGECACHE;

		ts = iyesde->i_mtime;
		if ((fattr->valid & NFS_ATTR_FATTR_MTIME) && !timespec64_equal(&ts, &fattr->mtime))
			invalid |= NFS_INO_INVALID_MTIME;

		ts = iyesde->i_ctime;
		if ((fattr->valid & NFS_ATTR_FATTR_CTIME) && !timespec64_equal(&ts, &fattr->ctime))
			invalid |= NFS_INO_INVALID_CTIME;

		if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
			cur_size = i_size_read(iyesde);
			new_isize = nfs_size_to_loff_t(fattr->size);
			if (cur_size != new_isize)
				invalid |= NFS_INO_INVALID_SIZE
					| NFS_INO_REVAL_PAGECACHE;
		}
	}

	/* Have any file permissions changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_MODE) && (iyesde->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO))
		invalid |= NFS_INO_INVALID_ACCESS
			| NFS_INO_INVALID_ACL
			| NFS_INO_INVALID_OTHER;
	if ((fattr->valid & NFS_ATTR_FATTR_OWNER) && !uid_eq(iyesde->i_uid, fattr->uid))
		invalid |= NFS_INO_INVALID_ACCESS
			| NFS_INO_INVALID_ACL
			| NFS_INO_INVALID_OTHER;
	if ((fattr->valid & NFS_ATTR_FATTR_GROUP) && !gid_eq(iyesde->i_gid, fattr->gid))
		invalid |= NFS_INO_INVALID_ACCESS
			| NFS_INO_INVALID_ACL
			| NFS_INO_INVALID_OTHER;

	/* Has the link count changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_NLINK) && iyesde->i_nlink != fattr->nlink)
		invalid |= NFS_INO_INVALID_OTHER;

	ts = iyesde->i_atime;
	if ((fattr->valid & NFS_ATTR_FATTR_ATIME) && !timespec64_equal(&ts, &fattr->atime))
		invalid |= NFS_INO_INVALID_ATIME;

	if (invalid != 0)
		nfs_set_cache_invalid(iyesde, invalid);

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
 * have raced with our update canyest clobber these new values.
 * Note that you are still responsible for ensuring that other
 * operations which change the attribute on the server do yest
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
	 * yest on the result */
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
 * nfs_iyesde_attrs_need_update - check if the iyesde attributes need updating
 * @iyesde: pointer to iyesde
 * @fattr: attributes
 *
 * Attempt to divine whether or yest an RPC call reply carrying stale
 * attributes got scheduled after ayesther call carrying updated ones.
 *
 * To do so, the function first assumes that a more recent ctime means
 * that the attributes in fattr are newer, however it also attempt to
 * catch the case where ctime either didn't change, or went backwards
 * (if someone reset the clock on the server) by looking at whether
 * or yest this RPC call was started after the iyesde was last updated.
 * Note also the check for wraparound of 'attr_gencount'
 *
 * The function returns 'true' if it thinks the attributes in 'fattr' are
 * more recent than the ones cached in the iyesde.
 *
 */
static int nfs_iyesde_attrs_need_update(const struct iyesde *iyesde, const struct nfs_fattr *fattr)
{
	const struct nfs_iyesde *nfsi = NFS_I(iyesde);

	return ((long)fattr->gencount - (long)nfsi->attr_gencount) > 0 ||
		((long)nfsi->attr_gencount - (long)nfs_read_attr_generation_counter() > 0);
}

static int nfs_refresh_iyesde_locked(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	int ret;

	trace_nfs_refresh_iyesde_enter(iyesde);

	if (nfs_iyesde_attrs_need_update(iyesde, fattr))
		ret = nfs_update_iyesde(iyesde, fattr);
	else
		ret = nfs_check_iyesde_attributes(iyesde, fattr);

	trace_nfs_refresh_iyesde_exit(iyesde, ret);
	return ret;
}

/**
 * nfs_refresh_iyesde - try to update the iyesde attribute cache
 * @iyesde: pointer to iyesde
 * @fattr: updated attributes
 *
 * Check that an RPC call that returned attributes has yest overlapped with
 * other recent updates of the iyesde metadata, then decide whether it is
 * safe to do a full update of the iyesde attributes, or whether just to
 * call nfs_check_iyesde_attributes.
 */
int nfs_refresh_iyesde(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	int status;

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	spin_lock(&iyesde->i_lock);
	status = nfs_refresh_iyesde_locked(iyesde, fattr);
	spin_unlock(&iyesde->i_lock);

	return status;
}
EXPORT_SYMBOL_GPL(nfs_refresh_iyesde);

static int nfs_post_op_update_iyesde_locked(struct iyesde *iyesde,
		struct nfs_fattr *fattr, unsigned int invalid)
{
	if (S_ISDIR(iyesde->i_mode))
		invalid |= NFS_INO_INVALID_DATA;
	nfs_set_cache_invalid(iyesde, invalid);
	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	return nfs_refresh_iyesde_locked(iyesde, fattr);
}

/**
 * nfs_post_op_update_iyesde - try to update the iyesde attribute cache
 * @iyesde: pointer to iyesde
 * @fattr: updated attributes
 *
 * After an operation that has changed the iyesde metadata, mark the
 * attribute cache as being invalid, then try to update it.
 *
 * NB: if the server didn't return any post op attributes, this
 * function will force the retrieval of attributes before the next
 * NFS request.  Thus it should be used only for operations that
 * are expected to change one or more attributes, to avoid
 * unnecessary NFS requests and trips through nfs_update_iyesde().
 */
int nfs_post_op_update_iyesde(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	int status;

	spin_lock(&iyesde->i_lock);
	nfs_fattr_set_barrier(fattr);
	status = nfs_post_op_update_iyesde_locked(iyesde, fattr,
			NFS_INO_INVALID_CHANGE
			| NFS_INO_INVALID_CTIME
			| NFS_INO_REVAL_FORCED);
	spin_unlock(&iyesde->i_lock);

	return status;
}
EXPORT_SYMBOL_GPL(nfs_post_op_update_iyesde);

/**
 * nfs_post_op_update_iyesde_force_wcc_locked - update the iyesde attribute cache
 * @iyesde: pointer to iyesde
 * @fattr: updated attributes
 *
 * After an operation that has changed the iyesde metadata, mark the
 * attribute cache as being invalid, then try to update it. Fake up
 * weak cache consistency data, if yesne exist.
 *
 * This function is mainly designed to be used by the ->write_done() functions.
 */
int nfs_post_op_update_iyesde_force_wcc_locked(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	int status;

	/* Don't do a WCC update if these attributes are already stale */
	if ((fattr->valid & NFS_ATTR_FATTR) == 0 ||
			!nfs_iyesde_attrs_need_update(iyesde, fattr)) {
		fattr->valid &= ~(NFS_ATTR_FATTR_PRECHANGE
				| NFS_ATTR_FATTR_PRESIZE
				| NFS_ATTR_FATTR_PREMTIME
				| NFS_ATTR_FATTR_PRECTIME);
		goto out_yesforce;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECHANGE) == 0) {
		fattr->pre_change_attr = iyesde_peek_iversion_raw(iyesde);
		fattr->valid |= NFS_ATTR_FATTR_PRECHANGE;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECTIME) == 0) {
		fattr->pre_ctime = iyesde->i_ctime;
		fattr->valid |= NFS_ATTR_FATTR_PRECTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_MTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PREMTIME) == 0) {
		fattr->pre_mtime = iyesde->i_mtime;
		fattr->valid |= NFS_ATTR_FATTR_PREMTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_SIZE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRESIZE) == 0) {
		fattr->pre_size = i_size_read(iyesde);
		fattr->valid |= NFS_ATTR_FATTR_PRESIZE;
	}
out_yesforce:
	status = nfs_post_op_update_iyesde_locked(iyesde, fattr,
			NFS_INO_INVALID_CHANGE
			| NFS_INO_INVALID_CTIME
			| NFS_INO_INVALID_MTIME);
	return status;
}

/**
 * nfs_post_op_update_iyesde_force_wcc - try to update the iyesde attribute cache
 * @iyesde: pointer to iyesde
 * @fattr: updated attributes
 *
 * After an operation that has changed the iyesde metadata, mark the
 * attribute cache as being invalid, then try to update it. Fake up
 * weak cache consistency data, if yesne exist.
 *
 * This function is mainly designed to be used by the ->write_done() functions.
 */
int nfs_post_op_update_iyesde_force_wcc(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	int status;

	spin_lock(&iyesde->i_lock);
	nfs_fattr_set_barrier(fattr);
	status = nfs_post_op_update_iyesde_force_wcc_locked(iyesde, fattr);
	spin_unlock(&iyesde->i_lock);
	return status;
}
EXPORT_SYMBOL_GPL(nfs_post_op_update_iyesde_force_wcc);


/*
 * Many nfs protocol calls return the new file attributes after
 * an operation.  Here we update the iyesde to reflect the state
 * of the server's iyesde.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_iyesde_pages.
 * To make sure yes other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
static int nfs_update_iyesde(struct iyesde *iyesde, struct nfs_fattr *fattr)
{
	struct nfs_server *server;
	struct nfs_iyesde *nfsi = NFS_I(iyesde);
	loff_t cur_isize, new_isize;
	unsigned long invalid = 0;
	unsigned long yesw = jiffies;
	unsigned long save_cache_validity;
	bool have_writers = nfs_file_has_buffered_writers(nfsi);
	bool cache_revalidated = true;
	bool attr_changed = false;
	bool have_delegation;

	dfprintk(VFS, "NFS: %s(%s/%lu fh_crc=0x%08x ct=%d info=0x%x)\n",
			__func__, iyesde->i_sb->s_id, iyesde->i_iyes,
			nfs_display_fhandle_hash(NFS_FH(iyesde)),
			atomic_read(&iyesde->i_count), fattr->valid);

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID)) {
		/* Only a mounted-on-fileid? Just exit */
		if (fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID)
			return 0;
	/* Has the iyesde gone and changed behind our back? */
	} else if (nfsi->fileid != fattr->fileid) {
		/* Is this perhaps the mounted-on fileid? */
		if ((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) &&
		    nfsi->fileid == fattr->mounted_on_fileid)
			return 0;
		printk(KERN_ERR "NFS: server %s error: fileid changed\n"
			"fsid %s: expected fileid 0x%Lx, got 0x%Lx\n",
			NFS_SERVER(iyesde)->nfs_client->cl_hostname,
			iyesde->i_sb->s_id, (long long)nfsi->fileid,
			(long long)fattr->fileid);
		goto out_err;
	}

	/*
	 * Make sure the iyesde's type hasn't changed.
	 */
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && (iyesde->i_mode & S_IFMT) != (fattr->mode & S_IFMT)) {
		/*
		* Big trouble! The iyesde has become a different object.
		*/
		printk(KERN_DEBUG "NFS: %s: iyesde %lu mode changed, %07o to %07o\n",
				__func__, iyesde->i_iyes, iyesde->i_mode, fattr->mode);
		goto out_err;
	}

	server = NFS_SERVER(iyesde);
	/* Update the fsid? */
	if (S_ISDIR(iyesde->i_mode) && (fattr->valid & NFS_ATTR_FATTR_FSID) &&
			!nfs_fsid_equal(&server->fsid, &fattr->fsid) &&
			!IS_AUTOMOUNT(iyesde))
		server->fsid = fattr->fsid;

	/* Save the delegation state before clearing cache_validity */
	have_delegation = nfs_have_delegated_attributes(iyesde);

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
	nfs_wcc_update_iyesde(iyesde, fattr);

	if (pnfs_layoutcommit_outstanding(iyesde)) {
		nfsi->cache_validity |= save_cache_validity & NFS_INO_INVALID_ATTR;
		cache_revalidated = false;
	}

	/* More cache consistency checks */
	if (fattr->valid & NFS_ATTR_FATTR_CHANGE) {
		if (!iyesde_eq_iversion_raw(iyesde, fattr->change_attr)) {
			/* Could it be a race with writeback? */
			if (!(have_writers || have_delegation)) {
				invalid |= NFS_INO_INVALID_DATA
					| NFS_INO_INVALID_ACCESS
					| NFS_INO_INVALID_ACL;
				/* Force revalidate of all attributes */
				save_cache_validity |= NFS_INO_INVALID_CTIME
					| NFS_INO_INVALID_MTIME
					| NFS_INO_INVALID_SIZE
					| NFS_INO_INVALID_OTHER;
				if (S_ISDIR(iyesde->i_mode))
					nfs_force_lookup_revalidate(iyesde);
				dprintk("NFS: change_attr change on server for file %s/%ld\n",
						iyesde->i_sb->s_id,
						iyesde->i_iyes);
			} else if (!have_delegation)
				nfsi->cache_validity |= NFS_INO_DATA_INVAL_DEFER;
			iyesde_set_iversion_raw(iyesde, fattr->change_attr);
			attr_changed = true;
		}
	} else {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_CHANGE
				| NFS_INO_REVAL_PAGECACHE
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_MTIME) {
		iyesde->i_mtime = fattr->mtime;
	} else if (server->caps & NFS_CAP_MTIME) {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_MTIME
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_CTIME) {
		iyesde->i_ctime = fattr->ctime;
	} else if (server->caps & NFS_CAP_CTIME) {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_CTIME
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	/* Check if our cached file size is stale */
	if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
		new_isize = nfs_size_to_loff_t(fattr->size);
		cur_isize = i_size_read(iyesde);
		if (new_isize != cur_isize && !have_delegation) {
			/* Do we perhaps have any outstanding writes, or has
			 * the file grown beyond our last write? */
			if (!nfs_have_writebacks(iyesde) || new_isize > cur_isize) {
				i_size_write(iyesde, new_isize);
				if (!have_writers)
					invalid |= NFS_INO_INVALID_DATA;
				attr_changed = true;
			}
			dprintk("NFS: isize change on server for file %s/%ld "
					"(%Ld to %Ld)\n",
					iyesde->i_sb->s_id,
					iyesde->i_iyes,
					(long long)cur_isize,
					(long long)new_isize);
		}
	} else {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_SIZE
				| NFS_INO_REVAL_PAGECACHE
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}


	if (fattr->valid & NFS_ATTR_FATTR_ATIME)
		iyesde->i_atime = fattr->atime;
	else if (server->caps & NFS_CAP_ATIME) {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_ATIME
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_MODE) {
		if ((iyesde->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO)) {
			umode_t newmode = iyesde->i_mode & S_IFMT;
			newmode |= fattr->mode & S_IALLUGO;
			iyesde->i_mode = newmode;
			invalid |= NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
			attr_changed = true;
		}
	} else if (server->caps & NFS_CAP_MODE) {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_OTHER
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_OWNER) {
		if (!uid_eq(iyesde->i_uid, fattr->uid)) {
			invalid |= NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
			iyesde->i_uid = fattr->uid;
			attr_changed = true;
		}
	} else if (server->caps & NFS_CAP_OWNER) {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_OTHER
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_GROUP) {
		if (!gid_eq(iyesde->i_gid, fattr->gid)) {
			invalid |= NFS_INO_INVALID_ACCESS
				| NFS_INO_INVALID_ACL;
			iyesde->i_gid = fattr->gid;
			attr_changed = true;
		}
	} else if (server->caps & NFS_CAP_OWNER_GROUP) {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_OTHER
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_NLINK) {
		if (iyesde->i_nlink != fattr->nlink) {
			if (S_ISDIR(iyesde->i_mode))
				invalid |= NFS_INO_INVALID_DATA;
			set_nlink(iyesde, fattr->nlink);
			attr_changed = true;
		}
	} else if (server->caps & NFS_CAP_NLINK) {
		nfsi->cache_validity |= save_cache_validity &
				(NFS_INO_INVALID_OTHER
				| NFS_INO_REVAL_FORCED);
		cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
		/*
		 * report the blocks in 512byte units
		 */
		iyesde->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
	} else if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
		iyesde->i_blocks = fattr->du.nfs2.blocks;
	else
		cache_revalidated = false;

	/* Update attrtimeo value if we're out of the unstable period */
	if (attr_changed) {
		invalid &= ~NFS_INO_INVALID_ATTR;
		nfs_inc_stats(iyesde, NFSIOS_ATTRINVALIDATE);
		nfsi->attrtimeo = NFS_MINATTRTIMEO(iyesde);
		nfsi->attrtimeo_timestamp = yesw;
		/* Set barrier to be more recent than all outstanding updates */
		nfsi->attr_gencount = nfs_inc_attr_generation_counter();
	} else {
		if (cache_revalidated) {
			if (!time_in_range_open(yesw, nfsi->attrtimeo_timestamp,
				nfsi->attrtimeo_timestamp + nfsi->attrtimeo)) {
				nfsi->attrtimeo <<= 1;
				if (nfsi->attrtimeo > NFS_MAXATTRTIMEO(iyesde))
					nfsi->attrtimeo = NFS_MAXATTRTIMEO(iyesde);
			}
			nfsi->attrtimeo_timestamp = yesw;
		}
		/* Set the barrier to be more recent than this fattr */
		if ((long)fattr->gencount - (long)nfsi->attr_gencount > 0)
			nfsi->attr_gencount = fattr->gencount;
	}

	/* Don't invalidate the data if we were to blame */
	if (!(S_ISREG(iyesde->i_mode) || S_ISDIR(iyesde->i_mode)
				|| S_ISLNK(iyesde->i_mode)))
		invalid &= ~NFS_INO_INVALID_DATA;
	nfs_set_cache_invalid(iyesde, invalid);

	return 0;
 out_err:
	/*
	 * No need to worry about unhashing the dentry, as the
	 * lookup validation will kyesw that the iyesde is bad.
	 * (But we fall through to invalidate the caches.)
	 */
	nfs_invalidate_iyesde(iyesde);
	return -ESTALE;
}

struct iyesde *nfs_alloc_iyesde(struct super_block *sb)
{
	struct nfs_iyesde *nfsi;
	nfsi = kmem_cache_alloc(nfs_iyesde_cachep, GFP_KERNEL);
	if (!nfsi)
		return NULL;
	nfsi->flags = 0UL;
	nfsi->cache_validity = 0UL;
#if IS_ENABLED(CONFIG_NFS_V4)
	nfsi->nfs4_acl = NULL;
#endif /* CONFIG_NFS_V4 */
	return &nfsi->vfs_iyesde;
}
EXPORT_SYMBOL_GPL(nfs_alloc_iyesde);

void nfs_free_iyesde(struct iyesde *iyesde)
{
	kmem_cache_free(nfs_iyesde_cachep, NFS_I(iyesde));
}
EXPORT_SYMBOL_GPL(nfs_free_iyesde);

static inline void nfs4_init_once(struct nfs_iyesde *nfsi)
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
	struct nfs_iyesde *nfsi = (struct nfs_iyesde *) foo;

	iyesde_init_once(&nfsi->vfs_iyesde);
	INIT_LIST_HEAD(&nfsi->open_files);
	INIT_LIST_HEAD(&nfsi->access_cache_entry_lru);
	INIT_LIST_HEAD(&nfsi->access_cache_iyesde_lru);
	INIT_LIST_HEAD(&nfsi->commit_info.list);
	atomic_long_set(&nfsi->nrequests, 0);
	atomic_long_set(&nfsi->commit_info.ncommit, 0);
	atomic_set(&nfsi->commit_info.rpcs_out, 0);
	init_rwsem(&nfsi->rmdir_sem);
	mutex_init(&nfsi->commit_mutex);
	nfs4_init_once(nfsi);
}

static int __init nfs_init_iyesdecache(void)
{
	nfs_iyesde_cachep = kmem_cache_create("nfs_iyesde_cache",
					     sizeof(struct nfs_iyesde),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (nfs_iyesde_cachep == NULL)
		return -ENOMEM;

	return 0;
}

static void nfs_destroy_iyesdecache(void)
{
	/*
	 * Make sure all delayed rcu free iyesdes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(nfs_iyesde_cachep);
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

	err = nfs_init_iyesdecache();
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
	nfs_destroy_iyesdecache();
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
	nfs_destroy_iyesdecache();
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
module_param(enable_iyes64, bool, 0644);

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
