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
#include <linux/kmod.h>
#include <linux/namei.h>
#include <linux/capability.h>
#include <linux/quotaops.h>
#include "../internal.h" /* ugh */

#include <linux/uaccess.h>

/*
 * There are three quota SMP locks. dq_list_lock protects all lists with quotas
 * and quota formats.
 * dq_data_lock protects data from dq_dqb and also mem_dqinfo structures and
 * also guards consistency of dquot->dq_dqb with inode->i_blocks, i_bytes.
 * i_blocks and i_bytes updates itself are guarded by i_lock acquired directly
 * in inode_add_bytes() and inode_sub_bytes(). dq_state_lock protects
 * modifications of quota state (on quotaon and quotaoff) and readers who care
 * about latest values take it as well.
 *
 * The spinlock ordering is hence: dq_data_lock > dq_list_lock > i_lock,
 *   dq_list_lock > dq_state_lock
 *
 * Note that some things (eg. sb pointer, type, id) doesn't change during
 * the life of the dquot structure and so needn't to be protected by a lock
 *
 * Any operation working on dquots via inode pointers must hold dqptr_sem.  If
 * operation is just reading pointers from inode (or not using them at all) the
 * read lock is enough. If pointers are altered function must hold write lock.
 * Special care needs to be taken about S_NOQUOTA inode flag (marking that
 * inode is a quota file). Functions adding pointers from inode to dquots have
 * to check this flag under dqptr_sem and then (if S_NOQUOTA is not set) they
 * have to do all pointer modifications before dropping dqptr_sem. This makes
 * sure they cannot race with quotaon which first sets S_NOQUOTA flag and
 * then drops all pointers to dquots from an inode.
 *
 * Each dquot has its dq_lock mutex. Locked dquots might not be referenced
 * from inodes (dquot_alloc_space() and such don't check the dq_lock).
 * Currently dquot is locked only when it is being read to memory (or space for
 * it is being allocated) on the first dqget() and when it is being released on
 * the last dqput(). The allocation and release oparations are serialized by
 * the dq_lock and by checking the use count in dquot_release().  Write
 * operations on dquots don't hold dq_lock as they copy data under dq_data_lock
 * spinlock to internal buffers before writing.
 *
 * Lock ordering (including related VFS locks) is the following:
 *   dqonoff_mutex > i_mutex > journal_lock > dqptr_sem > dquot->dq_lock >
 *   dqio_mutex
 * dqonoff_mutex > i_mutex comes from dquot_quota_sync, dquot_enable, etc.
 * The lock ordering of dqptr_sem imposed by quota code is only dqonoff_sem >
 * dqptr_sem. But filesystem has to count with the fact that functions such as
 * dquot_alloc_space() acquire dqptr_sem and they usually have to be called
 * from inside a transaction to keep filesystem consistency after a crash. Also
 * filesystems usually want to do some IO on dquot from ->mark_dirty which is
 * called with dqptr_sem held.
 */

static __cacheline_aligned_in_smp DEFINE_SPINLOCK(dq_list_lock);
static __cacheline_aligned_in_smp DEFINE_SPINLOCK(dq_state_lock);
__cacheline_aligned_in_smp DEFINE_SPINLOCK(dq_data_lock);
EXPORT_SYMBOL(dq_data_lock);

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
 * The quota code uses three lists for dquot management: the inuse_list,
 * free_dquots, and dquot_hash[] array. A single dquot structure may be
 * on all three lists, depending on its current state.
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
static void __dquot_initialize(struct inode *inode, int type);

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
	struct hlist_node *node;
	struct dquot *dquot;

	hlist_for_each (node, dquot_hash+hashent) {
		dquot = hlist_entry(node, struct dquot, dq_hash);
		if (dquot->dq_sb == sb && qid_eq(dquot->dq_id, qid))
			return dquot;
	}
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

/* This function needs dq_list_lock */
static inline int clear_dquot_dirty(struct dquot *dquot)
{
	if (!test_and_clear_bit(DQ_MOD_B, &dquot->dq_flags))
		return 0;
	list_del_init(&dquot->dq_dirty);
	return 1;
}

void mark_info_dirty(struct super_block *sb, int type)
{
	set_bit(DQF_INFO_DIRTY_B, &sb_dqopt(sb)->info[type].dqi_flags);
}
EXPORT_SYMBOL(mark_info_dirty);

/*
 *	Read dquot from disk and alloc space for it
 */

int dquot_acquire(struct dquot *dquot)
{
	int ret = 0, ret2 = 0;
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	mutex_lock(&dquot->dq_lock);
	mutex_lock(&dqopt->dqio_mutex);
	if (!test_bit(DQ_READ_B, &dquot->dq_flags))
		ret = dqopt->ops[dquot->dq_id.type]->read_dqblk(dquot);
	if (ret < 0)
		goto out_iolock;
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
	set_bit(DQ_ACTIVE_B, &dquot->dq_flags);
out_iolock:
	mutex_unlock(&dqopt->dqio_mutex);
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
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	mutex_lock(&dqopt->dqio_mutex);
	spin_lock(&dq_list_lock);
	if (!clear_dquot_dirty(dquot)) {
		spin_unlock(&dq_list_lock);
		goto out_sem;
	}
	spin_unlock(&dq_list_lock);
	/* Inactive dquot can be only if there was error during read/init
	 * => we have better not writing it */
	if (test_bit(DQ_ACTIVE_B, &dquot->dq_flags))
		ret = dqopt->ops[dquot->dq_id.type]->commit_dqblk(dquot);
	else
		ret = -EIO;
out_sem:
	mutex_unlock(&dqopt->dqio_mutex);
	return ret;
}
EXPORT_SYMBOL(dquot_commit);

/*
 *	Release dquot
 */
int dquot_release(struct dquot *dquot)
{
	int ret = 0, ret2 = 0;
	struct quota_info *dqopt = sb_dqopt(dquot->dq_sb);

	mutex_lock(&dquot->dq_lock);
	/* Check whether we are not racing with some other dqget() */
	if (atomic_read(&dquot->dq_count) > 1)
		goto out_dqlock;
	mutex_lock(&dqopt->dqio_mutex);
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
	mutex_unlock(&dqopt->dqio_mutex);
out_dqlock:
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
			DEFINE_WAIT(wait);

			atomic_inc(&dquot->dq_count);
			prepare_to_wait(&dquot->dq_wait_unused, &wait,
					TASK_UNINTERRUPTIBLE);
			spin_unlock(&dq_list_lock);
			/* Once dqput() wakes us up, we know it's time to free
			 * the dquot.
			 * IMPORTANT: we rely on the fact that there is always
			 * at most one process waiting for dquot to free.
			 * Otherwise dq_count would be > 1 and we would never
			 * wake up.
			 */
			if (atomic_read(&dquot->dq_count) > 1)
				schedule();
			finish_wait(&dquot->dq_wait_unused, &wait);
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

	mutex_lock(&sb_dqopt(sb)->dqonoff_mutex);
	spin_lock(&dq_list_lock);
	list_for_each_entry(dquot, &inuse_list, dq_inuse) {
		if (!test_bit(DQ_ACTIVE_B, &dquot->dq_flags))
			continue;
		if (dquot->dq_sb != sb)
			continue;
		/* Now we have active dquot so we can just increase use count */
		atomic_inc(&dquot->dq_count);
		spin_unlock(&dq_list_lock);
		dqstats_inc(DQST_LOOKUPS);
		dqput(old_dquot);
		old_dquot = dquot;
		ret = fn(dquot, priv);
		if (ret < 0)
			goto out;
		spin_lock(&dq_list_lock);
		/* We are safe to continue now because our dquot could not
		 * be moved out of the inuse list while we hold the reference */
	}
	spin_unlock(&dq_list_lock);
out:
	dqput(old_dquot);
	mutex_unlock(&sb_dqopt(sb)->dqonoff_mutex);
	return ret;
}
EXPORT_SYMBOL(dquot_scan_active);

