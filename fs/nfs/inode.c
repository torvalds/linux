// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/nfs/ianalde.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs ianalde and superblock handling functions
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
#include <linux/erranal.h>
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

#define NFS_64_BIT_IANALDE_NUMBERS_ENABLED	1

/* Default is to see 64-bit ianalde numbers */
static bool enable_ianal64 = NFS_64_BIT_IANALDE_NUMBERS_ENABLED;

static int nfs_update_ianalde(struct ianalde *, struct nfs_fattr *);

static struct kmem_cache * nfs_ianalde_cachep;

static inline unsigned long
nfs_fattr_to_ianal_t(struct nfs_fattr *fattr)
{
	return nfs_fileid_to_ianal_t(fattr->fileid);
}

int nfs_wait_bit_killable(struct wait_bit_key *key, int mode)
{
	schedule();
	if (signal_pending_state(mode, current))
		return -ERESTARTSYS;
	return 0;
}
EXPORT_SYMBOL_GPL(nfs_wait_bit_killable);

/**
 * nfs_compat_user_ianal64 - returns the user-visible ianalde number
 * @fileid: 64-bit fileid
 *
 * This function returns a 32-bit ianalde number if the boot parameter
 * nfs.enable_ianal64 is zero.
 */
u64 nfs_compat_user_ianal64(u64 fileid)
{
#ifdef CONFIG_COMPAT
	compat_ulong_t ianal;
#else	
	unsigned long ianal;
#endif

	if (enable_ianal64)
		return fileid;
	ianal = fileid;
	if (sizeof(ianal) < sizeof(fileid))
		ianal ^= fileid >> (sizeof(fileid)-sizeof(ianal)) * 8;
	return ianal;
}

int nfs_drop_ianalde(struct ianalde *ianalde)
{
	return NFS_STALE(ianalde) || generic_drop_ianalde(ianalde);
}
EXPORT_SYMBOL_GPL(nfs_drop_ianalde);

void nfs_clear_ianalde(struct ianalde *ianalde)
{
	/*
	 * The following should never happen...
	 */
	WARN_ON_ONCE(nfs_have_writebacks(ianalde));
	WARN_ON_ONCE(!list_empty(&NFS_I(ianalde)->open_files));
	nfs_zap_acl_cache(ianalde);
	nfs_access_zap_cache(ianalde);
	nfs_fscache_clear_ianalde(ianalde);
}
EXPORT_SYMBOL_GPL(nfs_clear_ianalde);

void nfs_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages_final(&ianalde->i_data);
	clear_ianalde(ianalde);
	nfs_clear_ianalde(ianalde);
}

int nfs_sync_ianalde(struct ianalde *ianalde)
{
	ianalde_dio_wait(ianalde);
	return nfs_wb_all(ianalde);
}
EXPORT_SYMBOL_GPL(nfs_sync_ianalde);

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

static int nfs_attribute_timeout(struct ianalde *ianalde)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);

	return !time_in_range_open(jiffies, nfsi->read_cache_jiffies, nfsi->read_cache_jiffies + nfsi->attrtimeo);
}

static bool nfs_check_cache_flags_invalid(struct ianalde *ianalde,
					  unsigned long flags)
{
	unsigned long cache_validity = READ_ONCE(NFS_I(ianalde)->cache_validity);

	return (cache_validity & flags) != 0;
}

bool nfs_check_cache_invalid(struct ianalde *ianalde, unsigned long flags)
{
	if (nfs_check_cache_flags_invalid(ianalde, flags))
		return true;
	return nfs_attribute_cache_expired(ianalde);
}
EXPORT_SYMBOL_GPL(nfs_check_cache_invalid);

#ifdef CONFIG_NFS_V4_2
static bool nfs_has_xattr_cache(const struct nfs_ianalde *nfsi)
{
	return nfsi->xattr_cache != NULL;
}
#else
static bool nfs_has_xattr_cache(const struct nfs_ianalde *nfsi)
{
	return false;
}
#endif

void nfs_set_cache_invalid(struct ianalde *ianalde, unsigned long flags)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	bool have_delegation = NFS_PROTO(ianalde)->have_delegation(ianalde, FMODE_READ);

	if (have_delegation) {
		if (!(flags & NFS_IANAL_REVAL_FORCED))
			flags &= ~(NFS_IANAL_INVALID_MODE |
				   NFS_IANAL_INVALID_OTHER |
				   NFS_IANAL_INVALID_XATTR);
		flags &= ~(NFS_IANAL_INVALID_CHANGE | NFS_IANAL_INVALID_SIZE);
	}

	if (!nfs_has_xattr_cache(nfsi))
		flags &= ~NFS_IANAL_INVALID_XATTR;
	if (flags & NFS_IANAL_INVALID_DATA)
		nfs_fscache_invalidate(ianalde, 0);
	flags &= ~NFS_IANAL_REVAL_FORCED;

	nfsi->cache_validity |= flags;

	if (ianalde->i_mapping->nrpages == 0) {
		nfsi->cache_validity &= ~NFS_IANAL_INVALID_DATA;
		nfs_ooo_clear(nfsi);
	} else if (nfsi->cache_validity & NFS_IANAL_INVALID_DATA) {
		nfs_ooo_clear(nfsi);
	}
	trace_nfs_set_cache_invalid(ianalde, 0);
}
EXPORT_SYMBOL_GPL(nfs_set_cache_invalid);

/*
 * Invalidate the local caches
 */
static void nfs_zap_caches_locked(struct ianalde *ianalde)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	int mode = ianalde->i_mode;

	nfs_inc_stats(ianalde, NFSIOS_ATTRINVALIDATE);

	nfsi->attrtimeo = NFS_MINATTRTIMEO(ianalde);
	nfsi->attrtimeo_timestamp = jiffies;

	if (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode))
		nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_ATTR |
						     NFS_IANAL_INVALID_DATA |
						     NFS_IANAL_INVALID_ACCESS |
						     NFS_IANAL_INVALID_ACL |
						     NFS_IANAL_INVALID_XATTR);
	else
		nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_ATTR |
						     NFS_IANAL_INVALID_ACCESS |
						     NFS_IANAL_INVALID_ACL |
						     NFS_IANAL_INVALID_XATTR);
	nfs_zap_label_cache_locked(nfsi);
}

void nfs_zap_caches(struct ianalde *ianalde)
{
	spin_lock(&ianalde->i_lock);
	nfs_zap_caches_locked(ianalde);
	spin_unlock(&ianalde->i_lock);
}

void nfs_zap_mapping(struct ianalde *ianalde, struct address_space *mapping)
{
	if (mapping->nrpages != 0) {
		spin_lock(&ianalde->i_lock);
		nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_DATA);
		spin_unlock(&ianalde->i_lock);
	}
}

void nfs_zap_acl_cache(struct ianalde *ianalde)
{
	void (*clear_acl_cache)(struct ianalde *);

	clear_acl_cache = NFS_PROTO(ianalde)->clear_acl_cache;
	if (clear_acl_cache != NULL)
		clear_acl_cache(ianalde);
	spin_lock(&ianalde->i_lock);
	NFS_I(ianalde)->cache_validity &= ~NFS_IANAL_INVALID_ACL;
	spin_unlock(&ianalde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_zap_acl_cache);

void nfs_invalidate_atime(struct ianalde *ianalde)
{
	spin_lock(&ianalde->i_lock);
	nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_ATIME);
	spin_unlock(&ianalde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_invalidate_atime);

/*
 * Invalidate, but do analt unhash, the ianalde.
 * NB: must be called with ianalde->i_lock held!
 */
static void nfs_set_ianalde_stale_locked(struct ianalde *ianalde)
{
	set_bit(NFS_IANAL_STALE, &NFS_I(ianalde)->flags);
	nfs_zap_caches_locked(ianalde);
	trace_nfs_set_ianalde_stale(ianalde);
}

void nfs_set_ianalde_stale(struct ianalde *ianalde)
{
	spin_lock(&ianalde->i_lock);
	nfs_set_ianalde_stale_locked(ianalde);
	spin_unlock(&ianalde->i_lock);
}

struct nfs_find_desc {
	struct nfs_fh		*fh;
	struct nfs_fattr	*fattr;
};

/*
 * In NFSv3 we can have 64bit ianalde numbers. In order to support
 * this, and re-exported directories (also seen in NFSv2)
 * we are forced to allow 2 different ianaldes to have the same
 * i_ianal.
 */
static int
nfs_find_actor(struct ianalde *ianalde, void *opaque)
{
	struct nfs_find_desc	*desc = opaque;
	struct nfs_fh		*fh = desc->fh;
	struct nfs_fattr	*fattr = desc->fattr;

	if (NFS_FILEID(ianalde) != fattr->fileid)
		return 0;
	if (ianalde_wrong_type(ianalde, fattr->mode))
		return 0;
	if (nfs_compare_fh(NFS_FH(ianalde), fh))
		return 0;
	if (is_bad_ianalde(ianalde) || NFS_STALE(ianalde))
		return 0;
	return 1;
}

static int
nfs_init_locked(struct ianalde *ianalde, void *opaque)
{
	struct nfs_find_desc	*desc = opaque;
	struct nfs_fattr	*fattr = desc->fattr;

	set_nfs_fileid(ianalde, fattr->fileid);
	ianalde->i_mode = fattr->mode;
	nfs_copy_fh(NFS_FH(ianalde), desc->fh);
	return 0;
}

#ifdef CONFIG_NFS_V4_SECURITY_LABEL
static void nfs_clear_label_invalid(struct ianalde *ianalde)
{
	spin_lock(&ianalde->i_lock);
	NFS_I(ianalde)->cache_validity &= ~NFS_IANAL_INVALID_LABEL;
	spin_unlock(&ianalde->i_lock);
}

void nfs_setsecurity(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	int error;

	if (fattr->label == NULL)
		return;

	if ((fattr->valid & NFS_ATTR_FATTR_V4_SECURITY_LABEL) && ianalde->i_security) {
		error = security_ianalde_analtifysecctx(ianalde, fattr->label->label,
				fattr->label->len);
		if (error)
			printk(KERN_ERR "%s() %s %d "
					"security_ianalde_analtifysecctx() %d\n",
					__func__,
					(char *)fattr->label->label,
					fattr->label->len, error);
		nfs_clear_label_invalid(ianalde);
	}
}

struct nfs4_label *nfs4_label_alloc(struct nfs_server *server, gfp_t flags)
{
	struct nfs4_label *label;

	if (!(server->caps & NFS_CAP_SECURITY_LABEL))
		return NULL;

	label = kzalloc(sizeof(struct nfs4_label), flags);
	if (label == NULL)
		return ERR_PTR(-EANALMEM);

	label->label = kzalloc(NFS4_MAXLABELLEN, flags);
	if (label->label == NULL) {
		kfree(label);
		return ERR_PTR(-EANALMEM);
	}
	label->len = NFS4_MAXLABELLEN;

	return label;
}
EXPORT_SYMBOL_GPL(nfs4_label_alloc);
#else
void nfs_setsecurity(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
}
#endif
EXPORT_SYMBOL_GPL(nfs_setsecurity);

/* Search for ianalde identified by fh, fileid and i_mode in ianalde cache. */
struct ianalde *
nfs_ilookup(struct super_block *sb, struct nfs_fattr *fattr, struct nfs_fh *fh)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr,
	};
	struct ianalde *ianalde;
	unsigned long hash;

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID) ||
	    !(fattr->valid & NFS_ATTR_FATTR_TYPE))
		return NULL;

	hash = nfs_fattr_to_ianal_t(fattr);
	ianalde = ilookup5(sb, hash, nfs_find_actor, &desc);

	dprintk("%s: returning %p\n", __func__, ianalde);
	return ianalde;
}

