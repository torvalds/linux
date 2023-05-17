// SPDX-License-Identifier: GPL-2.0
/*
 * Implementation of the diskquota system for the LINUX operating system. QUOTA
 * is implemented using the BSD system call interface as the means of
 * communication with the user level. This file contains the generic routines
 * called by the different filesystems on allocation of an inode or block.
 * These routines take care of the administration needed to have a consistent
 * diskquota tracking system. The ideas of both user and group quotas are based
 * on the Melbourne quota system as used on BSD derived systems. The internal
 * implementation is based on one of the several variants of the LINUX
 * inode-subsystem with added complexity of the diskquota system.
 *
 * Author:	Marco van Wieringen <mvw@planets.elm.net>
 *
 * Fixes:   Dmitry Gorodchanin <pgmdsg@ibi.com>, 11 Feb 96
 *
 *		Revised list management to avoid races
 *		-- Bill Hawes, <whawes@star.net>, 9/98
 *
 *		Fixed races in dquot_transfer(), dqget() and dquot_alloc_...().
 *		As the consequence the locking was moved from dquot_decr_...(),
 *		dquot_incr_...() to calling functions.
 *		invalidate_dquots() now writes modified dquots.
 *		Serialized quota_off() and quota_on() for mount point.
 *		Fixed a few bugs in grow_dquots().
 *		Fixed deadlock in write_dquot() - we no longer account quotas on
 *		quota files
 *		remove_dquot_ref() moved to inode.c - it now traverses through inodes
 *		add_dquot_ref() restarts after blocking
 *		Added check for bogus uid and fixed check for group in quotactl.
 *		Jan Kara, <jack@suse.cz>, sponsored by SuSE CR, 10-11/99
 *
 *		Used struct list_head instead of own list struct
 *		Invalidation of referenced dquots is no longer possible
 *		Improved free_dquots list management
 *		Quota and i_blocks are now updated in one place to avoid races
 *		Warnings are now delayed so we won't block in critical section
 *		Write updated not to require dquot lock
 *		Jan Kara, <jack@suse.cz>, 9/2000
 *
 *		Added dynamic quota structure allocation
 *		Jan Kara <jack@suse.cz> 12/2000
 *
 *		Rewritten quota interface. Implemented new quota format and
 *		formats registering.
 *		Jan Kara, <jack@suse.cz>, 2001,2002
 *
 *		New SMP locking.
 *		Jan Kara, <jack@suse.cz>, 10/2002
 *
 *		Added journalled quota support, fix lock inversion problems
 *		Jan Kara, <jack@suse.cz>, 2003,2004
 *
 * (C) Copyright 1994 - 1997 Marco van Wieringen
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/sysctl.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/security.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <linux/kmod.h>
#include <linux/namei.h>
#include <linux/capability.h>
#include <linux/quotaops.h>
#include <linux/blkdev.h>
#include <linux/sched/mm.h>
#include "../internal.h" /* ugh */

#include <linux/uaccess.h>

/*
 * There are five quota SMP locks:
 * * dq_list_lock protects all lists with quotas and quota formats.
 * * dquot->dq_dqb_lock protects data from dq_dqb
 * * inode->i_lock protects inode->i_blocks, i_bytes and also guards
 *   consistency of dquot->dq_dqb with inode->i_blocks, i_bytes so that
 *   dquot_transfer() can stabilize amount it transfers
 * * dq_data_lock protects mem_dqinfo structures and modifications of dquot
 *   pointers in the inode
 * * dq_state_lock protects modifications of quota state (on quotaon and
 *   quotaoff) and readers who care about latest values take it as well.
 *
 * The spinlock ordering is hence:
 *   dq_data_lock > dq_list_lock > i_lock > dquot->dq_dqb_lock,
 *   dq_list_lock > dq_state_lock
 *
 * Note that some things (eg. sb pointer, type, id) doesn't change during
 * the life of the dquot structure and so needn't to be protected by a lock
 *
 * Operation accessing dquots via inode pointers are protected by dquot_srcu.
 * Operation of reading pointer needs srcu_read_lock(&dquot_srcu), and
 * synchronize_srcu(&dquot_srcu) is called after clearing pointers from
 * inode and before dropping dquot references to avoid use of dquots after
 * they are freed. dq_data_lock is used to serialize the pointer setting and
 * clearing operations.
 * Special care needs to be taken about S_NOQUOTA inode flag (marking that
 * inode is a quota file). Functions adding pointers from inode to dquots have
 * to check this flag under dq_data_lock and then (if S_NOQUOTA is not set) they
 * have to do all pointer modifications before dropping dq_data_lock. This makes
 * sure they cannot race with quotaon which first sets S_NOQUOTA flag and
 * then drops all pointers to dquots from an inode.
 *
 * Each dquot has its dq_lock mutex.  Dquot is locked when it is being read to
 * memory (or space for it is being allocated) on the first dqget(), when it is
 * being written out, and when it is being released on the last dqput(). The
 * allocation and release operations are serialized by the dq_lock and by
 * checking the use count in dquot_release().
 *
 * Lock ordering (including related VFS locks) is the following:
 *   s_umount > i_mutex > journal_lock > dquot->dq_lock > dqio_sem
 */

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(dq_list_lock);
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(dq_state_lock);
__cacheline_aligned_in_smp DEFINE_SPINLOCK(dq_data_lock);
EXPORT_SYMBOL(dq_data_lock);
DEFINE_STATIC_SRCU(dquot_srcu);

static DECLARE_WAIT_QUEUE_HEAD(dquot_ref_wq);

void __quota_error(struct super_block *sb, const char *func,
		   const char *fmt, ...)
{
	if (printk_ratelimit()) {
		va_list args;
		struct va_format vaf;

		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		printk(KERN_ERR "Quota error (device %s): %s: %pV\n",
		       sb->s_id, func, &vaf);

		va_end(args);
	}
}
EXPORT_SYMBOL(__quota_error);

#if defined(CONFIG_QUOTA_DEBUG) || defined(CONFIG_PRINT_QUOTA_WARNING)
static char *quotatypes[] = INITQFNAMES;
#endif
static struct quota_format_type *quota_formats;	/* List of registered formats */
static struct quota_module_name module_names[] = INIT_QUOTA_MODULE_NAMES;

/* SLAB cache for dquot structures */
static struct kmem_cache *dquot_cachep;

int register_quota_format(struct quota_format_type *fmt)
{
	spin_lock(&dq_list_lock);
	fmt->qf_next = quota_formats;
	quota_formats = fmt;
	spin_unlock(&dq_list_lock);
	return 0;
}
EXPORT_SYMBOL(register_quota_format);

void unregister_quota_format(struct quota_format_type *fmt)
{
	struct quota_format_type **actqf;

	spin_lock(&dq_list_lock);
	for (actqf = &quota_formats; *actqf && *actqf != fmt;
	     actqf = &(*actqf)->qf_next)
		;
	if (*actqf)
		*actqf = (*actqf)->qf_next;
	spin_unlock(&dq_list_lock);
}
EXPORT_SYMBOL(unregister_quota_format);

static struct quota_format_type *find_quota_format(int id)
{
	struct quota_format_type *actqf;

	spin_lock(&dq_list_lock);
	for (actqf = quota_formats; actqf && actqf->qf_fmt_id != id;
	     actqf = actqf->qf_next)
		;
	if (!actqf || !try_module_get(actqf->qf_owner)) {
		int qm;

		spin_unlock(&dq_list_lock);

		for (qm = 0; module_names[qm].qm_fmt_id &&
			     module_names[qm].qm_fmt_id != id; qm++)
			;
		if (!module_names[qm].qm_fmt_id ||
		    request_module(module_names[qm].qm_mod_name))
			return NULL;

		spin_lock(&dq_list_lock);
		for (actqf = quota_formats; actqf && actqf->qf_fmt_id != id;
		     actqf = actqf->qf_next)
			;
		if (actqf && !try_module_get(actqf->qf_owner))
			actqf = NULL;
	}
	spin_unlock(&dq_list_lock);
	return actqf;
}

static void put_quota_format(struct quota_format_type *fmt)
{
	module_put(fmt->qf_owner);
}

/*
 * Dquot List Management:
 * The quota code uses four lists for dquot management: the inuse_list,
 * free_dquots, dqi_dirty_list, and dquot_hash[] array. A single dquot
 * structure may be on some of those lists, depending on its current state.
 *
 * All dquots are placed to the end of inuse_list when first created, and this
 * list is used for invalidate operation, which must look at every dquot.
 *
 * Unused dquots (dq_count == 0) are added to the free_dquots list when freed,
 * and this list is searched whenever we need an available dquot.  Dquots are
 * removed from the list as soon as they are used again, and
 * dqstats.free_dquots gives the number of dquots on the list. When
 * dquot is invalidated it's completely released from memory.
 *
 * Dirty dquots are added to the dqi_dirty_list of quota_info when mark
 * dirtied, and this list is searched when writing dirty dquots back to
 * quota file. Note that some filesystems do dirty dquot tracking on their
 * own (e.g. in a journal) and thus don't use dqi_dirty_list.
 *
 * Dquots with a specific identity (device, type and id) are placed on
 * one of the dquot_hash[] hash chains. The provides an efficient search
 * mechanism to locate a specific dquot.
 */

static LIST_HEAD(inuse_list);
static LIST_HEAD(free_dquots);
static unsigned int dq_hash_bits, dq_hash_mask;
static struct hlist_head *dquot_hash;

struct dqstats dqstats;
EXPORT_SYMBOL(dqstats);

static qsize_t inode_get_rsv_space(struct inode *inode);
static qsize_t __inode_get_rsv_space(struct inode *inode);
static int __dquot_initialize(struct inode *inode, int type);

static inline unsigned int
hashfn(const struct super_block *sb, struct kqid qid)
{
	unsigned int id = from_kqid(&init_user_ns, qid);
	int type = qid.type;
	unsigned long tmp;

	tmp = (((unsigned long)sb>>L1_CACHE_SHIFT) ^ id) * (MAXQUOTAS - type);
	return (tmp + (tmp >> dq_hash_bits)) & dq_hash_mask;
}

/*
 * Following list functions expect dq_list_lock to be held
 */
static inline void insert_dquot_hash(struct dquot *dquot)
{
	struct hlist_head *head;
	head = dquot_hash + hashfn(dquot->dq_sb, dquot->dq_id);
	hlist_add_head(&dquot->dq_hash, head);
}

static inline void remove_dquot_hash(struct dquot *dquot)
{
	hlist_del_init(&dquot->dq_hash);
}

static struct dquot *find_dquot(unsigned int hashent, struct super_block *sb,
				struct kqid qid)
{
	struct dquot *dquot;

	hlist_for_each_entry(dquot, dquot_hash+hashent, dq_hash)
		if (dquot->dq_sb == sb && qid_eq(dquot->dq_id, qid))
			return dquot;

	return NULL;
}

/* Add a dquot to the tail of the free list */
static inline void put_dquot_last(struct dquot *dquot)
{
	list_add_tail(&dquot->dq_free, &free_dquots);
	dqstats_inc(DQST_FREE_DQUOTS);
}

static inline void remove_free_dquot(struct dquot *dquot)
{
	if (list_empty(&dquot->dq_free))
		return;
	list_del_init(&dquot->dq_free);
	dqstats_dec(DQST_FREE_DQUOTS);
}

static inline void put_inuse(struct dquot *dquot)
{
	/* We add to the back of inuse list so we don't have to restart
	 * when traversing this list and we block */
	list_add_tail(&dquot->dq_inuse, &inuse_list);
	dqstats_inc(DQST_ALLOC_DQUOTS);
}

static inline void remove_inuse(struct dquot *dquot)
{
	dqstats_dec(DQST_ALLOC_DQUOTS);
	list_del(&dquot->dq_inuse);
}
/*
 * End of list functions needing dq_list_lock
 */

static void wait_on_dquot(struct dquot *dquot)
{
	mutex_lock(&dquot->dq_lock);
	mutex_unlock(&dquot->dq_lock);
}

static inline int dquot_dirty(struct dquot *dquot)
{
	return test_bit(DQ_MOD_B, &dquot->dq_flags);
}

static inline int mark_dquot_dirty(struct dquot *dquot)
{
	return dquot->dq_sb->dq_op->mark_dirty(dquot);
}

