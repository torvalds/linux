// SPDX-License-Identifier: GPL-2.0
#include <linux/fanotify.h>
#include <linux/fdtable.h>
#include <linux/fsnotify_backend.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h> /* UINT_MAX */
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/sched/user.h>
#include <linux/sched/signal.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/audit.h>
#include <linux/sched/mm.h>
#include <linux/statfs.h>

#include "fanotify.h"

static bool should_merge(struct fsnotify_event *old_fsn,
			 struct fsnotify_event *new_fsn)
{
	struct fanotify_event *old, *new;

	pr_debug("%s: old=%p new=%p\n", __func__, old_fsn, new_fsn);
	old = FANOTIFY_E(old_fsn);
	new = FANOTIFY_E(new_fsn);

	if (old_fsn->inode != new_fsn->inode || old->pid != new->pid ||
	    old->fh_type != new->fh_type || old->fh_len != new->fh_len)
		return false;

	if (fanotify_event_has_path(old)) {
		return old->path.mnt == new->path.mnt &&
			old->path.dentry == new->path.dentry;
	} else if (fanotify_event_has_fid(old)) {
		/*
		 * We want to merge many dirent events in the same dir (i.e.
		 * creates/unlinks/renames), but we do not want to merge dirent
		 * events referring to subdirs with dirent events referring to
		 * non subdirs, otherwise, user won't be able to tell from a
		 * mask FAN_CREATE|FAN_DELETE|FAN_ONDIR if it describes mkdir+
		 * unlink pair or rmdir+create pair of events.
		 */
		return (old->mask & FS_ISDIR) == (new->mask & FS_ISDIR) &&
			fanotify_fid_equal(&old->fid, &new->fid, old->fh_len);
	}

	/* Do not merge events if we failed to encode fid */
	return false;
}

/* and the list better be locked by something too! */
static int fanotify_merge(struct list_head *list, struct fsnotify_event *event)
{
	struct fsnotify_event *test_event;
	struct fanotify_event *new;

	pr_debug("%s: list=%p event=%p\n", __func__, list, event);
	new = FANOTIFY_E(event);

	/*
	 * Don't merge a permission event with any other event so that we know
	 * the event structure we have created in fanotify_handle_event() is the
	 * one we should check for permission response.
	 */
	if (fanotify_is_perm_event(new->mask))
		return 0;

	list_for_each_entry_reverse(test_event, list, list) {
		if (should_merge(test_event, event)) {
			FANOTIFY_E(test_event)->mask |= new->mask;
			return 1;
		}
	}

	return 0;
}

/*
 * Wait for response to permission event. The function also takes care of
 * freeing the permission event (or offloads that in case the wait is canceled
 * by a signal). The function returns 0 in case access got allowed by userspace,
 * -EPERM in case userspace disallowed the access, and -ERESTARTSYS in case
 * the wait got interrupted by a signal.
 */
static int fanotify_get_response(struct fsnotify_group *group,
				 struct fanotify_perm_event *event,
				 struct fsnotify_iter_info *iter_info)
{
	int ret;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	ret = wait_event_killable(group->fanotify_data.access_waitq,
				  event->state == FAN_EVENT_ANSWERED);
	/* Signal pending? */
	if (ret < 0) {
		spin_lock(&group->notification_lock);
		/* Event reported to userspace and no answer yet? */
		if (event->state == FAN_EVENT_REPORTED) {
			/* Event will get freed once userspace answers to it */
			event->state = FAN_EVENT_CANCELED;
			spin_unlock(&group->notification_lock);
			return ret;
		}
		/* Event not yet reported? Just remove it. */
		if (event->state == FAN_EVENT_INIT)
			fsnotify_remove_queued_event(group, &event->fae.fse);
		/*
		 * Event may be also answered in case signal delivery raced
		 * with wakeup. In that case we have nothing to do besides
		 * freeing the event and reporting error.
		 */
		spin_unlock(&group->notification_lock);
		goto out;
	}

	/* userspace responded, convert to something usable */
	switch (event->response & ~FAN_AUDIT) {
	case FAN_ALLOW:
		ret = 0;
		break;
	case FAN_DENY:
	default:
		ret = -EPERM;
	}

	/* Check if the response should be audited */
	if (event->response & FAN_AUDIT)
		audit_fanotify(event->response & ~FAN_AUDIT);

	pr_debug("%s: group=%p event=%p about to return ret=%d\n", __func__,
		 group, event, ret);
out:
	fsnotify_destroy_event(group, &event->fae.fse);

	return ret;
}

