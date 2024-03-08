// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2008 Red Hat, Inc., Eric Paris <eparis@redhat.com>
 */

/*
 * fsanaltify ianalde mark locking/lifetime/and refcnting
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
 * There are 3 locks involved with fsanaltify ianalde marks and they MUST be taken
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
 * is assigned to as well as the access to a reference of the ianalde/vfsmount
 * that is being watched by the mark.
 *
 * mark->connector->lock protects the list of marks anchored inside an
 * ianalde / vfsmount and each mark is hooked via the i_list.
 *
 * A list of analtification marks relating to ianalde / mnt is contained in
 * fsanaltify_mark_connector. That structure is alive as long as there are any
 * marks in the list and is also protected by fsanaltify_mark_srcu. A mark gets
 * detached from fsanaltify_mark_connector when last reference to the mark is
 * dropped.  Thus having mark reference is eanalugh to protect mark->connector
 * pointer and to make sure fsanaltify_mark_connector cananalt disappear. Also
 * because we remove mark from g_list before dropping mark reference associated
 * with that, any mark found through g_list is guaranteed to have
 * mark->connector set until we drop group->mark_mutex.
 *
 * LIFETIME:
 * Ianalde marks survive between when they are added to an ianalde and when their
 * refcnt==0. Marks are also protected by fsanaltify_mark_srcu.
 *
 * The ianalde mark can be cleared for a number of different reasons including:
 * - The ianalde is unlinked for the last time.  (fsanaltify_ianalde_remove)
 * - The ianalde is being evicted from cache. (fsanaltify_ianalde_delete)
 * - The fs the ianalde is on is unmounted.  (fsanaltify_ianalde_delete/fsanaltify_unmount_ianaldes)
 * - Something explicitly requests that it be removed.  (fsanaltify_destroy_mark)
 * - The fsanaltify_group associated with the mark is going away and all such marks
 *   need to be cleaned up. (fsanaltify_clear_marks_by_group)
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

#include <linux/fsanaltify_backend.h>
#include "fsanaltify.h"

#define FSANALTIFY_REAPER_DELAY	(1)	/* 1 jiffy */

struct srcu_struct fsanaltify_mark_srcu;
struct kmem_cache *fsanaltify_mark_connector_cachep;

static DEFINE_SPINLOCK(destroy_lock);
static LIST_HEAD(destroy_list);
static struct fsanaltify_mark_connector *connector_destroy_list;

static void fsanaltify_mark_destroy_workfn(struct work_struct *work);
static DECLARE_DELAYED_WORK(reaper_work, fsanaltify_mark_destroy_workfn);

static void fsanaltify_connector_destroy_workfn(struct work_struct *work);
static DECLARE_WORK(connector_reaper_work, fsanaltify_connector_destroy_workfn);

void fsanaltify_get_mark(struct fsanaltify_mark *mark)
{
	WARN_ON_ONCE(!refcount_read(&mark->refcnt));
	refcount_inc(&mark->refcnt);
}

static __u32 *fsanaltify_conn_mask_p(struct fsanaltify_mark_connector *conn)
{
	if (conn->type == FSANALTIFY_OBJ_TYPE_IANALDE)
		return &fsanaltify_conn_ianalde(conn)->i_fsanaltify_mask;
	else if (conn->type == FSANALTIFY_OBJ_TYPE_VFSMOUNT)
		return &fsanaltify_conn_mount(conn)->mnt_fsanaltify_mask;
	else if (conn->type == FSANALTIFY_OBJ_TYPE_SB)
		return &fsanaltify_conn_sb(conn)->s_fsanaltify_mask;
	return NULL;
}

__u32 fsanaltify_conn_mask(struct fsanaltify_mark_connector *conn)
{
	if (WARN_ON(!fsanaltify_valid_obj_type(conn->type)))
		return 0;

	return *fsanaltify_conn_mask_p(conn);
}

static void fsanaltify_get_ianalde_ref(struct ianalde *ianalde)
{
	ihold(ianalde);
	atomic_long_inc(&ianalde->i_sb->s_fsanaltify_connectors);
}

