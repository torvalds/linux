// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

/*
 * fsnotify inode mark locking/lifetime/and refcnting
 *
 * REFCNT:
 * The group->recnt and mark->refcnt tell how many "things" in the kernel
 * currently are referencing the objects. Both kind of objects typically will
 * live inside the kernel with a refcnt of 2, one for its creation and one for
 * the reference a group and a mark hold to each other.
 * If you are holding the appropriate locks, you can take a reference and the
 * object itself is guaranteed to survive until the reference is dropped.
 *
 * LOCKING:
 * There are 3 locks involved with fsnotify inode marks and they MUST be taken
 * in order as follows:
 *
 * group->mark_mutex
 * mark->lock
 * mark->connector->lock
 *
 * group->mark_mutex protects the marks_list anchored inside a given group and
 * each mark is hooked via the g_list.  It also protects the groups private
 * data (i.e group limits).

 * mark->lock protects the marks attributes like its masks and flags.
 * Furthermore it protects the access to a reference of the group that the mark
 * is assigned to as well as the access to a reference of the inode/vfsmount
 * that is being watched by the mark.
 *
 * mark->connector->lock protects the list of marks anchored inside an
 * inode / vfsmount and each mark is hooked via the i_list.
 *
 * A list of notification marks relating to inode / mnt is contained in
 * fsnotify_mark_connector. That structure is alive as long as there are any
 * marks in the list and is also protected by fsnotify_mark_srcu. A mark gets
 * detached from fsnotify_mark_connector when last reference to the mark is
 * dropped.  Thus having mark reference is enough to protect mark->connector
 * pointer and to make sure fsnotify_mark_connector cannot disappear. Also
 * because we remove mark from g_list before dropping mark reference associated
 * with that, any mark found through g_list is guaranteed to have
 * mark->connector set until we drop group->mark_mutex.
 *
 * LIFETIME:
 * Inode marks survive between when they are added to an inode and when their
 * refcnt==0. Marks are also protected by fsnotify_mark_srcu.
 *
 * The inode mark can be cleared for a number of different reasons including:
 * - The inode is unlinked for the last time.  (fsnotify_inode_remove)
 * - The inode is being evicted from cache. (fsnotify_inode_delete)
 * - The fs the inode is on is unmounted.  (fsnotify_inode_delete/fsnotify_unmount_inodes)
 * - Something explicitly requests that it be removed.  (fsnotify_destroy_mark)
 * - The fsnotify_group associated with the mark is going away and all such marks
 *   need to be cleaned up. (fsnotify_clear_marks_by_group)
 *
 * This has the very interesting property of being able to run concurrently with
 * any (or all) other directions.
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/ratelimit.h>

#include <linux/atomic.h>

#include <linux/fsnotify_backend.h>
#include "fsnotify.h"

#define FSNOTIFY_REAPER_DELAY	(1)	/* 1 jiffy */

struct srcu_struct fsnotify_mark_srcu;
struct kmem_cache *fsnotify_mark_connector_cachep;

static DEFINE_SPINLOCK(destroy_lock);
static LIST_HEAD(destroy_list);
static struct fsnotify_mark_connector *connector_destroy_list;

static void fsnotify_mark_destroy_workfn(struct work_struct *work);
static DECLARE_DELAYED_WORK(reaper_work, fsnotify_mark_destroy_workfn);

static void fsnotify_connector_destroy_workfn(struct work_struct *work);
static DECLARE_WORK(connector_reaper_work, fsnotify_connector_destroy_workfn);

void fsnotify_get_mark(struct fsnotify_mark *mark)
{
	WARN_ON_ONCE(!refcount_read(&mark->refcnt));
	refcount_inc(&mark->refcnt);
}

static __u32 *fsnotify_conn_mask_p(struct fsnotify_mark_connector *conn)
{
	if (conn->type == FSNOTIFY_OBJ_TYPE_INODE)
		return &fsnotify_conn_inode(conn)->i_fsnotify_mask;
	else if (conn->type == FSNOTIFY_OBJ_TYPE_VFSMOUNT)
		return &fsnotify_conn_mount(conn)->mnt_fsnotify_mask;
	else if (conn->type == FSNOTIFY_OBJ_TYPE_SB)
		return &fsnotify_conn_sb(conn)->s_fsnotify_mask;
	return NULL;
}