/* Mark dquot dirty in atomic manner, and return it's old dirty flag state */
int dquot_mark_dquot_dirty(struct dquot *dquot)
{
	int ret = 1;

	if (!test_bit(DQ_ACTIVE_B, &dquot->dq_flags))
		return 0;

	if (sb_dqopt(dquot->dq_sb)->flags & DQUOT_NOLIST_DIRTY)
		return test_and_set_bit(DQ_MOD_B, &dquot->dq_flags);

	/* If quota is dirty already, we don't have to acquire dq_list_lock */
	if (test_bit(DQ_MOD_B, &dquot->dq_flags))
		return 1;

	spin_lock(&dq_list_lock);
	if (!test_and_set_bit(DQ_MOD_B, &dquot->dq_flags)) {
		list_add(&dquot->dq_dirty, &sb_dqopt(dquot->dq_sb)->
				info[dquot->dq_id.type].dqi_dirty_list);
		ret = 0;
	}
	spin_unlock(&dq_list_lock);
	return ret;
}
EXPORT_SYMBOL(dquot_mark_dquot_dirty);

/* Dirtify all the dquots - this can block when journalling */
static inline int mark_all_dquot_dirty(struct dquot * const *dquot)
{
	int ret, err, cnt;

	ret = err = 0;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquot[cnt])
			/* Even in case of error we have to continue */
			ret = mark_dquot_dirty(dquot[cnt]);
		if (!err)
			err = ret;
	}
	return err;
}

static inline void dqput_all(struct dquot **dquot)
{
	unsigned int cnt;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		dqput(dquot[cnt]);
}

static inline int clear_dquot_dirty(struct dquot *dquot)
{
	if (sb_dqopt(dquot->dq_sb)->flags & DQUOT_NOLIST_DIRTY)
		return test_and_clear_bit(DQ_MOD_B, &dquot->dq_flags);

	spin_lock(&dq_list_lock);
	if (!test_and_clear_bit(DQ_MOD_B, &dquot->dq_flags)) {
		spin_unlock(&dq_list_lock);
		return 0;
	}
	list_del_init(&dquot->dq_dirty);
	spin_unlock(&dq_list_lock);
	return 1;
}

void mark_info_dirty(struct super_block *sb, int type)
{
	spin_lock(&dq_data_lock);
	sb_dqopt(sb)->info[type].dqi_flags |= DQF_INFO_DIRTY;
	spin_unlock(&dq_data_lock);
}
EXPORT_SYMBOL(mark_info_dirty);

/*
 *	Read dquot from disk and alloc space for it
 */

int dquot_acquire(struct dquot *dquot)
{
	int ret = 0, ret2 = 0;
	unsigned int memalloc;
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	mutex_lock(&dquot->dq_lock);
	memalloc = memalloc_nofs_save();
	if (!test_bit(DQ_READ_B, &dquot->dq_flags)) {
		ret = dqopt->ops[dquot->dq_id.type]->read_dqblk(dquot);
		if (ret < 0)
			goto out_iolock;
	}
	/* Make sure flags update is visible after dquot has been filled */
	smp_mb__before_atomic();
	set_bit(DQ_READ_B, &dquot->dq_flags);
	/* Instantiate dquot if needed */
	if (!test_bit(DQ_ACTIVE_B, &dquot->dq_flags) && !dquot->dq_off) {
		ret = dqopt->ops[dquot->dq_id.type]->commit_dqblk(dquot);
		/* Write the info if needed */
		if (info_dirty(&dqopt->info[dquot->dq_id.type])) {
			ret2 = dqopt->ops[dquot->dq_id.type]->write_file_info(
					dquot->dq_sb, dquot->dq_id.type);
		}
		if (ret < 0)
			goto out_iolock;
		if (ret2 < 0) {
			ret = ret2;
			goto out_iolock;
		}
	}
	/*
	 * Make sure flags update is visible after on-disk struct has been
	 * allocated. Paired with smp_rmb() in dqget().
	 */
	smp_mb__before_atomic();
	set_bit(DQ_ACTIVE_B, &dquot->dq_flags);
out_iolock:
	memalloc_nofs_restore(memalloc);
	mutex_unlock(&dquot->dq_lock);
	return ret;
}
EXPORT_SYMBOL(dquot_acquire);

/*
 *	Write dquot to disk
 */
int dquot_commit(struct dquot *dquot)
{
	int ret = 0;
	unsigned int memalloc;
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	mutex_lock(&dquot->dq_lock);
	memalloc = memalloc_nofs_save();
	if (!clear_dquot_dirty(dquot))
		goto out_lock;
	/* Inactive dquot can be only if there was error during read/init
	 * => we have better not writing it */
	if (test_bit(DQ_ACTIVE_B, &dquot->dq_flags))
		ret = dqopt->ops[dquot->dq_id.type]->commit_dqblk(dquot);
	else
		ret = -EIO;
out_lock:
	memalloc_nofs_restore(memalloc);
	mutex_unlock(&dquot->dq_lock);
	return ret;
}
EXPORT_SYMBOL(dquot_commit);

/*
 *	Release dquot
 */
int dquot_release(struct dquot *dquot)
{
	int ret = 0, ret2 = 0;
	unsigned int memalloc;
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	mutex_lock(&dquot->dq_lock);
	memalloc = memalloc_nofs_save();
	/* Check whether we are not racing with some other dqget() */
	if (dquot_is_busy(dquot))
		goto out_dqlock;
	if (dqopt->ops[dquot->dq_id.type]->release_dqblk) {
		ret = dqopt->ops[dquot->dq_id.type]->release_dqblk(dquot);
		/* Write the info */
		if (info_dirty(&dqopt->info[dquot->dq_id.type])) {
			ret2 = dqopt->ops[dquot->dq_id.type]->write_file_info(
						dquot->dq_sb, dquot->dq_id.type);
		}
		if (ret >= 0)
			ret = ret2;
	}
	clear_bit(DQ_ACTIVE_B, &dquot->dq_flags);
out_dqlock:
	memalloc_nofs_restore(memalloc);
	mutex_unlock(&dquot->dq_lock);
	return ret;
}
EXPORT_SYMBOL(dquot_release);

void dquot_destroy(struct dquot *dquot)
{
	kmem_cache_free(dquot_cachep, dquot);
}
EXPORT_SYMBOL(dquot_destroy);

static inline void do_destroy_dquot(struct dquot *dquot)
{
	dquot->dq_sb->dq_op->destroy_dquot(dquot);
}

/* Invalidate all dquots on the list. Note that this function is called after
 * quota is disabled and pointers from inodes removed so there cannot be new
 * quota users. There can still be some users of quotas due to inodes being
 * just deleted or pruned by prune_icache() (those are not attached to any
 * list) or parallel quotactl call. We have to wait for such users.
 */
static void invalidate_dquots(struct super_block *sb, int type)
{
	struct dquot *dquot, *tmp;

restart:
	spin_lock(&dq_list_lock);
	list_for_each_entry_safe(dquot, tmp, &inuse_list, dq_inuse) {
		if (dquot->dq_sb != sb)
			continue;
		if (dquot->dq_id.type != type)
			continue;
		/* Wait for dquot users */
		if (atomic_read(&dquot->dq_count)) {
			dqgrab(dquot);
			spin_unlock(&dq_list_lock);
			/*
			 * Once dqput() wakes us up, we know it's time to free
			 * the dquot.
			 * IMPORTANT: we rely on the fact that there is always
			 * at most one process waiting for dquot to free.
			 * Otherwise dq_count would be > 1 and we would never
			 * wake up.
			 */
			wait_event(dquot_ref_wq,
				   atomic_read(&dquot->dq_count) == 1);
			dqput(dquot);
			/* At this moment dquot() need not exist (it could be
			 * reclaimed by prune_dqcache(). Hence we must
			 * restart. */
			goto restart;
		}
		/*
		 * Quota now has no users and it has been written on last
		 * dqput()
		 */
		remove_dquot_hash(dquot);
		remove_free_dquot(dquot);
		remove_inuse(dquot);
		do_destroy_dquot(dquot);
	}
	spin_unlock(&dq_list_lock);
}

/* Call callback for every active dquot on given filesystem */
int dquot_scan_active(struct super_block *sb,
		      int (*fn)(struct dquot *dquot, unsigned long priv),
		      unsigned long priv)
{
	struct dquot *dquot, *old_dquot = NULL;
	int ret = 0;

	WARN_ON_ONCE(!rwsem_is_locked(&sb->s_umount));

	spin_lock(&dq_list_lock);
	list_for_each_entry(dquot, &inuse_list, dq_inuse) {
		if (!test_bit(DQ_ACTIVE_B, &dquot->dq_flags))
			continue;
		if (dquot->dq_sb != sb)
			continue;
		/* Now we have active dquot so we can just increase use count */
		atomic_inc(&dquot->dq_count);
		spin_unlock(&dq_list_lock);
		dqput(old_dquot);
		old_dquot = dquot;
		/*
		 * ->release_dquot() can be racing with us. Our reference
		 * protects us from new calls to it so just wait for any
		 * outstanding call and recheck the DQ_ACTIVE_B after that.
		 */
		wait_on_dquot(dquot);
		if (test_bit(DQ_ACTIVE_B, &dquot->dq_flags)) {
			ret = fn(dquot, priv);
			if (ret < 0)
				goto out;
		}
		spin_lock(&dq_list_lock);
		/* We are safe to continue now because our dquot could not
		 * be moved out of the inuse list while we hold the reference */
	}
	spin_unlock(&dq_list_lock);
out:
	dqput(old_dquot);
	return ret;
}
EXPORT_SYMBOL(dquot_scan_active);

/* Write all dquot structures to quota files */
int dquot_writeback_dquots(struct super_block *sb, int type)
{
	struct list_head dirty;
	struct dquot *dquot;
	struct quota_info *dqopt = sb_dqopt(sb);
	int cnt;
	int err, ret = 0;

	WARN_ON_ONCE(!rwsem_is_locked(&sb->s_umount));

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_active(sb, cnt))
			continue;
		spin_lock(&dq_list_lock);
		/* Move list away to avoid livelock. */
		list_replace_init(&dqopt->info[cnt].dqi_dirty_list, &dirty);
		while (!list_empty(&dirty)) {
			dquot = list_first_entry(&dirty, struct dquot,
						 dq_dirty);

			WARN_ON(!test_bit(DQ_ACTIVE_B, &dquot->dq_flags));

			/* Now we have active dquot from which someone is
 			 * holding reference so we can safely just increase
			 * use count */
			dqgrab(dquot);
			spin_unlock(&dq_list_lock);
			err = sb->dq_op->write_dquot(dquot);
			if (err) {
				/*
				 * Clear dirty bit anyway to avoid infinite
				 * loop here.
				 */
				clear_dquot_dirty(dquot);
				if (!ret)
					ret = err;
			}
			dqput(dquot);
			spin_lock(&dq_list_lock);
		}
		spin_unlock(&dq_list_lock);
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if ((cnt == type || type == -1) && sb_has_quota_active(sb, cnt)
		    && info_dirty(&dqopt->info[cnt]))
			sb->dq_op->write_info(sb, cnt);
	dqstats_inc(DQST_SYNCS);

	return ret;
}
EXPORT_SYMBOL(dquot_writeback_dquots);

/* Write all dquot structures to disk and make them visible from userspace */
int dquot_quota_sync(struct super_block *sb, int type)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	int cnt;
	int ret;

	ret = dquot_writeback_dquots(sb, type);
	if (ret)
		return ret;
	if (dqopt->flags & DQUOT_QUOTA_SYS_FILE)
		return 0;

	/* This is not very clever (and fast) but currently I don't know about
	 * any other simple way of getting quota data to disk and we must get
	 * them there for userspace to be visible... */
	if (sb->s_op->sync_fs) {
		ret = sb->s_op->sync_fs(sb, 1);
		if (ret)
			return ret;
	}
	ret = sync_blockdev(sb->s_bdev);
	if (ret)
		return ret;

	/*
	 * Now when everything is written we can discard the pagecache so
	 * that userspace sees the changes.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_active(sb, cnt))
			continue;
		inode_lock(dqopt->files[cnt]);
		truncate_inode_pages(&dqopt->files[cnt]->i_data, 0);
		inode_unlock(dqopt->files[cnt]);
	}

	return 0;
}
EXPORT_SYMBOL(dquot_quota_sync);

static unsigned long
dqcache_shrink_scan(struct shrinker *shrink, struct shrink_control *sc)
{
	struct dquot *dquot;
	unsigned long freed = 0;

	spin_lock(&dq_list_lock);
	while (!list_empty(&free_dquots) && sc->nr_to_scan) {
		dquot = list_first_entry(&free_dquots, struct dquot, dq_free);
		remove_dquot_hash(dquot);
		remove_free_dquot(dquot);
		remove_inuse(dquot);
		do_destroy_dquot(dquot);
		sc->nr_to_scan--;
		freed++;
	}
	spin_unlock(&dq_list_lock);
	return freed;
}

static unsigned long
dqcache_shrink_count(struct shrinker *shrink, struct shrink_control *sc)
{
	return vfs_pressure_ratio(
	percpu_counter_read_positive(&dqstats.counter[DQST_FREE_DQUOTS]));
}

static struct shrinker dqcache_shrinker = {
	.count_objects = dqcache_shrink_count,
	.scan_objects = dqcache_shrink_scan,
	.seeks = DEFAULT_SEEKS,
};

/*
 * Put reference to dquot
 */