/* Write all dquot structures to quota files */
int dquot_writeback_dquots(struct super_block *sb, int type)
{
	struct list_head *dirty;
	struct dquot *dquot;
	struct quota_info *dqopt = sb_dqopt(sb);
	int cnt;
	int err, ret = 0;

	mutex_lock(&dqopt->dqonoff_mutex);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_active(sb, cnt))
			continue;
		spin_lock(&dq_list_lock);
		dirty = &dqopt->info[cnt].dqi_dirty_list;
		while (!list_empty(dirty)) {
			dquot = list_first_entry(dirty, struct dquot,
						 dq_dirty);
			/* Dirty and inactive can be only bad dquot... */
			if (!test_bit(DQ_ACTIVE_B, &dquot->dq_flags)) {
				clear_dquot_dirty(dquot);
				continue;
			}
			/* Now we have active dquot from which someone is
 			 * holding reference so we can safely just increase
			 * use count */
			atomic_inc(&dquot->dq_count);
			spin_unlock(&dq_list_lock);
			dqstats_inc(DQST_LOOKUPS);
			err = sb->dq_op->write_dquot(dquot);
			if (!ret && err)
				err = ret;
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
	mutex_unlock(&dqopt->dqonoff_mutex);

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
	if (sb->s_op->sync_fs)
		sb->s_op->sync_fs(sb, 1);
	sync_blockdev(sb->s_bdev);

	/*
	 * Now when everything is written we can discard the pagecache so
	 * that userspace sees the changes.
	 */
	mutex_lock(&dqopt->dqonoff_mutex);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		if (!sb_has_quota_active(sb, cnt))
			continue;
		mutex_lock(&dqopt->files[cnt]->i_mutex);
		truncate_inode_pages(&dqopt->files[cnt]->i_data, 0);
		mutex_unlock(&dqopt->files[cnt]->i_mutex);
	}
	mutex_unlock(&dqopt->dqonoff_mutex);

	return 0;
}
EXPORT_SYMBOL(dquot_quota_sync);

/* Free unused dquots from cache */
static void prune_dqcache(int count)
{
	struct list_head *head;
	struct dquot *dquot;

	head = free_dquots.prev;
	while (head != &free_dquots && count) {
		dquot = list_entry(head, struct dquot, dq_free);
		remove_dquot_hash(dquot);
		remove_free_dquot(dquot);
		remove_inuse(dquot);
		do_destroy_dquot(dquot);
		count--;
		head = free_dquots.prev;
	}
}

/*
 * This is called from kswapd when we think we need some
 * more memory
 */
static int shrink_dqcache_memory(struct shrinker *shrink,
				 struct shrink_control *sc)
{
	int nr = sc->nr_to_scan;

	if (nr) {
		spin_lock(&dq_list_lock);
		prune_dqcache(nr);
		spin_unlock(&dq_list_lock);
	}
	return ((unsigned)
		percpu_counter_read_positive(&dqstats.counter[DQST_FREE_DQUOTS])
		/100) * sysctl_vfs_cache_pressure;
}

static struct shrinker dqcache_shrinker = {
	.shrink = shrink_dqcache_memory,
	.seeks = DEFAULT_SEEKS,
};

/*
 * Put reference to dquot
 * NOTE: If you change this function please check whether dqput_blocks() works right...
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
			wake_up(&dquot->dq_wait_unused);
		spin_unlock(&dq_list_lock);
		return;
	}
	/* Need to release dquot? */
	if (test_bit(DQ_ACTIVE_B, &dquot->dq_flags) && dquot_dirty(dquot)) {
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
			spin_lock(&dq_list_lock);
			clear_dquot_dirty(dquot);
			spin_unlock(&dq_list_lock);
		}
		goto we_slept;
	}
	/* Clear flag in case dquot was inactive (something bad happened) */
	clear_dquot_dirty(dquot);
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
	init_waitqueue_head(&dquot->dq_wait_unused);
	dquot->dq_sb = sb;
	dquot->dq_id = make_kqid_invalid(type);
	atomic_set(&dquot->dq_count, 1);

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
	struct dquot *dquot = NULL, *empty = NULL;

        if (!sb_has_quota_active(sb, qid.type))
		return NULL;
we_slept:
	spin_lock(&dq_list_lock);
	spin_lock(&dq_state_lock);
	if (!sb_has_quota_active(sb, qid.type)) {
		spin_unlock(&dq_state_lock);
		spin_unlock(&dq_list_lock);
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
	if (!test_bit(DQ_ACTIVE_B, &dquot->dq_flags) &&
	    sb->dq_op->acquire_dquot(dquot) < 0) {
		dqput(dquot);
		dquot = NULL;
		goto out;
	}
#ifdef CONFIG_QUOTA_DEBUG
	BUG_ON(!dquot->dq_sb);	/* Has somebody invalidated entry under us? */
#endif
out:
	if (empty)
		do_destroy_dquot(empty);

	return dquot;
}
EXPORT_SYMBOL(dqget);

static int dqinit_needed(struct inode *inode, int type)
{
	int cnt;

	if (IS_NOQUOTA(inode))
		return 0;
	if (type != -1)
		return !inode->i_dquot[type];
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (!inode->i_dquot[cnt])
			return 1;
	return 0;
}

/* This routine is guarded by dqonoff_mutex mutex */
static void add_dquot_ref(struct super_block *sb, int type)
{
	struct inode *inode, *old_inode = NULL;
#ifdef CONFIG_QUOTA_DEBUG
	int reserved = 0;
#endif

	spin_lock(&inode_sb_list_lock);
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
		spin_unlock(&inode_sb_list_lock);

#ifdef CONFIG_QUOTA_DEBUG
		if (unlikely(inode_get_rsv_space(inode) > 0))
			reserved = 1;
#endif
		iput(old_inode);
		__dquot_initialize(inode, type);

		/*
		 * We hold a reference to 'inode' so it couldn't have been
		 * removed from s_inodes list while we dropped the
		 * inode_sb_list_lock We cannot iput the inode now as we can be
		 * holding the last reference and we cannot iput it under
		 * inode_sb_list_lock. So we keep the reference and iput it
		 * later.
		 */
		old_inode = inode;
		spin_lock(&inode_sb_list_lock);
	}
	spin_unlock(&inode_sb_list_lock);
	iput(old_inode);

#ifdef CONFIG_QUOTA_DEBUG
	if (reserved) {
		quota_error(sb, "Writes happened before quota was turned on "
			"thus quota information is probably inconsistent. "
			"Please run quotacheck(8)");
	}
#endif
}

/*
 * Return 0 if dqput() won't block.
 * (note that 1 doesn't necessarily mean blocking)
 */
static inline int dqput_blocks(struct dquot *dquot)
{
	if (atomic_read(&dquot->dq_count) <= 1)
		return 1;
	return 0;
}

/*
 * Remove references to dquots from inode and add dquot to list for freeing
 * if we have the last reference to dquot
 * We can't race with anybody because we hold dqptr_sem for writing...
 */
static int remove_inode_dquot_ref(struct inode *inode, int type,
				  struct list_head *tofree_head)
{
	struct dquot *dquot = inode->i_dquot[type];