__u32 fsnotify_conn_mask(struct fsnotify_mark_connector *conn)
{
	if (WARN_ON(!fsnotify_valid_obj_type(conn->type)))
		return 0;

	return *fsnotify_conn_mask_p(conn);
}

static void __fsnotify_recalc_mask(struct fsnotify_mark_connector *conn)
{
	u32 new_mask = 0;
	struct fsnotify_mark *mark;

	assert_spin_locked(&conn->lock);
	/* We can get detached connector here when inode is getting unlinked. */
	if (!fsnotify_valid_obj_type(conn->type))
		return;
	hlist_for_each_entry(mark, &conn->list, obj_list) {
		if (mark->flags & FSNOTIFY_MARK_FLAG_ATTACHED)
			new_mask |= mark->mask;
	}
	*fsnotify_conn_mask_p(conn) = new_mask;
}

/*
 * Calculate mask of events for a list of marks. The caller must make sure
 * connector and connector->obj cannot disappear under us.  Callers achieve
 * this by holding a mark->lock or mark->group->mark_mutex for a mark on this
 * list.
 */
void fsnotify_recalc_mask(struct fsnotify_mark_connector *conn)
{
	if (!conn)
		return;

	spin_lock(&conn->lock);
	__fsnotify_recalc_mask(conn);
	spin_unlock(&conn->lock);
	if (conn->type == FSNOTIFY_OBJ_TYPE_INODE)
		__fsnotify_update_child_dentry_flags(
					fsnotify_conn_inode(conn));
}

/* Free all connectors queued for freeing once SRCU period ends */
static void fsnotify_connector_destroy_workfn(struct work_struct *work)
{
	struct fsnotify_mark_connector *conn, *free;

	spin_lock(&destroy_lock);
	conn = connector_destroy_list;
	connector_destroy_list = NULL;
	spin_unlock(&destroy_lock);

	synchronize_srcu(&fsnotify_mark_srcu);
	while (conn) {
		free = conn;
		conn = conn->destroy_next;
		kmem_cache_free(fsnotify_mark_connector_cachep, free);
	}
}

static void *fsnotify_detach_connector_from_object(
					struct fsnotify_mark_connector *conn,
					unsigned int *type)
{
	struct inode *inode = NULL;

	*type = conn->type;
	if (conn->type == FSNOTIFY_OBJ_TYPE_DETACHED)
		return NULL;

	if (conn->type == FSNOTIFY_OBJ_TYPE_INODE) {
		inode = fsnotify_conn_inode(conn);
		inode->i_fsnotify_mask = 0;
		atomic_long_inc(&inode->i_sb->s_fsnotify_inode_refs);
	} else if (conn->type == FSNOTIFY_OBJ_TYPE_VFSMOUNT) {
		fsnotify_conn_mount(conn)->mnt_fsnotify_mask = 0;
	} else if (conn->type == FSNOTIFY_OBJ_TYPE_SB) {
		fsnotify_conn_sb(conn)->s_fsnotify_mask = 0;
	}

	rcu_assign_pointer(*(conn->obj), NULL);
	conn->obj = NULL;
	conn->type = FSNOTIFY_OBJ_TYPE_DETACHED;

	return inode;
}

static void fsnotify_final_mark_destroy(struct fsnotify_mark *mark)
{
	struct fsnotify_group *group = mark->group;

	if (WARN_ON_ONCE(!group))
		return;
	group->ops->free_mark(mark);
	fsnotify_put_group(group);
}

/* Drop object reference originally held by a connector */
static void fsnotify_drop_object(unsigned int type, void *objp)
{
	struct inode *inode;
	struct super_block *sb;

	if (!objp)
		return;
	/* Currently only inode references are passed to be dropped */
	if (WARN_ON_ONCE(type != FSNOTIFY_OBJ_TYPE_INODE))
		return;
	inode = objp;
	sb = inode->i_sb;
	iput(inode);
	if (atomic_long_dec_and_test(&sb->s_fsnotify_inode_refs))
		wake_up_var(&sb->s_fsnotify_inode_refs);
}