void dqput(struct dquot *dquot)
{
	int ret;

	if (!dquot)
		return;
#ifdef CONFIG_QUOTA_DEBUG
	if (!atomic_read(&dquot->dq_count)) {
		quota_error(dquot->dq_sb, "trying to free free dquot of %s %d",
			    quotatypes[dquot->dq_id.type],
			    from_kqid(&init_user_ns, dquot->dq_id));
		BUG();
	}
#endif
	dqstats_inc(DQST_DROPS);
we_slept:
	spin_lock(&dq_list_lock);
	if (atomic_read(&dquot->dq_count) > 1) {
		/* We have more than one user... nothing to do */
		atomic_dec(&dquot->dq_count);
		/* Releasing dquot during quotaoff phase? */
		if (!sb_has_quota_active(dquot->dq_sb, dquot->dq_id.type) &&
		    atomic_read(&dquot->dq_count) == 1)
			wake_up(&dquot_ref_wq);
		spin_unlock(&dq_list_lock);
		return;
	}
	/* Need to release dquot? */
	if (dquot_dirty(dquot)) {
		spin_unlock(&dq_list_lock);
		/* Commit dquot before releasing */
		ret = dquot->dq_sb->dq_op->write_dquot(dquot);
		if (ret < 0) {
			quota_error(dquot->dq_sb, "Can't write quota structure"
				    " (error %d). Quota may get out of sync!",
				    ret);
			/*
			 * We clear dirty bit anyway, so that we avoid
			 * infinite loop here
			 */
			clear_dquot_dirty(dquot);
		}
		goto we_slept;
	}
	if (test_bit(DQ_ACTIVE_B, &dquot->dq_flags)) {
		spin_unlock(&dq_list_lock);
		dquot->dq_sb->dq_op->release_dquot(dquot);
		goto we_slept;
	}
	atomic_dec(&dquot->dq_count);
#ifdef CONFIG_QUOTA_DEBUG
	/* sanity check */
	BUG_ON(!list_empty(&dquot->dq_free));
#endif
	put_dquot_last(dquot);
	spin_unlock(&dq_list_lock);
}
EXPORT_SYMBOL(dqput);

struct dquot *dquot_alloc(struct super_block *sb, int type)
{
	return kmem_cache_zalloc(dquot_cachep, GFP_NOFS);
}
EXPORT_SYMBOL(dquot_alloc);

static struct dquot *get_empty_dquot(struct super_block *sb, int type)
{
	struct dquot *dquot;

	dquot = sb->dq_op->alloc_dquot(sb, type);
	if(!dquot)
		return NULL;

	mutex_init(&dquot->dq_lock);
	INIT_LIST_HEAD(&dquot->dq_free);
	INIT_LIST_HEAD(&dquot->dq_inuse);
	INIT_HLIST_NODE(&dquot->dq_hash);
	INIT_LIST_HEAD(&dquot->dq_dirty);
	dquot->dq_sb = sb;
	dquot->dq_id = make_kqid_invalid(type);
	atomic_set(&dquot->dq_count, 1);
	spin_lock_init(&dquot->dq_dqb_lock);

	return dquot;
}

/*
 * Get reference to dquot
 *
 * Locking is slightly tricky here. We are guarded from parallel quotaoff()
 * destroying our dquot by:
 *   a) checking for quota flags under dq_list_lock and
 *   b) getting a reference to dquot before we release dq_list_lock
 */
struct dquot *dqget(struct super_block *sb, struct kqid qid)
{
	unsigned int hashent = hashfn(sb, qid);
	struct dquot *dquot, *empty = NULL;

	if (!qid_has_mapping(sb->s_user_ns, qid))
		return ERR_PTR(-EINVAL);

        if (!sb_has_quota_active(sb, qid.type))
		return ERR_PTR(-ESRCH);
we_slept:
	spin_lock(&dq_list_lock);
	spin_lock(&dq_state_lock);
	if (!sb_has_quota_active(sb, qid.type)) {
		spin_unlock(&dq_state_lock);
		spin_unlock(&dq_list_lock);
		dquot = ERR_PTR(-ESRCH);
		goto out;
	}
	spin_unlock(&dq_state_lock);

	dquot = find_dquot(hashent, sb, qid);
	if (!dquot) {
		if (!empty) {
			spin_unlock(&dq_list_lock);
			empty = get_empty_dquot(sb, qid.type);
			if (!empty)
				schedule();	/* Try to wait for a moment... */
			goto we_slept;
		}
		dquot = empty;
		empty = NULL;
		dquot->dq_id = qid;
		/* all dquots go on the inuse_list */
		put_inuse(dquot);
		/* hash it first so it can be found */
		insert_dquot_hash(dquot);
		spin_unlock(&dq_list_lock);
		dqstats_inc(DQST_LOOKUPS);
	} else {
		if (!atomic_read(&dquot->dq_count))
			remove_free_dquot(dquot);
		atomic_inc(&dquot->dq_count);
		spin_unlock(&dq_list_lock);
		dqstats_inc(DQST_CACHE_HITS);
		dqstats_inc(DQST_LOOKUPS);
	}
	/* Wait for dq_lock - after this we know that either dquot_release() is
	 * already finished or it will be canceled due to dq_count > 1 test */
	wait_on_dquot(dquot);
	/* Read the dquot / allocate space in quota file */
	if (!test_bit(DQ_ACTIVE_B, &dquot->dq_flags)) {
		int err;

		err = sb->dq_op->acquire_dquot(dquot);
		if (err < 0) {
			dqput(dquot);
			dquot = ERR_PTR(err);
			goto out;
		}
	}
	/*
	 * Make sure following reads see filled structure - paired with
	 * smp_mb__before_atomic() in dquot_acquire().
	 */
	smp_rmb();
#ifdef CONFIG_QUOTA_DEBUG
	BUG_ON(!dquot->dq_sb);	/* Has somebody invalidated entry under us? */
#endif
out:
	if (empty)
		do_destroy_dquot(empty);

	return dquot;
}
EXPORT_SYMBOL(dqget);

static inline struct dquot **i_dquot(struct inode *inode)
{
	return inode->i_sb->s_op->get_dquots(inode);
}

static int dqinit_needed(struct inode *inode, int type)
{
	struct dquot * const *dquots;
	int cnt;

	if (IS_NOQUOTA(inode))
		return 0;

	dquots = i_dquot(inode);
	if (type != -1)
		return !dquots[type];
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (!dquots[cnt])
			return 1;
	return 0;
}

/* This routine is guarded by s_umount semaphore */
static int add_dquot_ref(struct super_block *sb, int type)
{
	struct inode *inode, *old_inode = NULL;
#ifdef CONFIG_QUOTA_DEBUG
	int reserved = 0;
#endif
	int err = 0;

	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		spin_lock(&inode->i_lock);
		if ((inode->i_state & (I_FREEING|I_WILL_FREE|I_NEW)) ||
		    !atomic_read(&inode->i_writecount) ||
		    !dqinit_needed(inode, type)) {
			spin_unlock(&inode->i_lock);
			continue;
		}
		__iget(inode);
		spin_unlock(&inode->i_lock);
		spin_unlock(&sb->s_inode_list_lock);

#ifdef CONFIG_QUOTA_DEBUG
		if (unlikely(inode_get_rsv_space(inode) > 0))
			reserved = 1;
#endif
		iput(old_inode);
		err = __dquot_initialize(inode, type);
		if (err) {
			iput(inode);
			goto out;
		}

		/*
		 * We hold a reference to 'inode' so it couldn't have been
		 * removed from s_inodes list while we dropped the
		 * s_inode_list_lock. We cannot iput the inode now as we can be
		 * holding the last reference and we cannot iput it under
		 * s_inode_list_lock. So we keep the reference and iput it
		 * later.
		 */
		old_inode = inode;
		cond_resched();
		spin_lock(&sb->s_inode_list_lock);
	}
	spin_unlock(&sb->s_inode_list_lock);
	iput(old_inode);
out:
#ifdef CONFIG_QUOTA_DEBUG
	if (reserved) {
		quota_error(sb, "Writes happened before quota was turned on "
			"thus quota information is probably inconsistent. "
			"Please run quotacheck(8)");
	}
#endif
	return err;
}

/*
 * Remove references to dquots from inode and add dquot to list for freeing
 * if we have the last reference to dquot
 */
static void remove_inode_dquot_ref(struct inode *inode, int type,
				   struct list_head *tofree_head)
{
	struct dquot **dquots = i_dquot(inode);
	struct dquot *dquot = dquots[type];

	if (!dquot)
		return;

	dquots[type] = NULL;
	if (list_empty(&dquot->dq_free)) {
		/*
		 * The inode still has reference to dquot so it can't be in the
		 * free list
		 */
		spin_lock(&dq_list_lock);
		list_add(&dquot->dq_free, tofree_head);
		spin_unlock(&dq_list_lock);
	} else {
		/*
		 * Dquot is already in a list to put so we won't drop the last
		 * reference here.
		 */
		dqput(dquot);
	}
}

/*
 * Free list of dquots
 * Dquots are removed from inodes and no new references can be got so we are
 * the only ones holding reference
 */
static void put_dquot_list(struct list_head *tofree_head)
{
	struct list_head *act_head;
	struct dquot *dquot;

	act_head = tofree_head->next;
	while (act_head != tofree_head) {
		dquot = list_entry(act_head, struct dquot, dq_free);
		act_head = act_head->next;
		/* Remove dquot from the list so we won't have problems... */
		list_del_init(&dquot->dq_free);
		dqput(dquot);
	}
}

static void remove_dquot_ref(struct super_block *sb, int type,
		struct list_head *tofree_head)
{
	struct inode *inode;
#ifdef CONFIG_QUOTA_DEBUG
	int reserved = 0;
#endif

	spin_lock(&sb->s_inode_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		/*
		 *  We have to scan also I_NEW inodes because they can already
		 *  have quota pointer initialized. Luckily, we need to touch
		 *  only quota pointers and these have separate locking
		 *  (dq_data_lock).
		 */
		spin_lock(&dq_data_lock);
		if (!IS_NOQUOTA(inode)) {
#ifdef CONFIG_QUOTA_DEBUG
			if (unlikely(inode_get_rsv_space(inode) > 0))
				reserved = 1;
#endif
			remove_inode_dquot_ref(inode, type, tofree_head);
		}
		spin_unlock(&dq_data_lock);
	}
	spin_unlock(&sb->s_inode_list_lock);
#ifdef CONFIG_QUOTA_DEBUG
	if (reserved) {
		printk(KERN_WARNING "VFS (%s): Writes happened after quota"
			" was disabled thus quota information is probably "
			"inconsistent. Please run quotacheck(8).\n", sb->s_id);
	}
#endif
}

/* Gather all references from inodes and drop them */
static void drop_dquot_ref(struct super_block *sb, int type)
{
	LIST_HEAD(tofree_head);

	if (sb->dq_op) {
		remove_dquot_ref(sb, type, &tofree_head);
		synchronize_srcu(&dquot_srcu);
		put_dquot_list(&tofree_head);
	}
}

static inline
void dquot_free_reserved_space(struct dquot *dquot, qsize_t number)
{
	if (dquot->dq_dqb.dqb_rsvspace >= number)
		dquot->dq_dqb.dqb_rsvspace -= number;
	else {
		WARN_ON_ONCE(1);
		dquot->dq_dqb.dqb_rsvspace = 0;
	}
	if (dquot->dq_dqb.dqb_curspace + dquot->dq_dqb.dqb_rsvspace <=
	    dquot->dq_dqb.dqb_bsoftlimit)
		dquot->dq_dqb.dqb_btime = (time64_t) 0;
	clear_bit(DQ_BLKS_B, &dquot->dq_flags);
}

static void dquot_decr_inodes(struct dquot *dquot, qsize_t number)
{
	if (sb_dqopt(dquot->dq_sb)->flags & DQUOT_NEGATIVE_USAGE ||
	    dquot->dq_dqb.dqb_curinodes >= number)
		dquot->dq_dqb.dqb_curinodes -= number;
	else
		dquot->dq_dqb.dqb_curinodes = 0;
	if (dquot->dq_dqb.dqb_curinodes <= dquot->dq_dqb.dqb_isoftlimit)
		dquot->dq_dqb.dqb_itime = (time64_t) 0;
	clear_bit(DQ_INODES_B, &dquot->dq_flags);
}