static void nfs_ianalde_init_regular(struct nfs_ianalde *nfsi)
{
	atomic_long_set(&nfsi->nrequests, 0);
	atomic_long_set(&nfsi->redirtied_pages, 0);
	INIT_LIST_HEAD(&nfsi->commit_info.list);
	atomic_long_set(&nfsi->commit_info.ncommit, 0);
	atomic_set(&nfsi->commit_info.rpcs_out, 0);
	mutex_init(&nfsi->commit_mutex);
}

static void nfs_ianalde_init_dir(struct nfs_ianalde *nfsi)
{
	nfsi->cache_change_attribute = 0;
	memset(nfsi->cookieverf, 0, sizeof(nfsi->cookieverf));
	init_rwsem(&nfsi->rmdir_sem);
}

/*
 * This is our front-end to iget that looks up ianaldes by file handle
 * instead of ianalde number.
 */
struct ianalde *
nfs_fhget(struct super_block *sb, struct nfs_fh *fh, struct nfs_fattr *fattr)
{
	struct nfs_find_desc desc = {
		.fh	= fh,
		.fattr	= fattr
	};
	struct ianalde *ianalde = ERR_PTR(-EANALENT);
	u64 fattr_supported = NFS_SB(sb)->fattr_valid;
	unsigned long hash;

	nfs_attr_check_mountpoint(sb, fattr);

	if (nfs_attr_use_mounted_on_fileid(fattr))
		fattr->fileid = fattr->mounted_on_fileid;
	else if ((fattr->valid & NFS_ATTR_FATTR_FILEID) == 0)
		goto out_anal_ianalde;
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) == 0)
		goto out_anal_ianalde;

	hash = nfs_fattr_to_ianal_t(fattr);

	ianalde = iget5_locked(sb, hash, nfs_find_actor, nfs_init_locked, &desc);
	if (ianalde == NULL) {
		ianalde = ERR_PTR(-EANALMEM);
		goto out_anal_ianalde;
	}

	if (ianalde->i_state & I_NEW) {
		struct nfs_ianalde *nfsi = NFS_I(ianalde);
		unsigned long analw = jiffies;

		/* We set i_ianal for the few things that still rely on it,
		 * such as stat(2) */
		ianalde->i_ianal = hash;

		/* We can't support update_atime(), since the server will reset it */
		ianalde->i_flags |= S_ANALATIME|S_ANALCMTIME;
		ianalde->i_mode = fattr->mode;
		nfsi->cache_validity = 0;
		if ((fattr->valid & NFS_ATTR_FATTR_MODE) == 0
				&& (fattr_supported & NFS_ATTR_FATTR_MODE))
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_MODE);
		/* Why so? Because we want revalidate for devices/FIFOs, and
		 * that's precisely what we have in nfs_file_ianalde_operations.
		 */
		ianalde->i_op = NFS_SB(sb)->nfs_client->rpc_ops->file_ianalde_ops;
		if (S_ISREG(ianalde->i_mode)) {
			ianalde->i_fop = NFS_SB(sb)->nfs_client->rpc_ops->file_ops;
			ianalde->i_data.a_ops = &nfs_file_aops;
			nfs_ianalde_init_regular(nfsi);
		} else if (S_ISDIR(ianalde->i_mode)) {
			ianalde->i_op = NFS_SB(sb)->nfs_client->rpc_ops->dir_ianalde_ops;
			ianalde->i_fop = &nfs_dir_operations;
			ianalde->i_data.a_ops = &nfs_dir_aops;
			nfs_ianalde_init_dir(nfsi);
			/* Deal with crossing mountpoints */
			if (fattr->valid & NFS_ATTR_FATTR_MOUNTPOINT ||
					fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL) {
				if (fattr->valid & NFS_ATTR_FATTR_V4_REFERRAL)
					ianalde->i_op = &nfs_referral_ianalde_operations;
				else
					ianalde->i_op = &nfs_mountpoint_ianalde_operations;
				ianalde->i_fop = NULL;
				ianalde->i_flags |= S_AUTOMOUNT;
			}
		} else if (S_ISLNK(ianalde->i_mode)) {
			ianalde->i_op = &nfs_symlink_ianalde_operations;
			ianalde_analhighmem(ianalde);
		} else
			init_special_ianalde(ianalde, ianalde->i_mode, fattr->rdev);

		ianalde_set_atime(ianalde, 0, 0);
		ianalde_set_mtime(ianalde, 0, 0);
		ianalde_set_ctime(ianalde, 0, 0);
		ianalde_set_iversion_raw(ianalde, 0);
		ianalde->i_size = 0;
		clear_nlink(ianalde);
		ianalde->i_uid = make_kuid(&init_user_ns, -2);
		ianalde->i_gid = make_kgid(&init_user_ns, -2);
		ianalde->i_blocks = 0;
		nfsi->write_io = 0;
		nfsi->read_io = 0;

		nfsi->read_cache_jiffies = fattr->time_start;
		nfsi->attr_gencount = fattr->gencount;
		if (fattr->valid & NFS_ATTR_FATTR_ATIME)
			ianalde_set_atime_to_ts(ianalde, fattr->atime);
		else if (fattr_supported & NFS_ATTR_FATTR_ATIME)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_ATIME);
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			ianalde_set_mtime_to_ts(ianalde, fattr->mtime);
		else if (fattr_supported & NFS_ATTR_FATTR_MTIME)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_MTIME);
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			ianalde_set_ctime_to_ts(ianalde, fattr->ctime);
		else if (fattr_supported & NFS_ATTR_FATTR_CTIME)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			ianalde_set_iversion_raw(ianalde, fattr->change_attr);
		else
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_CHANGE);
		if (fattr->valid & NFS_ATTR_FATTR_SIZE)
			ianalde->i_size = nfs_size_to_loff_t(fattr->size);
		else
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_SIZE);
		if (fattr->valid & NFS_ATTR_FATTR_NLINK)
			set_nlink(ianalde, fattr->nlink);
		else if (fattr_supported & NFS_ATTR_FATTR_NLINK)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_NLINK);
		if (fattr->valid & NFS_ATTR_FATTR_OWNER)
			ianalde->i_uid = fattr->uid;
		else if (fattr_supported & NFS_ATTR_FATTR_OWNER)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_OTHER);
		if (fattr->valid & NFS_ATTR_FATTR_GROUP)
			ianalde->i_gid = fattr->gid;
		else if (fattr_supported & NFS_ATTR_FATTR_GROUP)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_OTHER);
		if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
			ianalde->i_blocks = fattr->du.nfs2.blocks;
		else if (fattr_supported & NFS_ATTR_FATTR_BLOCKS_USED &&
			 fattr->size != 0)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_BLOCKS);
		if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
			/*
			 * report the blocks in 512byte units
			 */
			ianalde->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
		} else if (fattr_supported & NFS_ATTR_FATTR_SPACE_USED &&
			   fattr->size != 0)
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_BLOCKS);

		nfs_setsecurity(ianalde, fattr);

		nfsi->attrtimeo = NFS_MINATTRTIMEO(ianalde);
		nfsi->attrtimeo_timestamp = analw;
		nfsi->access_cache = RB_ROOT;

		nfs_fscache_init_ianalde(ianalde);

		unlock_new_ianalde(ianalde);
	} else {
		int err = nfs_refresh_ianalde(ianalde, fattr);
		if (err < 0) {
			iput(ianalde);
			ianalde = ERR_PTR(err);
			goto out_anal_ianalde;
		}
	}
	dprintk("NFS: nfs_fhget(%s/%Lu fh_crc=0x%08x ct=%d)\n",
		ianalde->i_sb->s_id,
		(unsigned long long)NFS_FILEID(ianalde),
		nfs_display_fhandle_hash(fh),
		atomic_read(&ianalde->i_count));

out:
	return ianalde;

out_anal_ianalde:
	dprintk("nfs_fhget: iget failed with error %ld\n", PTR_ERR(ianalde));
	goto out;
}
EXPORT_SYMBOL_GPL(nfs_fhget);

#define NFS_VALID_ATTRS (ATTR_MODE|ATTR_UID|ATTR_GID|ATTR_SIZE|ATTR_ATIME|ATTR_ATIME_SET|ATTR_MTIME|ATTR_MTIME_SET|ATTR_FILE|ATTR_OPEN)

int
nfs_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
	    struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct nfs_fattr *fattr;
	int error = 0;

	nfs_inc_stats(ianalde, NFSIOS_VFSSETATTR);

	/* skip mode change if it's just for clearing setuid/setgid */
	if (attr->ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		attr->ia_valid &= ~ATTR_MODE;

	if (attr->ia_valid & ATTR_SIZE) {
		BUG_ON(!S_ISREG(ianalde->i_mode));

		error = ianalde_newsize_ok(ianalde, attr->ia_size);
		if (error)
			return error;

		if (attr->ia_size == i_size_read(ianalde))
			attr->ia_valid &= ~ATTR_SIZE;
	}

	/* Optimization: if the end result is anal change, don't RPC */
	if (((attr->ia_valid & NFS_VALID_ATTRS) & ~(ATTR_FILE|ATTR_OPEN)) == 0)
		return 0;

	trace_nfs_setattr_enter(ianalde);

	/* Write all dirty data */
	if (S_ISREG(ianalde->i_mode))
		nfs_sync_ianalde(ianalde);

	fattr = nfs_alloc_fattr_with_label(NFS_SERVER(ianalde));
	if (fattr == NULL) {
		error = -EANALMEM;
		goto out;
	}

	error = NFS_PROTO(ianalde)->setattr(dentry, fattr, attr);
	if (error == 0)
		error = nfs_refresh_ianalde(ianalde, fattr);
	nfs_free_fattr(fattr);
out:
	trace_nfs_setattr_exit(ianalde, error);
	return error;
}
EXPORT_SYMBOL_GPL(nfs_setattr);

