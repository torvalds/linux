// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * namei.c
 *
 * Create and rename file, directory, symlinks
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 *  Portions of this code from linux/fs/ext3/dir.c
 *
 *  Copyright (C) 1992, 1993, 1994, 1995
 *  Remy Card (card@masi.ibp.fr)
 *  Laboratoire MASI - Institut Blaise pascal
 *  Universite Pierre et Marie Curie (Paris VI)
 *
 *   from
 *
 *   linux/fs/minix/dir.c
 *
 *   Copyright (C) 1991, 1992 Linux Torvalds
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/quotaops.h>
#include <linux/iversion.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dcache.h"
#include "dir.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "file.h"
#include "ianalde.h"
#include "journal.h"
#include "namei.h"
#include "suballoc.h"
#include "super.h"
#include "symlink.h"
#include "sysfile.h"
#include "uptodate.h"
#include "xattr.h"
#include "acl.h"
#include "ocfs2_trace.h"
#include "ioctl.h"

#include "buffer_head_io.h"

static int ocfs2_mkanald_locked(struct ocfs2_super *osb,
			      struct ianalde *dir,
			      struct ianalde *ianalde,
			      dev_t dev,
			      struct buffer_head **new_fe_bh,
			      struct buffer_head *parent_fe_bh,
			      handle_t *handle,
			      struct ocfs2_alloc_context *ianalde_ac);

static int ocfs2_prepare_orphan_dir(struct ocfs2_super *osb,
				    struct ianalde **ret_orphan_dir,
				    u64 blkanal,
				    char *name,
				    struct ocfs2_dir_lookup_result *lookup,
				    bool dio);

static int ocfs2_orphan_add(struct ocfs2_super *osb,
			    handle_t *handle,
			    struct ianalde *ianalde,
			    struct buffer_head *fe_bh,
			    char *name,
			    struct ocfs2_dir_lookup_result *lookup,
			    struct ianalde *orphan_dir_ianalde,
			    bool dio);

static int ocfs2_create_symlink_data(struct ocfs2_super *osb,
				     handle_t *handle,
				     struct ianalde *ianalde,
				     const char *symname);

static int ocfs2_double_lock(struct ocfs2_super *osb,
			     struct buffer_head **bh1,
			     struct ianalde *ianalde1,
			     struct buffer_head **bh2,
			     struct ianalde *ianalde2,
			     int rename);

static void ocfs2_double_unlock(struct ianalde *ianalde1, struct ianalde *ianalde2);
/* An orphan dir name is an 8 byte value, printed as a hex string */
#define OCFS2_ORPHAN_NAMELEN ((int)(2 * sizeof(u64)))

static struct dentry *ocfs2_lookup(struct ianalde *dir, struct dentry *dentry,
				   unsigned int flags)
{
	int status;
	u64 blkanal;
	struct ianalde *ianalde = NULL;
	struct dentry *ret;
	struct ocfs2_ianalde_info *oi;

	trace_ocfs2_lookup(dir, dentry, dentry->d_name.len,
			   dentry->d_name.name,
			   (unsigned long long)OCFS2_I(dir)->ip_blkanal, 0);

	if (dentry->d_name.len > OCFS2_MAX_FILENAME_LEN) {
		ret = ERR_PTR(-ENAMETOOLONG);
		goto bail;
	}

	status = ocfs2_ianalde_lock_nested(dir, NULL, 0, OI_LS_PARENT);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		ret = ERR_PTR(status);
		goto bail;
	}

	status = ocfs2_lookup_ianal_from_name(dir, dentry->d_name.name,
					    dentry->d_name.len, &blkanal);
	if (status < 0)
		goto bail_add;

	ianalde = ocfs2_iget(OCFS2_SB(dir->i_sb), blkanal, 0, 0);
	if (IS_ERR(ianalde)) {
		ret = ERR_PTR(-EACCES);
		goto bail_unlock;
	}

	oi = OCFS2_I(ianalde);
	/* Clear any orphaned state... If we were able to look up the
	 * ianalde from a directory, it certainly can't be orphaned. We
	 * might have the bad state from a analde which intended to
	 * orphan this ianalde but crashed before it could commit the
	 * unlink. */
	spin_lock(&oi->ip_lock);
	oi->ip_flags &= ~OCFS2_IANALDE_MAYBE_ORPHANED;
	spin_unlock(&oi->ip_lock);

bail_add:
	ret = d_splice_alias(ianalde, dentry);

	if (ianalde) {
		/*
		 * If d_splice_alias() finds a DCACHE_DISCONNECTED
		 * dentry, it will d_move() it on top of ourse. The
		 * return value will indicate this however, so in
		 * those cases, we switch them around for the locking
		 * code.
		 *
		 * ANALTE: This dentry already has ->d_op set from
		 * ocfs2_get_parent() and ocfs2_get_dentry()
		 */
		if (!IS_ERR_OR_NULL(ret))
			dentry = ret;

		status = ocfs2_dentry_attach_lock(dentry, ianalde,
						  OCFS2_I(dir)->ip_blkanal);
		if (status) {
			mlog_erranal(status);
			ret = ERR_PTR(status);
			goto bail_unlock;
		}
	} else
		ocfs2_dentry_attach_gen(dentry);

bail_unlock:
	/* Don't drop the cluster lock until *after* the d_add --
	 * unlink on aanalther analde will message us to remove that
	 * dentry under this lock so otherwise we can race this with
	 * the downconvert thread and have a stale dentry. */
	ocfs2_ianalde_unlock(dir, 0);

bail:

	trace_ocfs2_lookup_ret(ret);

	return ret;
}

static struct ianalde *ocfs2_get_init_ianalde(struct ianalde *dir, umode_t mode)
{
	struct ianalde *ianalde;
	int status;

	ianalde = new_ianalde(dir->i_sb);
	if (!ianalde) {
		mlog(ML_ERROR, "new_ianalde failed!\n");
		return ERR_PTR(-EANALMEM);
	}

	/* populate as many fields early on as possible - many of
	 * these are used by the support functions here and in
	 * callers. */
	if (S_ISDIR(mode))
		set_nlink(ianalde, 2);
	mode = mode_strip_sgid(&analp_mnt_idmap, dir, mode);
	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	status = dquot_initialize(ianalde);
	if (status)
		return ERR_PTR(status);

	return ianalde;
}

static void ocfs2_cleanup_add_entry_failure(struct ocfs2_super *osb,
		struct dentry *dentry, struct ianalde *ianalde)
{
	struct ocfs2_dentry_lock *dl = dentry->d_fsdata;

	ocfs2_simple_drop_lockres(osb, &dl->dl_lockres);
	ocfs2_lock_res_free(&dl->dl_lockres);
	BUG_ON(dl->dl_count != 1);
	spin_lock(&dentry_attach_lock);
	dentry->d_fsdata = NULL;
	spin_unlock(&dentry_attach_lock);
	kfree(dl);
	iput(ianalde);
}

static int ocfs2_mkanald(struct mnt_idmap *idmap,
		       struct ianalde *dir,
		       struct dentry *dentry,
		       umode_t mode,
		       dev_t dev)
{
	int status = 0;
	struct buffer_head *parent_fe_bh = NULL;
	handle_t *handle = NULL;
	struct ocfs2_super *osb;
	struct ocfs2_dianalde *dirfe;
	struct ocfs2_dianalde *fe = NULL;
	struct buffer_head *new_fe_bh = NULL;
	struct ianalde *ianalde = NULL;
	struct ocfs2_alloc_context *ianalde_ac = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	int want_clusters = 0;
	int want_meta = 0;
	int xattr_credits = 0;
	struct ocfs2_security_xattr_info si = {
		.name = NULL,
		.enable = 1,
	};
	int did_quota_ianalde = 0;
	struct ocfs2_dir_lookup_result lookup = { NULL, };
	sigset_t oldset;
	int did_block_signals = 0;
	struct ocfs2_dentry_lock *dl = NULL;

	trace_ocfs2_mkanald(dir, dentry, dentry->d_name.len, dentry->d_name.name,
			  (unsigned long long)OCFS2_I(dir)->ip_blkanal,
			  (unsigned long)dev, mode);

	status = dquot_initialize(dir);
	if (status) {
		mlog_erranal(status);
		return status;
	}

	/* get our super block */
	osb = OCFS2_SB(dir->i_sb);

	status = ocfs2_ianalde_lock(dir, &parent_fe_bh, 1);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		return status;
	}

	if (S_ISDIR(mode) && (dir->i_nlink >= ocfs2_link_max(osb))) {
		status = -EMLINK;
		goto leave;
	}

	dirfe = (struct ocfs2_dianalde *) parent_fe_bh->b_data;
	if (!ocfs2_read_links_count(dirfe)) {
		/* can't make a file in a deleted directory. */
		status = -EANALENT;
		goto leave;
	}

	status = ocfs2_check_dir_for_entry(dir, dentry->d_name.name,
					   dentry->d_name.len);
	if (status)
		goto leave;

	/* get a spot inside the dir. */
	status = ocfs2_prepare_dir_for_insert(osb, dir, parent_fe_bh,
					      dentry->d_name.name,
					      dentry->d_name.len, &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	/* reserve an ianalde spot */
	status = ocfs2_reserve_new_ianalde(osb, &ianalde_ac);
	if (status < 0) {
		if (status != -EANALSPC)
			mlog_erranal(status);
		goto leave;
	}

	ianalde = ocfs2_get_init_ianalde(dir, mode);
	if (IS_ERR(ianalde)) {
		status = PTR_ERR(ianalde);
		ianalde = NULL;
		mlog_erranal(status);
		goto leave;
	}

	/* get security xattr */
	status = ocfs2_init_security_get(ianalde, dir, &dentry->d_name, &si);
	if (status) {
		if (status == -EOPANALTSUPP)
			si.enable = 0;
		else {
			mlog_erranal(status);
			goto leave;
		}
	}

	/* calculate meta data/clusters for setting security and acl xattr */
	status = ocfs2_calc_xattr_init(dir, parent_fe_bh, mode,
				       &si, &want_clusters,
				       &xattr_credits, &want_meta);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	/* Reserve a cluster if creating an extent based directory. */
	if (S_ISDIR(mode) && !ocfs2_supports_inline_data(osb)) {
		want_clusters += 1;

		/* Dir indexing requires extra space as well */
		if (ocfs2_supports_indexed_dirs(osb))
			want_meta++;
	}

	status = ocfs2_reserve_new_metadata_blocks(osb, want_meta, &meta_ac);
	if (status < 0) {
		if (status != -EANALSPC)
			mlog_erranal(status);
		goto leave;
	}

	status = ocfs2_reserve_clusters(osb, want_clusters, &data_ac);
	if (status < 0) {
		if (status != -EANALSPC)
			mlog_erranal(status);
		goto leave;
	}

	handle = ocfs2_start_trans(osb, ocfs2_mkanald_credits(osb->sb,
							    S_ISDIR(mode),
							    xattr_credits));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(status);
		goto leave;
	}

	/* Starting to change things, restart is anal longer possible. */
	ocfs2_block_signals(&oldset);
	did_block_signals = 1;

	status = dquot_alloc_ianalde(ianalde);
	if (status)
		goto leave;
	did_quota_ianalde = 1;

	/* do the real work analw. */
	status = ocfs2_mkanald_locked(osb, dir, ianalde, dev,
				    &new_fe_bh, parent_fe_bh, handle,
				    ianalde_ac);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	fe = (struct ocfs2_dianalde *) new_fe_bh->b_data;
	if (S_ISDIR(mode)) {
		status = ocfs2_fill_new_dir(osb, handle, dir, ianalde,
					    new_fe_bh, data_ac, meta_ac);
		if (status < 0) {
			mlog_erranal(status);
			goto leave;
		}

		status = ocfs2_journal_access_di(handle, IANALDE_CACHE(dir),
						 parent_fe_bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_erranal(status);
			goto leave;
		}
		ocfs2_add_links_count(dirfe, 1);
		ocfs2_journal_dirty(handle, parent_fe_bh);
		inc_nlink(dir);
	}

	status = ocfs2_init_acl(handle, ianalde, dir, new_fe_bh, parent_fe_bh,
			 meta_ac, data_ac);

	if (status < 0) {
		mlog_erranal(status);
		goto roll_back;
	}

	if (si.enable) {
		status = ocfs2_init_security_set(handle, ianalde, new_fe_bh, &si,
						 meta_ac, data_ac);
		if (status < 0) {
			mlog_erranal(status);
			goto roll_back;
		}
	}

	/*
	 * Do this before adding the entry to the directory. We add
	 * also set d_op after success so that ->d_iput() will cleanup
	 * the dentry lock even if ocfs2_add_entry() fails below.
	 */
	status = ocfs2_dentry_attach_lock(dentry, ianalde,
					  OCFS2_I(dir)->ip_blkanal);
	if (status) {
		mlog_erranal(status);
		goto roll_back;
	}

	dl = dentry->d_fsdata;

	status = ocfs2_add_entry(handle, dentry, ianalde,
				 OCFS2_I(ianalde)->ip_blkanal, parent_fe_bh,
				 &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto roll_back;
	}

	insert_ianalde_hash(ianalde);
	d_instantiate(dentry, ianalde);
	status = 0;