static void dquot_decr_space(struct dquot *dquot, qsize_t number)
{
	if (sb_dqopt(dquot->dq_sb)->flags & DQUOT_NEGATIVE_USAGE ||
	    dquot->dq_dqb.dqb_curspace >= number)
		dquot->dq_dqb.dqb_curspace -= number;
	else
		dquot->dq_dqb.dqb_curspace = 0;
	if (dquot->dq_dqb.dqb_curspace + dquot->dq_dqb.dqb_rsvspace <=
	    dquot->dq_dqb.dqb_bsoftlimit)
		dquot->dq_dqb.dqb_btime = (time64_t) 0;
	clear_bit(DQ_BLKS_B, &dquot->dq_flags);
}

struct dquot_warn {
	struct super_block *w_sb;
	struct kqid w_dq_id;
	short w_type;
};

static int warning_issued(struct dquot *dquot, const int warntype)
{
	int flag = (warntype == QUOTA_NL_BHARDWARN ||
		warntype == QUOTA_NL_BSOFTLONGWARN) ? DQ_BLKS_B :
		((warntype == QUOTA_NL_IHARDWARN ||
		warntype == QUOTA_NL_ISOFTLONGWARN) ? DQ_INODES_B : 0);

	if (!flag)
		return 0;
	return test_and_set_bit(flag, &dquot->dq_flags);
}

#ifdef CONFIG_PRINT_QUOTA_WARNING
static int flag_print_warnings = 1;

static int need_print_warning(struct dquot_warn *warn)
{
	if (!flag_print_warnings)
		return 0;

	switch (warn->w_dq_id.type) {
		case USRQUOTA:
			return uid_eq(current_fsuid(), warn->w_dq_id.uid);
		case GRPQUOTA:
			return in_group_p(warn->w_dq_id.gid);
		case PRJQUOTA:
			return 1;
	}
	return 0;
}

/* Print warning to user which exceeded quota */
static void print_warning(struct dquot_warn *warn)
{
	char *msg = NULL;
	struct tty_struct *tty;
	int warntype = warn->w_type;

	if (warntype == QUOTA_NL_IHARDBELOW ||
	    warntype == QUOTA_NL_ISOFTBELOW ||
	    warntype == QUOTA_NL_BHARDBELOW ||
	    warntype == QUOTA_NL_BSOFTBELOW || !need_print_warning(warn))
		return;

	tty = get_current_tty();
	if (!tty)
		return;
	tty_write_message(tty, warn->w_sb->s_id);
	if (warntype == QUOTA_NL_ISOFTWARN || warntype == QUOTA_NL_BSOFTWARN)
		tty_write_message(tty, ": warning, ");
	else
		tty_write_message(tty, ": write failed, ");
	tty_write_message(tty, quotatypes[warn->w_dq_id.type]);
	switch (warntype) {
		case QUOTA_NL_IHARDWARN:
			msg = " file limit reached.\r\n";
			break;
		case QUOTA_NL_ISOFTLONGWARN:
			msg = " file quota exceeded too long.\r\n";
			break;
		case QUOTA_NL_ISOFTWARN:
			msg = " file quota exceeded.\r\n";
			break;
		case QUOTA_NL_BHARDWARN:
			msg = " block limit reached.\r\n";
			break;
		case QUOTA_NL_BSOFTLONGWARN:
			msg = " block quota exceeded too long.\r\n";
			break;
		case QUOTA_NL_BSOFTWARN:
			msg = " block quota exceeded.\r\n";
			break;
	}
	tty_write_message(tty, msg);
	tty_kref_put(tty);
}
#endif

static void prepare_warning(struct dquot_warn *warn, struct dquot *dquot,
			    int warntype)
{
	if (warning_issued(dquot, warntype))
		return;
	warn->w_type = warntype;
	warn->w_sb = dquot->dq_sb;
	warn->w_dq_id = dquot->dq_id;
}

/*
 * Write warnings to the console and send warning messages over netlink.
 *
 * Note that this function can call into tty and networking code.
 */
static void flush_warnings(struct dquot_warn *warn)
{
	int i;

	for (i = 0; i < MAXQUOTAS; i++) {
		if (warn[i].w_type == QUOTA_NL_NOWARN)
			continue;
#ifdef CONFIG_PRINT_QUOTA_WARNING
		print_warning(&warn[i]);
#endif
		quota_send_warning(warn[i].w_dq_id,
				   warn[i].w_sb->s_dev, warn[i].w_type);
	}
}

static int ignore_hardlimit(struct dquot *dquot)
{
	struct mem_dqinfo *info = &sb_dqopt(dquot->dq_sb)->info[dquot->dq_id.type];

	return capable(CAP_SYS_RESOURCE) &&
	       (info->dqi_format->qf_fmt_id != QFMT_VFS_OLD ||
		!(info->dqi_flags & DQF_ROOT_SQUASH));
}

static int dquot_add_inodes(struct dquot *dquot, qsize_t inodes,
			    struct dquot_warn *warn)
{
	qsize_t newinodes;
	int ret = 0;

	spin_lock(&dquot->dq_dqb_lock);
	newinodes = dquot->dq_dqb.dqb_curinodes + inodes;
	if (!sb_has_quota_limits_enabled(dquot->dq_sb, dquot->dq_id.type) ||
	    test_bit(DQ_FAKE_B, &dquot->dq_flags))
		goto add;

	if (dquot->dq_dqb.dqb_ihardlimit &&
	    newinodes > dquot->dq_dqb.dqb_ihardlimit &&
            !ignore_hardlimit(dquot)) {
		prepare_warning(warn, dquot, QUOTA_NL_IHARDWARN);
		ret = -EDQUOT;
		goto out;
	}

	if (dquot->dq_dqb.dqb_isoftlimit &&
	    newinodes > dquot->dq_dqb.dqb_isoftlimit &&
	    dquot->dq_dqb.dqb_itime &&
	    ktime_get_real_seconds() >= dquot->dq_dqb.dqb_itime &&
            !ignore_hardlimit(dquot)) {
		prepare_warning(warn, dquot, QUOTA_NL_ISOFTLONGWARN);
		ret = -EDQUOT;
		goto out;
	}

	if (dquot->dq_dqb.dqb_isoftlimit &&
	    newinodes > dquot->dq_dqb.dqb_isoftlimit &&
	    dquot->dq_dqb.dqb_itime == 0) {
		prepare_warning(warn, dquot, QUOTA_NL_ISOFTWARN);
		dquot->dq_dqb.dqb_itime = ktime_get_real_seconds() +
		    sb_dqopt(dquot->dq_sb)->info[dquot->dq_id.type].dqi_igrace;
	}
add:
	dquot->dq_dqb.dqb_curinodes = newinodes;

out:
	spin_unlock(&dquot->dq_dqb_lock);
	return ret;
}

static int dquot_add_space(struct dquot *dquot, qsize_t space,
			   qsize_t rsv_space, unsigned int flags,
			   struct dquot_warn *warn)
{
	qsize_t tspace;
	struct super_block *sb = dquot->dq_sb;
	int ret = 0;

	spin_lock(&dquot->dq_dqb_lock);
	if (!sb_has_quota_limits_enabled(sb, dquot->dq_id.type) ||
	    test_bit(DQ_FAKE_B, &dquot->dq_flags))
		goto finish;

	tspace = dquot->dq_dqb.dqb_curspace + dquot->dq_dqb.dqb_rsvspace
		+ space + rsv_space;

	if (dquot->dq_dqb.dqb_bhardlimit &&
	    tspace > dquot->dq_dqb.dqb_bhardlimit &&
            !ignore_hardlimit(dquot)) {
		if (flags & DQUOT_SPACE_WARN)
			prepare_warning(warn, dquot, QUOTA_NL_BHARDWARN);
		ret = -EDQUOT;
		goto finish;
	}

	if (dquot->dq_dqb.dqb_bsoftlimit &&
	    tspace > dquot->dq_dqb.dqb_bsoftlimit &&
	    dquot->dq_dqb.dqb_btime &&
	    ktime_get_real_seconds() >= dquot->dq_dqb.dqb_btime &&
            !ignore_hardlimit(dquot)) {
		if (flags & DQUOT_SPACE_WARN)
			prepare_warning(warn, dquot, QUOTA_NL_BSOFTLONGWARN);
		ret = -EDQUOT;
		goto finish;
	}

	if (dquot->dq_dqb.dqb_bsoftlimit &&
	    tspace > dquot->dq_dqb.dqb_bsoftlimit &&
	    dquot->dq_dqb.dqb_btime == 0) {
		if (flags & DQUOT_SPACE_WARN) {
			prepare_warning(warn, dquot, QUOTA_NL_BSOFTWARN);
			dquot->dq_dqb.dqb_btime = ktime_get_real_seconds() +
			    sb_dqopt(sb)->info[dquot->dq_id.type].dqi_bgrace;
		} else {
			/*
			 * We don't allow preallocation to exceed softlimit so exceeding will
			 * be always printed
			 */
			ret = -EDQUOT;
			goto finish;
		}
	}
finish:
	/*
	 * We have to be careful and go through warning generation & grace time
	 * setting even if DQUOT_SPACE_NOFAIL is set. That's why we check it
	 * only here...
	 */
	if (flags & DQUOT_SPACE_NOFAIL)
		ret = 0;
	if (!ret) {
		dquot->dq_dqb.dqb_rsvspace += rsv_space;
		dquot->dq_dqb.dqb_curspace += space;
	}
	spin_unlock(&dquot->dq_dqb_lock);
	return ret;
}

static int info_idq_free(struct dquot *dquot, qsize_t inodes)
{
	qsize_t newinodes;

	if (test_bit(DQ_FAKE_B, &dquot->dq_flags) ||
	    dquot->dq_dqb.dqb_curinodes <= dquot->dq_dqb.dqb_isoftlimit ||
	    !sb_has_quota_limits_enabled(dquot->dq_sb, dquot->dq_id.type))
		return QUOTA_NL_NOWARN;

	newinodes = dquot->dq_dqb.dqb_curinodes - inodes;
	if (newinodes <= dquot->dq_dqb.dqb_isoftlimit)
		return QUOTA_NL_ISOFTBELOW;
	if (dquot->dq_dqb.dqb_curinodes >= dquot->dq_dqb.dqb_ihardlimit &&
	    newinodes < dquot->dq_dqb.dqb_ihardlimit)
		return QUOTA_NL_IHARDBELOW;
	return QUOTA_NL_NOWARN;
}

static int info_bdq_free(struct dquot *dquot, qsize_t space)
{
	qsize_t tspace;

	tspace = dquot->dq_dqb.dqb_curspace + dquot->dq_dqb.dqb_rsvspace;

	if (test_bit(DQ_FAKE_B, &dquot->dq_flags) ||
	    tspace <= dquot->dq_dqb.dqb_bsoftlimit)
		return QUOTA_NL_NOWARN;

	if (tspace - space <= dquot->dq_dqb.dqb_bsoftlimit)
		return QUOTA_NL_BSOFTBELOW;
	if (tspace >= dquot->dq_dqb.dqb_bhardlimit &&
	    tspace - space < dquot->dq_dqb.dqb_bhardlimit)
		return QUOTA_NL_BHARDBELOW;
	return QUOTA_NL_NOWARN;
}

static int dquot_active(const struct inode *inode)
{
	struct super_block *sb = inode->i_sb;

	if (IS_NOQUOTA(inode))
		return 0;
	return sb_any_quota_loaded(sb) & ~sb_any_quota_suspended(sb);
}

/*
 * Initialize quota pointers in inode
 *
 * It is better to call this function outside of any transaction as it
 * might need a lot of space in journal for dquot structure allocation.
 */