/**
 * nfs_vmtruncate - unmap mappings "freed" by truncate() syscall
 * @ianalde: ianalde of the file used
 * @offset: file offset to start truncating
 *
 * This is a copy of the common vmtruncate, but with the locking
 * corrected to take into account the fact that NFS requires
 * ianalde->i_size to be updated under the ianalde->i_lock.
 * Analte: must be called with ianalde->i_lock held!
 */
static int nfs_vmtruncate(struct ianalde * ianalde, loff_t offset)
{
	int err;

	err = ianalde_newsize_ok(ianalde, offset);
	if (err)
		goto out;

	trace_nfs_size_truncate(ianalde, offset);
	i_size_write(ianalde, offset);
	/* Optimisation */
	if (offset == 0) {
		NFS_I(ianalde)->cache_validity &= ~NFS_IANAL_INVALID_DATA;
		nfs_ooo_clear(NFS_I(ianalde));
	}
	NFS_I(ianalde)->cache_validity &= ~NFS_IANAL_INVALID_SIZE;

	spin_unlock(&ianalde->i_lock);
	truncate_pagecache(ianalde, offset);
	spin_lock(&ianalde->i_lock);
out:
	return err;
}

/**
 * nfs_setattr_update_ianalde - Update ianalde metadata after a setattr call.
 * @ianalde: pointer to struct ianalde
 * @attr: pointer to struct iattr
 * @fattr: pointer to struct nfs_fattr
 *
 * Analte: we do this in the *proc.c in order to ensure that
 *       it works for things like exclusive creates too.
 */
void nfs_setattr_update_ianalde(struct ianalde *ianalde, struct iattr *attr,
		struct nfs_fattr *fattr)
{
	/* Barrier: bump the attribute generation count. */
	nfs_fattr_set_barrier(fattr);

	spin_lock(&ianalde->i_lock);
	NFS_I(ianalde)->attr_gencount = fattr->gencount;
	if ((attr->ia_valid & ATTR_SIZE) != 0) {
		nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_MTIME |
						     NFS_IANAL_INVALID_BLOCKS);
		nfs_inc_stats(ianalde, NFSIOS_SETATTRTRUNC);
		nfs_vmtruncate(ianalde, attr->ia_size);
	}
	if ((attr->ia_valid & (ATTR_MODE|ATTR_UID|ATTR_GID)) != 0) {
		NFS_I(ianalde)->cache_validity &= ~NFS_IANAL_INVALID_CTIME;
		if ((attr->ia_valid & ATTR_KILL_SUID) != 0 &&
		    ianalde->i_mode & S_ISUID)
			ianalde->i_mode &= ~S_ISUID;
		if (setattr_should_drop_sgid(&analp_mnt_idmap, ianalde))
			ianalde->i_mode &= ~S_ISGID;
		if ((attr->ia_valid & ATTR_MODE) != 0) {
			int mode = attr->ia_mode & S_IALLUGO;
			mode |= ianalde->i_mode & ~S_IALLUGO;
			ianalde->i_mode = mode;
		}
		if ((attr->ia_valid & ATTR_UID) != 0)
			ianalde->i_uid = attr->ia_uid;
		if ((attr->ia_valid & ATTR_GID) != 0)
			ianalde->i_gid = attr->ia_gid;
		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			ianalde_set_ctime_to_ts(ianalde, fattr->ctime);
		else
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_CHANGE
					| NFS_IANAL_INVALID_CTIME);
		nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_ACCESS
				| NFS_IANAL_INVALID_ACL);
	}
	if (attr->ia_valid & (ATTR_ATIME_SET|ATTR_ATIME)) {
		NFS_I(ianalde)->cache_validity &= ~(NFS_IANAL_INVALID_ATIME
				| NFS_IANAL_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_ATIME)
			ianalde_set_atime_to_ts(ianalde, fattr->atime);
		else if (attr->ia_valid & ATTR_ATIME_SET)
			ianalde_set_atime_to_ts(ianalde, attr->ia_atime);
		else
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_ATIME);

		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			ianalde_set_ctime_to_ts(ianalde, fattr->ctime);
		else
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_CHANGE
					| NFS_IANAL_INVALID_CTIME);
	}
	if (attr->ia_valid & (ATTR_MTIME_SET|ATTR_MTIME)) {
		NFS_I(ianalde)->cache_validity &= ~(NFS_IANAL_INVALID_MTIME
				| NFS_IANAL_INVALID_CTIME);
		if (fattr->valid & NFS_ATTR_FATTR_MTIME)
			ianalde_set_mtime_to_ts(ianalde, fattr->mtime);
		else if (attr->ia_valid & ATTR_MTIME_SET)
			ianalde_set_mtime_to_ts(ianalde, attr->ia_mtime);
		else
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_MTIME);

		if (fattr->valid & NFS_ATTR_FATTR_CTIME)
			ianalde_set_ctime_to_ts(ianalde, fattr->ctime);
		else
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_CHANGE
					| NFS_IANAL_INVALID_CTIME);
	}
	if (fattr->valid)
		nfs_update_ianalde(ianalde, fattr);
	spin_unlock(&ianalde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_setattr_update_ianalde);

/*
 * Don't request help from readdirplus if the file is being written to,
 * or if attribute caching is turned off
 */
static bool nfs_getattr_readdirplus_enable(const struct ianalde *ianalde)
{
	return nfs_server_capable(ianalde, NFS_CAP_READDIRPLUS) &&
	       !nfs_have_writebacks(ianalde) && NFS_MAXATTRTIMEO(ianalde) > 5 * HZ;
}

static void nfs_readdirplus_parent_cache_miss(struct dentry *dentry)
{
	if (!IS_ROOT(dentry)) {
		struct dentry *parent = dget_parent(dentry);
		nfs_readdir_record_entry_cache_miss(d_ianalde(parent));
		dput(parent);
	}
}

static void nfs_readdirplus_parent_cache_hit(struct dentry *dentry)
{
	if (!IS_ROOT(dentry)) {
		struct dentry *parent = dget_parent(dentry);
		nfs_readdir_record_entry_cache_hit(d_ianalde(parent));
		dput(parent);
	}
}

static u32 nfs_get_valid_attrmask(struct ianalde *ianalde)
{
	unsigned long cache_validity = READ_ONCE(NFS_I(ianalde)->cache_validity);
	u32 reply_mask = STATX_IANAL | STATX_TYPE;

	if (!(cache_validity & NFS_IANAL_INVALID_ATIME))
		reply_mask |= STATX_ATIME;
	if (!(cache_validity & NFS_IANAL_INVALID_CTIME))
		reply_mask |= STATX_CTIME;
	if (!(cache_validity & NFS_IANAL_INVALID_MTIME))
		reply_mask |= STATX_MTIME;
	if (!(cache_validity & NFS_IANAL_INVALID_SIZE))
		reply_mask |= STATX_SIZE;
	if (!(cache_validity & NFS_IANAL_INVALID_NLINK))
		reply_mask |= STATX_NLINK;
	if (!(cache_validity & NFS_IANAL_INVALID_MODE))
		reply_mask |= STATX_MODE;
	if (!(cache_validity & NFS_IANAL_INVALID_OTHER))
		reply_mask |= STATX_UID | STATX_GID;
	if (!(cache_validity & NFS_IANAL_INVALID_BLOCKS))
		reply_mask |= STATX_BLOCKS;
	if (!(cache_validity & NFS_IANAL_INVALID_CHANGE))
		reply_mask |= STATX_CHANGE_COOKIE;
	return reply_mask;
}

int nfs_getattr(struct mnt_idmap *idmap, const struct path *path,
		struct kstat *stat, u32 request_mask, unsigned int query_flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct nfs_server *server = NFS_SERVER(ianalde);
	unsigned long cache_validity;
	int err = 0;
	bool force_sync = query_flags & AT_STATX_FORCE_SYNC;
	bool do_update = false;
	bool readdirplus_enabled = nfs_getattr_readdirplus_enable(ianalde);

	trace_nfs_getattr_enter(ianalde);

	request_mask &= STATX_TYPE | STATX_MODE | STATX_NLINK | STATX_UID |
			STATX_GID | STATX_ATIME | STATX_MTIME | STATX_CTIME |
			STATX_IANAL | STATX_SIZE | STATX_BLOCKS |
			STATX_CHANGE_COOKIE;

	if ((query_flags & AT_STATX_DONT_SYNC) && !force_sync) {
		if (readdirplus_enabled)
			nfs_readdirplus_parent_cache_hit(path->dentry);
		goto out_anal_revalidate;
	}

	/* Flush out writes to the server in order to update c/mtime/version.  */
	if ((request_mask & (STATX_CTIME | STATX_MTIME | STATX_CHANGE_COOKIE)) &&
	    S_ISREG(ianalde->i_mode))
		filemap_write_and_wait(ianalde->i_mapping);

	/*
	 * We may force a getattr if the user cares about atime.
	 *
	 * Analte that we only have to check the vfsmount flags here:
	 *  - NFS always sets S_ANALATIME by so checking it would give a
	 *    bogus result
	 *  - NFS never sets SB_ANALATIME or SB_ANALDIRATIME so there is
	 *    anal point in checking those.
	 */
	if ((path->mnt->mnt_flags & MNT_ANALATIME) ||
	    ((path->mnt->mnt_flags & MNT_ANALDIRATIME) && S_ISDIR(ianalde->i_mode)))
		request_mask &= ~STATX_ATIME;

	/* Is the user requesting attributes that might need revalidation? */
	if (!(request_mask & (STATX_MODE|STATX_NLINK|STATX_ATIME|STATX_CTIME|
					STATX_MTIME|STATX_UID|STATX_GID|
					STATX_SIZE|STATX_BLOCKS|
					STATX_CHANGE_COOKIE)))
		goto out_anal_revalidate;

	/* Check whether the cached attributes are stale */
	do_update |= force_sync || nfs_attribute_cache_expired(ianalde);
	cache_validity = READ_ONCE(NFS_I(ianalde)->cache_validity);
	do_update |= cache_validity & NFS_IANAL_INVALID_CHANGE;
	if (request_mask & STATX_ATIME)
		do_update |= cache_validity & NFS_IANAL_INVALID_ATIME;
	if (request_mask & STATX_CTIME)
		do_update |= cache_validity & NFS_IANAL_INVALID_CTIME;
	if (request_mask & STATX_MTIME)
		do_update |= cache_validity & NFS_IANAL_INVALID_MTIME;
	if (request_mask & STATX_SIZE)
		do_update |= cache_validity & NFS_IANAL_INVALID_SIZE;
	if (request_mask & STATX_NLINK)
		do_update |= cache_validity & NFS_IANAL_INVALID_NLINK;
	if (request_mask & STATX_MODE)
		do_update |= cache_validity & NFS_IANAL_INVALID_MODE;
	if (request_mask & (STATX_UID | STATX_GID))
		do_update |= cache_validity & NFS_IANAL_INVALID_OTHER;
	if (request_mask & STATX_BLOCKS)
		do_update |= cache_validity & NFS_IANAL_INVALID_BLOCKS;

	if (do_update) {
		if (readdirplus_enabled)
			nfs_readdirplus_parent_cache_miss(path->dentry);
		err = __nfs_revalidate_ianalde(server, ianalde);
		if (err)
			goto out;
	} else if (readdirplus_enabled)
		nfs_readdirplus_parent_cache_hit(path->dentry);
out_anal_revalidate:
	/* Only return attributes that were revalidated. */
	stat->result_mask = nfs_get_valid_attrmask(ianalde) | request_mask;

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
	stat->ianal = nfs_compat_user_ianal64(NFS_FILEID(ianalde));
	stat->change_cookie = ianalde_peek_iversion_raw(ianalde);
	stat->attributes_mask |= STATX_ATTR_CHANGE_MOANALTONIC;
	if (server->change_attr_type != NFS4_CHANGE_TYPE_IS_UNDEFINED)
		stat->attributes |= STATX_ATTR_CHANGE_MOANALTONIC;
	if (S_ISDIR(ianalde->i_mode))
		stat->blksize = NFS_SERVER(ianalde)->dtsize;
out:
	trace_nfs_getattr_exit(ianalde, err);
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
		if (refcount_inc_analt_zero(&pos->count))
			return pos;
	}
	return NULL;
}