/*
 * This function returns a mask for an event that only contains the flags
 * that have been specifically requested by the user. Flags that may have
 * been included within the event mask, but have not been explicitly
 * requested by the user, will not be present in the returned mask.
 */
static u32 fanotify_group_event_mask(struct fsnotify_group *group,
				     struct fsnotify_iter_info *iter_info,
				     u32 event_mask, const void *data,
				     int data_type)
{
	__u32 marks_mask = 0, marks_ignored_mask = 0;
	__u32 test_mask, user_mask = FANOTIFY_OUTGOING_EVENTS;
	const struct path *path = data;
	struct fsnotify_mark *mark;
	int type;

	pr_debug("%s: report_mask=%x mask=%x data=%p data_type=%d\n",
		 __func__, iter_info->report_mask, event_mask, data, data_type);

	if (!FAN_GROUP_FLAG(group, FAN_REPORT_FID)) {
		/* Do we have path to open a file descriptor? */
		if (data_type != FSNOTIFY_EVENT_PATH)
			return 0;
		/* Path type events are only relevant for files and dirs */
		if (!d_is_reg(path->dentry) && !d_can_lookup(path->dentry))
			return 0;
	}

	fsnotify_foreach_obj_type(type) {
		if (!fsnotify_iter_should_report_type(iter_info, type))
			continue;
		mark = iter_info->marks[type];
		/*
		 * If the event is for a child and this mark doesn't care about
		 * events on a child, don't send it!
		 */
		if (event_mask & FS_EVENT_ON_CHILD &&
		    (type != FSNOTIFY_OBJ_TYPE_INODE ||
		     !(mark->mask & FS_EVENT_ON_CHILD)))
			continue;

		marks_mask |= mark->mask;
		marks_ignored_mask |= mark->ignored_mask;
	}

	test_mask = event_mask & marks_mask & ~marks_ignored_mask;

	/*
	 * dirent modification events (create/delete/move) do not carry the
	 * child entry name/inode information. Instead, we report FAN_ONDIR
	 * for mkdir/rmdir so user can differentiate them from creat/unlink.
	 *
	 * For backward compatibility and consistency, do not report FAN_ONDIR
	 * to user in legacy fanotify mode (reporting fd) and report FAN_ONDIR
	 * to user in FAN_REPORT_FID mode for all event types.
	 */
	if (FAN_GROUP_FLAG(group, FAN_REPORT_FID)) {
		/* Do not report FAN_ONDIR without any event */
		if (!(test_mask & ~FAN_ONDIR))
			return 0;
	} else {
		user_mask &= ~FAN_ONDIR;
	}

	if (event_mask & FS_ISDIR &&
	    !(marks_mask & FS_ISDIR & ~marks_ignored_mask))
		return 0;

	return test_mask & user_mask;
}

static int fanotify_encode_fid(struct fanotify_event *event,
			       struct inode *inode, gfp_t gfp,
			       __kernel_fsid_t *fsid)
{
	struct fanotify_fid *fid = &event->fid;
	int dwords, bytes = 0;
	int err, type;

	fid->ext_fh = NULL;
	dwords = 0;
	err = -ENOENT;
	type = exportfs_encode_inode_fh(inode, NULL, &dwords, NULL);
	if (!dwords)
		goto out_err;

	bytes = dwords << 2;
	if (bytes > FANOTIFY_INLINE_FH_LEN) {
		/* Treat failure to allocate fh as failure to allocate event */
		err = -ENOMEM;
		fid->ext_fh = kmalloc(bytes, gfp);
		if (!fid->ext_fh)
			goto out_err;
	}

	type = exportfs_encode_inode_fh(inode, fanotify_fid_fh(fid, bytes),
					&dwords, NULL);
	err = -EINVAL;
	if (!type || type == FILEID_INVALID || bytes != dwords << 2)
		goto out_err;

	fid->fsid = *fsid;
	event->fh_len = bytes;

	return type;

out_err:
	pr_warn_ratelimited("fanotify: failed to encode fid (fsid=%x.%x, "
			    "type=%d, bytes=%d, err=%i)\n",
			    fsid->val[0], fsid->val[1], type, bytes, err);
	kfree(fid->ext_fh);
	fid->ext_fh = NULL;
	event->fh_len = 0;

	return FILEID_INVALID;
}

/*
 * The inode to use as identifier when reporting fid depends on the event.
 * Report the modified directory inode on dirent modification events.
 * Report the "victim" inode otherwise.
 * For example:
 * FS_ATTRIB reports the child inode even if reported on a watched parent.
 * FS_CREATE reports the modified dir inode and not the created inode.
 */