static int __dquot_initialize(struct inode *inode, int type)
{
	int cnt, init_needed = 0;
	struct dquot **dquots, *got[MAXQUOTAS] = {};
	struct super_block *sb = inode->i_sb;
	qsize_t rsv;
	int ret = 0;

	if (!dquot_active(inode))
		return 0;

	dquots = i_dquot(inode);

	/* First get references to structures we might need. */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		struct kqid qid;
		kprojid_t projid;
		int rc;
		struct dquot *dquot;

		if (type != -1 && cnt != type)
			continue;
		/*
		 * The i_dquot should have been initialized in most cases,
		 * we check it without locking here to avoid unnecessary
		 * dqget()/dqput() calls.
		 */
		if (dquots[cnt])
			continue;

		if (!sb_has_quota_active(sb, cnt))
			continue;

		init_needed = 1;

		switch (cnt) {
		case USRQUOTA:
			qid = make_kqid_uid(inode->i_uid);
			break;
		case GRPQUOTA:
			qid = make_kqid_gid(inode->i_gid);
			break;
		case PRJQUOTA:
			rc = inode->i_sb->dq_op->get_projid(inode, &projid);
			if (rc)
				continue;
			qid = make_kqid_projid(projid);
			break;
		}
		dquot = dqget(sb, qid);
		if (IS_ERR(dquot)) {
			/* We raced with somebody turning quotas off... */
			if (PTR_ERR(dquot) != -ESRCH) {
				ret = PTR_ERR(dquot);
				goto out_put;
			}
			dquot = NULL;
		}
		got[cnt] = dquot;
	}

	/* All required i_dquot has been initialized */
	if (!init_needed)
		return 0;

	spin_lock(&dq_data_lock);
	if (IS_NOQUOTA(inode))
		goto out_lock;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		/* Avoid races with quotaoff() */
		if (!sb_has_quota_active(sb, cnt))
			continue;
		/* We could race with quotaon or dqget() could have failed */
		if (!got[cnt])
			continue;
		if (!dquots[cnt]) {
			dquots[cnt] = got[cnt];
			got[cnt] = NULL;
			/*
			 * Make quota reservation system happy if someone
			 * did a write before quota was turned on
			 */
			rsv = inode_get_rsv_space(inode);
			if (unlikely(rsv)) {
				spin_lock(&inode->i_lock);
				/* Get reservation again under proper lock */
				rsv = __inode_get_rsv_space(inode);
				spin_lock(&dquots[cnt]->dq_dqb_lock);
				dquots[cnt]->dq_dqb.dqb_rsvspace += rsv;
				spin_unlock(&dquots[cnt]->dq_dqb_lock);
				spin_unlock(&inode->i_lock);
			}
		}
	}
out_lock:
	spin_unlock(&dq_data_lock);
out_put:
	/* Drop unused references */
	dqput_all(got);

	return ret;
}

int dquot_initialize(struct inode *inode)
{
	return __dquot_initialize(inode, -1);
}
EXPORT_SYMBOL(dquot_initialize);

bool dquot_initialize_needed(struct inode *inode)
{
	struct dquot **dquots;
	int i;

	if (!dquot_active(inode))
		return false;

	dquots = i_dquot(inode);
	for (i = 0; i < MAXQUOTAS; i++)
		if (!dquots[i] && sb_has_quota_active(inode->i_sb, i))
			return true;
	return false;
}
EXPORT_SYMBOL(dquot_initialize_needed);

/*
 * Release all quotas referenced by inode.
 *
 * This function only be called on inode free or converting
 * a file to quota file, no other users for the i_dquot in
 * both cases, so we needn't call synchronize_srcu() after
 * clearing i_dquot.
 */
static void __dquot_drop(struct inode *inode)
{
	int cnt;
	struct dquot **dquots = i_dquot(inode);
	struct dquot *put[MAXQUOTAS];

	spin_lock(&dq_data_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		put[cnt] = dquots[cnt];
		dquots[cnt] = NULL;
	}
	spin_unlock(&dq_data_lock);
	dqput_all(put);
}

void dquot_drop(struct inode *inode)
{
	struct dquot * const *dquots;
	int cnt;

	if (IS_NOQUOTA(inode))
		return;

	/*
	 * Test before calling to rule out calls from proc and such
	 * where we are not allowed to block. Note that this is
	 * actually reliable test even without the lock - the caller
	 * must assure that nobody can come after the DQUOT_DROP and
	 * add quota pointers back anyway.
	 */
	dquots = i_dquot(inode);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquots[cnt])
			break;
	}

	if (cnt < MAXQUOTAS)
		__dquot_drop(inode);
}
EXPORT_SYMBOL(dquot_drop);

/*
 * inode_reserved_space is managed internally by quota, and protected by
 * i_lock similar to i_blocks+i_bytes.
 */
static qsize_t *inode_reserved_space(struct inode * inode)
{
	/* Filesystem must explicitly define it's own method in order to use
	 * quota reservation interface */
	BUG_ON(!inode->i_sb->dq_op->get_reserved_space);
	return inode->i_sb->dq_op->get_reserved_space(inode);
}

static qsize_t __inode_get_rsv_space(struct inode *inode)
{
	if (!inode->i_sb->dq_op->get_reserved_space)
		return 0;
	return *inode_reserved_space(inode);
}

static qsize_t inode_get_rsv_space(struct inode *inode)
{
	qsize_t ret;

	if (!inode->i_sb->dq_op->get_reserved_space)
		return 0;
	spin_lock(&inode->i_lock);
	ret = __inode_get_rsv_space(inode);
	spin_unlock(&inode->i_lock);
	return ret;
}

/*
 * This functions updates i_blocks+i_bytes fields and quota information
 * (together with appropriate checks).
 *
 * NOTE: We absolutely rely on the fact that caller dirties the inode
 * (usually helpers in quotaops.h care about this) and holds a handle for
 * the current transaction so that dquot write and inode write go into the
 * same transaction.
 */

/*
 * This operation can block, but only after everything is updated
 */
int __dquot_alloc_space(struct inode *inode, qsize_t number, int flags)
{
	int cnt, ret = 0, index;
	struct dquot_warn warn[MAXQUOTAS];
	int reserve = flags & DQUOT_SPACE_RESERVE;
	struct dquot **dquots;

	if (!dquot_active(inode)) {
		if (reserve) {
			spin_lock(&inode->i_lock);
			*inode_reserved_space(inode) += number;
			spin_unlock(&inode->i_lock);
		} else {
			inode_add_bytes(inode, number);
		}
		goto out;
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		warn[cnt].w_type = QUOTA_NL_NOWARN;

	dquots = i_dquot(inode);
	index = srcu_read_lock(&dquot_srcu);
	spin_lock(&inode->i_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!dquots[cnt])
			continue;
		if (reserve) {
			ret = dquot_add_space(dquots[cnt], 0, number, flags,
					      &warn[cnt]);
		} else {
			ret = dquot_add_space(dquots[cnt], number, 0, flags,
					      &warn[cnt]);
		}
		if (ret) {
			/* Back out changes we already did */
			for (cnt--; cnt >= 0; cnt--) {
				if (!dquots[cnt])
					continue;
				spin_lock(&dquots[cnt]->dq_dqb_lock);
				if (reserve)
					dquot_free_reserved_space(dquots[cnt],
								  number);
				else
					dquot_decr_space(dquots[cnt], number);
				spin_unlock(&dquots[cnt]->dq_dqb_lock);
			}
			spin_unlock(&inode->i_lock);
			goto out_flush_warn;
		}
	}
	if (reserve)
		*inode_reserved_space(inode) += number;
	else
		__inode_add_bytes(inode, number);
	spin_unlock(&inode->i_lock);

	if (reserve)
		goto out_flush_warn;
	mark_all_dquot_dirty(dquots);
out_flush_warn:
	srcu_read_unlock(&dquot_srcu, index);
	flush_warnings(warn);
out:
	return ret;
}
EXPORT_SYMBOL(__dquot_alloc_space);

/*
 * This operation can block, but only after everything is updated
 */
int dquot_alloc_inode(struct inode *inode)
{
	int cnt, ret = 0, index;
	struct dquot_warn warn[MAXQUOTAS];
	struct dquot * const *dquots;

	if (!dquot_active(inode))
		return 0;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		warn[cnt].w_type = QUOTA_NL_NOWARN;

	dquots = i_dquot(inode);
	index = srcu_read_lock(&dquot_srcu);
	spin_lock(&inode->i_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!dquots[cnt])
			continue;
		ret = dquot_add_inodes(dquots[cnt], 1, &warn[cnt]);
		if (ret) {
			for (cnt--; cnt >= 0; cnt--) {
				if (!dquots[cnt])
					continue;
				/* Back out changes we already did */
				spin_lock(&dquots[cnt]->dq_dqb_lock);
				dquot_decr_inodes(dquots[cnt], 1);
				spin_unlock(&dquots[cnt]->dq_dqb_lock);
			}
			goto warn_put_all;
		}
	}

warn_put_all:
	spin_unlock(&inode->i_lock);
	if (ret == 0)
		mark_all_dquot_dirty(dquots);
	srcu_read_unlock(&dquot_srcu, index);
	flush_warnings(warn);
	return ret;
}
EXPORT_SYMBOL(dquot_alloc_inode);

/*
 * Convert in-memory reserved quotas to real consumed quotas
 */
int dquot_claim_space_nodirty(struct inode *inode, qsize_t number)
{
	struct dquot **dquots;
	int cnt, index;

	if (!dquot_active(inode)) {
		spin_lock(&inode->i_lock);
		*inode_reserved_space(inode) -= number;
		__inode_add_bytes(inode, number);
		spin_unlock(&inode->i_lock);
		return 0;
	}

	dquots = i_dquot(inode);
	index = srcu_read_lock(&dquot_srcu);
	spin_lock(&inode->i_lock);
	/* Claim reserved quotas to allocated quotas */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquots[cnt]) {
			struct dquot *dquot = dquots[cnt];

			spin_lock(&dquot->dq_dqb_lock);
			if (WARN_ON_ONCE(dquot->dq_dqb.dqb_rsvspace < number))
				number = dquot->dq_dqb.dqb_rsvspace;
			dquot->dq_dqb.dqb_curspace += number;
			dquot->dq_dqb.dqb_rsvspace -= number;
			spin_unlock(&dquot->dq_dqb_lock);
		}
	}
	/* Update inode bytes */
	*inode_reserved_space(inode) -= number;
	__inode_add_bytes(inode, number);
	spin_unlock(&inode->i_lock);
	mark_all_dquot_dirty(dquots);
	srcu_read_unlock(&dquot_srcu, index);
	return 0;
}
EXPORT_SYMBOL(dquot_claim_space_nodirty);

/*
 * Convert allocated space back to in-memory reserved quotas
 */
void dquot_reclaim_space_nodirty(struct inode *inode, qsize_t number)
{
	struct dquot **dquots;
	int cnt, index;

	if (!dquot_active(inode)) {
		spin_lock(&inode->i_lock);
		*inode_reserved_space(inode) += number;
		__inode_sub_bytes(inode, number);
		spin_unlock(&inode->i_lock);
		return;
	}

	dquots = i_dquot(inode);
	index = srcu_read_lock(&dquot_srcu);
	spin_lock(&inode->i_lock);
	/* Claim reserved quotas to allocated quotas */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (dquots[cnt]) {
			struct dquot *dquot = dquots[cnt];

			spin_lock(&dquot->dq_dqb_lock);
			if (WARN_ON_ONCE(dquot->dq_dqb.dqb_curspace < number))
				number = dquot->dq_dqb.dqb_curspace;
			dquot->dq_dqb.dqb_rsvspace += number;
			dquot->dq_dqb.dqb_curspace -= number;
			spin_unlock(&dquot->dq_dqb_lock);
		}
	}
	/* Update inode bytes */
	*inode_reserved_space(inode) += number;
	__inode_sub_bytes(inode, number);
	spin_unlock(&inode->i_lock);
	mark_all_dquot_dirty(dquots);
	srcu_read_unlock(&dquot_srcu, index);
	return;
}
EXPORT_SYMBOL(dquot_reclaim_space_nodirty);

/*
 * This operation can block, but only after everything is updated
 */
void __dquot_free_space(struct inode *inode, qsize_t number, int flags)
{
	unsigned int cnt;
	struct dquot_warn warn[MAXQUOTAS];
	struct dquot **dquots;
	int reserve = flags & DQUOT_SPACE_RESERVE, index;

	if (!dquot_active(inode)) {
		if (reserve) {
			spin_lock(&inode->i_lock);
			*inode_reserved_space(inode) -= number;
			spin_unlock(&inode->i_lock);
		} else {
			inode_sub_bytes(inode, number);
		}
		return;
	}

	dquots = i_dquot(inode);
	index = srcu_read_lock(&dquot_srcu);
	spin_lock(&inode->i_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		int wtype;

		warn[cnt].w_type = QUOTA_NL_NOWARN;
		if (!dquots[cnt])
			continue;
		spin_lock(&dquots[cnt]->dq_dqb_lock);
		wtype = info_bdq_free(dquots[cnt], number);
		if (wtype != QUOTA_NL_NOWARN)
			prepare_warning(&warn[cnt], dquots[cnt], wtype);
		if (reserve)
			dquot_free_reserved_space(dquots[cnt], number);
		else
			dquot_decr_space(dquots[cnt], number);
		spin_unlock(&dquots[cnt]->dq_dqb_lock);
	}
	if (reserve)
		*inode_reserved_space(inode) -= number;
	else
		__inode_sub_bytes(inode, number);
	spin_unlock(&inode->i_lock);

	if (reserve)
		goto out_unlock;
	mark_all_dquot_dirty(dquots);
out_unlock:
	srcu_read_unlock(&dquot_srcu, index);
	flush_warnings(warn);
}
EXPORT_SYMBOL(__dquot_free_space);