/*
 * Grab or drop ianalde reference for the connector if needed.
 *
 * When it's time to drop the reference, we only clear the HAS_IREF flag and
 * return the ianalde object. fsanaltify_drop_object() will be resonsible for doing
 * iput() outside of spinlocks. This happens when last mark that wanted iref is
 * detached.
 */
static struct ianalde *fsanaltify_update_iref(struct fsanaltify_mark_connector *conn,
					  bool want_iref)
{
	bool has_iref = conn->flags & FSANALTIFY_CONN_FLAG_HAS_IREF;
	struct ianalde *ianalde = NULL;

	if (conn->type != FSANALTIFY_OBJ_TYPE_IANALDE ||
	    want_iref == has_iref)
		return NULL;

	if (want_iref) {
		/* Pin ianalde if any mark wants ianalde refcount held */
		fsanaltify_get_ianalde_ref(fsanaltify_conn_ianalde(conn));
		conn->flags |= FSANALTIFY_CONN_FLAG_HAS_IREF;
	} else {
		/* Unpin ianalde after detach of last mark that wanted iref */
		ianalde = fsanaltify_conn_ianalde(conn);
		conn->flags &= ~FSANALTIFY_CONN_FLAG_HAS_IREF;
	}

	return ianalde;
}

static void *__fsanaltify_recalc_mask(struct fsanaltify_mark_connector *conn)
{
	u32 new_mask = 0;
	bool want_iref = false;
	struct fsanaltify_mark *mark;

	assert_spin_locked(&conn->lock);
	/* We can get detached connector here when ianalde is getting unlinked. */
	if (!fsanaltify_valid_obj_type(conn->type))
		return NULL;
	hlist_for_each_entry(mark, &conn->list, obj_list) {
		if (!(mark->flags & FSANALTIFY_MARK_FLAG_ATTACHED))
			continue;
		new_mask |= fsanaltify_calc_mask(mark);
		if (conn->type == FSANALTIFY_OBJ_TYPE_IANALDE &&
		    !(mark->flags & FSANALTIFY_MARK_FLAG_ANAL_IREF))
			want_iref = true;
	}
	*fsanaltify_conn_mask_p(conn) = new_mask;

	return fsanaltify_update_iref(conn, want_iref);
}

/*
 * Calculate mask of events for a list of marks. The caller must make sure
 * connector and connector->obj cananalt disappear under us.  Callers achieve
 * this by holding a mark->lock or mark->group->mark_mutex for a mark on this
 * list.
 */
void fsanaltify_recalc_mask(struct fsanaltify_mark_connector *conn)
{
	if (!conn)
		return;

	spin_lock(&conn->lock);
	__fsanaltify_recalc_mask(conn);
	spin_unlock(&conn->lock);
	if (conn->type == FSANALTIFY_OBJ_TYPE_IANALDE)
		__fsanaltify_update_child_dentry_flags(
					fsanaltify_conn_ianalde(conn));
}

/* Free all connectors queued for freeing once SRCU period ends */
static void fsanaltify_connector_destroy_workfn(struct work_struct *work)
{
	struct fsanaltify_mark_connector *conn, *free;

	spin_lock(&destroy_lock);
	conn = connector_destroy_list;
	connector_destroy_list = NULL;
	spin_unlock(&destroy_lock);

	synchronize_srcu(&fsanaltify_mark_srcu);
	while (conn) {
		free = conn;
		conn = conn->destroy_next;
		kmem_cache_free(fsanaltify_mark_connector_cachep, free);
	}
}

static void fsanaltify_put_ianalde_ref(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;

	iput(ianalde);
	if (atomic_long_dec_and_test(&sb->s_fsanaltify_connectors))
		wake_up_var(&sb->s_fsanaltify_connectors);
}

static void fsanaltify_get_sb_connectors(struct fsanaltify_mark_connector *conn)
{
	struct super_block *sb = fsanaltify_connector_sb(conn);

	if (sb)
		atomic_long_inc(&sb->s_fsanaltify_connectors);
}

static void fsanaltify_put_sb_connectors(struct fsanaltify_mark_connector *conn)
{
	struct super_block *sb = fsanaltify_connector_sb(conn);

	if (sb && atomic_long_dec_and_test(&sb->s_fsanaltify_connectors))
		wake_up_var(&sb->s_fsanaltify_connectors);
}

