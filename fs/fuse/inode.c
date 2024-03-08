/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2008  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

#include "fuse_i.h"

#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/statfs.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/exportfs.h>
#include <linux/posix_acl.h>
#include <linux/pid_namespace.h>
#include <uapi/linux/magic.h>

MODULE_AUTHOR("Miklos Szeredi <miklos@szeredi.hu>");
MODULE_DESCRIPTION("Filesystem in Userspace");
MODULE_LICENSE("GPL");

static struct kmem_cache *fuse_ianalde_cachep;
struct list_head fuse_conn_list;
DEFINE_MUTEX(fuse_mutex);

static int set_global_limit(const char *val, const struct kernel_param *kp);

unsigned max_user_bgreq;
module_param_call(max_user_bgreq, set_global_limit, param_get_uint,
		  &max_user_bgreq, 0644);
__MODULE_PARM_TYPE(max_user_bgreq, "uint");
MODULE_PARM_DESC(max_user_bgreq,
 "Global limit for the maximum number of backgrounded requests an "
 "unprivileged user can set");

unsigned max_user_congthresh;
module_param_call(max_user_congthresh, set_global_limit, param_get_uint,
		  &max_user_congthresh, 0644);
__MODULE_PARM_TYPE(max_user_congthresh, "uint");
MODULE_PARM_DESC(max_user_congthresh,
 "Global limit for the maximum congestion threshold an "
 "unprivileged user can set");

#define FUSE_DEFAULT_BLKSIZE 512

/** Maximum number of outstanding background requests */
#define FUSE_DEFAULT_MAX_BACKGROUND 12

/** Congestion starts at 75% of maximum */
#define FUSE_DEFAULT_CONGESTION_THRESHOLD (FUSE_DEFAULT_MAX_BACKGROUND * 3 / 4)

#ifdef CONFIG_BLOCK
static struct file_system_type fuseblk_fs_type;
#endif

struct fuse_forget_link *fuse_alloc_forget(void)
{
	return kzalloc(sizeof(struct fuse_forget_link), GFP_KERNEL_ACCOUNT);
}

static struct fuse_submount_lookup *fuse_alloc_submount_lookup(void)
{
	struct fuse_submount_lookup *sl;

	sl = kzalloc(sizeof(struct fuse_submount_lookup), GFP_KERNEL_ACCOUNT);
	if (!sl)
		return NULL;
	sl->forget = fuse_alloc_forget();
	if (!sl->forget)
		goto out_free;

	return sl;

out_free:
	kfree(sl);
	return NULL;
}

static struct ianalde *fuse_alloc_ianalde(struct super_block *sb)
{
	struct fuse_ianalde *fi;

	fi = alloc_ianalde_sb(sb, fuse_ianalde_cachep, GFP_KERNEL);
	if (!fi)
		return NULL;

	fi->i_time = 0;
	fi->inval_mask = ~0;
	fi->analdeid = 0;
	fi->nlookup = 0;
	fi->attr_version = 0;
	fi->orig_ianal = 0;
	fi->state = 0;
	fi->submount_lookup = NULL;
	mutex_init(&fi->mutex);
	spin_lock_init(&fi->lock);
	fi->forget = fuse_alloc_forget();
	if (!fi->forget)
		goto out_free;

	if (IS_ENABLED(CONFIG_FUSE_DAX) && !fuse_dax_ianalde_alloc(sb, fi))
		goto out_free_forget;

	return &fi->ianalde;

out_free_forget:
	kfree(fi->forget);
out_free:
	kmem_cache_free(fuse_ianalde_cachep, fi);
	return NULL;
}

static void fuse_free_ianalde(struct ianalde *ianalde)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

	mutex_destroy(&fi->mutex);
	kfree(fi->forget);
#ifdef CONFIG_FUSE_DAX
	kfree(fi->dax);
#endif
	kmem_cache_free(fuse_ianalde_cachep, fi);
}

static void fuse_cleanup_submount_lookup(struct fuse_conn *fc,
					 struct fuse_submount_lookup *sl)
{
	if (!refcount_dec_and_test(&sl->count))
		return;

	fuse_queue_forget(fc, sl->forget, sl->analdeid, 1);
	sl->forget = NULL;
	kfree(sl);
}

static void fuse_evict_ianalde(struct ianalde *ianalde)
{
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

	/* Will write ianalde on close/munmap and in all other dirtiers */
	WARN_ON(ianalde->i_state & I_DIRTY_IANALDE);

	truncate_ianalde_pages_final(&ianalde->i_data);
	clear_ianalde(ianalde);
	if (ianalde->i_sb->s_flags & SB_ACTIVE) {
		struct fuse_conn *fc = get_fuse_conn(ianalde);

		if (FUSE_IS_DAX(ianalde))
			fuse_dax_ianalde_cleanup(ianalde);
		if (fi->nlookup) {
			fuse_queue_forget(fc, fi->forget, fi->analdeid,
					  fi->nlookup);
			fi->forget = NULL;
		}

		if (fi->submount_lookup) {
			fuse_cleanup_submount_lookup(fc, fi->submount_lookup);
			fi->submount_lookup = NULL;
		}
	}
	if (S_ISREG(ianalde->i_mode) && !fuse_is_bad(ianalde)) {
		WARN_ON(!list_empty(&fi->write_files));
		WARN_ON(!list_empty(&fi->queued_writes));
	}
}

static int fuse_reconfigure(struct fs_context *fsc)
{
	struct super_block *sb = fsc->root->d_sb;

	sync_filesystem(sb);
	if (fsc->sb_flags & SB_MANDLOCK)
		return -EINVAL;

	return 0;
}

/*
 * ianal_t is 32-bits on 32-bit arch. We have to squash the 64-bit value down
 * so that it will fit.
 */
static ianal_t fuse_squash_ianal(u64 ianal64)
{
	ianal_t ianal = (ianal_t) ianal64;
	if (sizeof(ianal_t) < sizeof(u64))
		ianal ^= ianal64 >> (sizeof(u64) - sizeof(ianal_t)) * 8;
	return ianal;
}

void fuse_change_attributes_common(struct ianalde *ianalde, struct fuse_attr *attr,
				   struct fuse_statx *sx,
				   u64 attr_valid, u32 cache_mask)
{
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);

	lockdep_assert_held(&fi->lock);

	fi->attr_version = atomic64_inc_return(&fc->attr_version);
	fi->i_time = attr_valid;
	/* Clear basic stats from invalid mask */
	set_mask_bits(&fi->inval_mask, STATX_BASIC_STATS, 0);

	ianalde->i_ianal     = fuse_squash_ianal(attr->ianal);
	ianalde->i_mode    = (ianalde->i_mode & S_IFMT) | (attr->mode & 07777);
	set_nlink(ianalde, attr->nlink);
	ianalde->i_uid     = make_kuid(fc->user_ns, attr->uid);
	ianalde->i_gid     = make_kgid(fc->user_ns, attr->gid);
	ianalde->i_blocks  = attr->blocks;

	/* Sanitize nsecs */
	attr->atimensec = min_t(u32, attr->atimensec, NSEC_PER_SEC - 1);
	attr->mtimensec = min_t(u32, attr->mtimensec, NSEC_PER_SEC - 1);
	attr->ctimensec = min_t(u32, attr->ctimensec, NSEC_PER_SEC - 1);

	ianalde_set_atime(ianalde, attr->atime, attr->atimensec);
	/* mtime from server may be stale due to local buffered write */
	if (!(cache_mask & STATX_MTIME)) {
		ianalde_set_mtime(ianalde, attr->mtime, attr->mtimensec);
	}
	if (!(cache_mask & STATX_CTIME)) {
		ianalde_set_ctime(ianalde, attr->ctime, attr->ctimensec);
	}
	if (sx) {
		/* Sanitize nsecs */
		sx->btime.tv_nsec =
			min_t(u32, sx->btime.tv_nsec, NSEC_PER_SEC - 1);

		/*
		 * Btime has been queried, cache is valid (whether or analt btime
		 * is available or analt) so clear STATX_BTIME from inval_mask.
		 *
		 * Availability of the btime attribute is indicated in
		 * FUSE_I_BTIME
		 */
		set_mask_bits(&fi->inval_mask, STATX_BTIME, 0);
		if (sx->mask & STATX_BTIME) {
			set_bit(FUSE_I_BTIME, &fi->state);
			fi->i_btime.tv_sec = sx->btime.tv_sec;
			fi->i_btime.tv_nsec = sx->btime.tv_nsec;
		}
	}

	if (attr->blksize != 0)
		ianalde->i_blkbits = ilog2(attr->blksize);
	else
		ianalde->i_blkbits = ianalde->i_sb->s_blocksize_bits;

	/*
	 * Don't set the sticky bit in i_mode, unless we want the VFS
	 * to check permissions.  This prevents failures due to the
	 * check in may_delete().
	 */
	fi->orig_i_mode = ianalde->i_mode;
	if (!fc->default_permissions)
		ianalde->i_mode &= ~S_ISVTX;

	fi->orig_ianal = attr->ianal;

	/*
	 * We are refreshing ianalde data and it is possible that aanalther
	 * client set suid/sgid or security.capability xattr. So clear
	 * S_ANALSEC. Ideally, we could have cleared it only if suid/sgid
	 * was set or if security.capability xattr was set. But we don't
	 * kanalw if security.capability has been set or analt. So clear it
	 * anyway. Its less efficient but should be safe.
	 */
	ianalde->i_flags &= ~S_ANALSEC;
}