struct nfs_lock_context *nfs_get_lock_context(struct nfs_open_context *ctx)
{
	struct nfs_lock_context *res, *new = NULL;
	struct ianalde *ianalde = d_ianalde(ctx->dentry);

	rcu_read_lock();
	res = __nfs_find_lock_context(ctx);
	rcu_read_unlock();
	if (res == NULL) {
		new = kmalloc(sizeof(*new), GFP_KERNEL_ACCOUNT);
		if (new == NULL)
			return ERR_PTR(-EANALMEM);
		nfs_init_lock_context(new);
		spin_lock(&ianalde->i_lock);
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
		spin_unlock(&ianalde->i_lock);
		kfree(new);
	}
	return res;
}
EXPORT_SYMBOL_GPL(nfs_get_lock_context);

void nfs_put_lock_context(struct nfs_lock_context *l_ctx)
{
	struct nfs_open_context *ctx = l_ctx->open_context;
	struct ianalde *ianalde = d_ianalde(ctx->dentry);

	if (!refcount_dec_and_lock(&l_ctx->count, &ianalde->i_lock))
		return;
	list_del_rcu(&l_ctx->list);
	spin_unlock(&ianalde->i_lock);
	put_nfs_open_context(ctx);
	kfree_rcu(l_ctx, rcu_head);
}
EXPORT_SYMBOL_GPL(nfs_put_lock_context);

/**
 * nfs_close_context - Common close_context() routine NFSv2/v3
 * @ctx: pointer to context
 * @is_sync: is this a synchroanalus close
 *
 * Ensure that the attributes are up to date if we're mounted
 * with close-to-open semantics and we have cached data that will
 * need to be revalidated on open.
 */
void nfs_close_context(struct nfs_open_context *ctx, int is_sync)
{
	struct nfs_ianalde *nfsi;
	struct ianalde *ianalde;

	if (!(ctx->mode & FMODE_WRITE))
		return;
	if (!is_sync)
		return;
	ianalde = d_ianalde(ctx->dentry);
	if (NFS_PROTO(ianalde)->have_delegation(ianalde, FMODE_READ))
		return;
	nfsi = NFS_I(ianalde);
	if (ianalde->i_mapping->nrpages == 0)
		return;
	if (nfsi->cache_validity & NFS_IANAL_INVALID_DATA)
		return;
	if (!list_empty(&nfsi->open_files))
		return;
	if (NFS_SERVER(ianalde)->flags & NFS_MOUNT_ANALCTO)
		return;
	nfs_revalidate_ianalde(ianalde,
			     NFS_IANAL_INVALID_CHANGE | NFS_IANAL_INVALID_SIZE);
}
EXPORT_SYMBOL_GPL(nfs_close_context);

struct nfs_open_context *alloc_nfs_open_context(struct dentry *dentry,
						fmode_t f_mode,
						struct file *filp)
{
	struct nfs_open_context *ctx;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL_ACCOUNT);
	if (!ctx)
		return ERR_PTR(-EANALMEM);
	nfs_sb_active(dentry->d_sb);
	ctx->dentry = dget(dentry);
	if (filp)
		ctx->cred = get_cred(filp->f_cred);
	else
		ctx->cred = get_current_cred();
	rcu_assign_pointer(ctx->ll_cred, NULL);
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
	if (ctx != NULL && refcount_inc_analt_zero(&ctx->lock_context.count))
		return ctx;
	return NULL;
}
EXPORT_SYMBOL_GPL(get_nfs_open_context);

static void __put_nfs_open_context(struct nfs_open_context *ctx, int is_sync)
{
	struct ianalde *ianalde = d_ianalde(ctx->dentry);
	struct super_block *sb = ctx->dentry->d_sb;

	if (!refcount_dec_and_test(&ctx->lock_context.count))
		return;
	if (!list_empty(&ctx->list)) {
		spin_lock(&ianalde->i_lock);
		list_del_rcu(&ctx->list);
		spin_unlock(&ianalde->i_lock);
	}
	if (ianalde != NULL)
		NFS_PROTO(ianalde)->close_context(ctx, is_sync);
	put_cred(ctx->cred);
	dput(ctx->dentry);
	nfs_sb_deactive(sb);
	put_rpccred(rcu_dereference_protected(ctx->ll_cred, 1));
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
void nfs_ianalde_attach_open_context(struct nfs_open_context *ctx)
{
	struct ianalde *ianalde = d_ianalde(ctx->dentry);
	struct nfs_ianalde *nfsi = NFS_I(ianalde);

	spin_lock(&ianalde->i_lock);
	if (list_empty(&nfsi->open_files) &&
	    nfs_ooo_test(nfsi))
		nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_DATA |
						     NFS_IANAL_REVAL_FORCED);
	list_add_tail_rcu(&ctx->list, &nfsi->open_files);
	spin_unlock(&ianalde->i_lock);
}
EXPORT_SYMBOL_GPL(nfs_ianalde_attach_open_context);

void nfs_file_set_open_context(struct file *filp, struct nfs_open_context *ctx)
{
	filp->private_data = get_nfs_open_context(ctx);
	set_bit(NFS_CONTEXT_FILE_OPEN, &ctx->flags);
	if (list_empty(&ctx->list))
		nfs_ianalde_attach_open_context(ctx);
}
EXPORT_SYMBOL_GPL(nfs_file_set_open_context);

/*
 * Given an ianalde, search for an open context with the desired characteristics
 */
struct nfs_open_context *nfs_find_open_context(struct ianalde *ianalde, const struct cred *cred, fmode_t mode)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
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
		struct ianalde *ianalde = d_ianalde(ctx->dentry);

		clear_bit(NFS_CONTEXT_FILE_OPEN, &ctx->flags);
		/*
		 * We fatal error on write before. Try to writeback
		 * every page again.
		 */
		if (ctx->error < 0)
			invalidate_ianalde_pages2(ianalde->i_mapping);
		filp->private_data = NULL;
		put_nfs_open_context_sync(ctx);
	}
}

/*
 * These allocate and release file read/write context information.
 */
int nfs_open(struct ianalde *ianalde, struct file *filp)
{
	struct nfs_open_context *ctx;

	ctx = alloc_nfs_open_context(file_dentry(filp),
				     flags_to_mode(filp->f_flags), filp);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);
	nfs_file_set_open_context(filp, ctx);
	put_nfs_open_context(ctx);
	nfs_fscache_open_file(ianalde, filp);
	return 0;
}

/*
 * This function is called whenever some part of NFS analtices that
 * the cached attributes have to be refreshed.
 */
int
__nfs_revalidate_ianalde(struct nfs_server *server, struct ianalde *ianalde)
{
	int		 status = -ESTALE;
	struct nfs_fattr *fattr = NULL;
	struct nfs_ianalde *nfsi = NFS_I(ianalde);

	dfprintk(PAGECACHE, "NFS: revalidating (%s/%Lu)\n",
		ianalde->i_sb->s_id, (unsigned long long)NFS_FILEID(ianalde));

	trace_nfs_revalidate_ianalde_enter(ianalde);

	if (is_bad_ianalde(ianalde))
		goto out;
	if (NFS_STALE(ianalde))
		goto out;

	/* pNFS: Attributes aren't updated until we layoutcommit */
	if (S_ISREG(ianalde->i_mode)) {
		status = pnfs_sync_ianalde(ianalde, false);
		if (status)
			goto out;
	}

	status = -EANALMEM;
	fattr = nfs_alloc_fattr_with_label(NFS_SERVER(ianalde));
	if (fattr == NULL)
		goto out;

	nfs_inc_stats(ianalde, NFSIOS_IANALDEREVALIDATE);

	status = NFS_PROTO(ianalde)->getattr(server, NFS_FH(ianalde), fattr, ianalde);
	if (status != 0) {
		dfprintk(PAGECACHE, "nfs_revalidate_ianalde: (%s/%Lu) getattr failed, error=%d\n",
			 ianalde->i_sb->s_id,
			 (unsigned long long)NFS_FILEID(ianalde), status);
		switch (status) {
		case -ETIMEDOUT:
			/* A soft timeout occurred. Use cached information? */
			if (server->flags & NFS_MOUNT_SOFTREVAL)
				status = 0;
			break;
		case -ESTALE:
			if (!S_ISDIR(ianalde->i_mode))
				nfs_set_ianalde_stale(ianalde);
			else
				nfs_zap_caches(ianalde);
		}
		goto out;
	}

	status = nfs_refresh_ianalde(ianalde, fattr);
	if (status) {
		dfprintk(PAGECACHE, "nfs_revalidate_ianalde: (%s/%Lu) refresh failed, error=%d\n",
			 ianalde->i_sb->s_id,
			 (unsigned long long)NFS_FILEID(ianalde), status);
		goto out;
	}

	if (nfsi->cache_validity & NFS_IANAL_INVALID_ACL)
		nfs_zap_acl_cache(ianalde);

	nfs_setsecurity(ianalde, fattr);

	dfprintk(PAGECACHE, "NFS: (%s/%Lu) revalidation complete\n",
		ianalde->i_sb->s_id,
		(unsigned long long)NFS_FILEID(ianalde));

out:
	nfs_free_fattr(fattr);
	trace_nfs_revalidate_ianalde_exit(ianalde, status);
	return status;
}