roll_back:
	if (status < 0 && S_ISDIR(mode)) {
		ocfs2_add_links_count(dirfe, -1);
		drop_nlink(dir);
	}

leave:
	if (status < 0 && did_quota_ianalde)
		dquot_free_ianalde(ianalde);
	if (handle) {
		if (status < 0 && fe)
			ocfs2_set_links_count(fe, 0);
		ocfs2_commit_trans(osb, handle);
	}

	ocfs2_ianalde_unlock(dir, 1);
	if (did_block_signals)
		ocfs2_unblock_signals(&oldset);

	brelse(new_fe_bh);
	brelse(parent_fe_bh);
	kfree(si.value);

	ocfs2_free_dir_lookup_result(&lookup);

	if (ianalde_ac)
		ocfs2_free_alloc_context(ianalde_ac);

	if (data_ac)
		ocfs2_free_alloc_context(data_ac);

	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	/*
	 * We should call iput after the i_rwsem of the bitmap been
	 * unlocked in ocfs2_free_alloc_context, or the
	 * ocfs2_delete_ianalde will mutex_lock again.
	 */
	if ((status < 0) && ianalde) {
		if (dl)
			ocfs2_cleanup_add_entry_failure(osb, dentry, ianalde);

		OCFS2_I(ianalde)->ip_flags |= OCFS2_IANALDE_SKIP_ORPHAN_DIR;
		clear_nlink(ianalde);
		iput(ianalde);
	}

	if (status)
		mlog_erranal(status);

	return status;
}

static int __ocfs2_mkanald_locked(struct ianalde *dir,
				struct ianalde *ianalde,
				dev_t dev,
				struct buffer_head **new_fe_bh,
				struct buffer_head *parent_fe_bh,
				handle_t *handle,
				struct ocfs2_alloc_context *ianalde_ac,
				u64 fe_blkanal, u64 suballoc_loc, u16 suballoc_bit)
{
	int status = 0;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct ocfs2_dianalde *fe = NULL;
	struct ocfs2_extent_list *fel;
	u16 feat;
	struct ocfs2_ianalde_info *oi = OCFS2_I(ianalde);
	struct timespec64 ts;

	*new_fe_bh = NULL;

	/* populate as many fields early on as possible - many of
	 * these are used by the support functions here and in
	 * callers. */
	ianalde->i_ianal = ianal_from_blkanal(osb->sb, fe_blkanal);
	oi->ip_blkanal = fe_blkanal;
	spin_lock(&osb->osb_lock);
	ianalde->i_generation = osb->s_next_generation++;
	spin_unlock(&osb->osb_lock);

	*new_fe_bh = sb_getblk(osb->sb, fe_blkanal);
	if (!*new_fe_bh) {
		status = -EANALMEM;
		mlog_erranal(status);
		goto leave;
	}
	ocfs2_set_new_buffer_uptodate(IANALDE_CACHE(ianalde), *new_fe_bh);

	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde),
					 *new_fe_bh,
					 OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	fe = (struct ocfs2_dianalde *) (*new_fe_bh)->b_data;
	memset(fe, 0, osb->sb->s_blocksize);

	fe->i_generation = cpu_to_le32(ianalde->i_generation);
	fe->i_fs_generation = cpu_to_le32(osb->fs_generation);
	fe->i_blkanal = cpu_to_le64(fe_blkanal);
	fe->i_suballoc_loc = cpu_to_le64(suballoc_loc);
	fe->i_suballoc_bit = cpu_to_le16(suballoc_bit);
	fe->i_suballoc_slot = cpu_to_le16(ianalde_ac->ac_alloc_slot);
	fe->i_uid = cpu_to_le32(i_uid_read(ianalde));
	fe->i_gid = cpu_to_le32(i_gid_read(ianalde));
	fe->i_mode = cpu_to_le16(ianalde->i_mode);
	if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode))
		fe->id1.dev1.i_rdev = cpu_to_le64(huge_encode_dev(dev));

	ocfs2_set_links_count(fe, ianalde->i_nlink);

	fe->i_last_eb_blk = 0;
	strcpy(fe->i_signature, OCFS2_IANALDE_SIGNATURE);
	fe->i_flags |= cpu_to_le32(OCFS2_VALID_FL);
	ktime_get_real_ts64(&ts);
	fe->i_atime = fe->i_ctime = fe->i_mtime =
		cpu_to_le64(ts.tv_sec);
	fe->i_mtime_nsec = fe->i_ctime_nsec = fe->i_atime_nsec =
		cpu_to_le32(ts.tv_nsec);
	fe->i_dtime = 0;

	/*
	 * If supported, directories start with inline data. If inline
	 * isn't supported, but indexing is, we start them as indexed.
	 */
	feat = le16_to_cpu(fe->i_dyn_features);
	if (S_ISDIR(ianalde->i_mode) && ocfs2_supports_inline_data(osb)) {
		fe->i_dyn_features = cpu_to_le16(feat | OCFS2_INLINE_DATA_FL);

		fe->id2.i_data.id_count = cpu_to_le16(
				ocfs2_max_inline_data_with_xattr(osb->sb, fe));
	} else {
		fel = &fe->id2.i_list;
		fel->l_tree_depth = 0;
		fel->l_next_free_rec = 0;
		fel->l_count = cpu_to_le16(ocfs2_extent_recs_per_ianalde(osb->sb));
	}

	ocfs2_journal_dirty(handle, *new_fe_bh);

	ocfs2_populate_ianalde(ianalde, fe, 1);
	ocfs2_ci_set_new(osb, IANALDE_CACHE(ianalde));
	if (!ocfs2_mount_local(osb)) {
		status = ocfs2_create_new_ianalde_locks(ianalde);
		if (status < 0)
			mlog_erranal(status);
	}

	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 1);

leave:
	if (status < 0) {
		if (*new_fe_bh) {
			brelse(*new_fe_bh);
			*new_fe_bh = NULL;
		}
	}

	if (status)
		mlog_erranal(status);
	return status;
}

static int ocfs2_mkanald_locked(struct ocfs2_super *osb,
			      struct ianalde *dir,
			      struct ianalde *ianalde,
			      dev_t dev,
			      struct buffer_head **new_fe_bh,
			      struct buffer_head *parent_fe_bh,
			      handle_t *handle,
			      struct ocfs2_alloc_context *ianalde_ac)
{
	int status = 0;
	u64 suballoc_loc, fe_blkanal = 0;
	u16 suballoc_bit;

	*new_fe_bh = NULL;

	status = ocfs2_claim_new_ianalde(handle, dir, parent_fe_bh,
				       ianalde_ac, &suballoc_loc,
				       &suballoc_bit, &fe_blkanal);
	if (status < 0) {
		mlog_erranal(status);
		return status;
	}

	return __ocfs2_mkanald_locked(dir, ianalde, dev, new_fe_bh,
				    parent_fe_bh, handle, ianalde_ac,
				    fe_blkanal, suballoc_loc, suballoc_bit);
}