void fsnotify_put_mark(struct fsnotify_mark *mark)
{
	struct fsnotify_mark_connector *conn = READ_ONCE(mark->connector);
	void *objp = NULL;
	unsigned int type = FSNOTIFY_OBJ_TYPE_DETACHED;
	bool free_conn = false;

	/* Catch marks that were actually never attached to object */
	if (!conn) {
		if (refcount_dec_and_test(&mark->refcnt))
			fsnotify_final_mark_destroy(mark);
		return;
	}

	/*
	 * We have to be careful so that traversals of obj_list under lock can
	 * safely grab mark reference.
	 */
	if (!refcount_dec_and_lock(&mark->refcnt, &conn->lock))
		return;

	hlist_del_init_rcu(&mark->obj_list);
	if (hlist_empty(&conn->list)) {
		objp = fsnotify_detach_connector_from_object(conn, &type);
		free_conn = true;
	} else {
		__fsnotify_recalc_mask(conn);
	}
	WRITE_ONCE(mark->connector, NULL);
	spin_unlock(&conn->lock);

	fsnotify_drop_object(type, objp);

	if (free_conn) {
		spin_lock(&destroy_lock);
		conn->destroy_next = connector_destroy_list;
		connector_destroy_list = conn;
		spin_unlock(&destroy_lock);
		queue_work(system_unbound_wq, &connector_reaper_work);
	}
	/*
	 * Note that we didn't update flags telling whether inode cares about
	 * what's happening with children. We update these flags from
	 * __fsnotify_parent() lazily when next event happens on one of our
	 * children.
	 */
	spin_lock(&destroy_lock);
	list_add(&mark->g_list, &destroy_list);
	spin_unlock(&destroy_lock);
	queue_delayed_work(system_unbound_wq, &reaper_work,
			   FSNOTIFY_REAPER_DELAY);
}
EXPORT_SYMBOL_GPL(fsnotify_put_mark);

/*
 * Get mark reference when we found the mark via lockless traversal of object
 * list. Mark can be already removed from the list by now and on its way to be
 * destroyed once SRCU period ends.
 *
 * Also pin the group so it doesn't disappear under us.
 */
static bool fsnotify_get_mark_safe(struct fsnotify_mark *mark)
{
	if (!mark)
		return true;

	if (refcount_inc_not_zero(&mark->refcnt)) {
		spin_lock(&mark->lock);
		if (mark->flags & FSNOTIFY_MARK_FLAG_ATTACHED) {
			/* mark is attached, group is still alive then */
			atomic_inc(&mark->group->user_waits);
			spin_unlock(&mark->lock);
			return true;
		}
		spin_unlock(&mark->lock);
		fsnotify_put_mark(mark);
	}
	return false;
}

/*
 * Puts marks and wakes up group destruction if necessary.
 *
 * Pairs with fsnotify_get_mark_safe()
 */
static void fsnotify_put_mark_wake(struct fsnotify_mark *mark)
{
	if (mark) {
		struct fsnotify_group *group = mark->group;

		fsnotify_put_mark(mark);
		/*
		 * We abuse notification_waitq on group shutdown for waiting for
		 * all marks pinned when waiting for userspace.
		 */
		if (atomic_dec_and_test(&group->user_waits) && group->shutdown)
			wake_up(&group->notification_waitq);
	}
}

bool fsnotify_prepare_user_wait(struct fsnotify_iter_info *iter_info)
	__releases(&fsnotify_mark_srcu)
{
	int type;

	fsnotify_foreach_obj_type(type) {
		/* This can fail if mark is being removed */
		if (!fsnotify_get_mark_safe(iter_info->marks[type])) {
			__release(&fsnotify_mark_srcu);
			goto fail;
		}
	}

	/*
	 * Now that both marks are pinned by refcount in the inode / vfsmount
	 * lists, we can drop SRCU lock, and safely resume the list iteration
	 * once userspace returns.
	 */
	srcu_read_unlock(&fsnotify_mark_srcu, iter_info->srcu_idx);

	return true;

fail:
	for (type--; type >= 0; type--)
		fsnotify_put_mark_wake(iter_info->marks[type]);
	return false;
}

void fsnotify_finish_user_wait(struct fsnotify_iter_info *iter_info)
	__acquires(&fsnotify_mark_srcu)
{
	int type;

	iter_info->srcu_idx = srcu_read_lock(&fsnotify_mark_srcu);
	fsnotify_foreach_obj_type(type)
		fsnotify_put_mark_wake(iter_info->marks[type]);
}