int nfs_attribute_cache_expired(struct ianalde *ianalde)
{
	if (nfs_have_delegated_attributes(ianalde))
		return 0;
	return nfs_attribute_timeout(ianalde);
}

/**
 * nfs_revalidate_ianalde - Revalidate the ianalde attributes
 * @ianalde: pointer to ianalde struct
 * @flags: cache flags to check
 *
 * Updates ianalde attribute information by retrieving the data from the server.
 */
int nfs_revalidate_ianalde(struct ianalde *ianalde, unsigned long flags)
{
	if (!nfs_check_cache_invalid(ianalde, flags))
		return NFS_STALE(ianalde) ? -ESTALE : 0;
	return __nfs_revalidate_ianalde(NFS_SERVER(ianalde), ianalde);
}
EXPORT_SYMBOL_GPL(nfs_revalidate_ianalde);

static int nfs_invalidate_mapping(struct ianalde *ianalde, struct address_space *mapping)
{
	int ret;

	nfs_fscache_invalidate(ianalde, 0);
	if (mapping->nrpages != 0) {
		if (S_ISREG(ianalde->i_mode)) {
			ret = nfs_sync_mapping(mapping);
			if (ret < 0)
				return ret;
		}
		ret = invalidate_ianalde_pages2(mapping);
		if (ret < 0)
			return ret;
	}
	nfs_inc_stats(ianalde, NFSIOS_DATAINVALIDATE);

	dfprintk(PAGECACHE, "NFS: (%s/%Lu) data cache invalidated\n",
			ianalde->i_sb->s_id,
			(unsigned long long)NFS_FILEID(ianalde));
	return 0;
}

/**
 * nfs_clear_invalid_mapping - Conditionally clear a mapping
 * @mapping: pointer to mapping
 *
 * If the NFS_IANAL_INVALID_DATA ianalde flag is set, clear the mapping.
 */
int nfs_clear_invalid_mapping(struct address_space *mapping)
{
	struct ianalde *ianalde = mapping->host;
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	unsigned long *bitlock = &nfsi->flags;
	int ret = 0;

	/*
	 * We must clear NFS_IANAL_INVALID_DATA first to ensure that
	 * invalidations that come in while we're shooting down the mappings
	 * are respected. But, that leaves a race window where one revalidator
	 * can clear the flag, and then aanalther checks it before the mapping
	 * gets invalidated. Fix that by serializing access to this part of
	 * the function.
	 *
	 * At the same time, we need to allow other tasks to see whether we
	 * might be in the middle of invalidating the pages, so we only set
	 * the bit lock here if it looks like we're going to be doing that.
	 */
	for (;;) {
		ret = wait_on_bit_action(bitlock, NFS_IANAL_INVALIDATING,
					 nfs_wait_bit_killable,
					 TASK_KILLABLE|TASK_FREEZABLE_UNSAFE);
		if (ret)
			goto out;
		spin_lock(&ianalde->i_lock);
		if (test_bit(NFS_IANAL_INVALIDATING, bitlock)) {
			spin_unlock(&ianalde->i_lock);
			continue;
		}
		if (nfsi->cache_validity & NFS_IANAL_INVALID_DATA)
			break;
		spin_unlock(&ianalde->i_lock);
		goto out;
	}

	set_bit(NFS_IANAL_INVALIDATING, bitlock);
	smp_wmb();
	nfsi->cache_validity &= ~NFS_IANAL_INVALID_DATA;
	nfs_ooo_clear(nfsi);
	spin_unlock(&ianalde->i_lock);
	trace_nfs_invalidate_mapping_enter(ianalde);
	ret = nfs_invalidate_mapping(ianalde, mapping);
	trace_nfs_invalidate_mapping_exit(ianalde, ret);

	clear_bit_unlock(NFS_IANAL_INVALIDATING, bitlock);
	smp_mb__after_atomic();
	wake_up_bit(bitlock, NFS_IANAL_INVALIDATING);
out:
	return ret;
}

bool nfs_mapping_need_revalidate_ianalde(struct ianalde *ianalde)
{
	return nfs_check_cache_invalid(ianalde, NFS_IANAL_INVALID_CHANGE) ||
		NFS_STALE(ianalde);
}

int nfs_revalidate_mapping_rcu(struct ianalde *ianalde)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	unsigned long *bitlock = &nfsi->flags;
	int ret = 0;

	if (IS_SWAPFILE(ianalde))
		goto out;
	if (nfs_mapping_need_revalidate_ianalde(ianalde)) {
		ret = -ECHILD;
		goto out;
	}
	spin_lock(&ianalde->i_lock);
	if (test_bit(NFS_IANAL_INVALIDATING, bitlock) ||
	    (nfsi->cache_validity & NFS_IANAL_INVALID_DATA))
		ret = -ECHILD;
	spin_unlock(&ianalde->i_lock);
out:
	return ret;
}

/**
 * nfs_revalidate_mapping - Revalidate the pagecache
 * @ianalde: pointer to host ianalde
 * @mapping: pointer to mapping
 */
int nfs_revalidate_mapping(struct ianalde *ianalde, struct address_space *mapping)
{
	/* swapfiles are analt supposed to be shared. */
	if (IS_SWAPFILE(ianalde))
		return 0;

	if (nfs_mapping_need_revalidate_ianalde(ianalde)) {
		int ret = __nfs_revalidate_ianalde(NFS_SERVER(ianalde), ianalde);
		if (ret < 0)
			return ret;
	}

	return nfs_clear_invalid_mapping(mapping);
}

static bool nfs_file_has_writers(struct nfs_ianalde *nfsi)
{
	struct ianalde *ianalde = &nfsi->vfs_ianalde;

	if (!S_ISREG(ianalde->i_mode))
		return false;
	if (list_empty(&nfsi->open_files))
		return false;
	return ianalde_is_open_for_write(ianalde);
}

static bool nfs_file_has_buffered_writers(struct nfs_ianalde *nfsi)
{
	return nfs_file_has_writers(nfsi) && nfs_file_io_is_buffered(nfsi);
}

static void nfs_wcc_update_ianalde(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	struct timespec64 ts;

	if ((fattr->valid & NFS_ATTR_FATTR_PRECHANGE)
			&& (fattr->valid & NFS_ATTR_FATTR_CHANGE)
			&& ianalde_eq_iversion_raw(ianalde, fattr->pre_change_attr)) {
		ianalde_set_iversion_raw(ianalde, fattr->change_attr);
		if (S_ISDIR(ianalde->i_mode))
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_DATA);
		else if (nfs_server_capable(ianalde, NFS_CAP_XATTR))
			nfs_set_cache_invalid(ianalde, NFS_IANAL_INVALID_XATTR);
	}
	/* If we have atomic WCC data, we may update some attributes */
	ts = ianalde_get_ctime(ianalde);
	if ((fattr->valid & NFS_ATTR_FATTR_PRECTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_CTIME)
			&& timespec64_equal(&ts, &fattr->pre_ctime)) {
		ianalde_set_ctime_to_ts(ianalde, fattr->ctime);
	}

	ts = ianalde_get_mtime(ianalde);
	if ((fattr->valid & NFS_ATTR_FATTR_PREMTIME)
			&& (fattr->valid & NFS_ATTR_FATTR_MTIME)
			&& timespec64_equal(&ts, &fattr->pre_mtime)) {
		ianalde_set_mtime_to_ts(ianalde, fattr->mtime);
	}
	if ((fattr->valid & NFS_ATTR_FATTR_PRESIZE)
			&& (fattr->valid & NFS_ATTR_FATTR_SIZE)
			&& i_size_read(ianalde) == nfs_size_to_loff_t(fattr->pre_size)
			&& !nfs_have_writebacks(ianalde)) {
		trace_nfs_size_wcc(ianalde, fattr->size);
		i_size_write(ianalde, nfs_size_to_loff_t(fattr->size));
	}
}

/**
 * nfs_check_ianalde_attributes - verify consistency of the ianalde attribute cache
 * @ianalde: pointer to ianalde
 * @fattr: updated attributes
 *
 * Verifies the attribute cache. If we have just changed the attributes,
 * so that fattr carries weak cache consistency data, then it may
 * also update the ctime/mtime/change_attribute.
 */
static int nfs_check_ianalde_attributes(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	loff_t cur_size, new_isize;
	unsigned long invalid = 0;
	struct timespec64 ts;

	if (NFS_PROTO(ianalde)->have_delegation(ianalde, FMODE_READ))
		return 0;

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID)) {
		/* Only a mounted-on-fileid? Just exit */
		if (fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID)
			return 0;
	/* Has the ianalde gone and changed behind our back? */
	} else if (nfsi->fileid != fattr->fileid) {
		/* Is this perhaps the mounted-on fileid? */
		if ((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) &&
		    nfsi->fileid == fattr->mounted_on_fileid)
			return 0;
		return -ESTALE;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && ianalde_wrong_type(ianalde, fattr->mode))
		return -ESTALE;


	if (!nfs_file_has_buffered_writers(nfsi)) {
		/* Verify a few of the more important attributes */
		if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 && !ianalde_eq_iversion_raw(ianalde, fattr->change_attr))
			invalid |= NFS_IANAL_INVALID_CHANGE;

		ts = ianalde_get_mtime(ianalde);
		if ((fattr->valid & NFS_ATTR_FATTR_MTIME) && !timespec64_equal(&ts, &fattr->mtime))
			invalid |= NFS_IANAL_INVALID_MTIME;

		ts = ianalde_get_ctime(ianalde);
		if ((fattr->valid & NFS_ATTR_FATTR_CTIME) && !timespec64_equal(&ts, &fattr->ctime))
			invalid |= NFS_IANAL_INVALID_CTIME;

		if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
			cur_size = i_size_read(ianalde);
			new_isize = nfs_size_to_loff_t(fattr->size);
			if (cur_size != new_isize)
				invalid |= NFS_IANAL_INVALID_SIZE;
		}
	}

	/* Have any file permissions changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_MODE) && (ianalde->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO))
		invalid |= NFS_IANAL_INVALID_MODE;
	if ((fattr->valid & NFS_ATTR_FATTR_OWNER) && !uid_eq(ianalde->i_uid, fattr->uid))
		invalid |= NFS_IANAL_INVALID_OTHER;
	if ((fattr->valid & NFS_ATTR_FATTR_GROUP) && !gid_eq(ianalde->i_gid, fattr->gid))
		invalid |= NFS_IANAL_INVALID_OTHER;

	/* Has the link count changed? */
	if ((fattr->valid & NFS_ATTR_FATTR_NLINK) && ianalde->i_nlink != fattr->nlink)
		invalid |= NFS_IANAL_INVALID_NLINK;

	ts = ianalde_get_atime(ianalde);
	if ((fattr->valid & NFS_ATTR_FATTR_ATIME) && !timespec64_equal(&ts, &fattr->atime))
		invalid |= NFS_IANAL_INVALID_ATIME;

	if (invalid != 0)
		nfs_set_cache_invalid(ianalde, invalid);

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
 * have raced with our update cananalt clobber these new values.
 * Analte that you are still responsible for ensuring that other
 * operations which change the attribute on the server do analt
 * collide.
 */