	inode->i_dquot[type] = NULL;
	if (dquot) {
		if (dqput_blocks(dquot)) {
#ifdef CONFIG_QUOTA_DEBUG
			if (atomic_read(&dquot->dq_count) != 1)
				quota_error(inode->i_sb, "Adding dquot with "
					    "dq_count %d to dispose list",
					    atomic_read(&dquot->dq_count));
#endif
			spin_lock(&dq_list_lock);
			/* As dquot must have currently users it can't be on
			 * the free list... */
			list_add(&dquot->dq_free, tofree_head);
			spin_unlock(&dq_list_lock);
			return 1;
		}
		else
			dqput(dquot);   /* We have guaranteed we won't block */
	}
	return 0;
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
	int reserved = 0;

	spin_lock(&inode_sb_list_lock);
	list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
		/*
		 *  We have to scan also I_NEW inodes because they can already
		 *  have quota pointer initialized. Luckily, we need to touch
		 *  only quota pointers and these have separate locking
		 *  (dqptr_sem).
		 */
		if (!IS_NOQUOTA(inode)) {
			if (unlikely(inode_get_rsv_space(inode) > 0))
				reserved = 1;
			remove_inode_dquot_ref(inode, type, tofree_head);
		}
	}
	spin_unlock(&inode_sb_list_lock);
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
		down_write(&sb_dqopt(sb)->dqptr_sem);
		remove_dquot_ref(sb, type, &tofree_head);
		up_write(&sb_dqopt(sb)->dqptr_sem);
		put_dquot_list(&tofree_head);
	}
}

static inline void dquot_incr_inodes(struct dquot *dquot, qsize_t number)
{
	dquot->dq_dqb.dqb_curinodes += number;
}

static inline void dquot_incr_space(struct dquot *dquot, qsize_t number)
{
	dquot->dq_dqb.dqb_curspace += number;
}

static inline void dquot_resv_space(struct dquot *dquot, qsize_t number)
{
	dquot->dq_dqb.dqb_rsvspace += number;
}

/*
 * Claim reserved quota space
 */
static void dquot_claim_reserved_space(struct dquot *dquot, qsize_t number)
{
	if (dquot->dq_dqb.dqb_rsvspace < number) {
		WARN_ON_ONCE(1);
		number = dquot->dq_dqb.dqb_rsvspace;
	}
	dquot->dq_dqb.dqb_curspace += number;
	dquot->dq_dqb.dqb_rsvspace -= number;
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
}

static void dquot_decr_inodes(struct dquot *dquot, qsize_t number)
{
	if (sb_dqopt(dquot->dq_sb)->flags & DQUOT_NEGATIVE_USAGE ||
	    dquot->dq_dqb.dqb_curinodes >= number)
		dquot->dq_dqb.dqb_curinodes -= number;
	else
		dquot->dq_dqb.dqb_curinodes = 0;
	if (dquot->dq_dqb.dqb_curinodes <= dquot->dq_dqb.dqb_isoftlimit)
		dquot->dq_dqb.dqb_itime = (time_t) 0;
	clear_bit(DQ_INODES_B, &dquot->dq_flags);
}

static void dquot_decr_space(struct dquot *dquot, qsize_t number)
{
	if (sb_dqopt(dquot->dq_sb)->flags & DQUOT_NEGATIVE_USAGE ||
	    dquot->dq_dqb.dqb_curspace >= number)
		dquot->dq_dqb.dqb_curspace -= number;
	else
		dquot->dq_dqb.dqb_curspace = 0;
	if (dquot->dq_dqb.dqb_curspace <= dquot->dq_dqb.dqb_bsoftlimit)
		dquot->dq_dqb.dqb_btime = (time_t) 0;
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
		case PRJQUOTA:	/* Never taken... Just make gcc happy */
			return 0;
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
		!(info->dqi_flags & V1_DQF_RSQUASH));
}

/* needs dq_data_lock */
static int check_idq(struct dquot *dquot, qsize_t inodes,
		     struct dquot_warn *warn)
{
	qsize_t newinodes = dquot->dq_dqb.dqb_curinodes + inodes;

	if (!sb_has_quota_limits_enabled(dquot->dq_sb, dquot->dq_id.type) ||
	    test_bit(DQ_FAKE_B, &dquot->dq_flags))
		return 0;

	if (dquot->dq_dqb.dqb_ihardlimit &&
	    newinodes > dquot->dq_dqb.dqb_ihardlimit &&
            !ignore_hardlimit(dquot)) {
		prepare_warning(warn, dquot, QUOTA_NL_IHARDWARN);
		return -EDQUOT;
	}

	if (dquot->dq_dqb.dqb_isoftlimit &&
	    newinodes > dquot->dq_dqb.dqb_isoftlimit &&
	    dquot->dq_dqb.dqb_itime &&
	    get_seconds() >= dquot->dq_dqb.dqb_itime &&
            !ignore_hardlimit(dquot)) {
		prepare_warning(warn, dquot, QUOTA_NL_ISOFTLONGWARN);
		return -EDQUOT;
	}

	if (dquot->dq_dqb.dqb_isoftlimit &&
	    newinodes > dquot->dq_dqb.dqb_isoftlimit &&
	    dquot->dq_dqb.dqb_itime == 0) {
		prepare_warning(warn, dquot, QUOTA_NL_ISOFTWARN);
		dquot->dq_dqb.dqb_itime = get_seconds() +
		    sb_dqopt(dquot->dq_sb)->info[dquot->dq_id.type].dqi_igrace;
	}

	return 0;
}

/* needs dq_data_lock */
static int check_bdq(struct dquot *dquot, qsize_t space, int prealloc,
		     struct dquot_warn *warn)
{
	qsize_t tspace;
	struct super_block *sb = dquot->dq_sb;

	if (!sb_has_quota_limits_enabled(sb, dquot->dq_id.type) ||
	    test_bit(DQ_FAKE_B, &dquot->dq_flags))
		return 0;

	tspace = dquot->dq_dqb.dqb_curspace + dquot->dq_dqb.dqb_rsvspace
		+ space;

	if (dquot->dq_dqb.dqb_bhardlimit &&
	    tspace > dquot->dq_dqb.dqb_bhardlimit &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			prepare_warning(warn, dquot, QUOTA_NL_BHARDWARN);
		return -EDQUOT;
	}

	if (dquot->dq_dqb.dqb_bsoftlimit &&
	    tspace > dquot->dq_dqb.dqb_bsoftlimit &&
	    dquot->dq_dqb.dqb_btime &&
	    get_seconds() >= dquot->dq_dqb.dqb_btime &&
            !ignore_hardlimit(dquot)) {
		if (!prealloc)
			prepare_warning(warn, dquot, QUOTA_NL_BSOFTLONGWARN);
		return -EDQUOT;
	}

	if (dquot->dq_dqb.dqb_bsoftlimit &&
	    tspace > dquot->dq_dqb.dqb_bsoftlimit &&
	    dquot->dq_dqb.dqb_btime == 0) {
		if (!prealloc) {
			prepare_warning(warn, dquot, QUOTA_NL_BSOFTWARN);
			dquot->dq_dqb.dqb_btime = get_seconds() +
			    sb_dqopt(sb)->info[dquot->dq_id.type].dqi_bgrace;
		}
		else
			/*
			 * We don't allow preallocation to exceed softlimit so exceeding will
			 * be always printed
			 */
			return -EDQUOT;
	}

	return 0;
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
	if (test_bit(DQ_FAKE_B, &dquot->dq_flags) ||
	    dquot->dq_dqb.dqb_curspace <= dquot->dq_dqb.dqb_bsoftlimit)
		return QUOTA_NL_NOWARN;