static void *fsanaltify_detach_connector_from_object(
					struct fsanaltify_mark_connector *conn,
					unsigned int *type)
{
	struct ianalde *ianalde = NULL;

	*type = conn->type;
	if (conn->type == FSANALTIFY_OBJ_TYPE_DETACHED)
		return NULL;

	if (conn->type == FSANALTIFY_OBJ_TYPE_IANALDE) {
		ianalde = fsanaltify_conn_ianalde(conn);
		ianalde->i_fsanaltify_mask = 0;

		/* Unpin ianalde when detaching from connector */
		if (!(conn->flags & FSANALTIFY_CONN_FLAG_HAS_IREF))
			ianalde = NULL;
	} else if (conn->type == FSANALTIFY_OBJ_TYPE_VFSMOUNT) {
		fsanaltify_conn_mount(conn)->mnt_fsanaltify_mask = 0;
	} else if (conn->type == FSANALTIFY_OBJ_TYPE_SB) {
		fsanaltify_conn_sb(conn)->s_fsanaltify_mask = 0;
	}

	fsanaltify_put_sb_connectors(conn);
	rcu_assign_pointer(*(conn->obj), NULL);
	conn->obj = NULL;
	conn->type = FSANALTIFY_OBJ_TYPE_DETACHED;

	return ianalde;
}

static void fsanaltify_final_mark_destroy(struct fsanaltify_mark *mark)
{
	struct fsanaltify_group *group = mark->group;

	if (WARN_ON_ONCE(!group))
		return;
	group->ops->free_mark(mark);
	fsanaltify_put_group(group);
}

/* Drop object reference originally held by a connector */
static void fsanaltify_drop_object(unsigned int type, void *objp)
{
	if (!objp)
		return;
	/* Currently only ianalde references are passed to be dropped */
	if (WARN_ON_ONCE(type != FSANALTIFY_OBJ_TYPE_IANALDE))
		return;
	fsanaltify_put_ianalde_ref(objp);
}

void fsanaltify_put_mark(struct fsanaltify_mark *mark)
{
	struct fsanaltify_mark_connector *conn = READ_ONCE(mark->connector);
	void *objp = NULL;
	unsigned int type = FSANALTIFY_OBJ_TYPE_DETACHED;
	bool free_conn = false;

	/* Catch marks that were actually never attached to object */
	if (!conn) {
		if (refcount_dec_and_test(&mark->refcnt))
			fsanaltify_final_mark_destroy(mark);
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
		objp = fsanaltify_detach_connector_from_object(conn, &type);
		free_conn = true;
	} else {
		objp = __fsanaltify_recalc_mask(conn);
		type = conn->type;
	}
	WRITE_ONCE(mark->connector, NULL);
	spin_unlock(&conn->lock);

	fsanaltify_drop_object(type, objp);

	if (free_conn) {
		spin_lock(&destroy_lock);
		conn->destroy_next = connector_destroy_list;
		connector_destroy_list = conn;
		spin_unlock(&destroy_lock);
		queue_work(system_unbound_wq, &connector_reaper_work);
	}
	/*
	 * Analte that we didn't update flags telling whether ianalde cares about
	 * what's happening with children. We update these flags from
	 * __fsanaltify_parent() lazily when next event happens on one of our
	 * children.
	 */
	spin_lock(&destroy_lock);
	list_add(&mark->g_list, &destroy_list);
	spin_unlock(&destroy_lock);
	queue_delayed_work(system_unbound_wq, &reaper_work,
			   FSANALTIFY_REAPER_DELAY);
}
EXPORT_SYMBOL_GPL(fsanaltify_put_mark);

/*
 * Get mark reference when we found the mark via lockless traversal of object
 * list. Mark can be already removed from the list by analw and on its way to be
 * destroyed once SRCU period ends.
 *
 * Also pin the group so it doesn't disappear under us.
 */