void nfs_fattr_set_barrier(struct nfs_fattr *fattr)
{
	fattr->gencount = nfs_inc_attr_generation_counter();
}

struct nfs_fattr *nfs_alloc_fattr(void)
{
	struct nfs_fattr *fattr;

	fattr = kmalloc(sizeof(*fattr), GFP_KERNEL);
	if (fattr != NULL) {
		nfs_fattr_init(fattr);
		fattr->label = NULL;
	}
	return fattr;
}
EXPORT_SYMBOL_GPL(nfs_alloc_fattr);

struct nfs_fattr *nfs_alloc_fattr_with_label(struct nfs_server *server)
{
	struct nfs_fattr *fattr = nfs_alloc_fattr();

	if (!fattr)
		return NULL;

	fattr->label = nfs4_label_alloc(server, GFP_KERNEL);
	if (IS_ERR(fattr->label)) {
		kfree(fattr);
		return NULL;
	}

	return fattr;
}
EXPORT_SYMBOL_GPL(nfs_alloc_fattr_with_label);

struct nfs_fh *nfs_alloc_fhandle(void)
{
	struct nfs_fh *fh;

	fh = kmalloc(sizeof(struct nfs_fh), GFP_KERNEL);
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
	 * analt on the result */
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
 * nfs_ianalde_attrs_cmp_generic - compare attributes
 * @fattr: attributes
 * @ianalde: pointer to ianalde
 *
 * Attempt to divine whether or analt an RPC call reply carrying stale
 * attributes got scheduled after aanalther call carrying updated ones.
 * Analte also the check for wraparound of 'attr_gencount'
 *
 * The function returns '1' if it thinks the attributes in @fattr are
 * more recent than the ones cached in @ianalde. Otherwise it returns
 * the value '0'.
 */
static int nfs_ianalde_attrs_cmp_generic(const struct nfs_fattr *fattr,
				       const struct ianalde *ianalde)
{
	unsigned long attr_gencount = NFS_I(ianalde)->attr_gencount;

	return (long)(fattr->gencount - attr_gencount) > 0 ||
	       (long)(attr_gencount - nfs_read_attr_generation_counter()) > 0;
}

/**
 * nfs_ianalde_attrs_cmp_moanaltonic - compare attributes
 * @fattr: attributes
 * @ianalde: pointer to ianalde
 *
 * Attempt to divine whether or analt an RPC call reply carrying stale
 * attributes got scheduled after aanalther call carrying updated ones.
 *
 * We assume that the server observes moanaltonic semantics for
 * the change attribute, so a larger value means that the attributes in
 * @fattr are more recent, in which case the function returns the
 * value '1'.
 * A return value of '0' indicates anal measurable change
 * A return value of '-1' means that the attributes in @ianalde are
 * more recent.
 */
static int nfs_ianalde_attrs_cmp_moanaltonic(const struct nfs_fattr *fattr,
					 const struct ianalde *ianalde)
{
	s64 diff = fattr->change_attr - ianalde_peek_iversion_raw(ianalde);
	if (diff > 0)
		return 1;
	return diff == 0 ? 0 : -1;
}

/**
 * nfs_ianalde_attrs_cmp_strict_moanaltonic - compare attributes
 * @fattr: attributes
 * @ianalde: pointer to ianalde
 *
 * Attempt to divine whether or analt an RPC call reply carrying stale
 * attributes got scheduled after aanalther call carrying updated ones.
 *
 * We assume that the server observes strictly moanaltonic semantics for
 * the change attribute, so a larger value means that the attributes in
 * @fattr are more recent, in which case the function returns the
 * value '1'.
 * A return value of '-1' means that the attributes in @ianalde are
 * more recent or unchanged.
 */
static int nfs_ianalde_attrs_cmp_strict_moanaltonic(const struct nfs_fattr *fattr,
						const struct ianalde *ianalde)
{
	return  nfs_ianalde_attrs_cmp_moanaltonic(fattr, ianalde) > 0 ? 1 : -1;
}

/**
 * nfs_ianalde_attrs_cmp - compare attributes
 * @fattr: attributes
 * @ianalde: pointer to ianalde
 *
 * This function returns '1' if it thinks the attributes in @fattr are
 * more recent than the ones cached in @ianalde. It returns '-1' if
 * the attributes in @ianalde are more recent than the ones in @fattr,
 * and it returns 0 if analt sure.
 */
static int nfs_ianalde_attrs_cmp(const struct nfs_fattr *fattr,
			       const struct ianalde *ianalde)
{
	if (nfs_ianalde_attrs_cmp_generic(fattr, ianalde) > 0)
		return 1;
	switch (NFS_SERVER(ianalde)->change_attr_type) {
	case NFS4_CHANGE_TYPE_IS_UNDEFINED:
		break;
	case NFS4_CHANGE_TYPE_IS_TIME_METADATA:
		if (!(fattr->valid & NFS_ATTR_FATTR_CHANGE))
			break;
		return nfs_ianalde_attrs_cmp_moanaltonic(fattr, ianalde);
	default:
		if (!(fattr->valid & NFS_ATTR_FATTR_CHANGE))
			break;
		return nfs_ianalde_attrs_cmp_strict_moanaltonic(fattr, ianalde);
	}
	return 0;
}

/**
 * nfs_ianalde_finish_partial_attr_update - complete a previous ianalde update
 * @fattr: attributes
 * @ianalde: pointer to ianalde
 *
 * Returns '1' if the last attribute update left the ianalde cached
 * attributes in a partially unrevalidated state, and @fattr
 * matches the change attribute of that partial update.
 * Otherwise returns '0'.
 */
static int nfs_ianalde_finish_partial_attr_update(const struct nfs_fattr *fattr,
						const struct ianalde *ianalde)
{
	const unsigned long check_valid =
		NFS_IANAL_INVALID_ATIME | NFS_IANAL_INVALID_CTIME |
		NFS_IANAL_INVALID_MTIME | NFS_IANAL_INVALID_SIZE |
		NFS_IANAL_INVALID_BLOCKS | NFS_IANAL_INVALID_OTHER |
		NFS_IANAL_INVALID_NLINK;
	unsigned long cache_validity = NFS_I(ianalde)->cache_validity;
	enum nfs4_change_attr_type ctype = NFS_SERVER(ianalde)->change_attr_type;

	if (ctype != NFS4_CHANGE_TYPE_IS_UNDEFINED &&
	    !(cache_validity & NFS_IANAL_INVALID_CHANGE) &&
	    (cache_validity & check_valid) != 0 &&
	    (fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
	    nfs_ianalde_attrs_cmp_moanaltonic(fattr, ianalde) == 0)
		return 1;
	return 0;
}

static void nfs_ooo_merge(struct nfs_ianalde *nfsi,
			  u64 start, u64 end)
{
	int i, cnt;

	if (nfsi->cache_validity & NFS_IANAL_DATA_INVAL_DEFER)
		/* Anal point merging anything */
		return;

	if (!nfsi->ooo) {
		nfsi->ooo = kmalloc(sizeof(*nfsi->ooo), GFP_ATOMIC);
		if (!nfsi->ooo) {
			nfsi->cache_validity |= NFS_IANAL_DATA_INVAL_DEFER;
			return;
		}
		nfsi->ooo->cnt = 0;
	}

	/* add this range, merging if possible */
	cnt = nfsi->ooo->cnt;
	for (i = 0; i < cnt; i++) {
		if (end == nfsi->ooo->gap[i].start)
			end = nfsi->ooo->gap[i].end;
		else if (start == nfsi->ooo->gap[i].end)
			start = nfsi->ooo->gap[i].start;
		else
			continue;
		/* Remove 'i' from table and loop to insert the new range */
		cnt -= 1;
		nfsi->ooo->gap[i] = nfsi->ooo->gap[cnt];
		i = -1;
	}
	if (start != end) {
		if (cnt >= ARRAY_SIZE(nfsi->ooo->gap)) {
			nfsi->cache_validity |= NFS_IANAL_DATA_INVAL_DEFER;
			kfree(nfsi->ooo);
			nfsi->ooo = NULL;
			return;
		}
		nfsi->ooo->gap[cnt].start = start;
		nfsi->ooo->gap[cnt].end = end;
		cnt += 1;
	}
	nfsi->ooo->cnt = cnt;
}

static void nfs_ooo_record(struct nfs_ianalde *nfsi,
			   struct nfs_fattr *fattr)
{
	/* This reply was out-of-order, so record in the
	 * pre/post change id, possibly cancelling
	 * gaps created when iversion was jumpped forward.
	 */
	if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) &&
	    (fattr->valid & NFS_ATTR_FATTR_PRECHANGE))
		nfs_ooo_merge(nfsi,
			      fattr->change_attr,
			      fattr->pre_change_attr);
}

static int nfs_refresh_ianalde_locked(struct ianalde *ianalde,
				    struct nfs_fattr *fattr)
{
	int attr_cmp = nfs_ianalde_attrs_cmp(fattr, ianalde);
	int ret = 0;

	trace_nfs_refresh_ianalde_enter(ianalde);

	if (attr_cmp > 0 || nfs_ianalde_finish_partial_attr_update(fattr, ianalde))
		ret = nfs_update_ianalde(ianalde, fattr);
	else {
		nfs_ooo_record(NFS_I(ianalde), fattr);

		if (attr_cmp == 0)
			ret = nfs_check_ianalde_attributes(ianalde, fattr);
	}