	if (dquot->dq_dqb.dqb_curspace - space <= dquot->dq_dqb.dqb_bsoftlimit)
		return QUOTA_NL_BSOFTBELOW;
	if (dquot->dq_dqb.dqb_curspace >= dquot->dq_dqb.dqb_bhardlimit &&
	    dquot->dq_dqb.dqb_curspace - space < dquot->dq_dqb.dqb_bhardlimit)
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
 * We do things in a bit complicated way but by that we avoid calling
 * dqget() and thus filesystem callbacks under dqptr_sem.
 *
 * It is better to call this function outside of any transaction as it
 * might need a lot of space in journal for dquot structure allocation.
 */
static void __dquot_initialize(struct inode *inode, int type)
{
	int cnt;
	struct dquot *got[MAXQUOTAS];
	struct super_block *sb = inode->i_sb;
	qsize_t rsv;

	/* First test before acquiring mutex - solves deadlocks when we
         * re-enter the quota code and are already holding the mutex */
	if (!dquot_active(inode))
		return;

	/* First get references to structures we might need. */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		struct kqid qid;
		got[cnt] = NULL;
		if (type != -1 && cnt != type)
			continue;
		switch (cnt) {
		case USRQUOTA:
			qid = make_kqid_uid(inode->i_uid);
			break;
		case GRPQUOTA:
			qid = make_kqid_gid(inode->i_gid);
			break;
		}
		got[cnt] = dqget(sb, qid);
	}

	down_write(&sb_dqopt(sb)->dqptr_sem);
	if (IS_NOQUOTA(inode))
		goto out_err;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;
		/* Avoid races with quotaoff() */
		if (!sb_has_quota_active(sb, cnt))
			continue;
		/* We could race with quotaon or dqget() could have failed */
		if (!got[cnt])
			continue;
		if (!inode->i_dquot[cnt]) {
			inode->i_dquot[cnt] = got[cnt];
			got[cnt] = NULL;
			/*
			 * Make quota reservation system happy if someone
			 * did a write before quota was turned on
			 */
			rsv = inode_get_rsv_space(inode);
			if (unlikely(rsv))
				dquot_resv_space(inode->i_dquot[cnt], rsv);
		}
	}
out_err:
	up_write(&sb_dqopt(sb)->dqptr_sem);
	/* Drop unused references */
	dqput_all(got);
}

void dquot_initialize(struct inode *inode)
{
	__dquot_initialize(inode, -1);
}
EXPORT_SYMBOL(dquot_initialize);

/*
 * 	Release all quotas referenced by inode
 */
static void __dquot_drop(struct inode *inode)
{
	int cnt;
	struct dquot *put[MAXQUOTAS];

	down_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		put[cnt] = inode->i_dquot[cnt];
		inode->i_dquot[cnt] = NULL;
	}
	up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	dqput_all(put);
}

void dquot_drop(struct inode *inode)
{
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
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt])
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

void inode_add_rsv_space(struct inode *inode, qsize_t number)
{
	spin_lock(&inode->i_lock);
	*inode_reserved_space(inode) += number;
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(inode_add_rsv_space);

void inode_claim_rsv_space(struct inode *inode, qsize_t number)
{
	spin_lock(&inode->i_lock);
	*inode_reserved_space(inode) -= number;
	__inode_add_bytes(inode, number);
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(inode_claim_rsv_space);

void inode_sub_rsv_space(struct inode *inode, qsize_t number)
{
	spin_lock(&inode->i_lock);
	*inode_reserved_space(inode) -= number;
	spin_unlock(&inode->i_lock);
}
EXPORT_SYMBOL(inode_sub_rsv_space);

static qsize_t inode_get_rsv_space(struct inode *inode)
{
	qsize_t ret;

	if (!inode->i_sb->dq_op->get_reserved_space)
		return 0;
	spin_lock(&inode->i_lock);
	ret = *inode_reserved_space(inode);
	spin_unlock(&inode->i_lock);
	return ret;
}

static void inode_incr_space(struct inode *inode, qsize_t number,
				int reserve)
{
	if (reserve)
		inode_add_rsv_space(inode, number);
	else
		inode_add_bytes(inode, number);
}

static void inode_decr_space(struct inode *inode, qsize_t number, int reserve)
{
	if (reserve)
		inode_sub_rsv_space(inode, number);
	else
		inode_sub_bytes(inode, number);
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
	int cnt, ret = 0;
	struct dquot_warn warn[MAXQUOTAS];
	struct dquot **dquots = inode->i_dquot;
	int reserve = flags & DQUOT_SPACE_RESERVE;

	/*
	 * First test before acquiring mutex - solves deadlocks when we
	 * re-enter the quota code and are already holding the mutex
	 */
	if (!dquot_active(inode)) {
		inode_incr_space(inode, number, reserve);
		goto out;
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		warn[cnt].w_type = QUOTA_NL_NOWARN;

	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	spin_lock(&dq_data_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!dquots[cnt])
			continue;
		ret = check_bdq(dquots[cnt], number,
				!(flags & DQUOT_SPACE_WARN), &warn[cnt]);
		if (ret && !(flags & DQUOT_SPACE_NOFAIL)) {
			spin_unlock(&dq_data_lock);
			goto out_flush_warn;
		}
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!dquots[cnt])
			continue;
		if (reserve)
			dquot_resv_space(dquots[cnt], number);
		else
			dquot_incr_space(dquots[cnt], number);
	}
	inode_incr_space(inode, number, reserve);
	spin_unlock(&dq_data_lock);

	if (reserve)
		goto out_flush_warn;
	mark_all_dquot_dirty(dquots);
out_flush_warn:
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	flush_warnings(warn);
out:
	return ret;
}
EXPORT_SYMBOL(__dquot_alloc_space);

/*
 * This operation can block, but only after everything is updated
 */
int dquot_alloc_inode(const struct inode *inode)
{
	int cnt, ret = 0;
	struct dquot_warn warn[MAXQUOTAS];
	struct dquot * const *dquots = inode->i_dquot;

	/* First test before acquiring mutex - solves deadlocks when we
         * re-enter the quota code and are already holding the mutex */
	if (!dquot_active(inode))
		return 0;
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		warn[cnt].w_type = QUOTA_NL_NOWARN;
	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	spin_lock(&dq_data_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!dquots[cnt])
			continue;
		ret = check_idq(dquots[cnt], 1, &warn[cnt]);
		if (ret)
			goto warn_put_all;
	}

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!dquots[cnt])
			continue;
		dquot_incr_inodes(dquots[cnt], 1);
	}

warn_put_all:
	spin_unlock(&dq_data_lock);
	if (ret == 0)
		mark_all_dquot_dirty(dquots);
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	flush_warnings(warn);
	return ret;
}
EXPORT_SYMBOL(dquot_alloc_inode);

/*
 * Convert in-memory reserved quotas to real consumed quotas
 */
int dquot_claim_space_nodirty(struct inode *inode, qsize_t number)
{
	int cnt;

	if (!dquot_active(inode)) {
		inode_claim_rsv_space(inode, number);
		return 0;
	}

	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	spin_lock(&dq_data_lock);
	/* Claim reserved quotas to allocated quotas */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (inode->i_dquot[cnt])
			dquot_claim_reserved_space(inode->i_dquot[cnt],
							number);
	}
	/* Update inode bytes */
	inode_claim_rsv_space(inode, number);
	spin_unlock(&dq_data_lock);
	mark_all_dquot_dirty(inode->i_dquot);
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	return 0;
}
EXPORT_SYMBOL(dquot_claim_space_nodirty);

/*
 * This operation can block, but only after everything is updated
 */