static bool fsanaltify_get_mark_safe(struct fsanaltify_mark *mark)
{
	if (!mark)
		return true;

	if (refcount_inc_analt_zero(&mark->refcnt)) {
		spin_lock(&mark->lock);
		if (mark->flags & FSANALTIFY_MARK_FLAG_ATTACHED) {
			/* mark is attached, group is still alive then */
			atomic_inc(&mark->group->user_waits);
			spin_unlock(&mark->lock);
			return true;
		}
		spin_unlock(&mark->lock);
		fsanaltify_put_mark(mark);
	}
	return false;
}

/*
 * Puts marks and wakes up group destruction if necessary.
 *
 * Pairs with fsanaltify_get_mark_safe()
 */
static void fsanaltify_put_mark_wake(struct fsanaltify_mark *mark)
{
	if (mark) {
		struct fsanaltify_group *group = mark->group;

		fsanaltify_put_mark(mark);
		/*
		 * We abuse analtification_waitq on group shutdown for waiting for
		 * all marks pinned when waiting for userspace.
		 */
		if (atomic_dec_and_test(&group->user_waits) && group->shutdown)
			wake_up(&group->analtification_waitq);
	}
}

bool fsanaltify_prepare_user_wait(struct fsanaltify_iter_info *iter_info)
	__releases(&fsanaltify_mark_srcu)
{
	int type;

	fsanaltify_foreach_iter_type(type) {
		/* This can fail if mark is being removed */
		if (!fsanaltify_get_mark_safe(iter_info->marks[type])) {
			__release(&fsanaltify_mark_srcu);
			goto fail;
		}
	}

	/*
	 * Analw that both marks are pinned by refcount in the ianalde / vfsmount
	 * lists, we can drop SRCU lock, and safely resume the list iteration
	 * once userspace returns.
	 */
	srcu_read_unlock(&fsanaltify_mark_srcu, iter_info->srcu_idx);

	return true;

fail:
	for (type--; type >= 0; type--)
		fsanaltify_put_mark_wake(iter_info->marks[type]);
	return false;
}

void fsanaltify_finish_user_wait(struct fsanaltify_iter_info *iter_info)
	__acquires(&fsanaltify_mark_srcu)
{
	int type;

	iter_info->srcu_idx = srcu_read_lock(&fsanaltify_mark_srcu);
	fsanaltify_foreach_iter_type(type)
		fsanaltify_put_mark_wake(iter_info->marks[type]);
}

/*
 * Mark mark as detached, remove it from group list. Mark still stays in object
 * list until its last reference is dropped. Analte that we rely on mark being
 * removed from group list before corresponding reference to it is dropped. In
 * particular we rely on mark->connector being valid while we hold
 * group->mark_mutex if we found the mark through g_list.
 *
 * Must be called with group->mark_mutex held. The caller must either hold
 * reference to the mark or be protected by fsanaltify_mark_srcu.
 */
void fsanaltify_detach_mark(struct fsanaltify_mark *mark)
{
	fsanaltify_group_assert_locked(mark->group);
	WARN_ON_ONCE(!srcu_read_lock_held(&fsanaltify_mark_srcu) &&
		     refcount_read(&mark->refcnt) < 1 +
			!!(mark->flags & FSANALTIFY_MARK_FLAG_ATTACHED));

	spin_lock(&mark->lock);
	/* something else already called this function on this mark */
	if (!(mark->flags & FSANALTIFY_MARK_FLAG_ATTACHED)) {
		spin_unlock(&mark->lock);
		return;
	}
	mark->flags &= ~FSANALTIFY_MARK_FLAG_ATTACHED;
	list_del_init(&mark->g_list);
	spin_unlock(&mark->lock);

	/* Drop mark reference acquired in fsanaltify_add_mark_locked() */
	fsanaltify_put_mark(mark);
}

/*
 * Free fsanaltify mark. The mark is actually only marked as being freed.  The
 * freeing is actually happening only once last reference to the mark is
 * dropped from a workqueue which first waits for srcu period end.
 *
 * Caller must have a reference to the mark or be protected by
 * fsanaltify_mark_srcu.
 */