static struct inode *fanotify_fid_inode(struct inode *to_tell, u32 event_mask,
					const void *data, int data_type)
{
	if (event_mask & ALL_FSNOTIFY_DIRENT_EVENTS)
		return to_tell;
	else if (data_type == FSNOTIFY_EVENT_INODE)
		return (struct inode *)data;
	else if (data_type == FSNOTIFY_EVENT_PATH)
		return d_inode(((struct path *)data)->dentry);
	return NULL;
}

struct fanotify_event *fanotify_alloc_event(struct fsnotify_group *group,
					    struct inode *inode, u32 mask,
					    const void *data, int data_type,
					    __kernel_fsid_t *fsid)
{
	struct fanotify_event *event = NULL;
	gfp_t gfp = GFP_KERNEL_ACCOUNT;
	struct inode *id = fanotify_fid_inode(inode, mask, data, data_type);

	/*
	 * For queues with unlimited length lost events are not expected and
	 * can possibly have security implications. Avoid losing events when
	 * memory is short.
	 */
	if (group->max_events == UINT_MAX)
		gfp |= __GFP_NOFAIL;

	/* Whoever is interested in the event, pays for the allocation. */
	memalloc_use_memcg(group->memcg);

	if (fanotify_is_perm_event(mask)) {
		struct fanotify_perm_event *pevent;

		pevent = kmem_cache_alloc(fanotify_perm_event_cachep, gfp);
		if (!pevent)
			goto out;
		event = &pevent->fae;
		pevent->response = 0;
		pevent->state = FAN_EVENT_INIT;
		goto init;
	}
	event = kmem_cache_alloc(fanotify_event_cachep, gfp);
	if (!event)
		goto out;
init: __maybe_unused
	fsnotify_init_event(&event->fse, inode);
	event->mask = mask;
	if (FAN_GROUP_FLAG(group, FAN_REPORT_TID))
		event->pid = get_pid(task_pid(current));
	else
		event->pid = get_pid(task_tgid(current));
	event->fh_len = 0;
	if (id && FAN_GROUP_FLAG(group, FAN_REPORT_FID)) {
		/* Report the event without a file identifier on encode error */
		event->fh_type = fanotify_encode_fid(event, id, gfp, fsid);
	} else if (data_type == FSNOTIFY_EVENT_PATH) {
		event->fh_type = FILEID_ROOT;
		event->path = *((struct path *)data);
		path_get(&event->path);
	} else {
		event->fh_type = FILEID_INVALID;
		event->path.mnt = NULL;
		event->path.dentry = NULL;
	}
out:
	memalloc_unuse_memcg();
	return event;
}

/*
 * Get cached fsid of the filesystem containing the object from any connector.
 * All connectors are supposed to have the same fsid, but we do not verify that
 * here.
 */
static __kernel_fsid_t fanotify_get_fsid(struct fsnotify_iter_info *iter_info)
{
	int type;
	__kernel_fsid_t fsid = {};

	fsnotify_foreach_obj_type(type) {
		struct fsnotify_mark_connector *conn;

		if (!fsnotify_iter_should_report_type(iter_info, type))
			continue;

		conn = READ_ONCE(iter_info->marks[type]->connector);
		/* Mark is just getting destroyed or created? */
		if (!conn)
			continue;
		if (!(conn->flags & FSNOTIFY_CONN_FLAG_HAS_FSID))
			continue;
		/* Pairs with smp_wmb() in fsnotify_add_mark_list() */
		smp_rmb();
		fsid = conn->fsid;
		if (WARN_ON_ONCE(!fsid.val[0] && !fsid.val[1]))
			continue;
		return fsid;
	}

	return fsid;
}

static int fanotify_handle_event(struct fsnotify_group *group,
				 struct inode *inode,
				 u32 mask, const void *data, int data_type,
				 const struct qstr *file_name, u32 cookie,
				 struct fsnotify_iter_info *iter_info)
{
	int ret = 0;
	struct fanotify_event *event;
	struct fsnotify_event *fsn_event;
	__kernel_fsid_t fsid = {};