/*
 * Mark mark as detached, remove it from group list. Mark still stays in object
 * list until its last reference is dropped. Note that we rely on mark being
 * removed from group list before corresponding reference to it is dropped. In
 * particular we rely on mark->connector being valid while we hold
 * group->mark_mutex if we found the mark through g_list.
 *
 * Must be called with group->mark_mutex held. The caller must either hold
 * reference to the mark or be protected by fsnotify_mark_srcu.
 */
void fsnotify_detach_mark(struct fsnotify_mark *mark)
{
	struct fsnotify_group *group = mark->group;

	WARN_ON_ONCE(!mutex_is_locked(&group->mark_mutex));
	WARN_ON_ONCE(!srcu_read_lock_held(&fsnotify_mark_srcu) &&
		     refcount_read(&mark->refcnt) < 1 +
			!!(mark->flags & FSNOTIFY_MARK_FLAG_ATTACHED));

	spin_lock(&mark->lock);
	/* something else already called this function on this mark */
	if (!(mark->flags & FSNOTIFY_MARK_FLAG_ATTACHED)) {
		spin_unlock(&mark->lock);
		return;
	}
	mark->flags &= ~FSNOTIFY_MARK_FLAG_ATTACHED;
	list_del_init(&mark->g_list);
	spin_unlock(&mark->lock);

	/* Drop mark reference acquired in fsnotify_add_mark_locked() */
	fsnotify_put_mark(mark);
}

/*
 * Free fsnotify mark. The mark is actually only marked as being freed.  The
 * freeing is actually happening only once last reference to the mark is
 * dropped from a workqueue which first waits for srcu period end.
 *
 * Caller must have a reference to the mark or be protected by
 * fsnotify_mark_srcu.
 */
void fsnotify_free_mark(struct fsnotify_mark *mark)
{
	struct fsnotify_group *group = mark->group;

	spin_lock(&mark->lock);
	/* something else already called this function on this mark */
	if (!(mark->flags & FSNOTIFY_MARK_FLAG_ALIVE)) {
		spin_unlock(&mark->lock);
		return;
	}
	mark->flags &= ~FSNOTIFY_MARK_FLAG_ALIVE;
	spin_unlock(&mark->lock);

	/*
	 * Some groups like to know that marks are being freed.  This is a
	 * callback to the group function to let it know that this mark
	 * is being freed.
	 */
	if (group->ops->freeing_mark)
		group->ops->freeing_mark(mark, group);
}

void fsnotify_destroy_mark(struct fsnotify_mark *mark,
			   struct fsnotify_group *group)
{
	mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
	fsnotify_detach_mark(mark);
	mutex_unlock(&group->mark_mutex);
	fsnotify_free_mark(mark);
}
EXPORT_SYMBOL_GPL(fsnotify_destroy_mark);

/*
 * Sorting function for lists of fsnotify marks.
 *
 * Fanotify supports different notification classes (reflected as priority of
 * notification group). Events shall be passed to notification groups in
 * decreasing priority order. To achieve this marks in notification lists for
 * inodes and vfsmounts are sorted so that priorities of corresponding groups
 * are descending.
 *
 * Furthermore correct handling of the ignore mask requires processing inode
 * and vfsmount marks of each group together. Using the group address as
 * further sort criterion provides a unique sorting order and thus we can
 * merge inode and vfsmount lists of marks in linear time and find groups
 * present in both lists.
 *
 * A return value of 1 signifies that b has priority over a.
 * A return value of 0 signifies that the two marks have to be handled together.
 * A return value of -1 signifies that a has priority over b.
 */
int fsnotify_compare_groups(struct fsnotify_group *a, struct fsnotify_group *b)
{
	if (a == b)
		return 0;
	if (!a)
		return 1;
	if (!b)
		return -1;
	if (a->priority < b->priority)
		return 1;
	if (a->priority > b->priority)
		return -1;
	if (a < b)
		return 1;
	return -1;
}