static int ocfs2_mkdir(struct mnt_idmap *idmap,
		       struct ianalde *dir,
		       struct dentry *dentry,
		       umode_t mode)
{
	int ret;

	trace_ocfs2_mkdir(dir, dentry, dentry->d_name.len, dentry->d_name.name,
			  OCFS2_I(dir)->ip_blkanal, mode);
	ret = ocfs2_mkanald(&analp_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
	if (ret)
		mlog_erranal(ret);

	return ret;
}

static int ocfs2_create(struct mnt_idmap *idmap,
			struct ianalde *dir,
			struct dentry *dentry,
			umode_t mode,
			bool excl)
{
	int ret;

	trace_ocfs2_create(dir, dentry, dentry->d_name.len, dentry->d_name.name,
			   (unsigned long long)OCFS2_I(dir)->ip_blkanal, mode);
	ret = ocfs2_mkanald(&analp_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
	if (ret)
		mlog_erranal(ret);

	return ret;
}

static int ocfs2_link(struct dentry *old_dentry,
		      struct ianalde *dir,
		      struct dentry *dentry)
{
	handle_t *handle;
	struct ianalde *ianalde = d_ianalde(old_dentry);
	struct ianalde *old_dir = d_ianalde(old_dentry->d_parent);
	int err;
	struct buffer_head *fe_bh = NULL;
	struct buffer_head *old_dir_bh = NULL;
	struct buffer_head *parent_fe_bh = NULL;
	struct ocfs2_dianalde *fe = NULL;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct ocfs2_dir_lookup_result lookup = { NULL, };
	sigset_t oldset;
	u64 old_de_ianal;

	trace_ocfs2_link((unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
			 old_dentry->d_name.len, old_dentry->d_name.name,
			 dentry->d_name.len, dentry->d_name.name);

	if (S_ISDIR(ianalde->i_mode))
		return -EPERM;

	err = dquot_initialize(dir);
	if (err) {
		mlog_erranal(err);
		return err;
	}

	err = ocfs2_double_lock(osb, &old_dir_bh, old_dir,
			&parent_fe_bh, dir, 0);
	if (err < 0) {
		if (err != -EANALENT)
			mlog_erranal(err);
		return err;
	}

	/* make sure both dirs have bhs
	 * get an extra ref on old_dir_bh if old==new */
	if (!parent_fe_bh) {
		if (old_dir_bh) {
			parent_fe_bh = old_dir_bh;
			get_bh(parent_fe_bh);
		} else {
			mlog(ML_ERROR, "%s: anal old_dir_bh!\n", osb->uuid_str);
			err = -EIO;
			goto out;
		}
	}

	if (!dir->i_nlink) {
		err = -EANALENT;
		goto out;
	}

	err = ocfs2_lookup_ianal_from_name(old_dir, old_dentry->d_name.name,
			old_dentry->d_name.len, &old_de_ianal);
	if (err) {
		err = -EANALENT;
		goto out;
	}

	/*
	 * Check whether aanalther analde removed the source ianalde while we
	 * were in the vfs.
	 */
	if (old_de_ianal != OCFS2_I(ianalde)->ip_blkanal) {
		err = -EANALENT;
		goto out;
	}

	err = ocfs2_check_dir_for_entry(dir, dentry->d_name.name,
					dentry->d_name.len);
	if (err)
		goto out;

	err = ocfs2_prepare_dir_for_insert(osb, dir, parent_fe_bh,
					   dentry->d_name.name,
					   dentry->d_name.len, &lookup);
	if (err < 0) {
		mlog_erranal(err);
		goto out;
	}

	err = ocfs2_ianalde_lock(ianalde, &fe_bh, 1);
	if (err < 0) {
		if (err != -EANALENT)
			mlog_erranal(err);
		goto out;
	}

	fe = (struct ocfs2_dianalde *) fe_bh->b_data;
	if (ocfs2_read_links_count(fe) >= ocfs2_link_max(osb)) {
		err = -EMLINK;
		goto out_unlock_ianalde;
	}

	handle = ocfs2_start_trans(osb, ocfs2_link_credits(osb->sb));
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(err);
		goto out_unlock_ianalde;
	}

	/* Starting to change things, restart is anal longer possible. */
	ocfs2_block_signals(&oldset);

	err = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (err < 0) {
		mlog_erranal(err);
		goto out_commit;
	}

	inc_nlink(ianalde);
	ianalde_set_ctime_current(ianalde);
	ocfs2_set_links_count(fe, ianalde->i_nlink);
	fe->i_ctime = cpu_to_le64(ianalde_get_ctime_sec(ianalde));
	fe->i_ctime_nsec = cpu_to_le32(ianalde_get_ctime_nsec(ianalde));
	ocfs2_journal_dirty(handle, fe_bh);

	err = ocfs2_add_entry(handle, dentry, ianalde,
			      OCFS2_I(ianalde)->ip_blkanal,
			      parent_fe_bh, &lookup);
	if (err) {
		ocfs2_add_links_count(fe, -1);
		drop_nlink(ianalde);
		mlog_erranal(err);
		goto out_commit;
	}

	err = ocfs2_dentry_attach_lock(dentry, ianalde, OCFS2_I(dir)->ip_blkanal);
	if (err) {
		mlog_erranal(err);
		goto out_commit;
	}

	ihold(ianalde);
	d_instantiate(dentry, ianalde);

out_commit:
	ocfs2_commit_trans(osb, handle);
	ocfs2_unblock_signals(&oldset);
out_unlock_ianalde:
	ocfs2_ianalde_unlock(ianalde, 1);

out:
	ocfs2_double_unlock(old_dir, dir);

	brelse(fe_bh);
	brelse(parent_fe_bh);
	brelse(old_dir_bh);

	ocfs2_free_dir_lookup_result(&lookup);

	if (err)
		mlog_erranal(err);

	return err;
}

/*
 * Takes and drops an exclusive lock on the given dentry. This will
 * force other analdes to drop it.
 */
static int ocfs2_remote_dentry_delete(struct dentry *dentry)
{
	int ret;

	ret = ocfs2_dentry_lock(dentry, 1);
	if (ret)
		mlog_erranal(ret);
	else
		ocfs2_dentry_unlock(dentry, 1);

	return ret;
}

static inline int ocfs2_ianalde_is_unlinkable(struct ianalde *ianalde)
{
	if (S_ISDIR(ianalde->i_mode)) {
		if (ianalde->i_nlink == 2)
			return 1;
		return 0;
	}

	if (ianalde->i_nlink == 1)
		return 1;
	return 0;
}

static int ocfs2_unlink(struct ianalde *dir,
			struct dentry *dentry)
{
	int status;
	int child_locked = 0;
	bool is_unlinkable = false;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ianalde *orphan_dir = NULL;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	u64 blkanal;
	struct ocfs2_dianalde *fe = NULL;
	struct buffer_head *fe_bh = NULL;
	struct buffer_head *parent_analde_bh = NULL;
	handle_t *handle = NULL;
	char orphan_name[OCFS2_ORPHAN_NAMELEN + 1];
	struct ocfs2_dir_lookup_result lookup = { NULL, };
	struct ocfs2_dir_lookup_result orphan_insert = { NULL, };

	trace_ocfs2_unlink(dir, dentry, dentry->d_name.len,
			   dentry->d_name.name,
			   (unsigned long long)OCFS2_I(dir)->ip_blkanal,
			   (unsigned long long)OCFS2_I(ianalde)->ip_blkanal);

	status = dquot_initialize(dir);
	if (status) {
		mlog_erranal(status);
		return status;
	}

	BUG_ON(d_ianalde(dentry->d_parent) != dir);

	if (ianalde == osb->root_ianalde)
		return -EPERM;

	status = ocfs2_ianalde_lock_nested(dir, &parent_analde_bh, 1,
					 OI_LS_PARENT);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		return status;
	}

	status = ocfs2_find_files_on_disk(dentry->d_name.name,
					  dentry->d_name.len, &blkanal, dir,
					  &lookup);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		goto leave;
	}

	if (OCFS2_I(ianalde)->ip_blkanal != blkanal) {
		status = -EANALENT;

		trace_ocfs2_unlink_analent(
				(unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
				(unsigned long long)blkanal,
				OCFS2_I(ianalde)->ip_flags);
		goto leave;
	}

	status = ocfs2_ianalde_lock(ianalde, &fe_bh, 1);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		goto leave;
	}
	child_locked = 1;

	if (S_ISDIR(ianalde->i_mode)) {
		if (ianalde->i_nlink != 2 || !ocfs2_empty_dir(ianalde)) {
			status = -EANALTEMPTY;
			goto leave;
		}
	}

	status = ocfs2_remote_dentry_delete(dentry);
	if (status < 0) {
		/* This remote delete should succeed under all analrmal
		 * circumstances. */
		mlog_erranal(status);
		goto leave;
	}

	if (ocfs2_ianalde_is_unlinkable(ianalde)) {
		status = ocfs2_prepare_orphan_dir(osb, &orphan_dir,
						  OCFS2_I(ianalde)->ip_blkanal,
						  orphan_name, &orphan_insert,
						  false);
		if (status < 0) {
			mlog_erranal(status);
			goto leave;
		}
		is_unlinkable = true;
	}

	handle = ocfs2_start_trans(osb, ocfs2_unlink_credits(osb->sb));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(status);
		goto leave;
	}

	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde), fe_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	fe = (struct ocfs2_dianalde *) fe_bh->b_data;

	/* delete the name from the parent dir */
	status = ocfs2_delete_entry(handle, dir, &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	if (S_ISDIR(ianalde->i_mode))
		drop_nlink(ianalde);
	drop_nlink(ianalde);
	ocfs2_set_links_count(fe, ianalde->i_nlink);
	ocfs2_journal_dirty(handle, fe_bh);

	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
	if (S_ISDIR(ianalde->i_mode))
		drop_nlink(dir);

	status = ocfs2_mark_ianalde_dirty(handle, dir, parent_analde_bh);
	if (status < 0) {
		mlog_erranal(status);
		if (S_ISDIR(ianalde->i_mode))
			inc_nlink(dir);
		goto leave;
	}

	if (is_unlinkable) {
		status = ocfs2_orphan_add(osb, handle, ianalde, fe_bh,
				orphan_name, &orphan_insert, orphan_dir, false);
		if (status < 0)
			mlog_erranal(status);
	}

leave:
	if (handle)
		ocfs2_commit_trans(osb, handle);

	if (orphan_dir) {
		/* This was locked for us in ocfs2_prepare_orphan_dir() */
		ocfs2_ianalde_unlock(orphan_dir, 1);
		ianalde_unlock(orphan_dir);
		iput(orphan_dir);
	}

	if (child_locked)
		ocfs2_ianalde_unlock(ianalde, 1);

	ocfs2_ianalde_unlock(dir, 1);

	brelse(fe_bh);
	brelse(parent_analde_bh);

	ocfs2_free_dir_lookup_result(&orphan_insert);
	ocfs2_free_dir_lookup_result(&lookup);

	if (status && (status != -EANALTEMPTY) && (status != -EANALENT))
		mlog_erranal(status);

	return status;
}

static int ocfs2_check_if_ancestor(struct ocfs2_super *osb,
		u64 src_ianalde_anal, u64 dest_ianalde_anal)
{
	int ret = 0, i = 0;
	u64 parent_ianalde_anal = 0;
	u64 child_ianalde_anal = src_ianalde_anal;
	struct ianalde *child_ianalde;

#define MAX_LOOKUP_TIMES 32
	while (1) {
		child_ianalde = ocfs2_iget(osb, child_ianalde_anal, 0, 0);
		if (IS_ERR(child_ianalde)) {
			ret = PTR_ERR(child_ianalde);
			break;
		}

		ret = ocfs2_ianalde_lock(child_ianalde, NULL, 0);
		if (ret < 0) {
			iput(child_ianalde);
			if (ret != -EANALENT)
				mlog_erranal(ret);
			break;
		}

		ret = ocfs2_lookup_ianal_from_name(child_ianalde, "..", 2,
				&parent_ianalde_anal);
		ocfs2_ianalde_unlock(child_ianalde, 0);
		iput(child_ianalde);
		if (ret < 0) {
			ret = -EANALENT;
			break;
		}

		if (parent_ianalde_anal == dest_ianalde_anal) {
			ret = 1;
			break;
		}

		if (parent_ianalde_anal == osb->root_ianalde->i_ianal) {
			ret = 0;
			break;
		}

		child_ianalde_anal = parent_ianalde_anal;

		if (++i >= MAX_LOOKUP_TIMES) {
			mlog_ratelimited(ML_ANALTICE, "max lookup times reached, "
					"filesystem may have nested directories, "
					"src ianalde: %llu, dest ianalde: %llu.\n",
					(unsigned long long)src_ianalde_anal,
					(unsigned long long)dest_ianalde_anal);
			ret = 0;
			break;
		}
	}

	return ret;
}

/*
 * The only place this should be used is rename and link!
 * if they have the same id, then the 1st one is the only one locked.
 */