void __dquot_free_space(struct inode *inode, qsize_t number, int flags)
{
	unsigned int cnt;
	struct dquot_warn warn[MAXQUOTAS];
	struct dquot **dquots = inode->i_dquot;
	int reserve = flags & DQUOT_SPACE_RESERVE;

	/* First test before acquiring mutex - solves deadlocks when we
         * re-enter the quota code and are already holding the mutex */
	if (!dquot_active(inode)) {
		inode_decr_space(inode, number, reserve);
		return;
	}

	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	spin_lock(&dq_data_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		int wtype;

		warn[cnt].w_type = QUOTA_NL_NOWARN;
		if (!dquots[cnt])
			continue;
		wtype = info_bdq_free(dquots[cnt], number);
		if (wtype != QUOTA_NL_NOWARN)
			prepare_warning(&warn[cnt], dquots[cnt], wtype);
		if (reserve)
			dquot_free_reserved_space(dquots[cnt], number);
		else
			dquot_decr_space(dquots[cnt], number);
	}
	inode_decr_space(inode, number, reserve);
	spin_unlock(&dq_data_lock);

	if (reserve)
		goto out_unlock;
	mark_all_dquot_dirty(dquots);
out_unlock:
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	flush_warnings(warn);
}
EXPORT_SYMBOL(__dquot_free_space);

/*
 * This operation can block, but only after everything is updated
 */
void dquot_free_inode(const struct inode *inode)
{
	unsigned int cnt;
	struct dquot_warn warn[MAXQUOTAS];
	struct dquot * const *dquots = inode->i_dquot;

	/* First test before acquiring mutex - solves deadlocks when we
         * re-enter the quota code and are already holding the mutex */
	if (!dquot_active(inode))
		return;

	down_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
	spin_lock(&dq_data_lock);
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		int wtype;

		warn[cnt].w_type = QUOTA_NL_NOWARN;
		if (!dquots[cnt])
			continue;
		wtype = info_idq_free(dquots[cnt], 1);
		if (wtype != QUOTA_NL_NOWARN)
			prepare_warning(&warn[cnt], dquots[cnt], wtype);
		dquot_decr_inodes(dquots[cnt], 1);
	}
	spin_unlock(&dq_data_lock);
	mark_all_dquot_dirty(dquots);
	up_read(&sb_dqopt(inode->i_sb)->dqptr_sem);
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
 */
int __dquot_transfer(struct inode *inode, struct dquot **transfer_to)
{
	qsize_t space, cur_space;
	qsize_t rsv_space = 0;
	struct dquot *transfer_from[MAXQUOTAS] = {};
	int cnt, ret = 0;
	char is_valid[MAXQUOTAS] = {};
	struct dquot_warn warn_to[MAXQUOTAS];
	struct dquot_warn warn_from_inodes[MAXQUOTAS];
	struct dquot_warn warn_from_space[MAXQUOTAS];

	/* First test before acquiring mutex - solves deadlocks when we
         * re-enter the quota code and are already holding the mutex */
	if (IS_NOQUOTA(inode))
		return 0;
	/* Initialize the arrays */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		warn_to[cnt].w_type = QUOTA_NL_NOWARN;
		warn_from_inodes[cnt].w_type = QUOTA_NL_NOWARN;
		warn_from_space[cnt].w_type = QUOTA_NL_NOWARN;
	}
	down_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	if (IS_NOQUOTA(inode)) {	/* File without quota accounting? */
		up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
		return 0;
	}
	spin_lock(&dq_data_lock);
	cur_space = inode_get_bytes(inode);
	rsv_space = inode_get_rsv_space(inode);
	space = cur_space + rsv_space;
	/* Build the transfer_from list and check the limits */
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
		transfer_from[cnt] = inode->i_dquot[cnt];
		ret = check_idq(transfer_to[cnt], 1, &warn_to[cnt]);
		if (ret)
			goto over_quota;
		ret = check_bdq(transfer_to[cnt], space, 0, &warn_to[cnt]);
		if (ret)
			goto over_quota;
	}

	/*
	 * Finally perform the needed transfer from transfer_from to transfer_to
	 */
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (!is_valid[cnt])
			continue;
		/* Due to IO error we might not have transfer_from[] structure */
		if (transfer_from[cnt]) {
			int wtype;
			wtype = info_idq_free(transfer_from[cnt], 1);
			if (wtype != QUOTA_NL_NOWARN)
				prepare_warning(&warn_from_inodes[cnt],
						transfer_from[cnt], wtype);
			wtype = info_bdq_free(transfer_from[cnt], space);
			if (wtype != QUOTA_NL_NOWARN)
				prepare_warning(&warn_from_space[cnt],
						transfer_from[cnt], wtype);
			dquot_decr_inodes(transfer_from[cnt], 1);
			dquot_decr_space(transfer_from[cnt], cur_space);
			dquot_free_reserved_space(transfer_from[cnt],
						  rsv_space);
		}

		dquot_incr_inodes(transfer_to[cnt], 1);
		dquot_incr_space(transfer_to[cnt], cur_space);
		dquot_resv_space(transfer_to[cnt], rsv_space);

		inode->i_dquot[cnt] = transfer_to[cnt];
	}
	spin_unlock(&dq_data_lock);
	up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);

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
	spin_unlock(&dq_data_lock);
	up_write(&sb_dqopt(inode->i_sb)->dqptr_sem);
	flush_warnings(warn_to);
	return ret;
}
EXPORT_SYMBOL(__dquot_transfer);

/* Wrapper for transferring ownership of an inode for uid/gid only
 * Called from FSXXX_setattr()
 */
int dquot_transfer(struct inode *inode, struct iattr *iattr)
{
	struct dquot *transfer_to[MAXQUOTAS] = {};
	struct super_block *sb = inode->i_sb;
	int ret;

	if (!dquot_active(inode))
		return 0;

	if (iattr->ia_valid & ATTR_UID && !uid_eq(iattr->ia_uid, inode->i_uid))
		transfer_to[USRQUOTA] = dqget(sb, make_kqid_uid(iattr->ia_uid));
	if (iattr->ia_valid & ATTR_GID && !gid_eq(iattr->ia_gid, inode->i_gid))
		transfer_to[GRPQUOTA] = dqget(sb, make_kqid_gid(iattr->ia_gid));

	ret = __dquot_transfer(inode, transfer_to);
	dqput_all(transfer_to);
	return ret;
}
EXPORT_SYMBOL(dquot_transfer);

/*
 * Write info of quota file to disk
 */
int dquot_commit_info(struct super_block *sb, int type)
{
	int ret;
	struct quota_info *dqopt = sb_dqopt(sb);

	mutex_lock(&dqopt->dqio_mutex);
	ret = dqopt->ops[type]->write_file_info(sb, type);
	mutex_unlock(&dqopt->dqio_mutex);
	return ret;
}
EXPORT_SYMBOL(dquot_commit_info);

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
		dquot_initialize(inode);
	return error;
}
EXPORT_SYMBOL(dquot_file_open);

/*
 * Turn quota off on a device. type == -1 ==> quotaoff for all types (umount)
 */