static int fsnotify_attach_connector_to_object(fsnotify_connp_t *connp,
					       unsigned int type,
					       __kernel_fsid_t *fsid)
{
	struct inode *inode = NULL;
	struct fsnotify_mark_connector *conn;

	conn = kmem_cache_alloc(fsnotify_mark_connector_cachep, GFP_KERNEL);
	if (!conn)
		return -ENOMEM;
	spin_lock_init(&conn->lock);
	INIT_HLIST_HEAD(&conn->list);
	conn->type = type;
	conn->obj = connp;
	/* Cache fsid of filesystem containing the object */
	if (fsid) {
		conn->fsid = *fsid;
		conn->flags = FSNOTIFY_CONN_FLAG_HAS_FSID;
	} else {
		conn->fsid.val[0] = conn->fsid.val[1] = 0;
		conn->flags = 0;
	}
	if (conn->type == FSNOTIFY_OBJ_TYPE_INODE)
		inode = igrab(fsnotify_conn_inode(conn));
	/*
	 * cmpxchg() provides the barrier so that readers of *connp can see
	 * only initialized structure
	 */
	if (cmpxchg(connp, NULL, conn)) {
		/* Someone else created list structure for us */
		if (inode)
			iput(inode);
		kmem_cache_free(fsnotify_mark_connector_cachep, conn);
	}

	return 0;
}

/*
 * Get mark connector, make sure it is alive and return with its lock held.
 * This is for users that get connector pointer from inode or mount. Users that
 * hold reference to a mark on the list may directly lock connector->lock as
 * they are sure list cannot go away under them.
 */
static struct fsnotify_mark_connector *fsnotify_grab_connector(
						fsnotify_connp_t *connp)
{
	struct fsnotify_mark_connector *conn;
	int idx;

	idx = srcu_read_lock(&fsnotify_mark_srcu);
	conn = srcu_dereference(*connp, &fsnotify_mark_srcu);
	if (!conn)
		goto out;
	spin_lock(&conn->lock);
	if (conn->type == FSNOTIFY_OBJ_TYPE_DETACHED) {
		spin_unlock(&conn->lock);
		srcu_read_unlock(&fsnotify_mark_srcu, idx);
		return NULL;
	}
out:
	srcu_read_unlock(&fsnotify_mark_srcu, idx);
	return conn;
}

/*
 * Add mark into proper place in given list of marks. These marks may be used
 * for the fsnotify backend to determine which event types should be delivered
 * to which group and for which inodes. These marks are ordered according to
 * priority, highest number first, and then by the group's location in memory.
 */
static int fsnotify_add_mark_list(struct fsnotify_mark *mark,
				  fsnotify_connp_t *connp, unsigned int type,
				  int allow_dups, __kernel_fsid_t *fsid)
{
	struct fsnotify_mark *lmark, *last = NULL;
	struct fsnotify_mark_connector *conn;
	int cmp;
	int err = 0;

	if (WARN_ON(!fsnotify_valid_obj_type(type)))
		return -EINVAL;

	/* Backend is expected to check for zero fsid (e.g. tmpfs) */
	if (fsid && WARN_ON_ONCE(!fsid->val[0] && !fsid->val[1]))
		return -ENODEV;

restart:
	spin_lock(&mark->lock);
	conn = fsnotify_grab_connector(connp);
	if (!conn) {
		spin_unlock(&mark->lock);
		err = fsnotify_attach_connector_to_object(connp, type, fsid);
		if (err)
			return err;
		goto restart;
	} else if (fsid && !(conn->flags & FSNOTIFY_CONN_FLAG_HAS_FSID)) {
		conn->fsid = *fsid;
		/* Pairs with smp_rmb() in fanotify_get_fsid() */
		smp_wmb();
		conn->flags |= FSNOTIFY_CONN_FLAG_HAS_FSID;
	} else if (fsid && (conn->flags & FSNOTIFY_CONN_FLAG_HAS_FSID) &&
		   (fsid->val[0] != conn->fsid.val[0] ||
		    fsid->val[1] != conn->fsid.val[1])) {
		/*
		 * Backend is expected to check for non uniform fsid
		 * (e.g. btrfs), but maybe we missed something?
		 * Only allow setting conn->fsid once to non zero fsid.
		 * inotify and non-fid fanotify groups do not set nor test
		 * conn->fsid.
		 */
		pr_warn_ratelimited("%s: fsid mismatch on object of type %u: "
				    "%x.%x != %x.%x\n", __func__, conn->type,
				    fsid->val[0], fsid->val[1],
				    conn->fsid.val[0], conn->fsid.val[1]);
		err = -EXDEV;
		goto out_err;
	}

	/* is mark the first mark? */
	if (hlist_empty(&conn->list)) {
		hlist_add_head_rcu(&mark->obj_list, &conn->list);
		goto added;
	}

	/* should mark be in the middle of the current list? */
	hlist_for_each_entry(lmark, &conn->list, obj_list) {
		last = lmark;

		if ((lmark->group == mark->group) &&
		    (lmark->flags & FSNOTIFY_MARK_FLAG_ATTACHED) &&
		    !allow_dups) {
			err = -EEXIST;
			goto out_err;
		}

		cmp = fsnotify_compare_groups(lmark->group, mark->group);
		if (cmp >= 0) {
			hlist_add_before_rcu(&mark->obj_list, &lmark->obj_list);
			goto added;
		}
	}

	BUG_ON(last == NULL);
	/* mark should be the last entry.  last is the current last entry */
	hlist_add_behind_rcu(&mark->obj_list, &last->obj_list);
added:
	/*
	 * Since connector is attached to object using cmpxchg() we are
	 * guaranteed that connector initialization is fully visible by anyone
	 * seeing mark->connector set.
	 */
	WRITE_ONCE(mark->connector, conn);
out_err:
	spin_unlock(&conn->lock);
	spin_unlock(&mark->lock);
	return err;
}