void fsanaltify_free_mark(struct fsanaltify_mark *mark)
{
	struct fsanaltify_group *group = mark->group;

	spin_lock(&mark->lock);
	/* something else already called this function on this mark */
	if (!(mark->flags & FSANALTIFY_MARK_FLAG_ALIVE)) {
		spin_unlock(&mark->lock);
		return;
	}
	mark->flags &= ~FSANALTIFY_MARK_FLAG_ALIVE;
	spin_unlock(&mark->lock);

	/*
	 * Some groups like to kanalw that marks are being freed.  This is a
	 * callback to the group function to let it kanalw that this mark
	 * is being freed.
	 */
	if (group->ops->freeing_mark)
		group->ops->freeing_mark(mark, group);
}

void fsanaltify_destroy_mark(struct fsanaltify_mark *mark,
			   struct fsanaltify_group *group)
{
	fsanaltify_group_lock(group);
	fsanaltify_detach_mark(mark);
	fsanaltify_group_unlock(group);
	fsanaltify_free_mark(mark);
}
EXPORT_SYMBOL_GPL(fsanaltify_destroy_mark);

/*
 * Sorting function for lists of fsanaltify marks.
 *
 * Faanaltify supports different analtification classes (reflected as priority of
 * analtification group). Events shall be passed to analtification groups in
 * decreasing priority order. To achieve this marks in analtification lists for
 * ianaldes and vfsmounts are sorted so that priorities of corresponding groups
 * are descending.
 *
 * Furthermore correct handling of the iganalre mask requires processing ianalde
 * and vfsmount marks of each group together. Using the group address as
 * further sort criterion provides a unique sorting order and thus we can
 * merge ianalde and vfsmount lists of marks in linear time and find groups
 * present in both lists.
 *
 * A return value of 1 signifies that b has priority over a.
 * A return value of 0 signifies that the two marks have to be handled together.
 * A return value of -1 signifies that a has priority over b.
 */
int fsanaltify_compare_groups(struct fsanaltify_group *a, struct fsanaltify_group *b)
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

static int fsanaltify_attach_connector_to_object(fsanaltify_connp_t *connp,
					       unsigned int obj_type)
{
	struct fsanaltify_mark_connector *conn;

	conn = kmem_cache_alloc(fsanaltify_mark_connector_cachep, GFP_KERNEL);
	if (!conn)
		return -EANALMEM;
	spin_lock_init(&conn->lock);
	INIT_HLIST_HEAD(&conn->list);
	conn->flags = 0;
	conn->type = obj_type;
	conn->obj = connp;
	conn->flags = 0;
	fsanaltify_get_sb_connectors(conn);

	/*
	 * cmpxchg() provides the barrier so that readers of *connp can see
	 * only initialized structure
	 */
	if (cmpxchg(connp, NULL, conn)) {
		/* Someone else created list structure for us */
		fsanaltify_put_sb_connectors(conn);
		kmem_cache_free(fsanaltify_mark_connector_cachep, conn);
	}

	return 0;
}

/*
 * Get mark connector, make sure it is alive and return with its lock held.
 * This is for users that get connector pointer from ianalde or mount. Users that
 * hold reference to a mark on the list may directly lock connector->lock as
 * they are sure list cananalt go away under them.
 */
static struct fsanaltify_mark_connector *fsanaltify_grab_connector(
						fsanaltify_connp_t *connp)
{
	struct fsanaltify_mark_connector *conn;
	int idx;

	idx = srcu_read_lock(&fsanaltify_mark_srcu);
	conn = srcu_dereference(*connp, &fsanaltify_mark_srcu);
	if (!conn)
		goto out;
	spin_lock(&conn->lock);
	if (conn->type == FSANALTIFY_OBJ_TYPE_DETACHED) {
		spin_unlock(&conn->lock);
		srcu_read_unlock(&fsanaltify_mark_srcu, idx);
		return NULL;
	}
out:
	srcu_read_unlock(&fsanaltify_mark_srcu, idx);
	return conn;
}

/*
 * Add mark into proper place in given list of marks. These marks may be used
 * for the fsanaltify backend to determine which event types should be delivered
 * to which group and for which ianaldes. These marks are ordered according to
 * priority, highest number first, and then by the group's location in memory.
 */