/*
 * This operation can block, but only after everything is updated
 */
void dquot_free_inode(struct inode *inode)
{
	unsigned int cnt;
	struct dquot_warn warn[MAXQUOTAS];
	struct dquot * const *dquots;
	int index;

	if (!dquot_active(inode))
		return;

	dquots = i_dquot(inode);
	index = srcu_read_lock(&dquot_srcu);
	spin_lock(&inode->i_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		int wtype;

		warn[cnt].w_type = QUOTA_NL_NOWARN;
		if (!dquots[cnt])
			continue;
		spin_lock(&dquots[cnt]->dq_dqb_lock);
		wtype = info_idq_free(dquots[cnt], 1);
		if (wtype != QUOTA_NL_NOWARN)
			prepare_warning(&warn[cnt], dquots[cnt], wtype);
		dquot_decr_inodes(dquots[cnt], 1);
		spin_unlock(&dquots[cnt]->dq_dqb_lock);
	}
	spin_unlock(&inode->i_lock);
	mark_all_dquot_dirty(dquots);
	srcu_read_unlock(&dquot_srcu, index);
	flush_warnings(warn);
}
EXPORT_SYMBOL(dquot_free_inode);

/*
 * Transfer the number of inode and blocks from one diskquota to an other.
 * On success, dquot references in transfer_to are consumed and references
 * to original dquots that need to be released are placed there. On failure,
 * references are kept untouched.
 *
 * This operation can block, but only after everything is updated
 * A transaction must be started when entering this function.
 *
 * We are holding reference on transfer_from & transfer_to, no need to
 * protect them by srcu_read_lock().
 */
int __dquot_transfer(struct inode *inode, struct dquot **transfer_to)
{
	qsize_t cur_space;
	qsize_t rsv_space = 0;
	qsize_t inode_usage = 1;
	struct dquot *transfer_from[MAXQUOTAS] = {};
	int cnt, ret = 0;
	char is_valid[MAXQUOTAS] = {};
	struct dquot_warn warn_to[MAXQUOTAS];
	struct dquot_warn warn_from_inodes[MAXQUOTAS];
	struct dquot_warn warn_from_space[MAXQUOTAS];

	if (IS_NOQUOTA(inode))
		return 0;

	if (inode->i_sb->dq_op->get_inode_usage) {
		ret = inode->i_sb->dq_op->get_inode_usage(inode, &inode_usage);
		if (ret)
			return ret;
	}

	/* Initialize the arrays */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		warn_to[cnt].w_type = QUOTA_NL_NOWARN;
		warn_from_inodes[cnt].w_type = QUOTA_NL_NOWARN;
		warn_from_space[cnt].w_type = QUOTA_NL_NOWARN;
	}

	spin_lock(&dq_data_lock);
	spin_lock(&inode->i_lock);
	if (IS_NOQUOTA(inode)) {	/* File without quota accounting? */
		spin_unlock(&inode->i_lock);
		spin_unlock(&dq_data_lock);
		return 0;
	}
	cur_space = __inode_get_bytes(inode);
	rsv_space = __inode_get_rsv_space(inode);
	/*
	 * Build the transfer_from list, check limits, and update usage in
	 * the target structures.
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		/*
		 * Skip changes for same uid or gid or for turned off quota-type.
		 */
		if (!transfer_to[cnt])
			continue;
		/* Avoid races with quotaoff() */
		if (!sb_has_quota_active(inode->i_sb, cnt))
			continue;
		is_valid[cnt] = 1;
		transfer_from[cnt] = i_dquot(inode)[cnt];
		ret = dquot_add_inodes(transfer_to[cnt], inode_usage,
				       &warn_to[cnt]);
		if (ret)
			goto over_quota;
		ret = dquot_add_space(transfer_to[cnt], cur_space, rsv_space,
				      DQUOT_SPACE_WARN, &warn_to[cnt]);
		if (ret) {
			spin_lock(&transfer_to[cnt]->dq_dqb_lock);
			dquot_decr_inodes(transfer_to[cnt], inode_usage);
			spin_unlock(&transfer_to[cnt]->dq_dqb_lock);
			goto over_quota;
		}
	}

	/* Decrease usage for source structures and update quota pointers */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!is_valid[cnt])
			continue;
		/* Due to IO error we might not have transfer_from[] structure */
		if (transfer_from[cnt]) {
			int wtype;

			spin_lock(&transfer_from[cnt]->dq_dqb_lock);
			wtype = info_idq_free(transfer_from[cnt], inode_usage);
			if (wtype != QUOTA_NL_NOWARN)
				prepare_warning(&warn_from_inodes[cnt],
						transfer_from[cnt], wtype);
			wtype = info_bdq_free(transfer_from[cnt],
					      cur_space + rsv_space);
			if (wtype != QUOTA_NL_NOWARN)
				prepare_warning(&warn_from_space[cnt],
						transfer_from[cnt], wtype);
			dquot_decr_inodes(transfer_from[cnt], inode_usage);
			dquot_decr_space(transfer_from[cnt], cur_space);
			dquot_free_reserved_space(transfer_from[cnt],
						  rsv_space);
			spin_unlock(&transfer_from[cnt]->dq_dqb_lock);
		}
		i_dquot(inode)[cnt] = transfer_to[cnt];
	}
	spin_unlock(&inode->i_lock);
	spin_unlock(&dq_data_lock);

	mark_all_dquot_dirty(transfer_from);
	mark_all_dquot_dirty(transfer_to);
	flush_warnings(warn_to);
	flush_warnings(warn_from_inodes);
	flush_warnings(warn_from_space);
	/* Pass back references to put */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (is_valid[cnt])
			transfer_to[cnt] = transfer_from[cnt];
	return 0;
over_quota:
	/* Back out changes we already did */
	for (cnt--; cnt >= 0; cnt--) {
		if (!is_valid[cnt])
			continue;
		spin_lock(&transfer_to[cnt]->dq_dqb_lock);
		dquot_decr_inodes(transfer_to[cnt], inode_usage);
		dquot_decr_space(transfer_to[cnt], cur_space);
		dquot_free_reserved_space(transfer_to[cnt], rsv_space);
		spin_unlock(&transfer_to[cnt]->dq_dqb_lock);
	}
	spin_unlock(&inode->i_lock);
	spin_unlock(&dq_data_lock);
	flush_warnings(warn_to);
	return ret;
}
EXPORT_SYMBOL(__dquot_transfer);

/* Wrapper for transferring ownership of an inode for uid/gid only
 * Called from FSXXX_setattr()
 */
int dquot_transfer(struct mnt_idmap *idmap, struct inode *inode,
		   struct iattr *iattr)
{
	struct dquot *transfer_to[MAXQUOTAS] = {};
	struct dquot *dquot;
	struct super_block *sb = inode->i_sb;
	int ret;

	if (!dquot_active(inode))
		return 0;

	if (i_uid_needs_update(idmap, iattr, inode)) {
		kuid_t kuid = from_vfsuid(idmap, i_user_ns(inode),
					  iattr->ia_vfsuid);

		dquot = dqget(sb, make_kqid_uid(kuid));
		if (IS_ERR(dquot)) {
			if (PTR_ERR(dquot) != -ESRCH) {
				ret = PTR_ERR(dquot);
				goto out_put;
			}
			dquot = NULL;
		}
		transfer_to[USRQUOTA] = dquot;
	}
	if (i_gid_needs_update(idmap, iattr, inode)) {
		kgid_t kgid = from_vfsgid(idmap, i_user_ns(inode),
					  iattr->ia_vfsgid);

		dquot = dqget(sb, make_kqid_gid(kgid));
		if (IS_ERR(dquot)) {
			if (PTR_ERR(dquot) != -ESRCH) {
				ret = PTR_ERR(dquot);
				goto out_put;
			}
			dquot = NULL;
		}
		transfer_to[GRPQUOTA] = dquot;
	}
	ret = __dquot_transfer(inode, transfer_to);
out_put:
	dqput_all(transfer_to);
	return ret;
}
EXPORT_SYMBOL(dquot_transfer);

/*
 * Write info of quota file to disk
 */
int dquot_commit_info(struct super_block *sb, int type)
{
	struct quota_info *dqopt = sb_dqopt(sb);

	return dqopt->ops[type]->write_file_info(sb, type);
}
EXPORT_SYMBOL(dquot_commit_info);

int dquot_get_next_id(struct super_block *sb, struct kqid *qid)
{
	struct quota_info *dqopt = sb_dqopt(sb);

	if (!sb_has_quota_active(sb, qid->type))
		return -ESRCH;
	if (!dqopt->ops[qid->type]->get_next_id)
		return -ENOSYS;
	return dqopt->ops[qid->type]->get_next_id(sb, qid);
}
EXPORT_SYMBOL(dquot_get_next_id);

/*
 * Definitions of diskquota operations.
 */
const struct dquot_operations dquot_operations = {
	.write_dquot	= dquot_commit,
	.acquire_dquot	= dquot_acquire,
	.release_dquot	= dquot_release,
	.mark_dirty	= dquot_mark_dquot_dirty,
	.write_info	= dquot_commit_info,
	.alloc_dquot	= dquot_alloc,
	.destroy_dquot	= dquot_destroy,
	.get_next_id	= dquot_get_next_id,
};
EXPORT_SYMBOL(dquot_operations);

/*
 * Generic helper for ->open on filesystems supporting disk quotas.
 */
int dquot_file_open(struct inode *inode, struct file *file)
{
	int error;

	error = generic_file_open(inode, file);
	if (!error && (file->f_mode & FMODE_WRITE))
		error = dquot_initialize(inode);
	return error;
}
EXPORT_SYMBOL(dquot_file_open);

static void vfs_cleanup_quota_inode(struct super_block *sb, int type)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	struct inode *inode = dqopt->files[type];

	if (!inode)
		return;
	if (!(dqopt->flags & DQUOT_QUOTA_SYS_FILE)) {
		inode_lock(inode);
		inode->i_flags &= ~S_NOQUOTA;
		inode_unlock(inode);
	}
	dqopt->files[type] = NULL;
	iput(inode);
}

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */
int dquot_disable(struct super_block *sb, int type, unsigned int flags)
{
	int cnt;
	struct quota_info *dqopt = sb_dqopt(sb);

	/* s_umount should be held in exclusive mode */
	if (WARN_ON_ONCE(down_read_trylock(&sb->s_umount)))
		up_read(&sb->s_umount);

	/* Cannot turn off usage accounting without turning off limits, or
	 * suspend quotas and simultaneously turn quotas off. */
	if ((flags & DQUOT_USAGE_ENABLED && !(flags & DQUOT_LIMITS_ENABLED))
	    || (flags & DQUOT_SUSPENDED && flags & (DQUOT_LIMITS_ENABLED |
	    DQUOT_USAGE_ENABLED)))
		return -EINVAL;

	/*
	 * Skip everything if there's nothing to do. We have to do this because
	 * sometimes we are called when fill_super() failed and calling
	 * sync_fs() in such cases does no good.
	 */
	if (!sb_any_quota_loaded(sb))
		return 0;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_loaded(sb, cnt))
			continue;

		if (flags & DQUOT_SUSPENDED) {
			spin_lock(&dq_state_lock);
			dqopt->flags |=
				dquot_state_flag(DQUOT_SUSPENDED, cnt);
			spin_unlock(&dq_state_lock);
		} else {
			spin_lock(&dq_state_lock);
			dqopt->flags &= ~dquot_state_flag(flags, cnt);
			/* Turning off suspended quotas? */
			if (!sb_has_quota_loaded(sb, cnt) &&
			    sb_has_quota_suspended(sb, cnt)) {
				dqopt->flags &=	~dquot_state_flag(
							DQUOT_SUSPENDED, cnt);
				spin_unlock(&dq_state_lock);
				vfs_cleanup_quota_inode(sb, cnt);
				continue;
			}
			spin_unlock(&dq_state_lock);
		}

		/* We still have to keep quota loaded? */
		if (sb_has_quota_loaded(sb, cnt) && !(flags & DQUOT_SUSPENDED))
			continue;

		/* Note: these are blocking operations */
		drop_dquot_ref(sb, cnt);
		invalidate_dquots(sb, cnt);
		/*
		 * Now all dquots should be invalidated, all writes done so we
		 * should be only users of the info. No locks needed.
		 */
		if (info_dirty(&dqopt->info[cnt]))
			sb->dq_op->write_info(sb, cnt);
		if (dqopt->ops[cnt]->free_file_info)
			dqopt->ops[cnt]->free_file_info(sb, cnt);
		put_quota_format(dqopt->info[cnt].dqi_format);
		dqopt->info[cnt].dqi_flags = 0;
		dqopt->info[cnt].dqi_igrace = 0;
		dqopt->info[cnt].dqi_bgrace = 0;
		dqopt->ops[cnt] = NULL;
	}

	/* Skip syncing and setting flags if quota files are hidden */
	if (dqopt->flags & DQUOT_QUOTA_SYS_FILE)
		goto put_inodes;

	/* Sync the superblock so that buffers with quota data are written to
	 * disk (and so userspace sees correct data afterwards). */
	if (sb->s_op->sync_fs)
		sb->s_op->sync_fs(sb, 1);
	sync_blockdev(sb->s_bdev);
	/* Now the quota files are just ordinary files and we can set the
	 * inode flags back. Moreover we discard the pagecache so that
	 * userspace sees the writes we did bypassing the pagecache. We
	 * must also discard the blockdev buffers so that we see the
	 * changes done by userspace on the next quotaon() */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (!sb_has_quota_loaded(sb, cnt) && dqopt->files[cnt]) {
			inode_lock(dqopt->files[cnt]);
			truncate_inode_pages(&dqopt->files[cnt]->i_data, 0);
			inode_unlock(dqopt->files[cnt]);
		}
	if (sb->s_bdev)
		invalidate_bdev(sb->s_bdev);