/*
 * Attach an initialized mark to a given group and fs object.
 * These marks may be used for the fsnotify backend to determine which
 * event types should be delivered to which group.
 */
int fsnotify_add_mark_locked(struct fsnotify_mark *mark,
			     fsnotify_connp_t *connp, unsigned int type,
			     int allow_dups, __kernel_fsid_t *fsid)
{
	struct fsnotify_group *group = mark->group;
	int ret = 0;

	BUG_ON(!mutex_is_locked(&group->mark_mutex));

	/*
	 * LOCKING ORDER!!!!
	 * group->mark_mutex
	 * mark->lock
	 * mark->connector->lock
	 */
	spin_lock(&mark->lock);
	mark->flags |= FSNOTIFY_MARK_FLAG_ALIVE | FSNOTIFY_MARK_FLAG_ATTACHED;

	list_add(&mark->g_list, &group->marks_list);
	fsnotify_get_mark(mark); /* for g_list */
	spin_unlock(&mark->lock);

	ret = fsnotify_add_mark_list(mark, connp, type, allow_dups, fsid);
	if (ret)
		goto err;

	if (mark->mask)
		fsnotify_recalc_mask(mark->connector);

	return ret;
err:
	spin_lock(&mark->lock);
	mark->flags &= ~(FSNOTIFY_MARK_FLAG_ALIVE |
			 FSNOTIFY_MARK_FLAG_ATTACHED);
	list_del_init(&mark->g_list);
	spin_unlock(&mark->lock);

	fsnotify_put_mark(mark);
	return ret;
}