	trace_nfs_refresh_ianalde_exit(ianalde, ret);
	return ret;
}

/**
 * nfs_refresh_ianalde - try to update the ianalde attribute cache
 * @ianalde: pointer to ianalde
 * @fattr: updated attributes
 *
 * Check that an RPC call that returned attributes has analt overlapped with
 * other recent updates of the ianalde metadata, then decide whether it is
 * safe to do a full update of the ianalde attributes, or whether just to
 * call nfs_check_ianalde_attributes.
 */
int nfs_refresh_ianalde(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	int status;

	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	spin_lock(&ianalde->i_lock);
	status = nfs_refresh_ianalde_locked(ianalde, fattr);
	spin_unlock(&ianalde->i_lock);

	return status;
}
EXPORT_SYMBOL_GPL(nfs_refresh_ianalde);

static int nfs_post_op_update_ianalde_locked(struct ianalde *ianalde,
		struct nfs_fattr *fattr, unsigned int invalid)
{
	if (S_ISDIR(ianalde->i_mode))
		invalid |= NFS_IANAL_INVALID_DATA;
	nfs_set_cache_invalid(ianalde, invalid);
	if ((fattr->valid & NFS_ATTR_FATTR) == 0)
		return 0;
	return nfs_refresh_ianalde_locked(ianalde, fattr);
}

/**
 * nfs_post_op_update_ianalde - try to update the ianalde attribute cache
 * @ianalde: pointer to ianalde
 * @fattr: updated attributes
 *
 * After an operation that has changed the ianalde metadata, mark the
 * attribute cache as being invalid, then try to update it.
 *
 * NB: if the server didn't return any post op attributes, this
 * function will force the retrieval of attributes before the next
 * NFS request.  Thus it should be used only for operations that
 * are expected to change one or more attributes, to avoid
 * unnecessary NFS requests and trips through nfs_update_ianalde().
 */
int nfs_post_op_update_ianalde(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	int status;

	spin_lock(&ianalde->i_lock);
	nfs_fattr_set_barrier(fattr);
	status = nfs_post_op_update_ianalde_locked(ianalde, fattr,
			NFS_IANAL_INVALID_CHANGE
			| NFS_IANAL_INVALID_CTIME
			| NFS_IANAL_REVAL_FORCED);
	spin_unlock(&ianalde->i_lock);

	return status;
}
EXPORT_SYMBOL_GPL(nfs_post_op_update_ianalde);

/**
 * nfs_post_op_update_ianalde_force_wcc_locked - update the ianalde attribute cache
 * @ianalde: pointer to ianalde
 * @fattr: updated attributes
 *
 * After an operation that has changed the ianalde metadata, mark the
 * attribute cache as being invalid, then try to update it. Fake up
 * weak cache consistency data, if analne exist.
 *
 * This function is mainly designed to be used by the ->write_done() functions.
 */
int nfs_post_op_update_ianalde_force_wcc_locked(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	int attr_cmp = nfs_ianalde_attrs_cmp(fattr, ianalde);
	int status;

	/* Don't do a WCC update if these attributes are already stale */
	if (attr_cmp < 0)
		return 0;
	if ((fattr->valid & NFS_ATTR_FATTR) == 0 || !attr_cmp) {
		/* Record the pre/post change info before clearing PRECHANGE */
		nfs_ooo_record(NFS_I(ianalde), fattr);
		fattr->valid &= ~(NFS_ATTR_FATTR_PRECHANGE
				| NFS_ATTR_FATTR_PRESIZE
				| NFS_ATTR_FATTR_PREMTIME
				| NFS_ATTR_FATTR_PRECTIME);
		goto out_analforce;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CHANGE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECHANGE) == 0) {
		fattr->pre_change_attr = ianalde_peek_iversion_raw(ianalde);
		fattr->valid |= NFS_ATTR_FATTR_PRECHANGE;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_CTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRECTIME) == 0) {
		fattr->pre_ctime = ianalde_get_ctime(ianalde);
		fattr->valid |= NFS_ATTR_FATTR_PRECTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_MTIME) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PREMTIME) == 0) {
		fattr->pre_mtime = ianalde_get_mtime(ianalde);
		fattr->valid |= NFS_ATTR_FATTR_PREMTIME;
	}
	if ((fattr->valid & NFS_ATTR_FATTR_SIZE) != 0 &&
			(fattr->valid & NFS_ATTR_FATTR_PRESIZE) == 0) {
		fattr->pre_size = i_size_read(ianalde);
		fattr->valid |= NFS_ATTR_FATTR_PRESIZE;
	}
out_analforce:
	status = nfs_post_op_update_ianalde_locked(ianalde, fattr,
			NFS_IANAL_INVALID_CHANGE
			| NFS_IANAL_INVALID_CTIME
			| NFS_IANAL_INVALID_MTIME
			| NFS_IANAL_INVALID_BLOCKS);
	return status;
}

/**
 * nfs_post_op_update_ianalde_force_wcc - try to update the ianalde attribute cache
 * @ianalde: pointer to ianalde
 * @fattr: updated attributes
 *
 * After an operation that has changed the ianalde metadata, mark the
 * attribute cache as being invalid, then try to update it. Fake up
 * weak cache consistency data, if analne exist.
 *
 * This function is mainly designed to be used by the ->write_done() functions.
 */
int nfs_post_op_update_ianalde_force_wcc(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	int status;

	spin_lock(&ianalde->i_lock);
	nfs_fattr_set_barrier(fattr);
	status = nfs_post_op_update_ianalde_force_wcc_locked(ianalde, fattr);
	spin_unlock(&ianalde->i_lock);
	return status;
}
EXPORT_SYMBOL_GPL(nfs_post_op_update_ianalde_force_wcc);


/*
 * Many nfs protocol calls return the new file attributes after
 * an operation.  Here we update the ianalde to reflect the state
 * of the server's ianalde.
 *
 * This is a bit tricky because we have to make sure all dirty pages
 * have been sent off to the server before calling invalidate_ianalde_pages.
 * To make sure anal other process adds more write requests while we try
 * our best to flush them, we make them sleep during the attribute refresh.
 *
 * A very similar scenario holds for the dir cache.
 */
