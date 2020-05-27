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

static bool fanotify_path_equal(struct path *p1, struct path *p2)
{
	return p1->mnt == p2->mnt && p1->dentry == p2->dentry;
}

static inline bool fanotify_fsid_equal(__kernel_fsid_t *fsid1,
				       __kernel_fsid_t *fsid2)
{
	return fsid1->val[0] == fsid2->val[0] && fsid1->val[1] == fsid2->val[1];
}

static bool fanotify_fh_equal(struct fanotify_fh *fh1,
			      struct fanotify_fh *fh2)
{
	if (fh1->type != fh2->type || fh1->len != fh2->len)
		return false;

	/* Do not merge events if we failed to encode fh */
	if (fh1->type == FILEID_INVALID)
		return false;

	return !fh1->len ||
		!memcmp(fanotify_fh_buf(fh1), fanotify_fh_buf(fh2), fh1->len);
}

static bool fanotify_fid_event_equal(struct fanotify_fid_event *ffe1,
				     struct fanotify_fid_event *ffe2)
{
	/* Do not merge fid events without object fh */
	if (!ffe1->object_fh.len)
		return false;

	return fanotify_fsid_equal(&ffe1->fsid, &ffe2->fsid) &&
		fanotify_fh_equal(&ffe1->object_fh, &ffe2->object_fh);
}

static bool fanotify_name_event_equal(struct fanotify_name_event *fne1,
				      struct fanotify_name_event *fne2)
{
	/*
	 * Do not merge name events without dir fh.
	 * FAN_DIR_MODIFY does not encode object fh, so it may be empty.
	 */
	if (!fne1->dir_fh.len)
		return false;

	if (fne1->name_len != fne2->name_len ||
	    !fanotify_fh_equal(&fne1->dir_fh, &fne2->dir_fh))
		return false;

	return !memcmp(fne1->name, fne2->name, fne1->name_len);
}

static bool should_merge(struct fsnotify_event *old_fsn,
			 struct fsnotify_event *new_fsn)
{
	struct fanotify_event *old, *new;

	pr_debug("%s: old=%p new=%p\n", __func__, old_fsn, new_fsn);
	old = FANOTIFY_E(old_fsn);
	new = FANOTIFY_E(new_fsn);

	if (old_fsn->objectid != new_fsn->objectid ||
	    old->type != new->type || old->pid != new->pid)
		return false;

	switch (old->type) {
	case FANOTIFY_EVENT_TYPE_PATH:
		return fanotify_path_equal(fanotify_event_path(old),
					   fanotify_event_path(new));
	case FANOTIFY_EVENT_TYPE_FID:
		/*
		 * We want to merge many dirent events in the same dir (i.e.
		 * creates/unlinks/renames), but we do not want to merge dirent
		 * events referring to subdirs with dirent events referring to
		 * non subdirs, otherwise, user won't be able to tell from a
		 * mask FAN_CREATE|FAN_DELETE|FAN_ONDIR if it describes mkdir+
		 * unlink pair or rmdir+create pair of events.
		 */
		if ((old->mask & FS_ISDIR) != (new->mask & FS_ISDIR))
			return false;

		return fanotify_fid_event_equal(FANOTIFY_FE(old),
						FANOTIFY_FE(new));
	case FANOTIFY_EVENT_TYPE_FID_NAME:
		return fanotify_name_event_equal(FANOTIFY_NE(old),
						 FANOTIFY_NE(new));
	default:
		WARN_ON_ONCE(1);
	}

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
	const struct path *path = fsnotify_data_path(data, data_type);
	struct fsnotify_mark *mark;
	int type;

	pr_debug("%s: report_mask=%x mask=%x data=%p data_type=%d\n",
		 __func__, iter_info->report_mask, event_mask, data, data_type);