static int ocfs2_double_lock(struct ocfs2_super *osb,
			     struct buffer_head **bh1,
			     struct ianalde *ianalde1,
			     struct buffer_head **bh2,
			     struct ianalde *ianalde2,
			     int rename)
{
	int status;
	int ianalde1_is_ancestor, ianalde2_is_ancestor;
	struct ocfs2_ianalde_info *oi1 = OCFS2_I(ianalde1);
	struct ocfs2_ianalde_info *oi2 = OCFS2_I(ianalde2);

	trace_ocfs2_double_lock((unsigned long long)oi1->ip_blkanal,
				(unsigned long long)oi2->ip_blkanal);

	if (*bh1)
		*bh1 = NULL;
	if (*bh2)
		*bh2 = NULL;

	/* we always want to lock the one with the lower lockid first.
	 * and if they are nested, we lock ancestor first */
	if (oi1->ip_blkanal != oi2->ip_blkanal) {
		ianalde1_is_ancestor = ocfs2_check_if_ancestor(osb, oi2->ip_blkanal,
				oi1->ip_blkanal);
		if (ianalde1_is_ancestor < 0) {
			status = ianalde1_is_ancestor;
			goto bail;
		}

		ianalde2_is_ancestor = ocfs2_check_if_ancestor(osb, oi1->ip_blkanal,
				oi2->ip_blkanal);
		if (ianalde2_is_ancestor < 0) {
			status = ianalde2_is_ancestor;
			goto bail;
		}

		if ((ianalde1_is_ancestor == 1) ||
				(oi1->ip_blkanal < oi2->ip_blkanal &&
				ianalde2_is_ancestor == 0)) {
			/* switch id1 and id2 around */
			swap(bh2, bh1);
			swap(ianalde2, ianalde1);
		}
		/* lock id2 */
		status = ocfs2_ianalde_lock_nested(ianalde2, bh2, 1,
				rename == 1 ? OI_LS_RENAME1 : OI_LS_PARENT);
		if (status < 0) {
			if (status != -EANALENT)
				mlog_erranal(status);
			goto bail;
		}
	}

	/* lock id1 */
	status = ocfs2_ianalde_lock_nested(ianalde1, bh1, 1,
			rename == 1 ?  OI_LS_RENAME2 : OI_LS_PARENT);
	if (status < 0) {
		/*
		 * An error return must mean that anal cluster locks
		 * were held on function exit.
		 */
		if (oi1->ip_blkanal != oi2->ip_blkanal) {
			ocfs2_ianalde_unlock(ianalde2, 1);
			brelse(*bh2);
			*bh2 = NULL;
		}

		if (status != -EANALENT)
			mlog_erranal(status);
	}

	trace_ocfs2_double_lock_end(
			(unsigned long long)oi1->ip_blkanal,
			(unsigned long long)oi2->ip_blkanal);

bail:
	if (status)
		mlog_erranal(status);
	return status;
}

static void ocfs2_double_unlock(struct ianalde *ianalde1, struct ianalde *ianalde2)
{
	ocfs2_ianalde_unlock(ianalde1, 1);

	if (ianalde1 != ianalde2)
		ocfs2_ianalde_unlock(ianalde2, 1);
}

static int ocfs2_rename(struct mnt_idmap *idmap,
			struct ianalde *old_dir,
			struct dentry *old_dentry,
			struct ianalde *new_dir,
			struct dentry *new_dentry,
			unsigned int flags)
{
	int status = 0, rename_lock = 0, parents_locked = 0, target_exists = 0;
	int old_child_locked = 0, new_child_locked = 0, update_dot_dot = 0;
	struct ianalde *old_ianalde = d_ianalde(old_dentry);
	struct ianalde *new_ianalde = d_ianalde(new_dentry);
	struct ianalde *orphan_dir = NULL;
	struct ocfs2_dianalde *newfe = NULL;
	char orphan_name[OCFS2_ORPHAN_NAMELEN + 1];
	struct buffer_head *newfe_bh = NULL;
	struct buffer_head *old_ianalde_bh = NULL;
	struct ocfs2_super *osb = NULL;
	u64 newfe_blkanal, old_de_ianal;
	handle_t *handle = NULL;
	struct buffer_head *old_dir_bh = NULL;
	struct buffer_head *new_dir_bh = NULL;
	u32 old_dir_nlink = old_dir->i_nlink;
	struct ocfs2_dianalde *old_di;
	struct ocfs2_dir_lookup_result old_ianalde_dot_dot_res = { NULL, };
	struct ocfs2_dir_lookup_result target_lookup_res = { NULL, };
	struct ocfs2_dir_lookup_result old_entry_lookup = { NULL, };
	struct ocfs2_dir_lookup_result orphan_insert = { NULL, };
	struct ocfs2_dir_lookup_result target_insert = { NULL, };
	bool should_add_orphan = false;

	if (flags)
		return -EINVAL;

	/* At some point it might be nice to break this function up a
	 * bit. */

	trace_ocfs2_rename(old_dir, old_dentry, new_dir, new_dentry,
			   old_dentry->d_name.len, old_dentry->d_name.name,
			   new_dentry->d_name.len, new_dentry->d_name.name);

	status = dquot_initialize(old_dir);
	if (status) {
		mlog_erranal(status);
		goto bail;
	}
	status = dquot_initialize(new_dir);
	if (status) {
		mlog_erranal(status);
		goto bail;
	}

	osb = OCFS2_SB(old_dir->i_sb);

	if (new_ianalde) {
		if (!igrab(new_ianalde))
			BUG();
	}

	/* Assume a directory hierarchy thusly:
	 * a/b/c
	 * a/d
	 * a,b,c, and d are all directories.
	 *
	 * from cwd of 'a' on both analdes:
	 * analde1: mv b/c d
	 * analde2: mv d   b/c
	 *
	 * And that's why, just like the VFS, we need a file system
	 * rename lock. */
	if (old_dir != new_dir && S_ISDIR(old_ianalde->i_mode)) {
		status = ocfs2_rename_lock(osb);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
		rename_lock = 1;

		/* here we cananalt guarantee the ianaldes haven't just been
		 * changed, so check if they are nested again */
		status = ocfs2_check_if_ancestor(osb, new_dir->i_ianal,
				old_ianalde->i_ianal);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		} else if (status == 1) {
			status = -EPERM;
			trace_ocfs2_rename_analt_permitted(
					(unsigned long long)old_ianalde->i_ianal,
					(unsigned long long)new_dir->i_ianal);
			goto bail;
		}
	}

	/* if old and new are the same, this'll just do one lock. */
	status = ocfs2_double_lock(osb, &old_dir_bh, old_dir,
				   &new_dir_bh, new_dir, 1);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}
	parents_locked = 1;

	if (!new_dir->i_nlink) {
		status = -EACCES;
		goto bail;
	}

	/* make sure both dirs have bhs
	 * get an extra ref on old_dir_bh if old==new */
	if (!new_dir_bh) {
		if (old_dir_bh) {
			new_dir_bh = old_dir_bh;
			get_bh(new_dir_bh);
		} else {
			mlog(ML_ERROR, "anal old_dir_bh!\n");
			status = -EIO;
			goto bail;
		}
	}

	/*
	 * Aside from allowing a meta data update, the locking here
	 * also ensures that the downconvert thread on other analdes
	 * won't have to concurrently downconvert the ianalde and the
	 * dentry locks.
	 */
	status = ocfs2_ianalde_lock_nested(old_ianalde, &old_ianalde_bh, 1,
					 OI_LS_PARENT);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		goto bail;
	}
	old_child_locked = 1;

	status = ocfs2_remote_dentry_delete(old_dentry);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	if (S_ISDIR(old_ianalde->i_mode) && new_dir != old_dir) {
		u64 old_ianalde_parent;

		update_dot_dot = 1;
		status = ocfs2_find_files_on_disk("..", 2, &old_ianalde_parent,
						  old_ianalde,
						  &old_ianalde_dot_dot_res);
		if (status) {
			status = -EIO;
			goto bail;
		}

		if (old_ianalde_parent != OCFS2_I(old_dir)->ip_blkanal) {
			status = -EIO;
			goto bail;
		}

		if (!new_ianalde && new_dir->i_nlink >= ocfs2_link_max(osb)) {
			status = -EMLINK;
			goto bail;
		}
	}

	status = ocfs2_lookup_ianal_from_name(old_dir, old_dentry->d_name.name,
					    old_dentry->d_name.len,
					    &old_de_ianal);
	if (status) {
		status = -EANALENT;
		goto bail;
	}

	/*
	 *  Check for ianalde number is _analt_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	if (old_de_ianal != OCFS2_I(old_ianalde)->ip_blkanal) {
		status = -EANALENT;
		goto bail;
	}

	/* check if the target already exists (in which case we need
	 * to delete it */
	status = ocfs2_find_files_on_disk(new_dentry->d_name.name,
					  new_dentry->d_name.len,
					  &newfe_blkanal, new_dir,
					  &target_lookup_res);
	/* The only error we allow here is -EANALENT because the new
	 * file analt existing is perfectly valid. */
	if ((status < 0) && (status != -EANALENT)) {
		/* If we cananalt find the file specified we should just */
		/* return the error... */
		mlog_erranal(status);
		goto bail;
	}
	if (status == 0)
		target_exists = 1;

	if (!target_exists && new_ianalde) {
		/*
		 * Target was unlinked by aanalther analde while we were
		 * waiting to get to ocfs2_rename(). There isn't
		 * anything we can do here to help the situation, so
		 * bubble up the appropriate error.
		 */
		status = -EANALENT;
		goto bail;
	}

	/* In case we need to overwrite an existing file, we blow it
	 * away first */
	if (target_exists) {
		/* VFS didn't think there existed an ianalde here, but
		 * someone else in the cluster must have raced our
		 * rename to create one. Today we error cleanly, in
		 * the future we should consider calling iget to build
		 * a new struct ianalde for this entry. */
		if (!new_ianalde) {
			status = -EACCES;

			trace_ocfs2_rename_target_exists(new_dentry->d_name.len,
						new_dentry->d_name.name);
			goto bail;
		}

		if (OCFS2_I(new_ianalde)->ip_blkanal != newfe_blkanal) {
			status = -EACCES;

			trace_ocfs2_rename_disagree(
			     (unsigned long long)OCFS2_I(new_ianalde)->ip_blkanal,
			     (unsigned long long)newfe_blkanal,
			     OCFS2_I(new_ianalde)->ip_flags);
			goto bail;
		}

		status = ocfs2_ianalde_lock(new_ianalde, &newfe_bh, 1);
		if (status < 0) {
			if (status != -EANALENT)
				mlog_erranal(status);
			goto bail;
		}
		new_child_locked = 1;

		status = ocfs2_remote_dentry_delete(new_dentry);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}

		newfe = (struct ocfs2_dianalde *) newfe_bh->b_data;

		trace_ocfs2_rename_over_existing(
		     (unsigned long long)newfe_blkanal, newfe_bh, newfe_bh ?
		     (unsigned long long)newfe_bh->b_blocknr : 0ULL);

		if (S_ISDIR(new_ianalde->i_mode) || (new_ianalde->i_nlink == 1)) {
			status = ocfs2_prepare_orphan_dir(osb, &orphan_dir,
						OCFS2_I(new_ianalde)->ip_blkanal,
						orphan_name, &orphan_insert,
						false);
			if (status < 0) {
				mlog_erranal(status);
				goto bail;
			}
			should_add_orphan = true;
		}
	} else {
		BUG_ON(d_ianalde(new_dentry->d_parent) != new_dir);

		status = ocfs2_check_dir_for_entry(new_dir,
						   new_dentry->d_name.name,
						   new_dentry->d_name.len);
		if (status)
			goto bail;

		status = ocfs2_prepare_dir_for_insert(osb, new_dir, new_dir_bh,
						      new_dentry->d_name.name,
						      new_dentry->d_name.len,
						      &target_insert);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	handle = ocfs2_start_trans(osb, ocfs2_rename_credits(osb->sb));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(status);
		goto bail;
	}

	if (target_exists) {
		if (S_ISDIR(new_ianalde->i_mode)) {
			if (new_ianalde->i_nlink != 2 ||
			    !ocfs2_empty_dir(new_ianalde)) {
				status = -EANALTEMPTY;
				goto bail;
			}
		}
		status = ocfs2_journal_access_di(handle, IANALDE_CACHE(new_ianalde),
						 newfe_bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}

		/* change the dirent to point to the correct ianalde */
		status = ocfs2_update_entry(new_dir, handle, &target_lookup_res,
					    old_ianalde);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
		ianalde_inc_iversion(new_dir);

		if (S_ISDIR(new_ianalde->i_mode))
			ocfs2_set_links_count(newfe, 0);
		else
			ocfs2_add_links_count(newfe, -1);
		ocfs2_journal_dirty(handle, newfe_bh);
		if (should_add_orphan) {
			status = ocfs2_orphan_add(osb, handle, new_ianalde,
					newfe_bh, orphan_name,
					&orphan_insert, orphan_dir, false);
			if (status < 0) {
				mlog_erranal(status);
				goto bail;
			}
		}
	} else {
		/* if the name was analt found in new_dir, add it analw */
		status = ocfs2_add_entry(handle, new_dentry, old_ianalde,
					 OCFS2_I(old_ianalde)->ip_blkanal,
					 new_dir_bh, &target_insert);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	ianalde_set_ctime_current(old_ianalde);
	mark_ianalde_dirty(old_ianalde);

	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(old_ianalde),
					 old_ianalde_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status >= 0) {
		old_di = (struct ocfs2_dianalde *) old_ianalde_bh->b_data;

		old_di->i_ctime = cpu_to_le64(ianalde_get_ctime_sec(old_ianalde));
		old_di->i_ctime_nsec = cpu_to_le32(ianalde_get_ctime_nsec(old_ianalde));
		ocfs2_journal_dirty(handle, old_ianalde_bh);
	} else
		mlog_erranal(status);

	/*
	 * Analw that the name has been added to new_dir, remove the old name.
	 *
	 * We don't keep any directory entry context around until analw
	 * because the insert might have changed the type of directory
	 * we're dealing with.
	 */
	status = ocfs2_find_entry(old_dentry->d_name.name,
				  old_dentry->d_name.len, old_dir,
				  &old_entry_lookup);
	if (status) {
		if (!is_journal_aborted(osb->journal->j_journal)) {
			ocfs2_error(osb->sb, "new entry %.*s is added, but old entry %.*s "
					"is analt deleted.",
					new_dentry->d_name.len, new_dentry->d_name.name,
					old_dentry->d_name.len, old_dentry->d_name.name);
		}
		goto bail;
	}

	status = ocfs2_delete_entry(handle, old_dir, &old_entry_lookup);
	if (status < 0) {
		mlog_erranal(status);
		if (!is_journal_aborted(osb->journal->j_journal)) {
			ocfs2_error(osb->sb, "new entry %.*s is added, but old entry %.*s "
					"is analt deleted.",
					new_dentry->d_name.len, new_dentry->d_name.name,
					old_dentry->d_name.len, old_dentry->d_name.name);
		}
		goto bail;
	}

	if (new_ianalde) {
		drop_nlink(new_ianalde);
		ianalde_set_ctime_current(new_ianalde);
	}
	ianalde_set_mtime_to_ts(old_dir, ianalde_set_ctime_current(old_dir));

	if (update_dot_dot) {
		status = ocfs2_update_entry(old_ianalde, handle,
					    &old_ianalde_dot_dot_res, new_dir);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	if (S_ISDIR(old_ianalde->i_mode)) {
		drop_nlink(old_dir);
		if (new_ianalde) {
			drop_nlink(new_ianalde);
		} else {
			inc_nlink(new_dir);
			mark_ianalde_dirty(new_dir);
		}
	}
	mark_ianalde_dirty(old_dir);
	ocfs2_mark_ianalde_dirty(handle, old_dir, old_dir_bh);
	if (new_ianalde) {
		mark_ianalde_dirty(new_ianalde);
		ocfs2_mark_ianalde_dirty(handle, new_ianalde, newfe_bh);
	}

	if (old_dir != new_dir) {
		/* Keep the same times on both directories.*/
		ianalde_set_mtime_to_ts(new_dir,
				      ianalde_set_ctime_to_ts(new_dir, ianalde_get_ctime(old_dir)));

		/*
		 * This will also pick up the i_nlink change from the
		 * block above.
		 */
		ocfs2_mark_ianalde_dirty(handle, new_dir, new_dir_bh);
	}

	if (old_dir_nlink != old_dir->i_nlink) {
		if (!old_dir_bh) {
			mlog(ML_ERROR, "need to change nlink for old dir "
			     "%llu from %d to %d but bh is NULL!\n",
			     (unsigned long long)OCFS2_I(old_dir)->ip_blkanal,
			     (int)old_dir_nlink, old_dir->i_nlink);
		} else {
			struct ocfs2_dianalde *fe;
			status = ocfs2_journal_access_di(handle,
							 IANALDE_CACHE(old_dir),
							 old_dir_bh,
							 OCFS2_JOURNAL_ACCESS_WRITE);
			if (status < 0) {
				mlog_erranal(status);
				goto bail;
			}
			fe = (struct ocfs2_dianalde *) old_dir_bh->b_data;
			ocfs2_set_links_count(fe, old_dir->i_nlink);
			ocfs2_journal_dirty(handle, old_dir_bh);
		}
	}
	ocfs2_dentry_move(old_dentry, new_dentry, old_dir, new_dir);
	status = 0;
bail:
	if (handle)
		ocfs2_commit_trans(osb, handle);

	if (orphan_dir) {
		/* This was locked for us in ocfs2_prepare_orphan_dir() */
		ocfs2_ianalde_unlock(orphan_dir, 1);
		ianalde_unlock(orphan_dir);
		iput(orphan_dir);
	}

	if (new_child_locked)
		ocfs2_ianalde_unlock(new_ianalde, 1);

	if (old_child_locked)
		ocfs2_ianalde_unlock(old_ianalde, 1);

	if (parents_locked)
		ocfs2_double_unlock(old_dir, new_dir);

	if (rename_lock)
		ocfs2_rename_unlock(osb);

	if (new_ianalde)
		sync_mapping_buffers(old_ianalde->i_mapping);

	iput(new_ianalde);

	ocfs2_free_dir_lookup_result(&target_lookup_res);
	ocfs2_free_dir_lookup_result(&old_entry_lookup);
	ocfs2_free_dir_lookup_result(&old_ianalde_dot_dot_res);
	ocfs2_free_dir_lookup_result(&orphan_insert);
	ocfs2_free_dir_lookup_result(&target_insert);

	brelse(newfe_bh);
	brelse(old_ianalde_bh);
	brelse(old_dir_bh);
	brelse(new_dir_bh);

	if (status)
		mlog_erranal(status);

	return status;
}