int dquot_disable(struct super_block *sb, int type, unsigned int flags)
{
	int cnt, ret = 0;
	struct quota_info *dqopt = sb_dqopt(sb);
	struct inode *toputinode[MAXQUOTAS];

	/* Cannot turn off usage accounting without turning off limits, or
	 * suspend quotas and simultaneously turn quotas off. */
	if ((flags & DQUOT_USAGE_ENABLED && !(flags & DQUOT_LIMITS_ENABLED))
	    || (flags & DQUOT_SUSPENDED && flags & (DQUOT_LIMITS_ENABLED |
	    DQUOT_USAGE_ENABLED)))
		return -EINVAL;

	/* We need to serialize quota_off() for device */
	mutex_lock(&dqopt->dqonoff_mutex);

	/*
	 * Skip everything if there's nothing to do. We have to do this because
	 * sometimes we are called when fill_super() failed and calling
	 * sync_fs() in such cases does no good.
	 */
	if (!sb_any_quota_loaded(sb)) {
		mutex_unlock(&dqopt->dqonoff_mutex);
		return 0;
	}
	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		toputinode[cnt] = NULL;
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
				iput(dqopt->files[cnt]);
				dqopt->files[cnt] = NULL;
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

		toputinode[cnt] = dqopt->files[cnt];
		if (!sb_has_quota_loaded(sb, cnt))
			dqopt->files[cnt] = NULL;
		dqopt->info[cnt].dqi_flags = 0;
		dqopt->info[cnt].dqi_igrace = 0;
		dqopt->info[cnt].dqi_bgrace = 0;
		dqopt->ops[cnt] = NULL;
	}
	mutex_unlock(&dqopt->dqonoff_mutex);

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
		if (toputinode[cnt]) {
			mutex_lock(&dqopt->dqonoff_mutex);
			/* If quota was reenabled in the meantime, we have
			 * nothing to do */
			if (!sb_has_quota_loaded(sb, cnt)) {
				mutex_lock(&toputinode[cnt]->i_mutex);
				toputinode[cnt]->i_flags &= ~(S_IMMUTABLE |
				  S_NOATIME | S_NOQUOTA);
				truncate_inode_pages(&toputinode[cnt]->i_data,
						     0);
				mutex_unlock(&toputinode[cnt]->i_mutex);
				mark_inode_dirty_sync(toputinode[cnt]);
			}
			mutex_unlock(&dqopt->dqonoff_mutex);
		}
	if (sb->s_bdev)
		invalidate_bdev(sb->s_bdev);
put_inodes:
	for (cnt = 0; cnt < MAXQUOTAS; cnt++)
		if (toputinode[cnt]) {
			/* On remount RO, we keep the inode pointer so that we
			 * can reenable quota on the subsequent remount RW. We
			 * have to check 'flags' variable and not use sb_has_
			 * function because another quotaon / quotaoff could
			 * change global state before we got here. We refuse
			 * to suspend quotas when there is pending delete on
			 * the quota file... */
			if (!(flags & DQUOT_SUSPENDED))
				iput(toputinode[cnt]);
			else if (!toputinode[cnt]->i_nlink)
				ret = -EBUSY;
		}
	return ret;
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

/*
 * Helper function to turn quotas on when we already have the inode of
 * quota file and no quota information is loaded.
 */