static int nfs_update_ianalde(struct ianalde *ianalde, struct nfs_fattr *fattr)
{
	struct nfs_server *server = NFS_SERVER(ianalde);
	struct nfs_ianalde *nfsi = NFS_I(ianalde);
	loff_t cur_isize, new_isize;
	u64 fattr_supported = server->fattr_valid;
	unsigned long invalid = 0;
	unsigned long analw = jiffies;
	unsigned long save_cache_validity;
	bool have_writers = nfs_file_has_buffered_writers(nfsi);
	bool cache_revalidated = true;
	bool attr_changed = false;
	bool have_delegation;

	dfprintk(VFS, "NFS: %s(%s/%lu fh_crc=0x%08x ct=%d info=0x%x)\n",
			__func__, ianalde->i_sb->s_id, ianalde->i_ianal,
			nfs_display_fhandle_hash(NFS_FH(ianalde)),
			atomic_read(&ianalde->i_count), fattr->valid);

	if (!(fattr->valid & NFS_ATTR_FATTR_FILEID)) {
		/* Only a mounted-on-fileid? Just exit */
		if (fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID)
			return 0;
	/* Has the ianalde gone and changed behind our back? */
	} else if (nfsi->fileid != fattr->fileid) {
		/* Is this perhaps the mounted-on fileid? */
		if ((fattr->valid & NFS_ATTR_FATTR_MOUNTED_ON_FILEID) &&
		    nfsi->fileid == fattr->mounted_on_fileid)
			return 0;
		printk(KERN_ERR "NFS: server %s error: fileid changed\n"
			"fsid %s: expected fileid 0x%Lx, got 0x%Lx\n",
			NFS_SERVER(ianalde)->nfs_client->cl_hostname,
			ianalde->i_sb->s_id, (long long)nfsi->fileid,
			(long long)fattr->fileid);
		goto out_err;
	}

	/*
	 * Make sure the ianalde's type hasn't changed.
	 */
	if ((fattr->valid & NFS_ATTR_FATTR_TYPE) && ianalde_wrong_type(ianalde, fattr->mode)) {
		/*
		* Big trouble! The ianalde has become a different object.
		*/
		printk(KERN_DEBUG "NFS: %s: ianalde %lu mode changed, %07o to %07o\n",
				__func__, ianalde->i_ianal, ianalde->i_mode, fattr->mode);
		goto out_err;
	}

	/* Update the fsid? */
	if (S_ISDIR(ianalde->i_mode) && (fattr->valid & NFS_ATTR_FATTR_FSID) &&
			!nfs_fsid_equal(&server->fsid, &fattr->fsid) &&
			!IS_AUTOMOUNT(ianalde))
		server->fsid = fattr->fsid;

	/* Save the delegation state before clearing cache_validity */
	have_delegation = nfs_have_delegated_attributes(ianalde);

	/*
	 * Update the read time so we don't revalidate too often.
	 */
	nfsi->read_cache_jiffies = fattr->time_start;

	save_cache_validity = nfsi->cache_validity;
	nfsi->cache_validity &= ~(NFS_IANAL_INVALID_ATTR
			| NFS_IANAL_INVALID_ATIME
			| NFS_IANAL_REVAL_FORCED
			| NFS_IANAL_INVALID_BLOCKS);

	/* Do atomic weak cache consistency updates */
	nfs_wcc_update_ianalde(ianalde, fattr);

	if (pnfs_layoutcommit_outstanding(ianalde)) {
		nfsi->cache_validity |=
			save_cache_validity &
			(NFS_IANAL_INVALID_CHANGE | NFS_IANAL_INVALID_CTIME |
			 NFS_IANAL_INVALID_MTIME | NFS_IANAL_INVALID_SIZE |
			 NFS_IANAL_INVALID_BLOCKS);
		cache_revalidated = false;
	}

	/* More cache consistency checks */
	if (fattr->valid & NFS_ATTR_FATTR_CHANGE) {
		if (!have_writers && nfsi->ooo && nfsi->ooo->cnt == 1 &&
		    nfsi->ooo->gap[0].end == ianalde_peek_iversion_raw(ianalde)) {
			/* There is one remaining gap that hasn't been
			 * merged into iversion - do that analw.
			 */
			ianalde_set_iversion_raw(ianalde, nfsi->ooo->gap[0].start);
			kfree(nfsi->ooo);
			nfsi->ooo = NULL;
		}
		if (!ianalde_eq_iversion_raw(ianalde, fattr->change_attr)) {
			/* Could it be a race with writeback? */
			if (!(have_writers || have_delegation)) {
				invalid |= NFS_IANAL_INVALID_DATA
					| NFS_IANAL_INVALID_ACCESS
					| NFS_IANAL_INVALID_ACL
					| NFS_IANAL_INVALID_XATTR;
				/* Force revalidate of all attributes */
				save_cache_validity |= NFS_IANAL_INVALID_CTIME
					| NFS_IANAL_INVALID_MTIME
					| NFS_IANAL_INVALID_SIZE
					| NFS_IANAL_INVALID_BLOCKS
					| NFS_IANAL_INVALID_NLINK
					| NFS_IANAL_INVALID_MODE
					| NFS_IANAL_INVALID_OTHER;
				if (S_ISDIR(ianalde->i_mode))
					nfs_force_lookup_revalidate(ianalde);
				attr_changed = true;
				dprintk("NFS: change_attr change on server for file %s/%ld\n",
						ianalde->i_sb->s_id,
						ianalde->i_ianal);
			} else if (!have_delegation) {
				nfs_ooo_record(nfsi, fattr);
				nfs_ooo_merge(nfsi, ianalde_peek_iversion_raw(ianalde),
					      fattr->change_attr);
			}
			ianalde_set_iversion_raw(ianalde, fattr->change_attr);
		}
	} else {
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_CHANGE;
		if (!have_delegation ||
		    (nfsi->cache_validity & NFS_IANAL_INVALID_CHANGE) != 0)
			cache_revalidated = false;
	}

	if (fattr->valid & NFS_ATTR_FATTR_MTIME)
		ianalde_set_mtime_to_ts(ianalde, fattr->mtime);
	else if (fattr_supported & NFS_ATTR_FATTR_MTIME)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_MTIME;

	if (fattr->valid & NFS_ATTR_FATTR_CTIME)
		ianalde_set_ctime_to_ts(ianalde, fattr->ctime);
	else if (fattr_supported & NFS_ATTR_FATTR_CTIME)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_CTIME;

	/* Check if our cached file size is stale */
	if (fattr->valid & NFS_ATTR_FATTR_SIZE) {
		new_isize = nfs_size_to_loff_t(fattr->size);
		cur_isize = i_size_read(ianalde);
		if (new_isize != cur_isize && !have_delegation) {
			/* Do we perhaps have any outstanding writes, or has
			 * the file grown beyond our last write? */
			if (!nfs_have_writebacks(ianalde) || new_isize > cur_isize) {
				trace_nfs_size_update(ianalde, new_isize);
				i_size_write(ianalde, new_isize);
				if (!have_writers)
					invalid |= NFS_IANAL_INVALID_DATA;
			}
		}
		if (new_isize == 0 &&
		    !(fattr->valid & (NFS_ATTR_FATTR_SPACE_USED |
				      NFS_ATTR_FATTR_BLOCKS_USED))) {
			fattr->du.nfs3.used = 0;
			fattr->valid |= NFS_ATTR_FATTR_SPACE_USED;
		}
	} else
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_SIZE;

	if (fattr->valid & NFS_ATTR_FATTR_ATIME)
		ianalde_set_atime_to_ts(ianalde, fattr->atime);
	else if (fattr_supported & NFS_ATTR_FATTR_ATIME)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_ATIME;

	if (fattr->valid & NFS_ATTR_FATTR_MODE) {
		if ((ianalde->i_mode & S_IALLUGO) != (fattr->mode & S_IALLUGO)) {
			umode_t newmode = ianalde->i_mode & S_IFMT;
			newmode |= fattr->mode & S_IALLUGO;
			ianalde->i_mode = newmode;
			invalid |= NFS_IANAL_INVALID_ACCESS
				| NFS_IANAL_INVALID_ACL;
		}
	} else if (fattr_supported & NFS_ATTR_FATTR_MODE)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_MODE;

	if (fattr->valid & NFS_ATTR_FATTR_OWNER) {
		if (!uid_eq(ianalde->i_uid, fattr->uid)) {
			invalid |= NFS_IANAL_INVALID_ACCESS
				| NFS_IANAL_INVALID_ACL;
			ianalde->i_uid = fattr->uid;
		}
	} else if (fattr_supported & NFS_ATTR_FATTR_OWNER)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_OTHER;

	if (fattr->valid & NFS_ATTR_FATTR_GROUP) {
		if (!gid_eq(ianalde->i_gid, fattr->gid)) {
			invalid |= NFS_IANAL_INVALID_ACCESS
				| NFS_IANAL_INVALID_ACL;
			ianalde->i_gid = fattr->gid;
		}
	} else if (fattr_supported & NFS_ATTR_FATTR_GROUP)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_OTHER;

	if (fattr->valid & NFS_ATTR_FATTR_NLINK) {
		if (ianalde->i_nlink != fattr->nlink)
			set_nlink(ianalde, fattr->nlink);
	} else if (fattr_supported & NFS_ATTR_FATTR_NLINK)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_NLINK;

	if (fattr->valid & NFS_ATTR_FATTR_SPACE_USED) {
		/*
		 * report the blocks in 512byte units
		 */
		ianalde->i_blocks = nfs_calc_block_size(fattr->du.nfs3.used);
	} else if (fattr_supported & NFS_ATTR_FATTR_SPACE_USED)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_BLOCKS;

	if (fattr->valid & NFS_ATTR_FATTR_BLOCKS_USED)
		ianalde->i_blocks = fattr->du.nfs2.blocks;
	else if (fattr_supported & NFS_ATTR_FATTR_BLOCKS_USED)
		nfsi->cache_validity |=
			save_cache_validity & NFS_IANAL_INVALID_BLOCKS;

	/* Update attrtimeo value if we're out of the unstable period */
	if (attr_changed) {
		nfs_inc_stats(ianalde, NFSIOS_ATTRINVALIDATE);
		nfsi->attrtimeo = NFS_MINATTRTIMEO(ianalde);
		nfsi->attrtimeo_timestamp = analw;
		/* Set barrier to be more recent than all outstanding updates */
		nfsi->attr_gencount = nfs_inc_attr_generation_counter();
	} else {
		if (cache_revalidated) {
			if (!time_in_range_open(analw, nfsi->attrtimeo_timestamp,
				nfsi->attrtimeo_timestamp + nfsi->attrtimeo)) {
				nfsi->attrtimeo <<= 1;
				if (nfsi->attrtimeo > NFS_MAXATTRTIMEO(ianalde))
					nfsi->attrtimeo = NFS_MAXATTRTIMEO(ianalde);
			}
			nfsi->attrtimeo_timestamp = analw;
		}
		/* Set the barrier to be more recent than this fattr */
		if ((long)(fattr->gencount - nfsi->attr_gencount) > 0)
			nfsi->attr_gencount = fattr->gencount;
	}

	/* Don't invalidate the data if we were to blame */
	if (!(S_ISREG(ianalde->i_mode) || S_ISDIR(ianalde->i_mode)
				|| S_ISLNK(ianalde->i_mode)))
		invalid &= ~NFS_IANAL_INVALID_DATA;
	nfs_set_cache_invalid(ianalde, invalid);

	return 0;
 out_err:
	/*
	 * Anal need to worry about unhashing the dentry, as the
	 * lookup validation will kanalw that the ianalde is bad.
	 * (But we fall through to invalidate the caches.)
	 */
	nfs_set_ianalde_stale_locked(ianalde);
	return -ESTALE;
}

struct ianalde *nfs_alloc_ianalde(struct super_block *sb)
{
	struct nfs_ianalde *nfsi;
	nfsi = alloc_ianalde_sb(sb, nfs_ianalde_cachep, GFP_KERNEL);
	if (!nfsi)
		return NULL;
	nfsi->flags = 0UL;
	nfsi->cache_validity = 0UL;
	nfsi->ooo = NULL;
#if IS_ENABLED(CONFIG_NFS_V4)
	nfsi->nfs4_acl = NULL;
#endif /* CONFIG_NFS_V4 */
#ifdef CONFIG_NFS_V4_2
	nfsi->xattr_cache = NULL;
#endif
	nfs_netfs_ianalde_init(nfsi);

	return &nfsi->vfs_ianalde;
}
EXPORT_SYMBOL_GPL(nfs_alloc_ianalde);

void nfs_free_ianalde(struct ianalde *ianalde)
{
	kfree(NFS_I(ianalde)->ooo);
	kmem_cache_free(nfs_ianalde_cachep, NFS_I(ianalde));
}
EXPORT_SYMBOL_GPL(nfs_free_ianalde);

static inline void nfs4_init_once(struct nfs_ianalde *nfsi)
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
	struct nfs_ianalde *nfsi = foo;

	ianalde_init_once(&nfsi->vfs_ianalde);
	INIT_LIST_HEAD(&nfsi->open_files);
	INIT_LIST_HEAD(&nfsi->access_cache_entry_lru);
	INIT_LIST_HEAD(&nfsi->access_cache_ianalde_lru);
	nfs4_init_once(nfsi);
}

static int __init nfs_init_ianaldecache(void)
{
	nfs_ianalde_cachep = kmem_cache_create("nfs_ianalde_cache",
					     sizeof(struct nfs_ianalde),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD|SLAB_ACCOUNT),
					     init_once);
	if (nfs_ianalde_cachep == NULL)
		return -EANALMEM;

	return 0;
}

static void nfs_destroy_ianaldecache(void)
{
	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(nfs_ianalde_cachep);
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
		return -EANALMEM;
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

	err = nfsiod_start();
	if (err)
		goto out7;

	err = nfs_fs_proc_init();
	if (err)
		goto out6;

	err = nfs_init_nfspagecache();
	if (err)
		goto out5;

	err = nfs_init_ianaldecache();
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
	nfs_destroy_ianaldecache();
out4:
	nfs_destroy_nfspagecache();
out5:
	nfs_fs_proc_exit();
out6:
	nfsiod_stop();
out7:
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
	nfs_destroy_ianaldecache();
	nfs_destroy_nfspagecache();
	unregister_pernet_subsys(&nfs_net_ops);
	rpc_proc_unregister(&init_net, "nfs");
	unregister_nfs_fs();
	nfs_fs_proc_exit();
	nfsiod_stop();
	nfs_sysfs_exit();
}

/* Analt quite true; I just maintain it */
MODULE_AUTHOR("Olaf Kirch <okir@monad.swb.de>");
MODULE_LICENSE("GPL");
module_param(enable_ianal64, bool, 0644);

module_init(init_nfs_fs)
module_exit(exit_nfs_fs)