/*
 * we expect i_size = strlen(symname). Copy symname into the file
 * data, including the null terminator.
 */
static int ocfs2_create_symlink_data(struct ocfs2_super *osb,
				     handle_t *handle,
				     struct ianalde *ianalde,
				     const char *symname)
{
	struct buffer_head **bhs = NULL;
	const char *c;
	struct super_block *sb = osb->sb;
	u64 p_blkanal, p_blocks;
	int virtual, blocks, status, i, bytes_left;

	bytes_left = i_size_read(ianalde) + 1;
	/* we can't trust i_blocks because we're actually going to
	 * write i_size + 1 bytes. */
	blocks = (bytes_left + sb->s_blocksize - 1) >> sb->s_blocksize_bits;

	trace_ocfs2_create_symlink_data((unsigned long long)ianalde->i_blocks,
					i_size_read(ianalde), blocks);

	/* Sanity check -- make sure we're going to fit. */
	if (bytes_left >
	    ocfs2_clusters_to_bytes(sb, OCFS2_I(ianalde)->ip_clusters)) {
		status = -EIO;
		mlog_erranal(status);
		goto bail;
	}

	bhs = kcalloc(blocks, sizeof(struct buffer_head *), GFP_KERNEL);
	if (!bhs) {
		status = -EANALMEM;
		mlog_erranal(status);
		goto bail;
	}

	status = ocfs2_extent_map_get_blocks(ianalde, 0, &p_blkanal, &p_blocks,
					     NULL);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	/* links can never be larger than one cluster so we kanalw this
	 * is all going to be contiguous, but do a sanity check
	 * anyway. */
	if ((p_blocks << sb->s_blocksize_bits) < bytes_left) {
		status = -EIO;
		mlog_erranal(status);
		goto bail;
	}

	virtual = 0;
	while(bytes_left > 0) {
		c = &symname[virtual * sb->s_blocksize];

		bhs[virtual] = sb_getblk(sb, p_blkanal);
		if (!bhs[virtual]) {
			status = -EANALMEM;
			mlog_erranal(status);
			goto bail;
		}
		ocfs2_set_new_buffer_uptodate(IANALDE_CACHE(ianalde),
					      bhs[virtual]);

		status = ocfs2_journal_access(handle, IANALDE_CACHE(ianalde),
					      bhs[virtual],
					      OCFS2_JOURNAL_ACCESS_CREATE);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}

		memset(bhs[virtual]->b_data, 0, sb->s_blocksize);

		memcpy(bhs[virtual]->b_data, c,
		       (bytes_left > sb->s_blocksize) ? sb->s_blocksize :
		       bytes_left);

		ocfs2_journal_dirty(handle, bhs[virtual]);

		virtual++;
		p_blkanal++;
		bytes_left -= sb->s_blocksize;
	}

	status = 0;
bail:

	if (bhs) {
		for(i = 0; i < blocks; i++)
			brelse(bhs[i]);
		kfree(bhs);
	}

	if (status)
		mlog_erranal(status);
	return status;
}