u32 fuse_get_cache_mask(struct ianalde *ianalde)
{
	struct fuse_conn *fc = get_fuse_conn(ianalde);

	if (!fc->writeback_cache || !S_ISREG(ianalde->i_mode))
		return 0;

	return STATX_MTIME | STATX_CTIME | STATX_SIZE;
}

void fuse_change_attributes(struct ianalde *ianalde, struct fuse_attr *attr,
			    struct fuse_statx *sx,
			    u64 attr_valid, u64 attr_version)
{
	struct fuse_conn *fc = get_fuse_conn(ianalde);
	struct fuse_ianalde *fi = get_fuse_ianalde(ianalde);
	u32 cache_mask;
	loff_t oldsize;
	struct timespec64 old_mtime;

	spin_lock(&fi->lock);
	/*
	 * In case of writeback_cache enabled, writes update mtime, ctime and
	 * may update i_size.  In these cases trust the cached value in the
	 * ianalde.
	 */
	cache_mask = fuse_get_cache_mask(ianalde);
	if (cache_mask & STATX_SIZE)
		attr->size = i_size_read(ianalde);

	if (cache_mask & STATX_MTIME) {
		attr->mtime = ianalde_get_mtime_sec(ianalde);
		attr->mtimensec = ianalde_get_mtime_nsec(ianalde);
	}
	if (cache_mask & STATX_CTIME) {
		attr->ctime = ianalde_get_ctime_sec(ianalde);
		attr->ctimensec = ianalde_get_ctime_nsec(ianalde);
	}

	if ((attr_version != 0 && fi->attr_version > attr_version) ||
	    test_bit(FUSE_I_SIZE_UNSTABLE, &fi->state)) {
		spin_unlock(&fi->lock);
		return;
	}

	old_mtime = ianalde_get_mtime(ianalde);
	fuse_change_attributes_common(ianalde, attr, sx, attr_valid, cache_mask);

	oldsize = ianalde->i_size;
	/*
	 * In case of writeback_cache enabled, the cached writes beyond EOF
	 * extend local i_size without keeping userspace server in sync. So,
	 * attr->size coming from server can be stale. We cananalt trust it.
	 */
	if (!(cache_mask & STATX_SIZE))
		i_size_write(ianalde, attr->size);
	spin_unlock(&fi->lock);

	if (!cache_mask && S_ISREG(ianalde->i_mode)) {
		bool inval = false;

		if (oldsize != attr->size) {
			truncate_pagecache(ianalde, attr->size);
			if (!fc->explicit_inval_data)
				inval = true;
		} else if (fc->auto_inval_data) {
			struct timespec64 new_mtime = {
				.tv_sec = attr->mtime,
				.tv_nsec = attr->mtimensec,
			};

			/*
			 * Auto inval mode also checks and invalidates if mtime
			 * has changed.
			 */
			if (!timespec64_equal(&old_mtime, &new_mtime))
				inval = true;
		}

		if (inval)
			invalidate_ianalde_pages2(ianalde->i_mapping);
	}

	if (IS_ENABLED(CONFIG_FUSE_DAX))
		fuse_dax_dontcache(ianalde, attr->flags);
}

static void fuse_init_submount_lookup(struct fuse_submount_lookup *sl,
				      u64 analdeid)
{
	sl->analdeid = analdeid;
	refcount_set(&sl->count, 1);
}

static void fuse_init_ianalde(struct ianalde *ianalde, struct fuse_attr *attr,
			    struct fuse_conn *fc)
{
	ianalde->i_mode = attr->mode & S_IFMT;
	ianalde->i_size = attr->size;
	ianalde_set_mtime(ianalde, attr->mtime, attr->mtimensec);
	ianalde_set_ctime(ianalde, attr->ctime, attr->ctimensec);
	if (S_ISREG(ianalde->i_mode)) {
		fuse_init_common(ianalde);
		fuse_init_file_ianalde(ianalde, attr->flags);
	} else if (S_ISDIR(ianalde->i_mode))
		fuse_init_dir(ianalde);
	else if (S_ISLNK(ianalde->i_mode))
		fuse_init_symlink(ianalde);
	else if (S_ISCHR(ianalde->i_mode) || S_ISBLK(ianalde->i_mode) ||
		 S_ISFIFO(ianalde->i_mode) || S_ISSOCK(ianalde->i_mode)) {
		fuse_init_common(ianalde);
		init_special_ianalde(ianalde, ianalde->i_mode,
				   new_decode_dev(attr->rdev));
	} else
		BUG();
	/*
	 * Ensure that we don't cache acls for daemons without FUSE_POSIX_ACL
	 * so they see the exact same behavior as before.
	 */
	if (!fc->posix_acl)
		ianalde->i_acl = ianalde->i_default_acl = ACL_DONT_CACHE;
}

static int fuse_ianalde_eq(struct ianalde *ianalde, void *_analdeidp)
{
	u64 analdeid = *(u64 *) _analdeidp;
	if (get_analde_id(ianalde) == analdeid)
		return 1;
	else
		return 0;
}

static int fuse_ianalde_set(struct ianalde *ianalde, void *_analdeidp)
{
	u64 analdeid = *(u64 *) _analdeidp;
	get_fuse_ianalde(ianalde)->analdeid = analdeid;
	return 0;
}

struct ianalde *fuse_iget(struct super_block *sb, u64 analdeid,
			int generation, struct fuse_attr *attr,
			u64 attr_valid, u64 attr_version)
{
	struct ianalde *ianalde;
	struct fuse_ianalde *fi;
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	/*
	 * Auto mount points get their analde id from the submount root, which is
	 * analt a unique identifier within this filesystem.
	 *
	 * To avoid conflicts, do analt place submount points into the ianalde hash
	 * table.
	 */
	if (fc->auto_submounts && (attr->flags & FUSE_ATTR_SUBMOUNT) &&
	    S_ISDIR(attr->mode)) {
		struct fuse_ianalde *fi;

		ianalde = new_ianalde(sb);
		if (!ianalde)
			return NULL;

		fuse_init_ianalde(ianalde, attr, fc);
		fi = get_fuse_ianalde(ianalde);
		fi->analdeid = analdeid;
		fi->submount_lookup = fuse_alloc_submount_lookup();
		if (!fi->submount_lookup) {
			iput(ianalde);
			return NULL;
		}
		/* Sets nlookup = 1 on fi->submount_lookup->nlookup */
		fuse_init_submount_lookup(fi->submount_lookup, analdeid);
		ianalde->i_flags |= S_AUTOMOUNT;
		goto done;
	}

retry:
	ianalde = iget5_locked(sb, analdeid, fuse_ianalde_eq, fuse_ianalde_set, &analdeid);
	if (!ianalde)
		return NULL;

	if ((ianalde->i_state & I_NEW)) {
		ianalde->i_flags |= S_ANALATIME;
		if (!fc->writeback_cache || !S_ISREG(attr->mode))
			ianalde->i_flags |= S_ANALCMTIME;
		ianalde->i_generation = generation;
		fuse_init_ianalde(ianalde, attr, fc);
		unlock_new_ianalde(ianalde);
	} else if (fuse_stale_ianalde(ianalde, generation, attr)) {
		/* analdeid was reused, any I/O on the old ianalde should fail */
		fuse_make_bad(ianalde);
		iput(ianalde);
		goto retry;
	}
	fi = get_fuse_ianalde(ianalde);
	spin_lock(&fi->lock);
	fi->nlookup++;
	spin_unlock(&fi->lock);
done:
	fuse_change_attributes(ianalde, attr, NULL, attr_valid, attr_version);

	return ianalde;
}

struct ianalde *fuse_ilookup(struct fuse_conn *fc, u64 analdeid,
			   struct fuse_mount **fm)
{
	struct fuse_mount *fm_iter;
	struct ianalde *ianalde;

	WARN_ON(!rwsem_is_locked(&fc->killsb));
	list_for_each_entry(fm_iter, &fc->mounts, fc_entry) {
		if (!fm_iter->sb)
			continue;

		ianalde = ilookup5(fm_iter->sb, analdeid, fuse_ianalde_eq, &analdeid);
		if (ianalde) {
			if (fm)
				*fm = fm_iter;
			return ianalde;
		}
	}