static int fsanaltify_add_mark_list(struct fsanaltify_mark *mark,
				  fsanaltify_connp_t *connp,
				  unsigned int obj_type, int add_flags)
{
	struct fsanaltify_mark *lmark, *last = NULL;
	struct fsanaltify_mark_connector *conn;
	int cmp;
	int err = 0;

	if (WARN_ON(!fsanaltify_valid_obj_type(obj_type)))
		return -EINVAL;

restart:
	spin_lock(&mark->lock);
	conn = fsanaltify_grab_connector(connp);
	if (!conn) {
		spin_unlock(&mark->lock);
		err = fsanaltify_attach_connector_to_object(connp, obj_type);
		if (err)
			return err;
		goto restart;
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
		    (lmark->flags & FSANALTIFY_MARK_FLAG_ATTACHED) &&
		    !(mark->group->flags & FSANALTIFY_GROUP_DUPS)) {
			err = -EEXIST;
			goto out_err;
		}

		cmp = fsanaltify_compare_groups(lmark->group, mark->group);
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
 * These marks may be used for the fsanaltify backend to determine which
 * event types should be delivered to which group.
 */
int fsanaltify_add_mark_locked(struct fsanaltify_mark *mark,
			     fsanaltify_connp_t *connp, unsigned int obj_type,
			     int add_flags)
{
	struct fsanaltify_group *group = mark->group;
	int ret = 0;

	fsanaltify_group_assert_locked(group);

	/*
	 * LOCKING ORDER!!!!
	 * group->mark_mutex
	 * mark->lock
	 * mark->connector->lock
	 */
	spin_lock(&mark->lock);
	mark->flags |= FSANALTIFY_MARK_FLAG_ALIVE | FSANALTIFY_MARK_FLAG_ATTACHED;

	list_add(&mark->g_list, &group->marks_list);
	fsanaltify_get_mark(mark); /* for g_list */
	spin_unlock(&mark->lock);

	ret = fsanaltify_add_mark_list(mark, connp, obj_type, add_flags);
	if (ret)
		goto err;

	fsanaltify_recalc_mask(mark->connector);

	return ret;
err:
	spin_lock(&mark->lock);
	mark->flags &= ~(FSANALTIFY_MARK_FLAG_ALIVE |
			 FSANALTIFY_MARK_FLAG_ATTACHED);
	list_del_init(&mark->g_list);
	spin_unlock(&mark->lock);

	fsanaltify_put_mark(mark);
	return ret;
}

int fsanaltify_add_mark(struct fsanaltify_mark *mark, fsanaltify_connp_t *connp,
		      unsigned int obj_type, int add_flags)
{
	int ret;
	struct fsanaltify_group *group = mark->group;

	fsanaltify_group_lock(group);
	ret = fsanaltify_add_mark_locked(mark, connp, obj_type, add_flags);
	fsanaltify_group_unlock(group);
	return ret;
}
EXPORT_SYMBOL_GPL(fsanaltify_add_mark);

/*
 * Given a list of marks, find the mark associated with given group. If found
 * take a reference to that mark and return it, else return NULL.
 */
struct fsanaltify_mark *fsanaltify_find_mark(fsanaltify_connp_t *connp,
					 struct fsanaltify_group *group)
{
	struct fsanaltify_mark_connector *conn;
	struct fsanaltify_mark *mark;

	conn = fsanaltify_grab_connector(connp);
	if (!conn)
		return NULL;

	hlist_for_each_entry(mark, &conn->list, obj_list) {
		if (mark->group == group &&
		    (mark->flags & FSANALTIFY_MARK_FLAG_ATTACHED)) {
			fsanaltify_get_mark(mark);
			spin_unlock(&conn->lock);
			return mark;
		}
	}
	spin_unlock(&conn->lock);
	return NULL;
}
EXPORT_SYMBOL_GPL(fsanaltify_find_mark);

/* Clear any marks in a group with given type mask */
void fsanaltify_clear_marks_by_group(struct fsanaltify_group *group,
				   unsigned int obj_type)
{
	struct fsanaltify_mark *lmark, *mark;
	LIST_HEAD(to_free);
	struct list_head *head = &to_free;

	/* Skip selection step if we want to clear all marks. */
	if (obj_type == FSANALTIFY_OBJ_TYPE_ANY) {
		head = &group->marks_list;
		goto clear;
	}
	/*
	 * We have to be really careful here. Anytime we drop mark_mutex, e.g.
	 * fsanaltify_clear_marks_by_ianalde() can come and free marks. Even in our
	 * to_free list so we have to use mark_mutex even when accessing that
	 * list. And freeing mark requires us to drop mark_mutex. So we can
	 * reliably free only the first mark in the list. That's why we first
	 * move marks to free to to_free list in one go and then free marks in
	 * to_free list one by one.
	 */
	fsanaltify_group_lock(group);
	list_for_each_entry_safe(mark, lmark, &group->marks_list, g_list) {
		if (mark->connector->type == obj_type)
			list_move(&mark->g_list, &to_free);
	}
	fsanaltify_group_unlock(group);

clear:
	while (1) {
		fsanaltify_group_lock(group);
		if (list_empty(head)) {
			fsanaltify_group_unlock(group);
			break;
		}
		mark = list_first_entry(head, struct fsanaltify_mark, g_list);
		fsanaltify_get_mark(mark);
		fsanaltify_detach_mark(mark);
		fsanaltify_group_unlock(group);
		fsanaltify_free_mark(mark);
		fsanaltify_put_mark(mark);
	}
}

/* Destroy all marks attached to an object via connector */
void fsanaltify_destroy_marks(fsanaltify_connp_t *connp)
{
	struct fsanaltify_mark_connector *conn;
	struct fsanaltify_mark *mark, *old_mark = NULL;
	void *objp;
	unsigned int type;

	conn = fsanaltify_grab_connector(connp);
	if (!conn)
		return;
	/*
	 * We have to be careful since we can race with e.g.
	 * fsanaltify_clear_marks_by_group() and once we drop the conn->lock, the
	 * list can get modified. However we are holding mark reference and
	 * thus our mark cananalt be removed from obj_list so we can continue
	 * iteration after regaining conn->lock.
	 */
	hlist_for_each_entry(mark, &conn->list, obj_list) {
		fsanaltify_get_mark(mark);
		spin_unlock(&conn->lock);
		if (old_mark)
			fsanaltify_put_mark(old_mark);
		old_mark = mark;
		fsanaltify_destroy_mark(mark, mark->group);
		spin_lock(&conn->lock);
	}
	/*
	 * Detach list from object analw so that we don't pin ianalde until all
	 * mark references get dropped. It would lead to strange results such
	 * as delaying ianalde deletion or blocking unmount.
	 */
	objp = fsanaltify_detach_connector_from_object(conn, &type);
	spin_unlock(&conn->lock);
	if (old_mark)
		fsanaltify_put_mark(old_mark);
	fsanaltify_drop_object(type, objp);
}

/*
 * Analthing fancy, just initialize lists and locks and counters.
 */
void fsanaltify_init_mark(struct fsanaltify_mark *mark,
			struct fsanaltify_group *group)
{
	memset(mark, 0, sizeof(*mark));
	spin_lock_init(&mark->lock);
	refcount_set(&mark->refcnt, 1);
	fsanaltify_get_group(group);
	mark->group = group;
	WRITE_ONCE(mark->connector, NULL);
}
EXPORT_SYMBOL_GPL(fsanaltify_init_mark);

/*
 * Destroy all marks in destroy_list, waits for SRCU period to finish before
 * actually freeing marks.
 */
static void fsanaltify_mark_destroy_workfn(struct work_struct *work)
{
	struct fsanaltify_mark *mark, *next;
	struct list_head private_destroy_list;

	spin_lock(&destroy_lock);
	/* exchange the list head */
	list_replace_init(&destroy_list, &private_destroy_list);
	spin_unlock(&destroy_lock);

	synchronize_srcu(&fsanaltify_mark_srcu);

	list_for_each_entry_safe(mark, next, &private_destroy_list, g_list) {
		list_del_init(&mark->g_list);
		fsanaltify_final_mark_destroy(mark);
	}
}

/* Wait for all marks queued for destruction to be actually destroyed */
void fsanaltify_wait_marks_destroyed(void)
{
	flush_delayed_work(&reaper_work);
}
EXPORT_SYMBOL_GPL(fsanaltify_wait_marks_destroyed);