	BUILD_BUG_ON(FAN_ACCESS != FS_ACCESS);
	BUILD_BUG_ON(FAN_MODIFY != FS_MODIFY);
	BUILD_BUG_ON(FAN_ATTRIB != FS_ATTRIB);
	BUILD_BUG_ON(FAN_CLOSE_NOWRITE != FS_CLOSE_NOWRITE);
	BUILD_BUG_ON(FAN_CLOSE_WRITE != FS_CLOSE_WRITE);
	BUILD_BUG_ON(FAN_OPEN != FS_OPEN);
	BUILD_BUG_ON(FAN_MOVED_TO != FS_MOVED_TO);
	BUILD_BUG_ON(FAN_MOVED_FROM != FS_MOVED_FROM);
	BUILD_BUG_ON(FAN_CREATE != FS_CREATE);
	BUILD_BUG_ON(FAN_DELETE != FS_DELETE);
	BUILD_BUG_ON(FAN_DELETE_SELF != FS_DELETE_SELF);
	BUILD_BUG_ON(FAN_MOVE_SELF != FS_MOVE_SELF);
	BUILD_BUG_ON(FAN_EVENT_ON_CHILD != FS_EVENT_ON_CHILD);
	BUILD_BUG_ON(FAN_Q_OVERFLOW != FS_Q_OVERFLOW);
	BUILD_BUG_ON(FAN_OPEN_PERM != FS_OPEN_PERM);
	BUILD_BUG_ON(FAN_ACCESS_PERM != FS_ACCESS_PERM);
	BUILD_BUG_ON(FAN_ONDIR != FS_ISDIR);
	BUILD_BUG_ON(FAN_OPEN_EXEC != FS_OPEN_EXEC);
	BUILD_BUG_ON(FAN_OPEN_EXEC_PERM != FS_OPEN_EXEC_PERM);

	BUILD_BUG_ON(HWEIGHT32(ALL_FANOTIFY_EVENT_BITS) != 19);

	mask = fanotify_group_event_mask(group, iter_info, mask, data,
					 data_type);
	if (!mask)
		return 0;

	pr_debug("%s: group=%p inode=%p mask=%x\n", __func__, group, inode,
		 mask);

	if (fanotify_is_perm_event(mask)) {
		/*
		 * fsnotify_prepare_user_wait() fails if we race with mark
		 * deletion.  Just let the operation pass in that case.
		 */
		if (!fsnotify_prepare_user_wait(iter_info))
			return 0;
	}

	if (FAN_GROUP_FLAG(group, FAN_REPORT_FID)) {
		fsid = fanotify_get_fsid(iter_info);
		/* Racing with mark destruction or creation? */
		if (!fsid.val[0] && !fsid.val[1])
			return 0;
	}

	event = fanotify_alloc_event(group, inode, mask, data, data_type,
				     &fsid);
	ret = -ENOMEM;
	if (unlikely(!event)) {
		/*
		 * We don't queue overflow events for permission events as
		 * there the access is denied and so no event is in fact lost.
		 */
		if (!fanotify_is_perm_event(mask))
			fsnotify_queue_overflow(group);
		goto finish;
	}

	fsn_event = &event->fse;
	ret = fsnotify_add_event(group, fsn_event, fanotify_merge);
	if (ret) {
		/* Permission events shouldn't be merged */
		BUG_ON(ret == 1 && mask & FANOTIFY_PERM_EVENTS);
		/* Our event wasn't used in the end. Free it. */
		fsnotify_destroy_event(group, fsn_event);

		ret = 0;
	} else if (fanotify_is_perm_event(mask)) {
		ret = fanotify_get_response(group, FANOTIFY_PE(fsn_event),
					    iter_info);
	}
finish:
	if (fanotify_is_perm_event(mask))
		fsnotify_finish_user_wait(iter_info);

	return ret;
}

static void fanotify_free_group_priv(struct fsnotify_group *group)
{
	struct user_struct *user;

	user = group->fanotify_data.user;
	atomic_dec(&user->fanotify_listeners);
	free_uid(user);
}

static void fanotify_free_event(struct fsnotify_event *fsn_event)
{
	struct fanotify_event *event;

	event = FANOTIFY_E(fsn_event);
	if (fanotify_event_has_path(event))
		path_put(&event->path);
	else if (fanotify_event_has_ext_fh(event))
		kfree(event->fid.ext_fh);
	put_pid(event->pid);
	if (fanotify_is_perm_event(event->mask)) {
		kmem_cache_free(fanotify_perm_event_cachep,
				FANOTIFY_PE(fsn_event));
		return;
	}
	kmem_cache_free(fanotify_event_cachep, event);
}

static void fanotify_free_mark(struct fsnotify_mark *fsn_mark)
{
	kmem_cache_free(fanotify_mark_cache, fsn_mark);
}

const struct fsnotify_ops fanotify_fsnotify_ops = {
	.handle_event = fanotify_handle_event,
	.free_group_priv = fanotify_free_group_priv,
	.free_event = fanotify_free_event,
	.free_mark = fanotify_free_mark,
};