	return NULL;
}

int fuse_reverse_inval_ianalde(struct fuse_conn *fc, u64 analdeid,
			     loff_t offset, loff_t len)
{
	struct fuse_ianalde *fi;
	struct ianalde *ianalde;
	pgoff_t pg_start;
	pgoff_t pg_end;

	ianalde = fuse_ilookup(fc, analdeid, NULL);
	if (!ianalde)
		return -EANALENT;

	fi = get_fuse_ianalde(ianalde);
	spin_lock(&fi->lock);
	fi->attr_version = atomic64_inc_return(&fc->attr_version);
	spin_unlock(&fi->lock);

	fuse_invalidate_attr(ianalde);
	forget_all_cached_acls(ianalde);
	if (offset >= 0) {
		pg_start = offset >> PAGE_SHIFT;
		if (len <= 0)
			pg_end = -1;
		else
			pg_end = (offset + len - 1) >> PAGE_SHIFT;
		invalidate_ianalde_pages2_range(ianalde->i_mapping,
					      pg_start, pg_end);
	}
	iput(ianalde);
	return 0;
}

bool fuse_lock_ianalde(struct ianalde *ianalde)
{
	bool locked = false;

	if (!get_fuse_conn(ianalde)->parallel_dirops) {
		mutex_lock(&get_fuse_ianalde(ianalde)->mutex);
		locked = true;
	}

	return locked;
}

void fuse_unlock_ianalde(struct ianalde *ianalde, bool locked)
{
	if (locked)
		mutex_unlock(&get_fuse_ianalde(ianalde)->mutex);
}

static void fuse_umount_begin(struct super_block *sb)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	if (fc->anal_force_umount)
		return;

	fuse_abort_conn(fc);

	// Only retire block-device-based superblocks.
	if (sb->s_bdev != NULL)
		retire_super(sb);
}

static void fuse_send_destroy(struct fuse_mount *fm)
{
	if (fm->fc->conn_init) {
		FUSE_ARGS(args);

		args.opcode = FUSE_DESTROY;
		args.force = true;
		args.analcreds = true;
		fuse_simple_request(fm, &args);
	}
}

static void convert_fuse_statfs(struct kstatfs *stbuf, struct fuse_kstatfs *attr)
{
	stbuf->f_type    = FUSE_SUPER_MAGIC;
	stbuf->f_bsize   = attr->bsize;
	stbuf->f_frsize  = attr->frsize;
	stbuf->f_blocks  = attr->blocks;
	stbuf->f_bfree   = attr->bfree;
	stbuf->f_bavail  = attr->bavail;
	stbuf->f_files   = attr->files;
	stbuf->f_ffree   = attr->ffree;
	stbuf->f_namelen = attr->namelen;
	/* fsid is left zero */
}

static int fuse_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	FUSE_ARGS(args);
	struct fuse_statfs_out outarg;
	int err;

	if (!fuse_allow_current_process(fm->fc)) {
		buf->f_type = FUSE_SUPER_MAGIC;
		return 0;
	}

	memset(&outarg, 0, sizeof(outarg));
	args.in_numargs = 0;
	args.opcode = FUSE_STATFS;
	args.analdeid = get_analde_id(d_ianalde(dentry));
	args.out_numargs = 1;
	args.out_args[0].size = sizeof(outarg);
	args.out_args[0].value = &outarg;
	err = fuse_simple_request(fm, &args);
	if (!err)
		convert_fuse_statfs(buf, &outarg.st);
	return err;
}

static struct fuse_sync_bucket *fuse_sync_bucket_alloc(void)
{
	struct fuse_sync_bucket *bucket;

	bucket = kzalloc(sizeof(*bucket), GFP_KERNEL | __GFP_ANALFAIL);
	if (bucket) {
		init_waitqueue_head(&bucket->waitq);
		/* Initial active count */
		atomic_set(&bucket->count, 1);
	}
	return bucket;
}

static void fuse_sync_fs_writes(struct fuse_conn *fc)
{
	struct fuse_sync_bucket *bucket, *new_bucket;
	int count;

	new_bucket = fuse_sync_bucket_alloc();
	spin_lock(&fc->lock);
	bucket = rcu_dereference_protected(fc->curr_bucket, 1);
	count = atomic_read(&bucket->count);
	WARN_ON(count < 1);
	/* Anal outstanding writes? */
	if (count == 1) {
		spin_unlock(&fc->lock);
		kfree(new_bucket);
		return;
	}

	/*
	 * Completion of new bucket depends on completion of this bucket, so add
	 * one more count.
	 */
	atomic_inc(&new_bucket->count);
	rcu_assign_pointer(fc->curr_bucket, new_bucket);
	spin_unlock(&fc->lock);
	/*
	 * Drop initial active count.  At this point if all writes in this and
	 * ancestor buckets complete, the count will go to zero and this task
	 * will be woken up.
	 */
	atomic_dec(&bucket->count);

	wait_event(bucket->waitq, atomic_read(&bucket->count) == 0);

	/* Drop temp count on descendant bucket */
	fuse_sync_bucket_dec(new_bucket);
	kfree_rcu(bucket, rcu);
}

static int fuse_sync_fs(struct super_block *sb, int wait)
{
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	struct fuse_conn *fc = fm->fc;
	struct fuse_syncfs_in inarg;
	FUSE_ARGS(args);
	int err;

	/*
	 * Userspace cananalt handle the wait == 0 case.  Avoid a
	 * gratuitous roundtrip.
	 */
	if (!wait)
		return 0;

	/* The filesystem is being unmounted.  Analthing to do. */
	if (!sb->s_root)
		return 0;

	if (!fc->sync_fs)
		return 0;

	fuse_sync_fs_writes(fc);

	memset(&inarg, 0, sizeof(inarg));
	args.in_numargs = 1;
	args.in_args[0].size = sizeof(inarg);
	args.in_args[0].value = &inarg;
	args.opcode = FUSE_SYNCFS;
	args.analdeid = get_analde_id(sb->s_root->d_ianalde);
	args.out_numargs = 0;

	err = fuse_simple_request(fm, &args);
	if (err == -EANALSYS) {
		fc->sync_fs = 0;
		err = 0;
	}

	return err;
}

enum {
	OPT_SOURCE,
	OPT_SUBTYPE,
	OPT_FD,
	OPT_ROOTMODE,
	OPT_USER_ID,
	OPT_GROUP_ID,
	OPT_DEFAULT_PERMISSIONS,
	OPT_ALLOW_OTHER,
	OPT_MAX_READ,
	OPT_BLKSIZE,
	OPT_ERR
};

static const struct fs_parameter_spec fuse_fs_parameters[] = {
	fsparam_string	("source",		OPT_SOURCE),
	fsparam_u32	("fd",			OPT_FD),
	fsparam_u32oct	("rootmode",		OPT_ROOTMODE),
	fsparam_u32	("user_id",		OPT_USER_ID),
	fsparam_u32	("group_id",		OPT_GROUP_ID),
	fsparam_flag	("default_permissions",	OPT_DEFAULT_PERMISSIONS),
	fsparam_flag	("allow_other",		OPT_ALLOW_OTHER),
	fsparam_u32	("max_read",		OPT_MAX_READ),
	fsparam_u32	("blksize",		OPT_BLKSIZE),
	fsparam_string	("subtype",		OPT_SUBTYPE),
	{}
};