put_inodes:
	/* We are done when suspending quotas */
	if (flags & DQUOT_SUSPENDED)
		return 0;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (!sb_has_quota_loaded(sb, cnt))
			vfs_cleanup_quota_inode(sb, cnt);
	return 0;
}
EXPORT_SYMBOL(dquot_disable);

int dquot_quota_off(struct super_block *sb, int type)
{
	return dquot_disable(sb, type,
			     DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED);
}
EXPORT_SYMBOL(dquot_quota_off);

/*
 *	Turn quotas on on a device
 */

static int vfs_setup_quota_inode(struct inode *inode, int type)
{
	struct super_block *sb = inode->i_sb;
	struct quota_info *dqopt = sb_dqopt(sb);

	if (is_bad_inode(inode))
		return -EUCLEAN;
	if (!S_ISREG(inode->i_mode))
		return -EACCES;
	if (IS_RDONLY(inode))
		return -EROFS;
	if (sb_has_quota_loaded(sb, type))
		return -EBUSY;

	dqopt->files[type] = igrab(inode);
	if (!dqopt->files[type])
		return -EIO;
	if (!(dqopt->flags & DQUOT_QUOTA_SYS_FILE)) {
		/* We don't want quota and atime on quota files (deadlocks
		 * possible) Also nobody should write to the file - we use
		 * special IO operations which ignore the immutable bit. */
		inode_lock(inode);
		inode->i_flags |= S_NOQUOTA;
		inode_unlock(inode);
		/*
		 * When S_NOQUOTA is set, remove dquot references as no more
		 * references can be added
		 */
		__dquot_drop(inode);
	}
	return 0;
}

int dquot_load_quota_sb(struct super_block *sb, int type, int format_id,
	unsigned int flags)
{
	struct quota_format_type *fmt = find_quota_format(format_id);
	struct quota_info *dqopt = sb_dqopt(sb);
	int error;

	/* Just unsuspend quotas? */
	BUG_ON(flags & DQUOT_SUSPENDED);
	/* s_umount should be held in exclusive mode */
	if (WARN_ON_ONCE(down_read_trylock(&sb->s_umount)))
		up_read(&sb->s_umount);

	if (!fmt)
		return -ESRCH;
	if (!sb->s_op->quota_write || !sb->s_op->quota_read ||
	    (type == PRJQUOTA && sb->dq_op->get_projid == NULL)) {
		error = -EINVAL;
		goto out_fmt;
	}
	/* Filesystems outside of init_user_ns not yet supported */
	if (sb->s_user_ns != &init_user_ns) {
		error = -EINVAL;
		goto out_fmt;
	}
	/* Usage always has to be set... */
	if (!(flags & DQUOT_USAGE_ENABLED)) {
		error = -EINVAL;
		goto out_fmt;
	}
	if (sb_has_quota_loaded(sb, type)) {
		error = -EBUSY;
		goto out_fmt;
	}

	if (!(dqopt->flags & DQUOT_QUOTA_SYS_FILE)) {
		/* As we bypass the pagecache we must now flush all the
		 * dirty data and invalidate caches so that kernel sees
		 * changes from userspace. It is not enough to just flush
		 * the quota file since if blocksize < pagesize, invalidation
		 * of the cache could fail because of other unrelated dirty
		 * data */
		sync_filesystem(sb);
		invalidate_bdev(sb->s_bdev);
	}

	error = -EINVAL;
	if (!fmt->qf_ops->check_quota_file(sb, type))
		goto out_fmt;

	dqopt->ops[type] = fmt->qf_ops;
	dqopt->info[type].dqi_format = fmt;
	dqopt->info[type].dqi_fmt_id = format_id;
	INIT_LIST_HEAD(&dqopt->info[type].dqi_dirty_list);
	error = dqopt->ops[type]->read_file_info(sb, type);
	if (error < 0)
		goto out_fmt;
	if (dqopt->flags & DQUOT_QUOTA_SYS_FILE) {
		spin_lock(&dq_data_lock);
		dqopt->info[type].dqi_flags |= DQF_SYS_FILE;
		spin_unlock(&dq_data_lock);
	}
	spin_lock(&dq_state_lock);
	dqopt->flags |= dquot_state_flag(flags, type);
	spin_unlock(&dq_state_lock);

	error = add_dquot_ref(sb, type);
	if (error)
		dquot_disable(sb, type, flags);

	return error;
out_fmt:
	put_quota_format(fmt);

	return error;
}
EXPORT_SYMBOL(dquot_load_quota_sb);

/*
 * More powerful function for turning on quotas on given quota inode allowing
 * setting of individual quota flags
 */
int dquot_load_quota_inode(struct inode *inode, int type, int format_id,
	unsigned int flags)
{
	int err;

	err = vfs_setup_quota_inode(inode, type);
	if (err < 0)
		return err;
	err = dquot_load_quota_sb(inode->i_sb, type, format_id, flags);
	if (err < 0)
		vfs_cleanup_quota_inode(inode->i_sb, type);
	return err;
}
EXPORT_SYMBOL(dquot_load_quota_inode);

/* Reenable quotas on remount RW */
int dquot_resume(struct super_block *sb, int type)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	int ret = 0, cnt;
	unsigned int flags;

	/* s_umount should be held in exclusive mode */
	if (WARN_ON_ONCE(down_read_trylock(&sb->s_umount)))
		up_read(&sb->s_umount);

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_suspended(sb, cnt))
			continue;

		spin_lock(&dq_state_lock);
		flags = dqopt->flags & dquot_state_flag(DQUOT_USAGE_ENABLED |
							DQUOT_LIMITS_ENABLED,
							cnt);
		dqopt->flags &= ~dquot_state_flag(DQUOT_STATE_FLAGS, cnt);
		spin_unlock(&dq_state_lock);

		flags = dquot_generic_flag(flags, cnt);
		ret = dquot_load_quota_sb(sb, cnt, dqopt->info[cnt].dqi_fmt_id,
					  flags);
		if (ret < 0)
			vfs_cleanup_quota_inode(sb, cnt);
	}

	return ret;
}
EXPORT_SYMBOL(dquot_resume);

int dquot_quota_on(struct super_block *sb, int type, int format_id,
		   const struct path *path)
{
	int error = security_quota_on(path->dentry);
	if (error)
		return error;
	/* Quota file not on the same filesystem? */
	if (path->dentry->d_sb != sb)
		error = -EXDEV;
	else
		error = dquot_load_quota_inode(d_inode(path->dentry), type,
					     format_id, DQUOT_USAGE_ENABLED |
					     DQUOT_LIMITS_ENABLED);
	return error;
}
EXPORT_SYMBOL(dquot_quota_on);

/*
 * This function is used when filesystem needs to initialize quotas
 * during mount time.
 */
int dquot_quota_on_mount(struct super_block *sb, char *qf_name,
		int format_id, int type)
{
	struct dentry *dentry;
	int error;

	dentry = lookup_positive_unlocked(qf_name, sb->s_root, strlen(qf_name));
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	error = security_quota_on(dentry);
	if (!error)
		error = dquot_load_quota_inode(d_inode(dentry), type, format_id,
				DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED);

	dput(dentry);
	return error;
}
EXPORT_SYMBOL(dquot_quota_on_mount);

static int dquot_quota_enable(struct super_block *sb, unsigned int flags)
{
	int ret;
	int type;
	struct quota_info *dqopt = sb_dqopt(sb);

	if (!(dqopt->flags & DQUOT_QUOTA_SYS_FILE))
		return -ENOSYS;
	/* Accounting cannot be turned on while fs is mounted */
	flags &= ~(FS_QUOTA_UDQ_ACCT | FS_QUOTA_GDQ_ACCT | FS_QUOTA_PDQ_ACCT);
	if (!flags)
		return -EINVAL;
	for (type = 0; type < MAXQUOTAS; type++) {
		if (!(flags & qtype_enforce_flag(type)))
			continue;
		/* Can't enforce without accounting */
		if (!sb_has_quota_usage_enabled(sb, type)) {
			ret = -EINVAL;
			goto out_err;
		}
		if (sb_has_quota_limits_enabled(sb, type)) {
			ret = -EBUSY;
			goto out_err;
		}
		spin_lock(&dq_state_lock);
		dqopt->flags |= dquot_state_flag(DQUOT_LIMITS_ENABLED, type);
		spin_unlock(&dq_state_lock);
	}
	return 0;
out_err:
	/* Backout enforcement enablement we already did */
	for (type--; type >= 0; type--)  {
		if (flags & qtype_enforce_flag(type))
			dquot_disable(sb, type, DQUOT_LIMITS_ENABLED);
	}
	/* Error code translation for better compatibility with XFS */
	if (ret == -EBUSY)
		ret = -EEXIST;
	return ret;
}

static int dquot_quota_disable(struct super_block *sb, unsigned int flags)
{
	int ret;
	int type;
	struct quota_info *dqopt = sb_dqopt(sb);

	if (!(dqopt->flags & DQUOT_QUOTA_SYS_FILE))
		return -ENOSYS;
	/*
	 * We don't support turning off accounting via quotactl. In principle
	 * quota infrastructure can do this but filesystems don't expect
	 * userspace to be able to do it.
	 */
	if (flags &
		  (FS_QUOTA_UDQ_ACCT | FS_QUOTA_GDQ_ACCT | FS_QUOTA_PDQ_ACCT))
		return -EOPNOTSUPP;

	/* Filter out limits not enabled */
	for (type = 0; type < MAXQUOTAS; type++)
		if (!sb_has_quota_limits_enabled(sb, type))
			flags &= ~qtype_enforce_flag(type);
	/* Nothing left? */
	if (!flags)
		return -EEXIST;
	for (type = 0; type < MAXQUOTAS; type++) {
		if (flags & qtype_enforce_flag(type)) {
			ret = dquot_disable(sb, type, DQUOT_LIMITS_ENABLED);
			if (ret < 0)
				goto out_err;
		}
	}
	return 0;
out_err:
	/* Backout enforcement disabling we already did */
	for (type--; type >= 0; type--)  {
		if (flags & qtype_enforce_flag(type)) {
			spin_lock(&dq_state_lock);
			dqopt->flags |=
				dquot_state_flag(DQUOT_LIMITS_ENABLED, type);
			spin_unlock(&dq_state_lock);
		}
	}
	return ret;
}

/* Generic routine for getting common part of quota structure */
static void do_get_dqblk(struct dquot *dquot, struct qc_dqblk *di)
{
	struct mem_dqblk *dm = &dquot->dq_dqb;

	memset(di, 0, sizeof(*di));
	spin_lock(&dquot->dq_dqb_lock);
	di->d_spc_hardlimit = dm->dqb_bhardlimit;
	di->d_spc_softlimit = dm->dqb_bsoftlimit;
	di->d_ino_hardlimit = dm->dqb_ihardlimit;
	di->d_ino_softlimit = dm->dqb_isoftlimit;
	di->d_space = dm->dqb_curspace + dm->dqb_rsvspace;
	di->d_ino_count = dm->dqb_curinodes;
	di->d_spc_timer = dm->dqb_btime;
	di->d_ino_timer = dm->dqb_itime;
	spin_unlock(&dquot->dq_dqb_lock);
}