int fsnotify_add_mark(struct fsnotify_mark *mark, fsnotify_connp_t *connp,
		      unsigned int type, int allow_dups, __kernel_fsid_t *fsid)
{
	int ret;
	struct fsnotify_group *group = mark->group;

	mutex_lock(&group->mark_mutex);
	ret = fsnotify_add_mark_locked(mark, connp, type, allow_dups, fsid);
	mutex_unlock(&group->mark_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(fsnotify_add_mark);

/*
 * Given a list of marks, find the mark associated with given group. If found
 * take a reference to that mark and return it, else return NULL.
 */
struct fsnotify_mark *fsnotify_find_mark(fsnotify_connp_t *connp,
					 struct fsnotify_group *group)
{
	struct fsnotify_mark_connector *conn;
	struct fsnotify_mark *mark;

	conn = fsnotify_grab_connector(connp);
	if (!conn)
		return NULL;

	hlist_for_each_entry(mark, &conn->list, obj_list) {
		if (mark->group == group &&
		    (mark->flags & FSNOTIFY_MARK_FLAG_ATTACHED)) {
			fsnotify_get_mark(mark);
			spin_unlock(&conn->lock);
			return mark;
		}
	}
	spin_unlock(&conn->lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(fsnotify_find_mark);

/* Clear any marks in a group with given type mask */
void fsnotify_clear_marks_by_group(struct fsnotify_group *group,
				   unsigned int type_mask)
{
	struct fsnotify_mark *lmark, *mark;
	LIST_HEAD(to_free);
	struct list_head *head = &to_free;

	/* Skip selection step if we want to clear all marks. */
	if (type_mask == FSNOTIFY_OBJ_ALL_TYPES_MASK) {
		head = &group->marks_list;
		goto clear;
	}
	/*
	 * We have to be really careful here. Anytime we drop mark_mutex, e.g.
	 * fsnotify_clear_marks_by_inode() can come and free marks. Even in our
	 * to_free list so we have to use mark_mutex even when accessing that
	 * list. And freeing mark requires us to drop mark_mutex. So we can
	 * reliably free only the first mark in the list. That's why we first
	 * move marks to free to to_free list in one go and then free marks in
	 * to_free list one by one.
	 */
	mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
	list_for_each_entry_safe(mark, lmark, &group->marks_list, g_list) {
		if ((1U << mark->connector->type) & type_mask)
			list_move(&mark->g_list, &to_free);
	}
	mutex_unlock(&group->mark_mutex);

clear:
	while (1) {
		mutex_lock_nested(&group->mark_mutex, SINGLE_DEPTH_NESTING);
		if (list_empty(head)) {
			mutex_unlock(&group->mark_mutex);
			break;
		}
		mark = list_first_entry(head, struct fsnotify_mark, g_list);
		fsnotify_get_mark(mark);
		fsnotify_detach_mark(mark);
		mutex_unlock(&group->mark_mutex);
		fsnotify_free_mark(mark);
		fsnotify_put_mark(mark);
	}
}

/* Destroy all marks attached to an object via connector */
void fsnotify_destroy_marks(fsnotify_connp_t *connp)
{
	struct fsnotify_mark_connector *conn;
	struct fsnotify_mark *mark, *old_mark = NULL;
	void *objp;
	unsigned int type;

	conn = fsnotify_grab_connector(connp);
	if (!conn)
		return;
	/*
	 * We have to be careful since we can race with e.g.
	 * fsnotify_clear_marks_by_group() and once we drop the conn->lock, the
	 * list can get modified. However we are holding mark reference and
	 * thus our mark cannot be removed from obj_list so we can continue
	 * iteration after regaining conn->lock.
	 */
	hlist_for_each_entry(mark, &conn->list, obj_list) {
		fsnotify_get_mark(mark);
		spin_unlock(&conn->lock);
		if (old_mark)
			fsnotify_put_mark(old_mark);
		old_mark = mark;
		fsnotify_destroy_mark(mark, mark->group);
		spin_lock(&conn->lock);
	}
	/*
	 * Detach list from object now so that we don't pin inode until all
	 * mark references get dropped. It would lead to strange results such
	 * as delaying inode deletion or blocking unmount.
	 */
	objp = fsnotify_detach_connector_from_object(conn, &type);
	spin_unlock(&conn->lock);
	if (old_mark)
		fsnotify_put_mark(old_mark);
	fsnotify_drop_object(type, objp);
}

/*
 * Nothing fancy, just initialize lists and locks and counters.
 */
void fsnotify_init_mark(struct fsnotify_mark *mark,
			struct fsnotify_group *group)
{
	memset(mark, 0, sizeof(*mark));
	spin_lock_init(&mark->lock);
	refcount_set(&mark->refcnt, 1);
	fsnotify_get_group(group);
	mark->group = group;
	WRITE_ONCE(mark->connector, NULL);
}
EXPORT_SYMBOL_GPL(fsnotify_init_mark);

/*
 * Destroy all marks in destroy_list, waits for SRCU period to finish before
 * actually freeing marks.
 */
static void fsnotify_mark_destroy_workfn(struct work_struct *work)
{
	struct fsnotify_mark *mark, *next;
	struct list_head private_destroy_list;

	spin_lock(&destroy_lock);
	/* exchange the list head */
	list_replace_init(&destroy_list, &private_destroy_list);
	spin_unlock(&destroy_lock);

	synchronize_srcu(&fsnotify_mark_srcu);

	list_for_each_entry_safe(mark, next, &private_destroy_list, g_list) {
		list_del_init(&mark->g_list);
		fsnotify_final_mark_destroy(mark);
	}
}

/* Wait for all marks queued for destruction to be actually destroyed */
void fsnotify_wait_marks_destroyed(void)
{
	flush_delayed_work(&reaper_work);
}
EXPORT_SYMBOL_GPL(fsnotify_wait_marks_destroyed);