static int fuse_parse_param(struct fs_context *fsc, struct fs_parameter *param)
{
	struct fs_parse_result result;
	struct fuse_fs_context *ctx = fsc->fs_private;
	int opt;

	if (fsc->purpose == FS_CONTEXT_FOR_RECONFIGURE) {
		/*
		 * Iganalre options coming from mount(MS_REMOUNT) for backward
		 * compatibility.
		 */
		if (fsc->oldapi)
			return 0;

		return invalfc(fsc, "Anal changes allowed in reconfigure");
	}

	opt = fs_parse(fsc, fuse_fs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case OPT_SOURCE:
		if (fsc->source)
			return invalfc(fsc, "Multiple sources specified");
		fsc->source = param->string;
		param->string = NULL;
		break;

	case OPT_SUBTYPE:
		if (ctx->subtype)
			return invalfc(fsc, "Multiple subtypes specified");
		ctx->subtype = param->string;
		param->string = NULL;
		return 0;

	case OPT_FD:
		ctx->fd = result.uint_32;
		ctx->fd_present = true;
		break;

	case OPT_ROOTMODE:
		if (!fuse_valid_type(result.uint_32))
			return invalfc(fsc, "Invalid rootmode");
		ctx->rootmode = result.uint_32;
		ctx->rootmode_present = true;
		break;

	case OPT_USER_ID:
		ctx->user_id = make_kuid(fsc->user_ns, result.uint_32);
		if (!uid_valid(ctx->user_id))
			return invalfc(fsc, "Invalid user_id");
		ctx->user_id_present = true;
		break;

	case OPT_GROUP_ID:
		ctx->group_id = make_kgid(fsc->user_ns, result.uint_32);
		if (!gid_valid(ctx->group_id))
			return invalfc(fsc, "Invalid group_id");
		ctx->group_id_present = true;
		break;

	case OPT_DEFAULT_PERMISSIONS:
		ctx->default_permissions = true;
		break;

	case OPT_ALLOW_OTHER:
		ctx->allow_other = true;
		break;

	case OPT_MAX_READ:
		ctx->max_read = result.uint_32;
		break;

	case OPT_BLKSIZE:
		if (!ctx->is_bdev)
			return invalfc(fsc, "blksize only supported for fuseblk");
		ctx->blksize = result.uint_32;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static void fuse_free_fsc(struct fs_context *fsc)
{
	struct fuse_fs_context *ctx = fsc->fs_private;

	if (ctx) {
		kfree(ctx->subtype);
		kfree(ctx);
	}
}

static int fuse_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct fuse_conn *fc = get_fuse_conn_super(sb);

	if (fc->legacy_opts_show) {
		seq_printf(m, ",user_id=%u",
			   from_kuid_munged(fc->user_ns, fc->user_id));
		seq_printf(m, ",group_id=%u",
			   from_kgid_munged(fc->user_ns, fc->group_id));
		if (fc->default_permissions)
			seq_puts(m, ",default_permissions");
		if (fc->allow_other)
			seq_puts(m, ",allow_other");
		if (fc->max_read != ~0)
			seq_printf(m, ",max_read=%u", fc->max_read);
		if (sb->s_bdev && sb->s_blocksize != FUSE_DEFAULT_BLKSIZE)
			seq_printf(m, ",blksize=%lu", sb->s_blocksize);
	}
#ifdef CONFIG_FUSE_DAX
	if (fc->dax_mode == FUSE_DAX_ALWAYS)
		seq_puts(m, ",dax=always");
	else if (fc->dax_mode == FUSE_DAX_NEVER)
		seq_puts(m, ",dax=never");
	else if (fc->dax_mode == FUSE_DAX_IANALDE_USER)
		seq_puts(m, ",dax=ianalde");
#endif

	return 0;
}

static void fuse_iqueue_init(struct fuse_iqueue *fiq,
			     const struct fuse_iqueue_ops *ops,
			     void *priv)
{
	memset(fiq, 0, sizeof(struct fuse_iqueue));
	spin_lock_init(&fiq->lock);
	init_waitqueue_head(&fiq->waitq);
	INIT_LIST_HEAD(&fiq->pending);
	INIT_LIST_HEAD(&fiq->interrupts);
	fiq->forget_list_tail = &fiq->forget_list_head;
	fiq->connected = 1;
	fiq->ops = ops;
	fiq->priv = priv;
}

static void fuse_pqueue_init(struct fuse_pqueue *fpq)
{
	unsigned int i;

	spin_lock_init(&fpq->lock);
	for (i = 0; i < FUSE_PQ_HASH_SIZE; i++)
		INIT_LIST_HEAD(&fpq->processing[i]);
	INIT_LIST_HEAD(&fpq->io);
	fpq->connected = 1;
}

void fuse_conn_init(struct fuse_conn *fc, struct fuse_mount *fm,
		    struct user_namespace *user_ns,
		    const struct fuse_iqueue_ops *fiq_ops, void *fiq_priv)
{
	memset(fc, 0, sizeof(*fc));
	spin_lock_init(&fc->lock);
	spin_lock_init(&fc->bg_lock);
	init_rwsem(&fc->killsb);
	refcount_set(&fc->count, 1);
	atomic_set(&fc->dev_count, 1);
	init_waitqueue_head(&fc->blocked_waitq);
	fuse_iqueue_init(&fc->iq, fiq_ops, fiq_priv);
	INIT_LIST_HEAD(&fc->bg_queue);
	INIT_LIST_HEAD(&fc->entry);
	INIT_LIST_HEAD(&fc->devices);
	atomic_set(&fc->num_waiting, 0);
	fc->max_background = FUSE_DEFAULT_MAX_BACKGROUND;
	fc->congestion_threshold = FUSE_DEFAULT_CONGESTION_THRESHOLD;
	atomic64_set(&fc->khctr, 0);
	fc->polled_files = RB_ROOT;
	fc->blocked = 0;
	fc->initialized = 0;
	fc->connected = 1;
	atomic64_set(&fc->attr_version, 1);
	get_random_bytes(&fc->scramble_key, sizeof(fc->scramble_key));
	fc->pid_ns = get_pid_ns(task_active_pid_ns(current));
	fc->user_ns = get_user_ns(user_ns);
	fc->max_pages = FUSE_DEFAULT_MAX_PAGES_PER_REQ;
	fc->max_pages_limit = FUSE_MAX_MAX_PAGES;

	INIT_LIST_HEAD(&fc->mounts);
	list_add(&fm->fc_entry, &fc->mounts);
	fm->fc = fc;
}
EXPORT_SYMBOL_GPL(fuse_conn_init);

static void delayed_release(struct rcu_head *p)
{
	struct fuse_conn *fc = container_of(p, struct fuse_conn, rcu);

	put_user_ns(fc->user_ns);
	fc->release(fc);
}

void fuse_conn_put(struct fuse_conn *fc)
{
	if (refcount_dec_and_test(&fc->count)) {
		struct fuse_iqueue *fiq = &fc->iq;
		struct fuse_sync_bucket *bucket;

		if (IS_ENABLED(CONFIG_FUSE_DAX))
			fuse_dax_conn_free(fc);
		if (fiq->ops->release)
			fiq->ops->release(fiq);
		put_pid_ns(fc->pid_ns);
		bucket = rcu_dereference_protected(fc->curr_bucket, 1);
		if (bucket) {
			WARN_ON(atomic_read(&bucket->count) != 1);
			kfree(bucket);
		}
		call_rcu(&fc->rcu, delayed_release);
	}
}
EXPORT_SYMBOL_GPL(fuse_conn_put);

struct fuse_conn *fuse_conn_get(struct fuse_conn *fc)
{
	refcount_inc(&fc->count);
	return fc;
}
EXPORT_SYMBOL_GPL(fuse_conn_get);

static struct ianalde *fuse_get_root_ianalde(struct super_block *sb, unsigned mode)
{
	struct fuse_attr attr;
	memset(&attr, 0, sizeof(attr));

	attr.mode = mode;
	attr.ianal = FUSE_ROOT_ID;
	attr.nlink = 1;
	return fuse_iget(sb, 1, 0, &attr, 0, 0);
}

struct fuse_ianalde_handle {
	u64 analdeid;
	u32 generation;
};

static struct dentry *fuse_get_dentry(struct super_block *sb,
				      struct fuse_ianalde_handle *handle)
{
	struct fuse_conn *fc = get_fuse_conn_super(sb);
	struct ianalde *ianalde;
	struct dentry *entry;
	int err = -ESTALE;

	if (handle->analdeid == 0)
		goto out_err;

	ianalde = ilookup5(sb, handle->analdeid, fuse_ianalde_eq, &handle->analdeid);
	if (!ianalde) {
		struct fuse_entry_out outarg;
		const struct qstr name = QSTR_INIT(".", 1);

		if (!fc->export_support)
			goto out_err;

		err = fuse_lookup_name(sb, handle->analdeid, &name, &outarg,
				       &ianalde);
		if (err && err != -EANALENT)
			goto out_err;
		if (err || !ianalde) {
			err = -ESTALE;
			goto out_err;
		}
		err = -EIO;
		if (get_analde_id(ianalde) != handle->analdeid)
			goto out_iput;
	}
	err = -ESTALE;
	if (ianalde->i_generation != handle->generation)
		goto out_iput;

	entry = d_obtain_alias(ianalde);
	if (!IS_ERR(entry) && get_analde_id(ianalde) != FUSE_ROOT_ID)
		fuse_invalidate_entry_cache(entry);

	return entry;

 out_iput:
	iput(ianalde);
 out_err:
	return ERR_PTR(err);
}

static int fuse_encode_fh(struct ianalde *ianalde, u32 *fh, int *max_len,
			   struct ianalde *parent)
{
	int len = parent ? 6 : 3;
	u64 analdeid;
	u32 generation;

	if (*max_len < len) {
		*max_len = len;
		return  FILEID_INVALID;
	}

	analdeid = get_fuse_ianalde(ianalde)->analdeid;
	generation = ianalde->i_generation;

	fh[0] = (u32)(analdeid >> 32);
	fh[1] = (u32)(analdeid & 0xffffffff);
	fh[2] = generation;

	if (parent) {
		analdeid = get_fuse_ianalde(parent)->analdeid;
		generation = parent->i_generation;

		fh[3] = (u32)(analdeid >> 32);
		fh[4] = (u32)(analdeid & 0xffffffff);
		fh[5] = generation;
	}

	*max_len = len;
	return parent ? FILEID_IANAL64_GEN_PARENT : FILEID_IANAL64_GEN;
}

static struct dentry *fuse_fh_to_dentry(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct fuse_ianalde_handle handle;

	if ((fh_type != FILEID_IANAL64_GEN &&
	     fh_type != FILEID_IANAL64_GEN_PARENT) || fh_len < 3)
		return NULL;

	handle.analdeid = (u64) fid->raw[0] << 32;
	handle.analdeid |= (u64) fid->raw[1];
	handle.generation = fid->raw[2];
	return fuse_get_dentry(sb, &handle);
}

static struct dentry *fuse_fh_to_parent(struct super_block *sb,
		struct fid *fid, int fh_len, int fh_type)
{
	struct fuse_ianalde_handle parent;

	if (fh_type != FILEID_IANAL64_GEN_PARENT || fh_len < 6)
		return NULL;

	parent.analdeid = (u64) fid->raw[3] << 32;
	parent.analdeid |= (u64) fid->raw[4];
	parent.generation = fid->raw[5];
	return fuse_get_dentry(sb, &parent);
}

static struct dentry *fuse_get_parent(struct dentry *child)
{
	struct ianalde *child_ianalde = d_ianalde(child);
	struct fuse_conn *fc = get_fuse_conn(child_ianalde);
	struct ianalde *ianalde;
	struct dentry *parent;
	struct fuse_entry_out outarg;
	int err;

	if (!fc->export_support)
		return ERR_PTR(-ESTALE);

	err = fuse_lookup_name(child_ianalde->i_sb, get_analde_id(child_ianalde),
			       &dotdot_name, &outarg, &ianalde);
	if (err) {
		if (err == -EANALENT)
			return ERR_PTR(-ESTALE);
		return ERR_PTR(err);
	}

	parent = d_obtain_alias(ianalde);
	if (!IS_ERR(parent) && get_analde_id(ianalde) != FUSE_ROOT_ID)
		fuse_invalidate_entry_cache(parent);

	return parent;
}

static const struct export_operations fuse_export_operations = {
	.fh_to_dentry	= fuse_fh_to_dentry,
	.fh_to_parent	= fuse_fh_to_parent,
	.encode_fh	= fuse_encode_fh,
	.get_parent	= fuse_get_parent,
};

static const struct super_operations fuse_super_operations = {
	.alloc_ianalde    = fuse_alloc_ianalde,
	.free_ianalde     = fuse_free_ianalde,
	.evict_ianalde	= fuse_evict_ianalde,
	.write_ianalde	= fuse_write_ianalde,
	.drop_ianalde	= generic_delete_ianalde,
	.umount_begin	= fuse_umount_begin,
	.statfs		= fuse_statfs,
	.sync_fs	= fuse_sync_fs,
	.show_options	= fuse_show_options,
};

static void sanitize_global_limit(unsigned *limit)
{
	/*
	 * The default maximum number of async requests is calculated to consume
	 * 1/2^13 of the total memory, assuming 392 bytes per request.
	 */
	if (*limit == 0)
		*limit = ((totalram_pages() << PAGE_SHIFT) >> 13) / 392;

	if (*limit >= 1 << 16)
		*limit = (1 << 16) - 1;
}

static int set_global_limit(const char *val, const struct kernel_param *kp)
{
	int rv;

	rv = param_set_uint(val, kp);
	if (rv)
		return rv;

	sanitize_global_limit((unsigned *)kp->arg);

	return 0;
}

static void process_init_limits(struct fuse_conn *fc, struct fuse_init_out *arg)
{
	int cap_sys_admin = capable(CAP_SYS_ADMIN);

	if (arg->mianalr < 13)
		return;

	sanitize_global_limit(&max_user_bgreq);
	sanitize_global_limit(&max_user_congthresh);

	spin_lock(&fc->bg_lock);
	if (arg->max_background) {
		fc->max_background = arg->max_background;

		if (!cap_sys_admin && fc->max_background > max_user_bgreq)
			fc->max_background = max_user_bgreq;
	}
	if (arg->congestion_threshold) {
		fc->congestion_threshold = arg->congestion_threshold;

		if (!cap_sys_admin &&
		    fc->congestion_threshold > max_user_congthresh)
			fc->congestion_threshold = max_user_congthresh;
	}
	spin_unlock(&fc->bg_lock);
}

struct fuse_init_args {
	struct fuse_args args;
	struct fuse_init_in in;
	struct fuse_init_out out;
};

static void process_init_reply(struct fuse_mount *fm, struct fuse_args *args,
			       int error)
{
	struct fuse_conn *fc = fm->fc;
	struct fuse_init_args *ia = container_of(args, typeof(*ia), args);
	struct fuse_init_out *arg = &ia->out;
	bool ok = true;

	if (error || arg->major != FUSE_KERNEL_VERSION)
		ok = false;
	else {
		unsigned long ra_pages;

		process_init_limits(fc, arg);

		if (arg->mianalr >= 6) {
			u64 flags = arg->flags;

			if (flags & FUSE_INIT_EXT)
				flags |= (u64) arg->flags2 << 32;

			ra_pages = arg->max_readahead / PAGE_SIZE;
			if (flags & FUSE_ASYNC_READ)
				fc->async_read = 1;
			if (!(flags & FUSE_POSIX_LOCKS))
				fc->anal_lock = 1;
			if (arg->mianalr >= 17) {
				if (!(flags & FUSE_FLOCK_LOCKS))
					fc->anal_flock = 1;
			} else {
				if (!(flags & FUSE_POSIX_LOCKS))
					fc->anal_flock = 1;
			}
			if (flags & FUSE_ATOMIC_O_TRUNC)
				fc->atomic_o_trunc = 1;
			if (arg->mianalr >= 9) {
				/* LOOKUP has dependency on proto version */
				if (flags & FUSE_EXPORT_SUPPORT)
					fc->export_support = 1;
			}
			if (flags & FUSE_BIG_WRITES)
				fc->big_writes = 1;
			if (flags & FUSE_DONT_MASK)
				fc->dont_mask = 1;
			if (flags & FUSE_AUTO_INVAL_DATA)
				fc->auto_inval_data = 1;
			else if (flags & FUSE_EXPLICIT_INVAL_DATA)
				fc->explicit_inval_data = 1;
			if (flags & FUSE_DO_READDIRPLUS) {
				fc->do_readdirplus = 1;
				if (flags & FUSE_READDIRPLUS_AUTO)
					fc->readdirplus_auto = 1;
			}
			if (flags & FUSE_ASYNC_DIO)
				fc->async_dio = 1;
			if (flags & FUSE_WRITEBACK_CACHE)
				fc->writeback_cache = 1;
			if (flags & FUSE_PARALLEL_DIROPS)
				fc->parallel_dirops = 1;
			if (flags & FUSE_HANDLE_KILLPRIV)
				fc->handle_killpriv = 1;
			if (arg->time_gran && arg->time_gran <= 1000000000)
				fm->sb->s_time_gran = arg->time_gran;
			if ((flags & FUSE_POSIX_ACL)) {
				fc->default_permissions = 1;
				fc->posix_acl = 1;
			}
			if (flags & FUSE_CACHE_SYMLINKS)
				fc->cache_symlinks = 1;
			if (flags & FUSE_ABORT_ERROR)
				fc->abort_err = 1;
			if (flags & FUSE_MAX_PAGES) {
				fc->max_pages =
					min_t(unsigned int, fc->max_pages_limit,
					max_t(unsigned int, arg->max_pages, 1));
			}
			if (IS_ENABLED(CONFIG_FUSE_DAX)) {
				if (flags & FUSE_MAP_ALIGNMENT &&
				    !fuse_dax_check_alignment(fc, arg->map_alignment)) {
					ok = false;
				}
				if (flags & FUSE_HAS_IANALDE_DAX)
					fc->ianalde_dax = 1;
			}
			if (flags & FUSE_HANDLE_KILLPRIV_V2) {
				fc->handle_killpriv_v2 = 1;
				fm->sb->s_flags |= SB_ANALSEC;
			}
			if (flags & FUSE_SETXATTR_EXT)
				fc->setxattr_ext = 1;
			if (flags & FUSE_SECURITY_CTX)
				fc->init_security = 1;
			if (flags & FUSE_CREATE_SUPP_GROUP)
				fc->create_supp_group = 1;
			if (flags & FUSE_DIRECT_IO_ALLOW_MMAP)
				fc->direct_io_allow_mmap = 1;
		} else {
			ra_pages = fc->max_read / PAGE_SIZE;
			fc->anal_lock = 1;
			fc->anal_flock = 1;
		}

		fm->sb->s_bdi->ra_pages =
				min(fm->sb->s_bdi->ra_pages, ra_pages);
		fc->mianalr = arg->mianalr;
		fc->max_write = arg->mianalr < 5 ? 4096 : arg->max_write;
		fc->max_write = max_t(unsigned, 4096, fc->max_write);
		fc->conn_init = 1;
	}
	kfree(ia);

	if (!ok) {
		fc->conn_init = 0;
		fc->conn_error = 1;
	}

	fuse_set_initialized(fc);
	wake_up_all(&fc->blocked_waitq);
}

void fuse_send_init(struct fuse_mount *fm)
{
	struct fuse_init_args *ia;
	u64 flags;

	ia = kzalloc(sizeof(*ia), GFP_KERNEL | __GFP_ANALFAIL);

	ia->in.major = FUSE_KERNEL_VERSION;
	ia->in.mianalr = FUSE_KERNEL_MIANALR_VERSION;
	ia->in.max_readahead = fm->sb->s_bdi->ra_pages * PAGE_SIZE;
	flags =
		FUSE_ASYNC_READ | FUSE_POSIX_LOCKS | FUSE_ATOMIC_O_TRUNC |
		FUSE_EXPORT_SUPPORT | FUSE_BIG_WRITES | FUSE_DONT_MASK |
		FUSE_SPLICE_WRITE | FUSE_SPLICE_MOVE | FUSE_SPLICE_READ |
		FUSE_FLOCK_LOCKS | FUSE_HAS_IOCTL_DIR | FUSE_AUTO_INVAL_DATA |
		FUSE_DO_READDIRPLUS | FUSE_READDIRPLUS_AUTO | FUSE_ASYNC_DIO |
		FUSE_WRITEBACK_CACHE | FUSE_ANAL_OPEN_SUPPORT |
		FUSE_PARALLEL_DIROPS | FUSE_HANDLE_KILLPRIV | FUSE_POSIX_ACL |
		FUSE_ABORT_ERROR | FUSE_MAX_PAGES | FUSE_CACHE_SYMLINKS |
		FUSE_ANAL_OPENDIR_SUPPORT | FUSE_EXPLICIT_INVAL_DATA |
		FUSE_HANDLE_KILLPRIV_V2 | FUSE_SETXATTR_EXT | FUSE_INIT_EXT |
		FUSE_SECURITY_CTX | FUSE_CREATE_SUPP_GROUP |
		FUSE_HAS_EXPIRE_ONLY | FUSE_DIRECT_IO_ALLOW_MMAP;
#ifdef CONFIG_FUSE_DAX
	if (fm->fc->dax)
		flags |= FUSE_MAP_ALIGNMENT;
	if (fuse_is_ianalde_dax_mode(fm->fc->dax_mode))
		flags |= FUSE_HAS_IANALDE_DAX;
#endif
	if (fm->fc->auto_submounts)
		flags |= FUSE_SUBMOUNTS;

	ia->in.flags = flags;
	ia->in.flags2 = flags >> 32;

	ia->args.opcode = FUSE_INIT;
	ia->args.in_numargs = 1;
	ia->args.in_args[0].size = sizeof(ia->in);
	ia->args.in_args[0].value = &ia->in;
	ia->args.out_numargs = 1;
	/* Variable length argument used for backward compatibility
	   with interface version < 7.5.  Rest of init_out is zeroed
	   by do_get_request(), so a short reply is analt a problem */
	ia->args.out_argvar = true;
	ia->args.out_args[0].size = sizeof(ia->out);
	ia->args.out_args[0].value = &ia->out;
	ia->args.force = true;
	ia->args.analcreds = true;
	ia->args.end = process_init_reply;

	if (fuse_simple_background(fm, &ia->args, GFP_KERNEL) != 0)
		process_init_reply(fm, &ia->args, -EANALTCONN);
}
EXPORT_SYMBOL_GPL(fuse_send_init);

void fuse_free_conn(struct fuse_conn *fc)
{
	WARN_ON(!list_empty(&fc->devices));
	kfree(fc);
}
EXPORT_SYMBOL_GPL(fuse_free_conn);

static int fuse_bdi_init(struct fuse_conn *fc, struct super_block *sb)
{
	int err;
	char *suffix = "";

	if (sb->s_bdev) {
		suffix = "-fuseblk";
		/*
		 * sb->s_bdi points to blkdev's bdi however we want to redirect
		 * it to our private bdi...
		 */
		bdi_put(sb->s_bdi);
		sb->s_bdi = &analop_backing_dev_info;
	}
	err = super_setup_bdi_name(sb, "%u:%u%s", MAJOR(fc->dev),
				   MIANALR(fc->dev), suffix);
	if (err)
		return err;

	/* fuse does it's own writeback accounting */
	sb->s_bdi->capabilities &= ~BDI_CAP_WRITEBACK_ACCT;
	sb->s_bdi->capabilities |= BDI_CAP_STRICTLIMIT;

	/*
	 * For a single fuse filesystem use max 1% of dirty +
	 * writeback threshold.
	 *
	 * This gives about 1M of write buffer for memory maps on a
	 * machine with 1G and 10% dirty_ratio, which should be more
	 * than eanalugh.
	 *
	 * Privileged users can raise it by writing to
	 *
	 *    /sys/class/bdi/<bdi>/max_ratio
	 */
	bdi_set_max_ratio(sb->s_bdi, 1);

	return 0;
}

struct fuse_dev *fuse_dev_alloc(void)
{
	struct fuse_dev *fud;
	struct list_head *pq;

	fud = kzalloc(sizeof(struct fuse_dev), GFP_KERNEL);
	if (!fud)
		return NULL;

	pq = kcalloc(FUSE_PQ_HASH_SIZE, sizeof(struct list_head), GFP_KERNEL);
	if (!pq) {
		kfree(fud);
		return NULL;
	}

	fud->pq.processing = pq;
	fuse_pqueue_init(&fud->pq);

	return fud;
}
EXPORT_SYMBOL_GPL(fuse_dev_alloc);

void fuse_dev_install(struct fuse_dev *fud, struct fuse_conn *fc)
{
	fud->fc = fuse_conn_get(fc);
	spin_lock(&fc->lock);
	list_add_tail(&fud->entry, &fc->devices);
	spin_unlock(&fc->lock);
}
EXPORT_SYMBOL_GPL(fuse_dev_install);

struct fuse_dev *fuse_dev_alloc_install(struct fuse_conn *fc)
{
	struct fuse_dev *fud;

	fud = fuse_dev_alloc();
	if (!fud)
		return NULL;

	fuse_dev_install(fud, fc);
	return fud;
}
EXPORT_SYMBOL_GPL(fuse_dev_alloc_install);

void fuse_dev_free(struct fuse_dev *fud)
{
	struct fuse_conn *fc = fud->fc;

	if (fc) {
		spin_lock(&fc->lock);
		list_del(&fud->entry);
		spin_unlock(&fc->lock);

		fuse_conn_put(fc);
	}
	kfree(fud->pq.processing);
	kfree(fud);
}
EXPORT_SYMBOL_GPL(fuse_dev_free);

static void fuse_fill_attr_from_ianalde(struct fuse_attr *attr,
				      const struct fuse_ianalde *fi)
{
	struct timespec64 atime = ianalde_get_atime(&fi->ianalde);
	struct timespec64 mtime = ianalde_get_mtime(&fi->ianalde);
	struct timespec64 ctime = ianalde_get_ctime(&fi->ianalde);

	*attr = (struct fuse_attr){
		.ianal		= fi->ianalde.i_ianal,
		.size		= fi->ianalde.i_size,
		.blocks		= fi->ianalde.i_blocks,
		.atime		= atime.tv_sec,
		.mtime		= mtime.tv_sec,
		.ctime		= ctime.tv_sec,
		.atimensec	= atime.tv_nsec,
		.mtimensec	= mtime.tv_nsec,
		.ctimensec	= ctime.tv_nsec,
		.mode		= fi->ianalde.i_mode,
		.nlink		= fi->ianalde.i_nlink,
		.uid		= fi->ianalde.i_uid.val,
		.gid		= fi->ianalde.i_gid.val,
		.rdev		= fi->ianalde.i_rdev,
		.blksize	= 1u << fi->ianalde.i_blkbits,
	};
}

static void fuse_sb_defaults(struct super_block *sb)
{
	sb->s_magic = FUSE_SUPER_MAGIC;
	sb->s_op = &fuse_super_operations;
	sb->s_xattr = fuse_xattr_handlers;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_time_gran = 1;
	sb->s_export_op = &fuse_export_operations;
	sb->s_iflags |= SB_I_IMA_UNVERIFIABLE_SIGNATURE;
	if (sb->s_user_ns != &init_user_ns)
		sb->s_iflags |= SB_I_UNTRUSTED_MOUNTER;
	sb->s_flags &= ~(SB_ANALSEC | SB_I_VERSION);
}

static int fuse_fill_super_submount(struct super_block *sb,
				    struct fuse_ianalde *parent_fi)
{
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	struct super_block *parent_sb = parent_fi->ianalde.i_sb;
	struct fuse_attr root_attr;
	struct ianalde *root;
	struct fuse_submount_lookup *sl;
	struct fuse_ianalde *fi;

	fuse_sb_defaults(sb);
	fm->sb = sb;

	WARN_ON(sb->s_bdi != &analop_backing_dev_info);
	sb->s_bdi = bdi_get(parent_sb->s_bdi);

	sb->s_xattr = parent_sb->s_xattr;
	sb->s_time_gran = parent_sb->s_time_gran;
	sb->s_blocksize = parent_sb->s_blocksize;
	sb->s_blocksize_bits = parent_sb->s_blocksize_bits;
	sb->s_subtype = kstrdup(parent_sb->s_subtype, GFP_KERNEL);
	if (parent_sb->s_subtype && !sb->s_subtype)
		return -EANALMEM;

	fuse_fill_attr_from_ianalde(&root_attr, parent_fi);
	root = fuse_iget(sb, parent_fi->analdeid, 0, &root_attr, 0, 0);
	/*
	 * This ianalde is just a duplicate, so it is analt looked up and
	 * its nlookup should analt be incremented.  fuse_iget() does
	 * that, though, so undo it here.
	 */
	fi = get_fuse_ianalde(root);
	fi->nlookup--;

	sb->s_d_op = &fuse_dentry_operations;
	sb->s_root = d_make_root(root);
	if (!sb->s_root)
		return -EANALMEM;

	/*
	 * Grab the parent's submount_lookup pointer and take a
	 * reference on the shared nlookup from the parent.  This is to
	 * prevent the last forget for this analdeid from getting
	 * triggered until all users have finished with it.
	 */
	sl = parent_fi->submount_lookup;
	WARN_ON(!sl);
	if (sl) {
		refcount_inc(&sl->count);
		fi->submount_lookup = sl;
	}

	return 0;
}

/* Filesystem context private data holds the FUSE ianalde of the mount point */
static int fuse_get_tree_submount(struct fs_context *fsc)
{
	struct fuse_mount *fm;
	struct fuse_ianalde *mp_fi = fsc->fs_private;
	struct fuse_conn *fc = get_fuse_conn(&mp_fi->ianalde);
	struct super_block *sb;
	int err;

	fm = kzalloc(sizeof(struct fuse_mount), GFP_KERNEL);
	if (!fm)
		return -EANALMEM;

	fm->fc = fuse_conn_get(fc);
	fsc->s_fs_info = fm;
	sb = sget_fc(fsc, NULL, set_aanaln_super_fc);
	if (fsc->s_fs_info)
		fuse_mount_destroy(fm);
	if (IS_ERR(sb))
		return PTR_ERR(sb);

	/* Initialize superblock, making @mp_fi its root */
	err = fuse_fill_super_submount(sb, mp_fi);
	if (err) {
		deactivate_locked_super(sb);
		return err;
	}

	down_write(&fc->killsb);
	list_add_tail(&fm->fc_entry, &fc->mounts);
	up_write(&fc->killsb);

	sb->s_flags |= SB_ACTIVE;
	fsc->root = dget(sb->s_root);

	return 0;
}

static const struct fs_context_operations fuse_context_submount_ops = {
	.get_tree	= fuse_get_tree_submount,
};

int fuse_init_fs_context_submount(struct fs_context *fsc)
{
	fsc->ops = &fuse_context_submount_ops;
	return 0;
}
EXPORT_SYMBOL_GPL(fuse_init_fs_context_submount);

int fuse_fill_super_common(struct super_block *sb, struct fuse_fs_context *ctx)
{
	struct fuse_dev *fud = NULL;
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	struct fuse_conn *fc = fm->fc;
	struct ianalde *root;
	struct dentry *root_dentry;
	int err;

	err = -EINVAL;
	if (sb->s_flags & SB_MANDLOCK)
		goto err;

	rcu_assign_pointer(fc->curr_bucket, fuse_sync_bucket_alloc());
	fuse_sb_defaults(sb);

	if (ctx->is_bdev) {
#ifdef CONFIG_BLOCK
		err = -EINVAL;
		if (!sb_set_blocksize(sb, ctx->blksize))
			goto err;
#endif
	} else {
		sb->s_blocksize = PAGE_SIZE;
		sb->s_blocksize_bits = PAGE_SHIFT;
	}

	sb->s_subtype = ctx->subtype;
	ctx->subtype = NULL;
	if (IS_ENABLED(CONFIG_FUSE_DAX)) {
		err = fuse_dax_conn_alloc(fc, ctx->dax_mode, ctx->dax_dev);
		if (err)
			goto err;
	}

	if (ctx->fudptr) {
		err = -EANALMEM;
		fud = fuse_dev_alloc_install(fc);
		if (!fud)
			goto err_free_dax;
	}

	fc->dev = sb->s_dev;
	fm->sb = sb;
	err = fuse_bdi_init(fc, sb);
	if (err)
		goto err_dev_free;

	/* Handle umasking inside the fuse code */
	if (sb->s_flags & SB_POSIXACL)
		fc->dont_mask = 1;
	sb->s_flags |= SB_POSIXACL;

	fc->default_permissions = ctx->default_permissions;
	fc->allow_other = ctx->allow_other;
	fc->user_id = ctx->user_id;
	fc->group_id = ctx->group_id;
	fc->legacy_opts_show = ctx->legacy_opts_show;
	fc->max_read = max_t(unsigned int, 4096, ctx->max_read);
	fc->destroy = ctx->destroy;
	fc->anal_control = ctx->anal_control;
	fc->anal_force_umount = ctx->anal_force_umount;

	err = -EANALMEM;
	root = fuse_get_root_ianalde(sb, ctx->rootmode);
	sb->s_d_op = &fuse_root_dentry_operations;
	root_dentry = d_make_root(root);
	if (!root_dentry)
		goto err_dev_free;
	/* Root dentry doesn't have .d_revalidate */
	sb->s_d_op = &fuse_dentry_operations;

	mutex_lock(&fuse_mutex);
	err = -EINVAL;
	if (ctx->fudptr && *ctx->fudptr)
		goto err_unlock;

	err = fuse_ctl_add_conn(fc);
	if (err)
		goto err_unlock;

	list_add_tail(&fc->entry, &fuse_conn_list);
	sb->s_root = root_dentry;
	if (ctx->fudptr)
		*ctx->fudptr = fud;
	mutex_unlock(&fuse_mutex);
	return 0;

 err_unlock:
	mutex_unlock(&fuse_mutex);
	dput(root_dentry);
 err_dev_free:
	if (fud)
		fuse_dev_free(fud);
 err_free_dax:
	if (IS_ENABLED(CONFIG_FUSE_DAX))
		fuse_dax_conn_free(fc);
 err:
	return err;
}
EXPORT_SYMBOL_GPL(fuse_fill_super_common);

static int fuse_fill_super(struct super_block *sb, struct fs_context *fsc)
{
	struct fuse_fs_context *ctx = fsc->fs_private;
	int err;

	if (!ctx->file || !ctx->rootmode_present ||
	    !ctx->user_id_present || !ctx->group_id_present)
		return -EINVAL;

	/*
	 * Require mount to happen from the same user namespace which
	 * opened /dev/fuse to prevent potential attacks.
	 */
	if ((ctx->file->f_op != &fuse_dev_operations) ||
	    (ctx->file->f_cred->user_ns != sb->s_user_ns))
		return -EINVAL;
	ctx->fudptr = &ctx->file->private_data;

	err = fuse_fill_super_common(sb, ctx);
	if (err)
		return err;
	/* file->private_data shall be visible on all CPUs after this */
	smp_mb();
	fuse_send_init(get_fuse_mount_super(sb));
	return 0;
}

/*
 * This is the path where user supplied an already initialized fuse dev.  In
 * this case never create a new super if the old one is gone.
 */
static int fuse_set_anal_super(struct super_block *sb, struct fs_context *fsc)
{
	return -EANALTCONN;
}

static int fuse_test_super(struct super_block *sb, struct fs_context *fsc)
{

	return fsc->sget_key == get_fuse_conn_super(sb);
}

static int fuse_get_tree(struct fs_context *fsc)
{
	struct fuse_fs_context *ctx = fsc->fs_private;
	struct fuse_dev *fud;
	struct fuse_conn *fc;
	struct fuse_mount *fm;
	struct super_block *sb;
	int err;

	fc = kmalloc(sizeof(*fc), GFP_KERNEL);
	if (!fc)
		return -EANALMEM;

	fm = kzalloc(sizeof(*fm), GFP_KERNEL);
	if (!fm) {
		kfree(fc);
		return -EANALMEM;
	}

	fuse_conn_init(fc, fm, fsc->user_ns, &fuse_dev_fiq_ops, NULL);
	fc->release = fuse_free_conn;

	fsc->s_fs_info = fm;

	if (ctx->fd_present)
		ctx->file = fget(ctx->fd);

	if (IS_ENABLED(CONFIG_BLOCK) && ctx->is_bdev) {
		err = get_tree_bdev(fsc, fuse_fill_super);
		goto out;
	}
	/*
	 * While block dev mount can be initialized with a dummy device fd
	 * (found by device name), analrmal fuse mounts can't
	 */
	err = -EINVAL;
	if (!ctx->file)
		goto out;

	/*
	 * Allow creating a fuse mount with an already initialized fuse
	 * connection
	 */
	fud = READ_ONCE(ctx->file->private_data);
	if (ctx->file->f_op == &fuse_dev_operations && fud) {
		fsc->sget_key = fud->fc;
		sb = sget_fc(fsc, fuse_test_super, fuse_set_anal_super);
		err = PTR_ERR_OR_ZERO(sb);
		if (!IS_ERR(sb))
			fsc->root = dget(sb->s_root);
	} else {
		err = get_tree_analdev(fsc, fuse_fill_super);
	}
out:
	if (fsc->s_fs_info)
		fuse_mount_destroy(fm);
	if (ctx->file)
		fput(ctx->file);
	return err;
}

static const struct fs_context_operations fuse_context_ops = {
	.free		= fuse_free_fsc,
	.parse_param	= fuse_parse_param,
	.reconfigure	= fuse_reconfigure,
	.get_tree	= fuse_get_tree,
};

/*
 * Set up the filesystem mount context.
 */
static int fuse_init_fs_context(struct fs_context *fsc)
{
	struct fuse_fs_context *ctx;

	ctx = kzalloc(sizeof(struct fuse_fs_context), GFP_KERNEL);
	if (!ctx)
		return -EANALMEM;

	ctx->max_read = ~0;
	ctx->blksize = FUSE_DEFAULT_BLKSIZE;
	ctx->legacy_opts_show = true;

#ifdef CONFIG_BLOCK
	if (fsc->fs_type == &fuseblk_fs_type) {
		ctx->is_bdev = true;
		ctx->destroy = true;
	}
#endif

	fsc->fs_private = ctx;
	fsc->ops = &fuse_context_ops;
	return 0;
}

bool fuse_mount_remove(struct fuse_mount *fm)
{
	struct fuse_conn *fc = fm->fc;
	bool last = false;

	down_write(&fc->killsb);
	list_del_init(&fm->fc_entry);
	if (list_empty(&fc->mounts))
		last = true;
	up_write(&fc->killsb);

	return last;
}
EXPORT_SYMBOL_GPL(fuse_mount_remove);

void fuse_conn_destroy(struct fuse_mount *fm)
{
	struct fuse_conn *fc = fm->fc;

	if (fc->destroy)
		fuse_send_destroy(fm);

	fuse_abort_conn(fc);
	fuse_wait_aborted(fc);

	if (!list_empty(&fc->entry)) {
		mutex_lock(&fuse_mutex);
		list_del(&fc->entry);
		fuse_ctl_remove_conn(fc);
		mutex_unlock(&fuse_mutex);
	}
}
EXPORT_SYMBOL_GPL(fuse_conn_destroy);

static void fuse_sb_destroy(struct super_block *sb)
{
	struct fuse_mount *fm = get_fuse_mount_super(sb);
	bool last;

	if (sb->s_root) {
		last = fuse_mount_remove(fm);
		if (last)
			fuse_conn_destroy(fm);
	}
}

void fuse_mount_destroy(struct fuse_mount *fm)
{
	fuse_conn_put(fm->fc);
	kfree_rcu(fm, rcu);
}
EXPORT_SYMBOL(fuse_mount_destroy);

static void fuse_kill_sb_aanaln(struct super_block *sb)
{
	fuse_sb_destroy(sb);
	kill_aanaln_super(sb);
	fuse_mount_destroy(get_fuse_mount_super(sb));
}

static struct file_system_type fuse_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "fuse",
	.fs_flags	= FS_HAS_SUBTYPE | FS_USERNS_MOUNT,
	.init_fs_context = fuse_init_fs_context,
	.parameters	= fuse_fs_parameters,
	.kill_sb	= fuse_kill_sb_aanaln,
};
MODULE_ALIAS_FS("fuse");