	if (!FAN_GROUP_FLAG(group, FAN_REPORT_FID)) {
		/* Do we have path to open a file descriptor? */
		if (!path)
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
		 * If the event is on dir and this mark doesn't care about
		 * events on dir, don't send it!
		 */
		if (event_mask & FS_ISDIR && !(mark->mask & FS_ISDIR))
			continue;

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
	 * For dirent modification events (create/delete/move) that do not carry
	 * the child entry name information, we report FAN_ONDIR for mkdir/rmdir
	 * so user can differentiate them from creat/unlink.
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

	return test_mask & user_mask;
}

static void fanotify_encode_fh(struct fanotify_fh *fh, struct inode *inode,
			       gfp_t gfp)
{
	int dwords, type, bytes = 0;
	char *ext_buf = NULL;
	void *buf = fh->buf;
	int err;

	if (!inode)
		goto out;

	dwords = 0;
	err = -ENOENT;
	type = exportfs_encode_inode_fh(inode, NULL, &dwords, NULL);
	if (!dwords)
		goto out_err;

	bytes = dwords << 2;
	if (bytes > FANOTIFY_INLINE_FH_LEN) {
		/* Treat failure to allocate fh as failure to allocate event */
		err = -ENOMEM;
		ext_buf = kmalloc(bytes, gfp);
		if (!ext_buf)
			goto out_err;

		*fanotify_fh_ext_buf_ptr(fh) = ext_buf;
		buf = ext_buf;
	}

	type = exportfs_encode_inode_fh(inode, buf, &dwords, NULL);
	err = -EINVAL;
	if (!type || type == FILEID_INVALID || bytes != dwords << 2)
		goto out_err;

	fh->type = type;
	fh->len = bytes;

	return;

out_err:
	pr_warn_ratelimited("fanotify: failed to encode fid (type=%d, len=%d, err=%i)\n",
			    type, bytes, err);
	kfree(ext_buf);
	*fanotify_fh_ext_buf_ptr(fh) = NULL;
out:
	/* Report the event without a file identifier on encode error */
	fh->type = FILEID_INVALID;
	fh->len = 0;
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

	return (struct inode *)fsnotify_data_inode(data, data_type);
}

struct fanotify_event *fanotify_alloc_event(struct fsnotify_group *group,
					    struct inode *inode, u32 mask,
					    const void *data, int data_type,
					    const struct qstr *file_name,
					    __kernel_fsid_t *fsid)
{
	struct fanotify_event *event = NULL;
	struct fanotify_fid_event *ffe = NULL;
	struct fanotify_name_event *fne = NULL;
	gfp_t gfp = GFP_KERNEL_ACCOUNT;
	struct inode *id = fanotify_fid_inode(inode, mask, data, data_type);
	const struct path *path = fsnotify_data_path(data, data_type);

	/*
	 * For queues with unlimited length lost events are not expected and
	 * can possibly have security implications. Avoid losing events when
	 * memory is short. For the limited size queues, avoid OOM killer in the
	 * target monitoring memcg as it may have security repercussion.
	 */
	if (group->max_events == UINT_MAX)
		gfp |= __GFP_NOFAIL;
	else
		gfp |= __GFP_RETRY_MAYFAIL;

	/* Whoever is interested in the event, pays for the allocation. */
	memalloc_use_memcg(group->memcg);

	if (fanotify_is_perm_event(mask)) {
		struct fanotify_perm_event *pevent;

		pevent = kmem_cache_alloc(fanotify_perm_event_cachep, gfp);
		if (!pevent)
			goto out;

		event = &pevent->fae;
		event->type = FANOTIFY_EVENT_TYPE_PATH_PERM;
		pevent->response = 0;
		pevent->state = FAN_EVENT_INIT;
		goto init;
	}

	/*
	 * For FAN_DIR_MODIFY event, we report the fid of the directory and
	 * the name of the modified entry.
	 * Allocate an fanotify_name_event struct and copy the name.
	 */
	if (mask & FAN_DIR_MODIFY && !(WARN_ON_ONCE(!file_name))) {
		fne = kmalloc(sizeof(*fne) + file_name->len + 1, gfp);
		if (!fne)
			goto out;

		event = &fne->fae;
		event->type = FANOTIFY_EVENT_TYPE_FID_NAME;
		fne->name_len = file_name->len;
		strcpy(fne->name, file_name->name);
		goto init;
	}

	if (FAN_GROUP_FLAG(group, FAN_REPORT_FID)) {
		ffe = kmem_cache_alloc(fanotify_fid_event_cachep, gfp);
		if (!ffe)
			goto out;

		event = &ffe->fae;
		event->type = FANOTIFY_EVENT_TYPE_FID;
	} else {
		struct fanotify_path_event *pevent;

		pevent = kmem_cache_alloc(fanotify_path_event_cachep, gfp);
		if (!pevent)
			goto out;

		event = &pevent->fae;
		event->type = FANOTIFY_EVENT_TYPE_PATH;
	}

init:
	/*
	 * Use the victim inode instead of the watching inode as the id for
	 * event queue, so event reported on parent is merged with event
	 * reported on child when both directory and child watches exist.
	 */
	fsnotify_init_event(&event->fse, (unsigned long)id);
	event->mask = mask;
	if (FAN_GROUP_FLAG(group, FAN_REPORT_TID))
		event->pid = get_pid(task_pid(current));
	else
		event->pid = get_pid(task_tgid(current));

	if (fsid && fanotify_event_fsid(event))
		*fanotify_event_fsid(event) = *fsid;

	if (fanotify_event_object_fh(event))
		fanotify_encode_fh(fanotify_event_object_fh(event), id, gfp);

	if (fanotify_event_dir_fh(event))
		fanotify_encode_fh(fanotify_event_dir_fh(event), id, gfp);

	if (fanotify_event_has_path(event)) {
		struct path *p = fanotify_event_path(event);

		if (path) {
			*p = *path;
			path_get(path);
		} else {
			p->mnt = NULL;
			p->dentry = NULL;
		}
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
	BUILD_BUG_ON(FAN_DIR_MODIFY != FS_DIR_MODIFY);
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
				     file_name, &fsid);
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
		ret = fanotify_get_response(group, FANOTIFY_PERM(event),
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

static void fanotify_free_path_event(struct fanotify_event *event)
{
	path_put(fanotify_event_path(event));
	kmem_cache_free(fanotify_path_event_cachep, FANOTIFY_PE(event));
}

static void fanotify_free_perm_event(struct fanotify_event *event)
{
	path_put(fanotify_event_path(event));
	kmem_cache_free(fanotify_perm_event_cachep, FANOTIFY_PERM(event));
}

static void fanotify_free_fid_event(struct fanotify_event *event)
{
	struct fanotify_fid_event *ffe = FANOTIFY_FE(event);

	if (fanotify_fh_has_ext_buf(&ffe->object_fh))
		kfree(fanotify_fh_ext_buf(&ffe->object_fh));
	kmem_cache_free(fanotify_fid_event_cachep, ffe);
}

static void fanotify_free_name_event(struct fanotify_event *event)
{
	struct fanotify_name_event *fne = FANOTIFY_NE(event);

	if (fanotify_fh_has_ext_buf(&fne->dir_fh))
		kfree(fanotify_fh_ext_buf(&fne->dir_fh));
	kfree(fne);
}

static void fanotify_free_event(struct fsnotify_event *fsn_event)
{
	struct fanotify_event *event;

	event = FANOTIFY_E(fsn_event);
	put_pid(event->pid);
	switch (event->type) {
	case FANOTIFY_EVENT_TYPE_PATH:
		fanotify_free_path_event(event);
		break;
	case FANOTIFY_EVENT_TYPE_PATH_PERM:
		fanotify_free_perm_event(event);
		break;
	case FANOTIFY_EVENT_TYPE_FID:
		fanotify_free_fid_event(event);
		break;
	case FANOTIFY_EVENT_TYPE_FID_NAME:
		fanotify_free_name_event(event);
		break;
	default:
		WARN_ON_ONCE(1);
	}
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