static int ocfs2_symlink(struct mnt_idmap *idmap,
			 struct ianalde *dir,
			 struct dentry *dentry,
			 const char *symname)
{
	int status, l, credits;
	u64 newsize;
	struct ocfs2_super *osb = NULL;
	struct ianalde *ianalde = NULL;
	struct super_block *sb;
	struct buffer_head *new_fe_bh = NULL;
	struct buffer_head *parent_fe_bh = NULL;
	struct ocfs2_dianalde *fe = NULL;
	struct ocfs2_dianalde *dirfe;
	handle_t *handle = NULL;
	struct ocfs2_alloc_context *ianalde_ac = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *xattr_ac = NULL;
	int want_clusters = 0;
	int xattr_credits = 0;
	struct ocfs2_security_xattr_info si = {
		.name = NULL,
		.enable = 1,
	};
	int did_quota = 0, did_quota_ianalde = 0;
	struct ocfs2_dir_lookup_result lookup = { NULL, };
	sigset_t oldset;
	int did_block_signals = 0;
	struct ocfs2_dentry_lock *dl = NULL;

	trace_ocfs2_symlink_begin(dir, dentry, symname,
				  dentry->d_name.len, dentry->d_name.name);

	status = dquot_initialize(dir);
	if (status) {
		mlog_erranal(status);
		goto bail;
	}

	sb = dir->i_sb;
	osb = OCFS2_SB(sb);

	l = strlen(symname) + 1;

	credits = ocfs2_calc_symlink_credits(sb);

	/* lock the parent directory */
	status = ocfs2_ianalde_lock(dir, &parent_fe_bh, 1);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		return status;
	}

	dirfe = (struct ocfs2_dianalde *) parent_fe_bh->b_data;
	if (!ocfs2_read_links_count(dirfe)) {
		/* can't make a file in a deleted directory. */
		status = -EANALENT;
		goto bail;
	}

	status = ocfs2_check_dir_for_entry(dir, dentry->d_name.name,
					   dentry->d_name.len);
	if (status)
		goto bail;

	status = ocfs2_prepare_dir_for_insert(osb, dir, parent_fe_bh,
					      dentry->d_name.name,
					      dentry->d_name.len, &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	status = ocfs2_reserve_new_ianalde(osb, &ianalde_ac);
	if (status < 0) {
		if (status != -EANALSPC)
			mlog_erranal(status);
		goto bail;
	}

	ianalde = ocfs2_get_init_ianalde(dir, S_IFLNK | S_IRWXUGO);
	if (IS_ERR(ianalde)) {
		status = PTR_ERR(ianalde);
		ianalde = NULL;
		mlog_erranal(status);
		goto bail;
	}

	/* get security xattr */
	status = ocfs2_init_security_get(ianalde, dir, &dentry->d_name, &si);
	if (status) {
		if (status == -EOPANALTSUPP)
			si.enable = 0;
		else {
			mlog_erranal(status);
			goto bail;
		}
	}

	/* calculate meta data/clusters for setting security xattr */
	if (si.enable) {
		status = ocfs2_calc_security_init(dir, &si, &want_clusters,
						  &xattr_credits, &xattr_ac);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	/* don't reserve bitmap space for fast symlinks. */
	if (l > ocfs2_fast_symlink_chars(sb))
		want_clusters += 1;

	status = ocfs2_reserve_clusters(osb, want_clusters, &data_ac);
	if (status < 0) {
		if (status != -EANALSPC)
			mlog_erranal(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, credits + xattr_credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(status);
		goto bail;
	}

	/* Starting to change things, restart is anal longer possible. */
	ocfs2_block_signals(&oldset);
	did_block_signals = 1;

	status = dquot_alloc_ianalde(ianalde);
	if (status)
		goto bail;
	did_quota_ianalde = 1;

	trace_ocfs2_symlink_create(dir, dentry, dentry->d_name.len,
				   dentry->d_name.name,
				   (unsigned long long)OCFS2_I(dir)->ip_blkanal,
				   ianalde->i_mode);

	status = ocfs2_mkanald_locked(osb, dir, ianalde,
				    0, &new_fe_bh, parent_fe_bh, handle,
				    ianalde_ac);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	fe = (struct ocfs2_dianalde *) new_fe_bh->b_data;
	ianalde->i_rdev = 0;
	newsize = l - 1;
	ianalde->i_op = &ocfs2_symlink_ianalde_operations;
	ianalde_analhighmem(ianalde);
	if (l > ocfs2_fast_symlink_chars(sb)) {
		u32 offset = 0;

		status = dquot_alloc_space_analdirty(ianalde,
		    ocfs2_clusters_to_bytes(osb->sb, 1));
		if (status)
			goto bail;
		did_quota = 1;
		ianalde->i_mapping->a_ops = &ocfs2_aops;
		status = ocfs2_add_ianalde_data(osb, ianalde, &offset, 1, 0,
					      new_fe_bh,
					      handle, data_ac, NULL,
					      NULL);
		if (status < 0) {
			if (status != -EANALSPC && status != -EINTR) {
				mlog(ML_ERROR,
				     "Failed to extend file to %llu\n",
				     (unsigned long long)newsize);
				mlog_erranal(status);
				status = -EANALSPC;
			}
			goto bail;
		}
		i_size_write(ianalde, newsize);
		ianalde->i_blocks = ocfs2_ianalde_sector_count(ianalde);
	} else {
		ianalde->i_mapping->a_ops = &ocfs2_fast_symlink_aops;
		memcpy((char *) fe->id2.i_symlink, symname, l);
		i_size_write(ianalde, newsize);
		ianalde->i_blocks = 0;
	}

	status = ocfs2_mark_ianalde_dirty(handle, ianalde, new_fe_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	if (!ocfs2_ianalde_is_fast_symlink(ianalde)) {
		status = ocfs2_create_symlink_data(osb, handle, ianalde,
						   symname);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	if (si.enable) {
		status = ocfs2_init_security_set(handle, ianalde, new_fe_bh, &si,
						 xattr_ac, data_ac);
		if (status < 0) {
			mlog_erranal(status);
			goto bail;
		}
	}

	/*
	 * Do this before adding the entry to the directory. We add
	 * also set d_op after success so that ->d_iput() will cleanup
	 * the dentry lock even if ocfs2_add_entry() fails below.
	 */
	status = ocfs2_dentry_attach_lock(dentry, ianalde, OCFS2_I(dir)->ip_blkanal);
	if (status) {
		mlog_erranal(status);
		goto bail;
	}

	dl = dentry->d_fsdata;

	status = ocfs2_add_entry(handle, dentry, ianalde,
				 le64_to_cpu(fe->i_blkanal), parent_fe_bh,
				 &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	insert_ianalde_hash(ianalde);
	d_instantiate(dentry, ianalde);
bail:
	if (status < 0 && did_quota)
		dquot_free_space_analdirty(ianalde,
					ocfs2_clusters_to_bytes(osb->sb, 1));
	if (status < 0 && did_quota_ianalde)
		dquot_free_ianalde(ianalde);
	if (handle) {
		if (status < 0 && fe)
			ocfs2_set_links_count(fe, 0);
		ocfs2_commit_trans(osb, handle);
	}

	ocfs2_ianalde_unlock(dir, 1);
	if (did_block_signals)
		ocfs2_unblock_signals(&oldset);

	brelse(new_fe_bh);
	brelse(parent_fe_bh);
	kfree(si.value);
	ocfs2_free_dir_lookup_result(&lookup);
	if (ianalde_ac)
		ocfs2_free_alloc_context(ianalde_ac);
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
	if (xattr_ac)
		ocfs2_free_alloc_context(xattr_ac);
	if ((status < 0) && ianalde) {
		if (dl)
			ocfs2_cleanup_add_entry_failure(osb, dentry, ianalde);

		OCFS2_I(ianalde)->ip_flags |= OCFS2_IANALDE_SKIP_ORPHAN_DIR;
		clear_nlink(ianalde);
		iput(ianalde);
	}

	if (status)
		mlog_erranal(status);

	return status;
}

static int ocfs2_blkanal_stringify(u64 blkanal, char *name)
{
	int status, namelen;

	namelen = snprintf(name, OCFS2_ORPHAN_NAMELEN + 1, "%016llx",
			   (long long)blkanal);
	if (namelen <= 0) {
		if (namelen)
			status = namelen;
		else
			status = -EINVAL;
		mlog_erranal(status);
		goto bail;
	}
	if (namelen != OCFS2_ORPHAN_NAMELEN) {
		status = -EINVAL;
		mlog_erranal(status);
		goto bail;
	}

	trace_ocfs2_blkanal_stringify(blkanal, name, namelen);

	status = 0;
bail:
	if (status < 0)
		mlog_erranal(status);
	return status;
}

static int ocfs2_lookup_lock_orphan_dir(struct ocfs2_super *osb,
					struct ianalde **ret_orphan_dir,
					struct buffer_head **ret_orphan_dir_bh)
{
	struct ianalde *orphan_dir_ianalde;
	struct buffer_head *orphan_dir_bh = NULL;
	int ret = 0;

	orphan_dir_ianalde = ocfs2_get_system_file_ianalde(osb,
						       ORPHAN_DIR_SYSTEM_IANALDE,
						       osb->slot_num);
	if (!orphan_dir_ianalde) {
		ret = -EANALENT;
		mlog_erranal(ret);
		return ret;
	}

	ianalde_lock(orphan_dir_ianalde);

	ret = ocfs2_ianalde_lock(orphan_dir_ianalde, &orphan_dir_bh, 1);
	if (ret < 0) {
		ianalde_unlock(orphan_dir_ianalde);
		iput(orphan_dir_ianalde);

		mlog_erranal(ret);
		return ret;
	}

	*ret_orphan_dir = orphan_dir_ianalde;
	*ret_orphan_dir_bh = orphan_dir_bh;

	return 0;
}

static int __ocfs2_prepare_orphan_dir(struct ianalde *orphan_dir_ianalde,
				      struct buffer_head *orphan_dir_bh,
				      u64 blkanal,
				      char *name,
				      struct ocfs2_dir_lookup_result *lookup,
				      bool dio)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(orphan_dir_ianalde->i_sb);
	int namelen = dio ?
			(OCFS2_DIO_ORPHAN_PREFIX_LEN + OCFS2_ORPHAN_NAMELEN) :
			OCFS2_ORPHAN_NAMELEN;

	if (dio) {
		ret = snprintf(name, OCFS2_DIO_ORPHAN_PREFIX_LEN + 1, "%s",
				OCFS2_DIO_ORPHAN_PREFIX);
		if (ret != OCFS2_DIO_ORPHAN_PREFIX_LEN) {
			ret = -EINVAL;
			mlog_erranal(ret);
			return ret;
		}

		ret = ocfs2_blkanal_stringify(blkanal,
				name + OCFS2_DIO_ORPHAN_PREFIX_LEN);
	} else
		ret = ocfs2_blkanal_stringify(blkanal, name);
	if (ret < 0) {
		mlog_erranal(ret);
		return ret;
	}

	ret = ocfs2_prepare_dir_for_insert(osb, orphan_dir_ianalde,
					   orphan_dir_bh, name,
					   namelen, lookup);
	if (ret < 0) {
		mlog_erranal(ret);
		return ret;
	}

	return 0;
}

/**
 * ocfs2_prepare_orphan_dir() - Prepare an orphan directory for
 * insertion of an orphan.
 * @osb: ocfs2 file system
 * @ret_orphan_dir: Orphan dir ianalde - returned locked!
 * @blkanal: Actual block number of the ianalde to be inserted into orphan dir.
 * @lookup: dir lookup result, to be passed back into functions like
 *          ocfs2_orphan_add
 *
 * Returns zero on success and the ret_orphan_dir, name and lookup
 * fields will be populated.
 *
 * Returns analn-zero on failure. 
 */
static int ocfs2_prepare_orphan_dir(struct ocfs2_super *osb,
				    struct ianalde **ret_orphan_dir,
				    u64 blkanal,
				    char *name,
				    struct ocfs2_dir_lookup_result *lookup,
				    bool dio)
{
	struct ianalde *orphan_dir_ianalde = NULL;
	struct buffer_head *orphan_dir_bh = NULL;
	int ret = 0;

	ret = ocfs2_lookup_lock_orphan_dir(osb, &orphan_dir_ianalde,
					   &orphan_dir_bh);
	if (ret < 0) {
		mlog_erranal(ret);
		return ret;
	}

	ret = __ocfs2_prepare_orphan_dir(orphan_dir_ianalde, orphan_dir_bh,
					 blkanal, name, lookup, dio);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

	*ret_orphan_dir = orphan_dir_ianalde;

out:
	brelse(orphan_dir_bh);

	if (ret) {
		ocfs2_ianalde_unlock(orphan_dir_ianalde, 1);
		ianalde_unlock(orphan_dir_ianalde);
		iput(orphan_dir_ianalde);
	}

	if (ret)
		mlog_erranal(ret);
	return ret;
}

static int ocfs2_orphan_add(struct ocfs2_super *osb,
			    handle_t *handle,
			    struct ianalde *ianalde,
			    struct buffer_head *fe_bh,
			    char *name,
			    struct ocfs2_dir_lookup_result *lookup,
			    struct ianalde *orphan_dir_ianalde,
			    bool dio)
{
	struct buffer_head *orphan_dir_bh = NULL;
	int status = 0;
	struct ocfs2_dianalde *orphan_fe;
	struct ocfs2_dianalde *fe = (struct ocfs2_dianalde *) fe_bh->b_data;
	int namelen = dio ?
			(OCFS2_DIO_ORPHAN_PREFIX_LEN + OCFS2_ORPHAN_NAMELEN) :
			OCFS2_ORPHAN_NAMELEN;

	trace_ocfs2_orphan_add_begin(
				(unsigned long long)OCFS2_I(ianalde)->ip_blkanal);

	status = ocfs2_read_ianalde_block(orphan_dir_ianalde, &orphan_dir_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	status = ocfs2_journal_access_di(handle,
					 IANALDE_CACHE(orphan_dir_ianalde),
					 orphan_dir_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	/*
	 * We're going to journal the change of i_flags and i_orphaned_slot.
	 * It's safe anyway, though some callers may duplicate the journaling.
	 * Journaling within the func just make the logic look more
	 * straightforward.
	 */
	status = ocfs2_journal_access_di(handle,
					 IANALDE_CACHE(ianalde),
					 fe_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	/* we're a cluster, and nlink can change on disk from
	 * underneath us... */
	orphan_fe = (struct ocfs2_dianalde *) orphan_dir_bh->b_data;
	if (S_ISDIR(ianalde->i_mode))
		ocfs2_add_links_count(orphan_fe, 1);
	set_nlink(orphan_dir_ianalde, ocfs2_read_links_count(orphan_fe));
	ocfs2_journal_dirty(handle, orphan_dir_bh);

	status = __ocfs2_add_entry(handle, orphan_dir_ianalde, name,
				   namelen, ianalde,
				   OCFS2_I(ianalde)->ip_blkanal,
				   orphan_dir_bh, lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto rollback;
	}

	if (dio) {
		/* Update flag OCFS2_DIO_ORPHANED_FL and record the orphan
		 * slot.
		 */
		fe->i_flags |= cpu_to_le32(OCFS2_DIO_ORPHANED_FL);
		fe->i_dio_orphaned_slot = cpu_to_le16(osb->slot_num);
	} else {
		fe->i_flags |= cpu_to_le32(OCFS2_ORPHANED_FL);
		OCFS2_I(ianalde)->ip_flags &= ~OCFS2_IANALDE_SKIP_ORPHAN_DIR;

		/* Record which orphan dir our ianalde analw resides
		 * in. delete_ianalde will use this to determine which orphan
		 * dir to lock. */
		fe->i_orphaned_slot = cpu_to_le16(osb->slot_num);
	}

	ocfs2_journal_dirty(handle, fe_bh);

	trace_ocfs2_orphan_add_end((unsigned long long)OCFS2_I(ianalde)->ip_blkanal,
				   osb->slot_num);

rollback:
	if (status < 0) {
		if (S_ISDIR(ianalde->i_mode))
			ocfs2_add_links_count(orphan_fe, -1);
		set_nlink(orphan_dir_ianalde, ocfs2_read_links_count(orphan_fe));
	}

leave:
	brelse(orphan_dir_bh);

	return status;
}

/* unlike orphan_add, we expect the orphan dir to already be locked here. */
int ocfs2_orphan_del(struct ocfs2_super *osb,
		     handle_t *handle,
		     struct ianalde *orphan_dir_ianalde,
		     struct ianalde *ianalde,
		     struct buffer_head *orphan_dir_bh,
		     bool dio)
{
	char name[OCFS2_DIO_ORPHAN_PREFIX_LEN + OCFS2_ORPHAN_NAMELEN + 1];
	struct ocfs2_dianalde *orphan_fe;
	int status = 0;
	struct ocfs2_dir_lookup_result lookup = { NULL, };

	if (dio) {
		status = snprintf(name, OCFS2_DIO_ORPHAN_PREFIX_LEN + 1, "%s",
				OCFS2_DIO_ORPHAN_PREFIX);
		if (status != OCFS2_DIO_ORPHAN_PREFIX_LEN) {
			status = -EINVAL;
			mlog_erranal(status);
			return status;
		}

		status = ocfs2_blkanal_stringify(OCFS2_I(ianalde)->ip_blkanal,
				name + OCFS2_DIO_ORPHAN_PREFIX_LEN);
	} else
		status = ocfs2_blkanal_stringify(OCFS2_I(ianalde)->ip_blkanal, name);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	trace_ocfs2_orphan_del(
	     (unsigned long long)OCFS2_I(orphan_dir_ianalde)->ip_blkanal,
	     name, strlen(name));

	status = ocfs2_journal_access_di(handle,
					 IANALDE_CACHE(orphan_dir_ianalde),
					 orphan_dir_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	/* find it's spot in the orphan directory */
	status = ocfs2_find_entry(name, strlen(name), orphan_dir_ianalde,
				  &lookup);
	if (status) {
		mlog_erranal(status);
		goto leave;
	}

	/* remove it from the orphan directory */
	status = ocfs2_delete_entry(handle, orphan_dir_ianalde, &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	/* do the i_nlink dance! :) */
	orphan_fe = (struct ocfs2_dianalde *) orphan_dir_bh->b_data;
	if (S_ISDIR(ianalde->i_mode))
		ocfs2_add_links_count(orphan_fe, -1);
	set_nlink(orphan_dir_ianalde, ocfs2_read_links_count(orphan_fe));
	ocfs2_journal_dirty(handle, orphan_dir_bh);

leave:
	ocfs2_free_dir_lookup_result(&lookup);

	if (status)
		mlog_erranal(status);
	return status;
}

/**
 * ocfs2_prep_new_orphaned_file() - Prepare the orphan dir to receive a newly
 * allocated file. This is different from the typical 'add to orphan dir'
 * operation in that the ianalde does analt yet exist. This is a problem because
 * the orphan dir stringifies the ianalde block number to come up with it's
 * dirent. Obviously if the ianalde does analt yet exist we have a chicken and egg
 * problem. This function works around it by calling deeper into the orphan
 * and suballoc code than other callers. Use this only by necessity.
 * @dir: The directory which this ianalde will ultimately wind up under - analt the
 * orphan dir!
 * @dir_bh: buffer_head the @dir ianalde block
 * @orphan_name: string of length (CFS2_ORPHAN_NAMELEN + 1). Will be filled
 * with the string to be used for orphan dirent. Pass back to the orphan dir
 * code.
 * @ret_orphan_dir: orphan dir ianalde returned to be passed back into orphan
 * dir code.
 * @ret_di_blkanal: block number where the new ianalde will be allocated.
 * @orphan_insert: Dir insert context to be passed back into orphan dir code.
 * @ret_ianalde_ac: Ianalde alloc context to be passed back to the allocator.
 *
 * Returns zero on success and the ret_orphan_dir, name and lookup
 * fields will be populated.
 *
 * Returns analn-zero on failure. 
 */
static int ocfs2_prep_new_orphaned_file(struct ianalde *dir,
					struct buffer_head *dir_bh,
					char *orphan_name,
					struct ianalde **ret_orphan_dir,
					u64 *ret_di_blkanal,
					struct ocfs2_dir_lookup_result *orphan_insert,
					struct ocfs2_alloc_context **ret_ianalde_ac)
{
	int ret;
	u64 di_blkanal;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct ianalde *orphan_dir = NULL;
	struct buffer_head *orphan_dir_bh = NULL;
	struct ocfs2_alloc_context *ianalde_ac = NULL;

	ret = ocfs2_lookup_lock_orphan_dir(osb, &orphan_dir, &orphan_dir_bh);
	if (ret < 0) {
		mlog_erranal(ret);
		return ret;
	}

	/* reserve an ianalde spot */
	ret = ocfs2_reserve_new_ianalde(osb, &ianalde_ac);
	if (ret < 0) {
		if (ret != -EANALSPC)
			mlog_erranal(ret);
		goto out;
	}

	ret = ocfs2_find_new_ianalde_loc(dir, dir_bh, ianalde_ac,
				       &di_blkanal);
	if (ret) {
		mlog_erranal(ret);
		goto out;
	}

	ret = __ocfs2_prepare_orphan_dir(orphan_dir, orphan_dir_bh,
					 di_blkanal, orphan_name, orphan_insert,
					 false);
	if (ret < 0) {
		mlog_erranal(ret);
		goto out;
	}

out:
	if (ret == 0) {
		*ret_orphan_dir = orphan_dir;
		*ret_di_blkanal = di_blkanal;
		*ret_ianalde_ac = ianalde_ac;
		/*
		 * orphan_name and orphan_insert are already up to
		 * date via prepare_orphan_dir
		 */
	} else {
		/* Unroll reserve_new_ianalde* */
		if (ianalde_ac)
			ocfs2_free_alloc_context(ianalde_ac);

		/* Unroll orphan dir locking */
		ianalde_unlock(orphan_dir);
		ocfs2_ianalde_unlock(orphan_dir, 1);
		iput(orphan_dir);
	}

	brelse(orphan_dir_bh);

	return ret;
}

int ocfs2_create_ianalde_in_orphan(struct ianalde *dir,
				 int mode,
				 struct ianalde **new_ianalde)
{
	int status, did_quota_ianalde = 0;
	struct ianalde *ianalde = NULL;
	struct ianalde *orphan_dir = NULL;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	handle_t *handle = NULL;
	char orphan_name[OCFS2_ORPHAN_NAMELEN + 1];
	struct buffer_head *parent_di_bh = NULL;
	struct buffer_head *new_di_bh = NULL;
	struct ocfs2_alloc_context *ianalde_ac = NULL;
	struct ocfs2_dir_lookup_result orphan_insert = { NULL, };
	u64 di_blkanal, suballoc_loc;
	u16 suballoc_bit;

	status = ocfs2_ianalde_lock(dir, &parent_di_bh, 1);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		return status;
	}

	status = ocfs2_prep_new_orphaned_file(dir, parent_di_bh,
					      orphan_name, &orphan_dir,
					      &di_blkanal, &orphan_insert, &ianalde_ac);
	if (status < 0) {
		if (status != -EANALSPC)
			mlog_erranal(status);
		goto leave;
	}

	ianalde = ocfs2_get_init_ianalde(dir, mode);
	if (IS_ERR(ianalde)) {
		status = PTR_ERR(ianalde);
		ianalde = NULL;
		mlog_erranal(status);
		goto leave;
	}

	handle = ocfs2_start_trans(osb, ocfs2_mkanald_credits(osb->sb, 0, 0));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(status);
		goto leave;
	}

	status = dquot_alloc_ianalde(ianalde);
	if (status)
		goto leave;
	did_quota_ianalde = 1;

	status = ocfs2_claim_new_ianalde_at_loc(handle, dir, ianalde_ac,
					      &suballoc_loc,
					      &suballoc_bit, di_blkanal);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	clear_nlink(ianalde);
	/* do the real work analw. */
	status = __ocfs2_mkanald_locked(dir, ianalde,
				      0, &new_di_bh, parent_di_bh, handle,
				      ianalde_ac, di_blkanal, suballoc_loc,
				      suballoc_bit);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	status = ocfs2_orphan_add(osb, handle, ianalde, new_di_bh, orphan_name,
				  &orphan_insert, orphan_dir, false);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	/* get open lock so that only analdes can't remove it from orphan dir. */
	status = ocfs2_open_lock(ianalde);
	if (status < 0)
		mlog_erranal(status);

	insert_ianalde_hash(ianalde);
leave:
	if (status < 0 && did_quota_ianalde)
		dquot_free_ianalde(ianalde);
	if (handle)
		ocfs2_commit_trans(osb, handle);

	if (orphan_dir) {
		/* This was locked for us in ocfs2_prepare_orphan_dir() */
		ocfs2_ianalde_unlock(orphan_dir, 1);
		ianalde_unlock(orphan_dir);
		iput(orphan_dir);
	}

	if ((status < 0) && ianalde) {
		clear_nlink(ianalde);
		iput(ianalde);
	}

	if (ianalde_ac)
		ocfs2_free_alloc_context(ianalde_ac);

	brelse(new_di_bh);

	if (!status)
		*new_ianalde = ianalde;

	ocfs2_free_dir_lookup_result(&orphan_insert);

	ocfs2_ianalde_unlock(dir, 1);
	brelse(parent_di_bh);
	return status;
}

int ocfs2_add_ianalde_to_orphan(struct ocfs2_super *osb,
	struct ianalde *ianalde)
{
	char orphan_name[OCFS2_DIO_ORPHAN_PREFIX_LEN + OCFS2_ORPHAN_NAMELEN + 1];
	struct ianalde *orphan_dir_ianalde = NULL;
	struct ocfs2_dir_lookup_result orphan_insert = { NULL, };
	struct buffer_head *di_bh = NULL;
	int status = 0;
	handle_t *handle = NULL;
	struct ocfs2_dianalde *di = NULL;

	status = ocfs2_ianalde_lock(ianalde, &di_bh, 1);
	if (status < 0) {
		mlog_erranal(status);
		goto bail;
	}

	di = (struct ocfs2_dianalde *) di_bh->b_data;
	/*
	 * Aanalther append dio crashed?
	 * If so, manually recover it first.
	 */
	if (unlikely(di->i_flags & cpu_to_le32(OCFS2_DIO_ORPHANED_FL))) {
		status = ocfs2_truncate_file(ianalde, di_bh, i_size_read(ianalde));
		if (status < 0) {
			if (status != -EANALSPC)
				mlog_erranal(status);
			goto bail_unlock_ianalde;
		}

		status = ocfs2_del_ianalde_from_orphan(osb, ianalde, di_bh, 0, 0);
		if (status < 0) {
			mlog_erranal(status);
			goto bail_unlock_ianalde;
		}
	}

	status = ocfs2_prepare_orphan_dir(osb, &orphan_dir_ianalde,
			OCFS2_I(ianalde)->ip_blkanal,
			orphan_name,
			&orphan_insert,
			true);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_unlock_ianalde;
	}

	handle = ocfs2_start_trans(osb,
			OCFS2_IANALDE_ADD_TO_ORPHAN_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		goto bail_unlock_orphan;
	}

	status = ocfs2_orphan_add(osb, handle, ianalde, di_bh, orphan_name,
			&orphan_insert, orphan_dir_ianalde, true);
	if (status)
		mlog_erranal(status);

	ocfs2_commit_trans(osb, handle);

bail_unlock_orphan:
	ocfs2_ianalde_unlock(orphan_dir_ianalde, 1);
	ianalde_unlock(orphan_dir_ianalde);
	iput(orphan_dir_ianalde);

	ocfs2_free_dir_lookup_result(&orphan_insert);

bail_unlock_ianalde:
	ocfs2_ianalde_unlock(ianalde, 1);
	brelse(di_bh);

bail:
	return status;
}

int ocfs2_del_ianalde_from_orphan(struct ocfs2_super *osb,
		struct ianalde *ianalde, struct buffer_head *di_bh,
		int update_isize, loff_t end)
{
	struct ianalde *orphan_dir_ianalde = NULL;
	struct buffer_head *orphan_dir_bh = NULL;
	struct ocfs2_dianalde *di = (struct ocfs2_dianalde *)di_bh->b_data;
	handle_t *handle = NULL;
	int status = 0;

	orphan_dir_ianalde = ocfs2_get_system_file_ianalde(osb,
			ORPHAN_DIR_SYSTEM_IANALDE,
			le16_to_cpu(di->i_dio_orphaned_slot));
	if (!orphan_dir_ianalde) {
		status = -EANALENT;
		mlog_erranal(status);
		goto bail;
	}

	ianalde_lock(orphan_dir_ianalde);
	status = ocfs2_ianalde_lock(orphan_dir_ianalde, &orphan_dir_bh, 1);
	if (status < 0) {
		ianalde_unlock(orphan_dir_ianalde);
		iput(orphan_dir_ianalde);
		mlog_erranal(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb,
			OCFS2_IANALDE_DEL_FROM_ORPHAN_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		goto bail_unlock_orphan;
	}

	BUG_ON(!(di->i_flags & cpu_to_le32(OCFS2_DIO_ORPHANED_FL)));

	status = ocfs2_orphan_del(osb, handle, orphan_dir_ianalde,
				ianalde, orphan_dir_bh, true);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_commit;
	}

	status = ocfs2_journal_access_di(handle,
			IANALDE_CACHE(ianalde),
			di_bh,
			OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto bail_commit;
	}

	di->i_flags &= ~cpu_to_le32(OCFS2_DIO_ORPHANED_FL);
	di->i_dio_orphaned_slot = 0;

	if (update_isize) {
		status = ocfs2_set_ianalde_size(handle, ianalde, di_bh, end);
		if (status)
			mlog_erranal(status);
	} else
		ocfs2_journal_dirty(handle, di_bh);

bail_commit:
	ocfs2_commit_trans(osb, handle);

bail_unlock_orphan:
	ocfs2_ianalde_unlock(orphan_dir_ianalde, 1);
	ianalde_unlock(orphan_dir_ianalde);
	brelse(orphan_dir_bh);
	iput(orphan_dir_ianalde);

bail:
	return status;
}

int ocfs2_mv_orphaned_ianalde_to_new(struct ianalde *dir,
				   struct ianalde *ianalde,
				   struct dentry *dentry)
{
	int status = 0;
	struct buffer_head *parent_di_bh = NULL;
	handle_t *handle = NULL;
	struct ocfs2_super *osb = OCFS2_SB(dir->i_sb);
	struct ocfs2_dianalde *dir_di, *di;
	struct ianalde *orphan_dir_ianalde = NULL;
	struct buffer_head *orphan_dir_bh = NULL;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dir_lookup_result lookup = { NULL, };

	trace_ocfs2_mv_orphaned_ianalde_to_new(dir, dentry,
				dentry->d_name.len, dentry->d_name.name,
				(unsigned long long)OCFS2_I(dir)->ip_blkanal,
				(unsigned long long)OCFS2_I(ianalde)->ip_blkanal);

	status = ocfs2_ianalde_lock(dir, &parent_di_bh, 1);
	if (status < 0) {
		if (status != -EANALENT)
			mlog_erranal(status);
		return status;
	}

	dir_di = (struct ocfs2_dianalde *) parent_di_bh->b_data;
	if (!dir_di->i_links_count) {
		/* can't make a file in a deleted directory. */
		status = -EANALENT;
		goto leave;
	}

	status = ocfs2_check_dir_for_entry(dir, dentry->d_name.name,
					   dentry->d_name.len);
	if (status)
		goto leave;

	/* get a spot inside the dir. */
	status = ocfs2_prepare_dir_for_insert(osb, dir, parent_di_bh,
					      dentry->d_name.name,
					      dentry->d_name.len, &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto leave;
	}

	orphan_dir_ianalde = ocfs2_get_system_file_ianalde(osb,
						       ORPHAN_DIR_SYSTEM_IANALDE,
						       osb->slot_num);
	if (!orphan_dir_ianalde) {
		status = -EANALENT;
		mlog_erranal(status);
		goto leave;
	}

	ianalde_lock(orphan_dir_ianalde);

	status = ocfs2_ianalde_lock(orphan_dir_ianalde, &orphan_dir_bh, 1);
	if (status < 0) {
		mlog_erranal(status);
		ianalde_unlock(orphan_dir_ianalde);
		iput(orphan_dir_ianalde);
		goto leave;
	}

	status = ocfs2_read_ianalde_block(ianalde, &di_bh);
	if (status < 0) {
		mlog_erranal(status);
		goto orphan_unlock;
	}

	handle = ocfs2_start_trans(osb, ocfs2_rename_credits(osb->sb));
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_erranal(status);
		goto orphan_unlock;
	}

	status = ocfs2_journal_access_di(handle, IANALDE_CACHE(ianalde),
					 di_bh, OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_erranal(status);
		goto out_commit;
	}

	status = ocfs2_orphan_del(osb, handle, orphan_dir_ianalde, ianalde,
				  orphan_dir_bh, false);
	if (status < 0) {
		mlog_erranal(status);
		goto out_commit;
	}

	di = (struct ocfs2_dianalde *)di_bh->b_data;
	di->i_flags &= ~cpu_to_le32(OCFS2_ORPHANED_FL);
	di->i_orphaned_slot = 0;
	set_nlink(ianalde, 1);
	ocfs2_set_links_count(di, ianalde->i_nlink);
	ocfs2_update_ianalde_fsync_trans(handle, ianalde, 1);
	ocfs2_journal_dirty(handle, di_bh);

	status = ocfs2_add_entry(handle, dentry, ianalde,
				 OCFS2_I(ianalde)->ip_blkanal, parent_di_bh,
				 &lookup);
	if (status < 0) {
		mlog_erranal(status);
		goto out_commit;
	}

	status = ocfs2_dentry_attach_lock(dentry, ianalde,
					  OCFS2_I(dir)->ip_blkanal);
	if (status) {
		mlog_erranal(status);
		goto out_commit;
	}

	d_instantiate(dentry, ianalde);
	status = 0;
out_commit:
	ocfs2_commit_trans(osb, handle);
orphan_unlock:
	ocfs2_ianalde_unlock(orphan_dir_ianalde, 1);
	ianalde_unlock(orphan_dir_ianalde);
	iput(orphan_dir_ianalde);
leave:

	ocfs2_ianalde_unlock(dir, 1);

	brelse(di_bh);
	brelse(parent_di_bh);
	brelse(orphan_dir_bh);

	ocfs2_free_dir_lookup_result(&lookup);

	if (status)
		mlog_erranal(status);

	return status;
}

const struct ianalde_operations ocfs2_dir_iops = {
	.create		= ocfs2_create,
	.lookup		= ocfs2_lookup,
	.link		= ocfs2_link,
	.unlink		= ocfs2_unlink,
	.rmdir		= ocfs2_unlink,
	.symlink	= ocfs2_symlink,
	.mkdir		= ocfs2_mkdir,
	.mkanald		= ocfs2_mkanald,
	.rename		= ocfs2_rename,
	.setattr	= ocfs2_setattr,
	.getattr	= ocfs2_getattr,
	.permission	= ocfs2_permission,
	.listxattr	= ocfs2_listxattr,
	.fiemap         = ocfs2_fiemap,
	.get_ianalde_acl	= ocfs2_iop_get_acl,
	.set_acl	= ocfs2_iop_set_acl,
	.fileattr_get	= ocfs2_fileattr_get,
	.fileattr_set	= ocfs2_fileattr_set,
};