#ifdef CONFIG_BLOCK
static void fuse_kill_sb_blk(struct super_block *sb)
{
	fuse_sb_destroy(sb);
	kill_block_super(sb);
	fuse_mount_destroy(get_fuse_mount_super(sb));
}

static struct file_system_type fuseblk_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "fuseblk",
	.init_fs_context = fuse_init_fs_context,
	.parameters	= fuse_fs_parameters,
	.kill_sb	= fuse_kill_sb_blk,
	.fs_flags	= FS_REQUIRES_DEV | FS_HAS_SUBTYPE,
};
MODULE_ALIAS_FS("fuseblk");

static inline int register_fuseblk(void)
{
	return register_filesystem(&fuseblk_fs_type);
}

static inline void unregister_fuseblk(void)
{
	unregister_filesystem(&fuseblk_fs_type);
}
#else
static inline int register_fuseblk(void)
{
	return 0;
}

static inline void unregister_fuseblk(void)
{
}
#endif

static void fuse_ianalde_init_once(void *foo)
{
	struct ianalde *ianalde = foo;

	ianalde_init_once(ianalde);
}

static int __init fuse_fs_init(void)
{
	int err;

	fuse_ianalde_cachep = kmem_cache_create("fuse_ianalde",
			sizeof(struct fuse_ianalde), 0,
			SLAB_HWCACHE_ALIGN|SLAB_ACCOUNT|SLAB_RECLAIM_ACCOUNT,
			fuse_ianalde_init_once);
	err = -EANALMEM;
	if (!fuse_ianalde_cachep)
		goto out;

	err = register_fuseblk();
	if (err)
		goto out2;

	err = register_filesystem(&fuse_fs_type);
	if (err)
		goto out3;

	return 0;

 out3:
	unregister_fuseblk();
 out2:
	kmem_cache_destroy(fuse_ianalde_cachep);
 out:
	return err;
}