int dquot_get_dqblk(struct super_block *sb, struct kqid qid,
		    struct qc_dqblk *di)
{
	struct dquot *dquot;

	dquot = dqget(sb, qid);
	if (IS_ERR(dquot))
		return PTR_ERR(dquot);
	do_get_dqblk(dquot, di);
	dqput(dquot);

	return 0;
}
EXPORT_SYMBOL(dquot_get_dqblk);

int dquot_get_next_dqblk(struct super_block *sb, struct kqid *qid,
			 struct qc_dqblk *di)
{
	struct dquot *dquot;
	int err;

	if (!sb->dq_op->get_next_id)
		return -ENOSYS;
	err = sb->dq_op->get_next_id(sb, qid);
	if (err < 0)
		return err;
	dquot = dqget(sb, *qid);
	if (IS_ERR(dquot))
		return PTR_ERR(dquot);
	do_get_dqblk(dquot, di);
	dqput(dquot);

	return 0;
}
EXPORT_SYMBOL(dquot_get_next_dqblk);

#define VFS_QC_MASK \
	(QC_SPACE | QC_SPC_SOFT | QC_SPC_HARD | \
	 QC_INO_COUNT | QC_INO_SOFT | QC_INO_HARD | \
	 QC_SPC_TIMER | QC_INO_TIMER)

/* Generic routine for setting common part of quota structure */
static int do_set_dqblk(struct dquot *dquot, struct qc_dqblk *di)
{
	struct mem_dqblk *dm = &dquot->dq_dqb;
	int check_blim = 0, check_ilim = 0;
	struct mem_dqinfo *dqi = &sb_dqopt(dquot->dq_sb)->info[dquot->dq_id.type];

	if (di->d_fieldmask & ~VFS_QC_MASK)
		return -EINVAL;

	if (((di->d_fieldmask & QC_SPC_SOFT) &&
	     di->d_spc_softlimit > dqi->dqi_max_spc_limit) ||
	    ((di->d_fieldmask & QC_SPC_HARD) &&
	     di->d_spc_hardlimit > dqi->dqi_max_spc_limit) ||
	    ((di->d_fieldmask & QC_INO_SOFT) &&
	     (di->d_ino_softlimit > dqi->dqi_max_ino_limit)) ||
	    ((di->d_fieldmask & QC_INO_HARD) &&
	     (di->d_ino_hardlimit > dqi->dqi_max_ino_limit)))
		return -ERANGE;

	spin_lock(&dquot->dq_dqb_lock);
	if (di->d_fieldmask & QC_SPACE) {
		dm->dqb_curspace = di->d_space - dm->dqb_rsvspace;
		check_blim = 1;
		set_bit(DQ_LASTSET_B + QIF_SPACE_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & QC_SPC_SOFT)
		dm->dqb_bsoftlimit = di->d_spc_softlimit;
	if (di->d_fieldmask & QC_SPC_HARD)
		dm->dqb_bhardlimit = di->d_spc_hardlimit;
	if (di->d_fieldmask & (QC_SPC_SOFT | QC_SPC_HARD)) {
		check_blim = 1;
		set_bit(DQ_LASTSET_B + QIF_BLIMITS_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & QC_INO_COUNT) {
		dm->dqb_curinodes = di->d_ino_count;
		check_ilim = 1;
		set_bit(DQ_LASTSET_B + QIF_INODES_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & QC_INO_SOFT)
		dm->dqb_isoftlimit = di->d_ino_softlimit;
	if (di->d_fieldmask & QC_INO_HARD)
		dm->dqb_ihardlimit = di->d_ino_hardlimit;
	if (di->d_fieldmask & (QC_INO_SOFT | QC_INO_HARD)) {
		check_ilim = 1;
		set_bit(DQ_LASTSET_B + QIF_ILIMITS_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & QC_SPC_TIMER) {
		dm->dqb_btime = di->d_spc_timer;
		check_blim = 1;
		set_bit(DQ_LASTSET_B + QIF_BTIME_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & QC_INO_TIMER) {
		dm->dqb_itime = di->d_ino_timer;
		check_ilim = 1;
		set_bit(DQ_LASTSET_B + QIF_ITIME_B, &dquot->dq_flags);
	}

	if (check_blim) {
		if (!dm->dqb_bsoftlimit ||
		    dm->dqb_curspace + dm->dqb_rsvspace <= dm->dqb_bsoftlimit) {
			dm->dqb_btime = 0;
			clear_bit(DQ_BLKS_B, &dquot->dq_flags);
		} else if (!(di->d_fieldmask & QC_SPC_TIMER))
			/* Set grace only if user hasn't provided his own... */
			dm->dqb_btime = ktime_get_real_seconds() + dqi->dqi_bgrace;
	}
	if (check_ilim) {
		if (!dm->dqb_isoftlimit ||
		    dm->dqb_curinodes <= dm->dqb_isoftlimit) {
			dm->dqb_itime = 0;
			clear_bit(DQ_INODES_B, &dquot->dq_flags);
		} else if (!(di->d_fieldmask & QC_INO_TIMER))
			/* Set grace only if user hasn't provided his own... */
			dm->dqb_itime = ktime_get_real_seconds() + dqi->dqi_igrace;
	}
	if (dm->dqb_bhardlimit || dm->dqb_bsoftlimit || dm->dqb_ihardlimit ||
	    dm->dqb_isoftlimit)
		clear_bit(DQ_FAKE_B, &dquot->dq_flags);
	else
		set_bit(DQ_FAKE_B, &dquot->dq_flags);
	spin_unlock(&dquot->dq_dqb_lock);
	mark_dquot_dirty(dquot);

	return 0;
}

int dquot_set_dqblk(struct super_block *sb, struct kqid qid,
		  struct qc_dqblk *di)
{
	struct dquot *dquot;
	int rc;

	dquot = dqget(sb, qid);
	if (IS_ERR(dquot)) {
		rc = PTR_ERR(dquot);
		goto out;
	}
	rc = do_set_dqblk(dquot, di);
	dqput(dquot);
out:
	return rc;
}
EXPORT_SYMBOL(dquot_set_dqblk);

/* Generic routine for getting common part of quota file information */
int dquot_get_state(struct super_block *sb, struct qc_state *state)
{
	struct mem_dqinfo *mi;
	struct qc_type_state *tstate;
	struct quota_info *dqopt = sb_dqopt(sb);
	int type;

	memset(state, 0, sizeof(*state));
	for (type = 0; type < MAXQUOTAS; type++) {
		if (!sb_has_quota_active(sb, type))
			continue;
		tstate = state->s_state + type;
		mi = sb_dqopt(sb)->info + type;
		tstate->flags = QCI_ACCT_ENABLED;
		spin_lock(&dq_data_lock);
		if (mi->dqi_flags & DQF_SYS_FILE)
			tstate->flags |= QCI_SYSFILE;
		if (mi->dqi_flags & DQF_ROOT_SQUASH)
			tstate->flags |= QCI_ROOT_SQUASH;
		if (sb_has_quota_limits_enabled(sb, type))
			tstate->flags |= QCI_LIMITS_ENFORCED;
		tstate->spc_timelimit = mi->dqi_bgrace;
		tstate->ino_timelimit = mi->dqi_igrace;
		if (dqopt->files[type]) {
			tstate->ino = dqopt->files[type]->i_ino;
			tstate->blocks = dqopt->files[type]->i_blocks;
		}
		tstate->nextents = 1;	/* We don't know... */
		spin_unlock(&dq_data_lock);
	}
	return 0;
}
EXPORT_SYMBOL(dquot_get_state);

/* Generic routine for setting common part of quota file information */
int dquot_set_dqinfo(struct super_block *sb, int type, struct qc_info *ii)
{
	struct mem_dqinfo *mi;

	if ((ii->i_fieldmask & QC_WARNS_MASK) ||
	    (ii->i_fieldmask & QC_RT_SPC_TIMER))
		return -EINVAL;
	if (!sb_has_quota_active(sb, type))
		return -ESRCH;
	mi = sb_dqopt(sb)->info + type;
	if (ii->i_fieldmask & QC_FLAGS) {
		if ((ii->i_flags & QCI_ROOT_SQUASH &&
		     mi->dqi_format->qf_fmt_id != QFMT_VFS_OLD))
			return -EINVAL;
	}
	spin_lock(&dq_data_lock);
	if (ii->i_fieldmask & QC_SPC_TIMER)
		mi->dqi_bgrace = ii->i_spc_timelimit;
	if (ii->i_fieldmask & QC_INO_TIMER)
		mi->dqi_igrace = ii->i_ino_timelimit;
	if (ii->i_fieldmask & QC_FLAGS) {
		if (ii->i_flags & QCI_ROOT_SQUASH)
			mi->dqi_flags |= DQF_ROOT_SQUASH;
		else
			mi->dqi_flags &= ~DQF_ROOT_SQUASH;
	}
	spin_unlock(&dq_data_lock);
	mark_info_dirty(sb, type);
	/* Force write to disk */
	return sb->dq_op->write_info(sb, type);
}
EXPORT_SYMBOL(dquot_set_dqinfo);

const struct quotactl_ops dquot_quotactl_sysfile_ops = {
	.quota_enable	= dquot_quota_enable,
	.quota_disable	= dquot_quota_disable,
	.quota_sync	= dquot_quota_sync,
	.get_state	= dquot_get_state,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.get_nextdqblk	= dquot_get_next_dqblk,
	.set_dqblk	= dquot_set_dqblk
};
EXPORT_SYMBOL(dquot_quotactl_sysfile_ops);

static int do_proc_dqstats(struct ctl_table *table, int write,
		     void *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int type = (unsigned long *)table->data - dqstats.stat;
	s64 value = percpu_counter_sum(&dqstats.counter[type]);

	/* Filter negative values for non-monotonic counters */
	if (value < 0 && (type == DQST_ALLOC_DQUOTS ||
			  type == DQST_FREE_DQUOTS))
		value = 0;

	/* Update global table */
	dqstats.stat[type] = value;
	return proc_doulongvec_minmax(table, write, buffer, lenp, ppos);
}

static struct ctl_table fs_dqstats_table[] = {
	{
		.procname	= "lookups",
		.data		= &dqstats.stat[DQST_LOOKUPS],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "drops",
		.data		= &dqstats.stat[DQST_DROPS],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "reads",
		.data		= &dqstats.stat[DQST_READS],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "writes",
		.data		= &dqstats.stat[DQST_WRITES],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "cache_hits",
		.data		= &dqstats.stat[DQST_CACHE_HITS],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "allocated_dquots",
		.data		= &dqstats.stat[DQST_ALLOC_DQUOTS],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "free_dquots",
		.data		= &dqstats.stat[DQST_FREE_DQUOTS],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "syncs",
		.data		= &dqstats.stat[DQST_SYNCS],
		.maxlen		= sizeof(unsigned long),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
#ifdef CONFIG_PRINT_QUOTA_WARNING
	{
		.procname	= "warnings",
		.data		= &flag_print_warnings,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
#endif
	{ },
};

static int __init dquot_init(void)
{
	int i, ret;
	unsigned long nr_hash, order;

	printk(KERN_NOTICE "VFS: Disk quotas %s\n", __DQUOT_VERSION__);

	register_sysctl_init("fs/quota", fs_dqstats_table);

	dquot_cachep = kmem_cache_create("dquot",
			sizeof(struct dquot), sizeof(unsigned long) * 4,
			(SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
				SLAB_MEM_SPREAD|SLAB_PANIC),
			NULL);

	order = 0;
	dquot_hash = (struct hlist_head *)__get_free_pages(GFP_KERNEL, order);
	if (!dquot_hash)
		panic("Cannot create dquot hash table");

	for (i = 0; i < _DQST_DQSTAT_LAST; i++) {
		ret = percpu_counter_init(&dqstats.counter[i], 0, GFP_KERNEL);
		if (ret)
			panic("Cannot create dquot stat counters");
	}

	/* Find power-of-two hlist_heads which can fit into allocation */
	nr_hash = (1UL << order) * PAGE_SIZE / sizeof(struct hlist_head);
	dq_hash_bits = ilog2(nr_hash);

	nr_hash = 1UL << dq_hash_bits;
	dq_hash_mask = nr_hash - 1;
	for (i = 0; i < nr_hash; i++)
		INIT_HLIST_HEAD(dquot_hash + i);

	pr_info("VFS: Dquot-cache hash table entries: %ld (order %ld,"
		" %ld bytes)\n", nr_hash, order, (PAGE_SIZE << order));

	if (register_shrinker(&dqcache_shrinker, "dquota-cache"))
		panic("Cannot register dquot shrinker");

	return 0;
}
fs_initcall(dquot_init);