static int vfs_load_quota_inode(struct inode *inode, int type, int format_id,
	unsigned int flags)
{
	struct quota_format_type *fmt = find_quota_format(format_id);
	struct super_block *sb = inode->i_sb;
	struct quota_info *dqopt = sb_dqopt(sb);
	int error;
	int oldflags = -1;

	if (!fmt)
		return -ESRCH;
	if (!S_ISREG(inode->i_mode)) {
		error = -EACCES;
		goto out_fmt;
	}
	if (IS_RDONLY(inode)) {
		error = -EROFS;
		goto out_fmt;
	}
	if (!sb->s_op->quota_write || !sb->s_op->quota_read) {
		error = -EINVAL;
		goto out_fmt;
	}
	/* Usage always has to be set... */
	if (!(flags & DQUOT_USAGE_ENABLED)) {
		error = -EINVAL;
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
	mutex_lock(&dqopt->dqonoff_mutex);
	if (sb_has_quota_loaded(sb, type)) {
		error = -EBUSY;
		goto out_lock;
	}

	if (!(dqopt->flags & DQUOT_QUOTA_SYS_FILE)) {
		/* We don't want quota and atime on quota files (deadlocks
		 * possible) Also nobody should write to the file - we use
		 * special IO operations which ignore the immutable bit. */
		mutex_lock(&inode->i_mutex);
		oldflags = inode->i_flags & (S_NOATIME | S_IMMUTABLE |
					     S_NOQUOTA);
		inode->i_flags |= S_NOQUOTA | S_NOATIME | S_IMMUTABLE;
		mutex_unlock(&inode->i_mutex);
		/*
		 * When S_NOQUOTA is set, remove dquot references as no more
		 * references can be added
		 */
		__dquot_drop(inode);
	}

	error = -EIO;
	dqopt->files[type] = igrab(inode);
	if (!dqopt->files[type])
		goto out_lock;
	error = -EINVAL;
	if (!fmt->qf_ops->check_quota_file(sb, type))
		goto out_file_init;

	dqopt->ops[type] = fmt->qf_ops;
	dqopt->info[type].dqi_format = fmt;
	dqopt->info[type].dqi_fmt_id = format_id;
	INIT_LIST_HEAD(&dqopt->info[type].dqi_dirty_list);
	mutex_lock(&dqopt->dqio_mutex);
	error = dqopt->ops[type]->read_file_info(sb, type);
	if (error < 0) {
		mutex_unlock(&dqopt->dqio_mutex);
		goto out_file_init;
	}
	if (dqopt->flags & DQUOT_QUOTA_SYS_FILE)
		dqopt->info[type].dqi_flags |= DQF_SYS_FILE;
	mutex_unlock(&dqopt->dqio_mutex);
	spin_lock(&dq_state_lock);
	dqopt->flags |= dquot_state_flag(flags, type);
	spin_unlock(&dq_state_lock);

	add_dquot_ref(sb, type);
	mutex_unlock(&dqopt->dqonoff_mutex);

	return 0;

out_file_init:
	dqopt->files[type] = NULL;
	iput(inode);
out_lock:
	if (oldflags != -1) {
		mutex_lock(&inode->i_mutex);
		/* Set the flags back (in the case of accidental quotaon()
		 * on a wrong file we don't want to mess up the flags) */
		inode->i_flags &= ~(S_NOATIME | S_NOQUOTA | S_IMMUTABLE);
		inode->i_flags |= oldflags;
		mutex_unlock(&inode->i_mutex);
	}
	mutex_unlock(&dqopt->dqonoff_mutex);
out_fmt:
	put_quota_format(fmt);

	return error; 
}

/* Reenable quotas on remount RW */
int dquot_resume(struct super_block *sb, int type)
{
	struct quota_info *dqopt = sb_dqopt(sb);
	struct inode *inode;
	int ret = 0, cnt;
	unsigned int flags;

	for (cnt = 0; cnt < MAXQUOTAS; cnt++) {
		if (type != -1 && cnt != type)
			continue;

		mutex_lock(&dqopt->dqonoff_mutex);
		if (!sb_has_quota_suspended(sb, cnt)) {
			mutex_unlock(&dqopt->dqonoff_mutex);
			continue;
		}
		inode = dqopt->files[cnt];
		dqopt->files[cnt] = NULL;
		spin_lock(&dq_state_lock);
		flags = dqopt->flags & dquot_state_flag(DQUOT_USAGE_ENABLED |
							DQUOT_LIMITS_ENABLED,
							cnt);
		dqopt->flags &= ~dquot_state_flag(DQUOT_STATE_FLAGS, cnt);
		spin_unlock(&dq_state_lock);
		mutex_unlock(&dqopt->dqonoff_mutex);

		flags = dquot_generic_flag(flags, cnt);
		ret = vfs_load_quota_inode(inode, cnt,
				dqopt->info[cnt].dqi_fmt_id, flags);
		iput(inode);
	}

	return ret;
}
EXPORT_SYMBOL(dquot_resume);

int dquot_quota_on(struct super_block *sb, int type, int format_id,
		   struct path *path)
{
	int error = security_quota_on(path->dentry);
	if (error)
		return error;
	/* Quota file not on the same filesystem? */
	if (path->dentry->d_sb != sb)
		error = -EXDEV;
	else
		error = vfs_load_quota_inode(path->dentry->d_inode, type,
					     format_id, DQUOT_USAGE_ENABLED |
					     DQUOT_LIMITS_ENABLED);
	return error;
}
EXPORT_SYMBOL(dquot_quota_on);

/*
 * More powerful function for turning on quotas allowing setting
 * of individual quota flags
 */
int dquot_enable(struct inode *inode, int type, int format_id,
		 unsigned int flags)
{
	int ret = 0;
	struct super_block *sb = inode->i_sb;
	struct quota_info *dqopt = sb_dqopt(sb);

	/* Just unsuspend quotas? */
	BUG_ON(flags & DQUOT_SUSPENDED);

	if (!flags)
		return 0;
	/* Just updating flags needed? */
	if (sb_has_quota_loaded(sb, type)) {
		mutex_lock(&dqopt->dqonoff_mutex);
		/* Now do a reliable test... */
		if (!sb_has_quota_loaded(sb, type)) {
			mutex_unlock(&dqopt->dqonoff_mutex);
			goto load_quota;
		}
		if (flags & DQUOT_USAGE_ENABLED &&
		    sb_has_quota_usage_enabled(sb, type)) {
			ret = -EBUSY;
			goto out_lock;
		}
		if (flags & DQUOT_LIMITS_ENABLED &&
		    sb_has_quota_limits_enabled(sb, type)) {
			ret = -EBUSY;
			goto out_lock;
		}
		spin_lock(&dq_state_lock);
		sb_dqopt(sb)->flags |= dquot_state_flag(flags, type);
		spin_unlock(&dq_state_lock);
out_lock:
		mutex_unlock(&dqopt->dqonoff_mutex);
		return ret;
	}

load_quota:
	return vfs_load_quota_inode(inode, type, format_id, flags);
}
EXPORT_SYMBOL(dquot_enable);

/*
 * This function is used when filesystem needs to initialize quotas
 * during mount time.
 */
int dquot_quota_on_mount(struct super_block *sb, char *qf_name,
		int format_id, int type)
{
	struct dentry *dentry;
	int error;

	mutex_lock(&sb->s_root->d_inode->i_mutex);
	dentry = lookup_one_len(qf_name, sb->s_root, strlen(qf_name));
	mutex_unlock(&sb->s_root->d_inode->i_mutex);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	if (!dentry->d_inode) {
		error = -ENOENT;
		goto out;
	}

	error = security_quota_on(dentry);
	if (!error)
		error = vfs_load_quota_inode(dentry->d_inode, type, format_id,
				DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED);

out:
	dput(dentry);
	return error;
}
EXPORT_SYMBOL(dquot_quota_on_mount);

static inline qsize_t qbtos(qsize_t blocks)
{
	return blocks << QIF_DQBLKSIZE_BITS;
}

static inline qsize_t stoqb(qsize_t space)
{
	return (space + QIF_DQBLKSIZE - 1) >> QIF_DQBLKSIZE_BITS;
}

/* Generic routine for getting common part of quota structure */
static void do_get_dqblk(struct dquot *dquot, struct fs_disk_quota *di)
{
	struct mem_dqblk *dm = &dquot->dq_dqb;

	memset(di, 0, sizeof(*di));
	di->d_version = FS_DQUOT_VERSION;
	di->d_flags = dquot->dq_id.type == USRQUOTA ?
			FS_USER_QUOTA : FS_GROUP_QUOTA;
	di->d_id = from_kqid_munged(current_user_ns(), dquot->dq_id);

	spin_lock(&dq_data_lock);
	di->d_blk_hardlimit = stoqb(dm->dqb_bhardlimit);
	di->d_blk_softlimit = stoqb(dm->dqb_bsoftlimit);
	di->d_ino_hardlimit = dm->dqb_ihardlimit;
	di->d_ino_softlimit = dm->dqb_isoftlimit;
	di->d_bcount = dm->dqb_curspace + dm->dqb_rsvspace;
	di->d_icount = dm->dqb_curinodes;
	di->d_btimer = dm->dqb_btime;
	di->d_itimer = dm->dqb_itime;
	spin_unlock(&dq_data_lock);
}

int dquot_get_dqblk(struct super_block *sb, struct kqid qid,
		    struct fs_disk_quota *di)
{
	struct dquot *dquot;

	dquot = dqget(sb, qid);
	if (!dquot)
		return -ESRCH;
	do_get_dqblk(dquot, di);
	dqput(dquot);

	return 0;
}
EXPORT_SYMBOL(dquot_get_dqblk);

#define VFS_FS_DQ_MASK \
	(FS_DQ_BCOUNT | FS_DQ_BSOFT | FS_DQ_BHARD | \
	 FS_DQ_ICOUNT | FS_DQ_ISOFT | FS_DQ_IHARD | \
	 FS_DQ_BTIMER | FS_DQ_ITIMER)

/* Generic routine for setting common part of quota structure */
static int do_set_dqblk(struct dquot *dquot, struct fs_disk_quota *di)
{
	struct mem_dqblk *dm = &dquot->dq_dqb;
	int check_blim = 0, check_ilim = 0;
	struct mem_dqinfo *dqi = &sb_dqopt(dquot->dq_sb)->info[dquot->dq_id.type];

	if (di->d_fieldmask & ~VFS_FS_DQ_MASK)
		return -EINVAL;

	if (((di->d_fieldmask & FS_DQ_BSOFT) &&
	     (di->d_blk_softlimit > dqi->dqi_maxblimit)) ||
	    ((di->d_fieldmask & FS_DQ_BHARD) &&
	     (di->d_blk_hardlimit > dqi->dqi_maxblimit)) ||
	    ((di->d_fieldmask & FS_DQ_ISOFT) &&
	     (di->d_ino_softlimit > dqi->dqi_maxilimit)) ||
	    ((di->d_fieldmask & FS_DQ_IHARD) &&
	     (di->d_ino_hardlimit > dqi->dqi_maxilimit)))
		return -ERANGE;

	spin_lock(&dq_data_lock);
	if (di->d_fieldmask & FS_DQ_BCOUNT) {
		dm->dqb_curspace = di->d_bcount - dm->dqb_rsvspace;
		check_blim = 1;
		set_bit(DQ_LASTSET_B + QIF_SPACE_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & FS_DQ_BSOFT)
		dm->dqb_bsoftlimit = qbtos(di->d_blk_softlimit);
	if (di->d_fieldmask & FS_DQ_BHARD)
		dm->dqb_bhardlimit = qbtos(di->d_blk_hardlimit);
	if (di->d_fieldmask & (FS_DQ_BSOFT | FS_DQ_BHARD)) {
		check_blim = 1;
		set_bit(DQ_LASTSET_B + QIF_BLIMITS_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & FS_DQ_ICOUNT) {
		dm->dqb_curinodes = di->d_icount;
		check_ilim = 1;
		set_bit(DQ_LASTSET_B + QIF_INODES_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & FS_DQ_ISOFT)
		dm->dqb_isoftlimit = di->d_ino_softlimit;
	if (di->d_fieldmask & FS_DQ_IHARD)
		dm->dqb_ihardlimit = di->d_ino_hardlimit;
	if (di->d_fieldmask & (FS_DQ_ISOFT | FS_DQ_IHARD)) {
		check_ilim = 1;
		set_bit(DQ_LASTSET_B + QIF_ILIMITS_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & FS_DQ_BTIMER) {
		dm->dqb_btime = di->d_btimer;
		check_blim = 1;
		set_bit(DQ_LASTSET_B + QIF_BTIME_B, &dquot->dq_flags);
	}

	if (di->d_fieldmask & FS_DQ_ITIMER) {
		dm->dqb_itime = di->d_itimer;
		check_ilim = 1;
		set_bit(DQ_LASTSET_B + QIF_ITIME_B, &dquot->dq_flags);
	}

	if (check_blim) {
		if (!dm->dqb_bsoftlimit ||
		    dm->dqb_curspace < dm->dqb_bsoftlimit) {
			dm->dqb_btime = 0;
			clear_bit(DQ_BLKS_B, &dquot->dq_flags);
		} else if (!(di->d_fieldmask & FS_DQ_BTIMER))
			/* Set grace only if user hasn't provided his own... */
			dm->dqb_btime = get_seconds() + dqi->dqi_bgrace;
	}
	if (check_ilim) {
		if (!dm->dqb_isoftlimit ||
		    dm->dqb_curinodes < dm->dqb_isoftlimit) {
			dm->dqb_itime = 0;
			clear_bit(DQ_INODES_B, &dquot->dq_flags);
		} else if (!(di->d_fieldmask & FS_DQ_ITIMER))
			/* Set grace only if user hasn't provided his own... */
			dm->dqb_itime = get_seconds() + dqi->dqi_igrace;
	}
	if (dm->dqb_bhardlimit || dm->dqb_bsoftlimit || dm->dqb_ihardlimit ||
	    dm->dqb_isoftlimit)
		clear_bit(DQ_FAKE_B, &dquot->dq_flags);
	else
		set_bit(DQ_FAKE_B, &dquot->dq_flags);
	spin_unlock(&dq_data_lock);
	mark_dquot_dirty(dquot);

	return 0;
}

int dquot_set_dqblk(struct super_block *sb, struct kqid qid,
		  struct fs_disk_quota *di)
{
	struct dquot *dquot;
	int rc;

	dquot = dqget(sb, qid);
	if (!dquot) {
		rc = -ESRCH;
		goto out;
	}
	rc = do_set_dqblk(dquot, di);
	dqput(dquot);
out:
	return rc;
}
EXPORT_SYMBOL(dquot_set_dqblk);

/* Generic routine for getting common part of quota file information */
int dquot_get_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii)
{
	struct mem_dqinfo *mi;
  
	mutex_lock(&sb_dqopt(sb)->dqonoff_mutex);
	if (!sb_has_quota_active(sb, type)) {
		mutex_unlock(&sb_dqopt(sb)->dqonoff_mutex);
		return -ESRCH;
	}
	mi = sb_dqopt(sb)->info + type;
	spin_lock(&dq_data_lock);
	ii->dqi_bgrace = mi->dqi_bgrace;
	ii->dqi_igrace = mi->dqi_igrace;
	ii->dqi_flags = mi->dqi_flags & DQF_GETINFO_MASK;
	ii->dqi_valid = IIF_ALL;
	spin_unlock(&dq_data_lock);
	mutex_unlock(&sb_dqopt(sb)->dqonoff_mutex);
	return 0;
}
EXPORT_SYMBOL(dquot_get_dqinfo);

/* Generic routine for setting common part of quota file information */
int dquot_set_dqinfo(struct super_block *sb, int type, struct if_dqinfo *ii)
{
	struct mem_dqinfo *mi;
	int err = 0;

	mutex_lock(&sb_dqopt(sb)->dqonoff_mutex);
	if (!sb_has_quota_active(sb, type)) {
		err = -ESRCH;
		goto out;
	}
	mi = sb_dqopt(sb)->info + type;
	spin_lock(&dq_data_lock);
	if (ii->dqi_valid & IIF_BGRACE)
		mi->dqi_bgrace = ii->dqi_bgrace;
	if (ii->dqi_valid & IIF_IGRACE)
		mi->dqi_igrace = ii->dqi_igrace;
	if (ii->dqi_valid & IIF_FLAGS)
		mi->dqi_flags = (mi->dqi_flags & ~DQF_SETINFO_MASK) |
				(ii->dqi_flags & DQF_SETINFO_MASK);
	spin_unlock(&dq_data_lock);
	mark_info_dirty(sb, type);
	/* Force write to disk */
	sb->dq_op->write_info(sb, type);
out:
	mutex_unlock(&sb_dqopt(sb)->dqonoff_mutex);
	return err;
}
EXPORT_SYMBOL(dquot_set_dqinfo);

const struct quotactl_ops dquot_quotactl_ops = {
	.quota_on	= dquot_quota_on,
	.quota_off	= dquot_quota_off,
	.quota_sync	= dquot_quota_sync,
	.get_info	= dquot_get_dqinfo,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.set_dqblk	= dquot_set_dqblk
};
EXPORT_SYMBOL(dquot_quotactl_ops);

static int do_proc_dqstats(struct ctl_table *table, int write,
		     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	unsigned int type = (int *)table->data - dqstats.stat;

	/* Update global table */
	dqstats.stat[type] =
			percpu_counter_sum_positive(&dqstats.counter[type]);
	return proc_dointvec(table, write, buffer, lenp, ppos);
}

static ctl_table fs_dqstats_table[] = {
	{
		.procname	= "lookups",
		.data		= &dqstats.stat[DQST_LOOKUPS],
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "drops",
		.data		= &dqstats.stat[DQST_DROPS],
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "reads",
		.data		= &dqstats.stat[DQST_READS],
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "writes",
		.data		= &dqstats.stat[DQST_WRITES],
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "cache_hits",
		.data		= &dqstats.stat[DQST_CACHE_HITS],
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "allocated_dquots",
		.data		= &dqstats.stat[DQST_ALLOC_DQUOTS],
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "free_dquots",
		.data		= &dqstats.stat[DQST_FREE_DQUOTS],
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= do_proc_dqstats,
	},
	{
		.procname	= "syncs",
		.data		= &dqstats.stat[DQST_SYNCS],
		.maxlen		= sizeof(int),
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

static ctl_table fs_table[] = {
	{
		.procname	= "quota",
		.mode		= 0555,
		.child		= fs_dqstats_table,
	},
	{ },
};

static ctl_table sys_table[] = {
	{
		.procname	= "fs",
		.mode		= 0555,
		.child		= fs_table,
	},
	{ },
};

static int __init dquot_init(void)
{
	int i, ret;
	unsigned long nr_hash, order;

	printk(KERN_NOTICE "VFS: Disk quotas %s\n", __DQUOT_VERSION__);

	register_sysctl_table(sys_table);

	dquot_cachep = kmem_cache_create("dquot",
			sizeof(struct dquot), sizeof(unsigned long) * 4,
			(SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
				SLAB_MEM_SPREAD|SLAB_PANIC),
			NULL);

	order = 0;
	dquot_hash = (struct hlist_head *)__get_free_pages(GFP_ATOMIC, order);
	if (!dquot_hash)
		panic("Cannot create dquot hash table");

	for (i = 0; i < _DQST_DQSTAT_LAST; i++) {
		ret = percpu_counter_init(&dqstats.counter[i], 0);
		if (ret)
			panic("Cannot create dquot stat counters");
	}

	/* Find power-of-two hlist_heads which can fit into allocation */
	nr_hash = (1UL << order) * PAGE_SIZE / sizeof(struct hlist_head);
	dq_hash_bits = 0;
	do {
		dq_hash_bits++;
	} while (nr_hash >> dq_hash_bits);
	dq_hash_bits--;

	nr_hash = 1UL << dq_hash_bits;
	dq_hash_mask = nr_hash - 1;
	for (i = 0; i < nr_hash; i++)
		INIT_HLIST_HEAD(dquot_hash + i);

	printk("Dquot-cache hash table entries: %ld (order %ld, %ld bytes)\n",
			nr_hash, order, (PAGE_SIZE << order));

	register_shrinker(&dqcache_shrinker);

	return 0;
}
module_init(dquot_init);