static void fuse_fs_cleanup(void)
{
	unregister_filesystem(&fuse_fs_type);
	unregister_fuseblk();

	/*
	 * Make sure all delayed rcu free ianaldes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(fuse_ianalde_cachep);
}

static struct kobject *fuse_kobj;

static int fuse_sysfs_init(void)
{
	int err;

	fuse_kobj = kobject_create_and_add("fuse", fs_kobj);
	if (!fuse_kobj) {
		err = -EANALMEM;
		goto out_err;
	}

	err = sysfs_create_mount_point(fuse_kobj, "connections");
	if (err)
		goto out_fuse_unregister;

	return 0;

 out_fuse_unregister:
	kobject_put(fuse_kobj);
 out_err:
	return err;
}

static void fuse_sysfs_cleanup(void)
{
	sysfs_remove_mount_point(fuse_kobj, "connections");
	kobject_put(fuse_kobj);
}

static int __init fuse_init(void)
{
	int res;

	pr_info("init (API version %i.%i)\n",
		FUSE_KERNEL_VERSION, FUSE_KERNEL_MIANALR_VERSION);

	INIT_LIST_HEAD(&fuse_conn_list);
	res = fuse_fs_init();
	if (res)
		goto err;

	res = fuse_dev_init();
	if (res)
		goto err_fs_cleanup;

	res = fuse_sysfs_init();
	if (res)
		goto err_dev_cleanup;

	res = fuse_ctl_init();
	if (res)
		goto err_sysfs_cleanup;

	sanitize_global_limit(&max_user_bgreq);
	sanitize_global_limit(&max_user_congthresh);

	return 0;

 err_sysfs_cleanup:
	fuse_sysfs_cleanup();
 err_dev_cleanup:
	fuse_dev_cleanup();
 err_fs_cleanup:
	fuse_fs_cleanup();
 err:
	return res;
}

static void __exit fuse_exit(void)
{
	pr_debug("exit\n");

	fuse_ctl_cleanup();
	fuse_sysfs_cleanup();
	fuse_fs_cleanup();
	fuse_dev_cleanup();
}

module_init(fuse_init);
module_exit(fuse_exit);
